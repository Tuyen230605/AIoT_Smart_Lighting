# Bảo mật và xử lý bí mật

## 1. Nhật ký sự cố — rò rỉ khoá riêng AWS

**Phát hiện:** trong quá trình rà soát mã nguồn bản Dự án Công nghệ.

**Nội dung rò rỉ:**

| File | Nội dung bị lộ |
|---|---|
| `Node2_Radar_GateLight/include/Config.h` | Khoá riêng RSA của chứng chỉ AWS IoT, chứng chỉ thiết bị, SSID và mật khẩu Wi-Fi — tất cả ở dạng chữ thường |
| `Node1_EdgeAI_Controller/src/Secrets.cpp` | Chứng chỉ AWS IoT và khoá riêng |
| `Node1_EdgeAI_Controller/include/Config.h` | SSID và mật khẩu Wi-Fi, endpoint AWS |

Toàn bộ đã được commit và đẩy lên một kho công khai trên GitHub.

**Mức độ:** nghiêm trọng. Khoá riêng AWS IoT cho phép bất kỳ ai mạo danh thiết bị,
publish dữ liệu giả lên topic của hệ thống, và subscribe để đọc mọi dữ liệu đi qua.

### Các bước xử lý bắt buộc

Đánh dấu `[x]` khi hoàn thành. **Không đẩy mã nguồn lên kho mới trước khi xong bước 1–3.**

- [ ] **1. Vô hiệu hoá chứng chỉ.** AWS IoT Core → Security → Certificates → chọn cả hai
      certificate → *Deactivate*, sau đó *Revoke* rồi *Delete*. Vô hiệu hoá quan trọng hơn
      xoá: xoá mà quên vô hiệu hoá trong lúc còn attach policy là chưa an toàn.
- [ ] **2. Đổi mật khẩu Wi-Fi** của mạng đã bị lộ.
- [ ] **3. Kiểm tra dấu hiệu lạm dụng.** AWS Billing → có tài nguyên nào không do mình
      tạo. CloudTrail → hoạt động bất thường. AWS IoT → số lượng kết nối bất thường.
- [ ] **4. Tạo chứng chỉ mới**, gắn policy tối thiểu (chỉ publish/subscribe đúng các
      topic của node đó, không dùng wildcard rộng).
- [ ] **5. Nạp chứng chỉ mới qua NVS**, không biên dịch vào firmware:
      `python tools/provision/provision_node.py --port COM5 --node node2`
- [ ] **6. Xử lý kho cũ.** Kho `DoanTuyen23/ESP32-AIoT-Smart-Lighting` vẫn còn khoá
      trong lịch sử git. Chuyển sang private hoặc xoá hẳn. Lưu ý: dù xoá, nội dung có
      thể đã bị nhân bản hoặc lập chỉ mục — **vô hiệu hoá chứng chỉ ở bước 1 mới là
      biện pháp thật sự, mọi thao tác trên git chỉ là dọn dẹp.**

### Vì sao kho mã mới bắt đầu bằng lịch sử sạch

Kho `Tuyen230605/AIoT_Smart_Lighting` được khởi tạo với **lịch sử git mới hoàn toàn**,
không mang theo lịch sử cũ. Lý do: khoá riêng nằm trong commit đầu tiên của kho cũ, và
`git filter-repo` trên một lịch sử chỉ có một commit thì không khác gì tạo lại từ đầu —
nhưng làm lại từ đầu thì chắc chắn sạch, còn viết lại lịch sử luôn có rủi ro sót.

Đánh đổi: mất lịch sử commit của bản DACN. Chấp nhận được, vì bản DACN đã được lưu
riêng dưới dạng báo cáo và kho cũ vẫn còn để tra cứu.

---

## 2. Nguyên tắc xử lý bí mật

| Loại | Nơi lưu | Đến thiết bị bằng cách |
|---|---|---|
| SSID / mật khẩu Wi-Fi | `firmware/<node>/secrets.ini` | Cờ biên dịch `-D`, file nằm trong `.gitignore` |
| Endpoint MQTT | `secrets.ini` | Cờ biên dịch |
| Chứng chỉ và khoá riêng AWS | Máy của người phát triển, ngoài kho mã | Nạp vào phân vùng NVS `oi_creds` bằng công cụ riêng |
| Mật khẩu broker nội bộ | `hub/.env` | Biến môi trường, file nằm trong `.gitignore` |

**Vì sao chứng chỉ đi qua NVS chứ không qua cờ biên dịch:** cờ biên dịch nhúng chuỗi
vào ảnh firmware, nên bất kỳ ai đọc được flash (kể cả qua `esptool read_flash`) đều lấy
được khoá. Để ở NVS thì vẫn đọc được nếu có quyền vật lý, nhưng khoá không lọt vào
file `.bin` mà ta hay chia sẻ khi nhờ người khác nạp hộ hoặc đính kèm báo cáo.

---

## 3. Phòng thủ tự động

`.github/workflows/ci.yml` chạy ba lớp chặn, **trước** mọi job khác:

1. `gitleaks` quét toàn bộ lịch sử với `fetch-depth: 0`
2. `git grep` chặn các mẫu đã biết: `BEGIN PRIVATE KEY`, `BEGIN CERTIFICATE`, `aws_secret_access_key`
3. `git ls-files` chặn việc `secrets.ini`, `.env`, `*.pem`, `*.key` bị theo dõi

Ngoài ra `.gitignore` đặt khối bí mật lên **đầu file** kèm chú thích cảnh báo, để mọi
lần sửa đều đập vào mắt.

### Trước mỗi lần push, chạy tay

```bash
git ls-files | grep -E 'secrets\.ini|\.env$|\.(pem|key)$'    # phải không ra gì
git grep -nE 'BEGIN (RSA )?PRIVATE KEY|BEGIN CERTIFICATE' -- ':!*.example'
```

---

## 4. Quyền riêng tư — ràng buộc thiết kế, không phải lời hứa

Hệ thống có camera và micro trong không gian sống, nên quyền riêng tư phải là ràng
buộc kiến trúc kiểm chứng được:

- **Khung hình không rời khỏi Pi.** Chỉ toạ độ và nhãn được publish. Không ghi ảnh ra đĩa.
- **Âm thanh không rời khỏi mạng LAN.** Luồng PCM đi từ node tới hub qua UDP nội bộ.
- **Công tắc cắt nguồn camera bằng phần cứng**, kèm đèn báo. Khi tắt, hệ thống tự
  chuyển sang mức suy giảm L4 chạy bằng radar chứ không ngừng hoạt động.
- **Kiểm chứng bằng thực nghiệm:** yêu cầu phi chức năng NFR‑07 đo bằng cách bắt gói
  Wireshark trong 1 giờ vận hành, chứng minh không có luồng dữ liệu media nào ra ngoài.
