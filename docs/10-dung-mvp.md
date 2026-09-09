# Dựng hệ thống từ số không

Hướng dẫn đi từ "chưa có gì" đến "hệ thống chạy hàng ngày". Tổng cộng khoảng
một ngày làm việc, phần lớn là chờ tải.

**Thứ tự có lý do:** mỗi bước chỉ bắt đầu được khi bước trước đã chạy. Đừng nhảy
cóc — nếu bước 3 hỏng mà bước 2 chưa kiểm chứng, bạn không biết lỗi ở đâu.

```
  A. Pi có hệ điều hành, SSH được          ← không có cái này thì không làm gì được
        ↓
  B. Pi phát sóng Oi-Net                    ← node cần chỗ để nối vào
        ↓
  C. Broker chạy                            ← node cần chỗ để gửi tin
        ↓
  D. Node 1 nối được, đèn sáng              ← lần đầu thấy hệ thống sống
        ↓
  E. Node 2 nối được, radar chạy
        ↓
  F. Dashboard xem được
        ↓
  G. Camera và các dịch vụ AI
        ↓
  H. Dọn vào ở — bấm đồng hồ S4
```

---

## Chuẩn bị trên bàn

| Món | Ghi chú |
|---|---|
| Raspberry Pi 5 + nguồn 27 W + active cooler | Nguồn yếu là nguyên nhân lỗi khó đoán nhất |
| Thẻ nhớ microSD ≥ 32 GB | Class A2 nếu có |
| Dây mạng LAN | Cắm Pi vào router, để nguyên suốt |
| Camera Module 3 + cáp CSI | |
| Node 1: ESP32-S3 + mic + LED + lux + 4 nút | Đã lắp từ DACN |
| Node 2: ESP32 + radar LD2410 + relay | Đã lắp từ DACN |
| Cáp USB nạp firmware | |
| *(nên có)* cáp USB-TTL | Cứu hộ khi cấu hình mạng hỏng |

---

## A. Pi 5 — hệ điều hành và SSH

### A1. Ghi thẻ nhớ

Cài **Raspberry Pi Imager** trên laptop. Chọn:

- Thiết bị: Raspberry Pi 5
- Hệ điều hành: **Raspberry Pi OS (64-bit)**
- Thẻ nhớ: thẻ của bạn

Bấm bánh răng (⚙) để đặt trước — **bước này quan trọng**, làm đúng thì không cần
màn hình cho Pi:

```
  Hostname            : oi
  Bật SSH             : ✓  (dùng mật khẩu)
  Tên người dùng      : oi
  Mật khẩu            : (đặt và nhớ)
  Cấu hình Wi-Fi      : điền wifi nhà bạn — TẠM THỜI, để lần đầu vào được
  Múi giờ / bàn phím  : Asia/Ho_Chi_Minh
```

> Wi-Fi ở đây chỉ dùng cho lần khởi động đầu. Bước B sẽ chuyển Wi-Fi sang chế độ
> phát sóng, và từ đó Pi dùng dây mạng để ra Internet.

Ghi thẻ, cắm vào Pi, **cắm dây mạng vào router**, cắm nguồn.

### A2. Vào được Pi

Đợi khoảng 2 phút cho lần khởi động đầu. Từ laptop:

```bash
ssh oi@oi.local
```

Không được thì tìm địa chỉ IP trong trang quản lý của router (tìm thiết bị tên
`oi`), rồi `ssh oi@192.168.1.xxx`.

**✓ Kiểm chứng:** thấy dấu nhắc `oi@oi:~ $`.

### A3. Cập nhật và cài Docker

```bash
sudo apt update && sudo apt full-upgrade -y
sudo apt install -y docker.io docker-compose-plugin git
sudo usermod -aG docker $USER
sudo reboot
```

Đợi Pi khởi động lại, SSH vào lần nữa.

**✓ Kiểm chứng:** `docker run --rm hello-world` chạy được, không cần `sudo`.

---

## B. Pi phát sóng Oi-Net

> ⚠ **Phải làm qua dây mạng, không làm qua SSH bằng Wi-Fi.** Lệnh dưới đây sẽ
> ngắt kết nối Wi-Fi của Pi — nếu bạn đang SSH qua Wi-Fi thì tự cắt đứt chính mình
> giữa chừng, có khi bỏ cấu hình dở dang.
>
> Kiểm tra trước: `who` — nếu thấy IP của bạn thuộc dải mạng dây thì an toàn.
> Cách chắc chắn: rút Wi-Fi khỏi laptop, chỉ dùng mạng dây, rồi SSH.

