    # oi-vision — camera, phát hiện người, luồng chú giải.
# YOLO là tuỳ chọn: thiếu trọng số thì dịch vụ lùi về bộ phát hiện
# theo chuyển động và vẫn chạy (xem detector.py).
    FROM python:3.11-slim

    WORKDIR /app
    ENV PYTHONPATH=/app/services PYTHONUNBUFFERED=1

    RUN apt-get update && apt-get install -y --no-install-recommends \n        libgl1 libglib2.0-0 \n    && rm -rf /var/lib/apt/lists/*

    RUN pip install --no-cache-dir paho-mqtt>=2.0 numpy opencv-python-headless

    COPY services/oi_common  ./services/oi_common
    COPY services/oi_vision  ./services/oi_vision

    CMD ["python", "-m", "oi_vision"]
