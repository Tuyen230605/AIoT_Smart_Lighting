/**
 * Config.h — Cấu hình phần cứng Node 2 (ESP32 Radar Gate Controller).
 *
 * FILE NÀY KHÔNG CHỨA BÍ MẬT và được commit lên git.
 *
 * ⚠ LỊCH SỬ: bản DACN của file này từng chứa nguyên khoá riêng AWS và mật khẩu
 * Wi-Fi dạng chữ thường, và đã bị đẩy lên một repo công khai. Toàn bộ đã được
 * gỡ bỏ ở đây. Xem docs/07-security.md để biết các bước thu hồi bắt buộc.
 */
#ifndef CONFIG_H
#define CONFIG_H

#include "oi_protocol.h"
#include "oi_state.h"

#define THING_NAME       OI_NODE_GATE

// ══════════════════════════════════════════════════════════════
//  Bí mật — bơm từ secrets.ini lúc biên dịch; chứng chỉ đọc từ NVS.
// ══════════════════════════════════════════════════════════════
#ifndef WIFI_SSID
#define WIFI_SSID        ""
#endif
#ifndef WIFI_PASS
#define WIFI_PASS        ""
#endif
#ifndef HUB_MQTT_HOST
#define HUB_MQTT_HOST    "192.168.1.10"
#endif
#ifndef HUB_MQTT_PORT
#define HUB_MQTT_PORT    1883
#endif
#ifndef AWS_MQTT_HOST
#define AWS_MQTT_HOST    ""
#endif
#define AWS_MQTT_PORT    8883

#define NVS_NAMESPACE    "oi_creds"
#define NVS_KEY_CA       "aws_ca"
#define NVS_KEY_CERT     "aws_cert"
#define NVS_KEY_PKEY     "aws_pkey"

// ══════════════════════════════════════════════════════════════
//  Sơ đồ chân — ESP32 DevKit
// ══════════════════════════════════════════════════════════════
#define PIN_RADAR_RX     16
#define PIN_RADAR_TX     17
#define RADAR_BAUD       256000
#define RADAR_UART_NUM   2

#define PIN_GATE_LED     2      // relay / đèn cổng
#define PIN_BOOT_BUTTON  0      // nút BOOT — test 10 s và chế độ thủ công

// ══════════════════════════════════════════════════════════════
//  Luật tự động hoá CỤC BỘ
//
//  Bản DACN để logic "có người → bật đèn" chạy vòng qua AWS, nên mất mạng
//  là mất tự động hoá — mâu thuẫn với chính luận điểm Edge Computing của đề tài.
//  Từ bản này, quyết định nằm hẳn trong firmware; hub chỉ được ghi đè
//  trong một khoảng có hạn rồi tự trả quyền về cho luật cục bộ.
// ══════════════════════════════════════════════════════════════
#define GATE_HOLD_MS         30000   // giữ đèn 30 s sau lần phát hiện cuối
#define GATE_LUX_THRESHOLD   50      // chỉ bật khi trời đủ tối
#define GATE_MAX_DIST_CM     400     // bỏ qua mục tiêu xa hơn mức này
#define HUB_OVERRIDE_TTL_MS  300000  // lệnh ghi đè của hub hết hiệu lực sau 5 phút

#define TEST_MODE_MS         10000   // nhấn nhả: 10 s bỏ qua lệnh từ xa
#define LONG_PRESS_MS        5000    // nhấn giữ: bật/tắt chế độ thủ công

// ══════════════════════════════════════════════════════════════
//  Thông số tác vụ FreeRTOS
//  Bản DACN đặt toàn bộ trong loop() với delay(5000) khi kết nối lại,
//  làm nghẽn việc đọc radar. Từ bản này chia thành các task riêng.
// ══════════════════════════════════════════════════════════════
#define STACK_RADAR      4096
#define STACK_NET        10240
#define STACK_LOGIC      4096

#define PRIO_RADAR       4
#define PRIO_LOGIC       3
#define PRIO_NET         3

#define RADAR_POLL_MS    100
#define TELE_PERIOD_MS   1000

#endif // CONFIG_H
