# Wearable Vital Sign Monitor
### IoT Development Competition — TETI UGM Bootcamp 2026
### Biomedical Application Category

---

## 1. Gambaran Umum Karya

### Latar Belakang

Pemantauan tanda vital pasien secara berkelanjutan merupakan kebutuhan kritis di fasilitas kesehatan. Tenaga medis perlu mengetahui perubahan kondisi pasien secara real-time agar dapat memberikan respons cepat terhadap situasi darurat. Namun, perangkat monitoring konvensional umumnya mahal, terbatas pada lingkungan rumah sakit, dan tidak mendukung pemantauan jarak jauh.

Perkembangan teknologi Internet of Things (IoT) membuka peluang untuk membangun sistem monitoring yang terjangkau, portabel, dan dapat diakses dari mana saja. Dengan memanfaatkan mikrokontroler ESP32, protokol MQTT, dan dashboard Node-RED, sistem monitoring tanda vital dapat dibangun dengan biaya rendah tanpa mengorbankan fungsionalitas.

Proyek ini dikembangkan sebagai bagian dari **IoT Development Competition TETI UGM Bootcamp 2026** dalam kategori **Biomedical Application**.

### Tujuan

1. Men-simulasikan data SpO₂, heart rate, dan suhu tubuh menggunakan dataset CSV Kaggle pada ESP32
2. Mengirimkan data ke broker MQTT secara real-time setiap 1 detik
3. Menyajikan informasi melalui dashboard Node-RED dengan gauge, chart tren, tabel alarm, dan notifikasi
4. Mengimplementasikan sistem alert tiga level: **Normal**, **Warning**, dan **Critical** dengan indikator LED dan buzzer
5. Menyediakan mekanisme data logging fallback saat MQTT offline + replay otomatis setelah reconnect
6. Menampilkan response command ESP32 pada dashboard (Command Response Display)

### Manfaat

1. **Monitoring Jarak Jauh** — Tenaga medis dapat memantau kondisi pasien dari mana saja melalui dashboard web
2. **Deteksi Dini** — Sistem alert tiga level memberikan peringatan dini saat tanda vital memasuki zona Warning atau Critical
3. **Respons Cepat** — Tombol kontrol dari dashboard memungkinkan tindakan jarak jauh (ACK alarm, reboot, cek status)
4. **Cek Threshold** — Tombol THRESHOLDS pada dashboard dapat menampilkan nilai threshold yang sedang aktif
5. **Ketahanan Data** — Data tidak hilang saat koneksi MQTT terputus berkat logging fallback ke SPIFFS + replay otomatis
6. **Biaya Rendah** — Menggunakan ESP32 dan komponen elektronik dasar, jauh lebih murah dibanding monitor pasien komersial

---

## 2. Diagram Skematik

### Wiring Diagram

| Komponen | Pin ESP32 | Keterangan |
|---|---|---|
| **OLED SSD1306** | VCC → 3V3, GND → GND, SDA → GPIO 21, SCL → GPIO 22 | I2C address 0x3C |
| **LED Merah (Critical)** | Anode → GPIO 25 (via 220Ω), Cathode → GND | Indikator status Critical |
| **LED Kuning (Warning)** | Anode → GPIO 33 (via 220Ω), Cathode → GND | Indikator status Warning |
| **LED Hijau (Normal)** | Anode → GPIO 26 (via 220Ω), Cathode → GND | Indikator status Normal |
| **Buzzer Pasif** | (+) → GPIO 27, (-) → GND | Alarm audio, non-blocking PWM |

### Diagram Blok Sistem

