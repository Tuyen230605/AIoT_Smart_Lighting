#include <Arduino.h>
#include <ArduinoJson.h>
#include <WiFiClientSecure.h>
#include <PubSubClient.h>
#include "Config.h"

// Các biến chứa Chứng chỉ bảo mật (được định nghĩa trong Secrets.cpp)
extern const char AWS_CERT_CA[];
extern const char AWS_CERT_CRT[];
extern const char AWS_CERT_PRIVATE[];

// Biến toàn cục để lưu lệnh
extern String cmdAction;
extern String cmdColor;
extern int cmdBrightness;

// Khởi tạo đối tượng WiFi Bảo mật và MQTT Client
WiFiClientSecure net = WiFiClientSecure();
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

  // 2. Nạp chứng chỉ bảo mật cho kết nối TLS
  net.setCACert(AWS_CERT_CA);
  net.setCertificate(AWS_CERT_CRT);
  net.setPrivateKey(AWS_CERT_PRIVATE);

  // 3. Cấu hình Server AWS
  client.setServer(MQTT_SERVER, MQTT_PORT);

  // Đăng ký hàm callback để nhận lệnh từ AWS
  client.setCallback(callback);

  // 4. Bắt tay với AWS IoT Core
  Serial.println("🔐 Đang kết nối với AWS IoT Core...");
  // Vòng lặp thử kết nối lại nếu thất bại
  while (!client.connected()) {
    Serial.print(".");
    // Sử dụng THING_NAME Tuyền vừa đặt làm Client ID
    if (client.connect("ESP32_AIoT_Node")) {
      Serial.println("\n🚀 ĐÃ KẾT NỐI AWS IOT CORE THÀNH CÔNG!");

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
  
  Serial.print("☁️ Đã đẩy lên AWS: ");
  Serial.println(jsonBuffer);
}