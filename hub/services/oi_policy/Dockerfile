# oi-policy — luật nền. LightGBM sẽ thêm ở G3.7.
FROM python:3.11-slim

WORKDIR /app
ENV PYTHONPATH=/app/services PYTHONUNBUFFERED=1

RUN pip install --no-cache-dir paho-mqtt>=2.0

COPY services/oi_common  ./services/oi_common
COPY services/oi_policy  ./services/oi_policy

CMD ["python", "-m", "oi_policy"]
