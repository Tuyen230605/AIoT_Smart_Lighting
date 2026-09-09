# Báo cáo thay đổi kiến trúc — từ DACN sang ĐATN

Tài liệu này giải thích **vì sao** kiến trúc thay đổi, không chỉ **thay đổi thành gì**.
Nó tồn tại để trả lời câu hỏi hội đồng chắc chắn sẽ đặt — *"vì sao em làm thế này?"* —
bằng lý lẽ đã ghi lại lúc quyết định, chứ không phải bằng lý lẽ dựng lại lúc bảo vệ.

Mỗi thay đổi trình bày theo bốn phần: hiện trạng, vấn đề, cách sửa, và **cách kiểm chứng
rằng nó thực sự được sửa**. Phần cuối cùng là phần quan trọng nhất: một thay đổi kiến trúc
không kiểm chứng được thì không phân biệt được với một lời tuyên bố.

---

## Phần I — Bỏ phụ thuộc đám mây: trước và sau

Quyết định đầy đủ nằm ở [ADR 0004](adr/0004-bo-phu-thuoc-dam-may.md). Mục này không nhắc
lại lý lẽ đó mà làm việc khác: **đặt hệ thống cũ và hệ thống mới cạnh nhau ở từng lớp**,
để lợi ích không phải là điều phải tin mà là điều nhìn thấy được.

### I.1 · Vì sao bỏ — ba dữ kiện

| Dữ kiện | Hệ quả |
|---|---|
| Không đóng góp nào (C1–C4) và không thí nghiệm nào (A/B/C) cần đến AWS | Nó không nằm trong phần được chấm điểm |
| Ma trận suy giảm ghi rõ mức L1: mất Internet thì cả 8 mô hình vẫn chạy | Mất nó đi không giảm chức năng nào |
| Đề tài định vị là sản phẩm bán theo hộ gia đình | Chi phí đám mây lặp lại theo từng thiết bị, kéo dài vô hạn, do nhà sản xuất gánh |

Dữ kiện thứ ba nặng nhất, và nó không phải chuyện kỹ thuật. Một sản phẩm nhà thông minh
phụ thuộc cloud của nhà sản xuất là sản phẩm **sẽ chết khi nhà sản xuất tắt server** —
chuyện đã xảy ra với nhiều dòng thiết bị thật. Với hệ thống chiếu sáng gắn cố định lên
tường, đó là rủi ro người mua gánh mà không kiểm soát được.

### I.2 · Sơ đồ hạ tầng: hai bức tranh

```
════ TRƯỚC ════════════════════════════════════════════════════════

  TẦNG 3 · AWS IoT Core, vùng ap-southeast-2 (Sydney)     BẮT BUỘC CÓ
           · broker chính — mọi bản tin đều đi qua đây
           · Device Shadow — một nửa nguồn sự thật trạng thái
           · Node-RED — nơi đặt luật "có người → bật đèn"
                              ▲
                              │  cần Internet, cần tài khoản, cần thẻ
                              │
  TẦNG 2 · (không tồn tại)
                              ▲
                              │
  TẦNG 1 · ESP32 × 2 — chỉ chấp hành, không tự quyết gì
           · chứng chỉ AWS biên dịch cứng vào ảnh firmware
           · mất Internet là mất tự động hoá


════ SAU ══════════════════════════════════════════════════════════

  TẦNG 3 · Đường hầm mạng riêng trên hub                  TUỲ CHỌN
           · chỉ để chủ nhà nhìn vào hệ thống từ xa
           · tắt hẳn đi thì không chức năng nào mất
                              ┄ ┄ ┄ (đứt được)
  TẦNG 2 · Hub · Raspberry Pi 5, trong nhà
           · Mosquitto — broker chính, cách node 5 ms
           · 9 container AI
           · SQLite — nguồn sự thật DUY NHẤT
                              ▲
                              │  chỉ cần mạng LAN
                              │
  TẦNG 1 · ESP32 × 3 — tự quyết được phần việc của mình
           · luật cổng nằm trong firmware, không rời con chip
           · chứng chỉ TLS mạng nhà, nằm trong phân vùng NVS
           · mất mạng vẫn chạy lớp phản xạ và luật cục bộ
```

