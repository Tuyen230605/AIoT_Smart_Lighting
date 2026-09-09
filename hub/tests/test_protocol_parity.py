"""Chống lệch hợp đồng giữa firmware (C) và hub (Python).

Đây là loại lỗi tốn thời gian gỡ nhất trong hệ phân tán: đổi tên một khoá JSON
ở một bên, quên bên kia, rồi mất nửa ngày đi tìm vì sao node không nhận lệnh.
Kiểm thử này đọc thẳng oi_protocol.h, bóc các #define ra và đối chiếu với
protocol.py — lệch là báo lỗi ngay khi chạy CI.
"""

from __future__ import annotations

import re
from pathlib import Path

import pytest
from oi_common import protocol as py

HEADER = (
    Path(__file__).resolve().parents[2]
    / "firmware"
    / "common"
    / "include"
    / "oi_protocol.h"
)

_DEFINE_STR = re.compile(r'#define\s+(OI_\w+)\s+"([^"]*)"')
_DEFINE_NUM = re.compile(r"#define\s+(OI_\w+)\s+([0-9]+\.?[0-9]*)f?\s*(?://.*)?$")


@pytest.fixture(scope="module")
def c_defines() -> dict[str, str]:
    assert HEADER.exists(), f"Không tìm thấy header: {HEADER}"
    text = HEADER.read_text(encoding="utf-8")
    out: dict[str, str] = {}
    for line in text.splitlines():
        if (m := _DEFINE_STR.search(line)) or (m := _DEFINE_NUM.search(line)):
            out[m.group(1)] = m.group(2)
    return out


# ── Chủ đề MQTT ────────────────────────────────────────────────
# Bên C dùng %s cho printf, bên Python dùng {node} cho str.format.
TOPIC_PAIRS = [
    ("OI_TOPIC_CMD_FMT", py.TOPIC_CMD_FMT),
    ("OI_TOPIC_STATE_FMT", py.TOPIC_STATE_FMT),
    ("OI_TOPIC_TELE_FMT", py.TOPIC_TELE_FMT),
    ("OI_TOPIC_WAKE_FMT", py.TOPIC_WAKE_FMT),
    ("OI_TOPIC_LWT_FMT", py.TOPIC_LWT_FMT),
]


@pytest.mark.parametrize("c_name,py_value", TOPIC_PAIRS)
def test_topic_formats_match(c_defines, c_name, py_value):
    assert c_name in c_defines, f"{c_name} biến mất khỏi oi_protocol.h"
    assert c_defines[c_name].replace("%s", "{node}") == py_value


STATIC_TOPIC_PAIRS = [
    ("OI_TOPIC_TRANSCRIPT", py.TOPIC_TRANSCRIPT),
    ("OI_TOPIC_CTX_VISION", py.TOPIC_CTX_VISION),
    ("OI_TOPIC_CTX_FUSED", py.TOPIC_CTX_FUSED),
    ("OI_TOPIC_AGENT_LOG", py.TOPIC_AGENT_LOG),
    ("OI_TOPIC_HEARTBEAT", py.TOPIC_HEARTBEAT),
]


@pytest.mark.parametrize("c_name,py_value", STATIC_TOPIC_PAIRS)
def test_static_topics_match(c_defines, c_name, py_value):
    assert c_defines.get(c_name) == py_value


# ── Khoá JSON ──────────────────────────────────────────────────
KEY_PAIRS = [
    ("OI_K_TS", py.K_TS),
    ("OI_K_SEQ", py.K_SEQ),
    ("OI_K_NODE", py.K_NODE),
    ("OI_K_PROTO", py.K_PROTO),
    ("OI_K_MODE", py.K_MODE),
    ("OI_K_ZONE", py.K_ZONE),
    ("OI_K_BRIGHTNESS", py.K_BRIGHTNESS),
    ("OI_K_CCT", py.K_CCT),
    ("OI_K_COLOR", py.K_COLOR),
    ("OI_K_FADE_MS", py.K_FADE_MS),
    ("OI_K_SOURCE", py.K_SOURCE),
    ("OI_K_REASON", py.K_REASON),
    ("OI_K_SCENE", py.K_SCENE),
    ("OI_K_LUX", py.K_LUX),
    ("OI_K_RSSI", py.K_RSSI),
    ("OI_K_HEAP", py.K_HEAP),
    ("OI_K_UPTIME", py.K_UPTIME),
    ("OI_K_HEAP_MIN", py.K_HEAP_MIN),
    ("OI_K_STACK", py.K_STACK),
    ("OI_K_RECONNECTS", py.K_RECONNECTS),
    ("OI_K_BOOTS", py.K_BOOTS),
    ("OI_K_PRESENCE", py.K_PRESENCE),
    ("OI_K_DIST_MOVING", py.K_DIST_MOVING),
    ("OI_K_DIST_STATIC", py.K_DIST_STATIC),
    ("OI_K_KEYWORD", py.K_KEYWORD),
    ("OI_K_CONF", py.K_CONF),
    ("OI_K_ESCALATED", py.K_ESCALATED),
    ("OI_K_PAN", py.K_PAN),
    ("OI_K_TILT", py.K_TILT),
    ("OI_K_TRACK_MODE", py.K_TRACK_MODE),
]


