"""Chó canh khung hình — biến "camera treo im lặng" thành "tiến trình chết có tiếng".

VÌ SAO CẦN
Khi cáp CSI tiếp xúc chập chờn, libcamera báo "Camera frontend has timed out"
rồi `capture_array()` **chặn mãi mãi** bên trong mã C. Tiến trình vẫn sống, nên
systemd thấy `active` và không khởi động lại — đèn mất nguồn thị giác hàng giờ
mà không có gì báo động. Đã xảy ra thật trên Pi 5 + Camera v1.3 (OV5647).

CÁCH LÀM
Một luồng phụ theo dõi thời điểm nhận khung hình cuối. Quá `timeout_s` mà không
có khung mới thì gọi `on_stall`. Trong dịch vụ thật, `on_stall` ghi log rồi
`os._exit(1)`: luồng chính đang kẹt trong mã C nên `sys.exit()` không với tới
được. Thoát cứng là đủ an toàn ở đây — broker tự phát Last Will báo dịch vụ đã
chết, và systemd (`Restart=always`) mở lại camera từ đầu sau vài giây.

Module này cố ý không biết gì về camera, để kiểm thử được trên laptop.
"""

from __future__ import annotations

import threading
import time
from collections.abc import Callable

Clock = Callable[[], float]


class FrameWatchdog:
    def __init__(
        self,
        timeout_s: float,
        on_stall: Callable[[float], None],
        clock: Clock = time.monotonic,
    ) -> None:
        if timeout_s <= 0:
            raise ValueError("timeout_s phải dương")
        self.timeout_s = timeout_s
        self._on_stall = on_stall
        self._clock = clock
        self._last = clock()
        self._stop = threading.Event()
        self._thread: threading.Thread | None = None

    def feed(self) -> None:
        """Gọi mỗi khi nhận được một khung hình hợp lệ."""
        self._last = self._clock()

    @property
    def age(self) -> float:
        """Số giây kể từ khung hình cuối."""
        return self._clock() - self._last

    def stalled(self) -> bool:
        return self.age > self.timeout_s

    def check(self) -> bool:
        """Kiểm tra một lần; gọi `on_stall` nếu đã quá hạn. Trả về True nếu đã gọi."""
        if self.stalled():
            self._on_stall(self.age)
            return True
        return False

    def start(self, interval_s: float = 1.0) -> None:
        """Chạy kiểm tra định kỳ trên một luồng nền. Báo động tối đa một lần."""

        def loop() -> None:
            while not self._stop.wait(interval_s):
                if self.check():
                    return

        self._thread = threading.Thread(target=loop, name="frame-watchdog", daemon=True)
        self._thread.start()

    def stop(self) -> None:
        self._stop.set()
        if self._thread is not None:
            self._thread.join(timeout=2.0)
