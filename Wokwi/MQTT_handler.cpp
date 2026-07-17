/**
 * @file    MQTT_handler.cpp
 * @brief   Implementasi modul koneksi WiFi dan MQTT
 * @project IoT Dev Comp TETI 2026
 *
 * Menangani:
 *   - Koneksi WiFi dengan retry otomatis
 *   - Koneksi MQTT ke broker publik test.mosquitto.org
 *   - Auto reconnect dengan re-subscribe + re-publish state
 *   - Callback command: PING, REBOOT, GET_STATE
 *   - Serialisasi payload JSON untuk topic vitals dan alarm
 *   - State retained (TOPIC_STATE) & presence LWT (TOPIC_PRESENCE)
 */

#include "MQTT_handler.h"
#include <WiFi.h>
#if MQTT_USE_TLS
#include <WiFiClientSecure.h>
#endif
#include <esp_task_wdt.h>
#include <cctype>
#include <ArduinoJson.h>
#include <cmath>
#include <WiFiManager.h>
#include <Preferences.h>
#include <ArduinoOTA.h>

/* External — shared state dari Sketch.ino */
extern unsigned long g_lastOledActivity;

/* ------------------------------------------------------------------ */
/*  Objek internal (file-scope)                                        */
/* ------------------------------------------------------------------ */
#if MQTT_USE_TLS
static WiFiClientSecure wifiClient;
#else
static WiFiClient       wifiClient;
#endif
static PubSubClient     mqttClient(wifiClient);
static WiFiManager      g_wifiManager;
static char             g_patientState[16] = "Normal";
static char             g_clientId[32];       /* digenerate dari MAC */
static bool             g_clientIdGenerated = false;
static bool             g_alarmAcknowledged = false;  /* ACK dari dashboard */
static unsigned long    g_lastNvsSave = 0;
static bool             g_rebootPending = false;

/* Command response queue — hindari publish() langsung dari callback (re-entrancy) */
#define CMD_RESP_BUF_SIZE 384
static char             g_cmdRespBuf[CMD_RESP_BUF_SIZE] = "";
static const char*      g_cmdRespTopic = NULL;
static bool             g_cmdRespPending = false;
static bool             g_cmdRespRetained = false;

/* ------------------------------------------------------------------ */
/*  Generate unique client ID dari MAC address ESP32                   */
/* ------------------------------------------------------------------ */
static const char* getClientId() {
  if (!g_clientIdGenerated) {
    uint64_t mac = ESP.getEfuseMac();
    snprintf(g_clientId, sizeof(g_clientId), "esp32_patient_%04X%08X",
             (uint16_t)(mac >> 32), (uint32_t)mac);
    g_clientIdGenerated = true;
    Serial.printf("[MQTT] Client ID: %s\n", g_clientId);
  }
  return g_clientId;
}

/* ------------------------------------------------------------------ */
/*  Fungsi internal                                                    */
/* ------------------------------------------------------------------ */

static void connectWifi() {
  if (WiFi.status() == WL_CONNECTED) return;

  g_wifiManager.setConnectTimeout(10);
  g_wifiManager.setConfigPortalTimeout(7);
  g_wifiManager.setDarkMode(true);

  Serial.print("[WiFi] WiFiManager autoConnect...");

  /* Percobaan 1: WiFiManager — saved credentials atau AP portal */
  esp_task_wdt_reset();
  if (g_wifiManager.autoConnect(WIFI_AP_NAME, WIFI_AP_PASS)) {
    Serial.println();
    Serial.print("[WiFi] Terhubung. IP: ");
    Serial.println(WiFi.localIP());
    return;
  }

  /* Percobaan 2: default credentials dari Config (Wokwi-GUEST untuk simulator) */
  Serial.println();
  Serial.println("[WiFi] WiFiManager gagal, coba default credentials...");
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 20) {
    esp_task_wdt_reset();
    delay(500);
    Serial.print(".");
    attempts++;
  }

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println();
    Serial.print("[WiFi] Terhubung via default SSID. IP: ");
    Serial.println(WiFi.localIP());
  } else {
    Serial.println();
    Serial.println("[WiFi] Gagal total -- mode offline aktif");
  }
}

