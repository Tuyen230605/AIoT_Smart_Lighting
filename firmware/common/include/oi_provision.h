/**
 * oi_provision.h — Cấp phát chứng chỉ qua cổng Serial (G1.7).
 *
 * VÌ SAO KHÔNG NẠP BẰNG CÔNG CỤ FLASH NGOÀI
 * Cách phổ biến là sinh ảnh phân vùng NVS bằng nvs_partition_gen.py của ESP-IDF
 * rồi ghi bằng esptool. Nó buộc phải cài thêm cả bộ ESP-IDF chỉ để nạp ba chuỗi,
 * và mỗi lần đổi chứng chỉ là một lần ghi flash toàn phân vùng. Ở đây firmware
 * tự nhận chứng chỉ qua Serial rồi tự ghi NVS: không phụ thuộc công cụ ngoài,
 * và đúng như kế hoạch G1.7 mô tả — thiếu chứng chỉ thì vào chế độ chờ cấp phát
 * và in hướng dẫn ra Serial, chứ không crash.
 *
 * GIAO THỨC (mỗi lệnh một dòng, kết thúc bằng '\n')
 *   PROV:PING                     → "OI-PROV READY <node>"
 *   PROV:STATUS                   → "OI-PROV STATUS ca=1 cert=0 pkey=0"
 *   PROV:SET:<khoá>:<base64>      → "OI-PROV OK <khoá> <số byte>"
 *   PROV:ERASE                    → "OI-PROV ERASED"
 * <khoá> là một trong: ca | cert | pkey
 *
 * BẢO MẬT
 * Chỉ nhận lệnh qua cổng USB nối trực tiếp — cần tiếp cận vật lý thiết bị. Khoá
 * riêng đi vào một chiều: ghi được, không có lệnh nào đọc nó ra.
 */
#ifndef OI_PROVISION_H
#define OI_PROVISION_H

#include <Arduino.h>
#include <Preferences.h>
#include <mbedtls/base64.h>
#include <nvs_flash.h>

#include "oi_creds.h"

#ifndef OI_PROV_MAX_LINE
#define OI_PROV_MAX_LINE 4096
#endif

static inline bool oiProvWrite(const char *ns, const char *key,
                               const uint8_t *data, size_t len) {
    if (nvs_flash_init_partition(OI_CREDS_PARTITION) != ESP_OK) return false;

    Preferences p;
    if (!p.begin(ns, /*readOnly=*/false, OI_CREDS_PARTITION)) return false;

    String value;
    value.reserve(len + 1);
    for (size_t i = 0; i < len; i++) value += (char)data[i];

    size_t written = p.putString(key, value);
    p.end();
    return written == value.length();
}

/**
 * Xử lý một dòng lệnh cấp phát. Trả về true nếu dòng đó là lệnh cấp phát
 * (đã xử lý xong), false nếu không phải để bên gọi tự dùng vào việc khác.
 */
static inline bool oiProvHandleLine(const String &line, const char *node,
                                    const char *ns, const char *k_ca,
                                    const char *k_cert, const char *k_pkey) {
    if (!line.startsWith("PROV:")) return false;

    if (line.startsWith("PROV:PING")) {
        Serial.printf("OI-PROV READY %s\n", node);
        return true;
    }

    if (line.startsWith("PROV:STATUS")) {
        OiCreds c;
        oiCredsLoad(c, ns, k_ca, k_cert, k_pkey);
        Serial.printf("OI-PROV STATUS ca=%d cert=%d pkey=%d\n",
                      c.ca.length() > 0, c.cert.length() > 0, c.pkey.length() > 0);
        return true;
    }

    if (line.startsWith("PROV:ERASE")) {
        Preferences p;
        if (p.begin(ns, false, OI_CREDS_PARTITION)) {
            p.clear();
            p.end();
            Serial.println("OI-PROV ERASED");
        } else {
            Serial.println("OI-PROV ERR khong mo duoc NVS");
        }
        return true;
    }

    if (line.startsWith("PROV:SET:")) {
        int p1 = line.indexOf(':', 5);          // sau "PROV:SET"
        int p2 = line.indexOf(':', p1 + 1);
        if (p1 < 0 || p2 < 0) {
            Serial.println("OI-PROV ERR cu phap");
            return true;
        }
        String field = line.substring(p1 + 1, p2);
        String b64   = line.substring(p2 + 1);
        b64.trim();

        const char *key = NULL;
        if (field == "ca")        key = k_ca;
        else if (field == "cert") key = k_cert;
        else if (field == "pkey") key = k_pkey;
        else {
            Serial.println("OI-PROV ERR khoa la");
            return true;
        }

        size_t need = 0;
        mbedtls_base64_decode(NULL, 0, &need,
                              (const unsigned char *)b64.c_str(), b64.length());
        if (need == 0 || need > OI_PROV_MAX_LINE) {
            Serial.println("OI-PROV ERR base64");
            return true;
        }

        uint8_t *buf = (uint8_t *)malloc(need + 1);
        if (buf == NULL) {
            Serial.println("OI-PROV ERR het bo nho");
            return true;
        }

        size_t out = 0;
        int rc = mbedtls_base64_decode(buf, need, &out,
                                       (const unsigned char *)b64.c_str(), b64.length());
        bool ok = (rc == 0) && oiProvWrite(ns, key, buf, out);
        free(buf);

        if (ok) Serial.printf("OI-PROV OK %s %u\n", field.c_str(), (unsigned)out);
        else    Serial.println("OI-PROV ERR ghi NVS that bai");
        return true;
    }

    Serial.println("OI-PROV ERR lenh la");
    return true;
}

/**
 * Đọc Serial không chặn và xử lý lệnh cấp phát nếu có.
 * Gọi đều đặn từ vòng lặp của task mạng.
 */
static inline void oiProvPoll(const char *node, const char *ns, const char *k_ca,
                              const char *k_cert, const char *k_pkey) {
    static String line;

    while (Serial.available()) {
        char c = (char)Serial.read();
        if (c == '\n' || c == '\r') {
            if (line.length() > 0) {
                oiProvHandleLine(line, node, ns, k_ca, k_cert, k_pkey);
                line = "";
            }
        } else if (line.length() < OI_PROV_MAX_LINE) {
            line += c;
        } else {
            line = "";   // dòng dài bất thường: bỏ, tránh ăn hết heap
        }
    }
}

#endif // OI_PROVISION_H
