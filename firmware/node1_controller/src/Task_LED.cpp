/**
 * Task_LED.cpp — CHỦ SỞ HỮU DUY NHẤT của trạng thái đèn.
 *
 * VAI TRÒ SAU G1.1
 * Không task nào khác được sửa trạng thái đèn hay chạm vào FastLED. Task này
 * nhận lệnh từ oiCmdQueue, áp dụng tuần tự, rồi đẩy trạng thái kết quả sang
 * oiStateQueue cho TaskNetwork phát lên MQTT (G1.5). Nhờ vậy không còn biến
 * nào bị ghi từ hai nhân, và cũng không còn khả năng mất cập nhật khi hai lệnh
 * tương đối đến sát nhau.
 *
 * G1.4 làm thêm ở đây:
 *   · giới hạn công suất — 256 bóng × trắng × 255 rút khoảng 15 A, đủ để kéo
 *     sụt áp và gây brownout reset;
 *   · nội suy chuyển cảnh tối thiểu 500 ms thay vì nhảy bậc;
 *   · addLeds() chỉ còn được gọi ở đây (trước đây gọi cả ở main.cpp).
 */
#include <Arduino.h>
#include <FastLED.h>

#include "Config.h"
#include "oi_cmdbus.h"

CRGB leds[NUM_LEDS];

// Danh sách màu để nút "đổi màu" xoay vòng.
static const uint32_t kColorWheel[] = {
    0xFFFFFF, 0xFF0000, 0x00FF00, 0x0000FF, 0xFFFF00, 0x800080, 0x00FFFF,
};
static const uint8_t kColorCount = sizeof(kColorWheel) / sizeof(kColorWheel[0]);

static const uint8_t  kBrightnessStep = 15;
static const uint8_t  kDefaultOnBri   = 100;

// ══════════════════════════════════════════════════════════════
//  Nhiệt độ màu → RGB (xấp xỉ Tanner Helland)
//
//  Hub gửi lệnh theo Kelvin ("dịu lại đi" → cct 3000) chứ không theo mã màu,
//  nên node phải tự dịch. Xấp xỉ này lệch vài phần trăm so với đường cong vật
//  đen thật — thừa chính xác cho mắt người trong bài toán chiếu sáng phòng.
// ══════════════════════════════════════════════════════════════
static CRGB cctToRgb(uint16_t kelvin) {
    float t = constrain(kelvin, 1000, 40000) / 100.0f;
    float r, g, b;

    if (t <= 66) {
        r = 255;
        g = 99.4708025861f * logf(t) - 161.1195681661f;
        b = (t <= 19) ? 0 : 138.5177312231f * logf(t - 10) - 305.0447927307f;
    } else {
        r = 329.698727446f * powf(t - 60, -0.1332047592f);
        g = 288.1221695283f * powf(t - 60, -0.0755148492f);
        b = 255;
    }
    return CRGB((uint8_t)constrain(r, 0.0f, 255.0f),
                (uint8_t)constrain(g, 0.0f, 255.0f),
                (uint8_t)constrain(b, 0.0f, 255.0f));
}

static CRGB targetColorOf(const OiLightState &s) {
    // rgb = trắng thuần nghĩa là "chưa chọn màu cụ thể" → dùng nhiệt độ màu.
    if (s.rgb == 0xFFFFFF && s.cct > 0) return cctToRgb(s.cct);
    return CRGB((s.rgb >> 16) & 0xFF, (s.rgb >> 8) & 0xFF, s.rgb & 0xFF);
}

