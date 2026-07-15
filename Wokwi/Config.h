/**
 * @file    Config.h
 * @brief   Deklarasi eksternal konstanta sistem
 * @project IoT Dev Comp TETI 2026
 *
 * Seluruh nilai didefinisikan di Config.cpp (kecuali pin GPIO
 * dan konstanta hardware murni yang tetap #define).
 *
 * Mengikuti pola modular Mentor_Example:
 *   Config.h  → extern declaration
 *   Config.cpp → definition
 */
#ifndef CONFIG_H
#define CONFIG_H

#include <Arduino.h>

/* ------------------------------------------------------------------ */
/*  WiFi                                                               */
/* ------------------------------------------------------------------ */
extern const char* WIFI_SSID;
extern const char* WIFI_PASS;
extern const char* WIFI_AP_NAME;   /* Nama AP untuk WiFiManager portal */
extern const char* WIFI_AP_PASS;   /* Password AP portal               */

/* ------------------------------------------------------------------ */
/*  MQTT Broker                                                        */
/* ------------------------------------------------------------------ */
extern const char*    MQTT_BROKER;
extern const uint16_t MQTT_PORT;
extern const uint16_t MQTT_TLS_PORT;
/* NOTE: MQTT_USE_TLS adalah #define 0/1 (bukan true/false) karena
   preprocessor C tidak mengenali true/false sebagai konstanta #if.
   1 = aktifkan TLS port 8883 (butuh WiFiClientSecure + cert).        */
#define MQTT_USE_TLS 0
extern const int      MQTT_KEEPALIVE;
/* MQTT_CLIENT_ID sekarang digenerate dinamis dari MAC di MQTT_handler.cpp */

/* ------------------------------------------------------------------ */
/*  MQTT Topics                                                        */
/* ------------------------------------------------------------------ */
extern const char* TOPIC_VITALS;     /* data vital tiap 1 detik         */
extern const char* TOPIC_ALARM;      /* event alarm saat transisi       */
extern const char* TOPIC_STATE;      /* state pasien retained           */
extern const char* TOPIC_PRESENCE;   /* LWT online/offline              */
extern const char* TOPIC_CMD;        /* perintah masuk (subscribe)      */

/* ------------------------------------------------------------------ */
/*  Pin Definitions — Aktuator (#define tetap karena compile-time)     */
/* ------------------------------------------------------------------ */
#define PIN_LED_RED         25
#define PIN_LED_YELLOW      33
#define PIN_LED_GREEN       26
#define PIN_BUZZER          27

/* ------------------------------------------------------------------ */
/*  OLED SSD1306 (#define tetap karena compile-time)                   */
/* ------------------------------------------------------------------ */
#define OLED_SDA            21
#define OLED_SCL            22
#define OLED_ADDRESS        0x3C
#define SCREEN_WIDTH        128
#define SCREEN_HEIGHT       64
/* Posisi layout OLED — diganti jadi konstanta agar mudah diubah       */
#define OLED_STATUS_X       2
#define OLED_STATUS_Y       3
#define OLED_STATUS_H       13
#define OLED_SPO2_X         0
#define OLED_SPO2_VALUE_X   50
#define OLED_SPO2_Y         17
#define OLED_HR_X           0
#define OLED_HR_VALUE_X     50
#define OLED_HR_Y           35
#define OLED_TEMP_X         0
#define OLED_TEMP_VALUE_X   50
#define OLED_TEMP_Y         53
#define OLED_MQTT_X         120
#define OLED_MQTT_Y         3

/* ------------------------------------------------------------------ */
/*  Threshold Alarm — SpO2 (%) — non-const agar bisa diubah via MQTT  */
/* ------------------------------------------------------------------ */
extern float SPO2_WARNING_LOW;
extern float SPO2_CRITICAL_LOW;
/* Validation ranges untuk SET_THRESHOLD via MQTT */
#define SPO2_VALID_MIN      80.0f
#define SPO2_VALID_MAX      100.0f

/* ------------------------------------------------------------------ */
/*  Threshold Alarm — Heart Rate (bpm)                                */
/* ------------------------------------------------------------------ */
extern int HR_WARNING_LOW;
extern int HR_WARNING_HIGH;
extern int HR_CRITICAL_LOW;
extern int HR_CRITICAL_HIGH;
/* Validation ranges */
#define HR_VALID_MIN        20
#define HR_VALID_MAX        250

/* ------------------------------------------------------------------ */
/*  Threshold Alarm — Suhu Tubuh (degC)                               */
/* ------------------------------------------------------------------ */
extern float TEMP_WARNING_HIGH;
extern float TEMP_CRITICAL_HIGH;
/* Validation ranges */
#define TEMP_VALID_MIN      34.0f
#define TEMP_VALID_MAX      42.0f

/* ------------------------------------------------------------------ */
/*  Patient Status string constants — M2: hilangkan hardcoded string   */
/* ------------------------------------------------------------------ */
extern const char* STATUS_NORMAL;
extern const char* STATUS_WARNING;
extern const char* STATUS_CRITICAL;

/* ------------------------------------------------------------------ */
/*  Timing                                                             */
/* ------------------------------------------------------------------ */
extern const unsigned long PUBLISH_INTERVAL;    /* ms — 1 baris CSV/dtk  */
extern const unsigned long BUZZER_CRITICAL_INTERVAL;  /* ms — beep period Critical */
extern const unsigned long BUZZER_WARNING_INTERVAL;   /* ms — beep period Warning */
extern const int           BUZZER_CRITICAL_FREQ;      /* Hz — frekuensi Critical   */
extern const int           BUZZER_WARNING_FREQ;       /* Hz — frekuensi Warning    */
extern const unsigned long BUZZER_CRITICAL_DURATION;  /* ms — durasi beep Critical */
extern const unsigned long BUZZER_WARNING_DURATION;   /* ms — durasi beep Warning  */

/* ------------------------------------------------------------------ */
/*  Moving Average Filter (#define karena dipakai sbg ukuran array)    */
/* ------------------------------------------------------------------ */
#define FILTER_SIZE         5

/* ------------------------------------------------------------------ */
/*  Sumber Data CSV (SPIFFS)                                          */
/* ------------------------------------------------------------------ */
extern const char* CSV_FILE_PATH;
extern const char* DATA_LOG_PATH;    /* Fallback buffer saat MQTT offline */
extern const size_t DATA_LOG_MAX_LINES;

/* ------------------------------------------------------------------ */
/*  OLED Sleep                                                         */
/* ------------------------------------------------------------------ */
extern const unsigned long OLED_IDLE_TIMEOUT;  /* ms — matikan OLED jika idle */

#endif /* CONFIG_H */