Điều thay đổi về bản chất không phải là "đổi nhà cung cấp broker", mà là **tầng bắt buộc
bị hạ từ ngoài lãnh thổ xuống trong nhà, rồi hạ tiếp một phần xuống tận con chip**.

### I.3 · Một sự kiện cụ thể: có người đi qua cổng lúc trời tối

Đây là chức năng đơn giản nhất của cả hệ thống, và là chỗ chênh lệch lộ ra rõ nhất.

```
TRƯỚC — 6 chặng, hai lần vượt biên giới quốc gia

  radar ─▶ ESP32 ─▶ Wi-Fi ─▶ ISP ─▶ AWS (Sydney) ─▶ Node-RED
                                                        │
  đèn ◀── ESP32 ◀── Wi-Fi ◀── ISP ◀── AWS (Sydney) ◀────┘

  └──────────── 200 ms – 1,4 s · CẦN Internet ────────────┘


SAU — 3 chặng, không chặng nào rời khỏi con chip

  radar ─▶ TaskRadar ─▶ hàng đợi ─▶ TaskLogic ─▶ đèn

  └────── ~150 ms · không cần gì ngoài chính nó ──────┘
```

**Trước** — 6 chặng, hai lần vượt biên giới quốc gia:

| # | Chặng | Thời gian |
|---|---|---|
| 1 | Radar phát hiện, nhưng `loop()` chỉ publish mỗi 1000 ms | 0–1000 ms |
| 2 | Node 2 → router → ISP → AWS IoT Core (Sydney) | ~80–150 ms |
| 3 | Rule engine → Node-RED xử lý luật | ~20–100 ms |
| 4 | Node-RED publish `oi/gate/command` → AWS | ~10 ms |
| 5 | AWS → ISP → router → Node 2 | ~80–150 ms |
| 6 | Callback MQTT → `digitalWrite` | <1 ms |
| | **Tổng** | **~200 ms đến 1,4 s** |

Và nếu đúng lúc đó Node 2 đang trong `delay(5000)` của vòng kết nối lại, radar **không hề
được đọc** trong 5 giây đó — người đã đi qua xong từ lâu.

**Sau** — 3 chặng, không chặng nào rời khỏi con chip:

| # | Chặng | Thời gian |
|---|---|---|
| 1 | `TaskRadar` đọc radar mỗi 100 ms, đẩy vào hàng đợi | 0–100 ms |
| 2 | `TaskLogic` đọc hàng đợi, đối chiếu luật + ngưỡng lux | ~50 ms |
| 3 | `digitalWrite` | <1 ms |
| | **Tổng** | **~150 ms**, không phụ thuộc mạng |

Chênh lệch không chỉ ở con số. Đường mới **không có chặng nào có thể hỏng vì lý do bên
ngoài**: không ISP, không vùng AWS, không hết hạn chứng chỉ, không hoá đơn quá hạn.

> **Lưu ý về nguồn số.** Các con số độ trễ mạng ở bảng "trước" là **ước tính** dựa trên
> độ trễ khứ hồi thông thường từ Việt Nam tới vùng ap-southeast-2, chưa phải số đo. Nếu
> đưa bảng này vào báo cáo, hãy đo lại bằng cách nạp firmware DACN cũ và bấm giờ bằng
> quay 240 fps — chênh lệch đo được sẽ thuyết phục hơn nhiều so với ước tính, và nó cũng
> là một số liệu đối chứng đẹp cho chương 4. Các con số ở bảng "sau" lấy trực tiếp từ
> tham số trong mã nguồn (`RADAR_POLL_MS = 100`, chu kỳ `TaskLogic` 50 ms).

### I.4 · Thay đổi ở từng node