static void connectMqtt() {
  if (mqttClient.connected()) return;
  if (WiFi.status() != WL_CONNECTED) return;

#if MQTT_USE_TLS
  wifiClient.setInsecure();
  Serial.print("[MQTT] TLS enabled, connecting to ");
#else
  Serial.print("[MQTT] Connecting to ");
#endif
  Serial.printf("%s:%d...", MQTT_BROKER, MQTT_USE_TLS ? MQTT_TLS_PORT : MQTT_PORT);

  const char* clientId = getClientId();

  /* LWT: broker publish "offline" ke TOPIC_PRESENCE jika ESP disconnect mendadak */
  bool ok = mqttClient.connect(clientId, NULL, NULL,
                               TOPIC_PRESENCE, 1, true, "offline");
  if (ok) {
    Serial.println(" OK");

    mqttClient.publish(TOPIC_PRESENCE, (const uint8_t*)"online", strlen("online"), true);

    /* Re-subscribe setelah reconnect (sesi broker hilang) */
    mqttClient.subscribe(TOPIC_CMD);
    Serial.printf("  Subscribed: %s\n", TOPIC_CMD);

    /* Re-publish state aktual */
    mqttPublishState(g_patientState);
  } else {
    Serial.print(" Gagal, rc=");
    Serial.println(mqttClient.state());
  }
}

/* Helper: case-insensitive strstr, gunakan untuk parsing payload MQTT */
static const char* stristr(const char* haystack, const char* needle) {
  if (!*needle) return haystack;
  for (; *haystack; ++haystack) {
    if (toupper((unsigned char)*haystack) == toupper((unsigned char)*needle)) {
      const char *h = haystack, *n = needle;
      while (*h && *n && toupper((unsigned char)*h) == toupper((unsigned char)*n)) {
        ++h; ++n;
      }
      if (!*n) return haystack;
    }
  }
  return NULL;
}

/* Helper: parse floating value setelah prefix dalam payload (case-insensitive) */
static float parseFloatAfter(const char* payload, const char* prefix) {
  const char* p = stristr(payload, prefix);
  if (!p) return -999;
  char* end = nullptr;
  float val = strtof(p + strlen(prefix), &end);
  if (end == p + strlen(prefix)) return -999;
  return val;
}

/* Helper: parse integer value setelah prefix dalam payload (case-insensitive) */
static int parseIntAfter(const char* payload, const char* prefix) {
  const char* p = stristr(payload, prefix);
  if (!p) return -999;
  char* end = nullptr;
  long val = strtol(p + strlen(prefix), &end, 10);
  if (end == p + strlen(prefix)) return -999;
  return (int)val;
}

/* Helper: threshold validator untuk SET_THRESHOLD reply */
static bool validateAndLogFloat(float val, float min, float max, const char* name) {
  if (val < min || val > max) {
    Serial.printf("[MQTT]   → %s: %.1f di luar range (%.0f-%.0f)\n", name, val, min, max);
    return false;
  }
  return true;
}

static bool validateAndLogInt(int val, int min, int max, const char* name) {
  if (val < min || val > max) {
    Serial.printf("[MQTT]   → %s: %d di luar range (%d-%d)\n", name, val, min, max);
    return false;
  }
  return true;
}

/* Batas panjang command buffer (M2) */
#define CMD_BUF_SIZE 512

/* Forward declaration untuk fungsi NVS (didefinisikan setelah callback) */
static void thresholdsSaveToNvs();

