/**
 * @file    Wokwi.ino
 * @brief   Program utama Wearable Vital Sign Monitor
 * @project IoT Dev Comp TETI 2026
 *
 * Arsitektur MVP:
 *   ESP32 --> MQTT Broker --> Node-RED Dashboard
 *
 * Sumber data:
 *   Dataset CSV Kaggle "Human Vital Signs Dataset 2024"
 *   File: vitals.csv (nama pendek agar ≤32 char — SPIFFS limit)
 *   Kolom: heart_rate, spo2, temperature, timestamp
 *   Dibaca dari SPIFFS, 1 baris per detik, loop ke awal saat EOF.
 *
 * Cara upload CSV ke Wokwi:
 *   1. Buka project di wokwi.com
 *   2. Klik dropdown tab (panah di samping tab terakhir)
 *   3. Pilih "Upload file(s)..."
 *   4. Upload file vitals.csv
 *   5. File akan muncul sebagai tab baru dan tersedia di SPIFFS
 *
 * Libraries:
 *   - Adafruit SSD1306
 *   - Adafruit GFX Library
 *   - PubSubClient
 *   - ArduinoJson
 */

#include <Wire.h>
#include <SPIFFS.h>
#include <cmath>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <esp_task_wdt.h>
#include <ArduinoOTA.h>
#include "Config.h"
#include "MQTT_handler.h"

/* ------------------------------------------------------------------ */
/*  Objek OLED                                                         */
/* ------------------------------------------------------------------ */
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);
static bool g_oledAvailable = false;

/* ------------------------------------------------------------------ */
/*  Handle file CSV                                                    */
/* ------------------------------------------------------------------ */
static File g_csvFile;  /* default-constructed (invalid/closed) */

/* ------------------------------------------------------------------ */
/*  Buffer moving average filter                                       */
/* ------------------------------------------------------------------ */
float g_spo2Buffer[FILTER_SIZE] = {0};
int   g_hrBuffer[FILTER_SIZE]   = {0};
float g_tempBuffer[FILTER_SIZE] = {0};
int   g_filterIndex             = 0;

/* ------------------------------------------------------------------ */
/*  State sensor (hasil setelah filter)                                */
/* ------------------------------------------------------------------ */
float g_spo2Value         = 98.0f;
int   g_heartRate         = 75;
float g_tempValue         = 36.5f;
char  g_patientStatus[16] = "Normal";
char  g_lastStatus[16]    = "Normal";

/* ------------------------------------------------------------------ */
/*  Hysteresis counter untuk cegah flapping status                     */
/* ------------------------------------------------------------------ */
#define HYSTERESIS_COUNT 2
static uint16_t g_hysteresisCount = 0;

/* ------------------------------------------------------------------ */
/*  Timing                                                             */
/* ------------------------------------------------------------------ */
unsigned long g_lastPublish           = 0;
unsigned long g_lastCriticalBuzzer    = 0;
unsigned long g_lastWarningBuzzer     = 0;
unsigned long g_lastOledActivity      = 0;
bool          g_oledSleeping          = false;

/* ------------------------------------------------------------------ */
/*  Data log fallback (MQTT offline buffer)                             */
/* ------------------------------------------------------------------ */
static bool   g_wasMqttOffline = true;
/* G3: g_logLineCount diinisialisasi dari file existing agar rotation
   tetap akurat setelah crash/reboot. Diupdate di logToSpiffs & replay. */
static size_t g_logLineCount = 0;

/* ------------------------------------------------------------------ */
/*  Forward declarations                                               */
/* ------------------------------------------------------------------ */
bool openCsvFile();
bool readCsvRow();
void applyMovingAverage();
void determinePatientStatus();
void handleActuators();
void updateOled();
void printSerial();
void logToSpiffs(float spo2, int hr, float temp, const char* status, const char* ts);
void replayDataLog();
const char* getTimestamp();

