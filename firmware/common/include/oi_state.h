/**
 * oi_state.h — Kiểu dữ liệu trạng thái đèn, chia sẻ an toàn giữa hai nhân.
 *
 * VÌ SAO FILE NÀY TỒN TẠI
 * Bản DACN dùng ba biến `String` toàn cục (cmdAction, cmdColor, cmdBrightness)
 * được ghi từ Core 0 (TaskMic) và Core 1 (callback MQTT, TaskButton), đồng thời
 * được đọc từ Core 1 (TaskLED) — không mutex, không queue, không volatile.
 * Vì `String` cấp phát động trên heap, một lần ghi trùng thời điểm với một lần
 * đọc có thể làm hỏng heap và gây reset ngẫu nhiên sau nhiều giờ chạy.
 *
 * CÁCH SỬA
 *   1. Trạng thái là struct POD — không con trỏ, không heap, sao chép nguyên khối.
 *   2. Mọi nguồn lệnh đẩy vào ĐÚNG MỘT hàng đợi FreeRTOS.
 *   3. Chỉ TaskLED được đọc hàng đợi đó và chạm vào phần cứng LED.
 * Không còn biến nào được ghi từ hai nhân.
 */
#ifndef OI_STATE_H
#define OI_STATE_H

#include <stdint.h>
#include <stdbool.h>

#include "oi_protocol.h"   // OI_ZONE_*, OI_SCENE_*

// ══════════════════════════════════════════════════════════════
//  Chế độ chiếu sáng
// ══════════════════════════════════════════════════════════════
typedef enum : uint8_t {
    OI_MODE_OFF    = 0,
    OI_MODE_SOLID  = 1,   // màu tĩnh
    OI_MODE_MUSIC  = 2,   // hiệu ứng nhạc
    OI_MODE_SCENE  = 3,   // cảnh dựng sẵn (đọc sách, xem phim, ngủ…)
    OI_MODE_FOLLOW = 4,   // bám người — điều khiển bởi hub
} OiLightMode;

// ══════════════════════════════════════════════════════════════
//  Nguồn phát sinh lệnh
//
//  KHÔNG CHỈ ĐỂ GHI LOG. Mô hình học sở thích (AI-6) dùng trường này
//  để phân loại nhãn: lệnh từ BUTTON/WEB ngay sau một lệnh AGENT
//  chính là một lần "người dùng sửa sai", tức mẫu huấn luyện trọng số 1.0.
//  Xem docs/03-ai-models.md mục M7.
// ══════════════════════════════════════════════════════════════
typedef enum : uint8_t {
    OI_SRC_BOOT        = 0,   // khôi phục lúc khởi động, không phải ý người dùng
    OI_SRC_VOICE_LOCAL = 1,   // lớp phản xạ trên chính MCU
    OI_SRC_VOICE_HUB   = 2,   // đã leo thang, hub trả lệnh về
    OI_SRC_BUTTON      = 3,   // nút bấm vật lý
    OI_SRC_WEB         = 4,   // bảng điều khiển
    OI_SRC_AGENT       = 5,   // hệ thống tự quyết, không ai ra lệnh
    OI_SRC_SCHEDULE    = 6,   // hẹn giờ
} OiCmdSource;

// ══════════════════════════════════════════════════════════════
//  Trạng thái đèn — POD, sao chép được nguyên khối
// ══════════════════════════════════════════════════════════════
typedef struct {
    OiLightMode mode;
    uint8_t     zone_mask;    // xem OI_ZONE_* trong oi_protocol.h
    uint8_t     brightness;   // 0–255, thang PWM
    uint16_t    cct;          // Kelvin
    uint32_t    rgb;          // 0xRRGGBB
    uint16_t    fade_ms;
    uint8_t     scene;        // xem OI_SCENE_* — có nghĩa khi mode = SCENE/MUSIC
    uint32_t    seq;          // tăng dần — bỏ qua lệnh cũ đến muộn
    OiCmdSource src;
    uint32_t    ts;           // epoch giây khi lệnh được sinh ra
} OiLightState;

// ══════════════════════════════════════════════════════════════
//  Giới hạn an toàn (xem docs/03-ai-models.md mục M7 — Rào an toàn)
// ══════════════════════════════════════════════════════════════

// Sàn ban đêm: không bao giờ tự tắt hẳn khi vùng đang có người sau khi trời tối.
#define OI_NIGHT_FLOOR_BRI        8

// Khoá sau can thiệp thủ công: người dùng vừa chỉnh tay thì hệ thống
// KHÔNG được tự động chỉnh lại trong khoảng này. Không gì làm người dùng
// bỏ hệ thống nhanh bằng việc nó cãi lại mình.
#define OI_MANUAL_LOCK_MS         (20 * 60 * 1000UL)

// Nhịp tối đa của thay đổi tự động, tránh đèn nhấp nháy theo nhãn AI
#define OI_AUTO_MIN_INTERVAL_MS   (2 * 60 * 1000UL)

// Trần quyền lực của mô hình: đầu ra không được lệch quá mức này so với
// quy tắc nền. Giới hạn bán kính thiệt hại khi mô hình học sai.
#define OI_MODEL_MAX_DEVIATION    60

// Chuyển cảnh tối thiểu — không nhảy bậc, không chớp
#define OI_MIN_FADE_MS            500

// ══════════════════════════════════════════════════════════════
//  Giới hạn công suất — bản DACN thiếu hoàn toàn phần này.
//  256 bóng WS2812B × trắng × độ sáng 255 rút khoảng 15 A.
// ══════════════════════════════════════════════════════════════
#define OI_PSU_VOLTS              5
#define OI_PSU_MILLIAMPS          16000   // để dư biên so với nguồn 20 A

// ══════════════════════════════════════════════════════════════
//  Trạng thái lúc vừa cấp nguồn: đèn tắt, chưa ai ra lệnh.
//  Đặt cuối file vì cần các hằng số phía trên.
// ══════════════════════════════════════════════════════════════
static inline OiLightState oiStateBoot(void) {
    OiLightState s;
    s.mode       = OI_MODE_OFF;
    s.zone_mask  = OI_ZONE_ALL;
    s.brightness = 0;
    s.cct        = 4000;
    s.rgb        = 0xFFFFFF;
    s.fade_ms    = OI_MIN_FADE_MS;
    s.scene      = OI_SCENE_NONE;
    s.seq        = 0;
    s.src        = OI_SRC_BOOT;
    s.ts         = 0;
    return s;
}

#endif // OI_STATE_H
