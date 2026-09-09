# Oi — Hệ thống chiếu sáng thích ứng ngữ cảnh

Hệ chiếu sáng trong nhà nghe được lời nói thường ngày, nhìn thấy người đang ở đâu và
quay về hướng nào, tự đưa ánh sáng đến đúng chỗ cần, và học dần để lần sau không phải
hỏi nữa.

**Đồ án tốt nghiệp** · Đoàn Văn Tuyền · 23020151 · Trường Đại học Công nghệ, ĐHQGHN
Kế thừa và mở rộng báo cáo Dự án Công nghệ.

---

## Luận điểm

Trợ lý giọng nói trong nhà bị kẹt giữa hai cực. Nhận dạng từ khoá trên vi điều khiển
phản hồi dưới 150 ms nhưng chỉ hiểu vài câu lệnh cứng. Mô hình ngôn ngữ hiểu được lời
nói tự nhiên nhưng mất vài giây trên phần cứng biên.

Đề tài giải mâu thuẫn bằng **kiến trúc AI hai lớp**: thiết bị tự trả lời phần lớn tình
huống trong tích tắc, và chỉ nhờ đến bộ não lớn hơn khi **tự nhận là chưa đủ hiểu**.

```
TẦNG 1 · THIẾT BỊ · PHẢN XẠ                    firmware/
   node1  ESP32-S3   từ khoá + LED + cảm biến       < 150 ms
   node2  ESP32      radar + đèn cổng, luật cục bộ
   node3  ESP32      đèn rọi pan/tilt bám người

         ▲ leo thang khi độ tin cậy < 0.75            ▼ lệnh

TẦNG 2 · HUB BIÊN · SUY LUẬN                    hub/
   Raspberry Pi 5 · lời nói→chữ · ý định · thị giác · học sở thích

         ▲ tuỳ chọn, KHÔNG nằm trên đường tới hạn      ▼

TẦNG 3 · ĐÁM MÂY                                truy cập từ xa · lưu vết
```

Mất Internet → tầng 2 vẫn đủ. Mất hub → tầng 1 vẫn đủ. Mất Wi-Fi → nút bấm vẫn đủ.

---

# Cấu trúc thư mục

Kho mã này chứa **bốn loại công việc rất khác nhau**: phần mềm nhúng chạy trên vi điều
khiển, dịch vụ chạy 24/7 trên máy chủ, việc huấn luyện mô hình chạy một lần trên laptop,
và các thí nghiệm đo đạc. Chúng có vòng đời khác nhau, ngôn ngữ khác nhau, và cách kiểm
thử khác nhau — nên mỗi loại có một thư mục gốc riêng.

```
AIoT_Smart_Lighting/
│
├── firmware/          ⚙  Mã chạy trên vi điều khiển ESP32
├── hub/               🖥  Dịch vụ chạy 24/7 trên Raspberry Pi 5
├── ml/                🧠  Huấn luyện mô hình — chạy trên laptop, một lần
├── eval/              📊  Thí nghiệm đo đạc cấp hệ thống
├── docs/              📄  Đặc tả, quyết định kiến trúc, báo cáo
├── tools/             🔧  Công cụ vận hành — nạp chứng chỉ, hiệu chuẩn
├── .github/workflows/ 🤖  Kiểm tra tự động mỗi lần push
│
├── README.md          File bạn đang đọc
├── .gitignore         Khối bí mật nằm ở ĐẦU file — cố ý, để đập vào mắt
└── .gitattributes     Chuẩn hoá xuống dòng, chấm dứt cảnh báo CRLF trên Windows
```

Bốn thư mục đầu tương ứng bốn câu hỏi khác nhau:

| Thư mục | Trả lời câu hỏi | Chạy ở đâu | Chạy khi nào |
|---|---|---|---|
| `firmware/` | Thiết bị phải làm gì? | ESP32 | Liên tục |
| `hub/` | Máy chủ phải làm gì? | Pi 5 | Liên tục |
| `ml/` | Mô hình học từ dữ liệu gì? | Laptop | Một lần, khi huấn luyện |
| `eval/` | Hệ thống có tốt không? | Laptop + hiện trường | Khi đo đạc |

---

## ⚙ `firmware/` — Mã chạy trên vi điều khiển

