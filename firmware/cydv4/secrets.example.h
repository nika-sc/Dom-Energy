// ============================================================================
//  secrets.example.h — ОБРАЗЕЦ. Скопировать в secrets.h и заполнить.
//
//    copy secrets.example.h secrets.h
//
//  MQTT_HOST пустой  ->  MQTT выключен (поведение прошивки прежнее).
//  Менять настройки без перепрошивки:
//    HTTP:  http://192.168.1.51/mqtt?host=192.168.1.72&port=1883&user=mqtt&pass=ПАРОЛЬ
//           http://192.168.1.51/mqtt?off=1     — выключить
//           http://192.168.1.51/mqtt           — состояние (JSON)
//    serial: MQTTSET "192.168.1.72" 1883 "mqtt" "пароль"
//            MQTTSET ""  — выключить,  MQTTSET — показать текущие
//
//  Топики при MQTT_BASE "dom_energy":
//    dom_energy/state                 состояние (JSON, раз в 30 с или при изменении)
//    dom_energy/status                online / offline (LWT, retained)
//    dom_energy/cmd                   команды: relay_on | relay_off | refresh
//    dom_energy/result                ответ на команду
//    homeassistant/sensor/dom_energy/<key>/config        автообнаружение HA
//    homeassistant/binary_sensor/dom_energy/<key>/config
// ============================================================================
#pragma once

#define MQTT_HOST      "192.168.1.72" // брокер Mosquitto
#define MQTT_PORT      1883
#define MQTT_USER      "mqtt"
#define MQTT_PASS      "пароль"
#define MQTT_BASE      "dom_energy"
