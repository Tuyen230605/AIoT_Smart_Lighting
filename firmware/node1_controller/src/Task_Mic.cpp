#include <Arduino.h>
#include "Config.h"
#include <Oi_Voice_Assistant_inferencing.h> // NHỚ SỬA TÊN THƯ VIỆN CỦA TUYỀN
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/i2s.h"

// ==========================================================
// 1. GỌI CÁC BIẾN CẦU NỐI TỪ TASK LED SANG ĐỂ ĐIỀU KHIỂN
// ==========================================================
extern String cmdAction;
extern String cmdColor;
extern int cmdBrightness;

#define PIN_LED_INDICATOR 3 

// --- KHỐI KHAI BÁO HÀM AI CỦA EDGE IMPULSE ---
static bool microphone_inference_start(uint32_t n_samples);
static bool microphone_inference_record(void);
static int microphone_audio_signal_get_data(size_t offset, size_t length, float *out_ptr);
static void microphone_inference_end(void);
static int i2s_init(uint32_t sampling_rate);
static int i2s_deinit(void);

typedef struct {
    int16_t *buffer;
    uint8_t buf_ready;
    uint32_t buf_count;
    uint32_t n_samples;
} inference_t;

static inference_t inference;
static const uint32_t sample_buffer_size = 2048;
static signed short sampleBuffer[sample_buffer_size];
static bool record_status = true;

enum VoiceState { STATE_IDLE, STATE_LISTENING };

// ==========================================================
// 2. TASK MIC: BỘ NÃO AI VỚI MÁY TRẠNG THÁI (FSM) ĐA LỆNH 10S
// ==========================================================

// void TaskMic(void *pvParameters) {
//     Serial.println("🎙️ Bật chế độ Ghi âm Raw Audio. Chờ Python kết nối...");
    
//     // ==========================================================
//     // 1. CẤU HÌNH VÀ BẬT MICRO I2S TRƯỚC KHI ĐỌC (CỰC KỲ QUAN TRỌNG)
//     // ==========================================================
//     i2s_config_t i2s_config = {
//         .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_RX),
//         .sample_rate = 16000,
//         .bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT,
//         .channel_format = I2S_CHANNEL_FMT_ONLY_LEFT,
//         .communication_format = I2S_COMM_FORMAT_STAND_I2S,
//         .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1,
//         .dma_buf_count = 8,
//         .dma_buf_len = 512,
//         .use_apll = false,
//         .tx_desc_auto_clear = false,
//         .fixed_mclk = 0
//     };

//     i2s_pin_config_t pin_config = {
//         .bck_io_num = PIN_I2S_SCK,
//         .ws_io_num = PIN_I2S_WS,
//         .data_out_num = I2S_PIN_NO_CHANGE,
//         .data_in_num = PIN_I2S_SD
//     };

//     // Khởi động Driver I2S ở cổng 0
//     i2s_driver_install(I2S_NUM_0, &i2s_config, 0, NULL);
//     i2s_set_pin(I2S_NUM_0, &pin_config);
//     i2s_start(I2S_NUM_0);

//     // ==========================================================
//     // 2. VÒNG LẶP ĐẨY DỮ LIỆU THÔ LÊN PYTHON
//     // ==========================================================
//     int16_t sampleBuffer[512]; 
//     size_t bytesRead;

//     for (;;) {
//         // Đọc dữ liệu thô từ I2S
//         i2s_read(I2S_NUM_0, &sampleBuffer, sizeof(sampleBuffer), &bytesRead, portMAX_DELAY);
        
//         if (bytesRead > 0) {
//             // Khuếch đại âm lượng lên 8 lần (để âm thanh không bị lí nhí)
//             for (int x = 0; x < bytesRead / 2; x++) {
//                 sampleBuffer[x] = (int16_t)(sampleBuffer[x]) * 8;
//             }

//             // Đẩy thẳng dữ liệu byte lên Serial
//             Serial.write((const uint8_t*)sampleBuffer, bytesRead);
//         }
//     }
// }

