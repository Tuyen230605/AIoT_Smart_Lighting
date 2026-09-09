"""oi-state — nguồn sự thật duy nhất, và nơi sinh ra dữ liệu huấn luyện cho AI-6.

Nghe mọi thứ chảy qua broker, ghi vào SQLite, và phát hiện những lần người dùng
sửa lại quyết định của hệ thống.

Dịch vụ này cũng phát nhịp tim `oi/sys/heartbeat` 1 Hz. Đặt ở đây vì nó là dịch
vụ bắt buộc phải sống: node dùng nhịp tim để biết hub còn hay mất (mất 8 nhịp →
vào FALLBACK), nên nhịp tim phải chết cùng lúc với phần lõi của hub, không sớm
hơn và không muộn hơn.
"""

from __future__ import annotations

import logging
import os
import time

from oi_common import protocol as proto
from oi_common.bus import OiBus, setup_logging

from .db import StateDB

log = logging.getLogger("oi-state")


def main() -> None:
    setup_logging()
    db = StateDB(os.getenv("OI_DB_PATH", "/data/oi.sqlite"))
    bus = OiBus("oi-state")

    # Ảnh chụp ngữ cảnh mới nhất, đính kèm vào mỗi sự kiện trạng thái để về sau
    # huấn luyện được: không có ngữ cảnh lúc đó thì một dòng "đặt sáng 132"
    # không nói lên điều gì.
    ctx_snapshot: dict = {}

    @bus.on(proto.TOPIC_CTX_FUSED)
    def _(topic, payload):
        nonlocal ctx_snapshot
        ctx_snapshot = payload

    @bus.on("oi/state/+")
    def _(topic, payload):
        node = topic.rsplit("/", 1)[-1]
        event_id = db.record_event(node, "state", payload, ctx_snapshot)

        corr = db.detect_correction(node, event_id)
        if corr:
            # Ghi ra log để nhìn thấy được ngay trong lúc vận hành: mỗi dòng
            # này là một mẫu huấn luyện vừa được thu thập.
            log.info(
                "📝 Người dùng sửa lại sau %.0f s trên %s: %s → %s",
                corr["seconds"],
                node,
                corr["auto_bri"],
                corr["manual_bri"],
            )
            bus.publish(
                proto.TOPIC_AGENT_LOG,
                {
                    proto.K_TS: int(time.time()),
                    proto.K_NODE: node,
                    proto.K_REASON: (
                        f"người dùng chỉnh lại sau {corr['seconds']:.0f} s "
                        f"({corr['auto_bri']} → {corr['manual_bri']})"
                    ),
                    "correction": True,
                },
                qos=1,
            )

    @bus.on("oi/cmd/+")
    def _(topic, payload):
        node = topic.rsplit("/", 1)[-1]
        db.record_event(node, "cmd", payload, ctx_snapshot)

    @bus.on(proto.TOPIC_AGENT_LOG)
    def _(topic, payload):
        if not payload.get("correction"):   # tránh ghi lại chính bản tin mình vừa phát
            db.record_agent_log(payload)

    # ── nhịp tim + số liệu tổng hợp ───────────────────────────
    last_summary = 0.0

    def tick() -> None:
        nonlocal last_summary
        now = time.time()

        bus.publish(proto.TOPIC_HEARTBEAT, {proto.K_TS: int(now)}, qos=0)

        # Mỗi phút phát lại chỉ số S4 để dashboard hiển thị mà không phải
        # truy vấn cơ sở dữ liệu — dashboard chỉ nói chuyện với broker.
        if now - last_summary >= 60:
            last_summary = now
            stats = db.correction_rate(days=7)
            bus.publish("oi/sys/stats", stats, qos=0, retain=True)

    log.info("oi-state khởi động · cơ sở dữ liệu: %s", db.path)
    bus.run_forever(tick=tick, tick_s=proto.HEARTBEAT_PERIOD_MS / 1000)
    db.close()


if __name__ == "__main__":
    main()
