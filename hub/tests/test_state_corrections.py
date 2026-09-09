"""Kiểm thử phần phát hiện "người dùng sửa lại quyết định của hệ thống".

VÌ SAO PHẦN NÀY ĐÁNG KIỂM THỬ KỸ
Nó sinh ra dữ liệu huấn luyện cho AI-6 và dữ liệu thô cho chỉ số S4 — bằng
chứng của đóng góp C4. Đếm sai thì mô hình học sai và biểu đồ trong báo cáo sai,
mà **dữ liệu bỏ lỡ thì không lấy lại được**: sai ở đây phát hiện càng muộn càng
mất nhiều tuần vận hành.
"""

from __future__ import annotations

import time

import pytest
from oi_common import protocol as proto
from oi_state.db import StateDB


@pytest.fixture
def db(tmp_path):
    d = StateDB(tmp_path / "test.sqlite")
    yield d
    d.close()


def _state(src: proto.CmdSource, bri: int, seq: int = 1) -> dict:
    return {
        proto.K_NODE: "node1",
        proto.K_MODE: int(proto.LightMode.SOLID),
        proto.K_BRIGHTNESS: bri,
        proto.K_SOURCE: int(src),
        proto.K_SEQ: seq,
    }


def _age_event(db: StateDB, event_id: int, seconds: int) -> None:
    """Lùi dấu thời gian của một sự kiện về quá khứ."""
    db.con.execute(
        "UPDATE events SET ts = ts - ? WHERE id = ?", (seconds, event_id)
    )
    db.con.commit()


def test_nguoi_dung_sua_ngay_sau_lenh_tu_dong_duoc_ghi_nhan(db):
    auto = db.record_event("node1", "state", _state(proto.CmdSource.AGENT, 200))
    _age_event(db, auto, 10)

    manual = db.record_event("node1", "state", _state(proto.CmdSource.BUTTON, 120, seq=2))
    corr = db.detect_correction("node1", manual)

    assert corr is not None
    assert corr["auto_bri"] == 200
    assert corr["manual_bri"] == 120
    assert corr["seconds"] == pytest.approx(10, abs=1)


def test_sua_qua_muon_thi_khong_tinh(db):
    auto = db.record_event("node1", "state", _state(proto.CmdSource.AGENT, 200))
    _age_event(db, auto, 300)   # 5 phút sau — ngoài cửa sổ 60 s

    manual = db.record_event("node1", "state", _state(proto.CmdSource.BUTTON, 120, seq=2))
    assert db.detect_correction("node1", manual) is None


def test_nguoi_dung_tu_bat_den_khong_phai_la_sua_sai(db):
    """Không có hành động tự động nào trước đó thì đây chỉ là dùng bình thường."""
    manual = db.record_event("node1", "state", _state(proto.CmdSource.VOICE_LOCAL, 150))
    assert db.detect_correction("node1", manual) is None


def test_he_thong_tu_doi_y_khong_phai_la_bi_sua(db):
    """AGENT theo sau AGENT là hệ thống tự điều chỉnh, không phải người dùng."""
    auto1 = db.record_event("node1", "state", _state(proto.CmdSource.AGENT, 200))
    _age_event(db, auto1, 10)

    auto2 = db.record_event("node1", "state", _state(proto.CmdSource.AGENT, 180, seq=2))
    assert db.detect_correction("node1", auto2) is None


def test_bam_nhieu_nut_lien_tiep_chi_tinh_mot_lan(db):
    """Một hành động tự động chỉ được tính là bị sửa MỘT lần.

    Người dùng thường bấm tăng sáng ba bốn nhát liên tiếp. Đếm mỗi nhát thành
    một lần sửa sẽ thổi phồng chỉ số S4 lên nhiều lần và làm sai cả biểu đồ
    trong báo cáo lẫn trọng số huấn luyện.
    """
    auto = db.record_event("node1", "state", _state(proto.CmdSource.AGENT, 200))
    _age_event(db, auto, 5)

    first = db.record_event("node1", "state", _state(proto.CmdSource.BUTTON, 185, seq=2))
    second = db.record_event("node1", "state", _state(proto.CmdSource.BUTTON, 170, seq=3))
    third = db.record_event("node1", "state", _state(proto.CmdSource.BUTTON, 155, seq=4))

    assert db.detect_correction("node1", first) is not None
    assert db.detect_correction("node1", second) is None
    assert db.detect_correction("node1", third) is None

    n = db.con.execute("SELECT COUNT(*) AS n FROM corrections").fetchone()["n"]
    assert n == 1