```
firmware/
│
├── common/                      ★ HỢP ĐỒNG DÙNG CHUNG
│   └── include/
│       ├── oi_protocol.h        Tên chủ đề MQTT + khoá JSON + các ngưỡng
│       └── oi_state.h           Struct trạng thái đèn + hằng số rào an toàn
│
├── node1_controller/            ESP32-S3 · giọng nói, LED, cảm biến, nút bấm
│   ├── platformio.ini           Cấu hình build — có `extra_configs = secrets.ini`
│   ├── partitions.csv           Bảng phân vùng: 2 slot OTA + vùng NVS cho chứng chỉ
│   ├── secrets.ini.example      MẪU — copy thành secrets.ini rồi điền
│   ├── include/Config.h         Chân cắm và tham số — KHÔNG có bí mật
│   ├── src/                     main.cpp và các task FreeRTOS
│   ├── lib/                     Thư viện đi kèm (mô hình Edge Impulse)
│   └── test/                    Kiểm thử chạy trên chính con chip
│
├── node2_gate/                  ESP32 · radar, đèn cổng, tự động hoá cục bộ
├── node3_spotlight/             ESP32 · đèn rọi pan/tilt  (giai đoạn 4)
```

### Vì sao có `common/`?

Ba node và cả hub đều phải **đồng ý với nhau** về tên chủ đề MQTT và tên khoá JSON.
Nếu mỗi chỗ tự gõ chuỗi `"oi/cmd/node1"` thì chỉ cần một lần gõ sai là node không nhận
được lệnh — và đó là loại lỗi tốn nửa ngày đi tìm vì không có gì báo lỗi cả, chỉ đơn
giản là không có gì xảy ra.

`common/` giải quyết bằng cách đặt **một nguồn sự thật duy nhất**:

- **`oi_protocol.h`** — mọi tên chủ đề, mọi khoá JSON, mọi ngưỡng của giao thức leo
  thang. Có bản song sinh bên Python là `hub/services/oi_common/protocol.py`, và có
  một bài kiểm thử tự động đọc thẳng file `.h` này rồi đối chiếu với bản Python —
  **lệch nhau là CI báo lỗi ngay**.

- **`oi_state.h`** — struct `OiLightState` và các hằng số rào an toàn. File này tồn tại
  để sửa một lỗi cụ thể của bản DACN: ba biến `String` toàn cục bị ghi từ cả hai nhân
  CPU mà không có khoá đồng bộ nào, có thể làm hỏng vùng nhớ heap và gây reset ngẫu
  nhiên sau nhiều giờ chạy.

### Vì sao ba dự án PlatformIO riêng, không gộp làm một?

Vì ba node dùng **bo mạch khác nhau** (ESP32-S3 với ESP32 thường), **thư viện khác
nhau**, và quan trọng nhất là cần **nạp độc lập** — sửa node 2 thì không phải nạp lại
node 1. Mỗi thư mục node là một dự án PlatformIO hoàn chỉnh, mở riêng được.

Chúng dùng chung `common/` qua dòng `-I ../common/include` trong `platformio.ini`.

### Bí mật đi vào firmware bằng đường nào?

Đây là phần **quan trọng nhất** của cấu trúc này, vì bản DACN từng để lộ khoá riêng AWS
lên một kho công khai. Nguyên nhân gốc không phải là bất cẩn một lần, mà là **kiến trúc
buộc phải bất cẩn**: khi khoá nằm trong file `.h` thì không có cách nào commit mã mà
không commit khoá.

```
  Wi-Fi + địa chỉ hub              Chứng chỉ TLS của mạng nhà
          │                                  │
  secrets.ini (gitignore)          Máy của bạn, ngoài kho mã
          │                                  │
  cờ -D lúc biên dịch              tools/provision/provision_node.py
          │                                  │
          ▼                                  ▼
     Config.h đọc được              Phân vùng NVS "oi_creds"
          │                                  │
          └──────────► FIRMWARE ◄────────────┘
```

Chứng chỉ **không** đi qua cờ biên dịch, vì cờ biên dịch nhúng chuỗi thẳng vào file
`.bin` — ai đọc được flash là lấy được khoá, và file `.bin` là thứ ta hay gửi cho người
khác nạp hộ hoặc đính kèm báo cáo. Để ở NVS thì khoá không lọt vào ảnh firmware.

> **Lần đầu mở dự án:** `cp secrets.ini.example secrets.ini` rồi điền. File
> `secrets.ini` đã nằm trong `.gitignore` nên không bao giờ bị commit nhầm.

---

## 🖥 `hub/` — Dịch vụ chạy trên Raspberry Pi 5

