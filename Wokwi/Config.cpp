/**
 * @file    Config.cpp
 * @brief   Definisi konstanta & shared globals
 * @project IoT Dev Comp TETI 2026
 *
 * Mengikuti pola Mentor_Example: edit nilai di sini, BUKAN di Config.h.
 */
#include "Config.h"

/* ------------------------------------------------------------------ */
/*  WiFi — default untuk Wokwi. connectWifi() di MQTT_handler.cpp     */
/*  menggunakan WIFI_SSID/WIFI_PASS sebagai fallback setelah WiFiMgr  */
/* ------------------------------------------------------------------ */
const char* WIFI_SSID    = "Wokwi-GUEST";
const char* WIFI_PASS    = "";
const char* WIFI_AP_NAME = "ESP32_VitalMon";
const char* WIFI_AP_PASS = "config123";

/* ------------------------------------------------------------------ */
/*  MQTT Broker — test.mosquitto.org (broker publik Eclipse)          */
/*  Untuk produksi: ganti ke broker sendiri + auth + TLS (port 8883). */
/* ------------------------------------------------------------------ */
const char*    MQTT_BROKER     = "test.mosquitto.org";
const uint16_t MQTT_PORT       = 1883;
const uint16_t MQTT_TLS_PORT   = 8883;
/* MQTT_USE_TLS ada di Config.h sebagai #define */
/* MQTT_CLIENT_ID digenerate dinamis di MQTT_handler.cpp */
const int      MQTT_KEEPALIVE  = 60;

/* ------------------------------------------------------------------ */
/*  MQTT Topics                                                        */
/* ------------------------------------------------------------------ */
const char* TOPIC_VITALS    = "hospital/patient/001/vitals";
const char* TOPIC_ALARM     = "hospital/patient/001/alarm";
const char* TOPIC_STATE     = "hospital/patient/001/state";
const char* TOPIC_PRESENCE  = "hospital/patient/001/presence";
const char* TOPIC_CMD       = "hospital/patient/001/cmd";

/* ------------------------------------------------------------------ */
/*  Threshold Alarm — SpO2 (%) — non-const agar bisa diubah via MQTT  */
/* ------------------------------------------------------------------ */
float SPO2_WARNING_LOW   = 94.0f;
float SPO2_CRITICAL_LOW  = 90.0f;

/* ------------------------------------------------------------------ */
/*  Threshold Alarm — Heart Rate (bpm)                                */
/* ------------------------------------------------------------------ */
int HR_WARNING_LOW      = 60;
int HR_WARNING_HIGH     = 100;
int HR_CRITICAL_LOW     = 50;
int HR_CRITICAL_HIGH    = 120;

/* ------------------------------------------------------------------ */
/*  Threshold Alarm — Suhu Tubuh (degC)                               */
/* ------------------------------------------------------------------ */
float TEMP_WARNING_HIGH   = 37.5f;
float TEMP_CRITICAL_HIGH  = 38.5f;

/* ------------------------------------------------------------------ */
/*  Patient Status string constants — M2                                 */
/* ------------------------------------------------------------------ */
const char* STATUS_NORMAL   = "Normal";
const char* STATUS_WARNING  = "Warning";
const char* STATUS_CRITICAL = "Critical";

/* ------------------------------------------------------------------ */
/*  Timing                                                             */
/* ------------------------------------------------------------------ */
const unsigned long PUBLISH_INTERVAL         = 1000;   /* ms */
const unsigned long BUZZER_CRITICAL_INTERVAL = 600;    /* ms — rapid beep */
const unsigned long BUZZER_WARNING_INTERVAL  = 2000;   /* ms — slow beep  */
const int           BUZZER_CRITICAL_FREQ     = 1200;   /* Hz */
const int           BUZZER_WARNING_FREQ      = 800;    /* Hz */
const unsigned long BUZZER_CRITICAL_DURATION = 200;    /* ms */
const unsigned long BUZZER_WARNING_DURATION  = 400;    /* ms */

/* ------------------------------------------------------------------ */
/*  Sumber Data CSV (SPIFFS)                                          */
/* ------------------------------------------------------------------ */
const char* CSV_FILE_PATH = "/vitals_2024.csv";
const char* DATA_LOG_PATH = "/data_log.csv";
const size_t DATA_LOG_MAX_LINES = 500;
const unsigned long OLED_IDLE_TIMEOUT = 30000;  /* 30 detik */
