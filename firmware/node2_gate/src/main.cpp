/**
 * main.cpp — Node 2 (ESP32): radar LD2410 + đèn cổng.
 *
 * HAI VẤN ĐỀ CỦA BẢN DACN, SỬA Ở G1.6
 *
 * 1. Toàn bộ logic nằm trong loop() với delay(5000) mỗi lần kết nối lại broker.
 *    Trong 5 giây đó radar KHÔNG được đọc: có người đi qua cũng không biết.
 *    Sửa: tách thành ba task độc lập. TaskRadar không bao giờ bị chặn bởi mạng.
 *
 * 2. Luật "có người → bật đèn" đi vòng qua AWS. Mất mạng là mất tự động hoá —
 *    mâu thuẫn trực tiếp với luận điểm Edge Computing của chính đề tài.
 *    Sửa: quyết định nằm hẳn trong TaskLogic. Hub chỉ được GHI ĐÈ trong
 *    HUB_OVERRIDE_TTL_MS rồi quyền tự trả về luật cục bộ.
 *
 * Kiểm chứng: rút cáp mạng, đi qua radar — đèn vẫn phải bật và tự tắt sau 30 s.
 */
#include <Arduino.h>
#include <ArduinoJson.h>
#include <PubSubClient.h>
#include <WiFi.h>
#include <ld2410.h>

#include "Config.h"
#include "oi_creds.h"
#include "oi_health.h"
#include "oi_ota.h"
#include "oi_provision.h"

OI_HEALTH_DEFINE()

// ══════════════════════════════════════════════════════════════
//  Dữ liệu đi giữa các task — chỉ qua hàng đợi, không biến dùng chung
// ══════════════════════════════════════════════════════════════
typedef struct {
    bool     presence;
    uint16_t dist_moving;   // cm
    uint16_t dist_static;   // cm
} RadarReading;

typedef struct {
    bool     on;
    uint8_t  src;       // OiCmdSource — vì sao đèn ở trạng thái này
    uint32_t seq;
} GateState;

static QueueHandle_t qRadar = NULL;   // TaskRadar → TaskLogic (ghi đè, 1 ô)
static QueueHandle_t qState = NULL;   // TaskLogic → TaskNet
static QueueHandle_t qHubCmd = NULL;  // TaskNet   → TaskLogic
static QueueHandle_t qLux = NULL;     // TaskNet   → TaskLogic (lux từ node 1)

static WiFiClient   espClient;
static PubSubClient client(espClient);

static char topicState[48], topicTele[48], topicCmd[48], topicLwt[48];

// ══════════════════════════════════════════════════════════════
//  TASK 1 — RADAR. Không bao giờ bị chặn bởi mạng.
// ══════════════════════════════════════════════════════════════
static void TaskRadar(void *pv) {
    ld2410 radar;
    HardwareSerial radarSerial(RADAR_UART_NUM);

    radarSerial.begin(RADAR_BAUD, SERIAL_8N1, PIN_RADAR_RX, PIN_RADAR_TX);
    Serial.print("🔍 Khởi tạo radar LD2410... ");
    Serial.println(radar.begin(radarSerial) ? "OK" : "LỖI (kiểm tra TX/RX)");

    for (;;) {
        radar.read();

        if (radar.isConnected()) {
            RadarReading r;
            r.presence    = radar.presenceDetected();
            r.dist_moving = r.presence ? radar.movingTargetDistance() : 0;
            r.dist_static = r.presence ? radar.stationaryTargetDistance() : 0;
            xQueueOverwrite(qRadar, &r);
        }

        vTaskDelay(RADAR_POLL_MS / portTICK_PERIOD_MS);
    }
}

