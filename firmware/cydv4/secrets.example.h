// ============================================================================
//  secrets.example.h — ОБРАЗЕЦ. Скопировать в secrets.h и заполнить своими.
//
//    copy secrets.example.h secrets.h        (Windows)
//    cp secrets.example.h secrets.h          (Linux/macOS)
//
//  secrets.h лежит в .gitignore: свои ключи в репозиторий не попадут.
//  Достаточно заполнить Wi-Fi и IP платы — остальное можно оставить пустым:
//  без ключей Tuya и OpenWeatherMap прошивка тоже работает, просто без
//  облачных счётчиков и без погоды (экран покажет «НЕТ ДАННЫХ»).
// ============================================================================
#pragma once

// ---- Wi-Fi ---------------------------------------------------------------
#define WIFI_SSID      "MyWiFi"
#define WIFI_PASS      "MyPassword"

// ---- Tuya (необязательно) -------------------------------------------------
// Ключи берутся из приложения Tuya IoT Platform для своего устройства.
#define TUYA_DEVICE_ID "your-device-id"
#define TUYA_LOCAL_KEY "your-local-key-16b"
#define TUYA_CLIENT_ID "your-client-id"
#define TUYA_SECRET    "your-client-secret"

// ---- Устройства в локальной сети -----------------------------------------
#define RELAY_IP       "192.168.1.25"      // реле/умная розетка Tuya
#define PC_API_URL     "http://192.168.1.20:8090"   // HTTP-API ПК (DomMonitor)

// ---- Погода: OpenWeatherMap (необязательно) -------------------------------
// Бесплатный ключ: https://home.openweathermap.org/api_keys
#define OWM_KEY        ""
// Координаты города строками, как в URL: Адлер — 43.43 / 39.92
#define WEATHER_LAT    "43.43"
#define WEATHER_LON    "39.92"

// ---- MQTT / Home Assistant (необязательно) --------------------------------
// MQTT_HOST пустой -> MQTT выключен.
// Менять без перепрошивки: http://<IP платы>/mqtt?host=...&port=1883&user=...&pass=...
//                        http://<IP платы>/mqtt?off=1   — выключить
#define MQTT_HOST      ""
#define MQTT_PORT      1883
#define MQTT_USER      "mqtt"
#define MQTT_PASS      ""
#define MQTT_BASE      "dom_energy"
