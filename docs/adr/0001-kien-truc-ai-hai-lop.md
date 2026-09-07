# ADR 0001 — Kiến trúc AI hai lớp phản xạ–suy luận

**Trạng thái:** Chấp nhận

## Bối cảnh

Nhận dạng từ khoá trên vi điều khiển phản hồi dưới 150 ms nhưng chỉ hiểu 6 câu lệnh
cứng. Mô hình ngôn ngữ hiểu lời nói tự nhiên nhưng đo trên Raspberry Pi 5 mất khoảng
5 giây cho toàn chuỗi. Chọn một trong hai đều làm hỏng sản phẩm: quá cứng nhắc, hoặc
quá chậm cho hành vi bật đèn.

## Quyết định

Chạy cả hai, phân tầng theo độ tin cậy. Vi điều khiển xử lý trực tiếp khi mô hình từ
khoá đủ tự tin (≥ 0.75), và chuyển việc lên hub khi không đủ tự tin **hoặc** khi phát
hiện phát ngôn dài hơn 1500 ms — dài hơn mọi từ khoá đã huấn luyện, nên chắc chắn là
câu tự do.

Ngưỡng và tham số nằm ở `firmware/common/include/oi_protocol.h`, khối
`OI_CONF_*` / `OI_ESCALATE_*`.

## Hệ quả

**Tốt:** độ trễ trung bình gần bằng phương án chỉ dùng từ khoá, độ phủ ý định gần bằng
phương án chỉ dùng mô hình ngôn ngữ. Hub sập thì vẫn còn 6 lệnh cục bộ.

**Xấu:** hai đường xử lý nghĩa là hai bộ lỗi và hai chỗ phải gỡ rối. Ngưỡng leo thang
trở thành siêu tham số phải hiệu chỉnh bằng thực nghiệm — chuyển quá nhiều thì chậm
như không có lớp phản xạ, chuyển quá ít thì đoán bừa.

**Ràng buộc:** node phải đệm sẵn 500 ms âm thanh **trước** thời điểm kích hoạt
(`OI_PREROLL_MS`), nếu không hub sẽ nhận được câu bị cụt đầu.

## Phương án đã cân nhắc

- **Chỉ dùng từ khoá** (chính là bản DACN) — giữ làm cấu hình A trong thí nghiệm so sánh.
- **Chỉ dùng mô hình ngôn ngữ, mọi lệnh đều lên hub** — giữ làm cấu hình B.
- **Đẩy mô hình ngôn ngữ lên đám mây** — bị loại vì phá vỡ luận điểm quyền riêng tư
  và làm hệ thống chết khi mất mạng.