/* ================================================================== */
/*  SETUP                                                              */
/* ================================================================== */
void setup() {
  Serial.begin(115200);

  /* Daftarkan loop task ke WDT (WDT sudah diinit ESP-IDF dengan timeout default).
     Nanti dilepas sementara di connectWifi() agar WiFiManager blocking aman. */
  enableLoopWDT();

  /* Inisialisasi pin aktuator */
  pinMode(PIN_LED_RED,    OUTPUT);
  pinMode(PIN_LED_YELLOW, OUTPUT);
  pinMode(PIN_LED_GREEN,  OUTPUT);
  pinMode(PIN_BUZZER,     OUTPUT);
  digitalWrite(PIN_LED_RED,    LOW);
  digitalWrite(PIN_LED_YELLOW, LOW);
  digitalWrite(PIN_LED_GREEN,  LOW);

  /* Inisialisasi I2C dan OLED */
  Wire.begin(OLED_SDA, OLED_SCL);
  if (!display.begin(SSD1306_SWITCHCAPVCC, OLED_ADDRESS)) {
    Serial.println("[ERROR] OLED tidak ditemukan!");
  } else {
    g_oledAvailable = true;
  }

  /* Splash screen */
  if (g_oledAvailable) {
    esp_task_wdt_reset();
    display.clearDisplay();
    display.setTextColor(WHITE);
    display.setTextSize(1);
    display.setCursor(14, 10);
    display.println("Vital Sign Monitor");
    display.setCursor(20, 24);
    display.println("IoT Dev Comp TETI");
    display.setCursor(36, 38);
    display.println("2026 - MVP");
    display.drawRect(0, 0, 128, 64, WHITE);
    display.display();
    delay(2000);
  }

  g_lastOledActivity = millis();

  /* Inisialisasi SPIFFS */
  esp_task_wdt_reset();
  if (!SPIFFS.begin(true)) {
    Serial.println("[ERROR] SPIFFS gagal di-mount!");
  }

  /* G3: Hitung baris existing data log agar rotation threshold akurat */
  if (SPIFFS.exists(DATA_LOG_PATH)) {
    File logFile = SPIFFS.open(DATA_LOG_PATH, FILE_READ);
    if (logFile) {
      char buf[128];
      while (logFile.available()) {
        size_t n = logFile.readBytesUntil('\n', buf, sizeof(buf) - 1);
        if (n > 0) g_logLineCount++;
        /* Skip sisa baris jika buffer penuh */
        while (n == sizeof(buf) - 1 && logFile.available()) {
          n = logFile.readBytesUntil('\n', buf, sizeof(buf) - 1);
        }
      }
      logFile.close();
      Serial.printf("[LOG]   Existing log: %zu baris\n", g_logLineCount);
    }
  }

  /* Buka file CSV dari SPIFFS */
  if (!openCsvFile()) {
    Serial.println("[ERROR] File CSV tidak ditemukan!");
    Serial.print("[ERROR] Pastikan ");
    Serial.print(CSV_FILE_PATH);
    Serial.println(" sudah diupload ke Wokwi.");
  }

  /* Seed moving average buffer dari baris CSV pertama */
  if (readCsvRow()) {
    for (int i = 0; i < FILTER_SIZE; i++) {
      g_hrBuffer[i]   = g_hrBuffer[0];
      g_spo2Buffer[i] = g_spo2Buffer[0];
      g_tempBuffer[i] = g_tempBuffer[0];
    }
    g_filterIndex = 0;
    Serial.println("[FILTER] Buffer di-seed dari baris CSV pertama");
  }

  /* Inisialisasi koneksi WiFi dan MQTT */
  esp_task_wdt_reset();
  mqttSetup();

  /* OTA: update firmware over WiFi tanpa kabel USB */
  ArduinoOTA.setHostname(OTA_HOSTNAME);
  ArduinoOTA.setPassword(OTA_PASSWORD);
  ArduinoOTA.onStart([]() {
    const char* type = ArduinoOTA.getCommand() == U_FLASH ? "sketch" : "filesystem";
    Serial.printf("[OTA] Mulai update %s\n", type);
  });
  ArduinoOTA.onEnd([]() {
    Serial.println("[OTA] Selesai");
  });
  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
    Serial.printf("[OTA] Progress: %u%%\r", total > 0 ? progress * 100 / total : 0);
  });
  ArduinoOTA.onError([](ota_error_t error) {
    Serial.printf("[OTA] Error %d\n", error);
  });
  ArduinoOTA.begin();

  /* Watchdog sudah diinit di awal setup() */

  Serial.println("[READY] Sistem aktif -- mode dataset CSV");
  Serial.print("[CSV]   File: ");
  Serial.println(CSV_FILE_PATH);
  Serial.println("[CSV]   Interval: 1 baris/detik, loop otomatis ke awal");
}

