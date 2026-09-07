#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_VEML7700.h>
#include "Config.h"

// Khởi tạo đối tượng cảm biến
Adafruit_VEML7700 veml = Adafruit_VEML7700();

extern void sendTelemetry(float luxValue);

void TaskSensors(void *pvParameters) {
  Serial.println("👁️ Đang khởi tạo giao tiếp I2C cho VEML7700...");
  
  // Khởi tạo I2C với 2 chân SDA và SCL đã định nghĩa trong Config.h
  Wire.begin(PIN_I2C_SDA, PIN_I2C_SCL);

  // Kiểm tra kết nối với cảm biến
  if (!veml.begin()) {
    Serial.println("❌ LỖI: Không tìm thấy VEML7700! Tuyền kiểm tra lại dây cắm nhé.");
    // Nếu lỗi, cho Task này tự đưa vào vòng lặp vô hạn (chỉ dừng Task này, không chết cả mạch)
    while (1) {
        vTaskDelay(1000 / portTICK_PERIOD_MS); 
    }
  }
  
  Serial.println("✅ Cảm biến Lux đã sẵn sàng!");
  
  // Tinh chỉnh độ nhạy sáng (Gain) và thời gian lấy mẫu (Integration Time)
  veml.setGain(VEML7700_GAIN_1);
  veml.setIntegrationTime(VEML7700_IT_800MS);

  // Vòng lặp vô hạn của Task Sensors
  for (;;) {
    // Đọc giá trị Lux từ môi trường
    float lux = veml.readLux();
    
    Serial.print("💡 Cường độ ánh sáng môi trường: ");
    Serial.print(lux);
    Serial.println(" lx");

    // TODO: (Bước tiếp theo) Chúng ta sẽ lấy biến 'lux' này 
    // đóng gói thành chuỗi JSON và bắn lên AWS IoT Core!
    sendTelemetry(lux);

    // FreeRTOS: Cho Task nghỉ ngơi 2 giây trước khi đọc lần tiếp theo
    vTaskDelay(2000 / portTICK_PERIOD_MS); 
  }
}