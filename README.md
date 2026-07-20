# 🩺 Wearable Vital Sign Monitor
### IoT Development Competition — TETI UGM Bootcamp 2026

> Sistem monitoring tanda vital berbasis ESP32 yang membaca dataset CSV, memprosesnya secara real-time, dan mengirimkan data **SpO₂**, **Heart Rate**, dan **Suhu Tubuh** ke dashboard Node-RED melalui protokol MQTT.

![Platform](https://img.shields.io/badge/Platform-ESP32-blue)
![Protocol](https://img.shields.io/badge/Protocol-MQTT-orange)
![Dashboard](https://img.shields.io/badge/Dashboard-Node--RED-red)
![Simulator](https://img.shields.io/badge/Simulator-Wokwi-green)
![Topic](https://img.shields.io/badge/Topic-Biomedical-purple)

---

## 📋 Daftar Isi

- [Latar Belakang](#-latar-belakang)
- [Tujuan](#-tujuan)
- [Fitur Utama](#-fitur-utama)
- [Arsitektur Sistem](#-arsitektur-sistem)
- [Komponen yang Digunakan](#-komponen-yang-digunakan)
- [Struktur Folder Proyek](#-struktur-folder-proyek)
- [Penjelasan File](#-penjelasan-file)
- [MQTT Topic & Payload](#-mqtt-topic--payload)
- [MQTT Command Set](#-mqtt-command-set)
- [Threshold Status Pasien](#-threshold-status-pasien)
- [Cara Menjalankan Simulasi](#-cara-menjalankan-simulasi)
- [Cara Import Flow Node-RED](#-cara-import-flow-node-red)
- [Demo Scenario](#-demo-scenario)
- [Screenshots](#-screenshots)
- [Hasil yang Diharapkan saat Demo](#-hasil-yang-diharapkan-saat-demo)
- [Future Enhancement](#-future-enhancement)
- [Kesimpulan](#-kesimpulan)

---

## 🏥 Latar Belakang

Pemantauan tanda vital pasien secara berkelanjutan merupakan kebutuhan kritis di fasilitas kesehatan. Keterbatasan tenaga medis dan perangkat monitoring konvensional yang mahal mendorong kebutuhan akan solusi IoT yang terjangkau, portabel, dan dapat dipantau dari jarak jauh.

Proyek ini dikembangkan sebagai bagian dari **IoT Development Competition TETI UGM Bootcamp 2026** dalam kategori **Biomedical Application**. Sistem dirancang dengan arsitektur MVP (Minimum Viable Product) yang berfokus pada pipeline data end-to-end: dari dataset CSV hingga visualisasi dashboard.

---

## 🎯 Tujuan

- Men-simulasikan data SpO₂, heart rate, dan suhu tubuh menggunakan dataset CSV Kaggle
- Mengirimkan data ke broker MQTT menggunakan protokol MQTT
- Menyajikan informasi melalui dashboard Node-RED dengan gauge, chart tren, dan tabel alarm
- Mengimplementasikan sistem alert tiga level: **Normal**, **Warning**, dan **Critical**
- Menyediakan mekanisme data logging fallback saat MQTT offline + replay otomatis
- Mendukung konfigurasi threshold jarak jauh via MQTT commands

---

## ✨ Fitur Utama

| Fitur | Deskripsi |
|---|---|
| 📡 **CSV Dataset Playback** | Membaca dataset vital sign dari SPIFFS, 1 baris/detik, loop otomatis |
| 🚨 **Three-level Alert** | Status Normal / Warning / Critical dengan LED indikator dan buzzer |
| 🖥️ **OLED Display** | Tampilan lokal nilai sensor, status pasien, dan indikator koneksi MQTT |
| 📊 **Node-RED Dashboard** | Gauge, line chart, status banner, dan alarm log table |
| 💾 **Data Logging Fallback** | Data tersimpan ke SPIFFS saat MQTT offline, replay otomatis setelah reconnect |
| 🧮 **Moving Average Filter** | Filter 5 sampel untuk transisi status yang lebih halus |
| 🔁 **Auto Reconnect** | WiFiManager dengan fallback credentials + auto-reconnect MQTT |
| 🔧 **Remote Threshold Config** | Ubah threshold Warning/Critical dari dashboard via MQTT |
| 🔔 **ACK Alarm** | Silent buzzer dari dashboard tanpa mengubah status |
| 🔐 **Unique Client ID** | Client ID digenerate dari MAC address untuk menghindari konflik broker |
| 📝 **NVS Persistence** | Threshold tersimpan di NVS, tetap bertahan setelah reboot |
| 🥶 **Hypothermia Detection** | Deteksi suhu tubuh rendah dengan threshold Warning/Critical sendiri |
| 🔄 **OTA Update** | Update firmware over-the-air tanpa kabel USB |
| ⏱️ **Watchdog Timer** | Reset otomatis jika loop hang >15 detik |
| 💤 **OLED Sleep** | Matikan OLED setelah 30 detik idle (timer reset tiap data read) |
| 🛑 **Non-blocking MQTT Retry** | Reconnect non-blocking, retry tiap 30 detik, tidak ganggu loop CSV |
| 📜 **Wokwi-compat Serial** | Output `\r\n` + single-line log — rapi di terminal Wokwi |

---

## 🏗️ Arsitektur Sistem

```
┌─────────────────────────────────────────────────────────────────┐
│                        DEVICE LAYER                             │
│                                                                 │
│    vitals.csv                                                   │
│           │                                                     │
│           ▼                                                     │
│    SPIFFS → Read row → Parse CSV → Moving Average               │
│           │                              │                      │
│           ▼                              ▼                      │
│    OLED Display               determinePatientStatus()          │
│    LED (R/Y/G)                       │                          │
│    Buzzer Alarm                      ▼                          │
│                              Normal / Warning / Critical        │
│                              │                                  │
│                              ▼                                  │
│    MQTT Publish: vitals / alarm / state / presence / feedback   │
│    MQTT Subscribe: cmd (PING, REBOOT, GET_STATE,                │
│                    GET_THRESHOLDS, SET_THRESHOLD_*, ACK_ALARM)  │
└──────────────────────────┬──────────────────────────────────────┘
                           │ WiFi
                           │ MQTT
                           ▼
┌────────────────────────────────────────────────────────────────┐
│                      BROKER LAYER                              │
│                                                                │
│             broker.emqx.io : 1883 / 8883 (TLS)                 │
│                                                                │
│          Topics: hospital/patient/001/{vitals,alarm,           │
│                    state,presence,cmd,feedback}                │
└──────────────────────────┬─────────────────────────────────────┘
                           │ MQTT Subscribe
                           ▼
┌────────────────────────────────────────────────────────────────┐
│                    DASHBOARD LAYER                             │
│                                                                │
│   Node-RED ──► Gauge SpO₂ / HR / Suhu                          │
│            ──► Line Chart Tren                                 │
│            ──► Patient Status Banner (color-coded)             │
│            ──► Alarm Log Table                                 │
│            ──► Alarm Notification (color-coded bar)            │
│            ──► Control Buttons (PING, GET STATE, REBOOT,       │
│                 THRESHOLDS, ACK ALARM)                         │
└────────────────────────────────────────────────────────────────┘
```

---

## 🔧 Komponen yang Digunakan

### Hardware (Implementasi Nyata)

| Komponen | Fungsi | Protokol |
|---|---|---|
| ESP32 DevKit C v4 | Mikrokontroler utama | — |
| OLED SSD1306 0.96" | Display lokal status dan nilai | I2C (0x3C) |
| LED Merah | Indikator status Critical | GPIO 25 |
| LED Kuning | Indikator status Warning | GPIO 33 |
| LED Hijau | Indikator status Normal | GPIO 26 |
| Buzzer Pasif | Alarm audio saat Critical/Warning | GPIO 27 |
| Resistor 220Ω × 3 | Pembatas arus LED | — |

### Simulasi Wokwi

| Komponen Wokwi | GPIO |
|---|---|
| OLED SSD1306 | I2C SDA=21, SCL=22 |
| LED Merah + Resistor | GPIO 25 |
| LED Kuning + Resistor | GPIO 33 |
| LED Hijau + Resistor | GPIO 26 |
| Buzzer Pasif | GPIO 27 |

Data vital tidak berasal dari slider ADC, melainkan dari **dataset CSV Kaggle** yang disimpan di SPIFFS.

---

## 📁 Struktur Folder Proyek

```
Project IOT/
│
├── README.md               # Dokumentasi proyek
│
├── Wokwi/                  # Source code ESP32 + konfigurasi simulator
│   ├── Wokwi.ino               # Program utama (setup, loop, OLED, aktuator)
│   ├── Config.h                # Semua konstanta: WiFi, MQTT, PIN, threshold
│   ├── Config.cpp              # Definisi konstanta runtime
│   ├── MQTT_handler.h          # Header modul MQTT (public API)
│   ├── MQTT_handler.cpp        # WiFi + MQTT + callback command + NVS
│   ├── diagram.json            # Wiring diagram Wokwi
│   ├── libraries.txt           # Daftar library Arduino + versi
│   ├── wokwi.toml              # Konfigurasi package Wokwi
│   └── vitals.csv
│                               # Dataset Kaggle (rename dari human_vital_signs_dataset_2024.csv)
│
└── Node-Red/               # Dashboard visualisasi
    └── flows.json              # Node-RED flow siap import
```

---

## 📄 Penjelasan File

### `Config.h`
Seluruh konfigurasi sistem dalam bentuk `extern` constants dan `#define`:
- **WiFi**: SSID, password, AP name/pass untuk WiFiManager
- **MQTT**: broker address (`broker.emqx.io`), port (1883/8883), keepalive
- **Topics**: `TOPIC_VITALS`, `TOPIC_ALARM`, `TOPIC_STATE`, `TOPIC_PRESENCE`, `TOPIC_CMD`, `TOPIC_FEEDBACK`
- **Pin definitions**: semua GPIO ESP32 untuk LED, buzzer, OLED
- **OLED**: konstanta layout posisi tiap elemen tampilan
- **Threshold**: batas Normal / Warning / Critical + validasi range
- **Status constants**: `STATUS_NORMAL`, `STATUS_WARNING`, `STATUS_CRITICAL`
- **Timing**: `PUBLISH_INTERVAL` (1000ms), buzzer interval/freq, `FILTER_SIZE` (5)
- **CSV**: path file dataset, data log path, max lines
- **OLED Sleep**: idle timeout 30 detik

### `Config.cpp`
Definisi semua `extern` constants yang dideklarasikan di `Config.h`. Threshold bersifat **non-const** (`float`/`int`, bukan `const`) agar bisa diubah via MQTT commands dan disimpan ke NVS.

### `MQTT_handler.h`
Header modul MQTT yang mengekspos API publik:
```cpp
void mqttSetup();                                           // Init WiFi + MQTT
void mqttLoop();                                            // Maintain koneksi + process queue
bool mqttPublishVitals(float spo2, int hr, float temp, const char* status, const char* ts);
bool mqttPublishAlarm(const char* status, float spo2, int hr, float temp, const char* ts);
void mqttPublishState(const char* state);                   // Retained state
void mqttSetState(const char* state);
const char* mqttGetState();
bool mqttIsConnected();
bool mqttIsAlarmAcknowledged();
void mqttResetAlarmAck();
```

### `MQTT_handler.cpp`
Implementasi lengkap modul koneksi dan komunikasi:
- **WiFi**: WiFiManager autoConnect dengan fallback ke `WIFI_SSID`/`WIFI_PASS` dari Config
- **MQTT**: koneksi ke `broker.emqx.io` dengan auto reconnect (non-blocking, retry 30s), LWT presence, re-subscribe
- **Callback**: parsing command case-insensitive dengan queue response (re-entrancy safe)
- **NVS Persistence**: threshold tersimpan di NVS dengan debounce 5s
- **Unique Client ID**: digenerate dari MAC address (`esp32_patient_XXXXXXXX`)
- **JSON Serialization**: menggunakan `StaticJsonDocument` (stack, no heap fragmentation)
- **SET_THRESHOLD**: 8 threshold dapat diubah via macro, divalidasi range sebelum disimpan
- **GET_THRESHOLDS**: reply JSON semua threshold ke `TOPIC_FEEDBACK`

### `Wokwi.ino`
Program utama yang mengatur alur sistem:
- **setup()**: inisialisasi pin, SPIFFS, OLED splash, OTA, watchdog, MQTT
- **loop()**: maintain koneksi → baca CSV → filter → tentukan status → aktuator → publish
- **readCsvRow()**: baca 1 baris CSV dari SPIFFS, parse 4 kolom, loop ke awal saat EOF
- **applyMovingAverage()**: rata-rata buffer 5 sampel untuk transisi halus
- **determinePatientStatus()**: evaluasi threshold 2-level + hysteresis 2 siklus
- **handleActuators()**: kontrol LED 3 warna + buzzer non-blocking (Critical: rapid 600ms, Warning: slow 2000ms)
- **updateOled()**: render layout rapi (label kiri, value di X=55, semua angka text size 2), sleep 30s idle
- **logToSpiffs()**: simpan data ke SPIFFS saat MQTT offline + auto rotation 500 baris
- **replayDataLog()**: publish ulang data yang terlewat setelah reconnect + WDT reset

### `diagram.json`
Wiring diagram Wokwi: ESP32 DevKit + OLED SSD1306 + 3 LED + 3 resistor + buzzer.

### `flows.json`
Flow Node-RED lengkap dengan:
- MQTT input untuk `vitals`, `alarm`, `state`, `presence`, `feedback`
- Split & route data ke gauge, chart, status banner
- Alarm table + alarm notification bar (color-coded via ui_template)
- Control buttons: PING, GET STATE, REBOOT, THRESHOLDS, ACK ALARM
- Command response display — tampilkan reply dari ESP32 (topic feedback)
- Presence change detection (hanya update jika value berubah)

### `libraries.txt` / `wokwi.toml`
Daftar library:
```
Adafruit SSD1306      @ 2.5.7
Adafruit GFX Library  @ 1.11.5
PubSubClient          @ 2.8.0
ArduinoJson           @ 6.21.2
WiFiManager           @ 2.0.14
```

---

## 📡 MQTT Topic & Payload

### Topic Structure

```
hospital/
└── patient/
    └── 001/
        ├── vitals      ← Data sensor setiap 1 detik
        ├── alarm       ← Event saat transisi Warning / Critical
        ├── state       ← Retained state pasien
        ├── presence    ← LWT online/offline + PING response
        ├── cmd         ← Command masuk (subscribe)
        └── feedback    ← Response command (GET_THRESHOLDS, ACK_ALARM)
```

### Payload: `hospital/patient/001/vitals`

```json
{
  "timestamp":      "2024-07-19 21:53:45",
  "spo2":           97,
  "heart_rate":     72,
  "temperature":    36.8,
  "patient_status": "Normal"
}
```

### Payload: `hospital/patient/001/alarm`

```json
{
  "alarm_status": "Critical",
  "spo2":         85,
  "heart_rate":   45,
  "temperature":  39.2,
  "timestamp":    "2024-07-19 21:54:01"
}
```

### Payload: `hospital/patient/001/state`

Retained — subscriber baru langsung mendapat state terakhir:
```
Normal / Warning / Critical
```

### Payload: `hospital/patient/001/presence`

LWT + presence:
- **online** — saat connect / PING response
- **offline** — LWT (broker publish otomatis jika ESP disconnect mendadak)

### Payload: `hospital/patient/001/feedback`

Response dari command ESP32 (GET_THRESHOLDS, ACK_ALARM):

```json
{
  "cmd": "THRESHOLDS",
  "spo2_warn": 94.0,
  "spo2_crit": 90.0,
  "hr_warn_low": 60,
  "hr_warn_high": 100,
  "hr_crit_low": 50,
  "hr_crit_high": 120,
  "temp_warn": 37.5,
  "temp_crit": 38.5,
  "temp_warn_low": 36.0,
  "temp_crit_low": 35.0
}
```

Contoh response ACK_ALARM:
```json
{"cmd":"ACK_ALARM","status":"ok"}
```

### Field Description

| Field | Tipe | Satuan | Keterangan |
|---|---|---|---|
| `timestamp` | string | — | Timestamp dari dataset CSV |
| `spo2` | integer | % | Saturasi oksigen darah |
| `heart_rate` | integer | bpm | Detak jantung per menit |
| `temperature` | float | °C | Suhu tubuh (1 desimal) |
| `patient_status` | string | — | `"Normal"` / `"Warning"` / `"Critical"` |
| `alarm_status` | string | — | Sama dengan `patient_status`, hanya di topic alarm |

---

## 📡 MQTT Command Set

Perintah dikirim ke `hospital/patient/001/cmd`. Case-insensitive.

| Command | Contoh | Response |
|---|---|---|
| `PING` | `PING` | `"online"` ke `presence` |
| `GET_STATE` | `GET_STATE` | State terakhir ke `state` |
| `GET_THRESHOLDS` | `GET_THRESHOLDS` | JSON semua threshold ke `feedback` |
| `SET_THRESHOLD_SPO2_WARN:<val>` | `SET_THRESHOLD_SPO2_WARN:93` | Ubah SpO₂ warning (80-100) |
| `SET_THRESHOLD_SPO2_CRIT:<val>` | `SET_THRESHOLD_SPO2_CRIT:88` | Ubah SpO₂ critical (80-100) |
| `SET_THRESHOLD_HR_WARN_LOW:<val>` | `SET_THRESHOLD_HR_WARN_LOW:55` | Ubah HR warning low (20-250) |
| `SET_THRESHOLD_HR_WARN_HIGH:<val>` | `SET_THRESHOLD_HR_WARN_HIGH:110` | Ubah HR warning high (20-250) |
| `SET_THRESHOLD_HR_CRIT_LOW:<val>` | `SET_THRESHOLD_HR_CRIT_LOW:45` | Ubah HR critical low (20-250) |
| `SET_THRESHOLD_HR_CRIT_HIGH:<val>` | `SET_THRESHOLD_HR_CRIT_HIGH:130` | Ubah HR critical high (20-250) |
| `SET_THRESHOLD_TEMP_WARN:<val>` | `SET_THRESHOLD_TEMP_WARN:38.0` | Ubah temp warning high (34-42) |
| `SET_THRESHOLD_TEMP_CRIT:<val>` | `SET_THRESHOLD_TEMP_CRIT:39.0` | Ubah temp critical high (34-42) |
| `SET_THRESHOLD_TEMP_WARN_LOW:<val>` | `SET_THRESHOLD_TEMP_WARN_LOW:36.0` | Ubah temp warning low / hipotermia (34-42) |
| `SET_THRESHOLD_TEMP_CRIT_LOW:<val>` | `SET_THRESHOLD_TEMP_CRIT_LOW:35.0` | Ubah temp critical low / hipotermia (34-42) |
| `ACK_ALARM` | `ACK_ALARM` | Silent buzzer sampai status berubah |
| `REBOOT` | `REBOOT` | Restart ESP32 dalam 1 detik |

### GET_THRESHOLDS Response Example
```json
{
  "cmd": "THRESHOLDS",
  "spo2_warn": 94.0,
  "spo2_crit": 90.0,
  "hr_warn_low": 60,
  "hr_warn_high": 100,
  "hr_crit_low": 50,
  "hr_crit_high": 120,
  "temp_warn": 37.5,
  "temp_crit": 38.5,
  "temp_warn_low": 36.0,
  "temp_crit_low": 35.0
}
```

---

## 🚦 Threshold Status Pasien

Sistem menggunakan **dua level threshold** per parameter. Evaluasi dilakukan dari Critical terlebih dahulu. Status berubah hanya setelah **2 siklus berturut-turut** (hysteresis) untuk mencegah flapping.

### SpO₂ (Saturasi Oksigen)

| Status | Kondisi | LED | Buzzer |
|---|---|---|---|
| 🟢 Normal | SpO₂ ≥ 94% | Hijau ON | OFF |
| 🟡 Warning | 90% ≤ SpO₂ < 94% | Kuning ON | Beep 800Hz tiap 2 detik |
| 🔴 Critical | SpO₂ < 90% | Merah ON | Beep 1200Hz tiap 600ms |

### Heart Rate

| Status | Kondisi | LED | Buzzer |
|---|---|---|---|
| 🟢 Normal | 60 ≤ HR ≤ 100 bpm | Hijau ON | OFF |
| 🟡 Warning | 50 ≤ HR < 60 atau 100 < HR ≤ 120 bpm | Kuning ON | Beep 800Hz tiap 2 detik |
| 🔴 Critical | HR < 50 atau HR > 120 bpm | Merah ON | Beep 1200Hz tiap 600ms |

### Suhu Tubuh

| Status | Kondisi | LED | Buzzer |
|---|---|---|---|
| 🟢 Normal | 36.0°C < Suhu ≤ 37.5°C | Hijau ON | OFF |
| 🟡 Warning (Demam) | 37.5°C < Suhu ≤ 38.5°C | Kuning ON | Beep 800Hz tiap 2 detik |
| 🟡 Warning (Hipotermia) | 35.0°C ≤ Suhu < 36.0°C | Kuning ON | Beep 800Hz tiap 2 detik |
| 🔴 Critical (Demam) | Suhu > 38.5°C | Merah ON | Beep 1200Hz tiap 600ms |
| 🔴 Critical (Hipotermia) | Suhu < 35.0°C | Merah ON | Beep 1200Hz tiap 600ms |

> **Catatan:** Buzzer Warning dapat di-silent via `ACK_ALARM`. Buzzer Critical juga silent setelah ACK. Saat status berubah, ACK di-reset dan buzzer aktif kembali. Jika lebih dari satu parameter memenuhi kondisi berbeda, sistem menampilkan status tertinggi (Critical > Warning > Normal).

---

## 🚀 Cara Menjalankan Simulasi

### Prasyarat

Install library berikut via **Arduino IDE → Manage Libraries**:
```
Adafruit SSD1306
Adafruit GFX Library
PubSubClient
ArduinoJson
WiFiManager
```

### Langkah di Wokwi

1. Buka [wokwi.com](https://wokwi.com) → klik **New Project** → pilih **ESP32**
2. Hapus konten default
3. Salin isi `Wokwi/diagram.json` ke tab **diagram.json** di Wokwi
4. Salin seluruh file kode dari folder `Wokwi/` ke tab yang sesuai:
   - `Wokwi.ino` → tab **Wokwi.ino**
   - `Config.h` → tab **Config.h**
   - `Config.cpp` → tab **Config.cpp**
   - `MQTT_handler.h` → tab **MQTT_handler.h**
   - `MQTT_handler.cpp` → tab **MQTT_handler.cpp**
5. Upload dataset CSV:
   - Buka tab terakhir → klik dropdown (panah) → pilih **Upload file(s)...**
   - Upload `Wokwi/vitals.csv`
   - File akan muncul sebagai tab baru
6. Klik **▶ Start Simulation**
7. Buka **Serial Monitor** untuk melihat output log

> **Catatan:** Nama file CSV akan otomatis terbaca sebagai `/vitals.csv` oleh ESP32 (≥32 chars aman).

---

## 📥 Cara Install & Import Flow Node-RED

### 1. Install Node.js (Jika Belum Ada)

```bash
# Linux (Debian/Ubuntu)
curl -fsSL https://deb.nodesource.com/setup_20.x | sudo -E bash -
sudo apt install -y nodejs

# Cek versi
node -v   # minimal v18
npm -v
```

### 2. Install Node-RED + Dashboard

```bash
npm install -g node-red node-red-dashboard
```

### 3. Jalankan Node-RED

```bash
node-red
```

Buka browser: `http://localhost:1880`

### 4. Import Flow

1. Buka `http://localhost:1880`
2. Klik **Menu ☰** (kanan atas) → **Import**
3. Salin seluruh isi file `Node-Red/flows.json`
4. Paste ke dialog import → klik **Import**
5. Klik **Deploy** (tombol merah kanan atas)

### 5. Dashboard

Buka dashboard di: `http://localhost:1880/ui`

### Konfigurasi MQTT Broker (Jika Perlu)

Flow sudah dikonfigurasi untuk `broker.emqx.io:1883`. Jika ingin ganti broker:
1. Klik dua kali node **MQTT Broker** (`broker.emqx.io`)
2. Ubah **Server** dan **Port**
3. Klik **Update** → **Deploy**

---

## 📊 Struktur Dashboard

### Tab: Vital Sign Monitor

| Group | Widget | Data Source |
|---|---|---|
| **Patient Status** | Status Banner (color-coded) | `state` topic |
| | SpO2 Gauge (80-100%, segmen 90/94) | `vitals.spo2` |
| | HR Gauge (30-160 bpm, segmen 50/60/100/120) | `vitals.heart_rate` |
| | Temp Gauge (35-41°C, segmen 37.5/38.5) | `vitals.temperature` |
| | Vitals Timeline Chart (300s window) | `vitals` |
| **Alarms & Events** | Alarm Log Table | `alarm` topic |
| | Alarm Notification Bar (color-coded) | `alarm` topic |
| | Device Connection Status | `presence` topic |
| **Controls** | PING Button | → `cmd` |
| | GET STATE Button | → `cmd` |
| | REBOOT Button | → `cmd` |
| | THRESHOLDS Button | → `cmd` |
| | ACK ALARM Button | → `cmd` |
| | Command Response Display | ← `feedback` topic |

### Alur Data

```
broker.emqx.io
  │
  ├─ hospital/patient/001/vitals    ──► SpO2 Gauge, HR Gauge, Temp Gauge, Chart
  ├─ hospital/patient/001/state     ──► Status Banner (hijau/kuning/merah)
  ├─ hospital/patient/001/alarm     ──► Alarm Table + Alarm Notification Bar
  ├─ hospital/patient/001/presence  ──► Connection Status (online/offline)
  ├─ hospital/patient/001/cmd       ──► Command Out ke ESP32
  │    ◄── PING / GET_STATE / REBOOT / GET_THRESHOLDS / ACK_ALARM
  └─ hospital/patient/001/feedback  ──► Command Response Display

Node-RED Dashboard: http://localhost:1880/ui
```

---

## 🎬 Demo Scenario

### Skenario 1 — Dataset Berjalan Normal ✅

**Saat dataset berisi data normal:**
- OLED menampilkan `STATUS: Normal`
- LED **hijau** menyala
- Buzzer diam
- Dashboard: status banner hijau, gauge di zona aman

### Skenario 2 — Dataset Mengandung Warning ⚠️

**Saat dataset beralih ke data threshold Warning (SpO₂ 90-93%, HR 50-58, atau suhu 37.6-38.5°C):**
- OLED menampilkan `STATUS: Warning`
- LED **kuning** menyala
- Buzzer beep pelan setiap 2 detik
- Dashboard: status banner kuning, alarm log bertambah

### Skenario 3 — Dataset Mengandung Critical 🚨

**Saat dataset beralih ke data threshold Critical (SpO₂ <90%, HR <50/>120, atau suhu >38.5°C):**
- OLED menampilkan `STATUS: Critical`
- LED **merah** menyala
- **Buzzer beep cepat** setiap 600ms
- Dashboard: status banner merah, alarm log baru, alarm notification bar merah

### Skenario 4 — Remote ACK Alarm + Threshold

1. Kirim `ACK_ALARM` dari dashboard → buzzer silent (status tetap Critical)
2. Kirim `SET_THRESHOLD_SPO2_CRIT:85` → threshold critical turun jadi 85%
3. Kirim `GET_THRESHOLDS` → konfirmasi perubahan

### Skenario 5 — MQTT Offline / Reconnect

1. Matikan WiFi di Wokwi (atah koneksi)
2. Data otomatis tersimpan ke SPIFFS (`data_log.csv`)
3. Hidupkan WiFi kembali → semua data ter-replay otomatis ke broker

---

## 📸 Screenshots

> **Tambahkan gambar berikut ke folder `/docs/images/` dan perbarui path di bawah ini:**

### Wiring Diagram Wokwi
```
[ Tambahkan screenshot: wokwi_diagram.png ]
```

### Simulasi Berjalan — Status Normal
```
[ Tambahkan screenshot: sim_normal.png ]
```

### Simulasi Berjalan — Status Critical
```
[ Tambahkan screenshot: sim_critical.png ]
```

### Node-RED Flow
```
[ Tambahkan screenshot: nodered_flow.png ]
```

### Node-RED Dashboard
```
[ Tambahkan screenshot: nodered_dashboard.png ]
```

### Serial Monitor Output
```
[ Tambahkan screenshot: serial_monitor.png ]
```

---

## ✅ Hasil yang Diharapkan saat Demo

- [ ] Simulasi Wokwi berjalan tanpa error compile
- [ ] Dataset CSV terbaca dan di-loop otomatis (Serial Monitor: `[CSV] EOF -- loop ke awal dataset`)
- [ ] OLED menampilkan nilai SpO₂, HR, Suhu, status, dan indikator MQTT
- [ ] LED berganti warna sesuai status pasien
- [ ] Buzzer beep cepat saat Critical, lambat saat Warning
- [ ] `ACK_ALARM` dari dashboard meng-silent buzzer
- [ ] Serial Monitor menampilkan payload JSON setiap 1 detik
- [ ] Node-RED dashboard terhubung dan menerima data
- [ ] Gauge, chart, status banner berubah sesuai data
- [ ] Alarm log table menampilkan entri saat transisi Warning/Critical
- [ ] Threshold dapat diubah via MQTT dan bertahan setelah reboot (NVS)
- [ ] WiFi/MQTT disconnect tidak menyebabkan data hilang (SPIFFS fallback)
- [ ] Replay data log setelah reconnect

---

## 🔮 Future Enhancement

### 🗄️ InfluxDB — Time-Series Database
Menggantikan CSV logging dengan database time-series yang scalable.

### 📈 Grafana — Advanced Analytics Dashboard
Visualisasi lebih canggih dengan alerting dan multi-day trends.

### 🌐 Multi-Patient Support
Ekspansi topic MQTT untuk mendukung banyak pasien via ID unik.

### 🔒 MQTT TLS + Autentikasi
Mengaktifkan `MQTT_USE_TLS = 1` untuk koneksi aman port 8883.

### 📱 Mobile Notification
Integrasi Telegram Bot atau WhatsApp API untuk notifikasi alarm.

### 🏥 Sensor Hardware Nyata
Mengganti dataset CSV dengan sensor MAX30102 dan MLX90614 via I2C.

---

## 📌 Kesimpulan

Proyek **Wearable Vital Sign Monitor** berhasil mengimplementasikan pipeline IoT end-to-end yang lengkap: dataset CSV → ESP32 → MQTT Broker → Node-RED Dashboard. Sistem mendukung remote konfigurasi threshold, data logging fallback, replay otomatis, dan three-level alert dengan buzzer yang dapat di-ACK dari jarak jauh.

Arsitektur modular (`Config` / `MQTT_handler` / `Wokwi.ino`) memastikan maintainability dan kemudahan ekspansi ke sensor nyata, database time-series, atau dashboard yang lebih canggih tanpa mengubah firmware secara fundamental.

---

<div align="center">

**IoT Development Competition — TETI UGM Bootcamp 2026**

*Biomedical Application Category*
</div>
