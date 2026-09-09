/**
 * Task_Network.cpp — Kênh liên lạc duy nhất của node 1 với thế giới bên ngoài.
 *
 * G1.5 — ĐỒNG BỘ TRẠNG THÁI HAI CHIỀU
 * Bản DACN chỉ publish giá trị lux. Bật đèn bằng giọng nói hay nút bấm thì
 * không có gì báo lên, nên bảng điều khiển hiển thị sai — Hình 3.11 của báo
 * cáo DACN mô tả luồng này nhưng mã nguồn chưa từng làm. Quan trọng hơn: mô
 * hình học sở thích (AI-6) bắt buộc phải thấy mọi thay đổi KÈM NGUỒN LỆNH, nếu
 * không sẽ không có gì để học — một lệnh BUTTON đến ngay sau một lệnh AGENT
 * chính là một lần người dùng sửa sai, tức mẫu huấn luyện quý nhất.
 *
 * Từ bản này: publish oi/state/node1 có retain sau MỌI thay đổi, kèm `src` và
 * `seq`, cộng Last Will để hub biết ngay khi node rớt đột ngột.
 *
 * VÌ SAO CHỈ TASK NÀY ĐƯỢC GỌI MQTT
 * PubSubClient không an toàn đa luồng. Trạng thái đi vào đây qua oiStateQueue,
 * lệnh đi ra qua oiCmdQueue — không task nào khác chạm vào `client`.
 */
#include <Arduino.h>
#include <ArduinoJson.h>
#include <PubSubClient.h>
#include <WiFi.h>
#include <WiFiClient.h>

#include "Config.h"
#include "Node1.h"
#include "oi_cmdbus.h"
#include "oi_creds.h"
#include "oi_health.h"
#include "oi_ota.h"
#include "oi_provision.h"

// Broker nội bộ trong mạng LAN là kênh liên lạc DUY NHẤT (ADR 0003 + 0004).
// Không còn đường ra đám mây nào: hệ thống chạy trọn vẹn trong nhà, và truy cập
// từ xa — nếu cần — đi qua đường hầm mạng riêng cài trên hub, không qua firmware.
// Cổng 1883 không TLS dùng được ngay; khi Mosquitto bật TLS ở G2.1 thì chuyển
// sang WiFiClientSecure với chứng chỉ đọc từ NVS.
static WiFiClient   net;
static PubSubClient client(net);

static char topicState[48];
static char topicTele[48];
static char topicCmd[48];
static char topicLwt[48];

static uint32_t lastRemoteSeq = 0;

// ══════════════════════════════════════════════════════════════
//  Nhận lệnh từ hub
// ══════════════════════════════════════════════════════════════
static void onMessage(char *topic, byte *payload, unsigned int length) {
    JsonDocument doc;
    if (deserializeJson(doc, payload, length)) {
        Serial.println("❌ Lệnh vào không phải JSON hợp lệ — bỏ qua.");
        return;
    }

    OiLightState s = oiStateBoot();
    s.mode       = (OiLightMode)(doc[OI_K_MODE]       | (int)OI_MODE_SOLID);
    s.brightness = (uint8_t)(doc[OI_K_BRIGHTNESS]     | 100);
    s.cct        = (uint16_t)(doc[OI_K_CCT]           | 4000);
    s.rgb        = (uint32_t)(doc[OI_K_COLOR]         | 0xFFFFFF);
    s.fade_ms    = (uint16_t)(doc[OI_K_FADE_MS]       | OI_MIN_FADE_MS);
    s.scene      = (uint8_t)(doc[OI_K_SCENE]          | OI_SCENE_NONE);
    s.zone_mask  = (uint8_t)(doc[OI_K_ZONE]           | OI_ZONE_ALL);
    s.seq        = (uint32_t)(doc[OI_K_SEQ]           | 0);

    // Nguồn lệnh do hub khai báo. Mặc định VOICE_HUB (đã leo thang) chứ không
    // phải AGENT: gán nhầm nhãn "hệ thống tự quyết" cho một lệnh do người dùng
    // nói ra sẽ đầu độc dữ liệu huấn luyện của AI-6.
    s.src = (OiCmdSource)(doc[OI_K_SOURCE] | (int)OI_SRC_VOICE_HUB);

    if (s.seq != 0 && s.seq <= lastRemoteSeq) {
        Serial.printf("↩ Bỏ lệnh hub đến muộn (seq %u ≤ %u)\n", s.seq, lastRemoteSeq);
        return;
    }
    if (s.seq != 0) lastRemoteSeq = s.seq;

    oiCmdPostState(s);
}