```bash
# Mã quốc gia — thiếu thì Wi-Fi bị chặn cứng, không phát được
sudo raspi-config nonint do_wifi_country VN

# Tạo profile điểm phát
sudo nmcli connection add type wifi ifname wlan0 con-name oi-ap \
     autoconnect yes ssid Oi-Net

sudo nmcli connection modify oi-ap \
     802-11-wireless.mode ap \
     802-11-wireless.band bg \
     802-11-wireless.channel 6 \
     ipv4.method shared \
     ipv4.addresses 192.168.50.1/24

sudo nmcli connection modify oi-ap \
     wifi-sec.key-mgmt wpa-psk \
     wifi-sec.proto rsn \
     wifi-sec.pairwise ccmp \
     wifi-sec.psk "MAT_KHAU_MANG_OI"

sudo nmcli connection up oi-ap
```

Đặt `MAT_KHAU_MANG_OI` thành mật khẩu riêng, **không dùng lại mật khẩu Wi-Fi nhà**
và không chia cho ai. Đây là hàng rào bảo mật chính của cả hệ thống.

**✓ Kiểm chứng:**
```bash
ip addr show wlan0        # phải thấy 192.168.50.1/24
nmcli device status       # wlan0 ở trạng thái "connected"
```
Lấy điện thoại dò sóng — phải thấy `Oi-Net`. Nối vào, mở trình duyệt gõ
`192.168.50.1` (chưa có gì hiện ra là đúng, chưa dựng web).

**Nếu hỏng:** `sudo rfkill list` xem `wlan0` có bị chặn không. `sudo journalctl -u
NetworkManager -n 50` xem log.

---

## C. Broker

### C1. Lấy mã nguồn lên Pi

```bash
cd ~
git clone <địa-chỉ-kho-của-bạn> oi
cd oi/hub
```

### C2. Tạo ba tài khoản broker

Ba tài khoản cho ba mức tin cậy — xem `config/mosquitto/acl` để biết vì sao không
dùng chung một tài khoản.

```bash
docker run --rm -v "$PWD/config/mosquitto:/m" eclipse-mosquitto:2 \
  mosquitto_passwd -c -b /m/passwd node MAT_KHAU_NODE
docker run --rm -v "$PWD/config/mosquitto:/m" eclipse-mosquitto:2 \
  mosquitto_passwd -b /m/passwd svc MAT_KHAU_DICH_VU
docker run --rm -v "$PWD/config/mosquitto:/m" eclipse-mosquitto:2 \
  mosquitto_passwd -b /m/passwd ui MAT_KHAU_DASHBOARD
```

### C3. Cấu hình và bật

```bash
cp .env.example .env
nano .env                     # điền OI_MQTT_PASS = MAT_KHAU_DICH_VU, toạ độ nhà

cp ui/config.js.example ui/config.js
nano ui/config.js             # điền MAT_KHAU_DASHBOARD

docker compose up -d --build broker state
docker compose logs -f state
```

**✓ Kiểm chứng:** log của `state` in `oi-state khởi động`. Rồi:

```bash
docker run --rm -it --network host eclipse-mosquitto:2 \
  mosquitto_sub -h 192.168.50.1 -u svc -P MAT_KHAU_DICH_VU -t 'oi/#' -v
```

Phải thấy `oi/sys/heartbeat` đập mỗi giây. **Cửa sổ này để mở** — nó là công cụ
gỡ lỗi tốt nhất cho mọi bước sau.

---

## D. Node 1 — đèn, mic, lux, nút bấm

### D1. Kiểm tra phần cứng trước khi nạp

Đối chiếu với `firmware/node1_controller/include/Config.h`:

| Bộ phận | Chân ESP32-S3 |
|---|---|
| Dải LED WS2812B (256 bóng) | GPIO **48** |
| Mic INMP441 — SCK / WS / SD | **41** / **42** / **2** |
| Cảm biến lux VEML7700 — SDA / SCL | **8** / **9** |
| LED chỉ thị nghe | GPIO **3** |
| Nút bật-tắt / tăng / giảm / đổi màu | **4** / **5** / **6** / **7** |

Ba điều về điện, đọc kỹ — đây là chỗ hỏng có hậu quả vật lý:

- **Mass chung.** Nếu dải LED và ESP32 lấy điện từ hai nguồn khác nhau, hai mass
  **phải** nối với nhau. Không nối thì tín hiệu không có mốc tham chiếu và dải LED
  sáng loạn — triệu chứng trông y hệt lỗi phần mềm, ngốn hàng giờ tìm nhầm chỗ.
