# oi-vision — camera, phát hiện người, luồng chú giải.
# YOLO là tuỳ chọn: thiếu trọng số, hoặc build với WITH_YOLO=0, thì dịch vụ lùi
# về bộ phát hiện theo chuyển động và vẫn chạy (xem detector.py).
FROM python:3.11-slim

WORKDIR /app
ENV PYTHONPATH=/app/services PYTHONUNBUFFERED=1

# Thư viện hệ thống OpenCV cần lúc import.
RUN apt-get update \
 && apt-get install -y --no-install-recommends libgl1 libglib2.0-0 \
 && rm -rf /var/lib/apt/lists/*

# Ràng buộc phiên bản phải nằm trong ngoặc kép: để trần thì shell hiểu `>` là
# chuyển hướng đầu ra, bỏ qua ràng buộc và ghi ra một file tên "=2.0".
RUN pip install --no-cache-dir "paho-mqtt>=2.0" numpy opencv-python-headless

# YOLOv8n cần ultralytics + torch bản CPU (vài trăm MB). Bỏ qua bằng:
#     docker compose build --build-arg WITH_YOLO=0 vision
# ultralytics kéo theo opencv-python bản có GUI, trùng thư mục cv2 với bản
# headless ở trên — gỡ nó rồi cài lại headless để cv2 không bị hỏng một nửa.
ARG WITH_YOLO=1
RUN if [ "$WITH_YOLO" = "1" ]; then \
      pip install --no-cache-dir --extra-index-url https://download.pytorch.org/whl/cpu ultralytics \
   && pip uninstall -y opencv-python \
   && pip install --no-cache-dir --force-reinstall --no-deps opencv-python-headless ; \
    fi

COPY services/oi_common  ./services/oi_common
COPY services/oi_vision  ./services/oi_vision

CMD ["python", "-m", "oi_vision"]
