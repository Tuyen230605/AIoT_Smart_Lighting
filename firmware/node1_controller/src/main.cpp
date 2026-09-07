#include <Arduino.h>
#include <FastLED.h> 
#include "Config.h"

// =======================================================
// BỘ BIẾN TOÀN CỤC (CẦU NỐI GIỮA AI VÀ CÁC THIẾT BỊ KHÁC)
// =======================================================
String cmdAction = "turn_off"; // Mặc định vừa bật máy lên là tắt đèn
String cmdColor = "black";
int cmdBrightness = 100;

// Mảng LED (Lấy từ TaskLED sang để dùng cho lúc khởi động)
extern CRGB leds[NUM_LEDS]; 

// =======================================================
// KHAI BÁO CÁC TASK TỪ CÁC FILE KHÁC (UNIT FILES)
// =======================================================
extern void TaskNetwork(void *pvParameters);
extern void TaskSensors(void *pvParameters);
extern void TaskLED(void *pvParameters);
extern void TaskMic(void *pvParameters); 
extern void TaskButton(void *pvParameters);

void setup() {
  Serial.begin(115200);
  delay(2000); // Đợi ổn định nguồn trước khi boot

  Serial.println("\n=============================================");
  Serial.println("🚀 KHỞI ĐỘNG HỆ THỐNG ESP32-S3 AIoT (Oi)");
  Serial.println("=============================================");

  // --- BẢO VỆ PHẦN CỨNG LÚC BOOT ---
  Serial.println("🛡️ Đang tắt bảng LED để bảo vệ nguồn...");
  FastLED.addLeds<WS2812B, PIN_LED, GRB>(leds, NUM_LEDS);
  FastLED.clear(); 
  FastLED.show();  
  delay(500);      

  // =======================================================
  // KÍCH HOẠT HỆ ĐIỀU HÀNH THỜI GIAN THỰC (FREERTOS)
  // =======================================================

  // 1. NHÓM TASK TRÊN CORE 1 (Nhân lo việc Kết nối & Hiển thị)
  // Mạng và Cảm biến chạy ưu tiên số 1
  xTaskCreatePinnedToCore(TaskNetwork, "NetworkTask", 10000, NULL, 1, NULL, 1);
  xTaskCreatePinnedToCore(TaskSensors, "SensorsTask", 5000, NULL, 1, NULL, 1);
  xTaskCreatePinnedToCore(TaskLED,     "LEDTask",     10000, NULL, 1, NULL, 1);
  xTaskCreatePinnedToCore(TaskButton,  "ButtonTask",  2048, NULL, 1, NULL, 1);

  // 2. NHÓM TASK TRÊN CORE 0 (Nhân chuyên biệt cho Trí tuệ Nhân tạo)
  // CẤP 32KB RAM (32768) VÀ ƯU TIÊN SỐ 2 ĐỂ AI CHẠY MƯỢT NHẤT
  xTaskCreatePinnedToCore(TaskMic,     "MicTask",     32768, NULL, 2, NULL, 0);

  Serial.println("✅ Phân luồng Dual-Core thành công! Đang vào chế độ ngủ đông Loop...");
}

void loop() {
  // FreeRTOS đã lo hết mọi việc ở các Task riêng biệt.
  // Hàm loop() của Arduino giờ chỉ việc đi ngủ để tiết kiệm CPU.
  vTaskDelay(portMAX_DELAY);
}