/* ================================================================== */
/*  LOOP                                                               */
/* ================================================================== */
void loop() {
  ArduinoOTA.handle();
  esp_task_wdt_reset();

  mqttLoop();

  /* Deteksi reconnect MQTT → replay data log yang terlewat */
  if (mqttIsConnected() && g_wasMqttOffline) {
    g_wasMqttOffline = false;
    replayDataLog();
  }
  if (!mqttIsConnected()) {
    g_wasMqttOffline = true;
  }

  /* Buzzer non-blocking diproses setiap iterasi */
  handleActuators();

  if ((unsigned long)(millis() - g_lastPublish) >= PUBLISH_INTERVAL) {
    if (g_csvFile) {
      if (readCsvRow()) {
        applyMovingAverage();
        determinePatientStatus();
        printSerial();
        /* updateOled() dipanggil di luar if agar tetap refresh walau
           readCsvRow gagal (OLED tidak tampilkan data basi) */

        const char* ts = getTimestamp();
        bool published = mqttPublishVitals(g_spo2Value, g_heartRate, g_tempValue, g_patientStatus, ts);

        /* Jika MQTT offline, simpan data ke SPIFFS sebagai buffer */
        if (!published) {
          logToSpiffs(g_spo2Value, g_heartRate, g_tempValue, g_patientStatus, ts);
        }

        /* Transisi status: update state + publish event */
        if (strcmp(g_patientStatus, g_lastStatus) != 0) {
          strncpy(g_lastStatus, g_patientStatus, sizeof(g_lastStatus) - 1);
          g_lastStatus[sizeof(g_lastStatus) - 1] = '\0';

          /* Reset alarm ack saat status berubah */
          mqttResetAlarmAck();
          /* Reset OLED idle timer — status berubah, tampilkan lagi */
          g_lastOledActivity = millis();

          /* State retained: subscriber baru langsung dapat status terakhir */
          mqttSetState(g_patientStatus);

          if (mqttIsConnected()) {
            mqttPublishState(g_patientStatus);

            /* Event alarm (not retained): hanya saat transisi ke non-Normal */
            if (strcmp(g_patientStatus, STATUS_NORMAL) != 0) {
              mqttPublishAlarm(g_patientStatus, g_spo2Value, g_heartRate, g_tempValue, ts);
            }
          }
        }
      } else {
        /* M6: Reset hysteresis counter saat CSV read gagal */
        g_hysteresisCount = 0;
      }
      g_lastOledActivity = millis();
      updateOled();
    } else {
      updateOled();
    }

    g_lastPublish = millis();
  }

  /* Non-blocking yield — beri waktu untuk task RTOS lain */
  delay(1);
}

/* ================================================================== */
/*  openCsvFile                                                        */
/*  Buka file CSV dari SPIFFS dan lewati baris header.                */
/*  Format header: heart_rate,spo2,temperature,timestamp              */
/*  Kembalikan true jika berhasil, false jika file tidak ada.         */
/* ================================================================== */
bool openCsvFile() {
  g_csvFile = SPIFFS.open(CSV_FILE_PATH, FILE_READ);
  if (!g_csvFile) return false;

  /* Lewati baris header (pakai buffer, bukan String → L2) */
  char _header[256];
  size_t _hdrLen = g_csvFile.readBytesUntil('\n', _header, sizeof(_header) - 1);
  _header[_hdrLen] = '\0';
  Serial.println("[CSV]   File terbuka, header dilewati");
  return true;
}

const char* getTimestamp() {
  /* LIMITASI: millis() wrap ~49.7 hari — timestamp reset ke SIM-00:00:00.
     Untuk production >49 hari, gunakan RTC eksternal atau NTP. */
  static char ts[25];
  unsigned long now = millis();
  unsigned long sec = now / 1000;
  unsigned long min = sec / 60;
  unsigned long hour = min / 60;
  snprintf(ts, sizeof(ts), "SIM-%02lu:%02lu:%02lu", hour % 24, min % 60, sec % 60);
  return ts;
}

