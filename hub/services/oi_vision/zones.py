"""Vùng chức năng trong phòng — biến toạ độ ảnh thành thứ người dùng hiểu được.

Toạ độ pixel vô nghĩa với người dùng; "vùng bàn làm việc" thì có nghĩa. Gán sai
vùng nghĩa là bật nhầm đèn, hậu quả người dùng thấy ngay lập tức.

Vùng định nghĩa bằng đa giác trên toạ độ ĐÃ CHUẨN HOÁ (0..1), không phải pixel,
để đổi độ phân giải camera không phải hiệu chuẩn lại.
"""

from __future__ import annotations

import json
from dataclasses import dataclass
from pathlib import Path

from oi_common import protocol as proto


@dataclass(frozen=True)
class Zone:
    name: str          # tên hiển thị, ví dụ "bàn làm việc"
    mask: int          # bit trong OI_ZONE_*, để gửi xuống node
    polygon: list[tuple[float, float]]

    def contains(self, x: float, y: float) -> bool:
        """Ray casting. Đa giác lồi hay lõm đều đúng."""
        inside = False
        n = len(self.polygon)
        for i in range(n):
            x1, y1 = self.polygon[i]
            x2, y2 = self.polygon[(i + 1) % n]
            if (y1 > y) != (y2 > y):
                x_cross = x1 + (y - y1) / (y2 - y1) * (x2 - x1)
                if x < x_cross:
                    inside = not inside
        return inside


DEFAULT_ZONES = [
    Zone("bàn làm việc", proto.Zone.DESK, [(0.02, 0.45), (0.42, 0.45), (0.42, 0.98), (0.02, 0.98)]),
    Zone("ghế đọc sách", proto.Zone.SEAT, [(0.58, 0.45), (0.98, 0.45), (0.98, 0.98), (0.58, 0.98)]),
    Zone("lối đi", proto.Zone.WALK, [(0.42, 0.40), (0.58, 0.40), (0.58, 0.98), (0.42, 0.98)]),
    Zone("giường", proto.Zone.BED, [(0.02, 0.05), (0.98, 0.05), (0.98, 0.40), (0.02, 0.40)]),
]


def load_zones(path: str | Path) -> list[Zone]:
    """Nạp vùng từ file cấu hình; thiếu file thì dùng bố cục mặc định.

    Bố cục mặc định gần như chắc chắn sai với phòng thật — nó chỉ để hệ thống
    chạy được ngay ngày đầu. Hiệu chuẩn thật làm ở G2.4 bằng công cụ vẽ đa giác.
    """
    p = Path(path)
    if not p.exists():
        return list(DEFAULT_ZONES)

    raw = json.loads(p.read_text(encoding="utf-8"))
    return [
        Zone(z["name"], int(z["mask"]), [(float(x), float(y)) for x, y in z["polygon"]])
        for z in raw["zones"]
    ]


def assign(zones: list[Zone], x: float, y: float) -> Zone | None:
    """Vùng chứa một điểm.

    Điểm dùng để gán là ĐIỂM CHÂN (đáy hộp bao), không phải tâm hộp: người đứng
    ở đâu được quyết định bởi chỗ chân chạm sàn, còn tâm hộp nằm ngang ngực và
    sẽ rơi sang vùng khác khi người đứng gần mép.
    """
    for z in zones:
        if z.contains(x, y):
            return z
    return None
