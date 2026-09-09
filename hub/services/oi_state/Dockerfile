# oi-state — nguồn sự thật, SQLite. Không cần thư viện nặng nào.
FROM python:3.11-slim

WORKDIR /app
ENV PYTHONPATH=/app/services PYTHONUNBUFFERED=1

RUN pip install --no-cache-dir paho-mqtt>=2.0

COPY services/oi_common  ./services/oi_common
COPY services/oi_state  ./services/oi_state

CMD ["python", "-m", "oi_state"]