void TaskMic(void *pvParameters) {
    Serial.println("🎤 Khởi động Bộ não AI (Tích hợp DSP: VAD, Noise Gate, DC Filter)...");

    pinMode(PIN_LED_INDICATOR, OUTPUT);
    digitalWrite(PIN_LED_INDICATOR, LOW);

    if (!microphone_inference_start(EI_CLASSIFIER_RAW_SAMPLE_COUNT)) {
        Serial.println("ERR: Lỗi khởi động Micro cho AI!");
        vTaskDelete(NULL);
    }

    VoiceState current_voice_state = STATE_IDLE;
    unsigned long listening_start_time = 0;
    const unsigned long LISTENING_TIMEOUT = 10000; 
    static String previousAction = "turn_off";

    for (;;) {
        // 1. Chờ Micro thu đủ 1 khung âm thanh (1 giây)
        bool m = microphone_inference_record();
        if (!m) continue;

        // ----------------------------------------------------------------
        // 🚀 KỸ THUẬT 3: VAD (Voice Activity Detection) - ĐO NĂNG LƯỢNG
        // ----------------------------------------------------------------
        unsigned long total_energy = 0;
        for (int i = 0; i < EI_CLASSIFIER_RAW_SAMPLE_COUNT; i++) {
            // Lấy trị tuyệt đối biên độ để tính tổng năng lượng
            total_energy += abs(inference.buffer[i]); 
        }
        unsigned long avg_energy = total_energy / EI_CLASSIFIER_RAW_SAMPLE_COUNT;

        // Nếu năng lượng trung bình < 100 (tức là phòng đang im lặng), 
        // BỎ QUA LUÔN hàm AI để tiết kiệm 100% CPU và tránh đoán bừa.
        if (avg_energy < 50) { 
            // Môi trường im lặng, dọn xô để hứng khung mới rồi bỏ qua AI
            inference.buf_count = 0;
            inference.buf_ready = 0;
            continue; 
        }

        // 2. Chuyển dữ liệu vào dạng Tín hiệu cho AI hiểu
        signal_t signal;
        signal.total_length = EI_CLASSIFIER_RAW_SAMPLE_COUNT;
        signal.get_data = &microphone_audio_signal_get_data;
        ei_impulse_result_t result = { 0 };

        // 3. AI CHẠY PHÂN TÍCH (Chỉ chạy khi có người đang thực sự nói do VAD đánh thức)
        EI_IMPULSE_ERROR r = run_classifier(&signal, &result, false);

        // Dọn xô để hứng khung mới ngay sau khi AI chạy xong, dù có thành công hay không, để sẵn sàng cho khung tiếp theo
        inference.buf_count = 0;
        inference.buf_ready = 0;
        if (r != EI_IMPULSE_OK) continue;

        // 4. TÌM TỪ CÓ ĐIỂM XÁC SUẤT CAO NHẤT
        String best_word = "noise";
        float max_score = 0.0;
        
        for (size_t ix = 0; ix < EI_CLASSIFIER_LABEL_COUNT; ix++) {
            if (result.classification[ix].value > max_score) {
                max_score = result.classification[ix].value;
                best_word = result.classification[ix].label;
            }
        }

        // 5. XỬ LÝ LOGIC MÁY TRẠNG THÁI (Ngưỡng tự tin > 85%)
        if (max_score > 0.85) {
            if (current_voice_state == STATE_IDLE) {
                if (best_word == "alo_oi") {
                    Serial.println("🔔 Đã nghe thấy Alo Oi! Mở tai 10s...");
                    previousAction = cmdAction; 
                    current_voice_state = STATE_LISTENING;
                    listening_start_time = millis(); 
                    digitalWrite(PIN_LED_INDICATOR, HIGH); 
                }
            } 
            else if (current_voice_state == STATE_LISTENING) {
                if (best_word == "batden") {
                    Serial.println("✅ Lệnh: BẬT ĐÈN!");
                    cmdAction = "turn_on";
                    cmdColor = "white"; 
                    cmdBrightness = 100;
                    previousAction = cmdAction;
                } 
                else if (best_word == "tatden") {
                    Serial.println("✅ Lệnh: TẮT ĐÈN!");
                    cmdAction = "turn_off";
                    previousAction = cmdAction;
                }
                else if (best_word == "nhaynhac") {
                    Serial.println("✅ Lệnh: NHÁY NHẠC!");
                    cmdAction = "music";
                    previousAction = cmdAction;
                }
                else if (best_word == "alo_oi") {
                    Serial.println("⏳ Gia hạn thời gian nghe thêm 10s...");
                    listening_start_time = millis();
                }
                else if (best_word == "tangsang") { 
                    cmdBrightness = constrain(cmdBrightness + 15, 0, 255);
                    cmdAction = previousAction; 
                    Serial.printf("💡 Tăng sáng: %d\n", cmdBrightness);
                }
                else if (best_word == "giamsang") { 
                    cmdBrightness = constrain(cmdBrightness - 15, 0, 255);
                    cmdAction = previousAction;
                    Serial.printf("💡 Giảm sáng: %d\n", cmdBrightness);
                }
            }
        }

        // 6. KIỂM TRA HẾT GIỜ (TIMEOUT 10 GIÂY)
        if (current_voice_state == STATE_LISTENING) {
            if (millis() - listening_start_time > LISTENING_TIMEOUT) {
                Serial.println("⏰ Hết 10s. Đóng mic!");
                digitalWrite(PIN_LED_INDICATOR, LOW); 
                current_voice_state = STATE_IDLE;
            }
        }

        vTaskDelay(10 / portTICK_PERIOD_MS); 
    }
}

// ==========================================================
// 3. KHỐI CÁC HÀM CẤU HÌNH I2S VÀ XỬ LÝ ÂM THANH
// ==========================================================
static void audio_inference_callback(uint32_t n_bytes) {
    for(int i = 0; i < n_bytes>>1; i++) {
        // CHỈ NẠP THÊM ÂM THANH NẾU AI ĐÃ ĐỌC XONG (buf_ready == 0)
        if (inference.buf_ready == 0) {
            inference.buffer[inference.buf_count++] = sampleBuffer[i];
            if(inference.buf_count >= inference.n_samples) {
                inference.buf_ready = 1; 
                // Không reset buf_count ở đây nữa để giữ nguyên dữ liệu cho AI
            }
        }
    }
}

