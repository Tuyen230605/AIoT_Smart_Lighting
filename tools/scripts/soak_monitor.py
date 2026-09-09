#!/usr/bin/env python3
"""Ghi và chấm điểm bài chạy liên tục 72 giờ (G1.8).

VÌ SAO CẦN
Rò rỉ bộ nhớ và tương tranh không lộ ra trong mười phút thử nghiệm; chúng lộ ra
sau nhiều giờ. Nếu không chốt ở cuối G1, chúng sẽ lộ ra đúng tuần 17. Script này
nghe số liệu sức khoẻ do firmware tự phát mỗi phút, ghi ra CSV, và chấm ba tiêu
chí của cổng chất lượng:

    heap_min không trôi xuống      → không rò rỉ bộ nhớ
    mọi task còn dư ≥ 25 % stack   → NFR-05
    boots không tăng               → 0 lần reset ngoài ý muốn

Đồ thị và bảng kết quả đi thẳng vào chương 4 của báo cáo — chúng thay bảng
"giả lập" của bản DACN bằng số đo thật.

CÁCH DÙNG
    # ghi log, để chạy suốt 72 giờ
    python tools/scripts/soak_monitor.py --host 192.168.1.10 --out soak.csv

    # chấm điểm file đã ghi
    python tools/scripts/soak_monitor.py --report soak.csv

    # kèm đồ thị (cần matplotlib)
    python tools/scripts/soak_monitor.py --report soak.csv --plot soak.png
"""

from __future__ import annotations

import argparse
import csv
import json
import sys
from datetime import datetime, timezone
from pathlib import Path

FIELDS = [
    "iso_time",
    "node",
    "uptime_s",
    "heap",
    "heap_min",
    "rssi",
    "boots",
    "reconnects",
    "stack_json",
]

# Kích thước ngăn xếp khai báo lúc tạo task, tính bằng BYTE (xem Config.h của
# từng node). Cần để đổi "số word còn dư" thành "phần trăm còn dư".
STACK_BYTES = {
    "net": 10240,
    "sens": 4096,
    "led": 8192,
    "btn": 3072,
    "mic": 32768,
    "radar": 4096,
    "logic": 4096,
}

MIN_STACK_FREE_PCT = 25.0   # NFR-05


def record(host: str, port: int, out: Path, username: str | None, password: str | None) -> None:
    try:
        import paho.mqtt.client as mqtt
    except ImportError:
        sys.exit("Thiếu paho-mqtt. Cài bằng:  pip install paho-mqtt")

    new_file = not out.exists()
    fh = out.open("a", newline="", encoding="utf-8")
    writer = csv.DictWriter(fh, fieldnames=FIELDS)
    if new_file:
        writer.writeheader()

    def on_connect(client, userdata, flags, rc, properties=None):
        print(f"Đã kết nối broker (rc={rc}). Đang nghe oi/tele/#")
        client.subscribe("oi/tele/#", qos=0)

    def on_message(client, userdata, msg):
        try:
            d = json.loads(msg.payload)
        except json.JSONDecodeError:
            return
        # Bản tin lux 2 giây một lần không có heap — chỉ ghi bản tin sức khoẻ.
        if "heap_min" not in d:
            return

        writer.writerow(
            {
                "iso_time": datetime.now(timezone.utc).isoformat(timespec="seconds"),
                "node": d.get("node", "?"),
                "uptime_s": d.get("up", 0),
                "heap": d.get("heap", 0),
                "heap_min": d.get("heap_min", 0),
                "rssi": d.get("rssi", 0),
                "boots": d.get("boots", 0),
                "reconnects": d.get("recon", 0),
                "stack_json": json.dumps(d.get("stack", {}), separators=(",", ":")),
            }
        )
        fh.flush()
        print(
            f"{d.get('node')}  up={d.get('up')}s  heap_min={d.get('heap_min')}  "
            f"boots={d.get('boots')}  recon={d.get('recon')}"
        )

    client = mqtt.Client()
    if username:
        client.username_pw_set(username, password or "")
    client.on_connect = on_connect
    client.on_message = on_message
    client.connect(host, port, keepalive=60)

    print(f"Ghi vào {out}. Dừng bằng Ctrl+C.")
    try:
        client.loop_forever()
    except KeyboardInterrupt:
        print("\nDừng ghi.")
    finally:
        fh.close()


