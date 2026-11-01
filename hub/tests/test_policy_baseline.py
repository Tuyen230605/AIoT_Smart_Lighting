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
    PRESENCE_STALE_S,
    VACANCY_HOLD_S,
    OccupancyHold,
    ZoneMemory,
    decide,
    lux_compensation,
    presence_reading,
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


# ── Bộ nhớ học sống qua khởi động lại ─────────────────────────

def test_bo_nho_hoc_song_qua_khoi_dong_lai(tmp_path):
    """Container khởi động lại không được làm hệ thống quên những gì đã học.

    Nếu quên, đường S4 theo tuần đo lại từ đầu sau mỗi lần cập nhật hub, và
    không còn chứng minh được việc học tích luỹ của đóng góp C4.
    """
    path = tmp_path / "mem.json"
    mem = ZoneMemory()
    for _ in range(10):
        mem.observe("bàn làm việc", 21, +40)
    mem.save(path)

    nap_lai = ZoneMemory.load(path)
    assert nap_lai.offset("bàn làm việc", 21) == pytest.approx(mem.offset("bàn làm việc", 21))
    assert nap_lai.sample_count() == 10


def test_chua_co_file_thi_bat_dau_tu_rong(tmp_path):
    assert ZoneMemory.load(tmp_path / "chua-co.json").sample_count() == 0


def test_file_hong_khong_lam_sap_va_duoc_giu_lai_de_xem(tmp_path):
    path = tmp_path / "mem.json"
    path.write_text("{khong phai json", encoding="utf-8")

    assert ZoneMemory.load(path).sample_count() == 0
    assert (tmp_path / "mem.json.bad").exists()


# ── Không tắt đèn vì một nhịp hụt cảm biến ────────────────────

def _ctx(now: float, radar=False, vision=False, age=1.0, lag=0.0) -> dict:
    """Bản tin oi/ctx/fused tối thiểu, đúng các khoá oi-context phát ra."""
    return {
        "ts": now - lag,
        "pres": {"radar": radar, "vision": vision},
        "sources": {"radar": age, "vision": age},
    }


def test_mot_nhip_khong_thay_ai_chua_duoc_tat_den():
    """Camera mất người ngồi yên đọc sách là chuyện thường, không phải phòng trống."""
    hold = OccupancyHold()
    assert hold.update(True, now=0)
    assert hold.update(False, now=30)
    assert hold.update(False, now=VACANCY_HOLD_S - 1)


def test_vang_lien_tuc_du_lau_moi_tat_den():
    hold = OccupancyHold()
    hold.update(True, now=0)
    assert not hold.update(False, now=VACANCY_HOLD_S + 1)


def test_thay_lai_nguoi_thi_dem_lai_tu_dau():
    hold = OccupancyHold()
    hold.update(True, now=0)
    hold.update(True, now=VACANCY_HOLD_S - 10)
    assert hold.update(False, now=VACANCY_HOLD_S + 60)


def test_ngu_canh_cu_la_khong_biet_chu_khong_phai_phong_trong():
    """oi-context chết thì policy phải đứng yên, không được suy ra phòng trống."""
    assert presence_reading({}, now=100) is None
    assert presence_reading(_ctx(100, lag=PRESENCE_STALE_S + 5), now=100) is None


def test_mat_ca_radar_lan_camera_la_khong_biet():
    assert presence_reading(_ctx(100, age=PRESENCE_STALE_S + 1), now=100) is None

    ctx = _ctx(100)
    ctx["sources"] = {"radar": None, "vision": None}
    assert presence_reading(ctx, now=100) is None


def test_radar_hoac_camera_thay_nguoi_la_co_nguoi():
    assert presence_reading(_ctx(100, radar=True), now=100) is True
    assert presence_reading(_ctx(100, vision=True), now=100) is True
    assert presence_reading(_ctx(100), now=100) is False
