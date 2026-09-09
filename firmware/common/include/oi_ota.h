/**
 * oi_ota.h — Cập nhật firmware qua Wi-Fi (G1.7).
 *
 * VÌ SAO CẦN
 * 18 tuần tới sẽ nạp lại firmware hàng trăm lần, và ba node sẽ được gắn cố định
 * lên tường. Bảng phân vùng đã có sẵn hai slot app0/app1 cho việc này.
 *
 * VÌ SAO BẮT BUỘC CÓ MẬT KHẨU
 * OTA không mật khẩu nghĩa là bất kỳ ai trong mạng Wi-Fi cũng nạp được firmware
 * tuỳ ý vào thiết bị. Sau sự cố ở G0.1, mặc định của dự án này là: thiếu bí mật
 * thì TẮT tính năng, không phải chạy tính năng ở chế độ mở.
 */
#ifndef OI_OTA_H
#define OI_OTA_H

#include <Arduino.h>
#include <ArduinoOTA.h>

/**
 * Bật OTA. Bỏ qua (và nói rõ lý do) khi chưa đặt OTA_PASSWORD trong secrets.ini.
 * Gọi sau khi Wi-Fi đã kết nối.
 */
static inline bool oiOtaBegin(const char *hostname, const char *password) {
    if (password == NULL || strlen(password) == 0) {
        Serial.println("⚠ OTA tắt: chưa đặt OTA_PASSWORD trong secrets.ini.");
        return false;
    }

    ArduinoOTA.setHostname(hostname);
    ArduinoOTA.setPassword(password);

    ArduinoOTA.onStart([]() {
        // Trong lúc ghi flash, mọi task khác vẫn chạy và vẫn có thể ghi NVS.
        // In mốc này ra để nếu nạp hỏng còn biết nó chết ở giai đoạn nào.
        Serial.println("\n⬇ OTA: bắt đầu nhận firmware mới...");
    });
    ArduinoOTA.onEnd([]() { Serial.println("\n✅ OTA: xong, đang khởi động lại."); });
    ArduinoOTA.onProgress([](unsigned int cur, unsigned int total) {
        Serial.printf("\rOTA: %u%%", (total == 0) ? 0 : (cur * 100) / total);
    });
    ArduinoOTA.onError([](ota_error_t e) { Serial.printf("\n❌ OTA lỗi mã %u\n", e); });

    ArduinoOTA.begin();
    Serial.printf("🔄 OTA sẵn sàng tại %s.local\n", hostname);
    return true;
}

/** Gọi đều đặn trong vòng lặp của task mạng. */
static inline void oiOtaLoop(void) { ArduinoOTA.handle(); }

#endif // OI_OTA_H
