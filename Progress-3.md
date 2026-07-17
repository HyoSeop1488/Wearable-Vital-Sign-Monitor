# Progress Report 3 — Wearable Vital Sign Monitor

**Proyek:** IoT Development Competition — TETI UGM Bootcamp 2026
**Periode:** Coaching 2 → Coaching 3
**Status MVP:** CLEAN (0 Critical, 0 High issues)

---

## Ringkasan

Sejak sesi coaching sebelumnya, proyek telah melalui **6 iterasi code audit** (scanning issues → fix → compile test) hingga benar-benar clean. Total ~60+ issue ditemukan dan diperbaiki, mencakup critical bugs, high-risk logic errors, medium maintainability, dan low style issues.

---

## Capaian Utama

### 1. Code Quality — 6 Iterasi sampai CLEAN

| Iterasi | Issue Ditemukan | Fokus |
|---------|----------------|-------|
| 1 | 8 (2C, 3H, 2M, 1L) | Buffer overflow, dangling pointer, logic error threshold, moving average filter |
| 2 | 4 (L1, L2, M2, M4) | getTimestamp() double call, openCsvFile() return ignored, response queue overwrite, OLED idle timer |
| 3 | 14 (1H, 6M, 7L) | lastReplayStatus persist antar sesi, trailing whitespace MQTT, off-by-one strncmp, delay(10) blocking |
| 4 | 15 (2H, 8M, 5L) | Missing include ArduinoOTA, WDT timeout setup(), String class, CSV truncation, SET_THRESHOLD error response |
| 5 | 18 (1C, 3H, 6M, 8L) | esp_task_wdt_init IDF API guard, WiFiManager WDT, reply queue overwrite, mid-replay data loss |
| 6 | 11 (0C, 0H, 8M, 3L) | Hanya defensive/minor — diverifikasi CLEAN |

### 2. Hipetermia (Low Temperature) Threshold

- Menambahkan `TEMP_WARNING_LOW` (36.0°C) dan `TEMP_CRITICAL_LOW` (35.0°C) di `Config.h`
- Hipotermia sekarang terdeteksi dengan level Warning dan Critical sendiri
- MQTT command: `SET_THRESHOLD_TEMP_WARN_LOW:<val>` dan `SET_THRESHOLD_TEMP_CRIT_LOW:<val>`
- GET_THRESHOLDS response menyertakan `temp_warn_low` dan `temp_crit_low`

### 3. Perbaikan Infrastruktur

| Area | Perbaikan |
|------|-----------|
| **Watchdog Timer** | `esp_task_wdt_reset()` ditambahkan sebelum splash, SPIFFS, WiFiManager autoConnect. Guard API untuk IDF v4.x vs v5.x |
| **MQTT Response Queue** | Overflow warning ketika command menimpa response sebelumnya; SET_THRESHOLD sekarang return error JSON saat value out of range |
| **CSV Reader** | Retry counter (max 3) untuk header read failure; buffer ditingkatkan ke 256 byte; truncation warning |
| **OLED** | Non-blocking wake (ganti delay(10) dengan timer-based); sleep idle 30 detik |
| **Data Log** | SPIFFS append failure logging; SPIFFS.remove() fallback overwrite; replay tidak delete buffer saat MQTT disconnect mid-replay |
| **NVS** | `isKey()`代替 sentinel value -1; return value `Preferences::begin()` dicek |
| **Stack Safety** | `CMD_BUF_SIZE` 256; `StaticJsonDocument` ditingkatkan ke 384; overflow check setelah serializeJson |
| **Coding Standard** | `strncpy` konsisten pakai `sizeof-1`; `#include <cmath>` ganti `<math.h>`; `#include <cctype>` ganti `<ctype.h>` |

### 4. Kompilasi

```
Sketch uses 1200070 bytes (91%) of program storage space. Maximum is 1310720 bytes.
Global variables use 54840 bytes (16%) of dynamic memory, leaving 272840 bytes for local variables. Maximum is 327680 bytes.
```

**0 error, 0 warning.**

---

## Arsitektur Saat Ini

