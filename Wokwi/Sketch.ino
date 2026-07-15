/**
 * @file    Sketch.ino
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
#include <FS.h>
#include <SPIFFS.h>
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

/* ------------------------------------------------------------------ */
/*  Handle file CSV                                                    */
/* ------------------------------------------------------------------ */
static File g_csvFile;
static char g_csvTimestamp[25] = "";

/* ------------------------------------------------------------------ */
/*  Buffer moving average filter                                       */
/* ------------------------------------------------------------------ */
float g_spo2Buffer[FILTER_SIZE] = {98, 98, 98, 98, 98};
int   g_hrBuffer[FILTER_SIZE]   = {75, 75, 75, 75, 75};
float g_tempBuffer[FILTER_SIZE] = {36.5, 36.5, 36.5, 36.5, 36.5};
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
static int g_hysteresisCount = 0;

/* ------------------------------------------------------------------ */
/*  Timing                                                             */
/* ------------------------------------------------------------------ */
unsigned long g_lastPublish           = 0;
unsigned long g_lastCriticalBuzzer    = 0;
unsigned long g_lastWarningBuzzer     = 0;
unsigned long g_lastOledActivity      = 0;
bool          g_buzzerState           = false;
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

/* ================================================================== */
/*  SETUP                                                              */
/* ================================================================== */
void setup() {
  Serial.begin(115200);

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
  }

  /* Splash screen */
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

  g_lastOledActivity = millis();  /* timer idle OLED mulai setelah splash */

  /* Inisialisasi SPIFFS */
  if (!SPIFFS.begin(true)) {
    Serial.println("[ERROR] SPIFFS gagal di-mount!");
  }

  /* G3: Hitung baris existing data log agar rotation threshold akurat */
  if (SPIFFS.exists(DATA_LOG_PATH)) {
    File _log = SPIFFS.open(DATA_LOG_PATH, FILE_READ);
    if (_log) {
      while (_log.available()) {
        if (_log.read() == '\n') g_logLineCount++;
      }
      _log.close();
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

  /* Inisialisasi koneksi WiFi dan MQTT */
  mqttSetup();

  /* OTA: update firmware over WiFi tanpa kabel USB */
  /* L2: Ganti password untuk production — "ota123" hanya untuk demo */
  ArduinoOTA.setHostname("ESP32-VitalMon");
  ArduinoOTA.setPassword("ota123");
  ArduinoOTA.onStart([]() {
    const char* type = ArduinoOTA.getCommand() == U_FLASH ? "sketch" : "filesystem";
    Serial.printf("[OTA] Mulai update %s\n", type);
  });
  ArduinoOTA.onEnd([]() {
    Serial.println("[OTA] Selesai");
  });
  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
    Serial.printf("[OTA] Progress: %u%%\r", progress / (total / 100));
  });
  ArduinoOTA.onError([](ota_error_t error) {
    Serial.printf("[OTA] Error %d\n", error);
  });
  ArduinoOTA.begin();

  /* Watchdog Timer: reset otomatis jika loop hang >15 detik */
  esp_task_wdt_init(15, true);
  esp_task_wdt_add(NULL);

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
      /* Baca satu baris CSV dan proses */
      if (readCsvRow()) {
        applyMovingAverage();
        determinePatientStatus();
        printSerial();
        /* updateOled() dipanggil di luar if agar tetap refresh walau
           readCsvRow gagal (OLED tidak tampilkan data basi) */

        bool published = mqttPublishVitals(g_spo2Value, g_heartRate, g_tempValue, g_patientStatus, g_csvTimestamp);

        /* Jika MQTT offline, simpan data ke SPIFFS sebagai buffer */
        if (!published) {
          logToSpiffs(g_spo2Value, g_heartRate, g_tempValue, g_patientStatus, g_csvTimestamp);
        }

        /* Transisi status: update state + publish event */
        if (strcmp(g_patientStatus, g_lastStatus) != 0) {
          strncpy(g_lastStatus, g_patientStatus, sizeof(g_lastStatus));

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
              mqttPublishAlarm(g_patientStatus, g_spo2Value, g_heartRate, g_tempValue, g_csvTimestamp);
            }
          }
        }
      }
      updateOled();  /* selalu refresh OLED dalam publish cycle */
    } else {
      updateOled();
    }

    g_lastPublish = millis();
  }

  delay(10);
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
/*  fragmentasi heap. Nilai divalidasi range sebelum dipakai.         */
/* ================================================================== */
bool readCsvRow() {
  if (!g_csvFile) return false;

  /* EOF -- loop ke awal dataset */
  if (!g_csvFile.available()) {
    Serial.println("[CSV]   EOF -- loop ke awal dataset");
    if (!g_csvFile.seek(0)) {
      Serial.println("[CSV]   Seek gagal, buka ulang file...");
      g_csvFile.close();
      openCsvFile();
      return false;
    }
    {
      char _hdr[256];
      g_csvFile.readBytesUntil('\n', _hdr, sizeof(_hdr) - 1); /* lewati header (L3) */
    }
  }

  /* Baca satu baris ke char buffer */
  char line[128];
  size_t len = g_csvFile.readBytesUntil('\n', line, sizeof(line) - 1);
  line[len] = '\0';

  /* Trim trailing whitespace */
  while (len > 0 && (line[len - 1] == ' ' || line[len - 1] == '\r')) {
    line[--len] = '\0';
  }

  if (len == 0) return false;

  /* Parse CSV: heart_rate, spo2, temperature, timestamp */
  int   rawHr   = 0;
  float rawSpo2 = 0.0f;
  float rawTemp = 0.0f;
  char  rawTs[25] = "";

  int parsed = sscanf(line, "%d,%f,%f,%24[^\n]", &rawHr, &rawSpo2, &rawTemp, rawTs);
  if (parsed < 3) {
    Serial.printf("[CSV]   Parse error (fields=%d): %s\n", parsed, line);
    return false;
  }

  /* Validasi range nilai fisiologis
     M3: Berbeda dengan SPO2_VALID_MIN/MAX di Config (80-100) yang untuk
     threshold admin. Ini untuk plausibility dataset: reject data rusak. */
  if (rawHr < 20 || rawHr > 250) return false;
  if (rawSpo2 < 50.0f || rawSpo2 > 100.0f) return false;
  if (rawTemp < 34.0f || rawTemp > 42.0f) return false;

  /* Simpan ke buffer moving average */
  g_hrBuffer[g_filterIndex]   = rawHr;
  g_spo2Buffer[g_filterIndex] = rawSpo2;
  g_tempBuffer[g_filterIndex] = rawTemp;
  g_filterIndex = (g_filterIndex + 1) % FILTER_SIZE;

  /* Simpan timestamp untuk dipakai di publish */
  if (parsed >= 4 && strlen(rawTs) > 0) {
    strncpy(g_csvTimestamp, rawTs, sizeof(g_csvTimestamp) - 1);
    g_csvTimestamp[sizeof(g_csvTimestamp) - 1] = '\0';
    Serial.printf("[CSV]   Timestamp: %s\n", g_csvTimestamp);
  }

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
  g_heartRate = (sumHr + FILTER_SIZE / 2) / FILTER_SIZE;  /* rounding */
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
                    (g_tempValue > TEMP_CRITICAL_HIGH);

  bool isWarning  = (g_spo2Value < SPO2_WARNING_LOW)   ||
                    (g_heartRate < HR_WARNING_LOW)      ||
                    (g_heartRate > HR_WARNING_HIGH)     ||
                    (g_tempValue > TEMP_WARNING_HIGH);

  /* Tentukan level yang seharusnya */
  char desired[16];
  if (isCritical)      strncpy(desired, STATUS_CRITICAL, sizeof(desired));
  else if (isWarning)  strncpy(desired, STATUS_WARNING,  sizeof(desired));
  else                 strncpy(desired, STATUS_NORMAL,   sizeof(desired));

  /* Hysteresis: hanya berubah jika level baru bertahan beberapa siklus */
  if (strcmp(desired, g_patientStatus) != 0) {
    g_hysteresisCount++;
    if (g_hysteresisCount >= HYSTERESIS_COUNT) {
      strncpy(g_patientStatus, desired, sizeof(g_patientStatus));
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
      g_buzzerState = !g_buzzerState;
      if (g_buzzerState) tone(PIN_BUZZER, BUZZER_CRITICAL_FREQ, BUZZER_CRITICAL_DURATION);
      else noTone(PIN_BUZZER);
      g_lastCriticalBuzzer = millis();
    }
    if (alarmAcked) {
      noTone(PIN_BUZZER);
      g_buzzerState = false;
    }

  } else if (strcmp(g_patientStatus, STATUS_WARNING) == 0) {
    digitalWrite(PIN_LED_RED,    LOW);
    digitalWrite(PIN_LED_YELLOW, HIGH);
    digitalWrite(PIN_LED_GREEN,  LOW);

    /* Warning: slow beep 800Hz setiap 2000ms */
    if (!alarmAcked && (unsigned long)(millis() - g_lastWarningBuzzer) >= BUZZER_WARNING_INTERVAL) {
      g_buzzerState = !g_buzzerState;
      if (g_buzzerState) tone(PIN_BUZZER, BUZZER_WARNING_FREQ, BUZZER_WARNING_DURATION);
      else noTone(PIN_BUZZER);
      g_lastWarningBuzzer = millis();
    }
    if (alarmAcked) {
      noTone(PIN_BUZZER);
      g_buzzerState = false;
    }

  } else {
    digitalWrite(PIN_LED_RED,    LOW);
    digitalWrite(PIN_LED_YELLOW, LOW);
    digitalWrite(PIN_LED_GREEN,  HIGH);
    noTone(PIN_BUZZER);
    g_buzzerState = false;
  }
}