/* ================================================================== */
/*  readCsvRow                                                         */
/*  Baca satu baris CSV dari SPIFFS dan parse empat kolom:            */
/*    heart_rate, spo2, temperature, timestamp                        */
/*                                                                    */
/*  Format baris: heart_rate,spo2,temperature,timestamp               */
/*  Contoh      : 72,97.5,36.8,2024-07-19 21:53:45                   */
/*                                                                    */
/*  Saat EOF tercapai, seek ke awal dan lewati header kembali --      */
/*  dataset loop tak terbatas selama simulasi berjalan.               */
/*                                                                    */
/*  Parsing menggunakan char buffer (bukan String) untuk menghindari  */
bool readCsvRow() {
  static uint8_t g_csvHeaderFail = 0;
  if (!g_csvFile) return false;

  /* EOF -- loop ke awal dataset */
  if (!g_csvFile.available()) {
    Serial.println("[CSV]   EOF -- loop ke awal dataset");
    g_hysteresisCount = 0;
    if (!g_csvFile.seek(0)) {
      Serial.println("[CSV]   Seek gagal, buka ulang file...");
      g_csvFile.close();
      if (!openCsvFile()) {
        Serial.println("[CSV]   Recovery open gagal!");
      }
      return false;
    }
    {
      char _hdr[256];
      size_t _n = g_csvFile.readBytesUntil('\n', _hdr, sizeof(_hdr) - 1);
      if (_n == 0) {
        if (++g_csvHeaderFail > 3) {
          Serial.println("[CSV]   FATAL: Header gagal 3x berturut-turut — stop CSV");
          g_csvFile.close();
          g_csvFile = File();
          return false;
        }
        Serial.println("[CSV]   Header read gagal!");
        return false;
      }
      g_csvHeaderFail = 0;
    }
  }

  /* Baca satu baris ke char buffer */
  char line[256];
  size_t len = g_csvFile.readBytesUntil('\n', line, sizeof(line) - 1);
  line[len] = '\0';
  if (len == sizeof(line) - 1) {
    Serial.printf("[CSV]   WARN: Line truncated at %zu chars\n", len);
  }

  /* Trim trailing whitespace */
  while (len > 0 && (line[len - 1] == ' ' || line[len - 1] == '\r')) {
    line[--len] = '\0';
  }

  if (len == 0) return false;

  /* Parse CSV: heart_rate, spo2, temperature */
  int   rawHr   = 0;
  float rawSpo2 = 0.0f;
  float rawTemp = 0.0f;

  int parsed = sscanf(line, "%d,%f,%f", &rawHr, &rawSpo2, &rawTemp);
  if (parsed < 3) {
    Serial.printf("[CSV]   Parse error (fields=%d): %s\n", parsed, line);
    return false;
  }

  /* Validasi range nilai fisiologis
     Bounds ini sengaja lebih longgar dari threshold Config.h (HR_VALID_MIN/MAX,
     SPO2_VALID_MIN/MAX, TEMP_VALID_MIN/MAX) karena untuk plausibility dataset:
     reject data rusak, bukan untuk threshold alarm. */
  if (rawHr < HR_VALID_MIN || rawHr > HR_VALID_MAX) return false;
  if (rawSpo2 < 50.0f || rawSpo2 > 100.0f) return false;
  if (rawTemp < TEMP_VALID_MIN || rawTemp > TEMP_VALID_MAX) return false;

  /* Simpan ke buffer moving average */
  g_hrBuffer[g_filterIndex]   = rawHr;
  g_spo2Buffer[g_filterIndex] = rawSpo2;
  g_tempBuffer[g_filterIndex] = rawTemp;
  g_filterIndex = (g_filterIndex + 1) % FILTER_SIZE;

  return true;
}

/* ================================================================== */
/*  applyMovingAverage                                                 */
/*  Hitung rata-rata buffer FILTER_SIZE sampel untuk transisi         */
/*  status yang lebih halus antar baris dataset.                      */
/* ================================================================== */
void applyMovingAverage() {
  float sumSpo2 = 0;
  int   sumHr   = 0;
  float sumTemp = 0;

  for (int i = 0; i < FILTER_SIZE; i++) {
    sumSpo2 += g_spo2Buffer[i];
    sumHr   += g_hrBuffer[i];
    sumTemp += g_tempBuffer[i];
  }

  g_spo2Value = sumSpo2 / FILTER_SIZE;
  g_heartRate = (int)round((float)sumHr / FILTER_SIZE);
  g_tempValue = sumTemp / FILTER_SIZE;
}