```
hub/
├── docker-compose.yml       Định nghĩa toàn bộ hệ — `docker compose up -d` là chạy
├── .env.example             MẪU — copy thành .env rồi điền
├── pyproject.toml           Phụ thuộc Python, chia nhóm theo dịch vụ
│
├── services/
│   ├── oi_common/           Thư viện dùng chung — protocol.py, client MQTT
│   ├── oi_state/            ★ Nguồn sự thật duy nhất về trạng thái
│   ├── oi_asr/              AI-2 · lời nói → chữ
│   ├── oi_agent/            AI-3 · ý định → lệnh cụ thể
│   ├── oi_vision/           AI-4, AI-5 · người ở đâu, đang làm gì
│   ├── oi_context/          Hợp nhất radar + thị giác + lux + giờ
│   ├── oi_policy/           AI-6 · học mức sáng ưa thích
│   └── oi_tts/              AI-8 · phản hồi bằng tiếng nói (mở rộng)
│
├── config/
│   ├── mosquitto/           Cấu hình broker MQTT nội bộ
│   └── nodered/             Luồng và bảng điều khiển
│
└── tests/
    └── test_protocol_parity.py   ★ Chống lệch hợp đồng giữa C và Python
```

### Vì sao chia thành 8 dịch vụ riêng thay vì một chương trình Python?

Đây là quyết định kiến trúc quan trọng nhất của phần hub, và lý do **không phải** là
"cho gọn". Lý do là:

> **Ranh giới giữa các dịch vụ chính là thiết bị thí nghiệm của đồ án.**

Chương đánh giá cần trả lời câu *"năng lực AI này có thật sự đóng góp gì không"*.
Cách trả lời là **thí nghiệm bóc tách**: tắt một năng lực đi rồi đo lại xem hệ thống
tệ đi bao nhiêu. Nếu tất cả nằm trong một chương trình, mỗi lần bóc tách phải sửa mã,
và mỗi lần sửa mã là một cơ hội làm hỏng thứ khác.

Vì các dịch vụ chỉ nói chuyện qua MQTT, mỗi biến thể thí nghiệm chỉ là **tắt một
container**:

```bash
docker compose stop vision      # biến thể −AI4/−AI5: hệ thống chỉ còn radar
docker compose stop policy      # biến thể −AI6: chỉ còn quy tắc nền
```

Không sửa một dòng mã nào. Đây là lý do `docs/05-evaluation.md` có 5 biến thể bóc tách
mà vẫn khả thi trong 4 ngày.

### `oi_state/` đặc biệt ở chỗ nào?

Nó là **nguồn sự thật duy nhất**, và cũng là nơi **sinh ra dữ liệu huấn luyện cho AI-6**.

Mỗi khi đèn đổi trạng thái — dù do giọng nói, nút bấm, web hay hệ thống tự quyết — dịch
vụ này ghi lại kèm trường `src` (nguồn lệnh) và ảnh chụp ngữ cảnh lúc đó. Sau này mô
hình học sở thích đọc chính bảng đó: một lệnh `BUTTON` đến ngay sau một lệnh `AGENT`
trong vòng 90 giây chính là **một lần người dùng sửa sai hệ thống** — tức một mẫu huấn
luyện quý.

Bản DACN không có dịch vụ này, và đó là lý do bảng điều khiển hiển thị sai khi bật đèn
bằng giọng nói.

---

## 🧠 `ml/` — Huấn luyện mô hình

```
ml/
├── kws/           AI-1 · nhận dạng từ khoá   → xuất ra thư viện cho node1
├── asr/           AI-2 · kịch bản đo, không huấn luyện
├── nlu/           AI-3 · prompt hệ thống, grammar, bộ 200 câu kiểm thử
├── vision/        AI-4, AI-5 · bộ phân loại hoạt động, tệp hiệu chuẩn vùng
├── policy/        AI-6 · kỹ thuật đặc trưng, huấn luyện, kiểm định ngược
├── notebooks/     Khám phá dữ liệu — nơi thử nghiệm lộn xộn được phép tồn tại
└── datasets/      ⚠ Nội dung KHÔNG commit — chỉ README và manifest
```

### Vì sao `ml/` tách khỏi `hub/`?

Vì chúng có **vòng đời hoàn toàn khác nhau**:

| | `ml/` | `hub/` |
|---|---|---|
| Chạy khi nào | Một lần, lúc huấn luyện | Liên tục 24/7 |
| Chạy ở đâu | Laptop (cần CPU mạnh) | Raspberry Pi 5 |
| Phụ thuộc | PyTorch, pandas, notebook | Chỉ thư viện suy luận |
| Sản phẩm | **File mô hình** | Hành vi của hệ thống |