| Khía cạnh | Trước (DACN) | Sau |
|---|---|---|
| Đích kết nối | `xxxxx-ats.iot.ap-southeast-2.amazonaws.com:8883` | `192.168.1.10:1883` → `:8883` khi bật TLS ở G2.1 |
| Số chặng mạng tới nơi ra quyết định | 2 chặng vượt Internet | 0 — quyết định nằm trong chính node |
| Thư viện TLS | `WiFiClientSecure` + xác thực máy chủ AWS | Chưa dùng; `WiFiClientSecure` với CA nhà từ G2.1 |
| Chứng chỉ nằm ở | `Secrets.cpp` và `Config.h`, **biên dịch cứng vào ảnh firmware** | Phân vùng NVS `oi_creds`, nạp qua Serial, **không có trong mã lẫn ảnh** |
| Ai cấp chứng chỉ | AWS, phải vào console để thu hồi hoặc xoay vòng | Bạn, bằng CA tự ký trên hub |
| Hạn dùng | Gói dùng thử 12 tháng rồi tính phí | Không có khái niệm đó |
| Khi mất mạng | Node 2 kẹt trong `delay(5000)`, radar không được đọc | `TaskNet` chờ riêng một luồng; `TaskRadar` và `TaskLogic` chạy bình thường |
| Cấu hình phải điền | `WIFI_*`, `AWS_MQTT_HOST`, cert, key, CA | `WIFI_*`, `HUB_MQTT_HOST`, `MQTT_USER/PASS`, `OTA_PASSWORD` |

Dòng áp chót là dòng đáng chú ý nhất: việc bỏ đám mây và việc tách FreeRTOS ở G1.6 nghe
như hai chuyện riêng, nhưng chúng chữa cùng một bệnh — **node cũ coi mạng là điều kiện để
hoạt động, node mới coi mạng là thứ có thì tốt**.

### I.5 · Thay đổi ở cách giao tiếp

| | Trước | Sau |
|---|---|---|
| Cây chủ đề | `oi/light/command`, `oi/radar/status`, `oi/telemetry/<thing>` — đặt tuỳ hứng, mỗi node một kiểu | `oi/cmd/<node>`, `oi/state/<node>`, `oi/tele/<node>`, `oi/sys/*` — một cây thống nhất |
| Định nghĩa ở đâu | Chuỗi ký tự rải rác trong từng file `.cpp` | `oi_protocol.h` + bản song sinh `protocol.py`, có kiểm thử parity trong CI |
| Đánh phiên bản | Không có | `OI_PROTO_MAJOR/MINOR`, node khác MAJOR từ chối nói chuyện |
| Báo trạng thái ngược lên | **Không có** — bật đèn bằng nút bấm thì không ai biết | `oi/state/<node>` retain sau mọi thay đổi, kèm `src` và `seq` |
| Biết node chết | Không có cách nào | Last Will `oi/sys/offline/<node>`, broker tự phát |
| Chống lệnh cũ đến muộn | Không có | Trường `seq` tăng dần, lệnh cũ bị bỏ |
| Độ trễ một chặng | ~80–150 ms (qua Sydney) | <5 ms (trong LAN) |
| Băng thông ra Internet | Mọi bản tin trạng thái và telemetry | 0 |

Ba dòng giữa là thứ mà việc bỏ đám mây *cho phép* làm tử tế: khi broker nằm cách 5 ms
thay vì 150 ms, phát một bản tin trạng thái sau **mọi** thay đổi trở thành chuyện bình
thường, chứ không còn là thứ phải cân nhắc vì tốn băng thông và tốn tiền theo số bản tin.

### I.6 · Thay đổi ở phía hub

| | Trước | Sau |
|---|---|---|
| Broker | AWS IoT Core | Mosquitto trong Docker trên Pi 5 |
| Số dịch vụ | 10 (gồm `oi-bridge` cầu nối lên Device Shadow) | 9 — bỏ `oi-bridge` |
| Nguồn sự thật trạng thái | Chia đôi: Device Shadow trên đám mây và bản ghi nội bộ, phải đồng bộ với nhau | Một chỗ duy nhất: SQLite của `oi-state` |
| Lưu vết dài hạn | Trên đám mây | SQLite trên hub — **phải tự đặt lịch sao lưu**, đây là việc mới phát sinh |
| Bảng điều khiển | Node-RED truy cập qua đám mây | Node-RED trong LAN, cũng chạy khi mất Internet |
| Truy cập từ xa | Qua AWS | Đường hầm mạng riêng trên hub, chủ nhà tự bật |

