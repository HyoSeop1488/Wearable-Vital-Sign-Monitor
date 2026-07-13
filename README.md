# 🩺 Wearable Vital Sign Monitor
### IoT Development Competition — TETI UGM Bootcamp 2026

> Sistem monitoring tanda vital berbasis ESP32 yang mengukur **SpO₂**, **Heart Rate**, dan **Suhu Tubuh** secara real-time, mengirimkan data ke dashboard Node-RED melalui protokol MQTT.

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
- [Simulasi Wokwi](#-simulasi-wokwi)
- [Struktur Folder Proyek](#-struktur-folder-proyek)
- [Penjelasan File](#-penjelasan-file)
- [MQTT Topic & Payload](#-mqtt-topic--payload)
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

Proyek ini dikembangkan sebagai bagian dari **IoT Development Competition TETI UGM Bootcamp 2026** dalam kategori **Biomedical Application**. Sistem dirancang dengan arsitektur MVP (Minimum Viable Product) yang berfokus pada pipeline data end-to-end: dari akuisisi sensor hingga visualisasi dashboard.

---

## 🎯 Tujuan

- Mengakuisisi data SpO₂, heart rate, dan suhu tubuh secara real-time menggunakan ESP32
- Mengirimkan data ke remote server menggunakan protokol MQTT
- Menyajikan informasi melalui dashboard Node-RED dengan gauge, chart tren, dan status alarm
- Mengimplementasikan sistem alert tiga level: **Normal**, **Warning**, dan **Critical**
- Menyediakan fitur data logging dan download CSV kapan saja

---

## ✨ Fitur Utama

| Fitur | Deskripsi |
|---|---|
| 📡 **Real-time Monitoring** | Data SpO₂, Heart Rate, Suhu dikirim setiap 2 detik via MQTT |
| 🚨 **Three-level Alert** | Status Normal / Warning / Critical dengan LED indikator dan buzzer |
| 🖥️ **OLED Display** | Tampilan lokal nilai sensor dan status pasien di perangkat |
| 📊 **Node-RED Dashboard** | Gauge, line chart tren 5 menit, dan alarm log terpusat |
| 💾 **Data Logging** | Setiap data point disimpan ke file CSV secara otomatis |
| ⬇️ **Download CSV** | Endpoint HTTP `GET /download-csv` untuk unduh histori kapan saja |
| 🔁 **Auto Reconnect** | ESP32 otomatis reconnect ke WiFi dan MQTT broker jika koneksi terputus |
| 🧮 **Moving Average Filter** | Filter 5 sampel untuk meredam noise dan mencegah alarm palsu |

---

## 🏗️ Arsitektur Sistem

```
┌─────────────────────────────────────────────────────────────────┐
│                        DEVICE LAYER                             │
│                                                                 │
│   MAX30102 ──┐                    ┌── OLED Display              │
│   MLX90614 ──┤── ESP32 ───────────┤── LED (R/Y/G)               │
│     Wokwi  ──┘                    └── Buzzer Alarm              │
│  (3x Slider)                                                    │
└────────────────────────┬────────────────────────────────────────┘
                         │ WiFi
                         │ MQTT Publish
                         ▼
┌────────────────────────────────────────────────────────────────┐
│                      BROKER LAYER                              │
│                                                                │
│              HiveMQ Public Broker                              │
│              broker.hivemq.com : 1883                          │
│                                                                │
│   Topic: hospital/patient/001/vitals                           │
│   Topic: hospital/patient/001/alarm                            │
└────────────────────────┬───────────────────────────────────────┘
                         │ MQTT Subscribe
                         ▼
┌────────────────────────────────────────────────────────────────┐
│                    DASHBOARD LAYER (MVP)                       │
│                                                                │
│   Node-RED ──► Gauge SpO₂ / HR / Suhu                          │
│            ──► Line Chart Tren 5 Menit                         │
│            ──► Patient Status Banner                           │
│            ──► Alarm Log Table                                 │
│            ──► CSV File Logger                                 │
│            ──► HTTP Download Endpoint                          │
└────────────────────────────────────────────────────────────────┘

               [ FUTURE ENHANCEMENT ]
                         │
                         ▼
              InfluxDB (time-series DB)
                         │
                         ▼
             Grafana (advanced analytics)
```

---

## 🔧 Komponen yang Digunakan

### Hardware (Implementasi Nyata)

| Komponen | Fungsi | Protokol |
|---|---|---|
| ESP32 DevKit C v4 | Mikrokontroler utama | — |
| MAX30102 | Sensor SpO₂ dan Heart Rate | I2C (0x57) |
| MLX90614 | Sensor suhu tubuh inframerah | I2C (0x5A) |
| OLED SSD1306 0.96" | Display lokal status dan nilai | I2C (0x3C) |
| LED Merah | Indikator status Critical | GPIO 25 |
| LED Kuning | Indikator status Warning | GPIO 33 |
| LED Hijau | Indikator status Normal | GPIO 26 |
| Buzzer Pasif | Alarm audio saat Critical | GPIO 27 |
| Resistor 220Ω × 3 | Pembatas arus LED | — |

### Simulasi Wokwi

| Komponen Wokwi | Menggantikan | GPIO |
|---|---|---|
| Slide Potentiometer 1 | MAX30102 (SpO₂) | GPIO 34 |
| Slide Potentiometer 2 | MAX30102 (Heart Rate) | GPIO 35 |
| Slide Potentiometer 3 | MLX90614 (Suhu) | GPIO 32 |

---

## 🎮 Simulasi Wokwi

Karena komponen `MAX30102` dan `MLX90614` tidak tersedia di library simulator Wokwi, nilai sensor disimulasikan menggunakan **3 slide potentiometer** yang terhubung ke pin ADC ESP32.

### Pemetaan Slider → Nilai Sensor

```
Slider 1 (GPIO 34)  ──►  SpO₂       : ADC 0–4095  →  85.0% – 100.0%
Slider 2 (GPIO 35)  ──►  Heart Rate : ADC 0–4095  →  40 – 140 bpm
Slider 3 (GPIO 32)  ──►  Suhu Tubuh : ADC 0–4095  →  35.0°C – 40.0°C
```

### Moving Average Filter

Setiap nilai mentah dari ADC dimasukkan ke buffer 5 sampel. Nilai yang dipublikasikan ke MQTT adalah rata-rata buffer tersebut, sehingga pergerakan slider yang tiba-tiba tidak langsung memicu alarm palsu.

```
Nilai ADC (raw)  →  Buffer[5]  →  MEAN  →  Nilai final yang dikirim
```

---

## 📁 Struktur Folder Proyek

```
wearable-vital-sign-monitor/
│
├── Sketch.ino              # Program utama ESP32 (setup, loop, sensor, display)
├── Config.h                # Semua konstanta: WiFi, MQTT, PIN, threshold
├── Config.cpp              # Pasangan Config.h (pola untuk future runtime config)
├── MQTT_handler.h          # Header modul MQTT (public API)
├── MQTT_handler.cpp        # Implementasi koneksi WiFi, MQTT, dan publish JSON
├── diagram.json            # Wiring diagram Wokwi (board, komponen, koneksi)
├── libraries.txt           # Daftar library Arduino yang dibutuhkan
│
└── node-red/
    └── flow.json           # Flow Node-RED siap import (dashboard + logging)
```

---

## 📄 Penjelasan File

### `Config.h`
Satu-satunya file yang perlu diubah untuk deployment. Berisi seluruh konfigurasi sistem:
- **WiFi**: SSID dan password
- **MQTT**: broker address, port, client ID, keepalive
- **Topics**: `TOPIC_VITALS`, `TOPIC_ALARM`, `TOPIC_STATUS`
- **Pin definitions**: semua GPIO ESP32
- **Threshold**: batas Normal / Warning / Critical untuk ketiga parameter
- **Timing**: `PUBLISH_INTERVAL` (2000ms), `BUZZER_INTERVAL` (600ms), `FILTER_SIZE` (5)

### `Config.cpp`
Pasangan `Config.h`. Saat ini berfungsi sebagai placeholder dokumentasi. Dipersiapkan untuk menyimpan variabel runtime yang dapat diubah saat eksekusi (misalnya threshold dari EEPROM atau NVS) tanpa mengubah `Config.h`.

### `MQTT_handler.h`
Header modul MQTT yang mengekspos API publik:
```cpp
void mqttSetup();
void mqttLoop();
bool mqttPublishVitals(float spo2, int heartRate, float temperature, const char* patientStatus);
bool mqttPublishAlarm(const char* patientStatus, float spo2, int heartRate, float temperature);
bool mqttIsConnected();
```

### `MQTT_handler.cpp`
Implementasi lengkap modul MQTT:
- Koneksi WiFi dengan retry 20x
- Koneksi MQTT dengan auto reconnect di setiap iterasi loop
- Serialisasi payload JSON menggunakan `ArduinoJson`
- Publish ke dua topic terpisah: `vitals` dan `alarm`

### `Sketch.ino`
Program utama yang mengatur alur sistem:
- `setup()`: inisialisasi pin, OLED splash screen, MQTT setup
- `loop()`: baca sensor → filter → cek threshold → aktuator → update OLED → publish MQTT
- Fungsi `readMockSensors()`: konversi ADC slider ke satuan fisik
- Fungsi `applyMovingAverage()`: hitung rata-rata buffer 5 sampel
- Fungsi `determinePatientStatus()`: evaluasi threshold dan set status string
- Fungsi `handleActuators()`: kontrol LED dan buzzer non-blocking
- Fungsi `updateOLED()`: render tampilan OLED

### `diagram.json`
Wiring diagram lengkap untuk Wokwi. Komponen utama:
- `board-esp32-devkit-c-v4` sebagai mikrokontroler
- `wokwi-breadboard-half` sebagai platform koneksi komponen
- `wokwi-ssd1306` sebagai OLED display
- Tiga `wokwi-resistor` 220Ω + tiga `wokwi-led` tersusun melalui breadboard
- `wokwi-buzzer` terhubung via kolom breadboard
- Tiga `wokwi-slide-potentiometer` sebagai mock sensor

### `node-red/flow.json`
Flow Node-RED yang sudah dikonfigurasi penuh:
- Subscribe ke dua MQTT topic
- Split data ke gauge, chart, dan status formatter
- Alarm log dari topic `alarm`
- CSV logger otomatis dengan header inject saat startup
- HTTP endpoint `GET /download-csv` untuk unduh data

---

## 📡 MQTT Topic & Payload

### Topic Structure

```
hospital/
└── patient/
    └── 001/
        ├── vitals    ← Data sensor setiap 2 detik
        └── alarm     ← Dikirim hanya saat status Warning atau Critical
```

### Payload: `hospital/patient/001/vitals`

```json
{
  "timestamp":      "2026-07-22T08:30:15Z",
  "spo2":           98,
  "heart_rate":     75,
  "temperature":    36.5,
  "patient_status": "Normal"
}
```

### Payload: `hospital/patient/001/alarm`

```json
{
  "alarm_status": "Critical",
  "spo2":         88,
  "heart_rate":   130,
  "temperature":  38.9,
  "triggered_at": 45230
}
```

| Field | Tipe | Satuan | Keterangan |
|---|---|---|---|
| `timestamp` | string | ISO 8601 | Waktu pengiriman data |
| `spo2` | integer | % | Saturasi oksigen darah |
| `heart_rate` | integer | bpm | Detak jantung per menit |
| `temperature` | float | °C | Suhu tubuh (1 desimal) |
| `patient_status` | string | — | `"Normal"` / `"Warning"` / `"Critical"` |
| `alarm_status` | string | — | Sama dengan `patient_status`, hanya di topic alarm |
| `triggered_at` | integer | ms | `millis()` saat alarm dipicu |

---

## 🚦 Threshold Status Pasien

Sistem menggunakan dua level threshold per parameter. Evaluasi dilakukan dari **Critical terlebih dahulu**; jika tidak ada yang Critical maka cek Warning.

### SpO₂ (Saturasi Oksigen)

| Status | Kondisi | LED | Buzzer |
|---|---|---|---|
| 🟢 Normal | SpO₂ ≥ 94% | Hijau ON | OFF |
| 🟡 Warning | 90% ≤ SpO₂ < 94% | Kuning ON | OFF |
| 🔴 Critical | SpO₂ < 90% | Merah ON | Beep berulang |

### Heart Rate

| Status | Kondisi | LED | Buzzer |
|---|---|---|---|
| 🟢 Normal | 60 ≤ HR ≤ 100 bpm | Hijau ON | OFF |
| 🟡 Warning | 50 ≤ HR < 60 atau 100 < HR ≤ 120 bpm | Kuning ON | OFF |
| 🔴 Critical | HR < 50 atau HR > 120 bpm | Merah ON | Beep berulang |

### Suhu Tubuh

| Status | Kondisi | LED | Buzzer |
|---|---|---|---|
| 🟢 Normal | Suhu ≤ 37.5°C | Hijau ON | OFF |
| 🟡 Warning | 37.5°C < Suhu ≤ 38.5°C | Kuning ON | OFF |
| 🔴 Critical | Suhu > 38.5°C | Merah ON | Beep berulang |

> **Catatan:** Jika lebih dari satu parameter memenuhi kondisi Critical atau Warning secara bersamaan, sistem tetap menampilkan satu status tertinggi (Critical > Warning > Normal).

---

## 🚀 Cara Menjalankan Simulasi

### Prasyarat

Install library berikut via **Arduino IDE → Manage Libraries** atau tambahkan ke `wokwi.toml`:

```
Adafruit SSD1306  @ 2.5.7
Adafruit GFX Library @ 1.11.5
PubSubClient      @ 2.8.0
ArduinoJson       @ 6.21.2
```

### Langkah di Wokwi

1. Buka [wokwi.com](https://wokwi.com) → klik **New Project** → pilih **ESP32**
2. Hapus konten default yang muncul
3. Salin isi `diagram.json` ke tab **diagram.json** di Wokwi
4. Salin seluruh file kode (`Sketch.ino`, `Config.h`, `Config.cpp`, `MQTT_handler.h`, `MQTT_handler.cpp`) ke tab yang sesuai
5. Klik **▶ Start Simulation**
6. Buka **Serial Monitor** untuk melihat output log dan payload MQTT
7. Geser ketiga slider untuk mengubah nilai sensor secara real-time

---

## 📥 Cara Import Flow Node-RED

### Prasyarat

Install Node-RED dan palette berikut:
```bash
npm install -g node-red
```
Buka Node-RED → **Menu ☰ → Manage Palette → Install:**
- `node-red-dashboard`

### Langkah Import

1. Buka Node-RED di browser: `http://localhost:1880`
2. Klik **Menu ☰ → Import**
3. Salin seluruh isi file `node-red/flow.json`
4. Paste ke dialog import → klik **Import**
5. Klik **Deploy** (tombol merah kanan atas)
6. Buka dashboard di: `http://localhost:1880/ui`

### Download CSV

Setelah data mulai masuk, unduh histori kapan saja melalui browser:
```
http://localhost:1880/download-csv
```

---

## 🎬 Demo Scenario

Berikut skenario yang dapat digunakan penguji untuk memverifikasi seluruh fungsi sistem secara live:

### Skenario 1 — Kondisi Normal ✅

**Pengaturan slider:**
```
Slider SpO₂  : posisi 75–100% (nilai ≥ 94%)
Slider HR    : posisi 35–65%  (nilai 60–100 bpm)
Slider Suhu  : posisi 30–50%  (nilai ≤ 37.5°C)
```
**Yang diharapkan:**
- OLED menampilkan `STATUS: Normal`
- LED **hijau** menyala, merah dan kuning OFF
- Buzzer diam
- Dashboard Node-RED: status banner **hijau**, gauge dalam zona aman
- Serial Monitor: `Status : Normal`

---

### Skenario 2 — Kondisi Warning ⚠️

**Pengaturan slider:**
```
Slider SpO₂  : posisi 30–45% (nilai ~90–93%)
Slider HR    : posisi 10–20% (nilai ~50–58 bpm)
```
atau geser **Slider Suhu** ke posisi 60–75% (nilai 37.5–38.5°C)

**Yang diharapkan:**
- OLED menampilkan `STATUS: Warning`
- LED **kuning** menyala, hijau dan merah OFF
- Buzzer tetap diam
- Dashboard Node-RED: status banner **kuning**
- Topic `alarm` menerima payload dengan `alarm_status: "Warning"`

---

### Skenario 3 — Kondisi Critical 🚨

**Pengaturan slider:**
```
Slider SpO₂  : posisi 0–15% (nilai ~85–88%)
```
atau:
```
Slider HR    : posisi 90–100% (nilai ~130–140 bpm)
```
atau:
```
Slider Suhu  : posisi 80–100% (nilai ~39–40°C)
```

**Yang diharapkan:**
- OLED menampilkan `STATUS: Critical`
- LED **merah** menyala, kuning dan hijau OFF
- **Buzzer beep** berulang setiap ±600ms
- Dashboard Node-RED: status banner **merah**
- Alarm log table di dashboard menampilkan entri baru
- Serial Monitor: `Status : Critical`

---

### Skenario 4 — Recovery ke Normal

Setelah skenario Critical, geser semua slider ke posisi tengah dan tunggu ≈3–5 detik (durasi filter moving average). Sistem kembali ke Normal — buzzer berhenti, LED hijau menyala kembali.

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

Berikut checklist penilaian yang dapat diverifikasi selama presentasi:

- [ ] Simulasi Wokwi berjalan tanpa error compile
- [ ] OLED menampilkan nilai SpO₂, HR, dan Suhu secara real-time
- [ ] LED berganti warna sesuai status saat slider digeser
- [ ] Buzzer berbunyi beep saat status Critical, diam saat Normal/Warning
- [ ] Serial Monitor menampilkan JSON payload setiap 2 detik
- [ ] Node-RED dashboard terhubung dan menerima data dari Wokwi
- [ ] Gauge SpO₂, HR, dan Suhu berubah nilai mengikuti slider
- [ ] Line chart menampilkan tren 5 menit terakhir
- [ ] Status banner berubah warna (hijau/kuning/merah) sesuai kondisi
- [ ] Alarm log table menampilkan entri saat Warning atau Critical
- [ ] File CSV tersimpan otomatis sejak pertama data masuk
- [ ] Mengakses `http://localhost:1880/download-csv` mengunduh file CSV

---

## 🔮 Future Enhancement

Berikut pengembangan lanjutan yang direncanakan setelah MVP selesai:

### 🗄️ InfluxDB — Time-Series Database

Menggantikan CSV logging dengan database time-series yang lebih scalable:
- Retensi data jangka panjang (30 hari atau lebih)
- Query agregasi: `MEAN(spo2) GROUP BY time(1m)`
- Export data yang lebih fleksibel

**Stack:** InfluxDB 1.8 (lokal) atau InfluxDB Cloud (gratis tier)

### 📈 Grafana — Advanced Analytics Dashboard

Menggantikan dashboard Node-RED untuk visualisasi yang lebih canggih:
- Panel tren multi-hari dengan zoom interaktif
- Alerting berbasis threshold langsung dari Grafana
- Anotasi kejadian kritis pada timeline

### 🌐 Multi-Patient Support

Ekspansi topic MQTT untuk mendukung lebih dari satu pasien:
```
hospital/patient/{patient_id}/vitals
hospital/patient/{patient_id}/alarm
```

### 🔒 MQTT dengan Autentikasi TLS

Mengganti public broker dengan private broker (Mosquitto) yang dilindungi TLS dan username/password.

### 📱 Mobile Notification

Integrasi dengan Telegram Bot atau WhatsApp API untuk notifikasi alarm ke perangkat mobile tenaga medis.

### 🏥 Sensor Hardware Nyata

Mengganti slider simulasi dengan sensor fisik:
- **MAX30102** untuk SpO₂ dan Heart Rate (I2C, GPIO 21/22)
- **MLX90614** untuk suhu tubuh inframerah (I2C, GPIO 21/22, alamat 0x5A)

---

## 📌 Kesimpulan

Proyek **Wearable Vital Sign Monitor** berhasil mengimplementasikan pipeline IoT end-to-end yang lengkap sesuai materi bootcamp: akuisisi data, transfer ke remote server, visualisasi dashboard, dan data logging yang dapat diunduh.

Arsitektur tiga lapis **ESP32 → MQTT Broker → Node-RED** terbukti ringan, dapat berjalan fully offline (kecuali MQTT broker publik), dan mudah diperluas ke stack yang lebih advanced seperti InfluxDB dan Grafana tanpa mengubah firmware ESP32.

Penggunaan Wokwi sebagai simulator memungkinkan seluruh pipeline didemonstrasikan tanpa hardware fisik, sementara struktur kode yang terpisah antara `Config`, `MQTT_handler`, dan `Sketch` memastikan maintainability dan kiesesuaian dengan standar bootcamp.

---

<div align="center">

**IoT Development Competition — TETI UGM Bootcamp 2026**

*Biomedical Application Category*
</div>
