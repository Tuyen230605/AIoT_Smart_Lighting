"""Chữ vẽ chồng lên khung hình.

`cv2.putText` chỉ có phông Hershey, chỉ vẽ được ASCII: mọi ký tự khác thành "?".
Tên vùng tiếng Việt "ghế đọc sách" vì thế hiện thành "gh??? ?????c s??ch", và cả
dấu "·" dùng làm phân cách cũng thành "??". Bỏ dấu trước khi vẽ thì vẫn đọc được
("ghe doc sach"); tên gốc có dấu vẫn đi qua MQTT nguyên vẹn, chỉ phần vẽ bị đổi.
"""

from __future__ import annotations

import unicodedata

# Ký tự không tự tách được thành "chữ gốc + dấu" khi chuẩn hoá NFKD.
_SPECIAL = str.maketrans({"đ": "d", "Đ": "D", "·": "-", "–": "-", "—": "-"})


def to_ascii(s: str) -> str:
    s = s.translate(_SPECIAL)
    s = unicodedata.normalize("NFKD", s)
    return s.encode("ascii", "ignore").decode("ascii")
