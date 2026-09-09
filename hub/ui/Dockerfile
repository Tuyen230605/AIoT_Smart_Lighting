# Dashboard — chỉ là file tĩnh. Không có backend nào.
#
# Trình duyệt nói THẲNG với Mosquitto qua WebSocket cổng 9001, và vì trạng thái
# đã có cờ retain từ G1.5, trang vừa mở đã nhận đủ trạng thái hiện tại mà không
# cần gọi API nào. Broker chính là API.
#
# MQTT.js được tải MỘT LẦN lúc dựng ảnh rồi nằm luôn trong ảnh. Lúc chạy không
# cần Internet — đúng nguyên tắc của ADR 0004. (Dựng ảnh cần mạng, cũng như
# `docker pull` và `pip install` vốn đã cần.)
FROM nginx:alpine

RUN apk add --no-cache curl \
    && mkdir -p /usr/share/nginx/html/vendor \
    && curl -fsSL https://unpkg.com/mqtt@5.10.1/dist/mqtt.min.js \
         -o /usr/share/nginx/html/vendor/mqtt.min.js \
    && apk del curl

COPY index.html /usr/share/nginx/html/index.html
# config.js do bạn tạo từ config.js.example; dấu * để build không
# đứt khi chưa có file (trang sẽ báo lỗi đăng nhập, thấy ngay).
COPY config.js* /usr/share/nginx/html/
COPY nginx.conf /etc/nginx/conf.d/default.conf
