#include <Arduino.h>
#include "Config.h"

// Mượn các biến điều khiển từ hệ thống
extern String cmdAction;
extern String cmdColor;
extern int cmdBrightness;

// Danh sách 7 màu cố định để xoay vòng
const String colorList[] = {"white", "red", "green", "blue", "yellow", "purple", "cyan"};
const int numColors = 7;
int currentColorIndex = 0;

// ======================================================
// THÔNG SỐ CẤU HÌNH THỜI GIAN NHẤN NÚT
// ======================================================
const unsigned long DEBOUNCE_DELAY = 200; // Nghỉ 200ms chống dội cho nút Toggle/Màu
const unsigned long HOLD_DELAY = 500;     // Giữ nút 500ms thì bắt đầu tính là "Nhấn giữ"
const unsigned long HOLD_SPEED = 50;      // Tốc độ lướt khi đang giữ: 50ms chạy 1 lần
const int CLICK_STEP = 15;                // Bước nhảy khi Click 1 cái
const int HOLD_STEP = 5;                  // Bước nhảy khi Nhấn giữ (để nó trượt mượt)

// Bộ nhớ chống dội phím cho 2 nút Bật/Tắt và Đổi màu
unsigned long lastDebounceTime[2] = {0, 0}; 

// Bộ nhớ trạng thái riêng cho nút Tăng Sáng
bool isUpPressed = false;
unsigned long upPressTime = 0;
unsigned long upLastHoldTime = 0;

// Bộ nhớ trạng thái riêng cho nút Giảm Sáng
bool isDownPressed = false;
unsigned long downPressTime = 0;
unsigned long downLastHoldTime = 0;

void TaskButton(void *pvParameters) {
    Serial.println("🔘 Task Button (Có chức năng Nhấn Giữ) đã khởi động!");

    // Cấu hình chân INPUT_PULLUP (Mặc định HIGH, bấm xuống là LOW)
    pinMode(PIN_BTN_TOGGLE, INPUT_PULLUP);
    pinMode(PIN_BTN_UP, INPUT_PULLUP);
    pinMode(PIN_BTN_DOWN, INPUT_PULLUP);
    pinMode(PIN_BTN_COLOR, INPUT_PULLUP);

    for (;;) {
        unsigned long currentMillis = millis();

        // ==================================================
        // 1. NÚT BẬT / TẮT (Chỉ Click)
        // ==================================================
        if (digitalRead(PIN_BTN_TOGGLE) == LOW && (currentMillis - lastDebounceTime[0] > DEBOUNCE_DELAY)) {
            if (cmdAction == "turn_off") {
                cmdAction = "turn_on"; 
                Serial.println("🔘 Nút: BẬT ĐÈN");
            } else {
                cmdAction = "turn_off"; 
                Serial.println("🔘 Nút: TẮT ĐÈN");
            }
            lastDebounceTime[0] = currentMillis;
        }

        // ==================================================
        // 2. NÚT ĐỔI MÀU (Chỉ Click, đèn phải đang bật)
        // ==================================================
        if (digitalRead(PIN_BTN_COLOR) == LOW && (currentMillis - lastDebounceTime[1] > DEBOUNCE_DELAY)) {
            if (cmdAction != "turn_off") {
                currentColorIndex = (currentColorIndex + 1) % numColors;
                cmdColor = colorList[currentColorIndex];
                cmdAction = "turn_on"; 
                Serial.println("🔘 Nút: ĐỔI MÀU -> " + cmdColor);
            }
            lastDebounceTime[1] = currentMillis;
        }

        // ==================================================
        // 3. NÚT TĂNG SÁNG (Click & Hold)
        // ==================================================
        if (digitalRead(PIN_BTN_UP) == LOW && cmdAction != "turn_off") {
            if (!isUpPressed) { // Vừa mới dập nút xuống
                isUpPressed = true;
                upPressTime = currentMillis;
                upLastHoldTime = currentMillis;

                // Xử lý ngay lệnh Click đầu tiên
                cmdBrightness = constrain(cmdBrightness + CLICK_STEP, 0, 255);
                if (cmdAction == "turn_off") cmdAction = "turn_on"; // Ép sáng nếu đang tắt
                Serial.printf("🔘 Nút: Click TĂNG SÁNG (%d)\n", cmdBrightness);
            } 
            else { // Nút vẫn đang bị đè (Hold)
                if (currentMillis - upPressTime > HOLD_DELAY) { // Đã đè quá 0.5s
                    if (currentMillis - upLastHoldTime > HOLD_SPEED) { // Cứ 50ms lại tăng 1 nhịp
                        cmdBrightness = constrain(cmdBrightness + HOLD_STEP, 0, 255);
                        Serial.printf("🔘 Nút: Giữ TĂNG SÁNG trơn tru (%d)\n", cmdBrightness);
                        upLastHoldTime = currentMillis;
                    }
                }
            }
        } else {
            isUpPressed = false; // Người dùng đã thả tay
        }

        // ==================================================
        // 4. NÚT GIẢM SÁNG (Click & Hold)
        // ==================================================
        if (digitalRead(PIN_BTN_DOWN) == LOW && cmdAction != "turn_off") {
            if (!isDownPressed) { 
                isDownPressed = true;
                downPressTime = currentMillis;
                downLastHoldTime = currentMillis;

                cmdBrightness = constrain(cmdBrightness - CLICK_STEP, 0, 255);
                Serial.printf("🔘 Nút: Click GIẢM SÁNG (%d)\n", cmdBrightness);
            } 
            else { 
                if (currentMillis - downPressTime > HOLD_DELAY) {
                    if (currentMillis - downLastHoldTime > HOLD_SPEED) {
                        cmdBrightness = constrain(cmdBrightness - HOLD_STEP, 0, 255);
                        Serial.printf("🔘 Nút: Giữ GIẢM SÁNG trơn tru (%d)\n", cmdBrightness);
                        downLastHoldTime = currentMillis;
                    }
                }
            }
        } else {
            isDownPressed = false; 
        }

        // Nghỉ 20ms thay vì 50ms để bắt tín hiệu Nhấn giữ cho nhạy và mượt hơn
        vTaskDelay(20 / portTICK_PERIOD_MS);
    }
}