```
┌─────────────────────────────────────────────────────────────────┐
│                        DEVICE LAYER                             │
│                                                                 │
│    vitals.csv (SPIFFS)                                          │
│         │                                                       │
│         ▼                                                       │
│    ESP32 DevKit C v4                                            │
│    ┌─────────────────────────────────────┐                      │
│    │  readCsvRow() → applyMovingAverage()│                      │
│    │  → determinePatientStatus()         │                      │
│    │  → handleActuators()                │                      │
│    │  → updateOled()                     │                      │
│    │  → mqttPublish*()                   │                      │
│    └─────────────────────────────────────┘                      │
│         │                                                       │
│    ┌────┼───────────┬───────────┬───────────┐                   │
│    ▼    ▼           ▼           ▼           ▼                   │
│  OLED  LED(R/Y/G)  Buzzer      MQTT       SPIFFS                │
│                               Client  (data log fallback)       │
└──────────────────────────┬──────────────────────────────────────┘
                           │ WiFi / MQTT
                           ▼
┌────────────────────────────────────────────────────────────────┐
│                      BROKER LAYER                              │
│                   broker.emqx.io : 1883                        │
│     Topics: hospital/patient/001/{vitals,alarm,state,          │
│                 presence,cmd,feedback}                         │
└──────────────────────────┬─────────────────────────────────────┘
                           │ MQTT Subscribe
                           ▼
┌────────────────────────────────────────────────────────────────┐
│                    DASHBOARD LAYER                             │
│   Node-RED Dashboard (http://localhost:1880/ui)                │
│   ┌──────────┐ ┌──────────┐ ┌──────────┐ ┌──────────────┐      │
│   │ SpO₂     │ │ HR       │ │ Temp     │ │ Status       │      │
│   │ Gauge    │ │ Gauge    │ │ Gauge    │ │ Banner       │      │ 
│   └──────────┘ └──────────┘ └──────────┘ └──────────────┘      │
│   ┌──────────────────────────────┐ ┌──────────────────────┐    │
│   │ Vitals Timeline Chart        │ │ Alarm Log Table      │    │
│   └──────────────────────────────┘ └──────────────────────┘    │
│   ┌──────────────────────┐ ┌────────────────────────────┐      │
│   │ Alarm Notification   │ │ Connection Status          │      │
│   └──────────────────────┘ └────────────────────────────┘      │
│   ┌────────────────────────────────────────────────────┐       │
│   │ Controls: PING | GET STATE | REBOOT | THRESHOLDS   │       │
│   │          | ACK ALARM                               │       │
│   └────────────────────────────────────────────────────┘       │
│   ┌────────────────────────────────────────────────────┐       │
│   │ Command Response Display                           │       │
│   └────────────────────────────────────────────────────┘       │
└─────────────────────────────────────────────────────────────   ┘
```

---

## 3. Algoritma / Flowchart Sistem

### Flowchart Utama (Main Loop)

```
                                    ┌─────────┐
                                    │  START  │
                                    └────┬────┘
                                         │
                                    ┌────▼────┐
                                    │  setup()│
                                    └────┬────┘
                                         │
                    ┌────────────────────┼────────────────────┐
                    │                    │                    │
              ┌─────▼──────┐      ┌──────▼──────┐     ┌──────▼──────┐
              │ Init SPIFFS│      │ Init OLED   │     │ Init GPIO   │
              │ mount fs   │      │ splash scr  │     │ LED + Buzzer│
              └─────┬──────┘      └──────┬──────┘     └──────┬──────┘
                    │                    │                   │
              ┌─────▼──────┐      ┌──────▼──────┐     ┌──────▼──────┐
              │ Init WiFi  │      │ Init MQTT   │     │ Init OTA    │
              │ WiFiManager│      │ connect brkr│     │ + Watchdog  │
              └─────┬──────┘      └──────┬──────┘     └──────┬──────┘
                    │                    │                   │
                    └──────────┬─────────┴─────────┬─────────┘
                               │                   │
                          ┌────▼────┐         ┌────▼────┐
                          │  loop() │◄────────│ delay   │
                          └────┬────┘         │ 1000ms  │
                               │              └─────────┘
                    ┌──────────┼──────────┐
                    │          │          │
              ┌─────▼────┐ ┌──▼───┐ ┌────▼─────┐
              │mqttLoop()│ │Read  │ │Apply     │
              │maintain  │ │CSV   │ │Moving    │
              │connection│ │Row   │ │Average   │
              └─────┬────┘ └── ┬──┘ └─── ─┬────┘
                    │          │          │
                    └────┬─────┴─────┬────┘
                         │           │
                    ┌────▼────┐ ┌────▼────┐
                    │Determine│ │Handle   │
                    │Patient  │ │Actuators│
                    │Status   │ │LED+Buz  │
                    └────┬────┘ └────┬────┘
                         │           │
                    ┌────▼────┐ ┌────▼────┐
                    │Update   │ │MQTT     │
                    │OLED     │ │Publish  │
                    └────┬────┘ └────┬────┘
                         │           │
                         └─────┬─────┘
                               │
                          ┌────▼────┐
                          │ Kembali │
                          │ ke loop │
                          └─────────┘
```

