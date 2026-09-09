# Bài chạy liên tục 72 giờ — cổng chất lượng của G1

## Vì sao có bài này

Rò rỉ bộ nhớ và tương tranh **không lộ ra trong mười phút thử nghiệm**. Chúng lộ
ra sau nhiều giờ. Nếu không chốt ở cuối G1, chúng sẽ lộ ra đúng tuần 17 — lúc
không còn thời gian để sửa.

Đây cũng là chỗ thay bảng số "giả lập" của bản DACN bằng số đo thật. Đồ thị sinh
ra ở đây đi thẳng vào chương 4 của báo cáo.

## Ba tiêu chí phải đạt

| Tiêu chí | Ngưỡng | Ý nghĩa nếu trượt |
|---|---|---|
| `heap_min` | không trôi xuống theo thời gian | còn rò rỉ bộ nhớ |
| Ngăn xếp mọi task | còn dư ≥ 25 % (NFR-05) | cấp stack quá sát, sẽ tràn khi tải cao |
| `boots` | không tăng trong suốt 72 giờ | có reset ngoài ý muốn |

## Chuẩn bị

Firmware tự phát số liệu sức khoẻ lên `oi/tele/<node>` mỗi 60 giây
(`HEALTH_PERIOD_MS` trong `Config.h`). Không cần bật gì thêm.

```bash
pip install paho-mqtt matplotlib
```

## Chạy

```bash
# Ghi log — để chạy suốt 72 giờ, tốt nhất trên Pi hoặc một máy không tắt
python tools/scripts/soak_monitor.py --host 192.168.1.10 --out soak_g1.csv
```

Trong 72 giờ đó, **phải có tải thực tế**, không để máy đứng yên:

- giọng nói: đọc từ khoá rải rác trong ngày, cả đúng lẫn sai
- nút bấm: bấm và giữ nhiều lần mỗi ngày
- MQTT: script bắn lệnh đổi màu/độ sáng ngẫu nhiên vài phút một lần
- LED chạy hiệu ứng cầu vồng ít nhất một phần ba thời gian (tải nặng nhất)

## Chấm điểm

```bash
python tools/scripts/soak_monitor.py --report soak_g1.csv --plot soak_g1.png
```

Script in bảng ba tiêu chí cho từng node và trả mã thoát khác 0 nếu chưa đạt.

## Khi trượt thì đọc gì

- **`heap_min` trôi đều**: tìm chỗ cấp phát động trong vòng lặp. Nghi ngờ đầu
  tiên là `String` — mọi phép nối chuỗi trong task chạy liên tục đều cấp phát heap.
- **Một task tụt dưới 25 %**: tăng hằng số `STACK_*` tương ứng trong `Config.h`,
  đừng tăng bừa tất cả — mỗi KB stack là 1 KB RAM không dùng được vào việc khác.
- **`boots` tăng**: đọc lý do reset qua `esp_reset_reason()`; brownout thì xem
  lại giới hạn công suất LED (G1.4), panic thì bật `monitor_filters =
  esp32_exception_decoder` và đọc lại vết ngăn xếp.

## Ghi chú cho báo cáo

Giữ nguyên file CSV và ảnh PNG. Gắn thẻ git `g1-done` sau khi đạt, để về sau còn
quay lại đúng phiên bản mã đã dùng để đo — thứ hội đồng có thể hỏi.