void TaskLED(void *pvParameters) {
    Serial.println("💡 TaskLED: chủ sở hữu trạng thái đèn đã khởi động.");

    FastLED.addLeds<WS2812B, PIN_LED, GRB>(leds, NUM_LEDS);

    // ── G1.4: trần công suất ──────────────────────────────────
    // FastLED tự hạ độ sáng toàn dải để dòng ước tính không vượt ngưỡng này.
    // Không có nó, một lệnh "bật trắng tối đa" là một lệnh rút 15 A.
    FastLED.setMaxPowerInVoltsAndMilliamps(OI_PSU_VOLTS, OI_PSU_MILLIAMPS);

    FastLED.clear(true);   // tắt sạch ngay khi vào, bảo vệ nguồn lúc boot

    OiLightState state    = oiStateBoot();
    uint32_t     appliedSeq  = 0;   // seq của lệnh từ xa đã áp dụng gần nhất
    uint32_t     localSeq    = 0;   // bộ đếm cho lệnh sinh tại chỗ
    uint8_t      colorIndex  = 0;
    uint8_t      lastOnBri   = kDefaultOnBri;   // nhớ để TOGGLE bật lại đúng mức

    // Trạng thái bộ nội suy chuyển cảnh
    CRGB     fromColor = CRGB::Black, curColor = CRGB::Black;
    uint8_t  fromBri = 0,  curBri = 0;
    uint32_t fadeStart = millis();
    uint16_t fadeMs = OI_MIN_FADE_MS;

    uint8_t gHue = 0;

    for (;;) {
        // ─────────────────────────────────────────────────────
        // 1. Nhận và áp dụng mọi lệnh đang chờ
        // ─────────────────────────────────────────────────────
        OiCommand cmd;
        bool changed = false;

        while (xQueueReceive(oiCmdQueue, &cmd, 0) == pdTRUE) {
            switch (cmd.kind) {
                case OI_CMD_SET:
                    // Lệnh từ xa mang seq của hub. Đến muộn hơn lệnh đã áp dụng
                    // thì bỏ — mạng có thể đảo thứ tự, và một lệnh "tắt" cũ đè
                    // lên lệnh "bật" mới là kiểu lỗi người dùng thấy ngay.
                    if (cmd.state.seq != 0 && cmd.state.seq <= appliedSeq) {
                        Serial.printf("↩ Bỏ lệnh cũ đến muộn (seq %u ≤ %u)\n",
                                      cmd.state.seq, appliedSeq);
                        break;
                    }
                    if (cmd.state.seq != 0) appliedSeq = cmd.state.seq;

                    state = cmd.state;
                    if (state.mode != OI_MODE_OFF && state.brightness > 0) {
                        lastOnBri = state.brightness;
                    }
                    changed = true;
                    break;

                case OI_CMD_ADJUST_BRI: {
                    if (state.mode == OI_MODE_OFF) break;   // đang tắt thì không chỉnh
                    int v = (int)state.brightness + cmd.delta;
                    state.brightness = (uint8_t)constrain(v, 1, 255);
                    lastOnBri = state.brightness;
                    state.src = cmd.src;
                    changed = true;
                    break;
                }

                case OI_CMD_TOGGLE:
                    if (state.mode == OI_MODE_OFF) {
                        state.mode       = OI_MODE_SOLID;
                        state.brightness = lastOnBri;
                    } else {
                        state.mode       = OI_MODE_OFF;
                    }
                    state.src = cmd.src;
                    changed = true;
                    break;

                case OI_CMD_NEXT_COLOR:
                    if (state.mode == OI_MODE_OFF) break;
                    colorIndex = (colorIndex + 1) % kColorCount;
                    state.rgb  = kColorWheel[colorIndex];
                    state.mode = OI_MODE_SOLID;
                    state.src  = cmd.src;
                    changed = true;
                    break;
            }
        }

        // ─────────────────────────────────────────────────────
        // 2. Đổi đích chuyển cảnh và báo trạng thái mới đi (G1.5)
        // ─────────────────────────────────────────────────────
        if (changed) {
            fromColor = curColor;
            fromBri   = curBri;
            fadeStart = millis();
            fadeMs    = max(state.fade_ms, (uint16_t)OI_MIN_FADE_MS);

            state.seq = ++localSeq;
            state.ts  = millis() / 1000;

            // Không chờ nếu hàng đợi phát tin đầy: mất một bản tin trạng thái
            // còn hơn làm nghẽn vòng vẽ LED.
            xQueueSend(oiStateQueue, &state, 0);
        }

        // ─────────────────────────────────────────────────────
        // 3. Nội suy — không nhảy bậc, không chớp (G1.4)
        // ─────────────────────────────────────────────────────
        uint8_t targetBri = (state.mode == OI_MODE_OFF) ? 0 : state.brightness;
        CRGB    targetCol = targetColorOf(state);

        uint32_t elapsed = millis() - fadeStart;
        uint8_t  t = (elapsed >= fadeMs) ? 255 : (uint8_t)((elapsed * 255UL) / fadeMs);

        curBri   = fromBri + (((int)targetBri - (int)fromBri) * t) / 255;
        curColor = blend(fromColor, targetCol, t);

        // ─────────────────────────────────────────────────────
        // 4. Vẽ
        // ─────────────────────────────────────────────────────
        switch (state.mode) {
            case OI_MODE_OFF:
                fill_solid(leds, NUM_LEDS, curColor);
                break;

            case OI_MODE_MUSIC:
                switch (state.scene) {
                    case OI_SCENE_MUSIC_LOFI:
                        fill_solid(leds, NUM_LEDS, CHSV(160, 255, beatsin8(15, 50, 255)));
                        break;
                    case OI_SCENE_MUSIC_ROCK:
                        fill_solid(leds, NUM_LEDS, (beat8(120) > 128) ? CRGB::Red : CRGB::Black);
                        break;
                    default:
                        fill_rainbow(leds, NUM_LEDS, gHue, 7);
                        if (random8() < 120) leds[random16(NUM_LEDS)] += CRGB::White;
                        gHue += 15;
                        break;
                }
                break;

            case OI_MODE_SOLID:
            case OI_MODE_SCENE:
            case OI_MODE_FOLLOW:
            default:
                fill_solid(leds, NUM_LEDS, curColor);
                break;
        }

        FastLED.setBrightness(curBri);
        FastLED.show();

        vTaskDelay(LED_FRAME_MS / portTICK_PERIOD_MS);
    }
}
