/**
 * @file    MQTT_handler.h
 * @brief   Header modul koneksi WiFi dan MQTT
 * @project IoT Dev Comp TETI 2026
 *
 * Mengekspos API publik untuk inisialisasi, loop maintenance,
 * publikasi data vital, dan publikasi alarm.
 */

#ifndef MQTT_HANDLER_H
#define MQTT_HANDLER_H

/* Tingkatkan buffer MQTT dari default 256 → 512 agar GET_THRESHOLDS
   dan payload besar lain tidak terpotong */
#define MQTT_MAX_PACKET_SIZE 512
#include <PubSubClient.h>
#include "Config.h"

/* ------------------------------------------------------------------ */
/*  Public API                                                         */
/* ------------------------------------------------------------------ */

/** Inisialisasi WiFi, konfigurasi broker, dan koneksi MQTT awal. */
void mqttSetup();

/** Dipanggil setiap iterasi loop(): menjaga koneksi dan memproses pesan. */
void mqttLoop();

/**
 * Publikasi data vital ke topic TOPIC_VITALS.
 * @param spo2          Nilai SpO2 dalam persen.
 * @param heartRate     Nilai heart rate dalam bpm.
 * @param temperature   Nilai suhu tubuh dalam derajat Celsius.
 * @param patientStatus String status: "Normal", "Warning", atau "Critical".
 * @param csvTimestamp  String timestamp dari dataset CSV.
 * @return true jika publish berhasil, false jika tidak terhubung.
 */
bool mqttPublishVitals(float spo2, int heartRate, float temperature, const char* patientStatus, const char* csvTimestamp);

/**
 * Publikasi payload alarm ke topic TOPIC_ALARM.
 * Hanya dipanggil saat status Warning atau Critical.
 * @param patientStatus String status saat alarm dipicu.
 * @param spo2          Nilai SpO2 saat alarm.
 * @param heartRate     Nilai heart rate saat alarm.
 * @param temperature   Nilai suhu saat alarm.
 * @param csvTimestamp  String timestamp dari dataset CSV.
 * @return true jika publish berhasil, false jika tidak terhubung.
 */
bool mqttPublishAlarm(const char* patientStatus, float spo2, int heartRate, float temperature, const char* csvTimestamp);

/**
 * Publikasi state pasien ke TOPIC_STATE (retained).
 * Subscriber baru langsung mendapat state terakhir tanpa nunggu siklus.
 * @param state String status: "Normal", "Warning", atau "Critical".
 */
void mqttPublishState(const char* state);

/** Catat state terkini tanpa publish (digunakan reconnect untuk re-publish). */
void mqttSetState(const char* state);

/** @return state terakhir yang tercatat. */
const char* mqttGetState();

/** @return true jika client MQTT sedang terhubung ke broker. */
bool mqttIsConnected();

/**
 * Alarm acknowledgement — digunakan untuk silent buzzer dari dashboard.
 * Dipanggil dari main loop untuk cek apakah alarm sudah di-ack.
 */
bool mqttIsAlarmAcknowledged();
void mqttResetAlarmAck();

#endif /* MQTT_HANDLER_H */
