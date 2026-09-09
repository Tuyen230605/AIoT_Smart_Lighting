"""Lưu trữ của oi-state — nguồn sự thật duy nhất về trạng thái hệ thống.

VÌ SAO SQLITE
Dữ liệu là chuỗi sự kiện có thứ tự thời gian, ghi vài chục dòng mỗi ngày, đọc
lại theo khoảng thời gian. Một file SQLite làm việc đó tốt hơn bất kỳ thứ gì
nặng hơn, không cần tiến trình riêng, và sao lưu bằng cách chép một file.

BẢNG `corrections` LÀ LÝ DO CẢ DỊCH VỤ NÀY TỒN TẠI
Mỗi dòng trong đó là một lần hệ thống tự quyết rồi bị người dùng sửa lại — mẫu
huấn luyện có trọng số cao nhất của mô hình học sở thích (AI-6), và là dữ liệu
thô của chỉ số S4, thứ chứng minh đóng góp C4. Không dịch vụ nào khác sinh ra
được dữ liệu này, và nó **không thể lấy lại được nếu bỏ lỡ** — đó là lý do mốc
G2.10 ("dọn vào ở") được đặt sớm trong lộ trình.
"""

from __future__ import annotations

import json
import sqlite3
import time
from pathlib import Path
from typing import Any

from oi_common import protocol as proto

SCHEMA = """
CREATE TABLE IF NOT EXISTS events (
    id        INTEGER PRIMARY KEY AUTOINCREMENT,
    ts        INTEGER NOT NULL,           -- epoch giây, giờ của hub
    node      TEXT    NOT NULL,
    kind      TEXT    NOT NULL,           -- 'state' | 'cmd'
    src       INTEGER,                    -- OiCmdSource
    seq       INTEGER,
    mode      INTEGER,
    zone      INTEGER,
    bri       INTEGER,
    cct       INTEGER,
    rgb       INTEGER,
    scene     INTEGER,
    ctx       TEXT,                       -- ảnh chụp vector ngữ cảnh lúc đó
    raw       TEXT    NOT NULL
);
CREATE INDEX IF NOT EXISTS idx_events_ts   ON events(ts);
CREATE INDEX IF NOT EXISTS idx_events_node ON events(node, ts);

-- Mỗi dòng: hệ thống tự quyết, rồi người dùng sửa lại trong CORRECTION_WINDOW_S.
CREATE TABLE IF NOT EXISTS corrections (
    id           INTEGER PRIMARY KEY AUTOINCREMENT,
    ts           INTEGER NOT NULL,
    node         TEXT    NOT NULL,
    auto_id      INTEGER NOT NULL REFERENCES events(id),
    manual_id    INTEGER NOT NULL REFERENCES events(id),
    seconds      REAL    NOT NULL,        -- người dùng chịu được bao lâu mới sửa
    auto_bri     INTEGER,
    manual_bri   INTEGER,
    auto_src     INTEGER,
    manual_src   INTEGER
);
CREATE INDEX IF NOT EXISTS idx_corr_ts ON corrections(ts);

-- Nhật ký giải thích quyết định (FR-E3), để dashboard hiển thị lại lịch sử.
CREATE TABLE IF NOT EXISTS agent_log (
    id     INTEGER PRIMARY KEY AUTOINCREMENT,
    ts     INTEGER NOT NULL,
    reason TEXT    NOT NULL,
    raw    TEXT    NOT NULL
);
CREATE INDEX IF NOT EXISTS idx_agentlog_ts ON agent_log(ts);
"""

# Người dùng sửa lại trong bao lâu thì tính là "sửa hành động tự động vừa rồi".
# 60 s theo đặc tả mục M7. Dài hơn thì bắt nhầm những lần chỉnh không liên quan;
# ngắn hơn thì bỏ sót người phản ứng chậm.
CORRECTION_WINDOW_S = 60.0

# Nguồn lệnh phản ánh ý muốn trực tiếp của người dùng.
USER_SOURCES = {
    int(proto.CmdSource.VOICE_LOCAL),
    int(proto.CmdSource.VOICE_HUB),
    int(proto.CmdSource.BUTTON),
    int(proto.CmdSource.WEB),
}
# Nguồn lệnh là hệ thống tự quyết — ứng viên để bị sửa.
AUTO_SOURCES = {
    int(proto.CmdSource.AGENT),
    int(proto.CmdSource.SCHEDULE),
}


