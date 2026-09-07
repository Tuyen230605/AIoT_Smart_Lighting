# ADR 0002 — Học phần dư thay vì học trực tiếp cho mô hình sở thích

**Trạng thái:** Chấp nhận

## Bối cảnh

Một hộ gia đình sinh ra khoảng 10–30 sự kiện có nhãn mỗi ngày. Sau bốn tuần chỉ được
400–700 dòng, lại dồn hết vào buổi tối. Ở quy mô đó, mô hình dự đoán trực tiếp mức
sáng sẽ overfit, và mạng nơ-ron thì hoàn toàn không phù hợp.

## Quyết định

Không dự đoán mức sáng. Dự đoán **phần lệch so với quy tắc nền**:

```
y = độ_sáng_người_dùng − độ_sáng_quy_tắc_nền
cuối_cùng = quy_tắc + α × phần_dư,   α = min(1, n/150)
```

Quy tắc nền là đường cong nhịp sinh học cộng vòng bù lux khép kín. Triển khai theo
bốn bậc: quy tắc thuần (n=0) → độ lệch co ngót (n<50) → mô hình phần dư (n≥50) →
huấn luyện lại hằng đêm có chốt chặn.

## Hệ quả

**Tốt:** quy tắc nền đã nắm trọn phần tín hiệu mạnh có cơ sở vật lý (trời tối → giảm
sáng), nên mô hình chỉ phải học phần lệch cá nhân — biến mục tiêu có phương sai nhỏ
hơn nhiều. Hệ thống dùng được ngay từ ngày đầu, không cần chờ tích luỹ dữ liệu.

**Xấu:** phải bảo trì hai thứ, quy tắc và mô hình. Nếu quy tắc nền sai lệch có hệ thống,
mô hình sẽ phải học bù cho nó thay vì học sở thích thật.

**Ràng buộc:** đầu ra bị chặn không lệch quá `OI_MODEL_MAX_DEVIATION` (60) so với quy
tắc, để giới hạn bán kính thiệt hại khi mô hình học sai.

## Phương án đã cân nhắc

- **Hồi quy trực tiếp bằng LightGBM** — vẫn giữ làm mốc so sánh trong đánh giá ngoại tuyến.
- **Mạng nơ-ron nhỏ** — loại vì dữ liệu quá ít và vì không giải thích được, trong khi
  yêu cầu FR‑E3 đòi hỏi ghi lại lý do của từng hành động tự động.
- **Chỉ dùng quy tắc, không học** — giữ làm mốc so sánh và làm phương án dự phòng khi
  bối cảnh nằm ngoài phân bố huấn luyện.
