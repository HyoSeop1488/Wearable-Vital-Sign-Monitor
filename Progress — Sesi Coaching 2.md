# 🩺 Progress — Sesi Coaching 2
### Wearable Vital Sign Monitor — IoT Dev Comp TETI 2026

---

## 📋 Ringkasan Proyek

Simulasi alat monitor pasien berbasis **ESP32** yang membaca dataset CSV, memproses data vital (SpO₂, Heart Rate, Suhu Tubuh), menentukan status pasien (Normal/Warning/Critical), menampilkan di OLED + LED + Buzzer, dan mengirimkan ke dashboard **Node-RED** via **MQTT**.

**Arsitektur:** `CSV → SPIFFS → ESP32 → MQTT Broker → Node-RED Dashboard`

---

## ✅ Progress Terselesaikan

### 0. Evolusi Data Source: Slider → Dataset CSV

**Sebelum (Wokwi Slider — fase awal):**
```
3 × Slide Potentiometer (GPIO 34, 35, 32) → ADC analogRead()
  → SpO₂ 85-100%, HR 40-140 bpm, Suhu 35-40°C
```
- Data statis (harus digeser manual)
- Tidak konsisten antar demo
- Cuma 3 nilai, tidak bisa menceritakan tren klinis

**Sesudah (Dataset CSV — sekarang):**
```
vitals.csv (100rb+ baris dari Kaggle — rename dari human_vital_signs_dataset_2024.csv)
  → Upload ke SPIFFS → readCsvRow() 1 baris/detik
    → Parse: heart_rate, spo2, temperature, timestamp
      → Moving Average → determinePatientStatus()
```
- **Data realistis** — dataset riset asli, bukan asal geser
- **Loop otomatis** — abis balik ke awal, simulasi jalan terus
- **Konsisten** — demo bisa diulang dengan hasil identik
- **Siap presentasi** — tinggal Start, dataset jalan sendiri
- **Timestamp asli** — bisa lihat tren pasien memburuk dari waktu ke waktu

**Yang berubah di kode:**
| Komponen | Slider (dulu) | CSV (sekarang) |
|---|---|---|
| Source | `analogRead()` | `readCsvRow()` dari SPIFFS |
| Init | Setup ADC | `SPIFFS.begin()` + `openCsvFile()` |
| Loop | Baca ADC → filter | Baca CSV → filter |
| Handling | — | EOF loop, parse error, file not found |
| Demo | Geser slider manual | **Start & jalan sendiri** |

### 1. Fungsionalitas Inti

| Fitur | Detail |
|---|---|
| **Dataset Playback** | Baca CSV dari SPIFFS, 1 baris/detik, loop otomatis ke awal saat EOF |
| **Moving Average Filter** | Buffer 5 sampel — transisi status lebih halus |
| **Three-level Alert** | Critical > Warning > Normal dengan hysteresis 2 siklus cegah flapping |
| **LED Indikator** | Merah (Critical), Kuning (Warning), Hijau (Normal) — GPIO 25, 33, 26 |
| **Buzzer** | Non-blocking: Critical rapid 1200Hz/600ms, Warning slow 800Hz/2000ms, silent via ACK |
| **OLED SSD1306** | Tampil SpO₂, HR, suhu, status, indikator MQTT — sleep 30s idle, wake saat ada perubahan |

### 2. Konektivitas

| Fitur | Detail |
|---|---|
| **WiFiManager** | AutoConnect dengan saved credentials, AP portal fallback, fallback ke Wokwi-GUEST |
| **MQTT** | Broker `test.mosquitto.org:1883`, auto reconnect, re-subscribe, re-publish state |
| **Unique Client ID** | Generate dari MAC address (`esp32_patient_XXXXXXXX`) |
| **LWT** | Broker publish "offline" ke TOPIC_PRESENCE jika ESP mati mendadak |
| **State Retained** | Subscriber baru langsung dapat status terakhir |
| **Data Log Fallback** | Simpan data ke SPIFFS saat MQTT offline, replay otomatis setelah reconnect |
| **Log Rotation** | Maks 500 baris, otomatis reset jika penuh |
| **OTA Update** | Update firmware via WiFi tanpa kabel USB |
| **Watchdog Timer** | Reset otomatis jika loop hang >15 detik |

### 3. Remote Configuration (via MQTT Command)

| Command | Fungsi |
|---|---|
| `PING` | Cek koneksi — response ke TOPIC_PRESENCE |
| `REBOOT` | Restart ESP32 dalam 1 detik |
| `GET_STATE` | Minta status pasien terkini |
| `GET_THRESHOLDS` | Lihat semua nilai threshold dalam JSON |
| `SET_THRESHOLD_SPO2_WARN:<val>` | Ubah threshold SpO₂ Warning (range 80-100) |
| `SET_THRESHOLD_SPO2_CRIT:<val>` | Ubah threshold SpO₂ Critical (range 80-100) |
| `SET_THRESHOLD_HR_WARN_LOW:<val>` | Ubah threshold HR Warning Low (20-250) |
| `SET_THRESHOLD_HR_WARN_HIGH:<val>` | Ubah threshold HR Warning High (20-250) |
| `SET_THRESHOLD_HR_CRIT_LOW:<val>` | Ubah threshold HR Critical Low (20-250) |
| `SET_THRESHOLD_HR_CRIT_HIGH:<val>` | Ubah threshold HR Critical High (20-250) |
| `SET_THRESHOLD_TEMP_WARN:<val>` | Ubah threshold Suhu Warning (34-42) |
| `SET_THRESHOLD_TEMP_CRIT:<val>` | Ubah threshold Suhu Critical (34-42) |
| `ACK_ALARM` | Silent buzzer sampai status berubah |