- **Cấp điện hai đầu dải.** 256 bóng nối tiếp có điện trở dây đáng kể; cấp một đầu
  thì bóng cuối tối hơn và ngả vàng.
- **Tụ 1000 µF** đầu dải và **điện trở 330 Ω** nối tiếp đường dữ liệu. Vài nghìn
  đồng, ngăn được lỗi vặt kéo dài nhiều tuần.

### D2. Cấu hình và nạp

Trên **laptop** (không phải trên Pi):

```bash
cd firmware/node1_controller
cp secrets.ini.example secrets.ini
```

Sửa `secrets.ini`:

```ini
-D WIFI_SSID='"Oi-Net"'
-D WIFI_PASS='"MAT_KHAU_MANG_OI"'
-D HUB_MQTT_HOST='"192.168.50.1"'
-D HUB_MQTT_PORT=1883
-D MQTT_USER='"node"'
-D MQTT_PASS='"MAT_KHAU_NODE"'
-D OTA_PASSWORD='"MAT_KHAU_OTA"'
```

Cắm USB, nạp:

```bash
pio run -t upload
pio device monitor
```

### D3. Kiểm chứng

Trên màn hình Serial phải thấy lần lượt:

```
🚀 KHỞI ĐỘNG NODE 1 — ESP32-S3 (Oi)
   Giao thức v1.1 · lần khởi động thứ 1
✅ Đã tạo 5 task trên hai nhân.
📡 Kết nối Wi-Fi Oi-Net...
✅ Wi-Fi ok, IP 192.168.50.x
🚀 Đã kết nối broker.
```

Ở cửa sổ `mosquitto_sub` đang mở phải thấy `oi/state/node1` và `oi/tele/node1`
chảy về.

**Thử tay:** bấm nút bật-tắt trên node — đèn phải đổi, **và** một bản tin
`oi/state/node1` với `"src":3` (nút bấm) phải hiện ra ở cửa sổ MQTT. Đó là đồng
bộ trạng thái hai chiều của G1.5 chạy thật.

**Nếu Wi-Fi không nối được:** ESP32 chỉ hiểu 2.4 GHz — kiểm tra lại `band bg` ở
bước B. Sai mật khẩu cũng cho triệu chứng giống hệt.

---

## E. Node 2 — radar và đèn cổng

### E1. Phần cứng

| Bộ phận | Chân ESP32 |
|---|---|
| Radar LD2410 — RX / TX | **16** / **17** (UART2, 256000 baud) |
| Relay đèn cổng | GPIO **2** |
| Nút BOOT | GPIO **0** (có sẵn trên board) |

Radar đấu chéo: TX của radar vào RX của ESP32 và ngược lại. Đấu thẳng là radar
không phản hồi — lỗi phổ biến nhất ở bước này.

### E2. Nạp

```bash
cd firmware/node2_gate
cp secrets.ini.example secrets.ini
# sửa giống node 1
pio run -t upload
pio device monitor
```

### E3. Kiểm chứng

```
🚪 KHỞI ĐỘNG NODE 2 — Radar Gate
🔍 Khởi tạo radar LD2410... OK
✅ Đã tạo 3 task.
```

Đi qua trước radar — cửa sổ MQTT phải thấy `oi/tele/node2` với `"pres":true` và
khoảng cách.

**Phép thử quan trọng nhất của cả node này:** rút dây mạng của Pi ra (hoặc tắt
`Oi-Net`), rồi đi qua radar lần nữa. **Đèn cổng vẫn phải bật và tự tắt sau 30
giây.** Đó là luật cục bộ của G1.6 — mất mạng không mất tự động hoá.

---

## F. Dashboard

```bash
cd ~/oi/hub
docker compose up -d --build ui
```

Từ laptop hoặc điện thoại **đã nối vào `Oi-Net`**, mở trình duyệt gõ:

```
192.168.50.1
```

**✓ Kiểm chứng:** thấy trạng thái hai node, chấm "đã nối broker" màu xanh, số lux
cập nhật. Bấm nút trên trang → đèn đổi, và bản tin trạng thái quay về với
`"src":4` (web).

**Nếu góc trên bên phải báo "lỗi kết nối":** mật khẩu trong `ui/config.js` sai,
hoặc chưa dựng lại ảnh sau khi sửa file đó (`docker compose up -d --build ui`).

