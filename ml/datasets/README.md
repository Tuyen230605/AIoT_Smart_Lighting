# Dữ liệu

**Nội dung thư mục này không được commit** (xem `.gitignore`). Chỉ `README.md` và
`manifest.yaml` được theo dõi.

Lý do: các bản ghi âm chứa giọng nói thật và video chứa hình ảnh trong nhà — đây là
dữ liệu cá nhân, không được đưa lên kho công khai. Ngoài ra dung lượng lớn làm git
phình không cần thiết.

| Thư mục | Nội dung | Cách sinh ra |
|---|---|---|
| `kws_vi/` | Mẫu từ khoá tiếng Việt | Thu bằng `tools/scripts/record_kws.py` |
| `vision_room/` | Video có gán nhãn vùng và hoạt động | Quay và gán nhãn thủ công |
| `policy_logs/` | Nhật ký ngữ cảnh và hành động chỉnh đèn | `oi-state` xuất ra từ SQLite |

Mỗi thư mục cần một `manifest.yaml` ghi: số mẫu, người nói / phiên quay, ngày thu,
điều kiện (khoảng cách, mức ồn, ánh sáng), và cách chia tập. Manifest **được commit**
để kết quả có thể tái lập dù dữ liệu không đi kèm.
