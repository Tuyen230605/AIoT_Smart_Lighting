# Tài liệu dự án Oi

| File | Nội dung | Trạng thái |
|---|---|---|
| `00-overview.md` | Tính năng, chức năng, điểm mới so với DACN | Cần điền |
| `01-requirements.md` | Yêu cầu chức năng (FR) và phi chức năng (NFR) | Cần điền |
| `02-architecture.md` | Ba tầng, bảng tác vụ FreeRTOS, các dịch vụ hub | Cần điền |
| `03-ai-models.md` | Bản đăng ký mô hình, ngân sách, chỉ số đánh giá | Cần điền |
| `04-protocols.md` | Chủ đề MQTT, lược đồ JSON, giao thức leo thang | Cần điền |
| `05-evaluation.md` | Chỉ số cấp hệ thống, thí nghiệm A và B | Cần điền |
| `06-hardware.md` | Danh mục linh kiện, sơ đồ chân, đấu nối | Cần điền |
| `07-security.md` | Xử lý bí mật, nhật ký sự cố | ✅ Xong |
| `08-soak-test.md` | Quy trình và tiêu chí bài chạy 72 giờ (G1.8) | ✅ Xong |
| `09-thay-doi-kien-truc.md` | Vì sao kiến trúc thay đổi so với DACN, kèm cách kiểm chứng | ✅ Xong |
| `10-dung-mvp.md` | Dựng lát cắt dọc G2 lên Pi 5, từng bước | ✅ Xong |
| `adr/` | Quyết định kiến trúc kèm lý do | 5 bản ghi |

## Quy ước

- Mọi con số hiệu năng phải **truy được về một phép đo cụ thể**. Ước tính thiết kế
  phải ghi rõ là ước tính và phải được thay bằng số đo thật ở giai đoạn G1b.
- Yêu cầu có mã số (`FR-A1`, `NFR-03`) để mã nguồn và kiểm thử tham chiếu ngược lại được.
- Quyết định kiến trúc quan trọng ghi thành ADR, không chôn trong lịch sử chat.