// ══════════════════════════════════════════════════════════════
//  TASK 2 — LOGIC. Chủ sở hữu duy nhất của đèn cổng.
//
//  Thứ tự ưu tiên, từ cao xuống thấp:
//    1. chế độ thủ công (giữ nút BOOT 5 s)   — người dùng luôn thắng
//    2. chế độ test 10 s sau khi nhấn nhả nút
//    3. lệnh ghi đè của hub, còn hạn TTL
//    4. luật cục bộ: có người + trời tối → bật, giữ 30 s sau lần thấy cuối
// ══════════════════════════════════════════════════════════════
static void TaskLogic(void *pv) {
    pinMode(PIN_GATE_LED, OUTPUT);
    digitalWrite(PIN_GATE_LED, LOW);
    pinMode(PIN_BOOT_BUTTON, INPUT_PULLUP);

    bool     manualMode      = false;
    bool     testMode        = false;
    uint32_t testStart       = 0;

    bool     lastBtn         = HIGH;
    uint32_t btnPressAt      = 0;
    bool     longPressDone   = false;

    bool     hubWants        = false;
    uint32_t hubCmdAt        = 0;
    bool     hubActive       = false;

    float    lux             = 1000.0f;   // chưa có số đo thì coi như sáng
    uint32_t luxAt           = 0;

    uint32_t lastPresenceAt  = 0;
    bool     ledOn           = false;
    uint32_t seq             = 0;

    for (;;) {
        uint32_t now = millis();

        // ── Nút BOOT ─────────────────────────────────────────
        bool btn = digitalRead(PIN_BOOT_BUTTON);
        if (btn == LOW && lastBtn == HIGH) {
            btnPressAt    = now;
            longPressDone = false;
        } else if (btn == LOW && !longPressDone && now - btnPressAt >= LONG_PRESS_MS) {
            manualMode    = !manualMode;
            longPressDone = true;
            testMode      = false;
            Serial.println(manualMode ? "⚠️ CHẾ ĐỘ THỦ CÔNG" : "🔄 CHẾ ĐỘ TỰ ĐỘNG");
        } else if (btn == HIGH && lastBtn == LOW && !longPressDone) {
            ledOn = !ledOn;
            if (!manualMode) {
                testMode  = true;
                testStart = now;
            }
            Serial.println(ledOn ? "💡 Nút: BẬT đèn cổng" : "🌑 Nút: TẮT đèn cổng");
            digitalWrite(PIN_GATE_LED, ledOn);
            GateState gs = {ledOn, OI_SRC_BUTTON, ++seq};
            xQueueSend(qState, &gs, 0);
        }
        lastBtn = btn;

        if (testMode && now - testStart > TEST_MODE_MS) {
            testMode = false;
            Serial.println("✅ Hết 10 s test — nhận lại lệnh từ hub.");
        }

        // ── Đầu vào từ mạng ──────────────────────────────────
        bool cmd;
        if (xQueueReceive(qHubCmd, &cmd, 0) == pdTRUE) {
            hubWants  = cmd;
            hubCmdAt  = now;
            hubActive = true;
        }
        float newLux;
        if (xQueueReceive(qLux, &newLux, 0) == pdTRUE) {
            lux   = newLux;
            luxAt = now;
        }

        // Lệnh ghi đè của hub hết hạn: quyền tự trả về luật cục bộ. Không có
        // cơ chế này, hub sập giữa lúc đang ghi đè là đèn kẹt vĩnh viễn.
        if (hubActive && now - hubCmdAt > HUB_OVERRIDE_TTL_MS) {
            hubActive = false;
            Serial.println("⌛ Lệnh ghi đè của hub hết hạn — về luật cục bộ.");
        }

        // ── Luật cục bộ ──────────────────────────────────────
        RadarReading r;
        if (xQueueReceive(qRadar, &r, 0) == pdTRUE) {
            bool near = r.presence &&
                        (r.dist_moving == 0 || r.dist_moving <= GATE_MAX_DIST_CM) &&
                        (r.dist_static == 0 || r.dist_static <= GATE_MAX_DIST_CM);
            if (near) lastPresenceAt = now;
        }

        // Số đo lux quá cũ (node 1 mất kết nối) thì bỏ điều kiện trời tối:
        // thà bật thừa còn hơn để người đi trong bóng tối vì thiếu dữ liệu.
        bool luxFresh = (luxAt != 0) && (now - luxAt < 5 * 60 * 1000UL);
        bool darkEnough = !luxFresh || lux < GATE_LUX_THRESHOLD;

        bool presenceHold = (lastPresenceAt != 0) && (now - lastPresenceAt < GATE_HOLD_MS);
        bool wantOn;
        uint8_t src;

        if (manualMode || testMode) {
            wantOn = ledOn;                     // người dùng đang giữ quyền
            src    = OI_SRC_BUTTON;
        } else if (hubActive) {
            wantOn = hubWants;
            src    = OI_SRC_AGENT;
        } else {
            wantOn = presenceHold && darkEnough;
            src    = OI_SRC_AGENT;
        }

        if (wantOn != ledOn) {
            ledOn = wantOn;
            digitalWrite(PIN_GATE_LED, ledOn);
            GateState gs = {ledOn, src, ++seq};
            xQueueSend(qState, &gs, 0);
            Serial.printf("%s đèn cổng (nguồn %u)\n", ledOn ? "💡 BẬT" : "🌑 TẮT", src);
        }

        vTaskDelay(50 / portTICK_PERIOD_MS);
    }
}