// ══════════════════════════════════════════════════════════════
//  Phát trạng thái — trái tim của G1.5
// ══════════════════════════════════════════════════════════════
static void publishState(const OiLightState &s) {
    if (!client.connected()) return;

    JsonDocument doc;
    doc[OI_K_NODE]       = THING_NAME;
    doc[OI_K_PROTO]      = OI_PROTO_STR;
    doc[OI_K_MODE]       = (uint8_t)s.mode;
    doc[OI_K_ZONE]       = s.zone_mask;
    doc[OI_K_BRIGHTNESS] = s.brightness;
    doc[OI_K_CCT]        = s.cct;
    doc[OI_K_COLOR]      = s.rgb;
    doc[OI_K_FADE_MS]    = s.fade_ms;
    doc[OI_K_SCENE]      = s.scene;
    doc[OI_K_SEQ]        = s.seq;
    doc[OI_K_SOURCE]     = (uint8_t)s.src;
    doc[OI_K_TS]         = s.ts;

    char buf[256];
    size_t n = serializeJson(doc, buf);

    // retain = true: dịch vụ nào vừa khởi động cũng biết ngay đèn đang thế nào
    // mà không phải hỏi. Đây là điều kiện để oi-state ở G2.2 có nguồn sự thật.
    client.publish(topicState, (const uint8_t *)buf, n, true);
    Serial.printf("📤 state → %s\n", buf);
}

static void publishLux(float lux) {
    if (!client.connected()) return;

    JsonDocument doc;
    doc[OI_K_NODE] = THING_NAME;
    doc[OI_K_LUX]  = lux;
    doc[OI_K_RSSI] = WiFi.RSSI();

    char buf[128];
    size_t n = serializeJson(doc, buf);
    client.publish(topicTele, (const uint8_t *)buf, n, false);
}

/** Số liệu cho bài chạy 72 giờ (G1.8). */
static void publishHealth(void) {
    if (!client.connected()) return;

    JsonDocument doc;
    doc[OI_K_NODE]       = THING_NAME;
    doc[OI_K_HEAP]       = ESP.getFreeHeap();
    doc[OI_K_HEAP_MIN]   = ESP.getMinFreeHeap();
    doc[OI_K_UPTIME]     = millis() / 1000;
    doc[OI_K_RSSI]       = WiFi.RSSI();
    doc[OI_K_BOOTS]      = oiHealth.boots;
    doc[OI_K_RECONNECTS] = oiHealth.reconnects;

    JsonObject stack = doc[OI_K_STACK].to<JsonObject>();
    for (uint8_t i = 0; i < oiHealth.count; i++) {
        stack[oiHealth.tasks[i].name] = oiHealthStackFree(i);
    }

    char buf[320];
    size_t n = serializeJson(doc, buf);
    client.publish(topicTele, (const uint8_t *)buf, n, false);
    Serial.printf("🩺 health → %s\n", buf);
}

// ══════════════════════════════════════════════════════════════
//  Kết nối
// ══════════════════════════════════════════════════════════════
static void ensureWiFi(void) {
    if (WiFi.status() == WL_CONNECTED) return;

    if (strlen(WIFI_SSID) == 0) {
        Serial.println("❌ Chưa cấu hình WIFI_SSID. Copy secrets.ini.example → secrets.ini.");
        vTaskDelay(10000 / portTICK_PERIOD_MS);
        return;
    }

    Serial.printf("📡 Kết nối Wi-Fi %s...\n", WIFI_SSID);
    WiFi.mode(WIFI_STA);
    WiFi.begin(WIFI_SSID, WIFI_PASS);

    // Chờ bằng vTaskDelay chứ không delay(): các task khác vẫn phải chạy.
    // Đây là lý do nút bấm và giọng nói vẫn hoạt động khi mất mạng (mức L3).
    uint8_t tries = 0;
    while (WiFi.status() != WL_CONNECTED && tries++ < 40) {
        vTaskDelay(500 / portTICK_PERIOD_MS);
    }
    if (WiFi.status() == WL_CONNECTED) {
        Serial.printf("✅ Wi-Fi ok, IP %s\n", WiFi.localIP().toString().c_str());
        oiOtaBegin(THING_NAME, OTA_PASSWORD);
    }
}

