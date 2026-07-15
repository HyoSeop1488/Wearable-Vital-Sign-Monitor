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
#include <ctype.h>
#include <ArduinoJson.h>
#include <WiFiManager.h>
#include <Preferences.h>

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
static const char*      g_patientState = "Normal";  /* H1: pakai literal, hindari static init order */
static char             g_clientId[32];       /* digenerate dari MAC */
static bool             g_clientIdGenerated = false;
static bool             g_alarmAcknowledged = false;  /* ACK dari dashboard */
static unsigned long    g_lastNvsSave = (-5000UL);     /* M1: unsigned wrap → (now - init) >= 5000 sejak boot */

/* Command response queue — hindari publish() langsung dari callback (re-entrancy) */
#define CMD_RESP_BUF_SIZE 256
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
  g_wifiManager.setConfigPortalTimeout(10); /* < WDT 15s agar tidak reset */
  g_wifiManager.setDarkMode(true);

  Serial.print("[WiFi] WiFiManager autoConnect...");

  /* Percobaan 1: WiFiManager — saved credentials atau AP portal */
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

    /* Presence (terpisah dari state): overwrite LWT dengan "online" */
    mqttClient.publish(TOPIC_PRESENCE, "online", true);

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
  return atof(p + strlen(prefix));
}

/* Helper: parse integer value setelah prefix dalam payload (case-insensitive) */
static int parseIntAfter(const char* payload, const char* prefix) {
  const char* p = stristr(payload, prefix);
  if (!p) return -999;
  return atoi(p + strlen(prefix));
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
#define CMD_BUF_SIZE 64

/* Forward declaration untuk fungsi NVS (didefinisikan setelah callback) */
static void thresholdsSaveToNvs();

/* Macro untuk SET_THRESHOLD — hilangkan duplikasi 8 blok serupa (D1) */
/* CATATAN: Parameter `buf` adalah char array uppercase hasil parsing MQTT  */
#define HANDLE_SET_THRESHOLD_FLOAT(buf, prefix, threshold, vmin, vmax) do {     \
  float _v = parseFloatAfter((buf), (prefix));                                  \
  if (validateAndLogFloat(_v, (vmin), (vmax), #threshold)) {                    \
    if ((threshold) != _v) { (threshold) = _v; thresholdsSaveDebounced(); }     \
    Serial.printf("[MQTT]   → " #threshold " = %.1f\n", _v);                    \
  }                                                                             \
} while(0)

#define HANDLE_SET_THRESHOLD_INT(buf, prefix, threshold, vmin, vmax) do {       \
  int _v = parseIntAfter((buf), (prefix));                                      \
  if (validateAndLogInt(_v, (vmin), (vmax), #threshold)) {                      \
    if ((threshold) != _v) { (threshold) = _v; thresholdsSaveDebounced(); }     \
    Serial.printf("[MQTT]   → " #threshold " = %d\n", _v);                      \
  }                                                                             \
} while(0)

/* Threshold save with debounce — cegah flash wear dari rapid command */
static void thresholdsSaveDebounced() {
  unsigned long now = millis();
  if ((unsigned long)(now - g_lastNvsSave) >= 5000) {
    thresholdsSaveToNvs();
    g_lastNvsSave = now;
  } else {
    Serial.println("[NVS]   Skip save (debounce 5s)");
  }
}

static void mqttCallback(char* topic, byte* payload, unsigned int len) {
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

  if (strcmp(upper, "PING") == 0) {
    /* Queue response — jangan publish langsung dari callback */
    g_cmdRespTopic = TOPIC_PRESENCE;
    strncpy(g_cmdRespBuf, "online", sizeof(g_cmdRespBuf) - 1);
    g_cmdRespBuf[sizeof(g_cmdRespBuf) - 1] = '\0';
    g_cmdRespRetained = true;
    g_cmdRespPending = true;
    Serial.println("[MQTT]   → PING replied");

  } else if (strcmp(upper, "REBOOT") == 0) {
    Serial.println("[MQTT]   → REBOOT dalam 1 detik...");
    delay(1000);
    ESP.restart();

  } else if (strcmp(upper, "GET_STATE") == 0) {
    /* Queue state — publish dari loop */
    g_cmdRespTopic = TOPIC_STATE;
    strncpy(g_cmdRespBuf, g_patientState, sizeof(g_cmdRespBuf) - 1);
    g_cmdRespBuf[sizeof(g_cmdRespBuf) - 1] = '\0';
    g_cmdRespRetained = true;
    g_cmdRespPending = true;
    Serial.printf("[MQTT]   → State queued: %s\n", g_patientState);

  } else if (strcmp(upper, "GET_THRESHOLDS") == 0) {
    /* B1: Gunakan TOPIC_CMD sebagai reply, bukan TOPIC_STATE */
    /* I1: StaticJsonDocument sebagai ganti DynamicJsonDocument        */
    StaticJsonDocument<256> doc;
    doc["cmd"]            = "THRESHOLDS";
    doc["spo2_warn"]      = SPO2_WARNING_LOW;
    doc["spo2_crit"]      = SPO2_CRITICAL_LOW;
    doc["hr_warn_low"]    = HR_WARNING_LOW;
    doc["hr_warn_high"]   = HR_WARNING_HIGH;
    doc["hr_crit_low"]    = HR_CRITICAL_LOW;
    doc["hr_crit_high"]   = HR_CRITICAL_HIGH;
    doc["temp_warn"]      = TEMP_WARNING_HIGH;
    doc["temp_crit"]      = TEMP_CRITICAL_HIGH;
    char buf[256];
    serializeJson(doc, buf, sizeof(buf));   /* N1 */
    g_cmdRespTopic = TOPIC_CMD;
    strncpy(g_cmdRespBuf, buf, sizeof(g_cmdRespBuf) - 1);
    g_cmdRespBuf[sizeof(g_cmdRespBuf) - 1] = '\0';
    g_cmdRespRetained = false;
    g_cmdRespPending = true;
    Serial.printf("[MQTT]   → Thresholds queued: %s\n", buf);

  } else if (strcmp(upper, "ACK_ALARM") == 0) {
    /* B2: Tidak publish ke TOPIC_PRESENCE — hanya silent buzzer lokal */
    g_alarmAcknowledged = true;
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
  } else {
    Serial.printf("[MQTT]   → Perintah tidak dikenal: %s\n", p);
  }
}

/* ------------------------------------------------------------------ */
/*  NVS Threshold Persistence — macros untuk eliminasi duplikasi       */
/* ------------------------------------------------------------------ */
#define NVS_SAVE_FLOAT(p, k, v)  do { (p).putFloat((k), (v)); } while(0)
#define NVS_SAVE_INT(p, k, v)    do { (p).putInt((k), (v));   } while(0)
#define NVS_LOAD_FLOAT(p, k, v)  do { float _v = (p).getFloat((k), -1); if (_v != -1.0f) (v) = _v; } while(0)
#define NVS_LOAD_INT(p, k, v)    do { int _v   = (p).getInt((k), -1);   if (_v != -1) (v) = _v; } while(0)

static void thresholdsSaveToNvs() {
  Preferences prefs;
  prefs.begin("thresholds", false);
  NVS_SAVE_FLOAT(prefs, "spo2_warn",  SPO2_WARNING_LOW);
  NVS_SAVE_FLOAT(prefs, "spo2_crit",  SPO2_CRITICAL_LOW);
  NVS_SAVE_INT(prefs, "hr_warn_low",  HR_WARNING_LOW);
  NVS_SAVE_INT(prefs, "hr_warn_high", HR_WARNING_HIGH);
  NVS_SAVE_INT(prefs, "hr_crit_low",  HR_CRITICAL_LOW);
  NVS_SAVE_INT(prefs, "hr_crit_high", HR_CRITICAL_HIGH);
  NVS_SAVE_FLOAT(prefs, "temp_warn",  TEMP_WARNING_HIGH);
  NVS_SAVE_FLOAT(prefs, "temp_crit",  TEMP_CRITICAL_HIGH);
  prefs.end();
  Serial.println("[NVS] Thresholds saved");
}

static void thresholdsLoadFromNvs() {
  Preferences prefs;
  prefs.begin("thresholds", true);
  NVS_LOAD_FLOAT(prefs, "spo2_warn",  SPO2_WARNING_LOW);
  NVS_LOAD_FLOAT(prefs, "spo2_crit",  SPO2_CRITICAL_LOW);
  NVS_LOAD_INT(prefs, "hr_warn_low",  HR_WARNING_LOW);
  NVS_LOAD_INT(prefs, "hr_warn_high", HR_WARNING_HIGH);
  NVS_LOAD_INT(prefs, "hr_crit_low",  HR_CRITICAL_LOW);
  NVS_LOAD_INT(prefs, "hr_crit_high", HR_CRITICAL_HIGH);
  NVS_LOAD_FLOAT(prefs, "temp_warn",  TEMP_WARNING_HIGH);
  NVS_LOAD_FLOAT(prefs, "temp_crit",  TEMP_CRITICAL_HIGH);
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
  mqttClient.setKeepAlive(MQTT_KEEPALIVE);
  mqttClient.setCallback(mqttCallback);
  connectMqtt();     /* sudah publish initial state */
}

void mqttLoop() {
  if (WiFi.status() != WL_CONNECTED) connectWifi();
  if (!mqttClient.connected())        connectMqtt();
  mqttClient.loop();

  /* Kirim response yang di-queue dari callback (re-entrancy safety) */
  if (g_cmdRespPending && mqttClient.connected()) {
    mqttClient.publish(g_cmdRespTopic, g_cmdRespBuf, g_cmdRespRetained);
    g_cmdRespPending = false;
  }
}

bool mqttIsConnected() {
  return mqttClient.connected();
}

void mqttPublishState(const char* state) {
  if (!mqttClient.connected()) return;
  mqttClient.publish(TOPIC_STATE, state, true);
  Serial.printf("[MQTT] State: %s\n", state);
}

void mqttSetState(const char* state) {
  g_patientState = state;
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
  serializeJson(doc, buf, sizeof(buf));
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
  serializeJson(doc, buf, sizeof(buf));
  return publishJsonTo(TOPIC_ALARM, buf, "Alarm");
}
