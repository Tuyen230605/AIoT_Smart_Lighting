/**
 * oi_cmdbus.h — Đường đi DUY NHẤT của mọi lệnh điều khiển đèn (G1.1).
 *
 * VẤN ĐỀ ĐANG SỬA
 * Bản DACN dùng ba biến `String` toàn cục (cmdAction, cmdColor, cmdBrightness):
 * ghi từ Core 0 (TaskMic) và Core 1 (callback MQTT, TaskButton), đọc từ Core 1
 * (TaskLED), không mutex, không queue. `String` cấp phát trên heap nên một lần
 * ghi trùng thời điểm với một lần đọc có thể làm hỏng heap — biểu hiện là reset
 * ngẫu nhiên sau nhiều giờ, đúng loại lỗi không bao giờ lộ ra trong 10 phút thử.
 *
 * VÌ SAO QUEUE CHỨ KHÔNG PHẢI MUTEX
 * Mutex chỉ chống hỏng dữ liệu, không chống *mất cập nhật*. Hai nút tăng sáng
 * bấm sát nhau, cả hai cùng đọc "100" rồi cùng ghi "115" — mutex vẫn cho ra 115
 * thay vì 130. Hàng đợi lệnh loại bỏ hẳn lớp lỗi này: không ai đọc trạng thái
 * để tính toán nữa, mọi nguồn chỉ *mô tả ý định* ("tăng 15"), và một chủ sở hữu
 * duy nhất (TaskLED) áp dụng tuần tự.
 *
 * LUẬT BẤT DI BẤT DỊCH
 *   1. Chỉ TaskLED được sở hữu và sửa OiLightState, chỉ TaskLED chạm phần cứng LED.
 *   2. Mọi nguồn lệnh (giọng nói, nút bấm, MQTT) chỉ được gọi oiCmdPost().
 *   3. Không biến trạng thái đèn nào được chia sẻ giữa hai nhân nữa.
 *   4. Sau khi áp dụng, TaskLED đẩy trạng thái sang oiStateQueue để TaskNetwork
 *      phát lên MQTT (G1.5) — chỉ một task nói chuyện với PubSubClient, vì thư
 *      viện này không an toàn đa luồng.
 */
#ifndef OI_CMDBUS_H
#define OI_CMDBUS_H

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

#include "oi_state.h"

// ══════════════════════════════════════════════════════════════
//  Loại lệnh
//
//  Phân biệt lệnh TUYỆT ĐỐI (đặt hẳn trạng thái) với lệnh TƯƠNG ĐỐI
//  (thay đổi so với hiện tại). Đây chính là chỗ sửa lỗi mất cập nhật:
//  "tăng sáng" đi qua đường dây dưới dạng ý định "+15", không phải dưới
//  dạng con số 115 đã tính sẵn bởi một task không sở hữu trạng thái.
// ══════════════════════════════════════════════════════════════
typedef enum : uint8_t {
    OI_CMD_SET        = 0,   // đặt toàn bộ trạng thái theo `state`
    OI_CMD_ADJUST_BRI = 1,   // cộng `delta` vào độ sáng, giữ nguyên chế độ/màu
    OI_CMD_TOGGLE     = 2,   // đang tắt thì bật, đang bật thì tắt
    OI_CMD_NEXT_COLOR = 3,   // xoay sang màu kế tiếp trong danh sách
} OiCmdKind;

typedef struct {
    OiCmdKind    kind;
    OiCmdSource  src;     // luôn phải điền — AI-6 dùng để gán nhãn huấn luyện
    int16_t      delta;   // chỉ dùng cho OI_CMD_ADJUST_BRI
    OiLightState state;   // chỉ dùng cho OI_CMD_SET
} OiCommand;

// ══════════════════════════════════════════════════════════════
//  Hai hàng đợi của hệ thống
//    oiCmdQueue   : mọi nguồn  → TaskLED     (lệnh vào)
//    oiStateQueue : TaskLED    → TaskNetwork (trạng thái đã áp dụng, để phát đi)
// ══════════════════════════════════════════════════════════════
extern QueueHandle_t oiCmdQueue;
extern QueueHandle_t oiStateQueue;

// Định nghĩa hai biến trên. Gọi ĐÚNG MỘT LẦN, trong đúng một file .cpp.
#define OI_CMDBUS_DEFINE()          \
    QueueHandle_t oiCmdQueue   = NULL; \
    QueueHandle_t oiStateQueue = NULL;

/** Tạo hai hàng đợi. Phải gọi trong setup() TRƯỚC khi tạo bất kỳ task nào. */
static inline bool oiCmdBusInit(uint8_t depth) {
    oiCmdQueue   = xQueueCreate(depth, sizeof(OiCommand));
    oiStateQueue = xQueueCreate(depth, sizeof(OiLightState));
    return oiCmdQueue != NULL && oiStateQueue != NULL;
}

/**
 * Gửi một lệnh. An toàn khi gọi từ bất kỳ nhân nào.
 *
 * Không chờ khi hàng đợi đầy (timeout 0): thà bỏ một lệnh còn hơn treo task
 * gọi nó — TaskMic bị treo là mất luôn khả năng nghe, cái giá cao hơn nhiều
 * so với mất một lần bấm nút trong tình huống đã quá tải.
 */
static inline bool oiCmdPost(const OiCommand &cmd) {
    if (oiCmdQueue == NULL) return false;
    return xQueueSend(oiCmdQueue, &cmd, 0) == pdTRUE;
}

/** Lệnh tương đối: đổi độ sáng đi `delta` bậc. */
static inline bool oiCmdPostAdjust(int16_t delta, OiCmdSource src) {
    OiCommand c = {};
    c.kind  = OI_CMD_ADJUST_BRI;
    c.src   = src;
    c.delta = delta;
    return oiCmdPost(c);
}

/** Lệnh không tham số: bật/tắt, đổi màu. */
static inline bool oiCmdPostSimple(OiCmdKind kind, OiCmdSource src) {
    OiCommand c = {};
    c.kind = kind;
    c.src  = src;
    return oiCmdPost(c);
}

/** Lệnh tuyệt đối: đặt hẳn trạng thái mới. */
static inline bool oiCmdPostState(const OiLightState &st) {
    OiCommand c = {};
    c.kind  = OI_CMD_SET;
    c.src   = st.src;
    c.state = st;
    return oiCmdPost(c);
}

#endif // OI_CMDBUS_H
