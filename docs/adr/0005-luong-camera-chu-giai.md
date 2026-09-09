# ADR 0005 — Luồng camera có chú giải trên dashboard

**Trạng thái:** Chấp nhận · **Nới lỏng có kiểm soát:** cam kết riêng tư ở mục M5 của đặc tả

## Bối cảnh

Đặc tả v2.1, mục M5, cam kết: *"khung hình xử lý hoàn toàn trên Pi và không bao giờ ghi ra
đĩa; **chỉ toạ độ và nhãn rời khỏi tiến trình**"*. Cam kết này tồn tại vì cả chương 1 của
bản DACN dựng lên luận điểm quyền riêng tư, và gắn camera trong nhà là câu hỏi đầu tiên
hội đồng sẽ đặt.

Khi chuyển sang xây MVP trên phần cứng đã có, xuất hiện nhu cầu **xem được hình camera
trên dashboard**. Nhu cầu này hợp lý: người dùng muốn biết hệ thống đang "nhìn" thấy gì,
và một hệ chiếu sáng theo vùng mà không cho xem vùng đó thì khó tin tưởng. Nhưng nó mâu
thuẫn trực tiếp với câu chữ ở trên.

Bỏ qua mâu thuẫn này và cứ làm là cách tệ nhất: tài liệu nói một đằng, hệ thống làm một
nẻo, và người phản biện sẽ tìm ra.

## Quyết định

**Cho phép phát hình lên dashboard, nhưng chỉ dưới dạng luồng đã chú giải, kèm bốn ràng buộc.**

Luồng phát đi **không phải khung hình thô**. Nó là khung hình đã được vẽ chồng lên:

- hộp bao quanh mỗi người phát hiện được,
- tên vùng chức năng người đó đang đứng,
- nhãn hoạt động và độ tin cậy,
- mức sáng mà hệ thống đang tính cho vùng đó.

Nghĩa là thứ rời khỏi tiến trình không còn là *dữ liệu camera*, mà là **hình ảnh trực quan
hoá quyết định của hệ thống**. Nó phục vụ trực tiếp yêu cầu FR-E3 (nhật ký giải thích
quyết định): người xem hiểu được *vì sao* đèn vừa tự bật, chứ không chỉ thấy nó bật.

Bốn ràng buộc bắt buộc, kiểm chứng được:

| # | Ràng buộc | Kiểm chứng bằng |
|---|---|---|
| 1 | Chỉ phát khi có người đang mở trang và bấm xem; không chạy nền | Đóng trang → `oi-vision` ngừng mã hoá, CPU giảm thấy được |
| 2 | Chỉ nghe trên giao diện mạng LAN; **mặc định không đi qua đường hầm ra ngoài** | Cấu hình đường hầm chỉ phơi cổng dashboard, không phơi cổng luồng hình |
| 3 | Không ghi khung hình nào ra đĩa, kể cả tạm | `find / -name "*.jpg" -newermt "-1 hour"` sau một giờ chạy phải rỗng |
| 4 | Công tắc cắt nguồn phần cứng vẫn cắt được; mất camera thì hệ xuống mức L4 chạy bằng radar | Gạt công tắc giữa lúc đang xem → dashboard báo mất nguồn thị giác, đèn vẫn hoạt động |

Yêu cầu **NFR-07 giữ nguyên không đổi**: *không khung hình, không mẫu âm thanh nào rời khỏi
mạng LAN*. Luồng tới trình duyệt trong nhà nằm trọn trong LAN nên không vi phạm — phép đo
vẫn là bắt gói Wireshark trong một giờ vận hành, và kết quả vẫn phải là không có luồng
media nào đi ra ngoài.

Câu chữ ở M5 sửa từ *"chỉ toạ độ và nhãn rời khỏi tiến trình"* thành *"khung hình thô không
rời khỏi tiến trình; chỉ luồng đã chú giải được phát trong mạng LAN, theo yêu cầu và có
thể tắt"*.

## Hệ quả

**Tốt**

- Dashboard trở thành công cụ giải thích, không chỉ là bảng số. Đây là cách rẻ nhất để
  hiện thực FR-E3, và là cảnh demo tốt khi bảo vệ.
- Gỡ lỗi thị giác nhanh hơn hẳn: nhìn thấy mô hình đang gán sai vùng nào ngay tại chỗ,
  thay vì đọc log toạ độ.
- Cam kết riêng tư vẫn đứng vững và **mạnh hơn trước về mặt trình bày**, vì giờ nó là một
  quyết định có ràng buộc kiểm chứng được thay vì một lời hứa tuyệt đối.

**Xấu**

- Tốn CPU của Pi 5 đúng lúc nó đang chạy ASR và mô hình ngôn ngữ. Giảm thiểu bằng: dùng
  lại chính khung hình đã giải mã cho suy luận (không mở luồng thu thứ hai), hạ xuống
  ~320 px và 5–10 fps cho phần xem, và chỉ mã hoá khi có người xem.
- Thêm một bề mặt tấn công trong LAN. Giảm thiểu bằng ràng buộc 2 và bằng tài khoản
  dashboard có ACL hạn chế (chỉ đọc, chỉ ghi được vào nhánh `oi/cmd/#`).
- Phải giải thích được sắc thái này khi bảo vệ. Chính ADR này là câu trả lời có sẵn.

## Phương án đã cân nhắc

- **Không cho xem hình gì cả** — giữ cam kết tuyệt đối. Bị loại: người dùng không tin được
  một hệ thống thị giác mà họ không kiểm tra được nó nhìn thấy gì, và ta mất luôn công cụ
  gỡ lỗi hữu ích nhất cho `oi-vision`.
- **Phát khung hình thô như camera an ninh** — đơn giản nhất. Bị loại: phá cam kết mà không
  đổi lại giá trị gì cho luận điểm của đề tài; biến một hệ chiếu sáng thành một hệ giám sát.
- **Chỉ vẽ sơ đồ mặt bằng, không có hình thật** — hộp bao trên nền sơ đồ phòng. Giữ được
  riêng tư tuyệt đối nhưng mất khả năng gỡ lỗi (không thấy được mô hình sai vì lý do gì).
  **Đây là phương án dự phòng nếu hội đồng phản đối mạnh** — dữ liệu đã có sẵn, chỉ đổi cách
  vẽ, nên chuyển sang mất khoảng nửa buổi.