### State Diagram Status Pasien

```
                    ┌──────────┐
                    │  NORMAL  │
                    └────┬─────┘
                         │
              ┌──────────┼──────────┐
              │          │          │
         ┌────▼────┐ ┌──▼───┐ ┌───▼────┐
         │SpO₂ <   │ │HR di │ │Suhu di │
         │  94%?   │ │luar  │ │luar    │
         └────┬────┘ │60-100│ │36-37.5?│
              │      └──┬───┘ └───┬────┘
              └────┬────┴────┬────┘
                   │         │
              ┌────▼────┐    │ (2 siklus berturut-turut)
              │ WARNING │◄───┘
              └────┬─────┘
                   │
              ┌────┼────┐
              │    │    │
         ┌────▼┐ ┌─▼──┐ ┌▼────┐
         │SpO₂ │ │HR  │ │Suhu │
         │<90%?│ │<50 │ │>38.5│
         │     │ │atau│ │atau │
         │     │ │>120│ │<35? │
         └──┬──┘ └──┬─┘ └──┬──┘
            └───┬───┴───┬──┘
                │       │ (2 siklus berturut-turut)
           ┌────▼────┐  │
           │ CRITICAL│◄─┘
           └─────────┘
```

### Algoritma Command Handler (MQTT Callback)

```
Command diterima di topic cmd
         │
    ┌────┴────────────────────────────────────────── ┐
    │                                                │
    ├─ "PING"         → publish "online" ke presence │
    ├─ "GET_STATE"    → publish state ke state       │
    ├─ "REBOOT"       → set flag reboot (1 detik)    │
    ├─ "GET_THRESHOLDS" → publish JSON ke feedback   │
    ├─ "SET_THRESHOLD_*" → update NVS + threshold    │
    ├─ "ACK_ALARM"    → silent buzzer, reply feedback│
    └─ default        → ignore                       │
```

---

## 4. Deskripsi Sistem

### 4.1 Perangkat Keras (ESP32)

Sistem menggunakan **ESP32 DevKit C v4** sebagai mikrokontroler utama yang terintegrasi dengan:

- **OLED SSD1306 0.96"** (I2C) — Menampilkan nilai SpO₂, Heart Rate, Suhu Tubuh, status pasien, dan indikator koneksi MQTT secara lokal. OLED memiliki fitur sleep otomatis setelah 30 detik idle untuk menghemat daya.
- **3 LED indikator** — LED Hijau (Normal, GPIO 26), LED Kuning (Warning, GPIO 33), LED Merah (Critical, GPIO 25). Masing-masing dilindungi resistor 220Ω.
- **Buzzer Pasif** (GPIO 27) — Menghasilkan bunyi alarm non-blocking dengan frekuensi berbeda: 800Hz setiap 2 detik untuk Warning, 1200Hz setiap 600ms untuk Critical. Buzzer dapat di-silent dari dashboard melalui perintah ACK_ALARM.

