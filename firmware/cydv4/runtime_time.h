#pragma once
#include <Arduino.h>
#include <esp_timer.h>
inline uint32_t uptimeSeconds(){return (uint32_t)(esp_timer_get_time()/1000000LL);}
inline bool beforeDeadline(unsigned long until){return until && (int32_t)(millis()-until)<0;}
