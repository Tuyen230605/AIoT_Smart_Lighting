"""Máy chủ MJPEG cho luồng camera đã chú giải (ADR 0005).

RÀNG BUỘC 1 CỦA ADR 0005 NẰM Ở ĐÂY
`viewers` đếm số trình duyệt đang thực sự mở luồng. Khi bằng 0, vòng lặp thị
giác **không mã hoá JPEG** — nó vẫn chạy suy luận (đèn vẫn phải hoạt động) nhưng
bỏ hẳn phần tốn CPU nhất của việc phát hình. Đóng tab là CPU giảm thấy được, và
đó chính là cách kiểm chứng ràng buộc này.

RÀNG BUỘC 3: không có đường nào ghi khung hình ra đĩa. Ảnh chỉ tồn tại trong bộ
nhớ, dưới dạng một khung mới nhất bị ghi đè liên tục.
"""

from __future__ import annotations

import logging
import threading
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

log = logging.getLogger("oi-vision")

BOUNDARY = "oiframe"


class FrameBuffer:
    """Giữ đúng MỘT khung JPEG mới nhất. Không hàng đợi, không lịch sử.

    Người xem chậm sẽ bỏ khung chứ không làm tồn bộ nhớ — với luồng xem trực
    tiếp, khung cũ không có giá trị gì.
    """

    def __init__(self) -> None:
        self._cond = threading.Condition()
        self._jpeg: bytes | None = None
        self._seq = 0
        self.viewers = 0

    def publish(self, jpeg: bytes) -> None:
        with self._cond:
            self._jpeg = jpeg
            self._seq += 1
            self._cond.notify_all()

    def wait_next(self, last_seq: int, timeout: float = 5.0) -> tuple[bytes | None, int]:
        with self._cond:
            if self._seq == last_seq:
                self._cond.wait(timeout)
            return self._jpeg, self._seq

    @property
    def has_viewers(self) -> bool:
        return self.viewers > 0


def make_handler(buf: FrameBuffer):
    class Handler(BaseHTTPRequestHandler):
        protocol_version = "HTTP/1.1"

        def log_message(self, *args):      # tắt log mỗi khung ra stdout
            pass

        def do_GET(self):
            if self.path.startswith("/stream.mjpg"):
                self._serve_stream()
            elif self.path.startswith("/health"):
                self.send_response(200)
                self.send_header("Content-Type", "text/plain")
                self.send_header("Content-Length", "2")
                self.end_headers()
                self.wfile.write(b"ok")
            else:
                self.send_error(404)

        def _serve_stream(self):
            self.send_response(200)
            self.send_header("Age", "0")
            self.send_header("Cache-Control", "no-cache, private")
            self.send_header("Pragma", "no-cache")
            # Trang dashboard được phục vụ từ cổng 80, luồng này ở 8090 —
            # khác cổng nên trình duyệt coi là khác nguồn.
            self.send_header("Access-Control-Allow-Origin", "*")
            self.send_header("Content-Type", f"multipart/x-mixed-replace; boundary={BOUNDARY}")
            self.end_headers()

            buf.viewers += 1
            log.info("Có người mở luồng hình (%d đang xem)", buf.viewers)
            last = -1
            try:
                while True:
                    jpeg, last = buf.wait_next(last)
                    if jpeg is None:
                        continue
                    self.wfile.write(f"--{BOUNDARY}\r\n".encode())
                    self.wfile.write(b"Content-Type: image/jpeg\r\n")
                    self.wfile.write(f"Content-Length: {len(jpeg)}\r\n\r\n".encode())
                    self.wfile.write(jpeg)
                    self.wfile.write(b"\r\n")
            except (BrokenPipeError, ConnectionResetError):
                pass                       # người xem đóng tab — bình thường
            finally:
                buf.viewers -= 1
                log.info("Đã đóng luồng hình (%d còn xem)", buf.viewers)

    return Handler


def serve_in_background(buf: FrameBuffer, port: int = 8090) -> ThreadingHTTPServer:
    # Chỉ nghe trên LAN. Ràng buộc 2 của ADR 0005 được giữ ở tầng cấu hình
    # đường hầm: đường hầm chỉ phơi cổng dashboard, không phơi cổng này.
    srv = ThreadingHTTPServer(("0.0.0.0", port), make_handler(buf))
    threading.Thread(target=srv.serve_forever, daemon=True).start()
    log.info("Luồng hình sẵn sàng tại :%d/stream.mjpg (chỉ mã hoá khi có người xem)", port)
    return srv