```
vitals.csv (SPIFFS)
     │
     ▼
readCsvRow() → applyMovingAverage() → determinePatientStatus()
     │                                        │
     ▼                                        ▼
  OLED Display                         LED (R/Y/G) + Buzzer
  MQTT Publish (vitals/alarm/state)    MQTT Subscribe (cmd)
     │
     ▼
  test.mosquitto.org :1883
     │
     ▼
  Node-RED Dashboard (gauge, chart, alarm log, control)
```

### Command MQTT yang Didukung

| Command | Fungsi |
|---------|--------|
| `PING` | Response "online" ke TOPIC_PRESENCE |
| `GET_STATE` | State terakhir (retained) |
| `GET_THRESHOLDS` | Semua threshold dalam JSON |
| `SET_THRESHOLD_*:<val>` | 10 threshold dapat diubah via MQTT |
| `ACK_ALARM` | Silent buzzer hingga status berubah |
| `REBOOT` | Restart ESP32 |

### Threshold Sistem

| Parameter | Normal | Warning | Critical |
|-----------|--------|---------|----------|
| SpO₂ | ≥94% | 90-93% | <90% |
| Heart Rate | 60-100 bpm | 50-59 / 101-120 | <50 / >120 |
| Suhu (Demam) | ≤37.5°C | 37.6-38.5°C | >38.5°C |
| Suhu (Hipotermia) | ≥36.0°C | 35.0-35.9°C | <35.0°C |

---

## Pertanyaan untuk Mentor

### Arsitektur & Production Readiness

1. **Power management** — Untuk implementasi battery-powered, apakah cukup dengan OLED sleep + deep sleep ESP32 saat idle? Atau ada pendekatan yang lebih baik untuk IoT wearable?

2. **MQTT QoS** — Saat ini pakai QoS 0 (fire-and-forget). Untuk production medis, apakah perlu naik ke QoS 1 atau 2? Konsekuensi performanya bagaimana pada ESP32?

3. **Over-the-air update** — Sudah ada OTA via ArduinoOTA. Untuk production, apakah better pindah ke ESP-IDF native OTA dengan signed firmware? Atau ArduinoOTA cukup untuk skala kecil?

4. **Data integrity** — Fallback log ke SPIFFS saat offline. Apakah ada risiko korupsi file jika power loss saat SPIFFS sedang write? Solusi: LittleFS vs SPIFFS?

### Code & Testing

5. **Unit testing** — Kode ini sulit di-test karena dependensi langsung ke hardware (ESP32, SPIFFS, MQTT). Ada strategi untuk mocking atau hardware-in-the-loop testing untuk ESP32?

6. **Static analysis** — Sudah dilakukan 6 iterasi manual review. Apakah ada tool static analysis khusus ESP32/Arduino yang bisa diintegrasikan ke CI/CD (misal: cppcheck, clang-tidy)?

7. **Stack safety** — MQTT callback saat ini pakai 2×256 byte stack buffer. ESP32 loop task 8KB. Apakah ini aman untuk jangka panjang, atau perlu pindah ke heap allocation?

### Fungsional

8. **Hypothermia threshold** — Sudah menambahkan low temperature threshold. Apakah cara evaluasi status (Critical first, then Warning, hysteresis 2 siklus) sudah sesuai standar klinis? Atau perlu pendekatan berbeda untuk multiparameter?

9. **Hysteresis** — Saat ini 2 siklus (~2 detik). Apakah ini cukup untuk mencegah flapping di data real sensor (bukan CSV)? Atau perlu adaptive hysteresis?

10. **Sensor real** — Rencana migrasi dari CSV ke sensor MAX30102 + MLX90614. Apakah sampling rate dan algoritma filtering saat ini (moving average 5 sampel) cocok untuk sensor optical yang noisy?

---

## Rencana Selanjutnya

| Prioritas | Item |
|-----------|------|
| **Segera** | Dokumentasi teknis untuk coaching 3 |
| **Jangka Pendek** | Finalisasi flow Node-RED dengan alarm toast + control panel lengkap |
| **Jangka Menengah** | Migrasi ke sensor hardware nyata (MAX30102, MLX90614) |
| **Jangka Panjang** | Multi-patient support, InfluxDB + Grafana, TLS MQTT, mobile notification |