`ml/` **sản xuất**, `hub/` **tiêu thụ**. Nếu trộn chung, Pi 5 sẽ phải cài cả bộ thư
viện huấn luyện nặng nề mà nó không bao giờ dùng đến.

### Vì sao `datasets/` không được commit?

Hai lý do, lý do đầu quan trọng hơn:

1. **Dữ liệu cá nhân.** Các bản ghi âm chứa giọng nói thật, video chứa hình ảnh bên
   trong nhà. Đưa lên kho công khai là vi phạm chính cam kết quyền riêng tư mà đề tài
   đặt ra ở chương 1.
2. Dung lượng lớn làm kho git phình vô ích.

Nhưng mỗi thư mục con **phải có `manifest.yaml` được commit**, ghi rõ: bao nhiêu mẫu,
bao nhiêu người nói, thu ngày nào, điều kiện gì, chia tập thế nào. Nhờ đó kết quả vẫn
**tái lập được** dù dữ liệu không đi kèm — đây là yêu cầu cơ bản của một báo cáo khoa học.

### Ba quy tắc chia tập — đọc trước khi huấn luyện bất cứ gì

Ba lỗi này làm kết quả đẹp giả tạo, và người phản biện sẽ hỏi đúng vào chúng:

| Mô hình | Phải chia theo | Nếu chia sai |
|---|---|---|
| Từ khoá | **Người nói** | Cùng một người ở cả train và test → độ chính xác cao ảo, sụp đổ khi gặp người lạ |
| Hoạt động | **Phiên quay** | Khung liền kề gần như trùng nhau → độ chính xác trên 95 % nhưng vô nghĩa |
| Sở thích | **Thời gian** | Xáo trộn ngẫu nhiên = để tương lai rò rỉ vào quá khứ |

---

## 📊 `eval/` — Thí nghiệm cấp hệ thống

```
eval/
├── protocols/     Quy trình từng thí nghiệm — làm gì, đo gì, bao nhiêu lần
├── scripts/       Mã tự động hoá việc đo
└── results/       Số liệu — chỉ file tổng hợp được commit
```

### Vì sao `eval/` tách khỏi `ml/`?

Vì chúng trả lời **hai câu hỏi khác nhau**, và việc lẫn lộn hai câu này là lỗi phổ
biến nhất trong đồ án AIoT:

| | `ml/` đo | `eval/` đo |
|---|---|---|
| Câu hỏi | *"Mô hình này tốt không?"* | *"Hệ thống này đáng dùng không?"* |
| Ví dụ | Độ chính xác nhận từ khoá 94 % | Tỉ lệ người dùng phải chỉnh tay giảm 40 % |
| Đơn vị | Mô hình đơn lẻ | Toàn hệ thống, đầu-cuối |

Một mô hình có độ chính xác 95 % vẫn có thể tạo ra một hệ thống khó chịu không ai muốn
dùng. Ngược lại, một mô hình 85 % đặt đúng chỗ trong kiến trúc tốt có thể cho trải
nghiệm mượt mà. Chỉ số trong `eval/` là thứ bắt được sự khác biệt đó — và đó cũng là
thứ hội đồng thật sự quan tâm.

Giữ hai thư mục riêng là để **giữ sự phân biệt này rõ ràng trong đầu bạn**, và nó phản
chiếu đúng cấu trúc chương 4 của báo cáo.

---

## 📄 `docs/` — Tài liệu

```
docs/
├── 00-overview.md        Tính năng và chức năng, chưa nói công nghệ
├── 01-requirements.md    Yêu cầu có mã số: FR-A1, NFR-03…
├── 02-architecture.md    Ba tầng, bảng task FreeRTOS, các dịch vụ
├── 03-ai-models.md       Bản đăng ký 8 mô hình: ngân sách, chỉ số
├── 04-protocols.md       Chủ đề MQTT, lược đồ JSON, giao thức leo thang
├── 05-evaluation.md      Chỉ số hệ thống, thí nghiệm A và B
├── 06-hardware.md        Linh kiện, sơ đồ chân, đấu nối
├── 07-security.md        ✅ Xử lý bí mật, nhật ký sự cố rò rỉ
├── 08-soak-test.md       ✅ Quy trình chạy liên tục 72 giờ (G1.8)
├── 09-thay-doi-kien-truc.md  ✅ Vì sao kiến trúc thay đổi, kèm cách kiểm chứng
│
├── adr/                  ★ Quyết định kiến trúc kèm LÝ DO
│   ├── 0001-kien-truc-ai-hai-lop.md
│   ├── 0002-hoc-phan-du-cho-so-thich.md
│   ├── 0003-broker-noi-bo-cloud-tuy-chon.md
│   └── 0004-bo-phu-thuoc-dam-may.md
│
├── diagrams/             Sơ đồ nguồn
└── thesis/               Bản báo cáo và hình ảnh
```

