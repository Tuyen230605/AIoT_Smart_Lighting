"""Hợp đồng giao tiếp — bản song sinh Python của firmware/common/include/oi_protocol.h

QUY TẮC: mọi thay đổi ở đây PHẢI được nhân bản sang oi_protocol.h trong cùng
một commit. hub/tests/test_protocol_parity.py đọc cả hai file và báo lỗi nếu lệch.
"""

from __future__ import annotations

from enum import IntEnum, IntFlag

# ══════════════════════════════════════════════════════════════
#  Phiên bản giao thức
# ══════════════════════════════════════════════════════════════
PROTO_MAJOR = 1
PROTO_MINOR = 0
PROTO_STR = f"{PROTO_MAJOR}.{PROTO_MINOR}"

# ══════════════════════════════════════════════════════════════
#  Định danh node
# ══════════════════════════════════════════════════════════════
NODE_CONTROLLER = "node1"
NODE_GATE = "node2"
NODE_SPOTLIGHT = "node3"
ALL_NODES = (NODE_CONTROLLER, NODE_GATE, NODE_SPOTLIGHT)

# ══════════════════════════════════════════════════════════════
#  Cây chủ đề MQTT
# ══════════════════════════════════════════════════════════════
TOPIC_CMD_FMT = "oi/cmd/{node}"
TOPIC_STATE_FMT = "oi/state/{node}"
TOPIC_TELE_FMT = "oi/tele/{node}"
TOPIC_WAKE_FMT = "oi/wake/{node}"
TOPIC_LWT_FMT = "oi/sys/offline/{node}"

TOPIC_TRANSCRIPT = "oi/nlu/transcript"
TOPIC_CTX_VISION = "oi/ctx/vision"
TOPIC_CTX_FUSED = "oi/ctx/fused"
TOPIC_AGENT_LOG = "oi/agent/log"
TOPIC_HEARTBEAT = "oi/sys/heartbeat"

HEARTBEAT_PERIOD_MS = 1000
HEARTBEAT_MISS_LIMIT = 8


def topic_cmd(node: str) -> str:
    return TOPIC_CMD_FMT.format(node=node)


def topic_state(node: str) -> str:
    return TOPIC_STATE_FMT.format(node=node)


def topic_tele(node: str) -> str:
    return TOPIC_TELE_FMT.format(node=node)


def topic_wake(node: str) -> str:
    return TOPIC_WAKE_FMT.format(node=node)


# ══════════════════════════════════════════════════════════════
#  Khoá JSON
# ══════════════════════════════════════════════════════════════
K_TS = "ts"
K_SEQ = "seq"
K_NODE = "node"
K_PROTO = "proto"

K_MODE = "mode"
K_ZONE = "zone"
K_BRIGHTNESS = "bri"
K_CCT = "cct"
K_COLOR = "rgb"
K_FADE_MS = "fade"
K_SOURCE = "src"
K_REASON = "why"

K_LUX = "lux"
K_RSSI = "rssi"
K_HEAP = "heap"
K_UPTIME = "up"

K_PRESENCE = "pres"
K_DIST_MOVING = "d_mov"
K_DIST_STATIC = "d_sta"

K_KEYWORD = "kw"
K_CONF = "conf"
K_ESCALATED = "esc"

K_PAN = "pan"
K_TILT = "tilt"
K_TRACK_MODE = "trk"


# ══════════════════════════════════════════════════════════════
#  Kiểu liệt kê — phải khớp giá trị số với oi_state.h
# ══════════════════════════════════════════════════════════════
class LightMode(IntEnum):
    OFF = 0
    SOLID = 1
    MUSIC = 2
    SCENE = 3
    FOLLOW = 4


class CmdSource(IntEnum):
    """Nguồn phát sinh lệnh.

    Không chỉ để ghi log: oi_policy dùng trường này để gán nhãn huấn luyện.
    Một lệnh BUTTON/WEB đến ngay sau một lệnh AGENT chính là một lần
    "người dùng sửa sai" — mẫu huấn luyện trọng số 1.0.
    """

    BOOT = 0
    VOICE_LOCAL = 1
    VOICE_HUB = 2
    BUTTON = 3
    WEB = 4
    AGENT = 5
    SCHEDULE = 6

    @property
    def is_user_intent(self) -> bool:
        """Lệnh này có phản ánh ý muốn trực tiếp của người dùng không."""
        return self in (
            CmdSource.VOICE_LOCAL,
            CmdSource.VOICE_HUB,
            CmdSource.BUTTON,
            CmdSource.WEB,
        )

    @property
    def is_autonomous(self) -> bool:
        """Hệ thống tự quyết — ứng viên để bị người dùng sửa lại."""
        return self in (CmdSource.AGENT, CmdSource.SCHEDULE)


class Zone(IntFlag):
    NONE = 0x00
    DESK = 0x01
    SEAT = 0x02
    WALK = 0x04
    BED = 0x08
    GATE = 0x10
    ALL = 0x1F


# ══════════════════════════════════════════════════════════════
#  Ngưỡng của giao thức leo thang
# ══════════════════════════════════════════════════════════════
CONF_WAKE_MIN = 0.90
CONF_CMD_MIN = 0.75
ESCALATE_BELOW = 0.75
UTTERANCE_LONG_MS = 1500
PREROLL_MS = 500
HUB_TIMEOUT_MS = 8000
LISTEN_WINDOW_MS = 10000