/* ================================================================== */
/*  determinePatientStatus                                             */
/*  Evaluasi threshold dua level (Critical > Warning > Normal)        */
/*  dengan hysteresis untuk mencegah flapping status.                 */
/*                                                                    */
/*  Hysteresis: status berubah hanya jika HYSTERESIS_COUNT kali      */
/*  berturut-turut berada di level yang berbeda.                     */
/* ================================================================== */
void determinePatientStatus() {
  /* N7: Reset counter jika data tidak valid (g_csvFile == false) —
     mencegah carry-over outlier dari sesi sebelumnya */
  if (!g_csvFile) { g_hysteresisCount = 0; return; }

  bool isCritical = (g_spo2Value < SPO2_CRITICAL_LOW)  ||
                    (g_heartRate < HR_CRITICAL_LOW)     ||
                    (g_heartRate > HR_CRITICAL_HIGH)    ||
                    (g_tempValue > TEMP_CRITICAL_HIGH)  ||
                    (g_tempValue < TEMP_CRITICAL_LOW);

  bool isWarning  = (g_spo2Value < SPO2_WARNING_LOW)   ||
                    (g_heartRate < HR_WARNING_LOW)      ||
                    (g_heartRate > HR_WARNING_HIGH)     ||
                    (g_tempValue > TEMP_WARNING_HIGH)   ||
                    (g_tempValue < TEMP_WARNING_LOW);

  /* Tentukan level yang seharusnya */
  char desired[16];
  if (isCritical)      strncpy(desired, STATUS_CRITICAL, sizeof(desired) - 1);
  else if (isWarning)  strncpy(desired, STATUS_WARNING,  sizeof(desired) - 1);
  else                 strncpy(desired, STATUS_NORMAL,   sizeof(desired) - 1);
  desired[sizeof(desired) - 1] = '\0';

  /* Hysteresis: hanya berubah jika level baru bertahan beberapa siklus */
  if (strcmp(desired, g_patientStatus) != 0) {
    g_hysteresisCount++;
    if (g_hysteresisCount >= HYSTERESIS_COUNT) {
      strncpy(g_patientStatus, desired, sizeof(g_patientStatus) - 1);
      g_patientStatus[sizeof(g_patientStatus) - 1] = '\0';
      g_hysteresisCount = 0;
    }
  } else {
    g_hysteresisCount = 0;
  }
}

/* ================================================================== */
/*  handleActuators                                                    */
/*  Kontrol LED tiga warna dan buzzer sesuai g_patientStatus.         */
/*  Buzzer dijalankan secara non-blocking menggunakan millis().        */
/* ================================================================== */
void handleActuators() {
  bool alarmAcked = mqttIsAlarmAcknowledged();

  if (strcmp(g_patientStatus, STATUS_CRITICAL) == 0) {
    digitalWrite(PIN_LED_RED,    HIGH);
    digitalWrite(PIN_LED_YELLOW, LOW);
    digitalWrite(PIN_LED_GREEN,  LOW);

    /* Critical: rapid beep 1200Hz setiap 600ms */
    if (!alarmAcked && (unsigned long)(millis() - g_lastCriticalBuzzer) >= BUZZER_CRITICAL_INTERVAL) {
      tone(PIN_BUZZER, BUZZER_CRITICAL_FREQ, BUZZER_CRITICAL_DURATION);
      g_lastCriticalBuzzer = millis();
    }
    if (alarmAcked) {
      noTone(PIN_BUZZER);
    }

  } else if (strcmp(g_patientStatus, STATUS_WARNING) == 0) {
    digitalWrite(PIN_LED_RED,    LOW);
    digitalWrite(PIN_LED_YELLOW, HIGH);
    digitalWrite(PIN_LED_GREEN,  LOW);

    /* Warning: slow beep 800Hz setiap 2000ms */
    if (!alarmAcked && (unsigned long)(millis() - g_lastWarningBuzzer) >= BUZZER_WARNING_INTERVAL) {
      tone(PIN_BUZZER, BUZZER_WARNING_FREQ, BUZZER_WARNING_DURATION);
      g_lastWarningBuzzer = millis();
    }
    if (alarmAcked) {
      noTone(PIN_BUZZER);
    }

  } else {
    digitalWrite(PIN_LED_RED,    LOW);
    digitalWrite(PIN_LED_YELLOW, LOW);
    digitalWrite(PIN_LED_GREEN,  HIGH);
    noTone(PIN_BUZZER);
  }
}