@pytest.mark.parametrize("c_name,py_value", KEY_PAIRS)
def test_json_keys_match(c_defines, c_name, py_value):
    assert c_defines.get(c_name) == py_value, f"{c_name} lệch giữa C và Python"


def test_no_orphan_json_keys_in_header(c_defines):
    """Thêm khoá bên C mà quên bên Python → báo lỗi."""
    c_keys = {n for n in c_defines if n.startswith("OI_K_")}
    covered = {n for n, _ in KEY_PAIRS}
    assert not (c_keys - covered), (
        f"Khoá mới trong oi_protocol.h chưa được nhân bản sang protocol.py: "
        f"{sorted(c_keys - covered)}"
    )


# ── Ngưỡng số ──────────────────────────────────────────────────
THRESHOLD_PAIRS = [
    ("OI_CONF_WAKE_MIN", py.CONF_WAKE_MIN),
    ("OI_CONF_CMD_MIN", py.CONF_CMD_MIN),
    ("OI_ESCALATE_BELOW", py.ESCALATE_BELOW),
    ("OI_UTTERANCE_LONG_MS", py.UTTERANCE_LONG_MS),
    ("OI_PREROLL_MS", py.PREROLL_MS),
    ("OI_HUB_TIMEOUT_MS", py.HUB_TIMEOUT_MS),
    ("OI_LISTEN_WINDOW_MS", py.LISTEN_WINDOW_MS),
    ("OI_HEARTBEAT_PERIOD_MS", py.HEARTBEAT_PERIOD_MS),
    ("OI_HEARTBEAT_MISS_LIMIT", py.HEARTBEAT_MISS_LIMIT),
]


@pytest.mark.parametrize("c_name,py_value", THRESHOLD_PAIRS)
def test_thresholds_match(c_defines, c_name, py_value):
    assert c_name in c_defines, f"{c_name} biến mất khỏi oi_protocol.h"
    assert float(c_defines[c_name]) == pytest.approx(float(py_value))


SCENE_PAIRS = [
    ("OI_SCENE_NONE", py.Scene.NONE),
    ("OI_SCENE_READ", py.Scene.READ),
    ("OI_SCENE_MOVIE", py.Scene.MOVIE),
    ("OI_SCENE_SLEEP", py.Scene.SLEEP),
    ("OI_SCENE_MUSIC_LOFI", py.Scene.MUSIC_LOFI),
    ("OI_SCENE_MUSIC_ROCK", py.Scene.MUSIC_ROCK),
    ("OI_SCENE_MUSIC_EDM", py.Scene.MUSIC_EDM),
]


@pytest.mark.parametrize("c_name,py_value", SCENE_PAIRS)
def test_scene_values_match(c_defines, c_name, py_value):
    assert c_name in c_defines, f"{c_name} biến mất khỏi oi_protocol.h"
    assert int(c_defines[c_name]) == int(py_value)


def test_no_orphan_scenes_in_header(c_defines):
    c_scenes = {n for n in c_defines if n.startswith("OI_SCENE_")}
    covered = {n for n, _ in SCENE_PAIRS}
    assert not (c_scenes - covered), (
        f"Cảnh mới trong oi_protocol.h chưa nhân bản sang protocol.py: "
        f"{sorted(c_scenes - covered)}"
    )


def test_protocol_version_matches(c_defines):
    assert int(c_defines["OI_PROTO_MAJOR"]) == py.PROTO_MAJOR
    assert int(c_defines["OI_PROTO_MINOR"]) == py.PROTO_MINOR