Dòng "lưu vết dài hạn" là một cái **mất** thật sự, không nên giấu: trước đây dữ liệu tự
động nằm ngoài nhà nên cháy nhà cũng còn; giờ nó nằm cùng chỗ với thiết bị. Cách bù là
sao lưu định kỳ ra ổ ngoài, và việc đó phải được ghi vào danh sách vận hành chứ không
phải nhớ trong đầu.

### I.7 · Bảng đối chiếu tổng hợp

| Tiêu chí | Trước | Sau | Đánh giá |
|---|---|---|---|
| Độ trễ luật đèn cổng | 200 ms – 1,4 s | ~150 ms | ✅ Tốt hơn |
| Chức năng khi mất Internet | Mất tự động hoá cổng và dashboard | Không mất gì | ✅ Tốt hơn |
| Chi phí định kỳ | Theo bản tin × số thiết bị, vô hạn | 0 | ✅ Tốt hơn |
| Số tài khoản bên thứ ba cần có | 1 (AWS, kèm thẻ thanh toán) | 0 | ✅ Tốt hơn |
| Bí mật trong ảnh firmware | Có chứng chỉ và khoá riêng | Không có gì | ✅ Tốt hơn |
| Số thành phần phải vận hành | Firmware + cấu hình đám mây | Firmware + một máy Pi trong nhà | ⚖️ Đổi chỗ, không tăng |
| Xem trạng thái khi đang ở ngoài nhà | Có sẵn qua AWS | Phải tự dựng đường hầm | ⚠️ Thêm việc |
| Xem được khi **hub đã sập** | Được — Device Shadow còn giữ bản cuối | Không được | ❌ Kém hơn |
| Lưu vết dài hạn ngoài nhà | Có sẵn | Phải tự sao lưu | ❌ Kém hơn |

Ba dòng cuối là ba cái mất. Chúng có thật, và bảng này giữ chúng lại có chủ ý: **một bảng
so sánh mà mọi dòng đều nghiêng về phương án mình chọn là một bảng chưa trung thực**, và
người phản biện sẽ nhận ra điều đó trước tiên. Lý lẽ để vẫn chọn phương án mới không phải
là "không mất gì", mà là ba cái mất đó đều nằm ở nhóm *tiện lợi*, còn những cái được nằm
ở nhóm *hệ thống có sống được một mình hay không*.

### I.8 · Kiểm chứng — làm sao biết việc bỏ đám mây là thật

Ba phép thử, xếp theo mức độ thuyết phục tăng dần:

1. **Quét mã nguồn.** `git grep -in "aws\|amazonaws"` chỉ còn khớp ở tài liệu lịch sử sự
   cố và ADR — không còn dòng mã nào. Đã chạy, đạt.
2. **Bắt gói.** Wireshark trên router trong 1 giờ vận hành bình thường: không gói nào từ
   ba node hoặc từ hub đi ra ngoài mạng LAN (yêu cầu NFR-07). Chạy ở G3.1.
3. **Rút dây và để nguyên.** Rút cáp WAN của router, vận hành **cả tuần** trong trạng thái
   đó, rồi đối chiếu bộ chỉ số S1–S7 với tuần có mạng. Không chỉ số nào được phép suy
   giảm. Đây là phép thử mạnh nhất, và cũng là cảnh đáng quay video để chiếu khi bảo vệ —
   nó chứng minh một luận điểm mà lời nói khó thuyết phục bằng.

Phép thử thứ ba là hiện thân của yêu cầu **NFR-11 · độc lập nhà cung cấp** mới thêm vào
bản đặc tả v2.1.

