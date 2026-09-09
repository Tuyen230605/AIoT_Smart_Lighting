# ADR 0004 — Bỏ phụ thuộc đám mây, giữ lại đường nối

**Trạng thái:** Chấp nhận · **Thay thế một phần:** [ADR 0003](0003-broker-noi-bo-cloud-tuy-chon.md)

## Bối cảnh

ADR 0003 đã hạ AWS IoT Core từ *đường tới hạn* xuống *kênh truy cập từ xa và lưu vết*,
kết nối qua dịch vụ cầu nối `oi-bridge`. Quyết định đó đúng nhưng chưa đi hết đường:
nó vẫn để lại một phụ thuộc đám mây trong hệ thống, và phụ thuộc đó phải được nuôi.

Sau khi hoàn thành G1, ba dữ kiện mới làm lộ ra rằng phần còn lại của AWS không đáng giữ:

1. **Không đóng góp nào cần đến nó.** C1 (giao thức leo thang), C2 (lan truyền sai số),
   C3 (hợp nhất radar–thị giác), C4 (học sở thích) đều nằm trọn trong nhà. Ba thí nghiệm
   A/B/C cũng vậy. AWS không xuất hiện trong bất kỳ phép đo nào của đồ án.

2. **Ma trận suy giảm đã tự nói ra điều đó.** Mức L1 ghi rõ: mất Internet thì cả 8 mô
   hình vẫn chạy. Một thành phần mà khi mất đi không làm giảm chức năng nào thì theo
   định nghĩa nó không thuộc về kiến trúc chức năng.

3. **Đề tài định vị là sản phẩm bán theo hộ gia đình.** Ở định vị đó, chi phí đám mây
   là chi phí *lặp lại theo từng thiết bị bán ra và kéo dài vô hạn*, do nhà sản xuất
   gánh. Đây không phải chi tiết vận hành, nó là một tính chất của mô hình sản phẩm.

## Quyết định

**Bỏ AWS IoT Core khỏi kiến trúc.** Hệ thống chạy trọn vẹn trong mạng nội bộ: không tài
khoản, không đăng ký, không phí định kỳ, không phụ thuộc vào việc nhà sản xuất còn sống.

**Giữ lại đường nối, bỏ phụ thuộc.** Cụ thể:

- Cơ chế nạp chứng chỉ qua NVS (`oi_creds.h`, `oi_provision.h`, `provision_node.py`)
  **giữ nguyên**, đổi mục tiêu từ chứng chỉ AWS sang chứng chỉ TLS của broker nội bộ.
  Mosquitto trên hub sẽ bật TLS, và nó cần đúng cơ chế nạp khoá đó.
- Dịch vụ `oi-bridge` **bỏ khỏi kế hoạch G2**.
- Nhu cầu truy cập từ xa — thứ duy nhất AWS thực sự phục vụ — chuyển sang **đường hầm
  mạng riêng** (Tailscale hoặc Cloudflare Tunnel) cài trên hub. Nó giải đúng bài toán
  đó, miễn phí ở quy mô hộ gia đình, và không phát sinh chi phí theo số thiết bị.
- Kiến trúc vẫn để ngỏ khả năng cắm một backend đám mây về sau: ranh giới là MQTT, nên
  một dịch vụ cầu nối có thể được thêm vào mà không sửa firmware.

## Hệ quả

**Tốt**

- Không còn thành phần nào trong hệ thống cần tài khoản bên thứ ba để hoạt động.
- Luận điểm Edge Computing của đề tài chuyển từ *tuyên bố* sang *tính chất kiểm chứng được*:
  rút cáp Internet vĩnh viễn thì không mất gì cả.
- Bớt một dịch vụ ở G2, bớt việc quản lý vòng đời chứng chỉ, bớt một mặt tấn công.
- Không còn bị ràng buộc bởi thời hạn gói dùng thử 12 tháng của AWS — một ràng buộc
  thời gian vô nghĩa nhưng có thật đối với một đồ án kéo dài 18 tuần.

**Xấu**

- Truy cập từ xa giờ phụ thuộc vào một đường hầm phải tự cài và tự bảo trì. Nếu hub sập,
  không còn kênh nào để biết trạng thái từ bên ngoài — trước đây Device Shadow vẫn giữ
  bản ghi cuối. Chấp nhận: đây là hệ quả trực tiếp của việc chọn một hệ thống không có
  phần phụ thuộc bên ngoài, và người dùng gia đình vốn ở gần thiết bị.
- Mất khả năng lưu vết dài hạn ngoài nhà. Bù bằng SQLite trên hub cộng sao lưu định kỳ.
- Hội đồng có thể hỏi *"vì sao không dùng cloud"*. Câu trả lời nằm ngay ở phần Bối cảnh,
  và nó mạnh hơn câu trả lời ngược lại.

## Phương án đã cân nhắc

- **Giữ nguyên ADR 0003** — AWS làm kênh phụ. Bị loại: một thành phần không phục vụ
  đóng góp nào nhưng vẫn phải nuôi là nợ kỹ thuật, không phải tính năng.
- **Thay AWS bằng một VPS tự dựng** — vẫn là chi phí định kỳ và vẫn là điểm hỏng ngoài
  tầm kiểm soát của chủ nhà, chỉ đổi nhà cung cấp.
- **Bỏ luôn cả truy cập từ xa** — bị loại: đây là nhu cầu có thật, và đường hầm mạng
  riêng đáp ứng được nó mà không kéo theo phụ thuộc kiến trúc nào.
