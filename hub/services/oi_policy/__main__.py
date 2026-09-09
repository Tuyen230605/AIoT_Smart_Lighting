"""oi-policy — hệ thống tự quyết mức sáng, và tự dừng lại khi người dùng chen vào.

Ở MVP đây mới là luật nền (bậc 0 + 1). Mô hình LightGBM học phần dư (bậc 2) lắp
vào ở G3.7, đúng chỗ này, đọc dữ liệu mà dịch vụ này đang giúp tích luỹ.
"""

from __future__ import annotations

import logging
import os
import time
from datetime import datetime

from oi_common import protocol as proto
from oi_common.bus import OiBus, setup_logging

from .baseline import AUTO_MIN_INTERVAL_S, MANUAL_LOCK_S, ZoneMemory, decide

log = logging.getLogger("oi-policy")

NODE = proto.NODE_CONTROLLER
USER_SOURCES = {
    int(proto.CmdSource.VOICE_LOCAL),
    int(proto.CmdSource.VOICE_HUB),
    int(proto.CmdSource.BUTTON),
    int(proto.CmdSource.WEB),
}


def main() -> None:
    setup_logging()
    bus = OiBus("oi-policy")
    memory = ZoneMemory()

    ctx: dict = {}
    last_lux: float | None = None
    last_auto_t = 0.0
    last_manual_t = 0.0
    last_auto_bri: int | None = None
    last_sent_bri: int | None = None

    @bus.on(proto.TOPIC_CTX_FUSED)
    def _(topic, payload):
        nonlocal ctx
        ctx = payload

    @bus.on(f"oi/tele/{NODE}")
    def _(topic, payload):
        nonlocal last_lux
        if proto.K_LUX in payload:
            last_lux = float(payload[proto.K_LUX])

    @bus.on(f"oi/state/{NODE}")
    def _(topic, payload):
        nonlocal last_manual_t, last_auto_bri
        src = payload.get(proto.K_SOURCE)
        if src in USER_SOURCES:
            last_manual_t = time.time()

            # Bậc 1 học ở đây: người dùng vừa sửa mức mà hệ thống đặt, chênh
            # lệch đó là tín hiệu về sở thích thật của họ.
            bri = payload.get(proto.K_BRIGHTNESS)
            if last_auto_bri is not None and bri is not None:
                zone = _zone_name(ctx)
                if zone:
                    delta = float(bri) - float(last_auto_bri)
                    memory.observe(zone, datetime.now().hour, delta)
                    log.info("Ghi nhận sở thích: %s lệch %+.0f so với đề xuất", zone, delta)
            last_auto_bri = None

    def _zone_name(c: dict) -> str | None:
        people = c.get("people") or []
        return people[0].get("zone") if people else None

    def tick() -> None:
        nonlocal last_auto_t, last_auto_bri, last_sent_bri
        now = time.time()

        # ── Rào 1: khoá sau can thiệp thủ công ──
        # Không gì làm người dùng bỏ hệ thống nhanh bằng việc nó cãi lại mình.
        if now - last_manual_t < MANUAL_LOCK_S:
            return

        # ── Rào 2: giới hạn nhịp thay đổi tự động ──
        if now - last_auto_t < AUTO_MIN_INTERVAL_S:
            return

        presence = ctx.get(proto.K_PRESENCE) or {}
        occupied = bool(presence.get("radar") or presence.get("vision"))
        hour = datetime.now().hour + datetime.now().minute / 60

        d = decide(hour, occupied, last_lux, _zone_name(ctx), memory)
        if d is None:
            return

        # ── Rào 3: không phát lại lệnh trùng ──
        # Publish y hệt giá trị đang có chỉ tạo nhiễu trong nhật ký và làm
        # chỉ số S4 sai (đếm thừa "hành động tự động").
        if last_sent_bri is not None and abs(d.brightness - last_sent_bri) < 5:
            return

        last_auto_t = now
        last_auto_bri = d.brightness
        last_sent_bri = d.brightness

        bus.publish_cmd(NODE, {
            proto.K_MODE: int(proto.LightMode.OFF if d.brightness == 0 else proto.LightMode.SOLID),
            proto.K_BRIGHTNESS: d.brightness,
            proto.K_CCT: d.cct,
            proto.K_FADE_MS: 1500,
            proto.K_SOURCE: int(proto.CmdSource.AGENT),
            proto.K_SEQ: int(now),
        })
        bus.publish(proto.TOPIC_AGENT_LOG, {
            proto.K_TS: int(now),
            proto.K_NODE: NODE,
            proto.K_REASON: f"đặt {d.brightness}/255 vì {d.reason}",
        }, qos=1)
        log.info("→ %d/255 · %s", d.brightness, d.reason)

    log.info("oi-policy chạy · luật nền, chưa có mô hình học (lắp ở G3.7)")
    bus.run_forever(tick=tick, tick_s=float(os.getenv("OI_POLICY_TICK_S", "10")))


if __name__ == "__main__":
    main()