// ══════════════════════════════════════════════════════════════
//  TASK 3 — MẠNG
// ══════════════════════════════════════════════════════════════
static void onMessage(char *topic, byte *payload, unsigned int length) {
    JsonDocument doc;
    if (deserializeJson(doc, payload, length)) return;

    // Lux do node 1 đo — dùng cho điều kiện "trời đủ tối" của luật cục bộ.
    if (doc[OI_K_LUX].is<float>()) {
        float lux = doc[OI_K_LUX];
        xQueueOverwrite(qLux, &lux);
        return;
    }

    if (doc[OI_K_MODE].is<int>()) {
        bool on = ((int)doc[OI_K_MODE]) != OI_MODE_OFF;
        xQueueSend(qHubCmd, &on, 0);
    }
}

static void publishState(const GateState &gs) {
    if (!client.connected()) return;

    JsonDocument doc;
    doc[OI_K_NODE]       = THING_NAME;
    doc[OI_K_PROTO]      = OI_PROTO_STR;
    doc[OI_K_MODE]       = gs.on ? (uint8_t)OI_MODE_SOLID : (uint8_t)OI_MODE_OFF;
    doc[OI_K_ZONE]       = OI_ZONE_GATE;
    doc[OI_K_BRIGHTNESS] = gs.on ? 255 : 0;
    doc[OI_K_SEQ]        = gs.seq;
    doc[OI_K_SOURCE]     = gs.src;
    doc[OI_K_TS]         = millis() / 1000;

    char buf[224];
    size_t n = serializeJson(doc, buf);
    client.publish(topicState, (const uint8_t *)buf, n, true);
}

static bool mqttConnect(void) {
    const char *willMsg = "{\"" OI_K_NODE "\":\"" THING_NAME "\",\"online\":false}";
    bool ok = client.connect(THING_NAME,
                             strlen(MQTT_USER) ? MQTT_USER : NULL,
                             strlen(MQTT_PASS) ? MQTT_PASS : NULL,
                             topicLwt, 1, true, willMsg, true);
    if (!ok) return false;

    const char *onlineMsg = "{\"" OI_K_NODE "\":\"" THING_NAME "\",\"online\":true}";
    client.publish(topicLwt, (const uint8_t *)onlineMsg, strlen(onlineMsg), true);
    client.subscribe(topicCmd, 1);

    // Nghe lux của node 1 để biết trời tối hay sáng.
    char topicNode1Tele[48];
    snprintf(topicNode1Tele, sizeof(topicNode1Tele), OI_TOPIC_TELE_FMT, OI_NODE_CONTROLLER);
    client.subscribe(topicNode1Tele, 0);

    Serial.println("🚀 Đã kết nối broker.");
    return true;
}