### `adr/` là gì và vì sao đáng bỏ công?

ADR = *Architecture Decision Record* — bản ghi quyết định kiến trúc. Mỗi file ghi theo
bốn phần: **bối cảnh → quyết định → hệ quả → phương án đã loại bỏ**.

Mục đích không phải thủ tục hành chính. Mục đích là: **sáu tháng nữa, khi hội đồng hỏi
"vì sao em chọn cách này mà không chọn cách kia", bạn có sẵn câu trả lời đã viết ra
lúc còn nhớ rõ mọi lý lẽ.**

Phần *"phương án đã loại bỏ"* là phần giá trị nhất — nó cho thấy bạn đã **cân nhắc**
chứ không phải làm bừa. Đó chính là thứ phân biệt một đồ án tốt nghiệp với một bài tập lớn.

### Quy ước đánh mã số

Yêu cầu có mã (`FR-A1`, `NFR-03`, `S4`) để **mã nguồn và kiểm thử tham chiếu ngược lại
được**. Ví dụ trong comment: `// hiện thực FR-D3: luật cục bộ không phụ thuộc mạng`.
Khi viết báo cáo, bạn tra ngược từ mã số ra được đúng đoạn mã và đúng bài kiểm thử.

---

## 🔧 `tools/` — Công cụ vận hành

```
tools/
├── provision/      Nạp Wi-Fi và chứng chỉ vào NVS của thiết bị
├── calibration/    Hiệu chuẩn camera ↔ góc servo cho đèn rọi
└── scripts/        Thu dữ liệu, đo độ trễ, tiện ích lặt vặt
```

Đây là mã **không chạy trong hệ thống**, chỉ dùng lúc lắp đặt và bảo trì. Tách riêng để
không lẫn với mã sản xuất.

Ghi chú về hiệu chuẩn đèn rọi: cách làm là rọi đèn thủ công vào ~12 điểm trên sàn, ghi
lại từng cặp (toạ độ ảnh ↔ góc servo), rồi nội suy — **không dựng mô hình hình học 3D
của căn phòng**. Nửa buổi là xong và ổn định hơn nhiều, vì nó tự hấp thụ mọi sai lệch
lắp đặt thực tế.

---

## 🤖 `.github/workflows/` — Kiểm tra tự động

`ci.yml` chạy ba nhóm kiểm tra mỗi lần push:

1. **Quét bí mật** — chạy **trước** mọi thứ khác, và không bao giờ được tắt.
   `gitleaks` quét toàn bộ lịch sử, cộng thêm hai lớp chặn thủ công cho các mẫu đã
   biết và cho việc file `secrets.ini` bị commit nhầm.
2. **Biên dịch firmware** — cả ba node, để `main` luôn ở trạng thái nạp được.
3. **Kiểm thử hub** — trong đó quan trọng nhất là `test_protocol_parity.py`.

Nhóm 1 tồn tại vì sự cố rò rỉ đã xảy ra một lần. Nó ở đây để điều đó **không lặp lại**.

---

# Bắt đầu

### Firmware

```bash
cd firmware/node1_controller
cp secrets.ini.example secrets.ini      # rồi điền Wi-Fi và địa chỉ hub
pio run -t upload
pio device monitor
```

Chứng chỉ TLS nạp riêng, không qua mã nguồn (chỉ cần sau khi đã dựng hub ở G2.1 —
trước đó node chạy đầy đủ qua cổng 1883 trong mạng nhà):

```bash
python tools/provision/provision_node.py --port COM5 --node node1     --ca certs/oi-ca.pem --cert certs/node1.crt --key certs/node1.key
```

### Hub

```bash
cd hub
cp .env.example .env                    # rồi điền
docker compose up -d
pytest                                  # kiểm tra hợp đồng C ↔ Python còn khớp
```

---

# Thêm file mới thì để đâu?

