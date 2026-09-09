/**
 * Task_Sensors.cpp — Cảm biến ánh sáng môi trường VEML7700.
 *
 * THAY ĐỔI (G1.1/G1.5)
 * Trước đây task này gọi thẳng sendTelemetry(), tức là chạm vào PubSubClient từ
 * một task khác — thư viện đó không an toàn đa luồng. Giờ nó chỉ đặt số đo mới
 * nhất vào oiLuxQueue; TaskNetwork là nơi duy nhất nói chuyện với broker.
 */
#include <Adafruit_VEML7700.h>
#include <Arduino.h>
#include <Wire.h>

#include "Config.h"
#include "Node1.h"

static Adafruit_VEML7700 veml = Adafruit_VEML7700();

void TaskSensors(void *pvParameters) {
    Serial.println("👁️ TaskSensors: khởi tạo I2C cho VEML7700...");
    Wire.begin(PIN_I2C_SDA, PIN_I2C_SCL);

    if (!veml.begin()) {
        // Hỏng một cảm biến không được phép kéo sập cả node: các task còn lại
        // (giọng nói, nút bấm, đèn) vẫn phải chạy.
        Serial.println("❌ Không tìm thấy VEML7700 — dừng riêng task này.");
        vTaskDelete(NULL);
    }

    veml.setGain(VEML7700_GAIN_1);
    veml.setIntegrationTime(VEML7700_IT_800MS);
    Serial.println("✅ Cảm biến lux sẵn sàng.");

    for (;;) {
        float lux = veml.readLux();
        // Ghi đè giá trị cũ: người đọc chỉ quan tâm số đo mới nhất, và người
        // ghi không bao giờ bị chặn dù TaskNetwork đang bận kết nối lại.
        xQueueOverwrite(oiLuxQueue, &lux);

        vTaskDelay(SENSOR_PERIOD_MS / portTICK_PERIOD_MS);
    }
}
