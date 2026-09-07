/**
 * Config.h — Cấu hình phần cứng Node 1 (ESP32-S3 Edge AI Controller).
 *
 * FILE NÀY KHÔNG CHỨA BÍ MẬT và được commit lên git.
 * Thông tin Wi-Fi, endpoint và chứng chỉ nạp qua secrets.ini (không commit).
 * Xem firmware/node1_controller/secrets.ini.example và docs/07-security.md.
 */
#ifndef CONFIG_H
#define CONFIG_H

#include "oi_protocol.h"
#include "oi_state.h"

// ══════════════════════════════════════════════════════════════
//  Định danh
// ══════════════════════════════════════════════════════════════
#define THING_NAME       OI_NODE_CONTROLLER

// ══════════════════════════════════════════════════════════════
//  Bí mật — do secrets.ini bơm vào qua -D lúc biên dịch.
//  Giá trị mặc định bên dưới chỉ để trình soạn thảo không báo đỏ;
//  build sẽ dừng nếu thiếu (xem khối kiểm tra cuối file).
// ══════════════════════════════════════════════════════════════
#ifndef WIFI_SSID
#define WIFI_SSID        ""
#endif
#ifndef WIFI_PASS
#define WIFI_PASS        ""
#endif
#ifndef HUB_MQTT_HOST
#define HUB_MQTT_HOST    "192.168.1.10"   // broker nội bộ — đường chính
#endif
#ifndef HUB_MQTT_PORT
#define HUB_MQTT_PORT    1883
#endif
#ifndef AWS_MQTT_HOST
#define AWS_MQTT_HOST    ""               // đám mây — đường phụ, có thể bỏ trống
#endif
#define AWS_MQTT_PORT    8883

// Chứng chỉ đọc từ phân vùng NVS lúc chạy, KHÔNG biên dịch cứng vào firmware.
// Nạp bằng: python tools/provision/provision_node.py --port COMx
#define NVS_NAMESPACE    "oi_creds"
#define NVS_KEY_CA       "aws_ca"
#define NVS_KEY_CERT     "aws_cert"
#define NVS_KEY_PKEY     "aws_pkey"

// ══════════════════════════════════════════════════════════════
//  Sơ đồ chân — ESP32-S3 DevKitC-1
// ══════════════════════════════════════════════════════════════

// --- Ma trận LED WS2812B ---
#define PIN_LED          48
#define NUM_LEDS         256
#define LED_MATRIX_W     16
#define LED_MATRIX_H     16

// --- Cảm biến ánh sáng VEML7700 (I2C) ---
#define PIN_I2C_SDA      8
#define PIN_I2C_SCL      9

// --- Micro INMP441 (I2S) ---
#define PIN_I2S_SCK      41
#define PIN_I2S_WS       42
#define PIN_I2S_SD       2
#define I2S_PORT_NUM     1

// --- LED chỉ thị trạng thái nghe ---
// CẢNH BÁO: GPIO3 là chân strapping của ESP32-S3 (chọn nguồn JTAG).
// Dùng tạm được nhưng nên chuyển sang chân khác — xem docs/06-hardware.md.
#define PIN_LED_INDICATOR 3

// --- Nút bấm vật lý (INPUT_PULLUP, nhấn = LOW) ---
#define PIN_BTN_TOGGLE   4
#define PIN_BTN_UP       5
#define PIN_BTN_DOWN     6
#define PIN_BTN_COLOR    7

// ══════════════════════════════════════════════════════════════
//  Thông số tác vụ FreeRTOS — xem docs/02-architecture.md bảng 9.1
// ══════════════════════════════════════════════════════════════
#define STACK_CAPTURE    4096
#define STACK_INFERENCE  32768
#define STACK_UPLINK     8192
#define STACK_LED        8192
#define STACK_NET        10240
#define STACK_SENSORS    4096
#define STACK_BUTTON     3072

#define PRIO_CAPTURE     10
#define PRIO_INFERENCE   5
#define PRIO_UPLINK      4
#define PRIO_LED         3
#define PRIO_NET         3
#define PRIO_SENSORS     2
#define PRIO_BUTTON      2

#define CORE_AI          0
#define CORE_IO          1

#define LED_FRAME_MS     30      // ≈33 FPS
#define SENSOR_PERIOD_MS 2000
#define BUTTON_SCAN_MS   20
#define CMD_QUEUE_DEPTH  8

// ══════════════════════════════════════════════════════════════
//  Chặn build khi quên cấu hình secrets.ini
// ══════════════════════════════════════════════════════════════
#if !defined(OI_ALLOW_EMPTY_SECRETS)
  #if !__has_include("../secrets.ini")
    // PlatformIO không cho #error dựa trên nội dung ini, nên kiểm tra ở runtime:
    // Task_Network sẽ dừng và in hướng dẫn nếu WIFI_SSID rỗng.
  #endif
#endif

#endif // CONFIG_H
