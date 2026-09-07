# ADR 0003 — Broker nội bộ là đường chính, đám mây là tuỳ chọn

**Trạng thái:** Chấp nhận

## Bối cảnh

Bản DACN để logic "radar phát hiện người → bật đèn cổng" đi vòng qua AWS IoT Core:
node publish trạng thái lên đám mây, Node-RED xử lý, rồi gửi lệnh ngược về. Mất
Internet là mất tự động hoá — mâu thuẫn trực tiếp với luận điểm Edge Computing mà
chương 1 của báo cáo dựng lên.

## Quyết định

Broker chính là Mosquitto chạy trên hub trong mạng nội bộ. AWS IoT Core hạ xuống vai
trò kênh truy cập từ xa và lưu vết dài hạn, kết nối qua dịch vụ cầu nối `oi-bridge`,
và **không nằm trên đường tới hạn của bất kỳ chức năng nào**.

Luật hiện diện → đèn cổng chuyển hẳn vào firmware Node 2. Hub chỉ được ghi đè trong
`HUB_OVERRIDE_TTL_MS` (5 phút), hết hạn thì quyền quyết định tự trả về luật cục bộ.

## Hệ quả

**Tốt:** mất Internet không ảnh hưởng gì. Độ trễ giảm vì không phải đi vòng ra ngoài.
Bốn mức suy giảm L1–L4 trở nên kiểm chứng được bằng cách rút dây thật.

**Xấu:** thêm một thành phần phải vận hành trong nhà. Hub trở thành điểm hỏng đơn lẻ
cho các năng lực tầng 2 — được bù bằng chế độ FALLBACK trên node.

**Ràng buộc:** trạng thái phải có nguồn sự thật duy nhất (`oi-state`), nếu không sẽ
xung đột giữa bản ghi nội bộ và Device Shadow trên đám mây.

## Phương án đã cân nhắc

- **Giữ AWS làm broker chính** — chính là bản DACN, bị loại vì lý do ở phần Bối cảnh.
- **Bỏ hẳn đám mây** — bị loại vì mất khả năng xem từ xa khi ra khỏi nhà, một tính
  năng người dùng thật sự cần.