static void capture_samples(void* arg) {
    const int32_t i2s_bytes_to_read = (uint32_t)arg;
    size_t bytes_read = i2s_bytes_to_read;

    // Biến trạng thái tĩnh cho bộ lọc IIR (High-pass filter)
    static float prev_raw_sample = 0.0f;
    static float prev_clean_sample = 0.0f;

    while (record_status) {
        i2s_read((i2s_port_t)1, (void*)sampleBuffer, i2s_bytes_to_read, &bytes_read, 100);

        if (bytes_read > 0 && bytes_read >= i2s_bytes_to_read) {
            for (int x = 0; x < i2s_bytes_to_read/2; x++) {
                
                float raw = (float)sampleBuffer[x];

                // ----------------------------------------------------------------
                // 🚀 KỸ THUẬT 1: LỌC THÀNH PHẦN DC (HIGH-PASS FILTER BẬC 1)
                // Công thức: y[n] = x[n] - x[n-1] + 0.995 * y[n-1]
                // ----------------------------------------------------------------
                float clean = raw - prev_raw_sample + 0.995f * prev_clean_sample;
                
                // Lưu lại trạng thái cho chu kỳ kế tiếp
                prev_raw_sample = raw;
                prev_clean_sample = clean;

                int16_t processed_sample = (int16_t)clean;

                // ----------------------------------------------------------------
                // 🚀 KỸ THUẬT 2: NOISE GATE (CỔNG CHỐNG ỒN)
                // ----------------------------------------------------------------
                const int NOISE_GATE_THRESHOLD = 40; // Ngưỡng biên độ cắt nhiễu
                
                if (abs(processed_sample) < NOISE_GATE_THRESHOLD) {
                    processed_sample = 0; // Nếu nhỏ hơn ngưỡng, triệt tiêu về 0 (Xóa tiếng xì xào)
                } else {
                    processed_sample = processed_sample * 8; // Chỉ khuếch đại khi có giọng nói lớn
                }

                sampleBuffer[x] = processed_sample;
            }

            if (record_status) {
                audio_inference_callback(i2s_bytes_to_read);
            } else {
                break;
            }
        }
    }
    vTaskDelete(NULL);
}

// ... (Các hàm i2s_init, i2s_deinit, microphone_inference_start Tuyền giữ nguyên y hệt như cũ nhé, Oi không chép lại cho đỡ dài) ...
static bool microphone_inference_start(uint32_t n_samples) {
    inference.buffer = (int16_t *)malloc(n_samples * sizeof(int16_t));
    if(inference.buffer == NULL) return false;
    inference.buf_count  = 0;
    inference.n_samples  = n_samples;
    inference.buf_ready  = 0;
    if (i2s_init(EI_CLASSIFIER_FREQUENCY)) ei_printf("Failed to start I2S!");
    ei_sleep(100);
    record_status = true;
    xTaskCreate(capture_samples, "CaptureSamples", 1024 * 32, (void*)sample_buffer_size, 10, NULL);
    return true;
}

static bool microphone_inference_record(void) {
    bool ret = true;
    while (inference.buf_ready == 0) {
        vTaskDelay(10 / portTICK_PERIOD_MS); // Nghỉ 10ms chờ xô đầy
    }
    return ret;
}

static int microphone_audio_signal_get_data(size_t offset, size_t length, float *out_ptr) {
    numpy::int16_to_float(&inference.buffer[offset], out_ptr, length);
    return 0;
}

static void microphone_inference_end(void) {
    i2s_deinit();
    ei_free(inference.buffer);
}

static int i2s_init(uint32_t sampling_rate) {
    i2s_config_t i2s_config = {
        .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_RX | I2S_MODE_TX),
        .sample_rate = sampling_rate,
        .bits_per_sample = (i2s_bits_per_sample_t)16,
        .channel_format = I2S_CHANNEL_FMT_ONLY_LEFT, 
        .communication_format = I2S_COMM_FORMAT_STAND_I2S,
        .intr_alloc_flags = 0,
        .dma_buf_count = 8,
        .dma_buf_len = 512,
        .use_apll = false,
        .tx_desc_auto_clear = false,
        .fixed_mclk = -1,
    };
    i2s_pin_config_t pin_config = {
        .bck_io_num = PIN_I2S_SCK,     
        .ws_io_num = PIN_I2S_WS,       
        .data_out_num = -1,            
        .data_in_num = PIN_I2S_SD      
    };
    esp_err_t ret = i2s_driver_install((i2s_port_t)1, &i2s_config, 0, NULL);
    ret = i2s_set_pin((i2s_port_t)1, &pin_config);
    ret = i2s_zero_dma_buffer((i2s_port_t)1);
    return int(ret);
}

static int i2s_deinit(void) {
    i2s_driver_uninstall((i2s_port_t)1); 
    return 0;
}