### 4.2 Firmware ESP32

Firmware ditulis dalam bahasa C++ menggunakan Arduino framework dengan struktur modular:

| Modul | Fungsi |
|---|---|
| **Wokwi.ino** | Program utama: setup (inisialisasi semua komponen) dan loop (alur utama setiap 1 detik) |
| **Config.h / Config.cpp** | Semua konstanta sistem: WiFi, MQTT, pin GPIO, threshold, timing. Threshold bersifat non-const agar dapat diubah via MQTT dan disimpan ke NVS |
| **MQTT_handler.h / MQTT_handler.cpp** | Manajemen koneksi WiFi + MQTT, callback command, queue response, NVS persistence |

**Fitur firmware:**
- **CSV Playback**: Membaca dataset vital sign dari SPIFFS (`vitals.csv`), 1 baris per detik, loop otomatis saat EOF
- **Moving Average Filter**: Rata-rata 5 sampel terakhir untuk transisi status yang lebih halus dan mengurangi noise
- **Three-level Alert**: Evaluasi threshold 2-level (Warning/Critical) per parameter dengan prioritas Critical > Warning > Normal
- **Hysteresis**: Status berubah hanya setelah 2 siklus berturut-turut memenuhi kondisi yang sama, mencegah flapping
- **Data Logging Fallback**: Saat MQTT offline, data disimpan ke SPIFFS (`data_log.csv`) dengan auto-rotation 500 baris. Setelah reconnect, semua data ter-replay otomatis
- **NVS Persistence**: Threshold tersimpan di Non-Volatile Storage, tetap bertahan setelah reboot (dapat diubah via MQTT client eksternal)
- **Non-blocking MQTT Retry**: Reconnect tidak memblokir loop utama, retry setiap 30 detik
- **Watchdog Timer**: Reset otomatis jika loop hang lebih dari 15 detik
- **OLED Sleep**: Matikan OLED setelah 30 detik tidak ada perubahan data
- **OTA Update**: Update firmware over-the-air tanpa kabel USB
- **Unique Client ID**: Client ID digenerate dari MAC address (`esp32_patient_XXXXXXXX`) untuk menghindari konflik broker

### 4.3 Protokol MQTT

Sistem menggunakan **broker.emqx.io** sebagai broker MQTT publik. Seluruh komunikasi menggunakan topik dengan struktur:

```
hospital/patient/001/
├── vitals      ← Data sensor setiap 1 detik (JSON)
├── alarm       ← Event saat transisi Warning / Critical (JSON)
├── state       ← Retained state pasien (string: Normal/Warning/Critical)
├── presence    ← LWT online/offline + PING response
├── cmd         ← Command masuk (subscribe)
└── feedback    ← Response command (GET_THRESHOLDS, ACK_ALARM)
```

**Command yang didukung** (dikirim ke topic `cmd`, case-insensitive):

| Command | Fungsi | Response |
|---|---|---|---|
| `PING` | Cek apakah ESP32 masih hidup | `"online"` ke topic `presence` |
| `GET_STATE` | Minta status pasien terkini | State terakhir ke topic `state` |
| `GET_THRESHOLDS` | Minta semua nilai threshold | JSON threshold ke topic `feedback` |
| `ACK_ALARM` | Silent buzzer tanpa mengubah status | `{"status":"ok"}` ke topic `feedback` |
| `REBOOT` | Restart ESP32 | ESP32 reboot dalam 1 detik |

> **Catatan:** Firmware ESP32 juga mendukung perintah `SET_THRESHOLD_*` untuk mengubah threshold via MQTT client eksternal, namun belum tersedia tombolnya di dashboard.

### 4.4 Dashboard Node-RED

Dashboard diakses melalui `http://localhost:1880/ui` dengan layout:

**Group: Patient Status**
- Status Banner (color-coded): Hijau (Normal), Kuning (Warning), Merah (Critical)
- SpO₂ Gauge (rentang 80-100%, segmen 90/94)
- Heart Rate Gauge (rentang 30-160 bpm, segmen 50/60/100/120)
- Temperature Gauge (rentang 35-41°C, segmen 37.5/38.5)
- Vitals Timeline Chart (line chart 3 series, window 300 detik)

**Group: Alarms & Events**
- Alarm Log Table (ui_table dengan kolom Time, Alarm, SpO₂, HR, Temp)
- Alarm Notification Bar (ui_template dengan background color: hijau/oranye/merah)
- Device Connection Status (ui_template: "Device Connection online/offline" dengan warna)

**Group: Controls**
- 5 Tombol Kontrol: PING (biru), GET STATE (hijau), REBOOT (merah), THRESHOLDS (ungu), ACK ALARM (oranye)
- Command Response Display (ui_template dengan `<pre>` box hitam menampilkan response ESP32)

### 4.5 Threshold Status Pasien

Sistem menggunakan dua level threshold per parameter dengan evaluasi prioritas Critical terlebih dahulu:

| Parameter | Normal | Warning | Critical |
|---|---|---|---|
| **SpO₂** | ≥ 94% | 90% - 93% | < 90% |
| **Heart Rate** | 60 - 100 bpm | 50-59 atau 101-120 bpm | < 50 atau > 120 bpm |
| **Suhu Tubuh** | 36.0°C - 37.5°C | 37.6-38.5°C (demam) atau 35.0-36.0°C (hipotermia) | > 38.5°C (demam) atau < 35.0°C (hipotermia) |

Jika lebih dari satu parameter memenuhi kondisi berbeda, sistem menampilkan status tertinggi (Critical > Warning > Normal).

## 5. Link Repository

**GitHub:** [https://github.com/HyoSeop1488/Wearable-Vital-Sign-Monitor](https://github.com/HyoSeop1488/Wearable-Vital-Sign-Monitor)

Repository ini berisi seluruh source code proyek:
- **Wokwi/** — Firmware ESP32 (Wokwi.ino, Config.h/cpp, MQTT_handler.h/cpp, diagram.json, vitals.csv)
- **Node-Red/** — Flow dashboard (flows.json)
- **Presentation.md** — Dokumen presentasi ini

---

## 6. Link Documentation

**Google Drive:** [https://drive.google.com/drive/folders/105Ej6OU-wZalZIZSvadgsUb7nOv8lq6x](https://drive.google.com/drive/folders/105Ej6OU-wZalZIZSvadgsUb7nOv8lq6x)

Folder ini berisi dokumentasi visual proyek:
- **Demo_project.mp4** — Video demo Wearable Vital Sign Monitor
- **Diagram.png** — Diagram arsitektur sistem
- **Diagram-Custom_Chip.png** — Diagram custom chip MAX30102 + MLX90614
- **Dashboard_Node-Red.png** — Tampilan dashboard Node-RED
- **flow_Node-Red.png** — Tampilan flow Node-RED

---

## 📌 Kesimpulan

Proyek **Wearable Vital Sign Monitor** berhasil mengimplementasikan pipeline IoT end-to-end yang lengkap: dataset CSV → ESP32 → MQTT Broker → Node-RED Dashboard. Sistem mendukung data logging fallback, replay otomatis, dan three-level alert dengan buzzer yang dapat di-ACK dari jarak jauh melalui tombol kontrol dashboard.

Arsitektur modular (Config / MQTT_handler / Wokwi.ino) memastikan maintainability dan kemudahan ekspansi ke sensor nyata, database time-series, atau dashboard yang lebih canggih tanpa mengubah firmware secara fundamental.

---

<div align="center">

**IoT Development Competition — TETI UGM Bootcamp 2026**

*Biomedical Application Category*
</div>
