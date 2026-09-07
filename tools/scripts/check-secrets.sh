#!/usr/bin/env bash
# Kiểm tra rò rỉ bí mật trước khi push.
#
# ĐÂY LÀ NƠI DUY NHẤT định nghĩa mẫu quét. CI (.github/workflows/ci.yml) gọi
# chính script này, nên không có chuyện CI và kiểm tra thủ công dùng hai bộ
# luật khác nhau rồi lệch nhau lúc nào không biết.
#
# Chạy:  bash tools/scripts/check-secrets.sh
# Trả 0 nếu sạch, 1 nếu phát hiện vấn đề.

set -uo pipefail
cd "$(git rev-parse --show-toplevel)" || exit 1

fail=0

# ──────────────────────────────────────────────────────────────
# Mẫu vật liệu mật thật. Bắt buộc có dấu gạch ngang bao quanh vì
# PEM thật luôn có chúng — nhờ vậy tài liệu nhắc tới "private key"
# bằng lời văn thì không bị báo nhầm.
# Chuỗi được ghép từ nhiều phần để CHÍNH FILE NÀY không tự khớp.
# ──────────────────────────────────────────────────────────────
D5="-----"
PAT="${D5}BEGIN (RSA |EC |OPENSSH )?PRIVATE KEY${D5}"
PAT="${PAT}|${D5}BEGIN CERTIFICATE${D5}"
PAT="${PAT}|aws_secret_access_key"
PAT="${PAT}|AKIA[0-9A-Z]{16}"

# File được miễn trừ: file mẫu, tài liệu bảo mật, và chính script này.
EXCLUDES=(':!*.example' ':!docs/07-security.md' ':!tools/scripts/check-secrets.sh')

echo "▸ Quét nội dung file đang được git theo dõi…"
# `-e` là bắt buộc: mẫu bắt đầu bằng dấu gạch ngang nên nếu không có -e
# thì git grep tưởng đó là tham số dòng lệnh.
if git grep -nIE -e "$PAT" -- "${EXCLUDES[@]}"; then
  echo "  ❌ Phát hiện khoá hoặc chứng chỉ trong mã nguồn."
  fail=1
else
  echo "  ✅ Không có vật liệu mật nào trong nội dung."
fi

echo "▸ Kiểm tra file bí mật có bị git theo dõi không…"
if git ls-files | grep -E '(^|/)(secrets\.ini|\.env)$|\.(pem|key|p12|pfx|der)$'; then
  echo "  ❌ File bí mật đang được git theo dõi. Gỡ ra và thêm vào .gitignore."
  fail=1
else
  echo "  ✅ Không có file bí mật nào được theo dõi."
fi

echo "▸ Kiểm tra file bí mật có đang chờ commit không…"
if git diff --cached --name-only | grep -E '(^|/)(secrets\.ini|\.env)$|Secrets\.(cpp|h)$'; then
  echo "  ❌ File bí mật đang nằm trong staging area."
  fail=1
else
  echo "  ✅ Staging area sạch."
fi

echo
if [ "$fail" -eq 0 ]; then
  echo "✅ SẠCH — an toàn để push."
else
  echo "❌ DỪNG LẠI — xử lý các vấn đề trên trước khi push."
  echo "   Nếu khoá đã từng bị đẩy lên remote, xem docs/07-security.md:"
  echo "   thu hồi trên nhà cung cấp mới là biện pháp thật, dọn git chỉ là dọn dẹp."
fi
exit "$fail"
