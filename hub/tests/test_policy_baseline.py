"""Kiểm thử luật nền và các rào an toàn của oi-policy.

Rào an toàn là thứ quyết định người dùng có giữ hệ thống hay gỡ nó xuống. Một
mô hình sai vài chục đơn vị độ sáng thì phiền; một hệ thống tắt đèn khi có người
trong phòng lúc nửa đêm, hoặc cãi lại người vừa chỉnh tay, thì bị gỡ.
"""

from __future__ import annotations

import pytest
from oi_policy.baseline import (
    MODEL_MAX_DEVIATION,
    NIGHT_FLOOR_BRI,
    ZoneMemory,
    decide,
    lux_compensation,
)


def test_khong_co_nguoi_thi_tat_den():
    d = decide(hour=20, occupied=False, measured_lux=10, zone=None, memory=ZoneMemory())
    assert d.brightness == 0
    assert "không có ai" in d.reason


def test_ban_ngay_sang_hon_ban_dem():
    mem = ZoneMemory()
    trua = decide(12, True, None, None, mem)
    khuya = decide(23, True, None, None, mem)
    assert trua.brightness > khuya.brightness


def test_nhiet_do_mau_am_dan_ve_dem():
    mem = ZoneMemory()
    assert decide(12, True, None, None, mem).cct > decide(22, True, None, None, mem).cct


def test_phong_da_sang_thi_den_bot_di():
    mem = ZoneMemory()
    toi = decide(19, True, 5.0, None, mem)
    sang = decide(19, True, 400.0, None, mem)
    assert sang.brightness < toi.brightness


def test_lux_bang_khong_thi_khong_bu():
    assert lux_compensation(300, 0) == 1.0


def test_san_ban_dem_khong_bao_gio_tat_han_khi_co_nguoi():
    """Phòng rất sáng lúc nửa đêm vẫn không được để đèn về 0 khi có người.

    Đây là rào an toàn, không phải tinh chỉnh thẩm mỹ: người đi lại trong bóng
    tối vì hệ thống "tiết kiệm điện" là một hỏng hóc nguy hiểm.
    """
    d = decide(hour=23, occupied=True, measured_lux=5000.0, zone=None, memory=ZoneMemory())
    assert d.brightness >= NIGHT_FLOOR_BRI
    assert "sàn ban đêm" in d.reason


def test_ban_ngay_khong_ap_san_ban_dem():
    d = decide(hour=13, occupied=True, measured_lux=5000.0, zone=None, memory=ZoneMemory())
    assert "sàn ban đêm" not in d.reason


def test_moi_quyet_dinh_deu_co_ly_do_doc_duoc():
    """FR-E3: chủ nhà phải luôn trả lời được câu 'vì sao đèn vừa tự bật'."""
    d = decide(21, True, 80.0, "bàn làm việc", ZoneMemory())
    assert d.reason and len(d.reason) > 10
    assert "nhịp sinh học" in d.reason


# ── Bậc 1: học độ lệch có co ngót ─────────────────────────────

def test_mot_mau_don_le_chi_anh_huong_rat_it():
    """Co ngót n/(n+5): một lần chỉnh cá biệt không được kéo lệch cả hành vi."""
    mem = ZoneMemory()
    mem.observe("bàn làm việc", 21, +60)
    assert mem.offset("bàn làm việc", 21) == pytest.approx(60 * 1 / 6, abs=0.1)


def test_thoi_quen_lap_lai_thi_duoc_nghe_theo():
    mem = ZoneMemory()
    for _ in range(20):
        mem.observe("bàn làm việc", 21, +40)
    off = mem.offset("bàn làm việc", 21)
    assert off == pytest.approx(40 * 20 / 25, abs=0.1)
    assert off > 30


def test_do_lech_bi_chan_boi_tran_quyen_luc():
    """Phần học không được lệch quá xa luật nền, kể cả khi dữ liệu bảo thế.

    Đây là giới hạn bán kính thiệt hại: nếu mô hình học sai, nó chỉ sai được
    trong một khoảng có kiểm soát.
    """
    mem = ZoneMemory()
    for _ in range(200):
        mem.observe("giường", 22, +250)
    assert mem.offset("giường", 22) == MODEL_MAX_DEVIATION


def test_vung_chua_co_du_lieu_thi_khong_lech():
    assert ZoneMemory().offset("lối đi", 10) == 0.0


def test_hai_vung_hoc_doc_lap_nhau():
    mem = ZoneMemory()
    for _ in range(10):
        mem.observe("bàn làm việc", 21, +50)
    assert mem.offset("bàn làm việc", 21) > 20
    assert mem.offset("ghế đọc sách", 21) == 0.0


def test_do_sang_luon_nam_trong_dai_hop_le():
    mem = ZoneMemory()
    for _ in range(50):
        mem.observe("bàn làm việc", 12, +250)
    for hour in range(24):
        d = decide(hour, True, 0.0, "bàn làm việc", mem)
        assert 0 <= d.brightness <= 255