/* ================================================================== */
/*  updateOled                                                         */
/*  Render tampilan OLED: header status, nilai SpO2, HR, suhu,        */
/*  dan indikator koneksi MQTT di pojok kanan atas.                   */
/*  Jika file CSV tidak ditemukan, tampilkan pesan error.              */
/* ================================================================== */
void updateOled() {
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
    g_oledSleeping = false;
    delay(10);  /* beri waktu display stabil */
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
  display.setCursor(OLED_SPO2_X, OLED_SPO2_Y);
  display.print("SpO2 :");
  display.setTextSize(2);
  display.setCursor(OLED_SPO2_VALUE_X, OLED_SPO2_Y - 2);
  display.print((int)round(g_spo2Value));
  display.setTextSize(1);
  display.print(" %");

  /* Heart Rate */
  display.setTextSize(1);
  display.setCursor(OLED_HR_X, OLED_HR_Y);
  display.print("HR   :");
  display.setTextSize(2);
  display.setCursor(OLED_HR_VALUE_X, OLED_HR_Y - 2);
  display.print(g_heartRate);
  display.setTextSize(1);
  display.print(" bpm");

  /* Suhu Tubuh */
  display.setTextSize(1);
  display.setCursor(OLED_TEMP_X, OLED_TEMP_Y);
  display.print("Temp :");
  display.setCursor(OLED_TEMP_VALUE_X, OLED_TEMP_Y);
  display.print(g_tempValue, 1);
  display.print(" C");

  /* Indikator MQTT sudah di-draw di header bar di atas */
  display.display();
}