/* Macro untuk SET_THRESHOLD — hilangkan duplikasi 8 blok serupa */
#define HANDLE_SET_THRESHOLD_FLOAT(buf, prefix, threshold, vmin, vmax) do {     \
  float _v = parseFloatAfter((buf), (prefix));                                  \
  if (validateAndLogFloat(_v, (vmin), (vmax), #threshold)) {                    \
    if ((threshold) != _v) { (threshold) = _v; thresholdsSaveDebounced(); }     \
    Serial.printf("[MQTT]   → " #threshold " = %.1f\n", _v);                    \
    g_cmdRespTopic = TOPIC_FEEDBACK;                                              \
    if (g_cmdRespPending) {                                                       \
      Serial.println("[MQTT] WARN: Response queue overwrite");                    \
    }                                                                             \
    snprintf(g_cmdRespBuf, sizeof(g_cmdRespBuf),                                     \
             "{\"cmd\":\"SET_THRESHOLD\",\"key\":\"" #threshold "\",\"value\":%.1f}", (double)_v); \
    g_cmdRespRetained = false;                                                       \
    g_cmdRespPending = true;                                                          \
  } else {                                                                           \
    g_cmdRespTopic = TOPIC_FEEDBACK;                                                 \
    snprintf(g_cmdRespBuf, sizeof(g_cmdRespBuf),                                      \
             "{\"cmd\":\"SET_THRESHOLD\",\"key\":\"" #threshold "\",\"error\":\"value out of range\"}"); \
    g_cmdRespRetained = false;                                                        \
    g_cmdRespPending = true;                                                           \
  }                                                                                    \
} while(0)

#define HANDLE_SET_THRESHOLD_INT(buf, prefix, threshold, vmin, vmax) do {       \
  int _v = parseIntAfter((buf), (prefix));                                      \
  if (validateAndLogInt(_v, (vmin), (vmax), #threshold)) {                      \
    if ((threshold) != _v) { (threshold) = _v; thresholdsSaveDebounced(); }     \
    Serial.printf("[MQTT]   → " #threshold " = %d\n", _v);                      \
    g_cmdRespTopic = TOPIC_FEEDBACK;                                              \
    if (g_cmdRespPending) {                                                       \
      Serial.println("[MQTT] WARN: Response queue overwrite");                    \
    }                                                                             \
    snprintf(g_cmdRespBuf, sizeof(g_cmdRespBuf),                                   \
             "{\"cmd\":\"SET_THRESHOLD\",\"key\":\"" #threshold "\",\"value\":%d}", _v); \
    g_cmdRespRetained = false;                                                     \
    g_cmdRespPending = true;                                                        \
  } else {                                                                           \
    g_cmdRespTopic = TOPIC_FEEDBACK;                                                 \
    snprintf(g_cmdRespBuf, sizeof(g_cmdRespBuf),                                      \
             "{\"cmd\":\"SET_THRESHOLD\",\"key\":\"" #threshold "\",\"error\":\"value out of range\"}"); \
    g_cmdRespRetained = false;                                                        \
    g_cmdRespPending = true;                                                           \
  }                                                                                    \
} while(0)

/* Threshold save with debounce — cegah flash wear dari rapid command */
static void thresholdsSaveDebounced() {
  unsigned long now = millis();
  if (g_lastNvsSave == 0 || (unsigned long)(now - g_lastNvsSave) >= 5000) {
    thresholdsSaveToNvs();
    g_lastNvsSave = now;
  } else {
    Serial.println("[NVS]   Skip save (debounce 5s)");
  }
}

static void mqttCallback(char* topic, byte* payload, unsigned int len) {
  /* M4: Reset OLED idle timer — aktivitas MQTT = user interaction */
  g_lastOledActivity = millis();

  /* L1/L4: Copy ke local buffer agar tidak memodifikasi internal PubSubClient */
  char local[CMD_BUF_SIZE];
  size_t copyLen = (len < sizeof(local) - 1) ? len : (sizeof(local) - 1);
  memcpy(local, payload, copyLen);
  local[copyLen] = '\0';
  Serial.printf("[MQTT] Command diterima di %s: %s\n", topic, local);

  /* Gunakan strcmp, bukan String — hindari heap alokasi */
  if (strcmp(topic, TOPIC_CMD) != 0) return;

  char* p = local;
  /* Trim leading whitespace (spasi, tab, CR) */
  while (*p == ' ' || *p == '\t' || *p == '\r') p++;
  /* Uppercase untuk case-insensitive match */
  char upper[CMD_BUF_SIZE];
  int i = 0;
  while (p[i] && i < (int)sizeof(upper) - 1) {
    upper[i] = toupper((unsigned char)p[i]);
    i++;
  }
  upper[i] = '\0';
  /* Trim trailing whitespace (spasi, tab, CR, LF) */
  while (i > 0 && (upper[i-1] == ' ' || upper[i-1] == '\t' || upper[i-1] == '\r' || upper[i-1] == '\n')) {
    upper[--i] = '\0';
  }

  if (strcmp(upper, "PING") == 0) {
    /* Queue response — jangan publish langsung dari callback */
    g_cmdRespTopic = TOPIC_PRESENCE;
    strncpy(g_cmdRespBuf, "online", sizeof(g_cmdRespBuf) - 1);
    g_cmdRespBuf[sizeof(g_cmdRespBuf) - 1] = '\0';
    g_cmdRespRetained = true;
    if (g_cmdRespPending) Serial.println("[MQTT] WARN: Response queue overwrite");
    g_cmdRespPending = true;
    Serial.println("[MQTT]   → PING replied");

  } else if (strcmp(upper, "REBOOT") == 0) {
    Serial.println("[MQTT]   → REBOOT antri...");
    g_rebootPending = true;

  } else if (strcmp(upper, "GET_STATE") == 0) {
    /* Queue state — publish dari loop */
    g_cmdRespTopic = TOPIC_STATE;
    strncpy(g_cmdRespBuf, g_patientState, sizeof(g_cmdRespBuf) - 1);
    g_cmdRespBuf[sizeof(g_cmdRespBuf) - 1] = '\0';
    g_cmdRespRetained = true;
    if (g_cmdRespPending) Serial.println("[MQTT] WARN: Response queue overwrite");
    g_cmdRespPending = true;
    Serial.printf("[MQTT]   → State queued: %s\n", g_patientState);

  } else if (strcmp(upper, "GET_THRESHOLDS") == 0) {
    StaticJsonDocument<384> doc;
    doc["cmd"]            = "THRESHOLDS";
    doc["spo2_warn"]      = SPO2_WARNING_LOW;
    doc["spo2_crit"]      = SPO2_CRITICAL_LOW;
    doc["hr_warn_low"]    = HR_WARNING_LOW;
    doc["hr_warn_high"]   = HR_WARNING_HIGH;
    doc["hr_crit_low"]    = HR_CRITICAL_LOW;
    doc["hr_crit_high"]   = HR_CRITICAL_HIGH;
    doc["temp_warn_high"] = TEMP_WARNING_HIGH;
    doc["temp_crit_high"] = TEMP_CRITICAL_HIGH;
    doc["temp_warn_low"]  = TEMP_WARNING_LOW;
    doc["temp_crit_low"]  = TEMP_CRITICAL_LOW;
    char buf[256];
    size_t jsonLen = serializeJson(doc, buf, sizeof(buf));
    if (jsonLen >= sizeof(buf)) {
      Serial.println("[MQTT] ERROR: Thresholds JSON overflow!");
      return;
    }
    g_cmdRespTopic = TOPIC_FEEDBACK;
    strncpy(g_cmdRespBuf, buf, sizeof(g_cmdRespBuf) - 1);
    g_cmdRespBuf[sizeof(g_cmdRespBuf) - 1] = '\0';
    g_cmdRespRetained = false;
    if (g_cmdRespPending) Serial.println("[MQTT] WARN: Response queue overwrite");
    g_cmdRespPending = true;
    Serial.printf("[MQTT]   → Thresholds queued: %s\n", buf);

  } else if (strcmp(upper, "ACK_ALARM") == 0) {
    g_alarmAcknowledged = true;
    g_cmdRespTopic = TOPIC_FEEDBACK;
    snprintf(g_cmdRespBuf, sizeof(g_cmdRespBuf), "{\"cmd\":\"ACK_ALARM\",\"status\":\"ok\"}");
    g_cmdRespRetained = false;
    g_cmdRespPending = true;
    Serial.println("[MQTT]   → Alarm acknowledged (buzzer silent)");

  } else if (strncmp(upper, "SET_THRESHOLD_SPO2_WARN:", 24) == 0) {
    HANDLE_SET_THRESHOLD_FLOAT(upper, "SET_THRESHOLD_SPO2_WARN:", SPO2_WARNING_LOW, SPO2_VALID_MIN, SPO2_VALID_MAX);
  } else if (strncmp(upper, "SET_THRESHOLD_SPO2_CRIT:", 24) == 0) {
    HANDLE_SET_THRESHOLD_FLOAT(upper, "SET_THRESHOLD_SPO2_CRIT:", SPO2_CRITICAL_LOW, SPO2_VALID_MIN, SPO2_VALID_MAX);
  } else if (strncmp(upper, "SET_THRESHOLD_HR_WARN_LOW:", 26) == 0) {
    HANDLE_SET_THRESHOLD_INT(upper, "SET_THRESHOLD_HR_WARN_LOW:", HR_WARNING_LOW, HR_VALID_MIN, HR_VALID_MAX);
  } else if (strncmp(upper, "SET_THRESHOLD_HR_WARN_HIGH:", 27) == 0) {
    HANDLE_SET_THRESHOLD_INT(upper, "SET_THRESHOLD_HR_WARN_HIGH:", HR_WARNING_HIGH, HR_VALID_MIN, HR_VALID_MAX);
  } else if (strncmp(upper, "SET_THRESHOLD_HR_CRIT_LOW:", 26) == 0) {
    HANDLE_SET_THRESHOLD_INT(upper, "SET_THRESHOLD_HR_CRIT_LOW:", HR_CRITICAL_LOW, HR_VALID_MIN, HR_VALID_MAX);
  } else if (strncmp(upper, "SET_THRESHOLD_HR_CRIT_HIGH:", 27) == 0) {
    HANDLE_SET_THRESHOLD_INT(upper, "SET_THRESHOLD_HR_CRIT_HIGH:", HR_CRITICAL_HIGH, HR_VALID_MIN, HR_VALID_MAX);
  } else if (strncmp(upper, "SET_THRESHOLD_TEMP_WARN:", 24) == 0) {
    HANDLE_SET_THRESHOLD_FLOAT(upper, "SET_THRESHOLD_TEMP_WARN:", TEMP_WARNING_HIGH, TEMP_VALID_MIN, TEMP_VALID_MAX);
  } else if (strncmp(upper, "SET_THRESHOLD_TEMP_CRIT:", 24) == 0) {
    HANDLE_SET_THRESHOLD_FLOAT(upper, "SET_THRESHOLD_TEMP_CRIT:", TEMP_CRITICAL_HIGH, TEMP_VALID_MIN, TEMP_VALID_MAX);
  } else if (strncmp(upper, "SET_THRESHOLD_TEMP_WARN_LOW:", 28) == 0) {
    HANDLE_SET_THRESHOLD_FLOAT(upper, "SET_THRESHOLD_TEMP_WARN_LOW:", TEMP_WARNING_LOW, TEMP_VALID_MIN, TEMP_VALID_MAX);
  } else if (strncmp(upper, "SET_THRESHOLD_TEMP_CRIT_LOW:", 28) == 0) {
    HANDLE_SET_THRESHOLD_FLOAT(upper, "SET_THRESHOLD_TEMP_CRIT_LOW:", TEMP_CRITICAL_LOW, TEMP_VALID_MIN, TEMP_VALID_MAX);
  } else {
    Serial.printf("[MQTT]   → Perintah tidak dikenal: %s\n", upper);
  }
}

/* ------------------------------------------------------------------ */
/*  NVS Threshold Persistence — macros untuk eliminasi duplikasi       */
/* ------------------------------------------------------------------ */
#define NVS_SAVE_FLOAT(p, k, v)  do { (p).putFloat((k), (v)); } while(0)
#define NVS_SAVE_INT(p, k, v)    do { (p).putInt((k), (v));   } while(0)
#define NVS_LOAD_FLOAT(p, k, v)  do { if ((p).isKey((k))) (v) = (p).getFloat((k), 0); } while(0)
#define NVS_LOAD_INT(p, k, v)    do { if ((p).isKey((k))) (v) = (p).getInt((k), 0);   } while(0)

static void thresholdsSaveToNvs() {
  Preferences prefs;
  if (!prefs.begin("thresholds", false)) {
    Serial.println("[NVS] ERROR: Gagal buka NVS untuk write");
    return;
  }
  NVS_SAVE_FLOAT(prefs, "spo2_warn",  SPO2_WARNING_LOW);
  NVS_SAVE_FLOAT(prefs, "spo2_crit",  SPO2_CRITICAL_LOW);
  NVS_SAVE_INT(prefs, "hr_warn_low",  HR_WARNING_LOW);
  NVS_SAVE_INT(prefs, "hr_warn_high", HR_WARNING_HIGH);
  NVS_SAVE_INT(prefs, "hr_crit_low",  HR_CRITICAL_LOW);
  NVS_SAVE_INT(prefs, "hr_crit_high", HR_CRITICAL_HIGH);
  NVS_SAVE_FLOAT(prefs, "temp_warn",  TEMP_WARNING_HIGH);
  NVS_SAVE_FLOAT(prefs, "temp_crit",  TEMP_CRITICAL_HIGH);
  NVS_SAVE_FLOAT(prefs, "temp_warn_low",  TEMP_WARNING_LOW);
  NVS_SAVE_FLOAT(prefs, "temp_crit_low",  TEMP_CRITICAL_LOW);
  prefs.end();
  Serial.println("[NVS] Thresholds saved");
}

static void thresholdsLoadFromNvs() {
  Preferences prefs;
  if (!prefs.begin("thresholds", true)) {
    Serial.println("[NVS] ERROR: Gagal buka NVS untuk read");
    return;
  }
  NVS_LOAD_FLOAT(prefs, "spo2_warn",  SPO2_WARNING_LOW);
  NVS_LOAD_FLOAT(prefs, "spo2_crit",  SPO2_CRITICAL_LOW);
  NVS_LOAD_INT(prefs, "hr_warn_low",  HR_WARNING_LOW);
  NVS_LOAD_INT(prefs, "hr_warn_high", HR_WARNING_HIGH);
  NVS_LOAD_INT(prefs, "hr_crit_low",  HR_CRITICAL_LOW);
  NVS_LOAD_INT(prefs, "hr_crit_high", HR_CRITICAL_HIGH);
  NVS_LOAD_FLOAT(prefs, "temp_warn",  TEMP_WARNING_HIGH);
  NVS_LOAD_FLOAT(prefs, "temp_crit",  TEMP_CRITICAL_HIGH);
  NVS_LOAD_FLOAT(prefs, "temp_warn_low",  TEMP_WARNING_LOW);
  NVS_LOAD_FLOAT(prefs, "temp_crit_low",  TEMP_CRITICAL_LOW);
  prefs.end();
  Serial.println("[NVS] Thresholds loaded");
}

/* ------------------------------------------------------------------ */
/*  Public API                                                         */
/* ------------------------------------------------------------------ */

void mqttSetup() {
  thresholdsLoadFromNvs();
  connectWifi();
  mqttClient.setServer(MQTT_BROKER, MQTT_USE_TLS ? MQTT_TLS_PORT : MQTT_PORT);
  mqttClient.setKeepAlive(MQTT_KEEPALIVE_SEC);
  mqttClient.setCallback(mqttCallback);
  connectMqtt();     /* sudah publish initial state */
}

void mqttLoop() {
  if (g_rebootPending) {
    mqttClient.publish(TOPIC_PRESENCE, (const uint8_t*)"offline", strlen("offline"), true);
    mqttClient.disconnect();
    delay(100);
    esp_task_wdt_reset();
    WiFi.disconnect();
    ESP.restart();
  }
  if (WiFi.status() != WL_CONNECTED) connectWifi();
  if (!mqttClient.connected())        connectMqtt();
  mqttClient.loop();

  /* Kirim response yang di-queue dari callback (re-entrancy safety) */
  if (g_cmdRespPending && mqttClient.connected() && g_cmdRespTopic != NULL) {
    mqttClient.publish(g_cmdRespTopic, g_cmdRespBuf, g_cmdRespRetained);
    g_cmdRespPending = false;
  }
}

void mqttLoopOnce() {
  if (mqttClient.connected()) mqttClient.loop();
}

bool mqttIsConnected() {
  return mqttClient.connected();
}

void mqttPublishState(const char* state) {
  if (!mqttClient.connected()) return;
  if (!mqttClient.publish(TOPIC_STATE, (const uint8_t*)state, strlen(state), true)) {
    Serial.println("[MQTT] WARN: State publish failed");
  }
  Serial.printf("[MQTT] State: %s\n", state);
}

void mqttSetState(const char* state) {
  strncpy(g_patientState, state, sizeof(g_patientState) - 1);
  g_patientState[sizeof(g_patientState) - 1] = '\0';
}

const char* mqttGetState() {
  return g_patientState;
}

bool mqttIsAlarmAcknowledged() {
  return g_alarmAcknowledged;
}

void mqttResetAlarmAck() {
  g_alarmAcknowledged = false;
}

/* Helper M3: publish string JSON ke topic tertentu dengan log */
static bool publishJsonTo(const char* topic, const char* json, const char* label) {
  if (!mqttClient.connected()) return false;
  size_t len = strlen(json);
  bool ok = mqttClient.publish(topic, (const uint8_t*)json, len, false);
  if (ok) Serial.printf("[MQTT] %s: %s\n", label, json);
  return ok;
}

bool mqttPublishVitals(float spo2, int heartRate, float temperature, const char* patientStatus, const char* csvTimestamp) {
  StaticJsonDocument<256> doc;
  doc["timestamp"]      = csvTimestamp ? csvTimestamp : "";
  doc["spo2"]           = (int)round(spo2);
  doc["heart_rate"]     = heartRate;
  doc["temperature"]    = round(temperature * 10) / 10.0;
  doc["patient_status"] = patientStatus;

  char buf[256];
  if (serializeJson(doc, buf, sizeof(buf)) >= sizeof(buf)) {
    Serial.println("[MQTT] ERROR: Vitals JSON overflow!");
    return false;
  }
  return publishJsonTo(TOPIC_VITALS, buf, "Vitals");
}

bool mqttPublishAlarm(const char* patientStatus, float spo2, int heartRate, float temperature, const char* csvTimestamp) {
  StaticJsonDocument<200> doc;
  doc["alarm_status"]  = patientStatus;
  doc["spo2"]          = (int)round(spo2);
  doc["heart_rate"]    = heartRate;
  doc["temperature"]   = round(temperature * 10) / 10.0;
  doc["timestamp"]     = csvTimestamp ? csvTimestamp : "";

  char buf[200];
  if (serializeJson(doc, buf, sizeof(buf)) >= sizeof(buf)) {
    Serial.println("[MQTT] ERROR: Alarm JSON overflow!");
    return false;
  }
  return publishJsonTo(TOPIC_ALARM, buf, "Alarm");
}
