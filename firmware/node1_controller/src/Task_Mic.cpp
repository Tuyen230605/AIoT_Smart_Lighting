/**
 * Task_Mic.cpp — Lớp phản xạ: nhận dạng từ khoá trên chính vi điều khiển.
 *
 * BA THAY ĐỔI SO VỚI BẢN DACN
 *
 * G1.1 — không còn ghi vào biến toàn cục dùng chung giữa hai nhân. Task này
 *        chạy trên Core 0, TaskLED chạy trên Core 1; mọi lệnh đi qua hàng đợi.
 *
 * G1.2 — suy luận LIÊN TỤC thay vì từng cửa sổ 1 giây rời rạc.
 *        Bản cũ ngừng nạp mẫu trong lúc AI chạy (`buf_ready == 1`), nên âm
 *        thanh phát ra đúng lúc đó bị vứt: từ khoá rơi vào ranh giới cửa sổ
 *        không bao giờ được nghe thấy. Bản này dùng hai bộ đệm luân phiên —
 *        micro ghi vào bộ đệm A trong khi bộ phân loại đọc bộ đệm B — và gọi
 *        run_classifier_continuous() bốn lần mỗi giây trên các lát 250 ms.
 *        Mô hình vốn đã khai báo SLICES_PER_MODEL_WINDOW = 4 nhưng mã cũ
 *        chưa hề dùng tới.
 *
 * G1.3 — bỏ hẳn noise gate phi tuyến (cắt mẫu < 40 về 0 rồi nhân phần còn lại
 *        ×8). Đó là một hàm gián đoạn: nó sinh hài bậc cao tại mỗi điểm cắt và
 *        làm clip int16 khi nói to, khiến phổ MFCC không còn giống dữ liệu đã
 *        huấn luyện trên Edge Impulse — tức là bộ lọc đang làm hại chính mô
 *        hình mà nó định giúp. Bộ lọc thông cao IIR được giữ lại vì nó đúng và
 *        cần thiết (khử lệch DC của INMP441). Việc chống ồn chuyển sang khâu
 *        tăng cường dữ liệu huấn luyện: cộng nhiễu nền thu tại chính căn phòng.
 */
#include <Arduino.h>
#include <Oi_Voice_Assistant_inferencing.h>

#include "driver/i2s.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "Config.h"
#include "oi_cmdbus.h"

// Số mẫu của một lát cắt. Mô hình khai báo 4 lát cho mỗi cửa sổ 1 giây.
#ifndef EI_CLASSIFIER_SLICE_SIZE
#define EI_CLASSIFIER_SLICE_SIZE \
    (EI_CLASSIFIER_RAW_SAMPLE_COUNT / EI_CLASSIFIER_SLICES_PER_MODEL_WINDOW)
#endif

static bool microphone_inference_start(uint32_t n_samples);
static bool microphone_inference_record(void);
static int  microphone_audio_signal_get_data(size_t offset, size_t length, float *out_ptr);
static int  i2s_init(uint32_t sampling_rate);

// Hai bộ đệm luân phiên: đây chính là thứ khiến không còn đoạn âm thanh nào
// bị vứt trong lúc bộ phân loại đang chạy (G1.2).
typedef struct {
    int16_t *buffers[2];
    uint8_t  buf_select;
    uint8_t  buf_ready;
    uint32_t buf_count;
    uint32_t n_samples;
} inference_t;

static inference_t   inference;
static const uint32_t sample_buffer_size = 2048;
static signed short   sampleBuffer[sample_buffer_size];
static volatile bool  record_status = true;

enum VoiceState { STATE_IDLE, STATE_LISTENING };

// ══════════════════════════════════════════════════════════════
//  Dựng lệnh gửi sang TaskLED
// ══════════════════════════════════════════════════════════════
static void postSolidWhite(void) {
    OiLightState s = oiStateBoot();
    s.mode       = OI_MODE_SOLID;
    s.brightness = 100;
    s.cct        = 4000;
    s.rgb        = 0xFFFFFF;
    s.src        = OI_SRC_VOICE_LOCAL;
    oiCmdPostState(s);
}

static void postOff(void) {
    OiLightState s = oiStateBoot();
    s.mode = OI_MODE_OFF;
    s.src  = OI_SRC_VOICE_LOCAL;
    oiCmdPostState(s);
}

static void postMusic(void) {
    OiLightState s = oiStateBoot();
    s.mode       = OI_MODE_MUSIC;
    s.scene      = OI_SCENE_MUSIC_EDM;
    s.brightness = 150;
    s.src        = OI_SRC_VOICE_LOCAL;
    oiCmdPostState(s);
}

