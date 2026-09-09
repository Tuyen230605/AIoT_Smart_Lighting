/**
 * TaskButton.cpp — Bốn nút bấm vật lý.
 *
 * THAY ĐỔI SO VỚI BẢN DACN (G1.1)
 * Trước đây task này đọc rồi ghi thẳng vào biến toàn cục dùng chung giữa hai
 * nhân. Giờ nó chỉ *mô tả ý định* và gửi qua hàng đợi. Hệ quả có thật, không
 * phải chỉ cho gọn: khi giữ nút tăng sáng, mỗi 50 ms gửi một lệnh "+5" thay vì
 * "đặt bằng 105" — nên dù TaskLED đang bận vẽ khung hình, không nhịp tăng nào
 * bị nuốt mất do hai bên cùng đọc một giá trị cũ.
 *
 * Nút bấm là lớp phòng thủ cuối cùng (mức suy giảm L3: mất Wi-Fi hoàn toàn),
 * nên task này không phụ thuộc vào mạng, hub hay AI.
 */
#include <Arduino.h>

#include "Config.h"
#include "oi_cmdbus.h"

static const unsigned long DEBOUNCE_DELAY = 200;   // chống dội cho nút nhấn-nhả
static const unsigned long HOLD_DELAY     = 500;   // giữ quá mức này là "nhấn giữ"
static const unsigned long HOLD_SPEED     = 50;    // nhịp lặp khi đang giữ
static const int16_t       CLICK_STEP     = 15;
static const int16_t       HOLD_STEP      = 5;

// Gom trạng thái của một nút "click và giữ" vào một chỗ, thay vì sáu biến rời.
struct HoldButton {
    uint8_t       pin;
    int8_t        dir;         // +1 tăng sáng, -1 giảm sáng
    bool          pressed;
    unsigned long pressTime;
    unsigned long lastRepeat;
};

static void serviceHoldButton(HoldButton &b, unsigned long now) {
    if (digitalRead(b.pin) == LOW) {
        if (!b.pressed) {
            b.pressed    = true;
            b.pressTime  = now;
            b.lastRepeat = now;
            oiCmdPostAdjust(b.dir * CLICK_STEP, OI_SRC_BUTTON);
        } else if (now - b.pressTime > HOLD_DELAY && now - b.lastRepeat > HOLD_SPEED) {
            b.lastRepeat = now;
            oiCmdPostAdjust(b.dir * HOLD_STEP, OI_SRC_BUTTON);
        }
    } else {
        b.pressed = false;
    }
}

void TaskButton(void *pvParameters) {
    Serial.println("🔘 TaskButton: 4 nút vật lý đã sẵn sàng.");

    pinMode(PIN_BTN_TOGGLE, INPUT_PULLUP);
    pinMode(PIN_BTN_UP,     INPUT_PULLUP);
    pinMode(PIN_BTN_DOWN,   INPUT_PULLUP);
    pinMode(PIN_BTN_COLOR,  INPUT_PULLUP);

    unsigned long lastToggle = 0, lastColor = 0;
    HoldButton up   = {PIN_BTN_UP,   +1, false, 0, 0};
    HoldButton down = {PIN_BTN_DOWN, -1, false, 0, 0};

    for (;;) {
        unsigned long now = millis();

        if (digitalRead(PIN_BTN_TOGGLE) == LOW && now - lastToggle > DEBOUNCE_DELAY) {
            lastToggle = now;
            oiCmdPostSimple(OI_CMD_TOGGLE, OI_SRC_BUTTON);
        }

        if (digitalRead(PIN_BTN_COLOR) == LOW && now - lastColor > DEBOUNCE_DELAY) {
            lastColor = now;
            oiCmdPostSimple(OI_CMD_NEXT_COLOR, OI_SRC_BUTTON);
        }

        serviceHoldButton(up,   now);
        serviceHoldButton(down, now);

        vTaskDelay(BUTTON_SCAN_MS / portTICK_PERIOD_MS);
    }
}
