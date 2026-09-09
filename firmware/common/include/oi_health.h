/**
 * oi_health.h — Thiết bị đo cho bài chạy liên tục 72 giờ (G1.8).
 *
 * VÌ SAO CẦN
 * Rò rỉ bộ nhớ và tương tranh không lộ ra trong mười phút thử nghiệm; chúng lộ
 * ra sau nhiều giờ. Cổng chất lượng cuối G1 đòi ba bằng chứng, và cả ba đều
 * phải do chính firmware tự ghi thì mới đo được liên tục 72 giờ:
 *
 *   heap_min  phẳng            → không rò rỉ bộ nhớ
 *   stack     mọi task dư ≥25% → không tràn ngăn xếp (NFR-05)
 *   boots     không tăng       → không có reset ngoài ý muốn
 *
 * Số lần boot phải sống qua reset nên nằm trong NVS. Ba chỉ số còn lại đọc
 * trực tiếp từ FreeRTOS nên không tốn gì.
 */
#ifndef OI_HEALTH_H
#define OI_HEALTH_H

#include <Arduino.h>
#include <Preferences.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#ifndef OI_HEALTH_MAX_TASKS
#define OI_HEALTH_MAX_TASKS 8
#endif

typedef struct {
    const char *name;
    TaskHandle_t handle;
} OiTaskSlot;

typedef struct {
    OiTaskSlot tasks[OI_HEALTH_MAX_TASKS];
    uint8_t    count;
    uint32_t   boots;
    uint32_t   reconnects;
} OiHealth;

extern OiHealth oiHealth;

// Định nghĩa biến trên. Gọi ĐÚNG MỘT LẦN, trong đúng một file .cpp.
#define OI_HEALTH_DEFINE() OiHealth oiHealth = {};

/**
 * Tăng và đọc bộ đếm số lần khởi động. Gọi một lần trong setup().
 *
 * Dùng namespace NVS riêng ("oi_health") ở phân vùng nvs mặc định, tách hẳn
 * khỏi phân vùng chứng chỉ oi_creds: xoá số liệu đo không được đụng tới
 * chứng chỉ, và nạp lại chứng chỉ không được xoá lịch sử đo.
 */
static inline uint32_t oiHealthBootCount(void) {
    Preferences p;
    if (!p.begin("oi_health", false)) return 0;
    uint32_t boots = p.getULong("boots", 0) + 1;
    p.putULong("boots", boots);
    p.end();
    oiHealth.boots = boots;
    return boots;
}

/** Đăng ký một task để theo dõi mức dùng ngăn xếp. Gọi sau xTaskCreate...(). */
static inline void oiHealthRegister(const char *name, TaskHandle_t h) {
    if (oiHealth.count >= OI_HEALTH_MAX_TASKS || h == NULL) return;
    oiHealth.tasks[oiHealth.count].name   = name;
    oiHealth.tasks[oiHealth.count].handle = h;
    oiHealth.count++;
}

static inline void oiHealthCountReconnect(void) { oiHealth.reconnects++; }

/**
 * Số WORD còn dư của ngăn xếp một task (1 word = 4 byte trên ESP32).
 * Càng gần 0 càng sắp tràn.
 */
static inline uint32_t oiHealthStackFree(uint8_t idx) {
    if (idx >= oiHealth.count) return 0;
    return (uint32_t)uxTaskGetStackHighWaterMark(oiHealth.tasks[idx].handle);
}

#endif // OI_HEALTH_H