static void TaskNet(void *pv) {
    snprintf(topicState, sizeof(topicState), OI_TOPIC_STATE_FMT, THING_NAME);
    snprintf(topicTele,  sizeof(topicTele),  OI_TOPIC_TELE_FMT,  THING_NAME);
    snprintf(topicCmd,   sizeof(topicCmd),   OI_TOPIC_CMD_FMT,   THING_NAME);
    snprintf(topicLwt,   sizeof(topicLwt),   OI_TOPIC_LWT_FMT,   THING_NAME);

    OiCreds creds;
    if (!oiCredsLoad(creds, NVS_NAMESPACE, NVS_KEY_CA, NVS_KEY_CERT, NVS_KEY_PKEY)) {
        oiCredsPrintHint(THING_NAME);
    }

    client.setServer(HUB_MQTT_HOST, HUB_MQTT_PORT);
    client.setCallback(onMessage);
    client.setBufferSize(512);
    client.setKeepAlive(15);

    uint32_t lastHealth = 0;
    bool otaStarted = false;

    for (;;) {
        oiProvPoll(THING_NAME, NVS_NAMESPACE, NVS_KEY_CA, NVS_KEY_CERT, NVS_KEY_PKEY);

        if (WiFi.status() != WL_CONNECTED && strlen(WIFI_SSID) > 0) {
            Serial.printf("📡 Kết nối Wi-Fi %s...\n", WIFI_SSID);
            WiFi.mode(WIFI_STA);
            WiFi.begin(WIFI_SSID, WIFI_PASS);
            uint8_t tries = 0;
            while (WiFi.status() != WL_CONNECTED && tries++ < 40) {
                vTaskDelay(500 / portTICK_PERIOD_MS);
            }
            if (WiFi.status() == WL_CONNECTED && !otaStarted) {
                otaStarted = oiOtaBegin(THING_NAME, OTA_PASSWORD);
            }
        }

        if (WiFi.status() == WL_CONNECTED && !client.connected()) {
            if (!mqttConnect()) {
                oiHealthCountReconnect();
                Serial.printf("❌ Broker từ chối (rc=%d), thử lại sau 5 s.\n", client.state());
                // Chờ ở ĐÂY không còn làm nghẽn radar: đó là toàn bộ mục đích
                // của việc tách task ở G1.6.
                vTaskDelay(5000 / portTICK_PERIOD_MS);
                continue;
            }
        }

        client.loop();
        if (otaStarted) oiOtaLoop();

        GateState gs;
        while (xQueueReceive(qState, &gs, 0) == pdTRUE) publishState(gs);

        if (millis() - lastHealth >= 60000) {
            lastHealth = millis();
            if (client.connected()) {
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

                char buf[288];
                size_t n = serializeJson(doc, buf);
                client.publish(topicTele, (const uint8_t *)buf, n, false);
            }
        }

        vTaskDelay(10 / portTICK_PERIOD_MS);
    }
}

// ══════════════════════════════════════════════════════════════
void setup() {
    Serial.begin(115200);
    delay(1000);

    Serial.println("\n=============================================");
    Serial.println("🚪 KHỞI ĐỘNG NODE 2 — Radar Gate");
    Serial.printf("   Giao thức v%d.%d · lần khởi động thứ %u\n",
                  OI_PROTO_MAJOR, OI_PROTO_MINOR, oiHealthBootCount());
    Serial.println("=============================================");

    qRadar  = xQueueCreate(1, sizeof(RadarReading));
    qState  = xQueueCreate(8, sizeof(GateState));
    qHubCmd = xQueueCreate(4, sizeof(bool));
    qLux    = xQueueCreate(1, sizeof(float));

    if (!qRadar || !qState || !qHubCmd || !qLux) {
        Serial.println("❌ Không cấp phát được hàng đợi — dừng lại.");
        while (true) delay(1000);
    }

    TaskHandle_t hRadar = NULL, hLogic = NULL, hNet = NULL;
    xTaskCreatePinnedToCore(TaskRadar, "radar", STACK_RADAR, NULL, PRIO_RADAR, &hRadar, 1);
    xTaskCreatePinnedToCore(TaskLogic, "logic", STACK_LOGIC, NULL, PRIO_LOGIC, &hLogic, 1);
    xTaskCreatePinnedToCore(TaskNet,   "net",   STACK_NET,   NULL, PRIO_NET,   &hNet,   0);

    oiHealthRegister("radar", hRadar);
    oiHealthRegister("logic", hLogic);
    oiHealthRegister("net",   hNet);

    Serial.println("✅ Đã tạo 3 task.");
}

void loop() {
    vTaskDelay(portMAX_DELAY);
}