### 4. Persistence

| Fitur | Detail |
|---|---|
| **NVS Threshold Save** | Semua threshold tersimpan ke NVS dengan debounce 5s cegah flash wear |
| **NVS Threshold Load** | Threshold otomatis terbaca saat boot — tetap setelah reboot/power cycle |
| **Data Log Persistence** | Data saat offline tetap aman di SPIFFS meskipun reboot |

### 5. Code Quality

| Aspek | Detail |
|---|---|
| **Modular** | Config (konstanta) / MQTT_handler (koneksi) / Sketch (logika utama) |
| **No Heap Fragmentation** | StaticJsonDocument, char buffer (bukan String), stack allocation |
| **No Duplication** | Macro HANDLE_SET_THRESHOLD_FLOAT/INT untuk 8 blok identik |
| **NVS Macros** | NVS_SAVE/LOAD_FLOAT/INT — DRY untuk 16 operasi save/load |
| **Re-entrancy Safe** | Response queue untuk MQTT publish dari callback |
| **Case-Insensitive** | stristr() — command bisa PING/ping/Ping |
| **No Magic Numbers** | CMD_BUF_SIZE, OLED position constants, validation range constants |
| **Static Init Safe** | Hindari static init order fiasco (pakai string literal, bukan extern cross-TU) |

### 6. Infrastructure

| Item | Detail |
|---|---|
| **Wokwi diagram** | ESP32 + OLED SSD1306 + 3 LED + 3 resistor + Buzzer |
| **Node-RED Flow** | Dashboard lengkap: gauge, chart, status banner, alarm table, control buttons |
| **GitHub** | Terpush ke `github.com/HyoSeop1488/Wearable-Vital-Sign-Monitor` |
| **Struktur Folder** | `Wokwi/` (firmware) + `Node-Red/` (dashboard) + `README.md` |

---

## 📊 Threshold Status Pasien

| Parameter | Normal | Warning | Critical |
|---|---|---|---|
| **SpO₂** | ≥ 94% | 90% ≤ spo2 < 94% | < 90% |
| **Heart Rate** | 60–100 bpm | 50–59 atau 101–120 bpm | < 50 atau > 120 bpm |
| **Suhu** | ≤ 37.5°C | 37.6–38.5°C | > 38.5°C |

---

## 📁 Struktur File

```
Project IOT/
├── README.md
├── Progress — Sesi Coaching 2.md
├── Wokwi/
│   ├── Sketch.ino
│   ├── Config.h / Config.cpp
│   ├── MQTT_handler.h / MQTT_handler.cpp
│   ├── diagram.json
│   ├── libraries.txt
│   ├── wokwi.toml
│   └── vitals.csv
└── Node-Red/
    └── flows.json
```

---

## ❓ Pertanyaan untuk Mentor

### A. Pilihan Broker MQTT
**Mosquitto (private) vs HiveMQ (public) — mana yang lebih direkomendasikan?**

| Aspek | Mosquitto (Private) | HiveMQ (Public) |
|---|---|---|
| Setup | Install sendiri (Docker/VPS) | Gratis, langsung daftar |
| Keamanan | TLS + username/password full control | Terbatas |
| Latency | Lokal / VPS — minimal | via internet |
| Cocok | **Real hardware / production** | **Demo / Wokwi** |
| Offline | Bisa akses via LAN | Harus internet |

Rencana: Saat coaching pakai `test.mosquitto.org` dulu. Untuk final, mungkin pindah ke Mosquitto private. Apakah saran mentor?

### B. Arsitektur & Penilaian
1. Apakah arsitektur MVP saat ini sudah cukup untuk standar penilaian kompetisi?
2. Prioritas enhancement paling bernilai: sensor nyata, multi-patient, atau mobile notification?
3. Untuk real hardware, apakah MAX30102 + MLX90614 bisa sharing I2C (SDA/SCL) tanpa multiplexer?

### C. Teknis
4. Dataset dari Kaggle — apakah kredibel untuk presentasi atau perlu bikin data sintetik sendiri?
5. Power management: deep sleep atau cukup OLED sleep + WiFi power save?
6. Metode testing ESP32 selain Serial Monitor — apa yang praktis di Wokwi?

---

---

## 🎯 Kesimpulan

Proyek **Wearable Vital Sign Monitor** telah menyelesaikan seluruh fungsionalitas MVP. Perubahan paling signifikan adalah **migrasi data source dari 3 slider potentiometer (ADC manual) ke dataset CSV Kaggle (100rb+ baris, loop otomatis)** — meningkatkan kredibilitas demo karena data realistis, konsisten, dan tidak perlu interaksi manual saat presentasi. Pipeline end-to-end dari CSV → SPIFFS → ESP32 → MQTT → Node-RED berjalan, three-level alert + buzzer + OLED berfungsi, remote threshold configuration via MQTT siap, dan data logging fallback + replay otomatis telah terimplementasi. Kode telah melalui 7 siklus scanning & fixing — **zero remaining issues**. Fokus sesi coaching: validasi arsitektur oleh mentor, keputusan broker MQTT untuk final, dan prioritas enhancement pasca-MVP.

---

## 📌 Catatan Tambahan

- **Total issues ditemukan & diperbaiki:** 55+ (bugs, duplikasi, logic gaps, maintainability, security)
- **Loop scanning:** 7 siklus scan-fix sampai CLEAN (zero issues)
- **CSV filename:** `/vitals.csv` — nama pendek, aman dari batas 32 karakter SPIFFS
- **MQTT_MAX_PACKET_SIZE:** Ditingkatkan dari 256 → 512 agar payload GET_THRESHOLDS tidak terpotong
