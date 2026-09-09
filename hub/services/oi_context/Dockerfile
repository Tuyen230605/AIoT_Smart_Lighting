# oi-context — hợp nhất radar + thị giác + lux + giờ.
FROM python:3.11-slim

WORKDIR /app
ENV PYTHONPATH=/app/services PYTHONUNBUFFERED=1

RUN pip install --no-cache-dir paho-mqtt>=2.0

COPY services/oi_common  ./services/oi_common
COPY services/oi_context  ./services/oi_context

CMD ["python", "-m", "oi_context"]