class StateDB:
    def __init__(self, path: str | Path) -> None:
        self.path = Path(path)
        self.path.parent.mkdir(parents=True, exist_ok=True)
        self.con = sqlite3.connect(self.path, check_same_thread=False)
        self.con.row_factory = sqlite3.Row
        # WAL: đọc (dashboard, huấn luyện ban đêm) không chặn ghi.
        self.con.execute("PRAGMA journal_mode=WAL")
        self.con.executescript(SCHEMA)
        self.con.commit()

    # ── ghi ───────────────────────────────────────────────────
    def record_event(
        self, node: str, kind: str, payload: dict[str, Any], ctx: dict[str, Any] | None = None
    ) -> int:
        """Ghi một sự kiện, trả về id của nó.

        Dấu thời gian dùng giờ của hub, không dùng trường `ts` trong bản tin:
        ESP32 không có đồng hồ thực và `ts` của nó chỉ là số giây từ lúc bật.
        Giữ nguyên giá trị gốc trong cột `raw` để còn đối chiếu khi cần.
        """
        cur = self.con.execute(
            """INSERT INTO events
               (ts, node, kind, src, seq, mode, zone, bri, cct, rgb, scene, ctx, raw)
               VALUES (?,?,?,?,?,?,?,?,?,?,?,?,?)""",
            (
                int(time.time()),
                node,
                kind,
                payload.get(proto.K_SOURCE),
                payload.get(proto.K_SEQ),
                payload.get(proto.K_MODE),
                payload.get(proto.K_ZONE),
                payload.get(proto.K_BRIGHTNESS),
                payload.get(proto.K_CCT),
                payload.get(proto.K_COLOR),
                payload.get(proto.K_SCENE),
                json.dumps(ctx, separators=(",", ":")) if ctx else None,
                json.dumps(payload, separators=(",", ":")),
            ),
        )
        self.con.commit()
        return int(cur.lastrowid)

    def record_agent_log(self, payload: dict[str, Any]) -> None:
        self.con.execute(
            "INSERT INTO agent_log (ts, reason, raw) VALUES (?,?,?)",
            (
                int(time.time()),
                str(payload.get(proto.K_REASON, "")),
                json.dumps(payload, separators=(",", ":")),
            ),
        )
        self.con.commit()

    # ── phát hiện người dùng sửa sai ──────────────────────────
    def detect_correction(self, node: str, manual_id: int) -> dict[str, Any] | None:
        """Sự kiện `manual_id` có phải là một lần sửa hành động tự động không?

        Điều kiện: sự kiện này do người dùng gây ra, và ngay trước nó (trong cửa
        sổ 60 s) có một sự kiện do hệ thống tự quyết trên cùng node.
        """
        manual = self.con.execute("SELECT * FROM events WHERE id = ?", (manual_id,)).fetchone()
        if manual is None or manual["src"] not in USER_SOURCES:
            return None

        auto = self.con.execute(
            """SELECT * FROM events
               WHERE node = ? AND id < ? AND kind = 'state'
                 AND src IN (?, ?)
                 AND ts >= ?
               ORDER BY id DESC LIMIT 1""",
            (
                node,
                manual_id,
                int(proto.CmdSource.AGENT),
                int(proto.CmdSource.SCHEDULE),
                manual["ts"] - int(CORRECTION_WINDOW_S),
            ),
        ).fetchone()
        if auto is None:
            return None

        # Đã ghi rồi thì thôi: một hành động tự động chỉ tính bị sửa MỘT lần,
        # dù người dùng có bấm thêm mấy nút nữa trong cùng cửa sổ. Không có
        # chốt này thì một lần chỉnh tay bị đếm thành năm, và chỉ số S4 sai.
        dup = self.con.execute(
            "SELECT 1 FROM corrections WHERE auto_id = ?", (auto["id"],)
        ).fetchone()
        if dup is not None:
            return None

        seconds = float(manual["ts"] - auto["ts"])
        self.con.execute(
            """INSERT INTO corrections
               (ts, node, auto_id, manual_id, seconds, auto_bri, manual_bri, auto_src, manual_src)
               VALUES (?,?,?,?,?,?,?,?,?)""",
            (
                manual["ts"],
                node,
                auto["id"],
                manual_id,
                seconds,
                auto["bri"],
                manual["bri"],
                auto["src"],
                manual["src"],
            ),
        )
        self.con.commit()
        return {
            "node": node,
            "seconds": seconds,
            "auto_bri": auto["bri"],
            "manual_bri": manual["bri"],
        }

    # ── đọc ───────────────────────────────────────────────────
    def latest_state(self, node: str) -> dict[str, Any] | None:
        row = self.con.execute(
            "SELECT raw FROM events WHERE node = ? AND kind = 'state' ORDER BY id DESC LIMIT 1",
            (node,),
        ).fetchone()
        return json.loads(row["raw"]) if row else None

    def correction_rate(self, days: int = 7) -> dict[str, Any]:
        """Chỉ số S4 thô: bao nhiêu hành động tự động, bao nhiêu lần bị sửa.

        Đây là con số đo *giá trị* của AI chứ không đo *độ chính xác* của nó —
        một mô hình có MAE đẹp mà người dùng vẫn phải chỉnh tay mỗi lần là một
        mô hình vô dụng, và chỉ số này phát hiện ra điều đó.
        """
        since = int(time.time()) - days * 86400
        auto = self.con.execute(
            """SELECT COUNT(*) AS n FROM events
               WHERE kind = 'state' AND ts >= ? AND src IN (?, ?)""",
            (since, int(proto.CmdSource.AGENT), int(proto.CmdSource.SCHEDULE)),
        ).fetchone()["n"]
        corr = self.con.execute(
            "SELECT COUNT(*) AS n FROM corrections WHERE ts >= ?", (since,)
        ).fetchone()["n"]
        return {
            "days": days,
            "auto_actions": auto,
            "corrections": corr,
            "rate": (corr / auto) if auto else None,
        }

    def recent_agent_log(self, limit: int = 20) -> list[dict[str, Any]]:
        rows = self.con.execute(
            "SELECT ts, reason FROM agent_log ORDER BY id DESC LIMIT ?", (limit,)
        ).fetchall()
        return [{"ts": r["ts"], "reason": r["reason"]} for r in rows]

    def close(self) -> None:
        self.con.close()