---

## G. Camera và các dịch vụ còn lại

```bash
# Cắm cáp CSI khi Pi ĐANG TẮT. Bật lại rồi kiểm tra:
rpicam-hello --list-cameras

cd ~/oi/hub
docker compose up -d --build vision context policy
docker compose logs -f vision
```

Chưa có trọng số YOLO thì dịch vụ tự lùi về bộ dò chuyển động và nói rõ trong
log — hệ thống vẫn chạy. Tải trọng số sau:

```bash
docker compose exec vision python -c \
  "from ultralytics import YOLO; YOLO('yolov8n.pt')"
```

**✓ Kiểm chứng:** trên dashboard bấm "Xem" ở khung camera → thấy hình có hộp bao
người. Đóng lại → CPU của Pi giảm thấy được (`htop`), đó là ràng buộc 1 của
ADR 0005 chạy đúng.

Hiệu chuẩn vùng: bố cục mặc định gần như chắc chắn sai với phòng bạn. Tạo file
`zones.json` trong volume `oi_data`, toạ độ **chuẩn hoá 0..1**:

```json
{"zones": [
  {"name": "bàn làm việc", "mask": 1, "polygon": [[0.05,0.5],[0.4,0.5],[0.4,0.95],[0.05,0.95]]},
  {"name": "ghế đọc sách", "mask": 2, "polygon": [[0.6,0.5],[0.95,0.5],[0.95,0.95],[0.6,0.95]]}
]}
```

Dùng toạ độ chuẩn hoá chứ không phải pixel, để đổi độ phân giải camera không phải
hiệu chuẩn lại.

---

## H. Dọn vào ở — mốc G2.10

Đây là việc dễ bị bỏ quên nhất vì nó không tạo ra dòng mã nào, nhưng là **mốc
quan trọng nhất của cả giai đoạn**.

1. Cắm điện liên tục. **Dùng hệ thống thật** thay vì bật lên để thử.
2. Ghi **ngày bắt đầu** vào `docs/05-evaluation.md`. Mọi số liệu S4 tính mốc từ đây.
3. Đặt lịch sao lưu:
   ```bash
   crontab -e
   # 0 4 * * * docker run --rm -v oi_oi_data:/d -v /home/oi/backup:/b alpine \
   #     sh -c 'cp /d/oi.sqlite /b/oi-$(date +\%F).sqlite'
   ```

Từ ngày này, **mỗi ngày trôi qua là một ngày dữ liệu tích luỹ** cho đóng góp C4,
kể cả những ngày bạn bận việc khác. Đó là toàn bộ lý do kế hoạch được tái cấu
trúc sang MVP.

**Sau một tuần, kiểm tra:**

```bash
docker compose exec state python -c "
from oi_state.db import StateDB
db = StateDB('/data/oi.sqlite')
print(db.correction_rate(days=7))"
```

Có ngày trống thì tìm nguyên nhân **ngay** — một tuần dữ liệu mất là một tuần
không lấy lại được.

---

## Việc để sau, đừng làm bây giờ

Làm bây giờ sẽ thêm biến số vào lúc bạn cần ít biến số nhất. Quay lại khi hệ đã
chạy ổn định vài ngày:

| Việc | Khi nào |
|---|---|
| Đặt mật khẩu cho dashboard (nginx basic auth) | Sau khi mọi thứ chạy |
| Bật TLS cho MQTT cổng 8883 | G2.1 hoàn chỉnh |
| Đường tiếng nói (`--profile voice`) | Sau khi tải trọng số Whisper |
| Nạp firmware qua Wi-Fi (OTA) | Sau lần nạp bằng cáp đầu tiên |

---

## Khi hỏng thì xem gì

| Triệu chứng | Nhìn vào đâu |
|---|---|
| Node không nối được Wi-Fi | `pio device monitor` · kiểm tra `band bg` ở bước B |
| Node nối Wi-Fi nhưng không tới broker | Sai mật khẩu `node`, hoặc sai IP hub trong `secrets.ini` |
| Dashboard trắng trang | `docker compose logs ui` |
| Dashboard hiện nhưng không có số | Mật khẩu trong `ui/config.js`; mở F12 xem tab Console |
| Đèn không sáng | Mass chung? Nguồn đủ dòng? Chân GPIO48? |
| Radar không phản hồi | Đấu chéo TX/RX chưa? |
| Mất hẳn đường vào Pi | Cáp USB-TTL — lý do nó nằm trong ngăn kéo |
