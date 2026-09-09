/**
 * oi_creds.h — Đọc chứng chỉ TLS từ phân vùng NVS `oi_creds` (G1.7).
 *
 * VÌ SAO TỒN TẠI
 * Sự cố lộ khoá ở G0.1 không phải do bất cẩn một lần, mà do *kiến trúc buộc
 * phải bất cẩn*: khi khoá nằm trong file .h thì không có cách nào commit mã mà
 * không commit khoá. File này đóng đường đó lại về mặt kiến trúc — chứng chỉ
 * nằm trong một phân vùng flash riêng, nạp bằng công cụ ngoài, và KHÔNG BAO GIỜ
 * đi qua mã nguồn lẫn ảnh firmware.
 *
 * Kiểm chứng:  strings firmware.bin | grep "BEGIN CERTIFICATE"   → phải rỗng.
 *
 * DÙNG CHO CÁI GÌ
 * Chứng chỉ TLS của chính mạng nhà: CA tự ký của hub, cộng chứng chỉ và khoá
 * riêng của từng node, để MQTT tới Mosquitto chạy trên TLS (bật ở G2.1).
 * Hệ thống KHÔNG kết nối ra đám mây — xem ADR 0004.
 *
 * CHƯA CÓ CHỨNG CHỈ THÌ SAO
 * Node vẫn chạy đầy đủ qua cổng 1883 không mã hoá trong mạng nhà. Không crash,
 * không chặn khởi động. Nạp chứng chỉ khi đã dựng hub bằng:
 *     python tools/provision/provision_node.py --port COM5 --node node1
 */
#ifndef OI_CREDS_H
#define OI_CREDS_H

#include <Arduino.h>
#include <Preferences.h>
#include <nvs_flash.h>

#ifndef OI_CREDS_PARTITION
#define OI_CREDS_PARTITION "oi_creds"
#endif

typedef struct {
    String ca;      // CA của mạng nhà (tự ký lúc dựng hub)
    String cert;    // chứng chỉ thiết bị
    String pkey;    // khoá riêng thiết bị
    bool   valid;   // đủ cả ba mới đi kết nối TLS được
} OiCreds;

/**
 * Nạp chứng chỉ từ NVS. Trả về false khi thiếu — KHÔNG phải lỗi, chỉ nghĩa là
 * thiết bị chưa được cấp phát.
 *
 * @param ns  namespace trong phân vùng, xem NVS_NAMESPACE ở Config.h
 */
static inline bool oiCredsLoad(OiCreds &out, const char *ns,
                               const char *k_ca, const char *k_cert,
                               const char *k_pkey) {
    out.valid = false;

    // Phân vùng chứng chỉ tách riêng khỏi nvs mặc định nên phải tự khởi tạo.
    // ESP_ERR_NVS_NEW_VERSION_FOUND / NO_FREE_PAGES nghĩa là phân vùng có mà
    // nội dung không dùng được — coi như chưa cấp phát, không xoá dữ liệu.
    esp_err_t err = nvs_flash_init_partition(OI_CREDS_PARTITION);
    if (err != ESP_OK) return false;

    Preferences p;
    if (!p.begin(ns, /*readOnly=*/true, OI_CREDS_PARTITION)) return false;

    out.ca   = p.getString(k_ca, "");
    out.cert = p.getString(k_cert, "");
    out.pkey = p.getString(k_pkey, "");
    p.end();

    out.valid = out.ca.length() > 0 && out.cert.length() > 0 && out.pkey.length() > 0;
    return out.valid;
}

/** In hướng dẫn cấp phát ra Serial thay vì im lặng bỏ qua hoặc crash. */
static inline void oiCredsPrintHint(const char *node) {
    Serial.println();
    Serial.println("⚠ Chưa có chứng chỉ trong phân vùng NVS oi_creds.");
    Serial.println("  → Node vẫn chạy đầy đủ qua broker nội bộ, cổng 1883 không mã hoá.");
    Serial.println("  → Chỉ chưa bật được MQTT-over-TLS trong mạng nhà.");
    Serial.printf("  → Khi đã có chứng chỉ, nạp bằng:\n"
                  "     python tools/provision/provision_node.py --port COMx --node %s\n",
                  node);
    Serial.println();
}

#endif // OI_CREDS_H