void TaskMic(void *pvParameters) {
    Serial.println("🎤 TaskMic: lớp phản xạ (suy luận liên tục 250 ms/lát)...");

    pinMode(PIN_LED_INDICATOR, OUTPUT);
    digitalWrite(PIN_LED_INDICATOR, LOW);

    if (!microphone_inference_start(EI_CLASSIFIER_SLICE_SIZE)) {
        Serial.println("❌ Không khởi động được micro cho AI.");
        vTaskDelete(NULL);
    }

    run_classifier_init();

    VoiceState    voice_state         = STATE_IDLE;
    unsigned long listening_start     = 0;
    uint16_t      silent_slices       = 0;
    bool          window_primed       = false;

    for (;;) {
        // Chờ một lát 250 ms. Micro vẫn tiếp tục ghi vào bộ đệm còn lại.
        if (!microphone_inference_record()) {
            Serial.println("⚠ Tràn bộ đệm âm thanh — bộ phân loại chạy không kịp.");
            continue;
        }

        // ── VAD năng lượng: chặn suy luận khi phòng im lặng ──
        // Không phải AI, và báo cáo không được gọi nó là AI. Nó chỉ là một
        // heuristic rẻ đặt trước một mô hình đắt; giá trị của nó là tỉ lệ lần
        // gọi mô hình cắt được, và đó là con số phải đo chứ không phải khẳng định.
        const int16_t *slice = inference.buffers[inference.buf_select ^ 1];
        uint64_t energy = 0;
        for (uint32_t i = 0; i < EI_CLASSIFIER_SLICE_SIZE; i++) energy += abs(slice[i]);
        uint32_t avg_energy = (uint32_t)(energy / EI_CLASSIFIER_SLICE_SIZE);

        if (avg_energy < VAD_ENERGY_THRESHOLD) {
            silent_slices++;
            // Im lặng đủ lâu thì dọn cửa sổ trượt, để câu nói sau không bị ghép
            // với đuôi của câu trước.
            if (window_primed && silent_slices >= VAD_SILENCE_SLICES) {
                run_classifier_init();
                window_primed = false;
            }
            continue;
        }
        silent_slices = 0;
        window_primed = true;

        signal_t signal;
        signal.total_length = EI_CLASSIFIER_SLICE_SIZE;
        signal.get_data     = &microphone_audio_signal_get_data;

        ei_impulse_result_t result = {0};
        if (run_classifier_continuous(&signal, &result, false) != EI_IMPULSE_OK) continue;

        // ── Chọn nhãn điểm cao nhất ──
        const char *best_word = "noise";
        float       max_score = 0.0f;
        for (size_t ix = 0; ix < EI_CLASSIFIER_LABEL_COUNT; ix++) {
            if (result.classification[ix].value > max_score) {
                max_score = result.classification[ix].value;
                best_word = result.classification[ix].label;
            }
        }

        // ── Máy trạng thái hội thoại ──
        // Hai ngưỡng khác nhau: từ khoá thức đặt cao để chống thức nhầm giữa
        // đêm; lệnh sau khi đã thức hạ xuống vì đã có ngữ cảnh (oi_protocol.h).
        if (voice_state == STATE_IDLE) {
            if (max_score >= OI_CONF_WAKE_MIN && strcmp(best_word, "alo_oi") == 0) {
                Serial.println("🔔 Nghe thấy 'Alo Oi' — mở cửa sổ nghe.");
                voice_state     = STATE_LISTENING;
                listening_start = millis();
                digitalWrite(PIN_LED_INDICATOR, HIGH);
            }
        } else {   // STATE_LISTENING
            if (max_score >= OI_CONF_CMD_MIN) {
                if (strcmp(best_word, "batden") == 0) {
                    Serial.println("✅ Lệnh: BẬT ĐÈN");
                    postSolidWhite();
                } else if (strcmp(best_word, "tatden") == 0) {
                    Serial.println("✅ Lệnh: TẮT ĐÈN");
                    postOff();
                } else if (strcmp(best_word, "nhaynhac") == 0) {
                    Serial.println("✅ Lệnh: NHÁY NHẠC");
                    postMusic();
                } else if (strcmp(best_word, "tangsang") == 0) {
                    Serial.println("✅ Lệnh: TĂNG SÁNG");
                    oiCmdPostAdjust(+15, OI_SRC_VOICE_LOCAL);
                } else if (strcmp(best_word, "giamsang") == 0) {
                    Serial.println("✅ Lệnh: GIẢM SÁNG");
                    oiCmdPostAdjust(-15, OI_SRC_VOICE_LOCAL);
                } else if (strcmp(best_word, "alo_oi") == 0) {
                    Serial.println("⏳ Gia hạn cửa sổ nghe.");
                    listening_start = millis();
                }
            }

            if (millis() - listening_start > OI_LISTEN_WINDOW_MS) {
                Serial.println("⏰ Hết cửa sổ nghe.");
                digitalWrite(PIN_LED_INDICATOR, LOW);
                voice_state = STATE_IDLE;
            }
        }
    }
}