/* ================================================================== */
/*  updateOled                                                         */
/*  Render tampilan OLED: header status, nilai SpO2, HR, suhu,        */
/*  dan indikator koneksi MQTT di pojok kanan atas.                   */
/*  Jika file CSV tidak ditemukan, tampilkan pesan error.              */
/* ================================================================== */
void updateOled() {
  if (!g_oledAvailable) return;
  unsigned long now = millis();

  /* Sleep mode: matikan OLED jika tidak ada perubahan status > timeout */
  /* L3: Gunakan display.ssd1306_command langsung karena Adafruit SSD1306
     tidak memiliki API sleep/wakeup publik. SSD1306_DISPLAYOFF/ON adalah
     opcode standar untuk semua controller SSD1306/SH1106. */
  if ((unsigned long)(now - g_lastOledActivity) >= OLED_IDLE_TIMEOUT) {
    if (!g_oledSleeping) {
      display.ssd1306_command(SSD1306_DISPLAYOFF);
      g_oledSleeping = true;
    }
    return;
  }

  /* Wake up display jika sedang sleep */
  if (g_oledSleeping) {
    display.ssd1306_command(SSD1306_DISPLAYON);
    delay(1);  /* stabilisasi charge pump */
    g_oledSleeping = false;
    g_lastOledActivity = millis();
  }

  display.clearDisplay();
  display.setTextColor(WHITE);

  if (!g_csvFile) {
    display.setTextSize(1);
    display.setCursor(10, 10);
    display.println("ERROR: CSV file");
    display.setCursor(10, 24);
    display.println("not found!");

    display.setTextColor(WHITE);
    display.setCursor(OLED_MQTT_X, OLED_MQTT_Y);
    display.print(mqttIsConnected() ? "*" : "x");

    display.display();
    return;
  }

  /* Header bar -- status pasien */
  display.fillRect(0, 0, SCREEN_WIDTH, OLED_STATUS_H, WHITE);
  display.setTextColor(BLACK);
  display.setTextSize(1);
  display.setCursor(OLED_STATUS_X, OLED_STATUS_Y);
  display.print("STATUS: ");
  display.print(g_patientStatus);
  /* Indikator MQTT di-draw sebelum textColor diubah ke WHITE —
     agar tetap terlihat di atas background putih fillRect */
  display.setCursor(OLED_MQTT_X, OLED_MQTT_Y);
  display.print(mqttIsConnected() ? "*" : "x");
  display.setTextColor(WHITE);

  /* SpO2 */
  display.setTextSize(1);
  display.setCursor(OLED_LABEL_X, OLED_SPO2_Y);
  display.print("SpO2");
  display.setCursor(OLED_VALUE_X, OLED_SPO2_Y - 2);
  display.setTextSize(2);
  display.print((int)round(g_spo2Value));
  display.setTextSize(1);
  display.print("%");

  /* Heart Rate */
  display.setTextSize(1);
  display.setCursor(OLED_LABEL_X, OLED_HR_Y);
  display.print("HR");
  display.setCursor(OLED_VALUE_X, OLED_HR_Y - 2);
  display.setTextSize(2);
  display.print(g_heartRate);
  display.setTextSize(1);
  display.print("bpm");

  /* Suhu Tubuh */
  display.setTextSize(1);
  display.setCursor(OLED_LABEL_X, OLED_TEMP_Y);
  display.print("Temp");
  display.setCursor(OLED_VALUE_X, OLED_TEMP_Y - 2);
  display.setTextSize(2);
  display.print(g_tempValue, 1);
  display.setTextSize(1);
  display.print("C");

  /* Indikator MQTT sudah di-draw di header bar di atas */
  display.display();
}

/* ================================================================== */
/*  printSerial                                                        */
/*  Cetak ringkasan nilai sensor dan status ke Serial Monitor          */
/*  setiap PUBLISH_INTERVAL ms.                                       */
/* ================================================================== */
void printSerial() {
  Serial.print("\r\n==============================\r\n");
  Serial.printf("SpO2: %.1f%% | HR: %d bpm | Suhu: %.1f C | Status: %s | MQTT: %s\r\n",
                g_spo2Value, g_heartRate, g_tempValue, g_patientStatus,
                mqttIsConnected() ? "Connected" : "Offline");
  Serial.print("==============================\r\n");
}