static bool mqttConnect(void) {
    // Last Will: broker tự phát bản tin này nếu node biến mất mà không chào.
    // Không có nó, hub không phân biệt được "node im lặng" với "node đã chết".
    const char *willMsg = "{\"" OI_K_NODE "\":\"" THING_NAME "\",\"online\":false}";

    bool ok = client.connect(THING_NAME,
                             strlen(MQTT_USER) ? MQTT_USER : NULL,
                             strlen(MQTT_PASS) ? MQTT_PASS : NULL,
                             topicLwt, 1, true, willMsg, true);
    if (!ok) return false;

    Serial.println("🚀 Đã kết nối broker.");
    const char *onlineMsg = "{\"" OI_K_NODE "\":\"" THING_NAME "\",\"online\":true}";
    client.publish(topicLwt, (const uint8_t *)onlineMsg, strlen(onlineMsg), true);
    client.subscribe(topicCmd, 1);
    return true;
}

void TaskNetwork(void *pvParameters) {
    snprintf(topicState, sizeof(topicState), OI_TOPIC_STATE_FMT, THING_NAME);
    snprintf(topicTele,  sizeof(topicTele),  OI_TOPIC_TELE_FMT,  THING_NAME);
    snprintf(topicCmd,   sizeof(topicCmd),   OI_TOPIC_CMD_FMT,   THING_NAME);
    snprintf(topicLwt,   sizeof(topicLwt),   OI_TOPIC_LWT_FMT,   THING_NAME);

    // Chứng chỉ dành cho MQTT-over-TLS tới broker nội bộ (bật ở G2.1).
    // Chưa có thì vẫn chạy đủ chức năng qua cổng 1883 trong mạng nhà.
    OiCreds creds;
    if (oiCredsLoad(creds, NVS_NAMESPACE, NVS_KEY_CA, NVS_KEY_CERT, NVS_KEY_PKEY)) {
        Serial.println("🔐 Đã có chứng chỉ TLS trong NVS — sẵn sàng bật TLS ở G2.1.");
    } else {
        oiCredsPrintHint(THING_NAME);
    }

    client.setServer(HUB_MQTT_HOST, HUB_MQTT_PORT);
    client.setCallback(onMessage);
    client.setBufferSize(512);
    client.setKeepAlive(15);

    uint32_t lastHealth = 0;

    for (;;) {
        // Luôn lắng nghe lệnh cấp phát chứng chỉ qua USB, kể cả khi đang mất
        // mạng — cấp phát không được phụ thuộc vào thứ mà nó sinh ra để dùng.
        oiProvPoll(THING_NAME, NVS_NAMESPACE, NVS_KEY_CA, NVS_KEY_CERT, NVS_KEY_PKEY);

        ensureWiFi();

        if (WiFi.status() == WL_CONNECTED && !client.connected()) {
            if (!mqttConnect()) {
                oiHealthCountReconnect();
                Serial.printf("❌ Broker từ chối (rc=%d), thử lại sau 5 s.\n", client.state());
                vTaskDelay(5000 / portTICK_PERIOD_MS);
                continue;
            }
        }

        client.loop();
        oiOtaLoop();

        // Mọi thay đổi trạng thái do TaskLED áp dụng đều đi ra ngoài ở đây.
        OiLightState st;
        while (xQueueReceive(oiStateQueue, &st, 0) == pdTRUE) publishState(st);

        float lux;
        if (xQueueReceive(oiLuxQueue, &lux, 0) == pdTRUE) publishLux(lux);

        if (millis() - lastHealth >= HEALTH_PERIOD_MS) {
            lastHealth = millis();
            publishHealth();
        }

        vTaskDelay(10 / portTICK_PERIOD_MS);
    }
}