def report(path: Path, plot: Path | None) -> int:
    rows = list(csv.DictReader(path.open(encoding="utf-8")))
    if not rows:
        sys.exit(f"{path} chưa có số liệu nào.")

    nodes = sorted({r["node"] for r in rows})
    failed = False

    for node in nodes:
        rs = [r for r in rows if r["node"] == node]
        heap_min = [int(r["heap_min"]) for r in rs]
        boots = [int(r["boots"]) for r in rs]
        hours = (int(rs[-1]["uptime_s"]) - int(rs[0]["uptime_s"])) / 3600.0

        print(f"\n═══ {node} ═══")
        print(f"  Số điểm đo      : {len(rs)}")
        print(f"  Thời lượng      : {hours:.1f} giờ")

        # 1. Rò rỉ bộ nhớ. heap_min chỉ có thể đi xuống; điều đáng lo là nó
        #    tiếp tục đi xuống sau khi hệ thống đã ổn định. So nửa đầu với nửa cuối.
        half = len(heap_min) // 2 or 1
        drift = min(heap_min[:half]) - min(heap_min[half:])
        leak_ok = drift <= 0 or (hours > 0 and drift / max(hours, 1) < 512)
        print(f"  heap_min đầu/cuối: {heap_min[0]} → {heap_min[-1]} byte")
        print(f"  Trôi nửa sau     : {drift:+d} byte  {'✅' if leak_ok else '❌ NGHI RÒ RỈ'}")
        failed |= not leak_ok

        # 2. Reset ngoài ý muốn
        boot_ok = boots[-1] == boots[0]
        print(f"  Số lần boot      : {boots[0]} → {boots[-1]}  "
              f"{'✅' if boot_ok else '❌ CÓ RESET NGOÀI Ý MUỐN'}")
        failed |= not boot_ok

        # 3. Ngăn xếp
        worst: dict[str, int] = {}
        for r in rs:
            for task, free_words in json.loads(r["stack_json"]).items():
                w = int(free_words)
                if task not in worst or w < worst[task]:
                    worst[task] = w
        print("  Ngăn xếp còn dư ít nhất:")
        for task, words in sorted(worst.items()):
            total = STACK_BYTES.get(task)
            if total:
                pct = (words * 4) / total * 100
                ok = pct >= MIN_STACK_FREE_PCT
                failed |= not ok
                print(f"    {task:6s} {words * 4:6d} byte ({pct:5.1f} %) "
                      f"{'✅' if ok else '❌ dưới 25 %'}")
            else:
                print(f"    {task:6s} {words * 4:6d} byte (chưa biết cỡ stack)")

    if plot:
        make_plot(rows, nodes, plot)

    print("\n" + ("❌ CHƯA QUA cổng chất lượng G1.8." if failed
                  else "✅ QUA cổng chất lượng G1.8."))
    return 1 if failed else 0


def make_plot(rows: list[dict], nodes: list[str], out: Path) -> None:
    try:
        import matplotlib.pyplot as plt
    except ImportError:
        print("Bỏ qua đồ thị: chưa cài matplotlib (pip install matplotlib)")
        return

    fig, ax = plt.subplots(figsize=(10, 4.5))
    for node in nodes:
        rs = [r for r in rows if r["node"] == node]
        hours = [int(r["uptime_s"]) / 3600 for r in rs]
        ax.plot(hours, [int(r["heap_min"]) / 1024 for r in rs], label=f"{node} heap_min")

    ax.set_xlabel("Thời gian chạy (giờ)")
    ax.set_ylabel("Heap thấp nhất (KB)")
    ax.set_title("Bài chạy liên tục 72 giờ — đường phải phẳng")
    ax.grid(alpha=0.3)
    ax.legend()
    fig.tight_layout()
    fig.savefig(out, dpi=150)
    print(f"Đã lưu đồ thị: {out}")


def main() -> None:
    ap = argparse.ArgumentParser(description="Giám sát bài chạy 72 giờ (G1.8)")
    ap.add_argument("--host", default="192.168.1.10", help="địa chỉ broker nội bộ")
    ap.add_argument("--port", type=int, default=1883)
    ap.add_argument("--user")
    ap.add_argument("--password")
    ap.add_argument("--out", type=Path, default=Path("soak.csv"))
    ap.add_argument("--report", type=Path, help="chấm điểm file CSV đã ghi")
    ap.add_argument("--plot", type=Path, help="xuất đồ thị PNG khi chấm điểm")
    args = ap.parse_args()

    if args.report:
        sys.exit(report(args.report, args.plot))
    record(args.host, args.port, args.out, args.user, args.password)


if __name__ == "__main__":
    main()
