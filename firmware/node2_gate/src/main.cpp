#include <Arduino.h>
#include <WiFiClientSecure.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include <ld2410.h>
#include "Config.h"

// Khởi tạo các đối tượng
ld2410 radar;
HardwareSerial RadarSerial(2); // Dùng UART2
WiFiClientSecure espClient;
PubSubClient client(espClient);

// Biến lưu thời gian để không gửi data liên tục gây nghẽn mạng
unsigned long lastMsgTime = 0;

// =======================================================
// CÁC BIẾN CHO LOGIC NÚT NHẤN BOOT
// =======================================================
bool manualMode = false;
bool isTestModeActive = false;
unsigned long lastTestPressTime = 0;

bool lastButtonState = HIGH;
unsigned long buttonPressTime = 0;
bool buttonPressed = false;
bool longPressHandled = false;

// =======================================================
// HÀM NHẬN LỆNH TỪ NODE-RED ĐỂ BẬT/TẮT ĐÈN CỔNG
// =======================================================
void callback(char* topic, byte* payload, unsigned int length) {
    Serial.print("\n📩 Có lệnh điều khiển Cổng từ Node-RED: ");
    
    String messageTemp;
    for (int i = 0; i < length; i++) {
        messageTemp += (char)payload[i];
    }
    Serial.println(messageTemp);

    // Giải mã JSON
    StaticJsonDocument<200> doc;
    DeserializationError error = deserializeJson(doc, messageTemp);

    if (error) {
        Serial.println("❌ Lỗi đọc JSON!");
        return;
    }

    if (manualMode) {
        Serial.println("🚫 Lệnh bị bỏ qua: Đang ở chế độ THỦ CÔNG!");
        return;
    }

    if (isTestModeActive) {
        Serial.println("🚫 Lệnh bị bỏ qua: Đang trong thời gian 10s TEST!");
        return;
    }

    // Nếu Node-RED gửi {"action": "turn_on"} -> Bật đèn cổng
    if (doc.containsKey("action")) {
        String action = doc["action"].as<String>();
        if (action == "turn_on") {
            digitalWrite(PIN_GATE_LED, HIGH); // Bật Relay/Đèn
            Serial.println("💡 ĐÃ BẬT ĐÈN CỔNG!");
        } else if (action == "turn_off") {
            digitalWrite(PIN_GATE_LED, LOW);  // Tắt Relay/Đèn
            Serial.println("🌑 ĐÃ TẮT ĐÈN CỔNG!");
        }
    }
}

// Hàm kết nối WiFi
void setupWiFi() {
    delay(10);
    Serial.println();
    Serial.print("📡 Đang kết nối WiFi: ");
    Serial.println(WIFI_SSID);

    WiFi.mode(WIFI_STA);
    WiFi.begin(WIFI_SSID, WIFI_PASS);

    while (WiFi.status() != WL_CONNECTED) {
        delay(500);
        Serial.print(".");
    }

    Serial.println("\n✅ Đã kết nối WiFi!");
    Serial.print("🌐 Địa chỉ IP: ");
    Serial.println(WiFi.localIP());
}

// Hàm kết nối AWS IoT Core
void reconnectAWS() {
    while (!client.connected()) {
        Serial.print("☁️ Đang kết nối đến AWS IoT Core... ");
        
        if (client.connect(THING_NAME)) {
            Serial.println("✅ THÀNH CÔNG!");
            
            // 💡 ĐĂNG KÝ NGHE LỆNH TỪ NODE-RED (Ví dụ: oi/gate/command)
            client.subscribe("oi/gate/command"); 
            Serial.println("👂 Đang lắng nghe lệnh bật/tắt đèn cổng...");

        } else {
            Serial.print("❌ THẤT BẠI, mã lỗi (rc) = ");
            Serial.print(client.state());
            Serial.println(" - Thử lại sau 5 giây...");
            delay(5000); 
        }
    }
}