/* ================================================================== */
/*  printSerial                                                        */
/*  Cetak ringkasan nilai sensor dan status ke Serial Monitor          */
/*  setiap PUBLISH_INTERVAL ms.                                       */
/* ================================================================== */
void printSerial() {
  Serial.println("==============================");
  Serial.printf("SpO2       : %.1f %%\n",  g_spo2Value);
  Serial.printf("Heart Rate : %d bpm\n",    g_heartRate);
  Serial.printf("Suhu       : %.1f degC\n", g_tempValue);
  Serial.printf("Status     : %s\n",        g_patientStatus);
  Serial.printf("MQTT       : %s\n",        mqttIsConnected() ? "Connected" : "Offline");
  Serial.println("==============================");
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
  if (!logFile) return;

  char line[128];
  snprintf(line, sizeof(line), "%.1f,%d,%.1f,%s,%s\n", spo2, hr, temp, status, ts ? ts : "");
  logFile.print(line);
  logFile.close();

  g_logLineCount++;
  Serial.printf("[LOG]   Data tersimpan (%zu/%zu): %s", g_logLineCount, DATA_LOG_MAX_LINES, line);
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
    SPIFFS.remove(DATA_LOG_PATH);
    g_logLineCount = 0;
    return;
  }

  Serial.printf("[LOG]   Replay %zu bytes data log...\n", size);

  char line[128];
  int replayed = 0;
  char lastReplayStatus[16];
  strncpy(lastReplayStatus, g_patientStatus, sizeof(lastReplayStatus) - 1);
  lastReplayStatus[sizeof(lastReplayStatus) - 1] = '\0';
  while (logFile.available()) {
    esp_task_wdt_reset();
    size_t len = logFile.readBytesUntil('\n', line, sizeof(line) - 1);
    line[len] = '\0';
    if (len == 0) continue;

    int   hr   = 0;
    float spo2 = 0.0f, temp = 0.0f;
    char  status[16] = "", ts[25] = "";
    int parsed = sscanf(line, "%f,%d,%f,%15[^,],%24[^\n]", &spo2, &hr, &temp, status, ts);

    if (!mqttIsConnected()) {
      Serial.printf("[LOG]   MQTT disconnect — replay dibatalkan (%d replayed)\n", replayed);
      logFile.close();
      return;  /* jangan hapus file, data tetap aman untuk replay nanti */
    }

    if (parsed >= 4) {
      mqttPublishVitals(spo2, hr, temp, status, parsed >= 5 ? ts : "");
      /* B-NEW2: Re-publish alarm event hanya pada transisi status */
      if (strcmp(status, lastReplayStatus) != 0 && strcmp(status, STATUS_NORMAL) != 0) {
        mqttPublishAlarm(status, spo2, hr, temp, parsed >= 5 ? ts : "");
      }
      strncpy(lastReplayStatus, status, sizeof(lastReplayStatus) - 1);
      lastReplayStatus[sizeof(lastReplayStatus) - 1] = '\0';
      replayed++;
      delay(50);  /* throttle agar tidak flood broker */
    } else {
      Serial.printf("[LOG]   Parse error (fields=%d): %s\n", parsed, line);
    }
  }
  logFile.close();

  /* Hapus file log setelah sukses replay */
  SPIFFS.remove(DATA_LOG_PATH);
  g_logLineCount = 0;

  Serial.printf("[LOG]   Replay selesai: %d data terpublish\n", replayed);
}