// ══════════════════════════════════════════════════════════════
//  Thu âm và tiền xử lý
// ══════════════════════════════════════════════════════════════
static void audio_inference_callback(uint32_t n_bytes) {
    for (uint32_t i = 0; i < n_bytes >> 1; i++) {
        inference.buffers[inference.buf_select][inference.buf_count++] = sampleBuffer[i];

        if (inference.buf_count >= inference.n_samples) {
            // Đổi bộ đệm và báo có lát mới. KHÔNG dừng việc nạp mẫu như bản cũ.
            inference.buf_select ^= 1;
            inference.buf_count   = 0;
            inference.buf_ready   = 1;
        }
    }
}

static void capture_samples(void *arg) {
    const int32_t i2s_bytes_to_read = (uint32_t)arg;
    size_t bytes_read = i2s_bytes_to_read;

    // Trạng thái của bộ lọc thông cao IIR bậc 1 — khử lệch DC của INMP441.
    static float prev_raw   = 0.0f;
    static float prev_clean = 0.0f;

    while (record_status) {
        i2s_read((i2s_port_t)I2S_PORT_NUM, (void *)sampleBuffer,
                 i2s_bytes_to_read, &bytes_read, 100);

        if (bytes_read > 0 && bytes_read >= (size_t)i2s_bytes_to_read) {
            for (int x = 0; x < i2s_bytes_to_read / 2; x++) {
                float raw = (float)sampleBuffer[x];

                // y[n] = x[n] - x[n-1] + 0.995·y[n-1]
                float clean = raw - prev_raw + 0.995f * prev_clean;
                prev_raw    = raw;
                prev_clean  = clean;

                // G1.3: đường tín hiệu dừng ở đây. Không cắt ngưỡng, không nhân
                // hệ số — mọi biến đổi phi tuyến đều làm lệch phổ MFCC so với
                // dữ liệu đã huấn luyện.
                sampleBuffer[x] = (int16_t)constrain(clean, -32768.0f, 32767.0f);
            }
            audio_inference_callback(i2s_bytes_to_read);
        }
    }
    vTaskDelete(NULL);
}

static bool microphone_inference_start(uint32_t n_samples) {
    inference.buffers[0] = (int16_t *)malloc(n_samples * sizeof(int16_t));
    inference.buffers[1] = (int16_t *)malloc(n_samples * sizeof(int16_t));
    if (inference.buffers[0] == NULL || inference.buffers[1] == NULL) {
        free(inference.buffers[0]);
        free(inference.buffers[1]);
        return false;
    }

    inference.buf_select = 0;
    inference.buf_count  = 0;
    inference.buf_ready  = 0;
    inference.n_samples  = n_samples;

    if (i2s_init(EI_CLASSIFIER_FREQUENCY)) {
        ei_printf("Failed to start I2S!\n");
        return false;
    }
    ei_sleep(100);

    record_status = true;
    xTaskCreatePinnedToCore(capture_samples, "capture", STACK_CAPTURE,
                            (void *)sample_buffer_size, PRIO_CAPTURE, NULL, CORE_AI);
    return true;
}

/**
 * Chờ một lát mới. Trả về false nếu lát trước chưa kịp xử lý xong đã bị lát
 * sau đè lên — dấu hiệu bộ phân loại chạy chậm hơn micro, phải biết để còn
 * chỉnh, chứ không được im lặng bỏ qua.
 */
static bool microphone_inference_record(void) {
    bool ok = true;

    if (inference.buf_ready == 1) ok = false;

    while (inference.buf_ready == 0) {
        vTaskDelay(1 / portTICK_PERIOD_MS);
    }
    inference.buf_ready = 0;
    return ok;
}

static int microphone_audio_signal_get_data(size_t offset, size_t length, float *out_ptr) {
    // Đọc bộ đệm KHÔNG đang được micro ghi vào.
    numpy::int16_to_float(&inference.buffers[inference.buf_select ^ 1][offset], out_ptr, length);
    return 0;
}

static int i2s_init(uint32_t sampling_rate) {
    i2s_config_t i2s_config = {
        .mode                 = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_RX),
        .sample_rate          = sampling_rate,
        .bits_per_sample      = (i2s_bits_per_sample_t)16,
        .channel_format       = I2S_CHANNEL_FMT_ONLY_LEFT,
        .communication_format = I2S_COMM_FORMAT_STAND_I2S,
        .intr_alloc_flags     = 0,
        .dma_buf_count        = 8,
        .dma_buf_len          = 512,
        .use_apll             = false,
        .tx_desc_auto_clear   = false,
        .fixed_mclk           = -1,
    };
    i2s_pin_config_t pin_config = {
        .bck_io_num   = PIN_I2S_SCK,
        .ws_io_num    = PIN_I2S_WS,
        .data_out_num = I2S_PIN_NO_CHANGE,
        .data_in_num  = PIN_I2S_SD,
    };

    if (i2s_driver_install((i2s_port_t)I2S_PORT_NUM, &i2s_config, 0, NULL) != ESP_OK) return -1;
    if (i2s_set_pin((i2s_port_t)I2S_PORT_NUM, &pin_config) != ESP_OK) return -1;
    return (int)i2s_zero_dma_buffer((i2s_port_t)I2S_PORT_NUM);
}