/* ================================================================== */
/*  logToSpiffs                                                        */
/*  Simpan data vital ke SPIFFS saat MQTT offline agar tidak hilang.   */
/*  Format CSV: spo2,heart_rate,temperature,status,timestamp           */
/*  Max DATA_LOG_MAX_LINES baris — jika penuh, rotate (hapus + start   */
/*  baru) agar SPIFFS tidak overflow dan data terbaru tetap tercatat.  */
/* ================================================================== */
void logToSpiffs(float spo2, int hr, float temp, const char* status, const char* ts) {
  if (g_logLineCount >= DATA_LOG_MAX_LINES) {
    SPIFFS.remove(DATA_LOG_PATH);
    g_logLineCount = 0;
    Serial.println("[LOG]   Log rotated (max lines reached, file reset)");
  }

  File logFile = SPIFFS.open(DATA_LOG_PATH, FILE_APPEND);
  if (!logFile) {
    Serial.println("[LOG]   ERROR: Gagal buka file untuk append!");
    return;
  }

  char line[128];
  snprintf(line, sizeof(line), "%.1f,%d,%.1f,%s,%s\n", spo2, hr, temp, status, ts ? ts : "");
  logFile.print(line);
  logFile.close();

  g_logLineCount++;
  line[strcspn(line, "\n")] = 0;
  Serial.printf("[LOG] Data tersimpan (%zu/%zu): %s\r\n", g_logLineCount, DATA_LOG_MAX_LINES, line);
}

/* ================================================================== */
/*  replayDataLog                                                      */
/*  Setelah MQTT reconnect, baca data log yang terlewat dan publish    */
/*  ulang ke broker, lalu hapus file log.                              */
/* ================================================================== */
void replayDataLog() {
  if (!SPIFFS.exists(DATA_LOG_PATH)) return;

  File logFile = SPIFFS.open(DATA_LOG_PATH, FILE_READ);
  if (!logFile) return;

  size_t size = logFile.size();
  if (size == 0) {
    logFile.close();
    if (SPIFFS.remove(DATA_LOG_PATH)) {
      g_logLineCount = 0;
    } else {
      /* Jika remove gagal, overwrite file dengan konten kosong */
      File emptyFile = SPIFFS.open(DATA_LOG_PATH, FILE_WRITE);
      if (emptyFile) emptyFile.close();
      g_logLineCount = 0;
    }
    return;
  }

  Serial.printf("[LOG] Replay %zu bytes data log...\r\n", size);

  char line[128];
  int replayed = 0;
  static char lastReplayStatus[16] = "";
  if (lastReplayStatus[0] == '\0') {
    strncpy(lastReplayStatus, g_patientStatus, sizeof(lastReplayStatus) - 1);
    lastReplayStatus[sizeof(lastReplayStatus) - 1] = '\0';
  }
  while (logFile.available()) {
    esp_task_wdt_reset();
    mqttLoopOnce();
    ArduinoOTA.handle();
    size_t len = logFile.readBytesUntil('\n', line, sizeof(line) - 1);
    line[len] = '\0';
    if (len == 0) continue;

    int   hr   = 0;
    float spo2 = 0.0f, temp = 0.0f;
    char  status[16] = "", ts[25] = "";
    int parsed = sscanf(line, "%f,%d,%f,%15[^,],%24[^\n]", &spo2, &hr, &temp, status, ts);

    if (!mqttIsConnected()) {
      Serial.printf("[LOG] MQTT disconnect - replay dibatalkan (%d replayed)\r\n", replayed);
      logFile.close();
      return;
    }

    if (parsed >= 4) {
      mqttPublishVitals(spo2, hr, temp, status, parsed >= 5 ? ts : "");
      if (strcmp(status, lastReplayStatus) != 0 && strcmp(status, STATUS_NORMAL) != 0) {
        mqttPublishAlarm(status, spo2, hr, temp, parsed >= 5 ? ts : "");
      }
      strncpy(lastReplayStatus, status, sizeof(lastReplayStatus) - 1);
      lastReplayStatus[sizeof(lastReplayStatus) - 1] = '\0';
      replayed++;
      delay(50);
    } else {
      Serial.printf("[LOG] Parse error (fields=%d): %s\r\n", parsed, line);
    }
  }
  logFile.close();

  if (SPIFFS.remove(DATA_LOG_PATH)) {
    g_logLineCount = 0;
    lastReplayStatus[0] = '\0';
  }

  Serial.printf("[LOG] Replay selesai: %d data terpublish\r\n", replayed);
}