def test_sua_o_node_khac_khong_bi_gan_nham(db):
    auto = db.record_event("node1", "state", _state(proto.CmdSource.AGENT, 200))
    _age_event(db, auto, 5)

    manual_node2 = db.record_event("node2", "state", _state(proto.CmdSource.BUTTON, 120, seq=2))
    assert db.detect_correction("node2", manual_node2) is None


def test_ti_le_chinh_tay_tinh_dung(db):
    for i in range(4):
        auto = db.record_event("node1", "state", _state(proto.CmdSource.AGENT, 200, seq=i))
        _age_event(db, auto, 30)
        if i < 3:
            manual = db.record_event(
                "node1", "state", _state(proto.CmdSource.BUTTON, 120, seq=100 + i)
            )
            db.detect_correction("node1", manual)
            _age_event(db, manual, 29)

    stats = db.correction_rate(days=7)
    assert stats["auto_actions"] == 4
    assert stats["corrections"] == 3
    assert stats["rate"] == pytest.approx(0.75)


def test_khong_co_hanh_dong_tu_dong_thi_ti_le_la_none(db):
    """Chia cho 0 phải trả về None, không được trả 0 — hai thứ khác nghĩa hẳn.

    Tỉ lệ 0 nghĩa là "hệ thống tự quyết nhiều lần và chưa bị sửa lần nào";
    None nghĩa là "chưa có gì để đánh giá". Nhầm hai cái này trong báo cáo là
    tự khoe một kết quả không tồn tại.
    """
    db.record_event("node1", "state", _state(proto.CmdSource.BUTTON, 150))
    stats = db.correction_rate(days=7)
    assert stats["auto_actions"] == 0
    assert stats["rate"] is None


def test_anh_chup_ngu_canh_duoc_luu_kem(db):
    ctx = {"lux": 84.2, "activity": {"label": "doc_sach", "conf": 0.81}}
    eid = db.record_event("node1", "state", _state(proto.CmdSource.AGENT, 200), ctx)

    row = db.con.execute("SELECT ctx FROM events WHERE id = ?", (eid,)).fetchone()
    assert "doc_sach" in row["ctx"]


def test_trang_thai_moi_nhat_doc_duoc(db):
    db.record_event("node1", "state", _state(proto.CmdSource.AGENT, 100, seq=1))
    db.record_event("node1", "state", _state(proto.CmdSource.BUTTON, 220, seq=2))

    latest = db.latest_state("node1")
    assert latest[proto.K_BRIGHTNESS] == 220
    assert db.latest_state("node3") is None


def test_dau_thoi_gian_dung_gio_hub_khong_dung_gio_node(db):
    """ESP32 không có đồng hồ thực; trường ts của nó chỉ là uptime.

    Nếu tin vào ts của node, mọi sự kiện sẽ mang dấu thời gian năm 1970 và
    toàn bộ phân tích theo thời gian trở nên vô nghĩa.
    """
    payload = _state(proto.CmdSource.AGENT, 200)
    payload[proto.K_TS] = 42          # uptime 42 giây kể từ lúc bật
    eid = db.record_event("node1", "state", payload)

    row = db.con.execute("SELECT ts, raw FROM events WHERE id = ?", (eid,)).fetchone()
    assert row["ts"] > time.time() - 60      # giờ thật của hub
    assert '"ts":42' in row["raw"]            # giá trị gốc vẫn giữ để đối chiếu