| Bạn muốn thêm… | Để vào |
|---|---|
| Một task FreeRTOS mới cho node 1 | `firmware/node1_controller/src/` |
| Một chân cắm hoặc tham số mới | `firmware/node1_controller/include/Config.h` |
| Một chủ đề MQTT hoặc khoá JSON mới | `firmware/common/include/oi_protocol.h` **và** `hub/services/oi_common/protocol.py` — cả hai, cùng một commit |
| Một năng lực AI mới trên hub | Thư mục mới trong `hub/services/` + một mục trong `docker-compose.yml` |
| Mã huấn luyện một mô hình | `ml/<tên-mô-hình>/` |
| Bản ghi âm, video, nhật ký | `ml/datasets/…` — không commit, nhưng cập nhật `manifest.yaml` |
| Kịch bản đo một chỉ số hệ thống | `eval/protocols/` + `eval/scripts/` |
| Một quyết định kiến trúc | `docs/adr/000N-<mô-tả-ngắn>.md` |
| Script chạy một lần lúc lắp đặt | `tools/` |
| Mật khẩu, khoá, chứng chỉ bất kỳ | **Không chỗ nào trong kho này.** `secrets.ini` hoặc NVS |

---

# Bảo mật

Bí mật **không bao giờ** được commit. CI chạy `gitleaks` trên toàn bộ lịch sử và chặn
merge nếu phát hiện khoá, chứng chỉ, hay file `secrets.ini` / `.env`.

Trước mỗi lần push, chạy:

```bash
bash tools/scripts/check-secrets.sh
```

Script này là **nơi duy nhất** định nghĩa mẫu quét — CI gọi đúng nó, nên kiểm tra
thủ công và kiểm tra tự động không bao giờ lệch nhau.

> ⚠ Dự án đã từng để lộ khoá riêng AWS trong phiên bản trước. Xem
> [`docs/07-security.md`](docs/07-security.md) để biết các bước thu hồi bắt buộc và vì
> sao kho mã này bắt đầu bằng một lịch sử git sạch.

---

# Bảy năng lực AI

| Mã | Năng lực | Chạy ở | Chứng minh bằng |
|---|---|---|---|
| AI‑1 | Nghe ra tên gọi và lệnh ngắn | Thiết bị | F1 từng lớp · số lần thức nhầm / 24 h |
| AI‑2 | Chuyển lời nói tiếng Việt thành chữ | Hub | Tỉ lệ chữ sai theo khoảng cách × mức ồn |
| AI‑3 | Suy ra ý định, phân rã thành thao tác | Hub | So với dò từ khoá, trên nhóm câu tự do |
| AI‑4 | Người ở đâu, quay hướng nào | Hub | Độ chính xác gán vùng · sai số hướng |
| AI‑5 | Người đang làm gì | Hub | Độ chính xác, và phép thử tháo bỏ |
| AI‑6 | Học mức sáng ưa thích | Hub | **Tỉ lệ chỉnh tay theo tuần** |
| AI‑7 | Biết khi nào mình chưa chắc | Cả hai | Tỉ lệ leo thang × độ trễ × tỉ lệ đúng |

AI‑6 là năng lực duy nhất khiến hệ thống **không cần được ra lệnh nữa** — ranh giới
thật giữa "nhà điều khiển bằng giọng nói" và "nhà thông minh".

---

# Trạng thái hiện tại

| Giai đoạn | Nội dung | Tuần | Trạng thái |
|---|---|---|---|
| G0 | Nền móng và xử lý sự cố bảo mật | 0 | 🔶 Đang làm |
| G1 | Trả nợ kỹ thuật firmware | 1–3 | ⬜ Chưa |
| G1b | Đo đường cơ sở | 3–4 | ⬜ Chưa |
| G2 | Lớp suy luận | 5–10 | ⬜ Chưa |
| G3 | Ngữ cảnh và học sở thích | 11–14 | ⬜ Chưa |
| G4 | Đèn rọi bám người *(cắt được)* | 15–16 | ⬜ Chưa |
| G5 | Đo đạc và viết báo cáo | 17–18 | ⬜ Chưa |

> **Ghi chú dọn dẹp:** hai thư mục `Node1_EdgeAI_Controller/` và
> `Node2_Radar_GateLight/` là cấu trúc cũ, đã được sao chép sang `firmware/` và **cần
> xoá** sau khi đối chiếu xong. Chúng vẫn chứa khoá bí mật trong `include/Config.h`,
> `src/Secrets.cpp` và trong các file build ở `.pio/`.
