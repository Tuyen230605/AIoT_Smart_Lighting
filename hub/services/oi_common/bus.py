"""Lớp bọc MQTT dùng chung cho mọi dịch vụ trên hub.

VÌ SAO CÓ FILE NÀY
Chín dịch vụ đều cần đúng một thứ: nối tới broker, nghe vài chủ đề, đăng vài
chủ đề, và tự báo tử khi chết. Viết lại chín lần là chín cơ hội để lệch nhau ở
những chỗ khó thấy — quên `retain`, quên QoS, quên Last Will, mỗi nơi một kiểu
xử lý JSON hỏng.

Ranh giới giữa các dịch vụ là chủ đề MQTT (xem docs/02-architecture.md), nên
file này cố tình mỏng: nó không biết gì về nghiệp vụ, chỉ lo phần vận chuyển.
"""

from __future__ import annotations

import json
import logging
import os
import signal
import threading
import time
from collections.abc import Callable
from typing import Any

import paho.mqtt.client as mqtt

from . import protocol as proto

log = logging.getLogger(__name__)

Handler = Callable[[str, dict[str, Any]], None]


class OiBus:
    """Một kết nối MQTT, dùng lại được cho mọi dịch vụ.

    Ví dụ:
        bus = OiBus("oi-state")

        @bus.on("oi/state/+")
        def _(topic, payload):
            print(topic, payload)

        bus.run_forever()
    """

    def __init__(
        self,
        service: str,
        host: str | None = None,
        port: int | None = None,
        username: str | None = None,
        password: str | None = None,
    ) -> None:
        self.service = service
        self.host = host or os.getenv("OI_MQTT_HOST", "broker")
        self.port = port or int(os.getenv("OI_MQTT_PORT", "1883"))

        self._handlers: list[tuple[str, Handler]] = []
        self._stop = threading.Event()

        self.client = mqtt.Client(
            mqtt.CallbackAPIVersion.VERSION2,
            client_id=f"{service}-{os.getpid()}",
        )
        user = username if username is not None else os.getenv("OI_MQTT_USER", "svc")
        pwd = password if password is not None else os.getenv("OI_MQTT_PASS", "")
        if user:
            self.client.username_pw_set(user, pwd)

        # Last Will: broker tự phát hộ nếu tiến trình này chết đột ngột.
        # Không có nó, không ai phân biệt được "dịch vụ đang im" với "đã chết".
        self._lwt_topic = f"oi/sys/offline/{service}"
        self.client.will_set(
            self._lwt_topic,
            json.dumps({"svc": service, "online": False}),
            qos=1,
            retain=True,
        )

        self.client.on_connect = self._on_connect
        self.client.on_message = self._on_message
        self.client.on_disconnect = self._on_disconnect

    # ── đăng ký người nghe ────────────────────────────────────
    def on(self, topic_filter: str, qos: int = 0) -> Callable[[Handler], Handler]:
        """Decorator gắn một hàm vào một bộ lọc chủ đề."""

        def wrap(fn: Handler) -> Handler:
            self._handlers.append((topic_filter, fn))
            if self.client.is_connected():
                self.client.subscribe(topic_filter, qos)
            return fn

        return wrap

    # ── gửi ───────────────────────────────────────────────────
    def publish(
        self, topic: str, payload: dict[str, Any], qos: int = 0, retain: bool = False
    ) -> None:
        self.client.publish(topic, json.dumps(payload, separators=(",", ":")), qos, retain)

    def publish_state(self, node: str, state: dict[str, Any]) -> None:
        """Trạng thái LUÔN đi kèm retain — client mới vào phải biết ngay."""
        self.publish(proto.topic_state(node), state, qos=1, retain=True)

    def publish_cmd(self, node: str, cmd: dict[str, Any]) -> None:
        """Lệnh KHÔNG BAO GIỜ retain: lệnh là sự kiện, không phải trạng thái.

        Retain một lệnh nghĩa là mỗi client mới nối vào sẽ nhận lại lệnh cũ và
        thực thi nó — đúng loại lỗi mất hàng giờ để tìm ra.
        """
        cmd.setdefault(proto.K_TS, int(time.time()))
        self.publish(proto.topic_cmd(node), cmd, qos=1, retain=False)

    # ── callback ──────────────────────────────────────────────
    def _on_connect(self, client, userdata, flags, reason_code, properties=None) -> None:
        if reason_code != 0:
            log.error("%s: broker từ chối, mã %s", self.service, reason_code)
            return
        log.info("%s: đã nối broker %s:%d", self.service, self.host, self.port)

        for topic_filter, _ in self._handlers:
            client.subscribe(topic_filter, 0)

        client.publish(
            self._lwt_topic,
            json.dumps({"svc": self.service, "online": True}),
            qos=1,
            retain=True,
        )

    def _on_disconnect(self, client, userdata, flags, reason_code, properties=None) -> None:
        if not self._stop.is_set():
            log.warning("%s: mất kết nối (%s), paho sẽ tự nối lại", self.service, reason_code)

    def _on_message(self, client, userdata, msg: mqtt.MQTTMessage) -> None:
        try:
            payload = json.loads(msg.payload)
        except (json.JSONDecodeError, UnicodeDecodeError):
            log.warning("%s: bỏ bản tin không phải JSON trên %s", self.service, msg.topic)
            return
        if not isinstance(payload, dict):
            return

        for topic_filter, fn in self._handlers:
            if mqtt.topic_matches_sub(topic_filter, msg.topic):
                try:
                    fn(msg.topic, payload)
                except Exception:
                    # Một handler hỏng không được phép kéo sập cả dịch vụ.
                    log.exception("%s: lỗi khi xử lý %s", self.service, msg.topic)

    # ── vòng đời ──────────────────────────────────────────────
    def start(self) -> None:
        self.client.connect_async(self.host, self.port, keepalive=30)
        self.client.loop_start()

    def stop(self) -> None:
        self._stop.set()
        self.client.publish(
            self._lwt_topic,
            json.dumps({"svc": self.service, "online": False}),
            qos=1,
            retain=True,
        )
        self.client.loop_stop()
        self.client.disconnect()

    def run_forever(self, tick: Callable[[], None] | None = None, tick_s: float = 1.0) -> None:
        """Chạy tới khi bị dừng. `tick` gọi định kỳ nếu dịch vụ cần nhịp riêng."""
        for sig in (signal.SIGINT, signal.SIGTERM):
            signal.signal(sig, lambda *_: self._stop.set())

        self.start()
        try:
            while not self._stop.is_set():
                if tick is not None:
                    try:
                        tick()
                    except Exception:
                        log.exception("%s: lỗi trong tick", self.service)
                self._stop.wait(tick_s)
        finally:
            self.stop()
            log.info("%s: đã dừng gọn", self.service)


def setup_logging() -> None:
    logging.basicConfig(
        level=os.getenv("OI_LOG_LEVEL", "INFO"),
        format="%(asctime)s %(levelname)-7s %(name)s · %(message)s",
        datefmt="%H:%M:%S",
    )