### I.9 · Ảnh hưởng lên tiến độ

Bớt một dịch vụ ở G2 (`oi-bridge`), bớt việc quản lý vòng đời chứng chỉ AWS, và bỏ được
ràng buộc thời hạn gói dùng thử. Thời gian dồn được chuyển sang C1 — phần thực sự được
chấm điểm. Đổi lại, thêm một việc vận hành mới: đặt lịch sao lưu SQLite của hub.

---

## Phần II — Sáu thay đổi trong firmware (G1)

### 1. Trạng thái đèn có một chủ sở hữu duy nhất

**Hiện trạng.** Ba biến `String` toàn cục (`cmdAction`, `cmdColor`, `cmdBrightness`) được
ghi từ Core 0 (`TaskMic`) và Core 1 (callback MQTT, `TaskButton`), đọc từ Core 1
(`TaskLED`). Không mutex, không queue, không `volatile`.

**Vấn đề.** `String` cấp phát trên heap. Một lần ghi trùng thời điểm với một lần đọc có
thể làm hỏng cấu trúc heap — biểu hiện là **reset ngẫu nhiên sau nhiều giờ chạy**, đúng
loại lỗi không bao giờ lộ ra trong mười phút thử nghiệm. Với một đồ án lấy FreeRTOS làm
trọng tâm, đây cũng là điểm hội đồng sẽ hỏi.

**Cách sửa và vì sao không dùng mutex.** Đây là chỗ dễ chọn sai. Mutex chống được *hỏng
dữ liệu* nhưng không chống được *mất cập nhật*: hai lần bấm nút tăng sáng sát nhau, cả hai
cùng đọc giá trị 100 rồi cùng ghi 115 — mutex vẫn cho ra 115 thay vì 130, và không có lỗi
nào được báo.

Cách sửa đi xa hơn một bước: tách lệnh thành hai loại.

| Loại lệnh | Nội dung truyền đi | Ví dụ |
|---|---|---|
| Tuyệt đối (`SET`) | toàn bộ trạng thái mong muốn | "bật đèn trắng, độ sáng 100" |
| Tương đối (`ADJUST_BRI`) | **ý định thay đổi**, không phải kết quả đã tính | "tăng 15" |

Lệnh tương đối đi qua đường dây dưới dạng ý định. Không task nào ngoài `TaskLED` cần đọc
trạng thái để tính toán, nên lớp lỗi mất cập nhật **biến mất về mặt cấu trúc** chứ không
chỉ được che bằng khoá.

Kèm theo: trường `seq` tăng dần cho phép bỏ lệnh cũ đến muộn. Mạng có thể đảo thứ tự, và
một lệnh "tắt" cũ đè lên lệnh "bật" mới là lỗi người dùng thấy ngay lập tức.

**Kiểm chứng.** Chạy tải nặng 12 giờ với giọng nói, nút bấm và lệnh MQTT dồn dập đồng
thời, LED chạy hiệu ứng cầu vồng. `ESP.getMinFreeHeap()` ghi mỗi phút phải cho đường phẳng.

### 2. Suy luận liên tục thay cho cửa sổ rời rạc

**Hiện trạng.** `audio_inference_callback()` ngừng nạp mẫu khi `buf_ready == 1`.

**Vấn đề.** Âm thanh phát ra trong lúc bộ phân loại đang chạy bị vứt bỏ. Từ khoá rơi vào
ranh giới cửa sổ **không bao giờ được nghe thấy**. Đây là một phần lý do độ chính xác thực
địa thấp hơn con số 94,5 % đo trên tập validation.

**Cách sửa.** Hai bộ đệm luân phiên: micro ghi vào bộ đệm A trong khi bộ phân loại đọc bộ
đệm B, và `run_classifier_continuous()` chạy trên các lát 250 ms. Mô hình vốn đã khai báo
`SLICES_PER_MODEL_WINDOW = 4` từ đầu nhưng mã nguồn chưa từng dùng đến.

