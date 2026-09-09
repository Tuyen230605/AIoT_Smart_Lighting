"""oi-vision — camera thành "ai đang ở vùng nào", cộng luồng hình có chú giải.

Khung hình thô không bao giờ rời khỏi tiến trình này và không bao giờ chạm đĩa.
Thứ đi ra ngoài là hai loại: toạ độ + nhãn qua MQTT, và (khi có người đang xem)
một luồng JPEG **đã vẽ chồng chú giải** trong mạng LAN — xem ADR 0005.
"""

from __future__ import annotations

import logging
import os
import time

from oi_common import protocol as proto
from oi_common.bus import OiBus, setup_logging

from .detector import build_detector
from .stream import FrameBuffer, serve_in_background
from .zones import assign, load_zones

log = logging.getLogger("oi-vision")

# Màu vẽ chú giải, BGR. Hổ quang cho hộp bao để khớp hệ màu của dự án.
C_BOX = (19, 134, 217)
C_TEXT = (255, 255, 255)
C_ZONE = (137, 102, 27)


def open_camera(width: int, height: int):
    """Camera Module 3 qua picamera2; lùi về V4L2 khi chạy trên máy khác."""
    try:
        from picamera2 import Picamera2

        cam = Picamera2()
        cam.configure(
            cam.create_video_configuration(
                main={"size": (width, height), "format": "RGB888"}
            )
        )
        cam.start()
        log.info("Camera: picamera2 %dx%d", width, height)
        return ("picamera2", cam)
    except Exception as e:
        log.warning("Không mở được picamera2 (%s) — thử V4L2", e)

    import cv2

    cap = cv2.VideoCapture(int(os.getenv("OI_CAM_INDEX", "0")))
    cap.set(cv2.CAP_PROP_FRAME_WIDTH, width)
    cap.set(cv2.CAP_PROP_FRAME_HEIGHT, height)
    if not cap.isOpened():
        raise RuntimeError("Không mở được camera nào")
    log.info("Camera: V4L2 %dx%d", width, height)
    return ("v4l2", cap)


def read_frame(kind, cam):
    if kind == "picamera2":
        return cam.capture_array()
    ok, frame = cam.read()
    return frame if ok else None


def annotate(frame, dets, zone_names, brightness_hint):
    """Vẽ chồng thông tin — biến luồng hình thành công cụ giải thích (FR-E3)."""
    import cv2

    h, w = frame.shape[:2]
    for det, zname in zip(dets, zone_names, strict=False):
        x1, y1 = int(det.x1 * w), int(det.y1 * h)
        x2, y2 = int(det.x2 * w), int(det.y2 * h)
        cv2.rectangle(frame, (x1, y1), (x2, y2), C_BOX, 2)

        label = f"{zname or 'ngoai vung'} · {det.conf:.2f}"
        (tw, th), _ = cv2.getTextSize(label, cv2.FONT_HERSHEY_SIMPLEX, 0.45, 1)
        cv2.rectangle(frame, (x1, max(0, y1 - th - 8)), (x1 + tw + 8, y1), C_BOX, -1)
        cv2.putText(frame, label, (x1 + 4, max(10, y1 - 5)),
                    cv2.FONT_HERSHEY_SIMPLEX, 0.45, C_TEXT, 1, cv2.LINE_AA)

        # Điểm chân — chỗ thực sự quyết định người thuộc vùng nào
        fx, fy = det.foot
        cv2.circle(frame, (int(fx * w), int(fy * h)), 4, C_BOX, -1)

    banner = f"nguoi: {len(dets)}"
    if brightness_hint is not None:
        banner += f" · he thong dang tinh: {brightness_hint}/255"
    cv2.rectangle(frame, (0, 0), (w, 22), C_ZONE, -1)
    cv2.putText(frame, banner, (8, 15), cv2.FONT_HERSHEY_SIMPLEX, 0.45, C_TEXT, 1, cv2.LINE_AA)
    return frame


def main() -> None:
    setup_logging()
    import cv2

    width = int(os.getenv("OI_CAM_WIDTH", "640"))
    height = int(os.getenv("OI_CAM_HEIGHT", "480"))
    target_fps = float(os.getenv("OI_CAM_FPS", "8"))
    stream_fps = float(os.getenv("OI_STREAM_FPS", "6"))
    jpeg_q = int(os.getenv("OI_STREAM_QUALITY", "70"))

    zones = load_zones(os.getenv("OI_ZONES_PATH", "/data/zones.json"))
    detector = build_detector(
        os.getenv("OI_YOLO_WEIGHTS", "/models/yolov8n.pt"),
        int(os.getenv("OI_YOLO_IMGSZ", "320")),
        float(os.getenv("OI_YOLO_CONF", "0.4")),
    )

    buf = FrameBuffer()
    serve_in_background(buf, int(os.getenv("OI_STREAM_PORT", "8090")))

    bus = OiBus("oi-vision")
    bus.start()

    # Mức sáng hệ thống đang tính — chỉ để hiển thị trên luồng hình, giúp người
    # xem nối được "thấy người ở đây" với "nên đèn sáng chừng này".
    brightness_hint = None

    @bus.on("oi/state/node1")
    def _(topic, payload):
        nonlocal brightness_hint
        brightness_hint = payload.get(proto.K_BRIGHTNESS)

    kind, cam = open_camera(width, height)
    period = 1.0 / target_fps
    stream_period = 1.0 / stream_fps
    last_stream = 0.0
    frames = 0
    t_fps = time.time()

    log.info("oi-vision chạy · %d vùng · %s", len(zones), detector.name)
    try:
        while True:
            t0 = time.time()
            frame = read_frame(kind, cam)
            if frame is None:
                time.sleep(0.1)
                continue

            dets = detector.detect(frame)

            zone_names, zone_mask = [], 0
            people = []
            for d in dets:
                fx, fy = d.foot
                z = assign(zones, fx, fy)
                zone_names.append(z.name if z else None)
                if z:
                    zone_mask |= z.mask
                people.append({
                    "xy": [round(fx, 3), round(fy, 3)],
                    "zone": z.name if z else None,
                    proto.K_CONF: round(d.conf, 2),
                })

            bus.publish(proto.TOPIC_CTX_VISION, {
                proto.K_TS: int(time.time()),
                "count": len(dets),
                "people": people,
                proto.K_ZONE: zone_mask,
                proto.K_CONF: round(max((d.conf for d in dets), default=0.0), 2),
                "detector": detector.name,
            })

            # ── Ràng buộc 1 của ADR 0005 ──
            # Không ai xem thì KHÔNG mã hoá. Suy luận ở trên vẫn chạy vì đèn
            # phụ thuộc vào nó; chỉ phần phát hình mới bị bỏ.
            now = time.time()
            if buf.has_viewers and (now - last_stream) >= stream_period:
                last_stream = now
                annotated = annotate(frame.copy(), dets, zone_names, brightness_hint)
                ok, jpeg = cv2.imencode(".jpg", annotated,
                                        [int(cv2.IMWRITE_JPEG_QUALITY), jpeg_q])
                if ok:
                    buf.publish(jpeg.tobytes())

            frames += 1
            if now - t_fps >= 30:
                log.info("%.1f FPS · %d người · %s người xem",
                         frames / (now - t_fps), len(dets),
                         buf.viewers if buf.viewers else "không")
                frames, t_fps = 0, now

            elapsed = time.time() - t0
            if elapsed < period:
                time.sleep(period - elapsed)
    except KeyboardInterrupt:
        pass
    finally:
        bus.stop()
        log.info("oi-vision đã dừng")


if __name__ == "__main__":
    main()
