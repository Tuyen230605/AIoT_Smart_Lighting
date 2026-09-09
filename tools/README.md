# Công cụ

| Thư mục | Công cụ | Dùng khi |
|---|---|---|
| `provision/` | Nạp Wi-Fi và chứng chỉ vào NVS | Mỗi lần cấp phát một thiết bị mới |
| `calibration/` | Hiệu chuẩn camera ↔ servo đèn rọi | Sau khi lắp hoặc di chuyển đèn rọi |
| `scripts/` | Thu dữ liệu, đo độ trễ, tiện ích lặt vặt | Thường xuyên |

## Hai công cụ đã có

| Lệnh | Việc |
|---|---|
| `provision/provision_node.py` | Nạp chứng chỉ TLS của mạng nhà vào NVS qua Serial (G1.7). Chưa dựng hub thì chưa cần chạy — node vẫn chạy đủ qua cổng 1883. |
| `scripts/soak_monitor.py` | Ghi và chấm điểm bài chạy 72 giờ (G1.8). Xem `docs/08-soak-test.md`. |
| `scripts/check-secrets.sh` | Quét rò rỉ bí mật. CI gọi chính script này. |

## Hiệu chuẩn đèn rọi — ghi chú phương pháp

Không dựng mô hình hình học 3D của căn phòng. Cách làm thô sơ mà chắc: rọi đèn thủ
công vào khoảng 12 điểm trên sàn, ghi lại từng cặp (toạ độ ảnh ↔ góc pan/tilt), rồi
nội suy bằng thin-plate spline. Nửa buổi là xong và ổn định hơn nhiều so với tính
toán qua mô hình phòng, vì nó tự hấp thụ mọi sai lệch lắp đặt.

Dung sai rất rộng: chùm sáng COB góc mở 15–30° ở khoảng cách 3 m tạo vùng sáng đường
kính 1–1,6 m, nên sai số bám ±5° vẫn nằm gọn trong vùng sáng.