**Kiểm chứng.** Nói từ khoá **cố ý lệch nhịp** — bắt đầu ngay giữa một cửa sổ cũ — lặp 30
lần, so tỉ lệ bắt được trước và sau khi sửa. Ghi cả hai con số vào báo cáo.

### 3. Bỏ noise gate phi tuyến

**Hiện trạng.** Đường tín hiệu cắt mọi mẫu có biên độ dưới 40 về 0, rồi nhân phần còn lại
với 8.

**Vấn đề.** Đây là một hàm gián đoạn. Nó sinh hài bậc cao tại mỗi điểm cắt và làm clip
int16 khi người dùng nói to. Phổ MFCC thu được không còn giống dữ liệu đã huấn luyện trên
Edge Impulse — nghĩa là **bộ lọc đang làm hại chính mô hình mà nó định giúp**.

**Cách sửa.** Bỏ hẳn khối gate; giữ bộ lọc thông cao IIR vì bộ này đúng và cần thiết (khử
lệch DC của INMP441). Việc chống ồn chuyển sang khâu tăng cường dữ liệu huấn luyện: cộng
nhiễu nền thu tại chính căn phòng vào tập train. Xử lý nhiễu ở khâu huấn luyện thì mô hình
học được cách sống chung với nhiễu; xử lý ở khâu suy luận thì chỉ làm lệch đầu vào.

**Hệ quả kéo theo.** Bỏ hệ số nhân ×8 làm biên độ mẫu nhỏ đi khoảng 8 lần, nên ngưỡng VAD
phải hạ theo cho tương đương: từ 50 xuống 6. **Con số 6 là ước tính, phải đo lại ở G1b.1** —
đã ghi rõ ngay trong mã nguồn để không ai coi nó là số đã chốt.

### 4. Giới hạn công suất và làm mượt chuyển cảnh

**Vấn đề.** 256 bóng WS2812B × trắng × độ sáng 255 rút khoảng **15 A**. Mã cũ không có
giới hạn nào, nên một lệnh "bật trắng tối đa" là một lệnh kéo sụt áp và gây brownout
reset. Ngoài ra `addLeds()` bị gọi ở cả `main.cpp` lẫn `Task_LED.cpp` — hai controller
cùng trỏ vào một mảng LED.

**Cách sửa.** `setMaxPowerInVoltsAndMilliamps(5 V, 16 A)`; gọi `addLeds()` đúng một lần;
thêm bộ nội suy chuyển cảnh tối thiểu 500 ms; thêm hàm đổi nhiệt độ màu Kelvin → RGB, vì
hub gửi lệnh theo `cct` chứ không theo mã màu.

### 5. Đồng bộ trạng thái hai chiều

**Hiện trạng.** `Task_Network` chỉ publish giá trị lux.

**Vấn đề.** Bật đèn bằng giọng nói hay nút bấm thì không có gì báo lên, nên bảng điều
khiển hiển thị sai. Báo cáo DACN có hẳn Hình 3.11 mô tả luồng đồng bộ này nhưng mã nguồn
chưa từng làm. Nghiêm trọng hơn: **mô hình học sở thích bắt buộc phải thấy mọi thay đổi
kèm nguồn lệnh**, nếu không sẽ không có gì để học — một lệnh `BUTTON` đến ngay sau một
lệnh `AGENT` chính là một lần người dùng sửa sai, tức mẫu huấn luyện quý nhất.

**Cách sửa.** Publish `oi/state/node1` có retain sau **mọi** thay đổi, kèm `src` và `seq`;
thêm Last Will để hub phân biệt được "node im lặng" với "node đã chết".

Kèm một lỗi thầm lặng được phát hiện trong lúc sửa: `Task_Sensors` trước đây gọi thẳng
`sendTelemetry()`, tức chạm vào PubSubClient từ một task khác — thư viện này không an toàn
đa luồng. Giờ chỉ `TaskNetwork` nói chuyện với broker.

### 6. Node 2 sang FreeRTOS và tự động hoá cục bộ

