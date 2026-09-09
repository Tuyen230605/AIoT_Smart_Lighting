"""Luật nền — bậc 0 và bậc 1 của mô hình học sở thích (ADR 0002).

VÌ SAO LÀM PHẦN NÀY TRƯỚC MÔ HÌNH HỌC MÁY
Nó chạy được **ngày đầu cắm điện, với 0 mẫu dữ liệu**. Điều đó quan trọng hơn
vẻ ngoài của nó: chính vì hệ thống tự làm gì đó mà người dùng mới có cái để
chỉnh tay, và mỗi lần chỉnh tay là một mẫu huấn luyện chảy vào SQLite. Không có
luật nền thì không có hành động tự động, không có hành động tự động thì không có
dữ liệu, và mô hình LightGBM ở G3.7 sẽ không có gì để học.

Phần lớn cảm giác "hệ thống đang học" mà người dùng cảm nhận được đến từ bậc 1
(độ lệch co ngót) chứ không phải từ mô hình phức tạp phía sau.
"""

from __future__ import annotations

import math
from dataclasses import dataclass, field

# ── Rào an toàn, khớp với oi_state.h của firmware ──────────────
NIGHT_FLOOR_BRI = 8          # không bao giờ tắt hẳn khi có người sau khi trời tối
MANUAL_LOCK_S = 20 * 60      # người vừa chỉnh tay thì hệ thống im trong 20 phút
AUTO_MIN_INTERVAL_S = 2 * 60  # nhịp tối đa của thay đổi tự động
MODEL_MAX_DEVIATION = 60     # trần quyền lực của phần học so với luật nền

# Đường cong nhịp sinh học: (giờ, độ sáng 0..255, nhiệt độ màu K).
# Nội suy tuyến tính giữa các mốc, vòng qua nửa đêm.
CIRCADIAN = [
    (0,  20,  2200),
    (6,  60,  2700),
    (8,  180, 4000),
    (12, 220, 5000),
    (17, 200, 4000),
    (20, 140, 3000),
    (22, 80,  2700),
    (23, 40,  2200),
]


def _interp(hour: float) -> tuple[int, int]:
    """Tra đường cong nhịp sinh học, nội suy giữa hai mốc gần nhất."""
    pts = CIRCADIAN + [(24, CIRCADIAN[0][1], CIRCADIAN[0][2])]
    for i in range(len(pts) - 1):
        h0, b0, c0 = pts[i]
        h1, b1, c1 = pts[i + 1]
        if h0 <= hour <= h1:
            t = 0.0 if h1 == h0 else (hour - h0) / (h1 - h0)
            return round(b0 + (b1 - b0) * t), round(c0 + (c1 - c0) * t)
    return CIRCADIAN[0][1], CIRCADIAN[0][2]


def lux_compensation(target_lux: float, measured_lux: float) -> float:
    """Hệ số nhân để bù ánh sáng tự nhiên.

    Phòng đã sáng sẵn thì đèn phải bớt đi. Dùng thang log vì cảm nhận sáng của
    mắt người là log, không phải tuyến tính — chênh 100 lx ở phòng tối cảm thấy
    rõ hơn nhiều so với chênh 100 lx ở phòng đã sáng.
    """
    if measured_lux <= 1:
        return 1.0
    ratio = math.log10(max(measured_lux, 1.0) + 1) / math.log10(target_lux + 1)
    return max(0.0, min(1.0, 1.0 - 0.7 * ratio))


@dataclass
class ZoneMemory:
    """Bậc 1: độ lệch trung bình mà người dùng hay chỉnh, theo (vùng × khung giờ).

    Co ngót `n/(n+5)`: với 1 mẫu thì chỉ tin 17 %, với 20 mẫu thì tin 80 %. Nhờ
    vậy một lần chỉnh cá biệt không kéo lệch cả hành vi, mà thói quen lặp lại
    thì dần dần được nghe theo.
    """

    offsets: dict[tuple[str, int], list[float]] = field(default_factory=dict)

    def observe(self, zone: str, hour: int, delta: float) -> None:
        self.offsets.setdefault((zone, hour // 3), []).append(delta)

    def offset(self, zone: str, hour: int) -> float:
        samples = self.offsets.get((zone, hour // 3), [])
        if not samples:
            return 0.0
        n = len(samples)
        mean = sum(samples) / n
        shrunk = mean * (n / (n + 5))
        return max(-MODEL_MAX_DEVIATION, min(MODEL_MAX_DEVIATION, shrunk))


@dataclass
class Decision:
    brightness: int
    cct: int
    reason: str


def decide(
    hour: float,
    occupied: bool,
    measured_lux: float | None,
    zone: str | None,
    memory: ZoneMemory,
    target_lux: float = 300.0,
) -> Decision | None:
    """Quyết định mức sáng cho lúc này. `None` nghĩa là không cần đổi gì.

    Trả về kèm **lý do đọc được bằng tiếng Việt** — yêu cầu FR-E3. Đây cũng là
    lý do chọn luật và mô hình giải thích được thay vì mạng nơ-ron (ADR 0002):
    chủ nhà luôn phải trả lời được câu "vì sao đèn vừa tự bật".
    """
    if not occupied:
        return Decision(0, 2700, "không có ai trong phòng")

    base_bri, base_cct = _interp(hour)
    parts = [f"nhịp sinh học lúc {int(hour)}h cho {base_bri}"]

    bri = float(base_bri)
    if measured_lux is not None:
        factor = lux_compensation(target_lux, measured_lux)
        bri *= factor
        parts.append(f"phòng đang {measured_lux:.0f} lx nên nhân {factor:.2f}")

    if zone:
        off = memory.offset(zone, int(hour))
        if abs(off) >= 1:
            bri += off
            parts.append(f"lịch sử của bạn ở {zone} giờ này lệch {off:+.0f}")

    final = int(max(0, min(255, round(bri))))

    # Sàn ban đêm: có người mà đèn tắt hẳn sau khi trời tối là hành vi nguy hiểm,
    # không phải hành vi tiết kiệm.
    if final < NIGHT_FLOOR_BRI and (hour >= 19 or hour <= 6):
        final = NIGHT_FLOOR_BRI
        parts.append(f"nâng lên sàn ban đêm {NIGHT_FLOOR_BRI}")

    return Decision(final, base_cct, " · ".join(parts))
