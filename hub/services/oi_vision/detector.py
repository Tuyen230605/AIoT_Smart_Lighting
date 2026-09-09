"""Phát hiện người trong khung hình.

HAI BỘ PHÁT HIỆN, CÓ CHỦ Ý
`YoloDetector` là bản thật (YOLOv8n). `MotionDetector` là bản dự phòng dùng
sai khác khung hình — kém hơn hẳn, nhưng nó khiến **cả lát cắt dọc chạy được
ngay trong ngày đầu**, trước khi tải xong trọng số mô hình. Đó là tinh thần của
MVP: mọi tầng chạy mỏng trước, đào sâu sau.

Bản dự phòng cũng có ích lâu dài: nó là mốc đối chứng phi-AI rẻ nhất để trả lời
câu "mô hình học sâu có thật sự cần không" ở Thí nghiệm B.
"""

from __future__ import annotations

import logging
from dataclasses import dataclass
from typing import Protocol

import numpy as np

log = logging.getLogger("oi-vision")


@dataclass
class Detection:
    """Hộp bao đã chuẩn hoá về 0..1, gốc toạ độ ở góc trên trái."""

    x1: float
    y1: float
    x2: float
    y2: float
    conf: float

    @property
    def foot(self) -> tuple[float, float]:
        """Điểm chân — nơi người chạm sàn, dùng để gán vùng."""
        return ((self.x1 + self.x2) / 2, self.y2)


class Detector(Protocol):
    name: str

    def detect(self, frame: np.ndarray) -> list[Detection]: ...


class YoloDetector:
    """YOLOv8n, chỉ lấy lớp `person` (id 0 trong COCO)."""

    name = "yolov8n"

    def __init__(self, weights: str = "/models/yolov8n.pt", imgsz: int = 320, conf: float = 0.4):
        from ultralytics import YOLO  # nhập tại đây để bản dự phòng không cần thư viện này

        self.model = YOLO(weights)
        self.imgsz = imgsz
        self.conf = conf

    def detect(self, frame: np.ndarray) -> list[Detection]:
        h, w = frame.shape[:2]
        res = self.model.predict(
            frame, imgsz=self.imgsz, conf=self.conf, classes=[0], verbose=False
        )[0]

        out = []
        for box in res.boxes:
            x1, y1, x2, y2 = (float(v) for v in box.xyxy[0])
            out.append(Detection(x1 / w, y1 / h, x2 / w, y2 / h, float(box.conf[0])))
        return out


class MotionDetector:
    """Dự phòng: sai khác so với nền học dần.

    Giới hạn phải nói thẳng: nó phát hiện **chuyển động**, không phát hiện
    *người*. Người ngồi yên đọc sách sẽ biến mất khỏi nền sau vài chục giây —
    đúng điểm mù kinh điển mà radar sinh ra để bù, và cũng chính là lý do
    Thí nghiệm B so ba cấu hình radar / camera / hợp nhất.
    """

    name = "motion-fallback"

    def __init__(self, min_area: float = 0.012, learning_rate: float = 0.02):
        self.bg: np.ndarray | None = None
        self.min_area = min_area
        self.lr = learning_rate

    def detect(self, frame: np.ndarray) -> list[Detection]:
        import cv2

        gray = cv2.cvtColor(frame, cv2.COLOR_BGR2GRAY)
        gray = cv2.GaussianBlur(gray, (21, 21), 0).astype(np.float32)

        if self.bg is None:
            self.bg = gray
            return []

        diff = cv2.absdiff(gray, self.bg).astype(np.uint8)
        self.bg = (1 - self.lr) * self.bg + self.lr * gray

        _, thresh = cv2.threshold(diff, 18, 255, cv2.THRESH_BINARY)
        thresh = cv2.dilate(thresh, None, iterations=2)
        contours, _ = cv2.findContours(thresh, cv2.RETR_EXTERNAL, cv2.CHAIN_APPROX_SIMPLE)

        h, w = gray.shape
        out = []
        for c in contours:
            x, y, cw, ch = cv2.boundingRect(c)
            if (cw * ch) / (w * h) < self.min_area:
                continue
            out.append(Detection(x / w, y / h, (x + cw) / w, (y + ch) / h, 0.5))
        return out


def build_detector(weights: str, imgsz: int, conf: float) -> Detector:
    """Dùng YOLO nếu nạp được, không thì lùi về bản dự phòng và nói rõ ra."""
    try:
        d = YoloDetector(weights, imgsz, conf)
        log.info("Bộ phát hiện: YOLOv8n (%s)", weights)
        return d
    except Exception as e:
        log.warning(
            "Không nạp được YOLO (%s) — dùng bản dự phòng theo chuyển động. "
            "Độ chính xác thấp hơn nhiều và người ngồi yên sẽ bị bỏ sót.",
            e,
        )
        return MotionDetector()
