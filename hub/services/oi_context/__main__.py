"""oi-context — gộp radar, thị giác, lux và thời gian thành một bức tranh duy nhất.

VÌ SAO PHẢI LÀ MỘT DỊCH VỤ RIÊNG
Agent và mô hình sở thích đều cần "tình hình căn phòng lúc này". Nếu mỗi bên tự
gộp lấy, hai bên sẽ gộp khác nhau, và khi kết quả lệch nhau thì không ai biết
bên nào đúng. Một chỗ gộp, một lược đồ, mọi bên đọc chung.

MỖI KHỐI KÈM ĐỘ TIN CẬY VÀ TUỔI DỮ LIỆU
Không có hai trường đó thì không phân tích được lan truyền sai số (đóng góp C2),
và tệ hơn: không phân biệt được "camera báo không có ai" với "camera đã chết từ
mười phút trước".
"""

from __future__ import annotations

import logging
import os
import time
from datetime import datetime

from oi_common import protocol as proto
from oi_common.bus import OiBus, setup_logging

log = logging.getLogger("oi-context")

# Dữ liệu cũ hơn ngưỡng này coi như không còn giá trị.
STALE_S = 15.0


class Freshest:
    """Giá trị mới nhất kèm thời điểm nhận — để biết nó còn tươi hay đã ôi."""

    def __init__(self) -> None:
        self.value: dict | None = None
        self.at: float = 0.0

    def set(self, v: dict) -> None:
        self.value, self.at = v, time.time()

    @property
    def age(self) -> float:
        return time.time() - self.at if self.at else float("inf")

    @property
    def fresh(self) -> bool:
        return self.age < STALE_S


def main() -> None:
    setup_logging()
    bus = OiBus("oi-context")

    vision, radar, lux = Freshest(), Freshest(), Freshest()

    @bus.on(proto.TOPIC_CTX_VISION)
    def _(topic, payload):
        vision.set(payload)

    @bus.on(f"oi/state/{proto.NODE_GATE}")
    def _(topic, payload):
        radar.set(payload)

    @bus.on(f"oi/tele/{proto.NODE_GATE}")
    def _(topic, payload):
        if proto.K_PRESENCE in payload:
            radar.set(payload)

    @bus.on(f"oi/tele/{proto.NODE_CONTROLLER}")
    def _(topic, payload):
        if proto.K_LUX in payload:
            lux.set(payload)

    def tick() -> None:
        now = datetime.now()

        v_ok = vision.fresh and vision.value is not None
        r_ok = radar.fresh and radar.value is not None

        v_count = int(vision.value.get("count", 0)) if v_ok else 0
        r_pres = bool(radar.value.get(proto.K_PRESENCE, False)) if r_ok else False

        # Xử lý bất đồng: radar báo có người, camera báo không.
        # Tin RADAR. Lý do: điểm mù của camera (người bị che, ngồi ngoài khung,
        # ánh sáng kém) gây bỏ sót thường xuyên hơn nhiều so với radar báo nhầm.
        # Bỏ sót người đang ở trong phòng là lỗi người dùng thấy ngay; báo thừa
        # chỉ tốn ít điện. Giả định này chính là thứ Thí nghiệm B đo lại.
        occupied = bool(v_count > 0 or r_pres)

        fused = {
            proto.K_TS: int(time.time()),
            proto.K_PRESENCE: {
                "radar": r_pres,
                "vision": v_count > 0,
                "count": v_count,
                proto.K_CONF: round(float(vision.value.get(proto.K_CONF, 0.0)), 2) if v_ok else 0.0,
                "occupied": occupied,
            },
            "people": vision.value.get("people", []) if v_ok else [],
            proto.K_LUX: float(lux.value[proto.K_LUX]) if (lux.fresh and lux.value) else None,
            "clock": {
                "hour": now.hour,
                "dow": now.weekday(),
                "minute": now.minute,
            },
            # Nguồn nào còn sống — dashboard và agent dùng để biết đang ở mức
            # suy giảm nào mà không phải tự suy đoán.
            "sources": {
                "vision": round(vision.age, 1) if vision.at else None,
                "radar": round(radar.age, 1) if radar.at else None,
                "lux": round(lux.age, 1) if lux.at else None,
            },
            "degraded": "L0" if (v_ok and r_ok) else ("L4" if r_ok else "L2"),
        }

        bus.publish(proto.TOPIC_CTX_FUSED, fused, qos=0, retain=True)

    log.info("oi-context chạy · gộp mỗi giây, phát lên %s có retain", proto.TOPIC_CTX_FUSED)
    bus.run_forever(tick=tick, tick_s=float(os.getenv("OI_CTX_TICK_S", "1")))


if __name__ == "__main__":
    main()
