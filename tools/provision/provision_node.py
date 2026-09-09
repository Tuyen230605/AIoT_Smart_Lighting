#!/usr/bin/env python3
"""Nạp chứng chỉ TLS vào phân vùng NVS `oi_creds` của một node (G1.7).

VÌ SAO CÓ CÔNG CỤ NÀY
Sự cố lộ khoá ở G0.1 xảy ra vì chứng chỉ nằm trong mã nguồn — một kiến trúc
không có cách nào commit mã mà không commit khoá. Công cụ này là nửa còn lại
của cách sửa: khoá đi thẳng từ máy của bạn vào flash thiết bị, không bao giờ
đi qua kho mã lẫn ảnh firmware.

    strings .pio/build/*/firmware.bin | grep "BEGIN CERTIFICATE"   → phải rỗng

CHỨNG CHỈ NÀO
Chứng chỉ TLS của chính mạng nhà, không phải của nhà cung cấp đám mây nào (xem
ADR 0004 — hệ thống không kết nối ra ngoài). Bộ ba cần nạp:
    ca    CA tự ký của hub, để node xác thực được broker
    cert  chứng chỉ của riêng node này, do CA trên ký
    key   khoá riêng của node

Sinh bộ này lúc dựng hub ở G2.1; quy trình ghi trong docs/07-security.md.

CÁCH DÙNG
    python tools/provision/provision_node.py --port COM5 --node node1 \\
        --ca   certs/oi-ca.pem \\
        --cert certs/node1.crt \\
        --key  certs/node1.key

    python tools/provision/provision_node.py --port COM5 --status
    python tools/provision/provision_node.py --port COM5 --erase

CHƯA DỰNG HUB THÌ CHƯA CẦN CHẠY. Node vẫn hoạt động đầy đủ qua cổng 1883 không
mã hoá; thiếu chứng chỉ chỉ nghĩa là chưa bật được TLS trong mạng nhà.

THƯ MỤC CHỨA CHỨNG CHỈ PHẢI NẰM NGOÀI KHO MÃ, hoặc ít nhất trong một đường dẫn
đã .gitignore. Công cụ sẽ cảnh báo nếu phát hiện file nằm trong kho mã.
"""

from __future__ import annotations

import argparse
import base64
import subprocess
import sys
import time
from pathlib import Path

try:
    import serial  # pyserial
except ImportError:
    sys.exit("Thiếu pyserial. Cài bằng:  pip install pyserial")

BAUD = 115200
FIELDS = ("ca", "cert", "pkey")


def in_git_repo(path: Path) -> bool:
    """Cảnh báo nếu chứng chỉ đang nằm trong kho mã — nguyên nhân gốc của G0.1."""
    try:
        out = subprocess.run(
            ["git", "check-ignore", "-q", str(path)],
            capture_output=True,
            cwd=path.parent,
        )
        # rc 0 = đã bị gitignore (an toàn), 1 = KHÔNG bị ignore, 128 = ngoài repo
        return out.returncode == 1
    except OSError:
        return False


def read_pem(path: Path, kind: str) -> bytes:
    if not path.exists():
        sys.exit(f"Không tìm thấy {kind}: {path}")
    data = path.read_bytes()
    if b"-----BEGIN" not in data:
        sys.exit(f"{path} không giống file PEM (thiếu dòng BEGIN).")
    if in_git_repo(path):
        print(f"⚠ CẢNH BÁO: {path} nằm trong kho mã và KHÔNG bị gitignore.")
        print("  Chuyển nó ra ngoài kho trước khi commit bất cứ thứ gì.")
    return data


def send_line(ser: serial.Serial, line: str, expect: str, timeout: float = 10.0) -> str:
    ser.reset_input_buffer()
    ser.write((line + "\n").encode())
    ser.flush()

    deadline = time.time() + timeout
    while time.time() < deadline:
        raw = ser.readline().decode(errors="replace").strip()
        if not raw:
            continue
        if raw.startswith("OI-PROV"):
            if raw.startswith("OI-PROV ERR"):
                sys.exit(f"Thiết bị báo lỗi: {raw}")
            if expect in raw:
                return raw
    sys.exit(f"Hết thời gian chờ phản hồi '{expect}'. Node có đang chạy firmware G1.7 không?")


def main() -> None:
    ap = argparse.ArgumentParser(description="Nạp chứng chỉ vào NVS của node Oi")
    ap.add_argument("--port", required=True, help="cổng serial, ví dụ COM5 hoặc /dev/ttyUSB0")
    ap.add_argument("--node", default="node1", help="chỉ để hiển thị và kiểm tra")
    ap.add_argument("--ca", type=Path, help="CA tự ký của hub (.pem)")
    ap.add_argument("--cert", type=Path, help="chứng chỉ của node (.crt)")
    ap.add_argument("--key", type=Path, help="khoá riêng của node (.key)")
    ap.add_argument("--status", action="store_true", help="chỉ hỏi trạng thái cấp phát")
    ap.add_argument("--erase", action="store_true", help="xoá sạch chứng chỉ trong NVS")
    args = ap.parse_args()

    payloads: dict[str, bytes] = {}
    if not args.status and not args.erase:
        if not (args.ca and args.cert and args.key):
            sys.exit(
                "Cần đủ --ca, --cert, --key.\n"
                "Chưa dựng hub và chưa sinh chứng chỉ thì chưa cần chạy lệnh này:\n"
                "node vẫn chạy đầy đủ qua cổng 1883 trong mạng nhà."
            )
        payloads["ca"] = read_pem(args.ca, "CA của hub")
        payloads["cert"] = read_pem(args.cert, "chứng chỉ node")
        payloads["pkey"] = read_pem(args.key, "khoá riêng")

    with serial.Serial(args.port, BAUD, timeout=1) as ser:
        time.sleep(2)  # ESP32 tự reset khi mở cổng — đợi nó khởi động xong

        ready = send_line(ser, "PROV:PING", "READY", timeout=15)
        print(f"✅ Đã bắt tay: {ready}")

        if args.erase:
            print(send_line(ser, "PROV:ERASE", "ERASED"))
            return

        if args.status:
            print(send_line(ser, "PROV:STATUS", "STATUS"))
            return

        for field in FIELDS:
            b64 = base64.b64encode(payloads[field]).decode()
            reply = send_line(ser, f"PROV:SET:{field}:{b64}", f"OK {field}", timeout=20)
            print(f"✅ {reply}")

        print(send_line(ser, "PROV:STATUS", "STATUS"))
        print("\nXong. Kiểm chứng bắt buộc — ảnh firmware phải sạch:")
        print('  strings .pio/build/*/firmware.bin | grep "BEGIN CERTIFICATE"')


if __name__ == "__main__":
    main()
