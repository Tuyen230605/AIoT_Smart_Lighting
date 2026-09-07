# Thí nghiệm cấp hệ thống

Chỉ số từng mô hình trả lời *"mô hình này tốt không"*. Thư mục này trả lời câu quan
trọng hơn: *"hệ thống này có đáng dùng không"*.

## Bộ chỉ số

| Mã | Chỉ số | Mục tiêu |
|---|---|---|
| S1 | Tỉ lệ hoàn thành tác vụ đầu-cuối | ≥ 88 % |
| S2 | Thời gian từ hết câu đến đèn đổi | p50 ≤ 0,4 s · p95 ≤ 6 s |
| S3 | Tỉ lệ leo thang lên hub | 15–25 % |
| S4 | Tỉ lệ chỉnh tay | giảm ≥ 40 % sau 4 tuần |
| S5 | Tỉ lệ kích hoạt thừa | ≤ 2 / ngày |
| S6 | Khả dụng ở từng mức suy giảm | L2 ≥ 40 % · L3 ≥ 25 % |
| S7 | Điện năng tiêu thụ so với thủ công | giảm ≥ 15 % |

S3 và S4 là hai chỉ số đặc trưng nhất của đề tài này.

## Thí nghiệm

- **A · So sánh ba kiến trúc** — chỉ từ khoá / chỉ mô hình ngôn ngữ / hệ lai, trên
  cùng 200 phát ngôn. Chứng minh đóng góp C1.
- **B · Bóc tách** — tháo hoặc lý tưởng hoá từng năng lực rồi đo lại S1 và S4.
  Trả lời câu *"năng lực này có đáng tồn tại không"*.

Quy trình chi tiết từng thí nghiệm nằm trong `protocols/`. Kết quả thô nằm trong
`results/` và không được commit; chỉ file tổng hợp `summary_*.csv` được theo dõi.

> **Kết quả âm tính là kết quả hợp lệ.** Nếu bóc tách cho thấy một năng lực không đóng
> góp gì, viết đúng như vậy kèm phân tích nguyên nhân được đánh giá cao hơn hẳn việc
> giữ nó lại và mô tả suông.
