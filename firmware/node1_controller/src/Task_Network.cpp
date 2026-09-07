#include <Arduino.h>
#include <ArduinoJson.h>
#include <WiFi.h>
#include <WiFiClient.h>
#include <PubSubClient.h>
#include "Config.h"

// ĐẤU NỐI TẠM THỜI (xem G1.5 trong kế hoạch thi công).
// Bản DACN kết nối thẳng lên AWS IoT Core qua TLS bằng chứng chỉ nạp cứng.
// Theo ADR 0003, broker nội bộ mới là đường chính; AWS chỉ còn là kênh xem
// từ xa và sẽ được nối lại qua dịch vụ oi-bridge ở G2. Vì cơ chế nạp chứng
// chỉ qua NVS (G1.7) chưa làm, file này tạm dùng MQTT thường (không TLS)
// trỏ vào HUB_MQTT_HOST — không còn chứng chỉ nào trong mã nguồn.
// G1.5 sẽ thay toàn bộ chuỗi topic/khoá JSON cứng dưới đây bằng các hằng số
// trong oi_protocol.h và publish qua struct OiLightState.

// Biến toàn cục để lưu lệnh
extern String cmdAction;
extern String cmdColor;
extern int cmdBrightness;

// Khởi tạo đối tượng WiFi và MQTT Client
WiFiClient net;
PubSubClient client(net);


// Hàm này sẽ tự động được gọi mỗi khi có tin nhắn từ AWS bay về
void callback(char* topic, byte* payload, unsigned int length) {
  Serial.print("\n📩 Có lệnh mới từ Topic: ");
  Serial.println(topic);

  // Chuyển đổi mã byte nhận được thành chuỗi văn bản (String)
  String messageTemp;
  for (int i = 0; i < length; i++) {
    messageTemp += (char)payload[i];
  }
  Serial.print("📦 Nội dung lệnh: ");
  Serial.println(messageTemp);

  // Dùng ArduinoJson để giải mã tin nhắn
  StaticJsonDocument<200> doc;
  DeserializationError error = deserializeJson(doc, messageTemp);

  if (error) {
    Serial.print("❌ Lỗi đọc JSON: ");
    Serial.println(error.c_str());
    return;
  }

  // Cập nhật biến toàn cục dựa trên nội dung JSON nhận được
  if (doc.containsKey("action")) {
    cmdAction = doc["action"].as<String>();
  }
  if (doc.containsKey("color")) {
    cmdColor = doc["color"].as<String>();
  }
  if (doc.containsKey("brightness")) {
      cmdBrightness = doc["brightness"].as<int>();
      Serial.print("Độ sáng mới: ");
      Serial.println(cmdBrightness);
    }
}

// Hàm kết nối WiFi và AWS
void connectAWS() {
  // 1. Kết nối WiFi
  Serial.print("📡 Đang kết nối WiFi: ");
  Serial.println(WIFI_SSID);
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASS);

  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\n✅ WiFi đã kết nối!");

  // 2. Cấu hình broker nội bộ (không TLS — xem ghi chú đầu file)
  client.setServer(HUB_MQTT_HOST, HUB_MQTT_PORT);

  // Đăng ký hàm callback để nhận lệnh
  client.setCallback(callback);

  // 3. Bắt tay với broker
  Serial.println("🔐 Đang kết nối với broker MQTT...");
  // Vòng lặp thử kết nối lại nếu thất bại
  while (!client.connected()) {
    Serial.print(".");
    if (client.connect(THING_NAME)) {
      Serial.println("\n🚀 ĐÃ KẾT NỐI BROKER THÀNH CÔNG!");

      // ESP32 đăng ký nghe lệnh từ Topic này
      client.subscribe("oi/light/command");

    } else {
      Serial.println("\n❌ Kết nối thất bại, thử lại sau 5 giây...");
      delay(5000);
    }
  }
}

// ==========================================
// ĐÂY LÀ HÀM TASK ĐỂ CHẠY TRONG FREERTOS
// ==========================================
void TaskNetwork(void *pvParameters) {
  // Chạy 1 lần khi khởi động
  connectAWS();

  // Vòng lặp vô hạn của Task
  for (;;) {
    // Kiểm tra nếu rớt mạng thì gọi lại hàm kết nối
    if (!client.connected()) {
      connectAWS();
    }
    
    // Hàm này giữ cho kết nối MQTT luôn sống (nhận Ping/Pong từ Server)
    client.loop();

    // FreeRTOS: Cho Task nghỉ ngơi 10ms để nhường CPU cho Task khác (LED, Mic...)
    // Không bao giờ dùng delay() trong Task nhé!
    vTaskDelay(10 / portTICK_PERIOD_MS); 
  }
}

// Hàm đóng gói và gửi dữ liệu lên AWS
void sendTelemetry(float luxValue) {
  // Nếu mất mạng thì không gửi để tránh lỗi bộ nhớ
  if (!client.connected()) return;

  // Tạo một tài liệu JSON dung lượng 200 byte
  StaticJsonDocument<200> doc;
  
  // Đóng gói dữ liệu
  doc["device"] = THING_NAME;
  doc["sensor"] = "VEML7700";
  doc["lux"] = luxValue;

  // Chuyển JSON thành chuỗi văn bản (String)
  char jsonBuffer[256];
  serializeJson(doc, jsonBuffer);

  // Tạo Topic theo tên thiết bị (Ví dụ: oi/telemetry/ESP32_AIoT_Node)
  String topic = String("oi/telemetry/") + THING_NAME;

  // Publish lên AWS
  client.publish(topic.c_str(), jsonBuffer);
  
  Serial.print("☁️ Đã đẩy lên broker: ");
  Serial.println(jsonBuffer);
}