void setup() {
    Serial.begin(115200);
    
    // Khởi tạo chân Đèn Cổng (Tuyền nhớ khai báo PIN_GATE_LED trong Config.h nhé)
    pinMode(PIN_GATE_LED, OUTPUT);
    digitalWrite(PIN_GATE_LED, LOW); // Mặc định tắt khi khởi động

    // Khởi tạo nút BOOT
    pinMode(PIN_BOOT_BUTTON, INPUT_PULLUP);

    // 1. Khởi tạo Radar
    RadarSerial.begin(256000, SERIAL_8N1, PIN_RADAR_RX, PIN_RADAR_TX);
    Serial.print("\n🔍 Khởi tạo Radar LD2410... ");
    if (radar.begin(RadarSerial)) {
        Serial.println("OK!");
    } else {
        Serial.println("LỖI! Kiểm tra dây TX/RX.");
    }

    // 2. Khởi tạo WiFi & AWS
    setupWiFi();
    espClient.setCACert(AWS_CERT_CA);
    espClient.setCertificate(AWS_CERT_CRT);
    espClient.setPrivateKey(AWS_CERT_PRIVATE);
    client.setServer(MQTT_SERVER, MQTT_PORT);
    
    // Báo cho thư viện MQTT biết phải gọi hàm 'callback' khi có tin nhắn tới
    client.setCallback(callback);
}

void loop() {
    if (!client.connected()) {
        reconnectAWS();
    }
    client.loop(); 

    // --- XỬ LÝ NÚT NHẤN BOOT ---
    bool currentButtonState = digitalRead(PIN_BOOT_BUTTON);
    
    if (currentButtonState == LOW && lastButtonState == HIGH) {
        // Nút vừa được nhấn xuống
        buttonPressTime = millis();
        buttonPressed = true;
        longPressHandled = false;
        delay(50); // Chống dội phím (debounce)
    } 
    else if (currentButtonState == HIGH && lastButtonState == LOW) {
        // Nút vừa được nhả ra
        if (buttonPressed && !longPressHandled) {
            unsigned long pressDuration = millis() - buttonPressTime;
            if (pressDuration < 5000) {
                // NHẤN NHẢ (Short Press) < 5s -> Bật/tắt đèn test
                int currentLed = digitalRead(PIN_GATE_LED);
                digitalWrite(PIN_GATE_LED, !currentLed);
                Serial.println(currentLed ? "🌑 ĐÃ TẮT ĐÈN CỔNG (Nút nhấn)!" : "💡 ĐÃ BẬT ĐÈN CỔNG (Nút nhấn)!");
                
                if (!manualMode) {
                    isTestModeActive = true;
                    lastTestPressTime = millis();
                    Serial.println("⏳ Chế độ Test: Bỏ qua App trong 10 giây!");
                }
            }
        }
        buttonPressed = false;
        delay(50); // Chống dội phím
    }
    
    // Đang giữ nút
    if (buttonPressed && !longPressHandled) {
        unsigned long pressDuration = millis() - buttonPressTime;
        if (pressDuration >= 5000) {
            // NHẤN GIỮ (Long Press) >= 5s
            manualMode = !manualMode;
            longPressHandled = true;
            if (manualMode) {
                isTestModeActive = false; // Xóa trạng thái test nếu có
                Serial.println("⚠️ ĐÃ CHUYỂN SANG CHẾ ĐỘ THỦ CÔNG (Bỏ qua App hoàn toàn)!");
            } else {
                Serial.println("🔄 ĐÃ CHUYỂN VỀ CHẾ ĐỘ TỰ ĐỘNG (Nhận lệnh từ App)!");
            }
        }
    }
    
    // Kiểm tra hết hạn 10s test
    if (!manualMode && isTestModeActive) {
        if (millis() - lastTestPressTime > 10000) {
            isTestModeActive = false;
            Serial.println("✅ Hết 10 giây Test. Đã nhận lại lệnh từ App!");
        }
    }
    lastButtonState = currentButtonState;
    // ----------------------------

    // Đọc liên tục dữ liệu từ cảm biến
    radar.read();

    // Gửi dữ liệu mỗi 1 giây (1000ms)
    if (millis() - lastMsgTime > 1000) {
        lastMsgTime = millis();

        if (radar.isConnected()) {
            bool currentPresence = radar.presenceDetected();
            
            StaticJsonDocument<200> doc;
            doc["device"] = THING_NAME;
            doc["presence"] = currentPresence;
            
            if (currentPresence) {
                doc["stationary_dist"] = radar.stationaryTargetDistance();
                doc["moving_dist"] = radar.movingTargetDistance();
            } else {
                doc["stationary_dist"] = 0;
                doc["moving_dist"] = 0;
            }

            char jsonBuffer[256];
            serializeJson(doc, jsonBuffer);

            client.publish("oi/radar/status", jsonBuffer);
            
            Serial.print("📦 Đã gửi AWS: ");
            Serial.println(jsonBuffer);
        }
    }
}