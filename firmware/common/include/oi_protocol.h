/**
 * oi_protocol.h — Hợp đồng giao tiếp giữa các node và hub.
 *
 * ĐÂY LÀ NGUỒN SỰ THẬT DUY NHẤT cho tên chủ đề MQTT và khoá JSON.
 * Bản song sinh phía Python: hub/services/oi_common/protocol.py
 *
 * QUY TẮC: mọi thay đổi ở file này PHẢI được nhân bản sang protocol.py
 * trong cùng một commit. Kiểm thử hub/tests/test_protocol_parity.py
 * sẽ báo lỗi nếu hai bên lệch nhau.
 */
#ifndef OI_PROTOCOL_H
#define OI_PROTOCOL_H

#include <stdint.h>

// ══════════════════════════════════════════════════════════════
//  Phiên bản giao thức
//  Tăng MINOR khi thêm trường (tương thích ngược).
//  Tăng MAJOR khi đổi/xoá trường (phá vỡ tương thích) — node và hub
//  khác MAJOR phải từ chối nói chuyện với nhau.
// ══════════════════════════════════════════════════════════════
#define OI_PROTO_MAJOR 1
#define OI_PROTO_MINOR 0

// ══════════════════════════════════════════════════════════════
//  Định danh node
// ══════════════════════════════════════════════════════════════
#define OI_NODE_CONTROLLER "node1"   // ESP32-S3: giọng nói, LED, cảm biến
#define OI_NODE_GATE       "node2"   // ESP32: radar, đèn cổng
#define OI_NODE_SPOTLIGHT  "node3"   // ESP32: đèn rọi pan/tilt

// ══════════════════════════════════════════════════════════════
//  Cây chủ đề MQTT
//
//  Hướng    : ai gửi → ai nhận
//  QoS      : 0 = có thể mất, 1 = đảm bảo đến ít nhất một lần
//  Retain   : broker giữ lại bản tin cuối để client mới vào biết ngay
// ══════════════════════════════════════════════════════════════

// hub → node · QoS 1 · lệnh chấp hành
#define OI_TOPIC_CMD_FMT        "oi/cmd/%s"

// node → hub · QoS 1 · RETAIN · phát sau MỌI thay đổi, bất kể nguồn lệnh
// Đây là cơ chế sửa lỗi mất đồng bộ của bản DACN.
#define OI_TOPIC_STATE_FMT      "oi/state/%s"

// node → hub · QoS 0 · lux, RSSI, heap còn trống, uptime
#define OI_TOPIC_TELE_FMT       "oi/tele/%s"

// node → hub · QoS 1 · sự kiện thức + độ tin cậy + có leo thang hay không
#define OI_TOPIC_WAKE_FMT       "oi/wake/%s"

// hub nội bộ
#define OI_TOPIC_TRANSCRIPT     "oi/nlu/transcript"   // asr → agent   · QoS 1
#define OI_TOPIC_CTX_VISION     "oi/ctx/vision"       // vision → ctx  · QoS 0
#define OI_TOPIC_CTX_FUSED      "oi/ctx/fused"        // ctx → mọi     · QoS 0 · RETAIN
#define OI_TOPIC_AGENT_LOG      "oi/agent/log"        // agent → mọi   · QoS 1

// hub → node · QoS 0 · 1 Hz. Mất 8 nhịp liên tiếp → node vào FALLBACK.
#define OI_TOPIC_HEARTBEAT      "oi/sys/heartbeat"
#define OI_HEARTBEAT_PERIOD_MS  1000
#define OI_HEARTBEAT_MISS_LIMIT 8

// node → hub · Last Will · broker tự phát khi node mất kết nối đột ngột
#define OI_TOPIC_LWT_FMT        "oi/sys/offline/%s"

// ══════════════════════════════════════════════════════════════
//  Khoá JSON — chung cho mọi bản tin
// ══════════════════════════════════════════════════════════════
#define OI_K_TS          "ts"        // uint32, epoch giây
#define OI_K_SEQ         "seq"       // uint32, tăng dần — phát hiện lệnh đến muộn
#define OI_K_NODE        "node"      // chuỗi, xem OI_NODE_*
#define OI_K_PROTO       "proto"     // "1.0"

// --- Lệnh và trạng thái đèn ---
#define OI_K_MODE        "mode"      // xem OiLightMode
#define OI_K_ZONE        "zone"      // uint8 bitmask, xem OI_ZONE_*
#define OI_K_BRIGHTNESS  "bri"       // uint8 0–255, thang PWM
#define OI_K_CCT         "cct"       // uint16 Kelvin, 2200–6500
#define OI_K_COLOR       "rgb"       // uint32 0xRRGGBB
#define OI_K_FADE_MS     "fade"      // uint16, thời gian chuyển cảnh
#define OI_K_SOURCE      "src"       // xem OiCmdSource — AI-6 cần trường này
#define OI_K_REASON      "why"       // chuỗi ngắn, phục vụ nhật ký giải thích

// --- Đo đạc ---
#define OI_K_LUX         "lux"
#define OI_K_RSSI        "rssi"
#define OI_K_HEAP        "heap"
#define OI_K_UPTIME      "up"

// --- Radar (node2) ---
#define OI_K_PRESENCE    "pres"
#define OI_K_DIST_MOVING "d_mov"
#define OI_K_DIST_STATIC "d_sta"

// --- Sự kiện thức (node1) ---
#define OI_K_KEYWORD     "kw"
#define OI_K_CONF        "conf"      // float 0..1
#define OI_K_ESCALATED   "esc"       // bool — có chuyển lên hub không

// --- Đèn rọi (node3) ---
#define OI_K_PAN         "pan"       // độ
#define OI_K_TILT        "tilt"      // độ
#define OI_K_TRACK_MODE  "trk"       // 1=bám liên tục 2=theo vùng 3=nền

// ══════════════════════════════════════════════════════════════
//  Vùng chức năng trong phòng — bitmask, một đèn có thể phủ nhiều vùng
// ══════════════════════════════════════════════════════════════
#define OI_ZONE_NONE     0x00
#define OI_ZONE_DESK     0x01   // bàn làm việc
#define OI_ZONE_SEAT     0x02   // ghế đọc sách
#define OI_ZONE_WALK     0x04   // lối đi
#define OI_ZONE_BED      0x08   // giường
#define OI_ZONE_GATE     0x10   // cổng (node2)
#define OI_ZONE_ALL      0x1F

// ══════════════════════════════════════════════════════════════
//  Ngưỡng của giao thức leo thang (xem docs/04-protocols.md)
// ══════════════════════════════════════════════════════════════
#define OI_CONF_WAKE_MIN      0.90f  // ngưỡng nhận từ khoá thức — đặt cao để chống thức nhầm
#define OI_CONF_CMD_MIN       0.75f  // ngưỡng lệnh sau khi đã thức — hạ vì đã có ngữ cảnh
#define OI_ESCALATE_BELOW     0.75f  // dưới ngưỡng này thì chuyển việc lên hub
#define OI_UTTERANCE_LONG_MS  1500   // nói dài hơn mọi từ khoá đã học → chắc chắn là câu tự do
#define OI_PREROLL_MS         500    // gửi kèm 500 ms TRƯỚC thời điểm kích hoạt, tránh mất đầu câu
#define OI_HUB_TIMEOUT_MS     8000   // hub không trả lời trong 8 s → về FALLBACK
#define OI_LISTEN_WINDOW_MS   10000  // cửa sổ nghe lệnh sau khi thức

#endif // OI_PROTOCOL_H
