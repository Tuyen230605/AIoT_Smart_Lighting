"""Kiểm thử chó canh khung hình và phần chữ vẽ chồng của oi-vision.

VÌ SAO KIỂM THỬ
Chó canh là thứ duy nhất đứng giữa "cáp camera lỏng" và "đèn mất thị giác hàng
giờ mà không ai biết". Báo động trễ thì hỏng mục đích; báo động nhầm thì dịch vụ
tự giết mình liên tục khi camera vẫn chạy bình thường. Cả hai phía đều phải đúng.
"""

from __future__ import annotations

import threading

import pytest
from oi_vision.text import to_ascii
from oi_vision.watchdog import FrameWatchdog


class FakeClock:
    def __init__(self) -> None:
        self.t = 1000.0

    def __call__(self) -> float:
        return self.t


@pytest.fixture
def clock():
    return FakeClock()


def test_khong_bao_dong_khi_hinh_den_deu(clock):
    stalls = []
    wd = FrameWatchdog(15, stalls.append, clock=clock)
    for _ in range(100):          # 100 khung, mỗi khung cách 0.125 s (8 FPS)
        clock.t += 0.125
        wd.feed()
        assert not wd.check()
    assert stalls == []


def test_bao_dong_khi_camera_treo(clock):
    stalls = []
    wd = FrameWatchdog(15, stalls.append, clock=clock)
    wd.feed()
    clock.t += 14.9
    assert not wd.check()         # chưa quá hạn
    clock.t += 0.2
    assert wd.check()
    assert stalls and stalls[0] == pytest.approx(15.1)


def test_bao_dong_khi_khong_co_khung_dau_tien(clock):
    """Camera mở xong nhưng chưa bao giờ ra hình — đúng tình huống timeout lúc khởi động."""
    stalls = []
    wd = FrameWatchdog(15, stalls.append, clock=clock)
    clock.t += 16
    assert wd.check()


def test_feed_dat_lai_dong_ho(clock):
    wd = FrameWatchdog(15, lambda age: None, clock=clock)
    clock.t += 14
    wd.feed()
    clock.t += 14
    assert not wd.stalled()


def test_luong_nen_chi_bao_dong_mot_lan():
    fired = threading.Event()
    calls = []

    def on_stall(age):
        calls.append(age)
        fired.set()

    wd = FrameWatchdog(0.05, on_stall)
    wd.start(interval_s=0.01)
    assert fired.wait(2.0), "chó canh không báo động"
    wd.stop()
    assert len(calls) == 1


def test_timeout_phai_duong():
    with pytest.raises(ValueError):
        FrameWatchdog(0, lambda age: None)


@pytest.mark.parametrize(
    "goc, mong_doi",
    [
        ("ghế đọc sách", "ghe doc sach"),
        ("bàn làm việc", "ban lam viec"),
        ("Đèn cổng", "Den cong"),
        ("ghế đọc sách · 0.50", "ghe doc sach - 0.50"),
        ("ngoai vung | 0.42", "ngoai vung | 0.42"),
    ],
)
def test_bo_dau_cho_putText(goc, mong_doi):
    assert to_ascii(goc) == mong_doi


def test_ket_qua_luon_la_ascii():
    s = to_ascii("Ỷ ỹ ữ ặ ơ ư Ơ Ư đĐ “ngoặc” …")
    assert s.isascii()
