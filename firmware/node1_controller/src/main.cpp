/**
 * main.cpp — Node 1 (ESP32-S3): khởi tạo và phân luồng.
 *
 * THAY ĐỔI SO VỚI BẢN DACN (G1.1)
 * File này từng khai báo ba biến `String` toàn cục dùng chung giữa hai nhân.
 * Chúng đã bị xoá hẳn. Trạng thái đèn giờ chỉ tồn tại bên trong TaskLED, và
 * mọi nguồn lệnh nói chuyện với nó qua hàng đợi trong oi_cmdbus.h.
 *
 * Cũng bỏ luôn lời gọi FastLED.addLeds() ở đây: nó bị gọi cả ở main.cpp lẫn
 * Task_LED.cpp, tức là hai controller cùng trỏ vào một mảng LED (G1.4).
 */
#include <Arduino.h>

#include "Config.h"
#include "Node1.h"
#include "oi_cmdbus.h"
#include "oi_health.h"

// Định nghĩa các biến toàn cục của hai module dùng chung — đúng một lần.
OI_CMDBUS_DEFINE()
OI_HEALTH_DEFINE()

QueueHandle_t oiLuxQueue = NULL;

void setup() {
  Serial.begin(115200);
  delay(2000);   // đợi nguồn ổn định và cổng USB CDC lên

  Serial.println("\n=============================================");
  Serial.println("🚀 KHỞI ĐỘNG NODE 1 — ESP32-S3 (Oi)");
  Serial.printf("   Giao thức v%d.%d · lần khởi động thứ %u\n",
                OI_PROTO_MAJOR, OI_PROTO_MINOR, oiHealthBootCount());
  Serial.println("=============================================");

  // Hàng đợi phải tồn tại TRƯỚC khi có task nào chạy, nếu không lệnh đầu tiên
  // sẽ rơi vào một hàng đợi NULL.
  oiLuxQueue = xQueueCreate(1, sizeof(float));
  if (!oiCmdBusInit(CMD_QUEUE_DEPTH) || oiLuxQueue == NULL) {
    Serial.println("❌ Không cấp phát được hàng đợi — dừng lại.");
    while (true) delay(1000);
  }

  TaskHandle_t hNet = NULL, hSensors = NULL, hLed = NULL, hMic = NULL, hBtn = NULL;

  // Core 1 — vào/ra và kết nối
  xTaskCreatePinnedToCore(TaskNetwork, "net",  STACK_NET,     NULL, PRIO_NET,     &hNet,     CORE_IO);
  xTaskCreatePinnedToCore(TaskSensors, "sens", STACK_SENSORS, NULL, PRIO_SENSORS, &hSensors, CORE_IO);
  xTaskCreatePinnedToCore(TaskLED,     "led",  STACK_LED,     NULL, PRIO_LED,     &hLed,     CORE_IO);
  xTaskCreatePinnedToCore(TaskButton,  "btn",  STACK_BUTTON,  NULL, PRIO_BUTTON,  &hBtn,     CORE_IO);

  // Core 0 — dành riêng cho suy luận AI
  xTaskCreatePinnedToCore(TaskMic,     "mic",  STACK_INFERENCE, NULL, PRIO_INFERENCE, &hMic, CORE_AI);

  // Đăng ký để đo mức dùng ngăn xếp trong bài chạy 72 giờ (G1.8)
  oiHealthRegister("net",  hNet);
  oiHealthRegister("sens", hSensors);
  oiHealthRegister("led",  hLed);
  oiHealthRegister("btn",  hBtn);
  oiHealthRegister("mic",  hMic);

  Serial.println("✅ Đã tạo 5 task trên hai nhân.");
}

void loop() {
  // FreeRTOS lo hết. Task rỗng này ngủ vĩnh viễn để nhường CPU.
  vTaskDelay(portMAX_DELAY);
}
