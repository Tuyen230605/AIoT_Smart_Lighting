# Huấn luyện và đánh giá mô hình

Mỗi thư mục con tương ứng một năng lực AI trong `docs/03-ai-models.md`.

| Thư mục | Năng lực | Sản phẩm |
|---|---|---|
| `kws/` | AI‑1 · nhận dạng từ khoá | Mô hình int8 nạp vào `firmware/node1_controller/lib/` |
| `asr/` | AI‑2 · giọng nói → chữ | Kịch bản đo, không huấn luyện |
| `nlu/` | AI‑3 · ý định | Prompt hệ thống, GBNF grammar, bộ 200 câu kiểm thử |
| `vision/` | AI‑4, AI‑5 · người và hoạt động | Bộ phân loại hoạt động, tệp hiệu chuẩn vùng |
| `policy/` | AI‑6 · sở thích chiếu sáng | Mô hình phần dư + đặc trưng |

## Quy tắc chia tập — đọc trước khi huấn luyện bất cứ gì

Ba lỗi chia tập làm cho kết quả đẹp giả tạo, và người phản biện sẽ hỏi:

1. **KWS:** chia **theo người nói**. Cùng một người xuất hiện ở cả train và test là rò
   rỉ dữ liệu — độ chính xác sẽ cao ảo và sụp đổ khi gặp người lạ.
2. **Hoạt động:** chia **theo phiên quay**. Các khung liền kề gần như trùng nhau; chia
   ngẫu nhiên theo khung cho ra độ chính xác trên 95 % nhưng vô nghĩa.
3. **Sở thích:** chia **theo thời gian**. Train quá khứ, test tương lai. Xáo trộn ngẫu
   nhiên là để tương lai rò rỉ vào quá khứ.