**Vấn đề.** Toàn bộ logic nằm trong `loop()` với `delay(5000)` mỗi lần kết nối lại broker.
Trong 5 giây đó radar **không được đọc**: có người đi qua cũng không biết. Và luật "có
người → bật đèn" đi vòng qua AWS, nên mất mạng là mất tự động hoá — mâu thuẫn trực tiếp
với luận điểm Edge Computing của chính đề tài.

**Cách sửa.** Tách thành `TaskRadar` / `TaskLogic` / `TaskNet`. Luật hiện diện nằm hẳn
trong `TaskLogic`. Hub chỉ được ghi đè trong `HUB_OVERRIDE_TTL_MS`, hết hạn thì quyền tự
trả về luật cục bộ — nếu không có cơ chế này, hub sập giữa lúc đang ghi đè là đèn kẹt
vĩnh viễn.

Một chi tiết đáng ghi: nếu số đo lux từ node 1 quá cũ (mất kết nối), luật **bỏ điều kiện
trời tối** thay vì giữ nguyên. Thà bật thừa còn hơn để người đi trong bóng tối chỉ vì
thiếu dữ liệu. Nguyên tắc chung: khi thiếu thông tin, chọn phía an toàn cho người dùng.

**Kiểm chứng.** Rút cáp mạng, đi qua radar — đèn vẫn phải bật và tự tắt sau 30 giây.

---

## Phần III — Hai thay đổi trong hợp đồng giao tiếp

**Giao thức lên phiên bản 1.1.**

Thêm trường `scene`. Bản DACN chở tên hiệu ứng trong chuỗi màu (`cmdColor = "lofi"`), tức
dùng một trường để chở hai nghĩa — thứ vỡ ngay khi cần vừa chọn hiệu ứng vừa chọn màu.

Thêm bốn khoá sức khoẻ thiết bị: `heap_min`, `stack`, `recon`, `boots`. Chúng là toàn bộ
bằng chứng cho cổng chất lượng 72 giờ, và phải do chính firmware tự ghi thì mới đo được
liên tục.

Cả hai thay đổi được nhân bản sang `protocol.py` và bổ sung kiểm thử parity **trong cùng
một commit** — đây là quy ước bắt buộc của repo, vì lệch hợp đồng giữa firmware và hub là
loại lỗi tốn thời gian gỡ nhất trong hệ phân tán.

---

## Phần IV — Bảng tổng hợp: thay đổi nào phục vụ đóng góp nào

| Thay đổi | Phục vụ | Nếu không làm thì sao |
|---|---|---|
| Bỏ phụ thuộc đám mây | Luận điểm Edge Computing, định vị sản phẩm | Luận điểm chỉ là tuyên bố, không kiểm chứng được bằng rút dây |
| Hàng đợi lệnh một chủ sở hữu | Nền cho mọi thứ sau | Reset ngẫu nhiên xuất hiện đúng tuần 17 |
| Suy luận liên tục | C1 — độ phủ của lớp phản xạ | Tỉ lệ leo thang bị thổi phồng vì lớp phản xạ bỏ sót oan |
| Bỏ noise gate | M1 — độ chính xác thực địa | Đo được một con số thấp mà không biết vì sao |
| Giới hạn công suất | NFR-09, độ ổn định | Brownout reset giữa buổi bảo vệ |
| Đồng bộ trạng thái hai chiều | **C4 — không có nó thì không có dữ liệu huấn luyện** | Mô hình học sở thích không thể tồn tại |
| Node 2 cục bộ hoá | C3, mức suy giảm L2/L3 | Mất mạng là mất tự động hoá |
| Sức khoẻ thiết bị trong telemetry | Cổng chất lượng G1.8, NFR-04/05 | Chương 4 lại có bảng "giả lập" như bản DACN |

Cột cuối cùng là cột đáng đọc nhất. Mỗi dòng ở đó là một sự cố đã được ngăn trước, và
trong báo cáo, **ngăn trước một sự cố có giá trị trình bày ngang với thêm một tính năng** —
miễn là giải thích được vì sao nó sẽ xảy ra nếu không làm gì.
