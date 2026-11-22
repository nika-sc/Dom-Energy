// ============================================================================
//  ДОМ ЭНЕРГИЯ 2.0 — прошивка для ESP32-2432S028 (экран 2.8" 240x320)
//
//  Экраны:
//    1. ЭНЕРГИЯ   — напряжение, температура, мощность, расход, стоимость
//    2. ГРАФИК    — напряжение за последние 30 минут
//    3. ПОГОДА    — ВАШЕГО ГОРОДА: температура, ветер, влажность (open-meteo.com)
//    4. НАСТРОЙКИ — смена Wi-Fi с экрана, без перепрошивки
//    5. РИСОВАНИЕ — холст с палитрой
//
//  Тема: ВСЕГДА светлая бело-синяя (фон белый, текст тёмный, акцент #2563EB).
//  Переключения по часам больше нет.
// ============================================================================

#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <TFT_eSPI.h>
#include <Preferences.h>
#include <HTTPClient.h>
#include <SPI.h>
#include <SD.h>
#include <time.h>

#include "font5x7.h"

int rndRad(int w, int h);
void updatePeriods();
#include <esp_task_wdt.h>

#include <mbedtls/gcm.h>
#include <mbedtls/md.h>

// ---------------------------------------------------------------- настройки
const char* FW_VER = "4.2";

const char* DEF_SSID = "YOUR_WIFI_SSID";
const char* DEF_PASS = "YOUR_WIFI_PASSWORD";

const char* DEVICE_ID = "YOUR_TUYA_DEVICE_ID";
const char* LOCAL_KEY = "YOUR_TUYA_LOCAL_KEY";
const char* RELAY_IP  = "192.168.1.25";
const uint16_t RELAY_PORT = 6668;

const char* PC_API = "http://192.168.1.20:8090";

// ---- Tuya Cloud: даёт напряжение, мощность и счётчик без компьютера ----
const char* TUYA_CID    = "YOUR_TUYA_CLIENT_ID";
const char* TUYA_SECRET = "YOUR_TUYA_SECRET";
const char* TUYA_HOST   = "openapi.tuyaeu.com";

// wttr.in отдаёт компактную строку: +11°C|86%|↑5km/h|Partly Cloudy
const char* WEATHER_URL = "http://wttr.in/your city?format=%t|%h|%w|%C";

#define CMD_SESS_START   3
#define CMD_SESS_RESP    4
#define CMD_SESS_FINISH  5
#define CMD_DP_QUERY     16
#define HEADER_LEN       18
#define IV_LEN           12
#define TAG_LEN          16


// ---------------------------------------------------------------- объекты
TFT_eSPI tft = TFT_eSPI();

// ---------------------------------------------------------------- экран
enum { SCR_MAIN = 0, SCR_VOLT, SCR_TEMP, SCR_POWER, SCR_TODAY, SCR_WEATHER };
int gScreen = SCR_MAIN;
unsigned long gLastDraw = 0;
uint32_t gLastSig = 0;          // отпечаток данных, по нему решаем, нужна ли перерисовка
unsigned long gLastWinFull = 0; // когда окно перерисовывали целиком (для графика)
bool gNoClear = false;          // не заливать весь экран — частичное обновление

// ---------------------------------------------------------------- захват экрана
// Все примитивы рисования идут через ui*(). При gCap=true каждый вызов
// дублируется в serial командами S/R/C/L/F — по ним shot.py собирает PNG.
// Буфер передатчика не ждём бесконечно: если хост не читает, захват
// прекращается сам, плата продолжает работать.
bool     gCap = false;
uint16_t gCapLines = 0;
unsigned long gCapStartMs = 0;

bool capOut(const char* s, int n) {
  if (!gCap || n <= 0) return false;
  if (gCapLines == 0) gCapStartMs = millis();
  if (gCapLines > 30000 || millis() - gCapStartMs > 25000UL) { gCap = false; return false; }

  unsigned long t0 = millis();
  while (Serial.availableForWrite() < n + 4) {
    if (!gCap || millis() - t0 > 1200UL) { gCap = false; return false; }
    delay(2);
    esp_task_wdt_reset();
  }
  Serial.write((const uint8_t*)s, n);
  gCapLines++;
  if ((gCapLines & 15) == 0) { delay(1); esp_task_wdt_reset(); }
  return true;
}

void uiScreen(uint16_t c) {
  if (gCap) { char b[16]; capOut(b, snprintf(b, sizeof(b), "F %04X\n", (unsigned)c)); }
  if (!gNoClear) tft.fillScreen(c);
}
void uiFill(int x, int y, int w, int h, uint16_t c) {
  if (w <= 0 || h <= 0) return;
  if (gCap) { char b[48]; capOut(b, snprintf(b, sizeof(b), "S %d %d %d %d %04X\n", x, y, w, h, (unsigned)c)); }
  tft.fillRect(x, y, w, h, c);
}
void uiFillRound(int x, int y, int w, int h, int r, uint16_t c) {
  if (w <= 0 || h <= 0) return;
  if (gCap) { char b[56]; capOut(b, snprintf(b, sizeof(b), "R %d %d %d %d %d %04X\n", x, y, w, h, r, (unsigned)c)); }
  tft.fillRoundRect(x, y, w, h, r, c);
}
void uiCircle(int x, int y, int r, uint16_t c) {
  if (r <= 0) return;
  if (gCap) { char b[32]; capOut(b, snprintf(b, sizeof(b), "C %d %d %d %04X\n", x, y, r, (unsigned)c)); }
  tft.fillCircle(x, y, r, c);
}
void uiLine(int x1, int y1, int x2, int y2, uint16_t c) {
  if (gCap) { char b[40]; capOut(b, snprintf(b, sizeof(b), "L %d %d %d %d %04X\n", x1, y1, x2, y2, (unsigned)c)); }
  tft.drawLine(x1, y1, x2, y2, c);
}
void uiHLine(int x, int y, int w, uint16_t c) { uiFill(x, y, w, 1, c); }
void uiVLine(int x, int y, int h, uint16_t c) { uiFill(x, y, 1, h, c); }
void uiRect(int x, int y, int w, int h, uint16_t c) {
  uiHLine(x, y, w, c); uiHLine(x, y + h - 1, w, c);
  uiFill(x, y, 1, h, c); uiFill(x + w - 1, y, 1, h, c);
}
// окружность без заливки: в PNG попадает 32 отрезка
void uiRing(int cx, int cy, int r, uint16_t c) {
  if (gCap && r > 1) {
    int px = cx + r, py = cy;
    for (int i = 1; i <= 32; i++) {
      float a = i * 2.0f * PI / 32.0f;
      int x = cx + (int)(cos(a) * r);
      int y = cy + (int)(sin(a) * r);
      char b[40];
      capOut(b, snprintf(b, sizeof(b), "L %d %d %d %d %04X\n", px, py, x, y, (unsigned)c));
      px = x; py = y;
    }
  }
  tft.drawCircle(cx, cy, r, c);
}

// ---------------------------------------------------------------- тема
// Тема ОДНА и всегда светлая, бело-синяя:
//   фон  — белый с холодком, поверхности — белые, текст — тёмный графит,
//   акцент — синий #2563EB (напряжение, графики, рамка шапки).
// Ночной палитры больше нет: переключение по часам убрано.
bool gNight = false;            // оставлен для совместимости, всегда false

uint16_t cBg, cSurf, cElev, cText, cMuted, cLine, cAccent, cAccent2;
uint16_t C_VOLT, C_TEMP, C_POWER, C_KWH, C_MONEY, C_OK, C_BAD, C_SKY;
uint16_t cSky, cWarn, cGood, cBadC, cGhost, cTEMP_ACC, cPOWER_ACC;

void pickTheme() {
  gNight = false;

  cBg      = tft.color565(248, 250, 255);   // почти белый, с холодком
  cSurf    = tft.color565(255, 255, 255);   // карточки
  cElev    = tft.color565(239, 244, 255);   // плашки/кнопки
  cText    = tft.color565(15, 23, 42);      // тёмный графит
  cMuted   = tft.color565(100, 116, 139);   // подписи
  cLine    = tft.color565(219, 228, 245);   // рамки
  cAccent  = tft.color565(37, 99, 235);     // #2563EB — синий акцент
  cAccent2 = tft.color565(29, 78, 216);     // #1D4ED8
  C_KWH    = tft.color565(22, 163, 74);     // расход — зелёный
  C_OK     = C_KWH;
  C_BAD    = tft.color565(220, 38, 38);
  analogWrite(21, 255);                     // подсветка всегда на полную

  C_VOLT  = cAccent;
  C_TEMP  = tft.color565(124, 58, 237);     // фиолетовый
  C_POWER = tft.color565(217, 119, 6);      // янтарный
  C_MONEY = tft.color565(219, 39, 119);     // рубли — розовый
  C_SKY   = tft.color565(2, 132, 199);      // небо
  cTEMP_ACC  = tft.color565(139, 92, 246);
  cPOWER_ACC = tft.color565(245, 158, 11);
  cSky    = C_SKY;
  cGood   = C_KWH;
  cBadC   = C_BAD;
  cWarn   = tft.color565(217, 119, 6);      // предупреждение — тёмно-янтарный
  cGhost  = tft.color565(226, 232, 240);    // погасшие сегменты
}

// буквы с разрядкой — для подписей
void drawSpaced(int x, int y, const char* s, uint16_t fg, uint16_t bg, int gap, uint8_t font) {
  tft.setTextDatum(TL_DATUM);
  tft.setTextColor(fg, bg);
  int cx = x;
  for (const char* p = s; *p; p++) {
    char b[2] = { *p, 0 };
    if (*p != ' ') tft.drawString(b, cx, y, font);
    cx += tft.textWidth(b, font) + gap;
  }
}

// ---------------------------------------------------------------- объекты
Preferences prefs;

WiFiClient sock;
uint32_t seqNo = 1;
uint8_t  sessionKey[16];
bool     sessionReady = false;

// ---------------------------------------------------------------- данные
float gVoltage = 0, gTemp = 0, gPower = 0, gEnergy = 0, gCost = 0;
float gTodayKwh = 0, gTodayCost = 0;
bool  gTempValid = false;      // температура реально пришла (а не ноль по умолчанию)

// ---- тарифы: ночь 23:00-06:00
const float TARIFF_DAY   = 8.39f;
const float TARIFF_NIGHT = 4.49f;

// нормы напряжения, ГОСТ 32144-2013
const float V_NORM_LO = 207.0f, V_NORM_HI = 253.0f;
const float V_WARN_LO = 190.0f, V_WARN_HI = 265.0f;

float tariffForHour(int h) {
  return (h >= 23 || h < 6) ? TARIFF_NIGHT : TARIFF_DAY;
}

// средний тариф при равномерной нагрузке: 7 ночных часов, 17 дневных
float avgTariffEstimate() {
  return (7.0f * TARIFF_NIGHT + 17.0f * TARIFF_DAY) / 24.0f;   // 7.2525
}

// цвет и словесная оценка напряжения по ГОСТ 32144-2013
// норма — синий акцент темы, отклонения — янтарный/красный
uint16_t voltColor(float v) {
  if (v < V_WARN_LO || v > V_WARN_HI) return C_BAD;    // авария
  if (v < V_NORM_LO || v > V_NORM_HI) return cWarn;    // предупреждение
  return cAccent;                                      // норма — синий
}
const char* voltVerdict(float v) {
  if (v < V_WARN_LO) return "АВАРИЯ";
  if (v < V_NORM_LO) return "НИЗКО";
  if (v <= V_NORM_HI) return "НОРМА";
  if (v <= V_WARN_HI) return "ВЫСОКО";
  return "АВАРИЯ";
}

// ---- собственный счёт «сегодня», без компьютера ----
float dayStartKwh   = -1;      // показание счётчика в начале суток
long  dayStamp      = -1;      // номер дня (дней от 1970)
float prevEnergyVal = -1;      // предыдущее показание для прироста
float myTodayKwh    = 0;       // мой расход за сегодня
float myTodayCost   = 0;       // моя стоимость за сегодня
bool  myTodayReady  = false;

// ---- вчера и месяц, переживают перезагрузку ----
float yKwh = 0, yCost = 0;
float mKwh = 0, mCost = 0;
float mBaseKwh = -1, prevMonthE = -1;
int   mMonthStamp = -2;
long  yDayKey = -2;

// ---- суточная статистика в RAM ----
float dayMinV = 999, dayMaxV = 0;
int   dayMinVMin = -1, dayMaxVMin = -1;
double daySumV = 0; long dayVSec = 0;
long  dayOutSec = 0;
float dayMinT = 999, dayMaxT = -999;
int   dayMinTMin = -1, dayMaxTMin = -1;
double daySumT = 0; long dayTSec = 0;
float dayMaxP = 0; int dayMaxPMin = -1;
double daySumP = 0; long dayPSec = 0;
float statDayKwh = 0, statNightKwh = 0;
bool  statsSeeded = false;

// 24-часовой график с шагом 5 минут (288 точек)
#define H24_N 288
float   h24V[H24_N], h24T[H24_N], h24P[H24_N];
uint8_t h24Mask[H24_N];

// ---- сканер карты ----
long daysFromCivil(int y, unsigned m, unsigned d);
long nowLocalEpoch();
float tariffAvg(long a, long b);
bool scanSdRange(long start, long end, float nowKwh,
                 float* outKwh, float* outCost, float* outDay, float* outNight);
bool computeTodayFromSd(int year, int mon, int day, float nowKwh,
                        float* outKwh, float* outCost);
bool sdMonthBase(int year, int mon, float* outKwh);
bool seedStatsFromSd();


// Проверяет смену суток и копит расход/стоимость по приросту счётчика.
// Начало суток хранится в энергонезависимой памяти — переживает перезагрузку.
void loadToday() {
  Preferences p;
  p.begin("today", true);
  dayStamp    = p.getLong("dayl", -1);   // новый ключ: старый "day" был int
  dayStartKwh = p.getFloat("start", -1);
  myTodayKwh  = p.getFloat("kwh", 0);
  myTodayCost = p.getFloat("cost", 0);
  p.end();
}

void saveToday() {
  Preferences p;
  p.begin("today", false);
  p.putLong("dayl", dayStamp);
  p.putFloat("start", dayStartKwh);
  p.putFloat("kwh", myTodayKwh);
  p.putFloat("cost", myTodayCost);
  p.end();
}

// ---------------------------------------------------------------- статистика суток
void resetStats() {
  dayMinV = 999; dayMaxV = 0; dayMinVMin = -1; dayMaxVMin = -1;
  daySumV = 0; dayVSec = 0; dayOutSec = 0;
  dayMinT = 999; dayMaxT = -999; dayMinTMin = -1; dayMaxTMin = -1;
  daySumT = 0; dayTSec = 0;
  dayMaxP = 0; dayMaxPMin = -1; daySumP = 0; dayPSec = 0;
  statDayKwh = 0; statNightKwh = 0;
  for (int i = 0; i < H24_N; i++) { h24V[i] = 0; h24T[i] = 0; h24P[i] = 0; h24Mask[i] = 0; }
}

void statsTick() {
  static unsigned long last = 0;
  unsigned long now = millis();
  if (last == 0) { last = now; return; }
  if (now - last < 5000) return;
  int dt = (int)((now - last) / 1000);
  last = now;
  if (dt < 1) dt = 1;
  if (dt > 60) dt = 60;

  struct tm ti;
  bool tok = getLocalTime(&ti, 5);
  int minute = tok ? ti.tm_hour * 60 + ti.tm_min : -1;
  int slot = (minute >= 0) ? minute / 5 : 0;
  if (slot >= H24_N) slot = H24_N - 1;

  if (gVoltage > 50) {
    if (gVoltage < dayMinV) { dayMinV = gVoltage; dayMinVMin = minute; }
    if (gVoltage > dayMaxV) { dayMaxV = gVoltage; dayMaxVMin = minute; }
    daySumV += (double)gVoltage * dt;
    dayVSec += dt;
    if (gVoltage < V_NORM_LO || gVoltage > V_NORM_HI) dayOutSec += dt;
    h24V[slot] = gVoltage;
    h24Mask[slot] |= 1;
  }
  if (gTempValid && gTemp > -40 && gTemp < 100) {
    if (gTemp < dayMinT) { dayMinT = gTemp; dayMinTMin = minute; }
    if (gTemp > dayMaxT) { dayMaxT = gTemp; dayMaxTMin = minute; }
    daySumT += (double)gTemp * dt;
    dayTSec += dt;
    h24T[slot] = gTemp;
    h24Mask[slot] |= 2;
  }
  if (gPower >= 0) {
    if (gPower > dayMaxP) { dayMaxP = gPower; dayMaxPMin = minute; }
    daySumP += (double)gPower * dt;
    dayPSec += dt;
    h24P[slot] = gPower;
    h24Mask[slot] |= 4;
  }
}

void updateToday() {
  struct tm ti;
  if (!getLocalTime(&ti, 10)) return;
  long key = daysFromCivil(ti.tm_year + 1900, ti.tm_mon + 1, ti.tm_mday);

  if (dayStamp != key) {                       // новые сутки
    dayStamp = key;
    dayStartKwh = (gEnergy > 0) ? gEnergy : -1;
    prevEnergyVal = gEnergy;
    myTodayKwh = 0;
    myTodayCost = 0;
    myTodayReady = false;
    resetStats();
    statsSeeded = false;                       // засеем статистику нового дня из CSV
    saveToday();
  }

  if (dayStartKwh < 0) dayStartKwh = gEnergy;

  // раз в минуту сверяемся с картой: сумма по интервалам с тарифом,
  // взвешенным по времени, а не по моменту записи строки
  static unsigned long lastSd = 0;
  if (lastSd == 0 || millis() - lastSd > 60000UL) {
    lastSd = millis();
    float k1, k2, dK, nK;
    long start = key * 86400L;
    if (scanSdRange(start, 0, gEnergy, &k1, &k2, &dK, &nK)) {
      myTodayKwh = k1;
      myTodayCost = k2;
      statDayKwh = dK;
      statNightKwh = nK;
      myTodayReady = true;
      prevEnergyVal = gEnergy;
      gCost = myTodayCost;
      static unsigned long lastSave1 = 0;
      if (millis() - lastSave1 > 600000UL) { lastSave1 = millis(); saveToday(); }
      return;                 // карта точнее счёта от запуска
    }
  }

  if (gEnergy <= 0) return;          // нет показаний счётчика — не доначисляем

  // прирост счётчика — двигаем только вверх и на разумную величину
  if (prevEnergyVal >= 0 && gEnergy > prevEnergyVal) {
    float d = gEnergy - prevEnergyVal;
    if (d < 0.5f) {
      myTodayKwh += d;
      myTodayCost += d * tariffForHour(ti.tm_hour);
      if (ti.tm_hour >= 23 || ti.tm_hour < 6) statNightKwh += d;
      else                                   statDayKwh += d;
      prevEnergyVal = gEnergy;
    }
  } else if (prevEnergyVal < 0) {
    prevEnergyVal = gEnergy;
  }

  // контроль по разнице с началом суток: если она больше накопленного,
  // доначисляем разницу по текущему тарифу
  float diff = gEnergy - dayStartKwh;
  if (diff > myTodayKwh + 0.02f) {
    float add = diff - myTodayKwh;
    myTodayCost += add * tariffForHour(ti.tm_hour);
    if (ti.tm_hour >= 23 || ti.tm_hour < 6) statNightKwh += add;
    else                                   statDayKwh += add;
    myTodayKwh = diff;
  }
  myTodayReady = true;

  static unsigned long lastSave = 0;
  if (millis() - lastSave > 600000UL) { lastSave = millis(); saveToday(); }
}
bool  gRelay = false, gTuyaOk = false, gFromApi = false;
unsigned long gPhaseMs = 0;   // когда последний раз пришло напряжение от реле
int   gRssi = 0;
unsigned long gLastOk = 0;

#define GH_SAMPLES 120
float gHist[GH_SAMPLES];
int   gHistPos = 0, gHistCount = 0;
float gHistMin = 999, gHistMax = 0;

float wTemp = 0, wWind = 0, wHum = 0;
int   wCode = -1;
String wDescEn = "";
unsigned long wLast = 0;
time_t wTime = 0;
bool  wOk = false;

bool gSdOk = false;
int  gSdRows = 0;
unsigned long gSdLast = 0;
SPIClass sdSPI(HSPI);

// ---------------------------------------------------------------- история с карты
// Стоимость считается не по тарифу момента записи строки, а как
//   сумма ( Δэнергии × средний тариф на интервале времени ).
// Средний тариф учитывает границы 06:00 и 23:00, поэтому крупные шаги
// импортированных из облака строк больше не завышают счёт.
// Итог всегда приводится к kwh × средневзвешенный тариф и ограничен
// сверху дневным, снизу ночным тарифом.
struct SdRec { uint32_t t; float e; };
#define SD_REC_MAX 4096
static SdRec* sRec = nullptr;
static int sRecN = 0;

static int cmpSdRec(const void* a, const void* b) {
  uint32_t x = ((const SdRec*)a)->t, y = ((const SdRec*)b)->t;
  return x < y ? -1 : (x > y ? 1 : 0);
}

long daysFromCivil(int y, unsigned m, unsigned d) {
  y -= m <= 2;
  const int era = (y >= 0 ? y : y - 399) / 400;
  const unsigned yoe = (unsigned)(y - era * 400);
  const unsigned doy = (153u * (m + (m > 2 ? -3 : 9)) + 2u) / 5u + d - 1u;
  const unsigned doe = yoe * 365u + yoe / 4u - yoe / 100u + doy;
  return era * 146097L + (long)doe - 719468L;
}

long nowLocalEpoch() {
  struct tm ti;
  if (!getLocalTime(&ti, 5)) return 0;
  return daysFromCivil(ti.tm_year + 1900, ti.tm_mon + 1, ti.tm_mday) * 86400L
       + ti.tm_hour * 3600L + ti.tm_min * 60L + ti.tm_sec;
}

long lineEpoch(const String& line) {
  if (line.length() < 19) return 0;
  if (line[4] != '-' || line[7] != '-' || line[10] != ' ') return 0;
  int y  = line.substring(0, 4).toInt();
  int mo = line.substring(5, 7).toInt();
  int d  = line.substring(8, 10).toInt();
  int h  = line.substring(11, 13).toInt();
  int mi = line.substring(14, 16).toInt();
  int se = line.substring(17, 19).toInt();
  if (y < 2020 || mo < 1 || mo > 12 || d < 1 || d > 31) return 0;
  if (h < 0 || h > 23 || mi < 0 || mi > 59 || se < 0 || se > 59) return 0;
  return daysFromCivil(y, (unsigned)mo, (unsigned)d) * 86400L
       + h * 3600L + mi * 60L + se;
}

// Средний тариф на отрезке [a,b] с учётом ночных границ.
float tariffAvg(long a, long b) {
  if (b <= a) {
    long sod = ((a % 86400L) + 86400L) % 86400L;
    return tariffForHour((int)(sod / 3600L));
  }
  long total = b - a;
  long stop = b;
  if (total > 3L * 86400L) stop = a + 3L * 86400L;
  long night = 0;
  long s = a;
  int guard = 0;
  while (s < stop && guard++ < 128) {
    long sod = ((s % 86400L) + 86400L) % 86400L;
    long next;
    if (sod < 21600L)       next = s + (21600L - sod);       // до 06:00
    else if (sod < 82800L)  next = s + (82800L - sod);       // до 23:00
    else                    next = s + (86400L - sod);       // до полуночи
    if (next > stop) next = stop;
    if (sod >= 82800L || sod < 21600L) night += next - s;
    s = next;
  }
  if (total > 3L * 86400L && stop > a)
    night = (long)((double)night * (double)total / (double)(stop - a));
  double nf = (double)night / (double)total;
  return (float)(nf * TARIFF_NIGHT + (1.0 - nf) * TARIFF_DAY);
}

// Расход и стоимость за [start, end). end=0 — до текущего показания.
bool scanSdRange(long start, long end, float nowKwh,
                 float* outKwh, float* outCost, float* outDay, float* outNight) {
  if (!gSdOk || start <= 0) return false;
  long now = nowLocalEpoch();
  if (now <= 0) return false;
  long rangeEnd = (end > 0) ? end : now;
  if (rangeEnd <= start) return false;

  if (!sRec) {
    sRec = (SdRec*)malloc(sizeof(SdRec) * SD_REC_MAX);
    if (!sRec) return false;
  }

  File f = SD.open("/energy.csv", FILE_READ);
  if (!f) return false;

  // читаем только хвост файла — этого хватает на сутки с запасом
  long span = rangeEnd - start + 6L * 3600L;
  unsigned long need = (unsigned long)(span / 30L) * 52UL;
  if (need < 60000UL) need = 60000UL;
  if (need > 230000UL) need = 230000UL;
  size_t sz = f.size();
  size_t off = (sz > need) ? (sz - need) : 0;
  if (off > 0) {
    f.seek(off);
    f.readStringUntil('\n');          // первая строка может быть обрезана
  }

  long minT = start - 4L * 3600L;
  long maxT = rangeEnd + 4L * 3600L;
  sRecN = 0;
  while (f.available()) {
    String line = f.readStringUntil('\n');
    long t = lineEpoch(line);
    if (!t || t < minT || t > maxT) continue;
    int p1 = line.indexOf(',');
    if (p1 < 0) continue;
    int p2 = line.indexOf(',', p1 + 1);
    int p3 = line.indexOf(',', p2 + 1);
    int p4 = line.indexOf(',', p3 + 1);
    if (p4 < 0) continue;
    float e = line.substring(p3 + 1, p4).toFloat();
    if (e <= 0) continue;
    if (sRecN < SD_REC_MAX) {
      sRec[sRecN].t = (uint32_t)t;
      sRec[sRecN].e = e;
      sRecN++;
    }
  }
  f.close();
  if (sRecN < 2) return false;

  qsort(sRec, sRecN, sizeof(SdRec), cmpSdRec);
  int n = 0;
  for (int i = 0; i < sRecN; i++) {
    if (n > 0 && sRec[n - 1].t == sRec[i].t) {
      if (sRec[i].e > sRec[n - 1].e) sRec[n - 1].e = sRec[i].e;
      continue;
    }
    sRec[n++] = sRec[i];
  }
  if (n < 2) return false;

  int i0 = 0;
  while (i0 < n && sRec[i0].t < (uint32_t)start) i0++;

  float baseE;
  if (i0 == 0) baseE = sRec[0].e;
  else if (i0 >= n) baseE = sRec[n - 1].e;
  else {
    SdRec a = sRec[i0 - 1], b = sRec[i0];
    long gap = (long)b.t - (long)a.t;
    if (gap > 0 && gap <= 4L * 3600L && b.e >= a.e)
      baseE = a.e + (b.e - a.e) * (float)(start - (long)a.t) / (float)gap;
    else
      baseE = b.e;
  }

  float endE;
  if (end <= 0) {
    endE = nowKwh;
    if (endE <= 0 || endE < sRec[n - 1].e) endE = sRec[n - 1].e;
  } else {
    int i1 = 0;
    while (i1 < n && sRec[i1].t <= (uint32_t)end) i1++;
    if (i1 == 0) endE = sRec[0].e;
    else if (i1 >= n) endE = sRec[n - 1].e;
    else {
      SdRec a = sRec[i1 - 1], b = sRec[i1];
      long gap = (long)b.t - (long)a.t;
      if (gap > 0 && gap <= 4L * 3600L && b.e >= a.e)
        endE = a.e + (b.e - a.e) * (float)(end - (long)a.t) / (float)gap;
      else
        endE = a.e;
    }
  }

  float kwh = endE - baseE;
  if (kwh < 0) kwh = 0;
  if (kwh > 100000) return false;

  double costSum = 0, deltaSum = 0, nightDelta = 0;
  for (int i = 1; i < n; i++) {
    long t0 = (long)sRec[i - 1].t, t1 = (long)sRec[i].t;
    float de = sRec[i].e - sRec[i - 1].e;
    if (t1 <= t0 || de <= 0) continue;
    long a = (t0 > start) ? t0 : start;
    long b = (t1 < rangeEnd) ? t1 : rangeEnd;
    if (b <= a) continue;
    float w = (float)(b - a) / (float)(t1 - t0);
    double din = (double)de * w;
    float ta = tariffAvg(a, b);
    costSum += din * ta;
    deltaSum += din;
    double nf = (TARIFF_DAY - ta) / (TARIFF_DAY - TARIFF_NIGHT);
    if (nf < 0) nf = 0;
    if (nf > 1) nf = 1;
    nightDelta += din * nf;
  }

  // хвост от последней записи до текущего показания счётчика
  if (end <= 0) {
    long lt = (long)sRec[n - 1].t;
    float le = sRec[n - 1].e;
    if (lt < now && nowKwh > le) {
      float de = nowKwh - le;
      if (de < 2.0f) {
        long a = (lt > start) ? lt : start;
        long b = now;
        if (b > a) {
          double din = (double)de;
          float ta = tariffAvg(a, b);
          costSum += din * ta;
          deltaSum += din;
          double nf = (TARIFF_DAY - ta) / (TARIFF_DAY - TARIFF_NIGHT);
          if (nf < 0) nf = 0;
          if (nf > 1) nf = 1;
          nightDelta += din * nf;
        }
      }
    }
  }

  float cost;
  if (deltaSum > 0.0001) cost = (float)(costSum * (double)kwh / deltaSum);
  else                   cost = kwh * avgTariffEstimate();
  if (cost < kwh * TARIFF_NIGHT) cost = kwh * TARIFF_NIGHT;
  if (cost > kwh * TARIFF_DAY)   cost = kwh * TARIFF_DAY;

  *outKwh = kwh;
  *outCost = cost;
  if (outDay || outNight) {
    float nf = (deltaSum > 0.0001) ? (float)(nightDelta / deltaSum) : 7.0f / 24.0f;
    if (nf < 0) nf = 0;
    if (nf > 1) nf = 1;
    if (outNight) *outNight = kwh * nf;
    if (outDay)   *outDay = kwh * (1.0f - nf);
  }
  return true;
}

bool computeTodayFromSd(int year, int mon, int day, float nowKwh,
                        float* outKwh, float* outCost) {
  float d, n;
  long start = daysFromCivil(year, (unsigned)mon, (unsigned)day) * 86400L;
  return scanSdRange(start, 0, nowKwh, outKwh, outCost, &d, &n);
}

// Одна база месяца — самая ранняя запись месяца на карте.
bool sdMonthBase(int year, int mon, float* outKwh) {
  if (!gSdOk) return false;
  File f = SD.open("/energy.csv", FILE_READ);
  if (!f) return false;
  char prefix[10];
  snprintf(prefix, sizeof(prefix), "%04d-%02d", year, mon);
  long bestT = 0;
  float bestE = 0;
  while (f.available()) {
    String line = f.readStringUntil('\n');
    if (!line.startsWith(prefix)) continue;
    long t = lineEpoch(line);
    if (!t) continue;
    int p1 = line.indexOf(',');
    if (p1 < 0) continue;
    int p2 = line.indexOf(',', p1 + 1);
    int p3 = line.indexOf(',', p2 + 1);
    int p4 = line.indexOf(',', p3 + 1);
    if (p4 < 0) continue;
    float e = line.substring(p3 + 1, p4).toFloat();
    if (e <= 0) continue;
    if (bestT == 0 || t < bestT) { bestT = t; bestE = e; }
  }
  f.close();
  if (bestT == 0) return false;
  *outKwh = bestE;
  return true;
}

// Смещение в файле, с которого начинаются строки за сегодня.
// Идём с конца файла блоками по 2 КБ назад, пока блоки ещё содержат
// сегодняшнюю дату. В каждом блоке берём САМОЕ РАННЕЕ вхождение, поэтому
// в итоге получаем начало текущих суток (раньше брался хвост 200 КБ и
// терялось утро). Как только блок без сегодняшней даты — это вчера.
static size_t findDayStartOffset(File& f, const char* dayPrefix) {
  size_t sz = f.size();
  if (sz == 0) return 0;
  char pat[16];
  snprintf(pat, sizeof(pat), "\n%s", dayPrefix);
  int plen = (int)strlen(pat);
  const size_t CH = 2048;
  char buf[CH + 1];
  size_t off = sz;
  size_t best = 0;
  bool found = false;
  while (off > 0) {
    size_t take = (off > CH) ? CH : off;
    off -= take;
    f.seek(off);
    int n = (int)f.read((uint8_t*)buf, take);
    if (n <= 0) break;
    buf[n] = 0;
    int first = -1;
    for (int i = 0; i + plen <= n; i++) {
      if (memcmp(buf + i, pat, plen) == 0) { first = i; break; }
    }
    if (first < 0) break;            // дальше уже вчерашние строки
    best = off + (size_t)first + 1;
    found = true;
    if (off == 0) break;
  }
  return found ? best : 0;
}

// Последняя строка файла — за сегодня? Читаем только хвост, это дешево.
static bool fileTailIsToday(File& f, const char* dayPrefix) {
  size_t sz = f.size();
  if (sz < 20) return false;
  size_t from = (sz > 200) ? sz - 200 : 0;
  f.seek(from);
  String last;
  while (f.available()) {
    String l = f.readStringUntil('\n');
    l.trim();
    if (l.length() > 10) last = l;
  }
  return last.startsWith(dayPrefix);
}

// Заполняем суточную статистику и график по уже записанным строкам.
// Подхватываем ВЕСЬ текущий день: мин/макс/среднее напряжение, температуру,
// мощность и точки графика 24 ч. Формат строк — 6 колонок (старый) или
// 7 колонок (с температурой в 7-м поле); старое тоже читается.
bool seedStatsFromSd() {
  if (!gSdOk || statsSeeded) return statsSeeded;
  struct tm ti;
  if (!getLocalTime(&ti, 10)) return false;
  long start = daysFromCivil(ti.tm_year + 1900, ti.tm_mon + 1, ti.tm_mday) * 86400L;
  if (start <= 0) return false;

  File f = SD.open("/energy.csv", FILE_READ);
  if (!f) return false;

  char dayPrefix[12];
  snprintf(dayPrefix, sizeof(dayPrefix), "%04d-%02d-%02d",
           ti.tm_year + 1900, ti.tm_mon + 1, ti.tm_mday);
  size_t off = findDayStartOffset(f, dayPrefix);
  // Сегодняшних строк может ещё не быть (сразу после полуночи) — тогда
  // весь файл не перечитываем, это дорого.
  if (off == 0 && !fileTailIsToday(f, dayPrefix)) {
    Serial.println("статистика: строк за сегодня пока нет");
    return false;
  }
  f.seek(off);

  long prevT = 0, firstT = 0, lastT = 0;
  int rows = 0, usedV = 0, usedT = 0;
  while (f.available()) {
    String line = f.readStringUntil('\n');
    long t = lineEpoch(line);
    if (!t || t < start) continue;
    int p1 = line.indexOf(',');
    if (p1 < 0) continue;
    int p2 = line.indexOf(',', p1 + 1);
    int p3 = line.indexOf(',', p2 + 1);
    int p4 = line.indexOf(',', p3 + 1);
    if (p2 < 0 || p3 < 0 || p4 < 0) continue;

    float v = line.substring(p1 + 1, p2).toFloat();
    float p = line.substring(p2 + 1, p3).toFloat();
    // 7-е поле — температура, оно идёт ПОСЛЕ 6-й запятой (дата,В,Вт,кВт·ч,реле,руб,темп).
    // В старых строках его нет — это не ошибка.
    float tv = 0;
    bool hasT = false;
    int p5 = line.indexOf(',', p4 + 1);            // после реле
    int p6 = (p5 > 0) ? line.indexOf(',', p5 + 1) : -1;   // после рублей
    if (p6 > 0 && p6 + 1 < (int)line.length()) {
      String ts = line.substring(p6 + 1);
      ts.trim();
      if (ts.length() > 0) { tv = ts.toFloat(); hasT = true; }
    }

    float dt = 30.0f;
    if (prevT > 0 && t > prevT && t - prevT <= 180) dt = (float)(t - prevT);
    int minute = (int)((t % 86400L) / 60);
    int slot = minute / 5;
    if (slot >= H24_N) slot = H24_N - 1;
    if (firstT == 0) firstT = t;
    lastT = t;
    rows++;

    if (v > 50) {
      if (v < dayMinV) { dayMinV = v; dayMinVMin = minute; }
      if (v > dayMaxV) { dayMaxV = v; dayMaxVMin = minute; }
      daySumV += (double)v * dt;
      dayVSec += (long)dt;
      if (v < V_NORM_LO || v > V_NORM_HI) dayOutSec += (long)dt;
      h24V[slot] = v;
      h24Mask[slot] |= 1;
      usedV++;
    }
    if (p >= 0) {
      if (p > dayMaxP) { dayMaxP = p; dayMaxPMin = minute; }
      daySumP += (double)p * dt;
      dayPSec += (long)dt;
      h24P[slot] = p;
      h24Mask[slot] |= 4;
    }
    if (hasT && tv > -40.0f && tv < 100.0f) {
      if (tv < dayMinT) { dayMinT = tv; dayMinTMin = minute; }
      if (tv > dayMaxT) { dayMaxT = tv; dayMaxTMin = minute; }
      daySumT += (double)tv * dt;
      dayTSec += (long)dt;
      h24T[slot] = tv;
      h24Mask[slot] |= 2;
      usedT++;
    }
    prevT = t;
  }
  f.close();

  if (rows == 0) {
    Serial.printf("статистика: за сегодня строк нет (смещение %u)\n", (unsigned)off);
    return false;                 // попробуем ещё раз позже
  }
  statsSeeded = true;
  char b1[8], b2[8];
  snprintf(b1, sizeof(b1), "%02d:%02d", (int)((firstT % 86400L) / 3600),
           (int)((firstT % 3600L) / 60));
  snprintf(b2, sizeof(b2), "%02d:%02d", (int)((lastT % 86400L) / 3600),
           (int)((lastT % 3600L) / 60));
  Serial.printf("статистика за сегодня: строк %d (напряжение %d, температура %d), "
                "%s..%s, мин %.0f@%02d:%02d макс %.0f@%02d:%02d сред %.1f, "
                "вне нормы %ld с\n",
                rows, usedV, usedT, b1, b2,
                (dayMinV < 900) ? dayMinV : 0, (dayMinVMin < 0 ? 0 : dayMinVMin) / 60,
                (dayMinVMin < 0 ? 0 : dayMinVMin) % 60,
                dayMaxV, (dayMaxVMin < 0 ? 0 : dayMaxVMin) / 60,
                (dayMaxVMin < 0 ? 0 : dayMaxVMin) % 60,
                dayVSec ? daySumV / dayVSec : 0.0, dayOutSec);
  return true;
}

// ---------------------------------------------------------------- тачскрин
#define TP_CLK   25
#define TP_MOSI  32
#define TP_MISO  39
#define TP_CS    33
#define TP_IRQ   36

int calYleft = 3480, calYright = 586, calXtop = 916, calXbottom = 3010;

void loadCal() {
  Preferences p; p.begin("touchcal", true);
  calYleft = p.getInt("yleft", calYleft);
  calYright = p.getInt("yright", calYright);
  calXtop = p.getInt("xtop", calXtop);
  calXbottom = p.getInt("xbottom", calXbottom);
  p.end();
}
void saveCal() {
  Preferences p; p.begin("touchcal", false);
  p.putInt("yleft", calYleft); p.putInt("yright", calYright);
  p.putInt("xtop", calXtop);  p.putInt("xbottom", calXbottom);
  p.end();
}

uint16_t tpRaw(uint8_t cmd) {
  digitalWrite(TP_CS, LOW); delayMicroseconds(2);
  for (int i = 7; i >= 0; i--) {
    digitalWrite(TP_MOSI, (cmd >> i) & 1);
    delayMicroseconds(1); digitalWrite(TP_CLK, HIGH);
    delayMicroseconds(1); digitalWrite(TP_CLK, LOW);
    delayMicroseconds(1);
  }
  uint16_t v = 0;
  for (int i = 0; i < 16; i++) {
    digitalWrite(TP_CLK, HIGH); delayMicroseconds(1);
    v = (v << 1) | (digitalRead(TP_MISO) ? 1 : 0);
    digitalWrite(TP_CLK, LOW); delayMicroseconds(1);
  }
  digitalWrite(TP_CS, HIGH);
  return v >> 3;
}

void tpInit() {
  pinMode(TP_CS, OUTPUT); pinMode(TP_CLK, OUTPUT);
  pinMode(TP_MOSI, OUTPUT); pinMode(TP_MISO, INPUT);
  pinMode(TP_IRQ, INPUT_PULLUP);
  digitalWrite(TP_CS, HIGH); digitalWrite(TP_CLK, LOW);
}
bool tpDown() { return digitalRead(TP_IRQ) == LOW; }

bool tpPoint(int* sx, int* sy) {
  long ax = 0, ay = 0; int n = 0;
  unsigned long t0 = millis();
  while (millis() - t0 < 60) {
    ax += tpRaw(0x90); ay += tpRaw(0xD0); n++;
    if (!tpDown()) break;
    delay(3);
  }
  if (n == 0) return false;
  long rx = ay / n, ry = ax / n;
  if (rx < 100 || rx > 4050 || ry < 100 || ry > 4050) return false;
  *sx = constrain((int)map(rx, calYleft, calYright, 0, 239), 0, 239);
  *sy = constrain((int)map(ry, calXtop, calXbottom, 0, 319), 0, 319);
  return true;
}

bool tpTap(int* x, int* y, unsigned long timeout = 20000) {
  unsigned long t0 = millis();
  while (millis() - t0 < timeout) {
    if (tpDown()) {
      int px, py;
      if (tpPoint(&px, &py)) {
        while (tpDown()) delay(20);
        delay(80);
        *x = px; *y = py;
        return true;
      }
    }
    delay(20);
  }
  return false;
}

// ---------------------------------------------------------------- Wi-Fi
String gSsid, gPass;

void loadWifi() {
  Preferences p; p.begin("wifi", true);
  gSsid = p.getString("ssid", DEF_SSID);
  gPass = p.getString("pass", DEF_PASS);
  p.end();
}
void saveWifi(const String& s, const String& pw) {
  Preferences p; p.begin("wifi", false);
  p.putString("ssid", s);
  p.putString("pass", pw);
  p.end();
  gSsid = s; gPass = pw;
}

bool connectWifi(int waitMs = 25000) {
  WiFi.mode(WIFI_STA);
  WiFi.setTxPower(WIFI_POWER_11dBm);
  WiFi.setSleep(false);
  WiFi.begin(gSsid.c_str(), gPass.c_str());
  unsigned long t0 = millis();
  while (millis() - t0 < (unsigned long)waitMs) {
    esp_task_wdt_reset();
    delay(250);
    if (WiFi.status() == WL_CONNECTED) { gRssi = WiFi.RSSI(); return true; }
  }
  return false;
}


// ---------------------------------------------------------------- base64
// DP 6 приходит строкой base64: 8 байт, из них word0 — напряжение,
// word3 — мощность. Реле присылает его не всегда, но когда присылает —
// плата получает данные сама, без компьютера.
int b64val(char ch) {
  if (ch >= 'A' && ch <= 'Z') return ch - 'A';
  if (ch >= 'a' && ch <= 'z') return ch - 'a' + 26;
  if (ch >= '0' && ch <= '9') return ch - '0' + 52;
  if (ch == '+') return 62;
  if (ch == '/') return 63;
  return -1;
}

int b64decode(const char* in, uint8_t* out, int maxOut) {
  int n = 0;
  uint32_t buf = 0;
  int bits = 0;
  for (const char* p = in; *p; p++) {
    int v = b64val(*p);
    if (v < 0) break;
    buf = (buf << 6) | (uint32_t)v;
    bits += 6;
    if (bits >= 8) {
      bits -= 8;
      if (n < maxOut) out[n++] = (uint8_t)((buf >> bits) & 0xFF);
    }
  }
  return n;
}

// Разбирает DP 6: возвращает true, если получилось
bool parsePhase(const char* js) {
  const char* p = strstr(js, "\"6\":\"");
  if (!p) return false;
  p += 5;
  const char* e = strchr(p, '"');
  if (!e) return false;

  char b64[32];
  int len = (int)(e - p);
  if (len <= 0 || len >= (int)sizeof(b64)) return false;
  memcpy(b64, p, len);
  b64[len] = 0;

  uint8_t raw[16];
  int n = b64decode(b64, raw, sizeof(raw));
  if (n < 8) return false;

  uint16_t w0 = ((uint16_t)raw[0] << 8) | raw[1];
  uint16_t w1 = ((uint16_t)raw[2] << 8) | raw[3];
  uint16_t w2 = ((uint16_t)raw[4] << 8) | raw[5];
  uint16_t w3 = ((uint16_t)raw[6] << 8) | raw[7];

  float v = w0 / 10.0f;
  float a = w1 / 1000.0f;
  float pwr = (float)w3;          // мощность — прямое значение

  // отсекаем мусор
  bool got = false;
  if (v > 80 && v < 400) { gVoltage = v; got = true; }
  if (pwr >= 0 && pwr < 20000) { gPower = pwr; got = true; }
  (void)w2; (void)a;
  if (got) gPhaseMs = millis();
  return got;
}


// ============================================================================
//  TUYA CLOUD — то же, что видит приложение на телефоне
// ============================================================================
//  Запрос подписывается HMAC-SHA256:
//    stringToSign = метод \n sha256(тело) \n \n путь
//    подпись      = HMAC(secret, clientId + [токен] + время + случайная строка + stringToSign)
// ============================================================================

String gTuyaToken;
unsigned long gTuyaTokenMs = 0;
unsigned long gCloudLast = 0;
bool gCloudOk = false;

String sha256hex(const String& s) {
  uint8_t h[32];
  mbedtls_md_context_t ctx;
  const mbedtls_md_info_t* info = mbedtls_md_info_from_type(MBEDTLS_MD_SHA256);
  mbedtls_md_init(&ctx);
  mbedtls_md_setup(&ctx, info, 0);
  mbedtls_md_starts(&ctx);
  mbedtls_md_update(&ctx, (const uint8_t*)s.c_str(), s.length());
  mbedtls_md_finish(&ctx, h);
  mbedtls_md_free(&ctx);

  char out[65];
  for (int i = 0; i < 32; i++) sprintf(out + i * 2, "%02x", h[i]);
  out[64] = 0;
  return String(out);
}

String hmacHex(const String& key, const String& data) {
  uint8_t h[32];
  const mbedtls_md_info_t* info = mbedtls_md_info_from_type(MBEDTLS_MD_SHA256);
  mbedtls_md_context_t ctx;
  mbedtls_md_init(&ctx);
  mbedtls_md_setup(&ctx, info, 1);
  mbedtls_md_hmac_starts(&ctx, (const uint8_t*)key.c_str(), key.length());
  mbedtls_md_hmac_update(&ctx, (const uint8_t*)data.c_str(), data.length());
  mbedtls_md_hmac_finish(&ctx, h);
  mbedtls_md_free(&ctx);

  char out[65];
  for (int i = 0; i < 32; i++) sprintf(out + i * 2, "%02X", h[i]);
  out[64] = 0;
  return String(out);
}

String randomHex(int n) {
  String s;
  for (int i = 0; i < n; i++) {
    char b[3];
    sprintf(b, "%02x", (uint8_t)(esp_random() & 0xFF));
    s += b;
  }
  return s;
}

// Один HTTPS-запрос к облаку. Возвращает тело ответа.
bool cloudCall(const String& method, const String& path, String& body) {
  time_t now = time(nullptr);
  if (now < 1600000000) return false;              // часы не выставлены

  String t = String((unsigned long)(now * 1000ULL % 10000000000ULL));
  String nonce = randomHex(8);
  String sha = sha256hex("");
  String sts = method + "\n" + sha + "\n\n" + path;

  String strSign = String(TUYA_CID);
  if (gTuyaToken.length()) strSign += gTuyaToken;
  strSign += t + nonce + sts;

  String sig = hmacHex(String(TUYA_SECRET), strSign);

  WiFiClientSecure client;
  client.setInsecure();                            // без проверки сертификата
  client.setTimeout(10);
  if (!client.connect(TUYA_HOST, 443, 8000)) return false;

  String req = method + " " + path + " HTTP/1.1\r\n";
  req += String("Host: ") + TUYA_HOST + "\r\n";
  req += String("client_id: ") + TUYA_CID + "\r\n";
  req += "sign: " + sig + "\r\n";
  req += "t: " + t + "\r\n";
  req += "sign_method: HMAC-SHA256\r\n";
  req += "nonce: " + nonce + "\r\n";
  if (gTuyaToken.length()) req += "access_token: " + gTuyaToken + "\r\n";
  req += "Content-Type: application/json\r\n";
  req += "Connection: close\r\n\r\n";

  client.print(req);

  unsigned long t0 = millis();
  while (client.connected() && !client.available() && millis() - t0 < 9000) {
    esp_task_wdt_reset();
    delay(20);
  }

  String raw;
  while (client.available()) {
    raw += (char)client.read();
    esp_task_wdt_reset();
    if (raw.length() > 3500) break;
  }
  client.stop();

  int hdr = raw.indexOf("\r\n\r\n");
  if (hdr < 0) return false;
  body = raw.substring(hdr + 4);
  return body.length() > 0;
}

// Получить токен доступа (живёт ~18 минут)
bool cloudToken() {
  if (gTuyaToken.length() && millis() - gTuyaTokenMs < 900000UL) return true;

  gTuyaToken = "";
  String body;
  if (!cloudCall("GET", "/v1.0/token?grant_type=1", body)) return false;
  if (body.indexOf("\"success\":true") < 0) return false;

  int i = body.indexOf("\"access_token\":\"");
  if (i < 0) return false;
  i += 16;
  int j = body.indexOf('"', i);
  if (j < 0) return false;

  gTuyaToken = body.substring(i, j);
  gTuyaTokenMs = millis();
  Serial.printf("облако: токен получен (%d симв.)\n", gTuyaToken.length());
  return gTuyaToken.length() > 10;
}

// Найти в ответе значение поля по коду
bool cloudValue(const String& body, const char* code, String& val) {
  String pat = String("\"code\":\"") + code + "\",\"value\":";
  int i = body.indexOf(pat);
  if (i < 0) return false;
  i += pat.length();
  if (i < (int)body.length() && body[i] == '"') {
    i++;
    int j = body.indexOf('"', i);
    if (j < 0) return false;
    val = body.substring(i, j);
  } else {
    int j = i;
    while (j < (int)body.length() && (isDigit(body[j]) || body[j] == '-' || body[j] == '.')) j++;
    val = body.substring(i, j);
  }
  return val.length() > 0;
}

// Запросить состояние устройства — забираем всё нужное сразу
bool cloudStatus() {
  if (WiFi.status() != WL_CONNECTED) return false;
  if (!cloudToken()) return false;

  String body;
  String path = String("/v1.0/devices/") + DEVICE_ID;
  if (!cloudCall("GET", path, body)) return false;
  if (body.indexOf("\"success\":true") < 0) {
    gTuyaToken = "";                 // токен мог протухнуть — обновим в следующий раз
    return false;
  }

  String v;

  // счётчик энергии (в сотых)
  if (cloudValue(body, "total_forward_energy", v)) {
    float e = v.toFloat() / 100.0f;
    if (e > 0 && e < 100000) gEnergy = e;
  }

  // напряжение и мощность в одной строке base64
  if (cloudValue(body, "phase_a", v)) {
    uint8_t raw[16];
    int n = b64decode(v.c_str(), raw, sizeof(raw));
    if (n >= 8) {
      uint16_t w0 = ((uint16_t)raw[0] << 8) | raw[1];
      uint16_t w3 = ((uint16_t)raw[6] << 8) | raw[7];
      float volt = w0 / 10.0f;
      float pwr  = (float)w3;         // мощность — прямое значение
      if (volt > 80 && volt < 400) { gVoltage = volt; gPhaseMs = millis(); }
      if (pwr >= 0 && pwr < 20000) gPower = pwr;
    }
  }

  // температура (в десятых)
  if (cloudValue(body, "temp_current", v)) {
    float t = v.toFloat() / 10.0f;
    if (t > -40 && t < 125) { gTemp = t; gTempValid = true; }
  }

  // реле
  if (cloudValue(body, "switch", v)) {
    gRelay = (v == "true" || v == "1");
  }

  gCloudOk = true;
  gTuyaOk = true;
  gLastOk = millis();
  gCost = myTodayCost;
  return true;
}


// ---------------------------------------------------------------- база на полночь из облака
// Берём историю счётчика за 6 часов до полуночи и МАКСИМУМ — это показание
// ровно в 00:00. Счётчик только растёт, порядок записей в ответе не важен.
// Параметры в подписи идут строго по алфавиту и без URL-кодирования.
bool cloudDayStart(int year, int mon, int day, float* outKwh) {
  if (WiFi.status() != WL_CONNECTED) return false;
  if (!cloudToken()) return false;

  struct tm t0;
  memset(&t0, 0, sizeof(t0));
  t0.tm_year = year - 1900;
  t0.tm_mon  = mon - 1;
  t0.tm_mday = day;
  t0.tm_isdst = -1;
  time_t midnight = mktime(&t0);
  if (midnight < 1600000000) return false;

  unsigned long long endMs   = (unsigned long long)midnight * 1000ULL;
  unsigned long long startMs = endMs - 6ULL * 3600ULL * 1000ULL;

  char path[220];
  snprintf(path, sizeof(path),
           "/v1.0/devices/%s/report-logs?codes=total_forward_energy"
           "&end_time=%llu&size=100&start_time=%llu",
           DEVICE_ID, endMs, startMs);

  String body;
  if (!cloudCall("GET", String(path), body)) return false;
  if (body.indexOf("success") < 0) return false;

  float best = -1;
  int pos = 0;
  while (true) {
    int i = body.indexOf("value", pos);
    if (i < 0) break;
    i = body.indexOf(':', i);
    if (i < 0) break;
    i++;
    while (i < (int)body.length() && body[i] == ' ') i++;
    if (i < (int)body.length() && body[i] == '"') i++;
    int j = i;
    while (j < (int)body.length() && (isDigit(body[j]) || body[j] == '.' || body[j] == '-')) j++;
    if (j > i) {
      float v = body.substring(i, j).toFloat() / 100.0f;
      if (v > best) best = v;
    }
    pos = j > i ? j : i + 1;
  }

  if (best <= 0) return false;
  *outKwh = best;
  Serial.printf("облако: на полночь %.2f кВт*ч\n", best);
  return true;
}

// ---------------------------------------------------------------- вчера и месяц
void loadPeriods() {
  Preferences p; p.begin("periods", true);
  mMonthStamp = p.getInt("mstamp", -2);
  mBaseKwh    = p.getFloat("mbase", -1);
  mKwh        = p.getFloat("mkwh", 0);
  mCost       = p.getFloat("mcost", 0);
  yKwh        = p.getFloat("ykwh", 0);
  yCost       = p.getFloat("ycost", 0);
  yDayKey     = p.getLong("yday", -2);
  p.end();
}
void savePeriods() {
  Preferences p; p.begin("periods", false);
  p.putInt("mstamp", mMonthStamp);
  p.putFloat("mbase", mBaseKwh);
  p.putFloat("mkwh", mKwh);
  p.putFloat("mcost", mCost);
  p.putFloat("ykwh", yKwh);
  p.putFloat("ycost", yCost);
  p.putLong("yday", yDayKey);
  p.end();
}

// Средневзвешенный тариф по фактическому расходу день/ночь.
// Если данных мало — средний по суткам (7 ночных часов, 17 дневных).
float measuredTariff() {
  float tot = statDayKwh + statNightKwh;
  if (tot > 0.05f)
    return (statDayKwh * TARIFF_DAY + statNightKwh * TARIFF_NIGHT) / tot;
  return avgTariffEstimate();
}

void updateMonth(struct tm& ti) {
  int stamp = (ti.tm_year + 1900) * 12 + ti.tm_mon;

  if (mMonthStamp != stamp || mBaseKwh < 0) {
    float base = -1;
    if (WiFi.status() == WL_CONNECTED)
      cloudDayStart(ti.tm_year + 1900, ti.tm_mon + 1, 1, &base);
    if (base < 0) sdMonthBase(ti.tm_year + 1900, ti.tm_mon + 1, &base);
    if (base < 0) {
      if (gEnergy <= 0) return;      // счётчик ещё не прочитан — попробуем позже
      base = gEnergy;
    }

    mBaseKwh = base;
    mMonthStamp = stamp;
    mKwh = (gEnergy > base) ? gEnergy - base : 0;
    mCost = mKwh * measuredTariff();
    prevMonthE = gEnergy;
    savePeriods();
    Serial.printf("месяц: база %.2f, %.2f кВт*ч, %.2f руб (тариф %.2f)",
                  base, mKwh, mCost, measuredTariff());
    Serial.println();
    return;
  }

  if (prevMonthE >= 0 && gEnergy > prevMonthE) {
    float d = gEnergy - prevMonthE;
    if (d < 2.0f) {
      mKwh += d;
      mCost += d * tariffForHour(ti.tm_hour);
    }
    prevMonthE = gEnergy;
  }
  float diff = gEnergy - mBaseKwh;
  if (diff > mKwh + 0.02f) {
    mCost += (diff - mKwh) * tariffForHour(ti.tm_hour);
    mKwh = diff;
  }
  if (diff >= 0 && diff + 0.05f < mKwh) mKwh = diff;   // счётчик сбросили

  static unsigned long lastSave = 0;
  if (millis() - lastSave > 600000UL) { lastSave = millis(); savePeriods(); }
}

void updateYesterday(struct tm& ti) {
  long todayKey = daysFromCivil(ti.tm_year + 1900, ti.tm_mon + 1, ti.tm_mday);
  long yKey = todayKey - 1;
  if (yDayKey == yKey) return;

  float k, c, d, n;
  if (scanSdRange(yKey * 86400L, todayKey * 86400L, 0, &k, &c, &d, &n) && k > 0.0001f) {
    yKwh = k;
    yCost = c;
    yDayKey = yKey;
    savePeriods();
    Serial.printf("вчера: %.3f кВт*ч, %.2f руб", yKwh, yCost);
    Serial.println();
    return;
  }

  if (WiFi.status() != WL_CONNECTED) return;
  static unsigned long lastCloud = 0;
  if (lastCloud != 0 && millis() - lastCloud < 600000UL) return;
  lastCloud = millis();
  time_t now_t = time(nullptr);
  if (now_t < 1600000000) return;
  time_t y_t = now_t - 86400;
  struct tm ytm;
  localtime_r(&y_t, &ytm);
  float yStart, tStart;
  if (cloudDayStart(ytm.tm_year + 1900, ytm.tm_mon + 1, ytm.tm_mday, &yStart) &&
      cloudDayStart(ti.tm_year + 1900, ti.tm_mon + 1, ti.tm_mday, &tStart)) {
    yKwh = tStart - yStart;
    if (yKwh < 0) yKwh = 0;
    yCost = yKwh * measuredTariff();
    yDayKey = yKey;
    savePeriods();
    Serial.printf("вчера (облако): %.2f кВт*ч, %.2f руб", yKwh, yCost);
    Serial.println();
  }
}

void updatePeriods() {
  struct tm ti;
  if (!getLocalTime(&ti, 10)) return;
  updateMonth(ti);
  updateYesterday(ti);
}

// ---------------------------------------------------------------- Tuya
void hmac256(const uint8_t* key, size_t kl, const uint8_t* data, size_t dl, uint8_t* out) {
  const mbedtls_md_info_t* info = mbedtls_md_info_from_type(MBEDTLS_MD_SHA256);
  mbedtls_md_context_t ctx;
  mbedtls_md_init(&ctx);
  mbedtls_md_setup(&ctx, info, 1);
  mbedtls_md_hmac_starts(&ctx, key, kl);
  mbedtls_md_hmac_update(&ctx, data, dl);
  mbedtls_md_hmac_finish(&ctx, out);
  mbedtls_md_free(&ctx);
}

size_t buildFrame(uint8_t cmd, const uint8_t* payload, size_t plen,
                  const uint8_t* key, uint8_t* out) {
  uint32_t length = IV_LEN + plen + TAG_LEN;
  size_t p = 0;
  out[p++] = 0; out[p++] = 0; out[p++] = 0x66; out[p++] = 0x99;
  out[p++] = 0; out[p++] = 0;
  out[p++] = (seqNo >> 24) & 0xFF; out[p++] = (seqNo >> 16) & 0xFF;
  out[p++] = (seqNo >> 8) & 0xFF;  out[p++] = seqNo & 0xFF;
  out[p++] = (cmd >> 24) & 0xFF; out[p++] = (cmd >> 16) & 0xFF;
  out[p++] = (cmd >> 8) & 0xFF;  out[p++] = cmd & 0xFF;
  out[p++] = (length >> 24) & 0xFF; out[p++] = (length >> 16) & 0xFF;
  out[p++] = (length >> 8) & 0xFF;  out[p++] = length & 0xFF;

  uint8_t iv[IV_LEN];
  for (int i = 0; i < IV_LEN; i++) iv[i] = (uint8_t)esp_random();
  memcpy(out + p, iv, IV_LEN); p += IV_LEN;

  mbedtls_gcm_context gcm;
  mbedtls_gcm_init(&gcm);
  mbedtls_gcm_setkey(&gcm, MBEDTLS_CIPHER_ID_AES, key, 128);
  mbedtls_gcm_crypt_and_tag(&gcm, MBEDTLS_GCM_ENCRYPT, plen, iv, IV_LEN,
                            out + 4, HEADER_LEN - 4, payload, out + p, TAG_LEN, out + p + plen);
  mbedtls_gcm_free(&gcm);
  p += plen + TAG_LEN;
  out[p++] = 0; out[p++] = 0; out[p++] = 0x99; out[p++] = 0x66;
  return p;
}

int parseFrame(const uint8_t* buf, size_t len, const uint8_t* key,
               uint8_t* cmdOut, uint8_t* payloadOut, size_t payloadMax) {
  if (len < HEADER_LEN + IV_LEN + TAG_LEN + 4) return -1;
  if (!(buf[0] == 0 && buf[1] == 0 && buf[2] == 0x66 && buf[3] == 0x99)) return -1;
  uint32_t cmd = ((uint32_t)buf[10] << 24) | ((uint32_t)buf[11] << 16) |
                 ((uint32_t)buf[12] << 8) | buf[13];
  uint32_t length = ((uint32_t)buf[14] << 24) | ((uint32_t)buf[15] << 16) |
                    ((uint32_t)buf[16] << 8) | buf[17];
  if (length < IV_LEN + TAG_LEN) return -1;
  size_t ctLen = length - IV_LEN - TAG_LEN;
  if (ctLen > payloadMax) return -1;
  const uint8_t* iv = buf + HEADER_LEN;
  const uint8_t* ct = iv + IV_LEN;
  const uint8_t* tag = ct + ctLen;
  mbedtls_gcm_context gcm;
  mbedtls_gcm_init(&gcm);
  mbedtls_gcm_setkey(&gcm, MBEDTLS_CIPHER_ID_AES, key, 128);
  int rc = mbedtls_gcm_auth_decrypt(&gcm, ctLen, iv, IV_LEN, buf + 4, HEADER_LEN - 4,
                                    tag, TAG_LEN, ct, payloadOut);
  mbedtls_gcm_free(&gcm);
  if (rc != 0) return -1;
  *cmdOut = (uint8_t)cmd;
  return (int)ctLen;
}

int readFrame(uint8_t* buf, size_t bufMax, uint32_t timeoutMs) {
  unsigned long t0 = millis();
  size_t got = 0;
  while (got < HEADER_LEN) {
    if (millis() - t0 > timeoutMs) return -1;
    if (sock.available()) got += sock.read(buf + got, HEADER_LEN - got);
    else { esp_task_wdt_reset(); delay(5); }
  }
  uint32_t length = ((uint32_t)buf[14] << 24) | ((uint32_t)buf[15] << 16) |
                    ((uint32_t)buf[16] << 8) | buf[17];
  size_t total = HEADER_LEN + length + 4;
  if (total > bufMax) return -1;
  while (got < total) {
    if (millis() - t0 > timeoutMs) return -1;
    if (sock.available()) got += sock.read(buf + got, total - got);
    else { esp_task_wdt_reset(); delay(5); }
  }
  return (int)got;
}

void drainSocket(unsigned long ms) {
  unsigned long t0 = millis();
  while (millis() - t0 < ms) {
    while (sock.available()) sock.read();
    esp_task_wdt_reset();
    delay(10);
  }
}

bool negotiate() {
  uint8_t localNonce[16], remoteNonce[16];
  uint8_t buf[512], payload[256], cmd = 0;
  for (int i = 0; i < 16; i++) localNonce[i] = (uint8_t)esp_random();
  sessionReady = false;

  seqNo++;
  size_t n = buildFrame(CMD_SESS_START, localNonce, 16, (const uint8_t*)LOCAL_KEY, buf);
  sock.write(buf, n);
  int got = readFrame(buf, sizeof(buf), 6000);
  if (got < 0) return false;

  int plen = parseFrame(buf, got, (const uint8_t*)LOCAL_KEY, &cmd, payload, sizeof(payload));
  if (cmd != CMD_SESS_RESP || plen < 52) return false;

  uint32_t rc = ((uint32_t)payload[0] << 24) | ((uint32_t)payload[1] << 16) |
                ((uint32_t)payload[2] << 8) | payload[3];
  if (rc != 0) return false;

  memcpy(remoteNonce, payload + 4, 16);
  uint8_t expect[32];
  hmac256((const uint8_t*)LOCAL_KEY, 16, localNonce, 16, expect);
  if (memcmp(expect, payload + 20, 32) != 0) return false;

  uint8_t finish[32];
  hmac256((const uint8_t*)LOCAL_KEY, 16, remoteNonce, 16, finish);
  seqNo++;
  n = buildFrame(CMD_SESS_FINISH, finish, 32, (const uint8_t*)LOCAL_KEY, buf);
  sock.write(buf, n);

  uint8_t xored[16];
  for (int i = 0; i < 16; i++) xored[i] = localNonce[i] ^ remoteNonce[i];
  uint8_t ct[16], tag[TAG_LEN];
  mbedtls_gcm_context gcm;
  mbedtls_gcm_init(&gcm);
  mbedtls_gcm_setkey(&gcm, MBEDTLS_CIPHER_ID_AES, (const uint8_t*)LOCAL_KEY, 128);
  mbedtls_gcm_crypt_and_tag(&gcm, MBEDTLS_GCM_ENCRYPT, 16, localNonce, 12, NULL, 0,
                            xored, ct, TAG_LEN, tag);
  mbedtls_gcm_free(&gcm);
  memcpy(sessionKey, ct, 16);
  sessionReady = true;
  return true;
}

bool findDpInt(const char* js, const char* dp, long* out) {
  char key[16]; snprintf(key, sizeof(key), "\"%s\":", dp);
  const char* p = strstr(js, key);
  if (!p) return false;
  p += strlen(key);
  while (*p == ' ') p++;
  *out = atol(p);
  return true;
}
bool findDpBool(const char* js, const char* dp, bool* out) {
  char key[16]; snprintf(key, sizeof(key), "\"%s\":", dp);
  const char* p = strstr(js, key);
  if (!p) return false;
  p += strlen(key);
  while (*p == ' ') p++;
  if (strncmp(p, "true", 4) == 0) { *out = true; return true; }
  if (strncmp(p, "false", 5) == 0) { *out = false; return true; }
  *out = (atol(p) != 0);
  return true;
}

bool tuyaQuery() {
  if (!sock.connected()) {
    sock.stop();
    sock.setTimeout(8);
    if (!sock.connect(RELAY_IP, RELAY_PORT, 6000)) return false;
    sessionReady = false;
  }
  if (!sessionReady && !negotiate()) { sock.stop(); return false; }

  uint8_t buf[512], payload[256];
  bool gotDps = false;
  bool gotPhase = false;

  // DP 6 (напряжение и мощность) приходит не на каждый запрос — примерно
  // в половине случаев. Поэтому делаем до трёх попыток.
  for (int attempt = 0; attempt < 3; attempt++) {
    drainSocket(80);

    seqNo++;
    const char* req = (attempt == 0)
                      ? "{}"
                      : "{}";
    size_t n = buildFrame(CMD_DP_QUERY, (const uint8_t*)req, strlen(req), sessionKey, buf);
    sock.write(buf, n);

    int got = readFrame(buf, sizeof(buf), 5000);
    if (got < 0) { sock.stop(); sessionReady = false; return gotDps; }

    uint8_t cmd = 0;
    int plen = parseFrame(buf, got, sessionKey, &cmd, payload, sizeof(payload));
    if (plen <= 4) { sock.stop(); sessionReady = false; return gotDps; }
    payload[plen] = 0;

    uint32_t rc = ((uint32_t)payload[0] << 24) | ((uint32_t)payload[1] << 16) |
                  ((uint32_t)payload[2] << 8) | payload[3];
    if (rc != 0) continue;

    const char* js = (const char*)(payload + 4);
    if (!strstr(js, "\"dps\"")) continue;

    gotDps = true;

    long e = 0, t10 = 0;
    bool sw = false;
    findDpInt(js, "1", &e);
    findDpBool(js, "16", &sw);
    if (findDpInt(js, "101", &t10)) { gTemp = t10 / 10.0f; gTempValid = true; }

    if (e > 0) gEnergy = e / 100.0f;    // нет DP1 — счётчик не обнуляем
    gRelay = sw;

    if (parsePhase(js)) gotPhase = true;
    if (gotPhase) break;              // напряжение поймали — хватит
    delay(100);
  }

  if (gotDps) {
    gCost = myTodayCost;
    gTuyaOk = true;
    gLastOk = millis();
  }
  return gotDps;
}

// ---------------------------------------------------------------- HTTP
bool httpGet(const String& url, String& out, int timeout = 5000) {
  HTTPClient h;
  h.setTimeout(timeout);
  if (!h.begin(url)) return false;
  int code = h.GET();
  if (code != 200) { h.end(); return false; }
  out = h.getString();
  h.end();
  return true;
}

bool fetchFromApi() {
  String body;
  if (!httpGet(String(PC_API) + "/relay", body)) return false;

  auto num = [&](const char* key) -> float {
    String pat = String("\"") + key + "\":";
    int i = body.indexOf(pat);
    if (i < 0) return -999;
    i += pat.length();
    while (i < (int)body.length() && body[i] == ' ') i++;
    int s = i;
    while (s < (int)body.length() && (body[s] == '-' || body[s] == '.' || isDigit(body[s]))) s++;
    if (s == i) return -999;
    return body.substring(i, s).toFloat();
  };

  float v = num("voltage");
  if (v > 50 && v < 400) gVoltage = v;
  float p = num("power");  if (p > -900) gPower = p;
  float t = num("temperature");
  if (t > -900 && t > -40 && t < 125) { gTemp = t; gTempValid = true; }
  float ek = num("energy_kwh"); if (ek > 0) gEnergy = ek;

  String st;
  if (httpGet(String(PC_API) + "/stats", st)) {
    int i = st.indexOf("\"today\":");
    if (i >= 0) {
      String part = st.substring(i, min((int)st.length(), i + 200));
      int k = part.indexOf("\"kwh\":");
      if (k >= 0) gTodayKwh = part.substring(k + 6, k + 16).toFloat();
      k = part.indexOf("\"cost\":");
      if (k >= 0) gTodayCost = part.substring(k + 7, k + 17).toFloat();
      gFromApi = true;
    }
  }
  return true;
}

// ---------------------------------------------------------------- погода
void codeToText(int code, char* out, size_t n) {
  if (code == 0)                     snprintf(out, n, "ЯСНО");
  else if (code <= 2)                snprintf(out, n, "МАЛООБЛАЧНО");
  else if (code == 3)                snprintf(out, n, "ПАСМУРНО");
  else if (code == 45 || code == 48) snprintf(out, n, "ТУМАН");
  else if (code <= 57)               snprintf(out, n, "МОРОСЬ");
  else if (code <= 67)               snprintf(out, n, "ОСАДКИ");
  else if (code <= 77)               snprintf(out, n, "СНЕГ");
  else if (code <= 82)               snprintf(out, n, "ЛИВЕНЬ");
  else if (code <= 86)               snprintf(out, n, "СНЕГОПАД");
  else                               snprintf(out, n, "ГРОЗА");
}

// Описание на английском переводим в короткое русское
void descToRus(const String& en, char* out, size_t n) {
  String s = en;
  s.toLowerCase();
  if (s.indexOf("thunder") >= 0)                snprintf(out, n, "ГРОЗА");
  else if (s.indexOf("snow") >= 0 ||
           s.indexOf("sleet") >= 0 ||
           s.indexOf("blizzard") >= 0)            snprintf(out, n, "СНЕГ");
  else if (s.indexOf("rain") >= 0 ||
           s.indexOf("drizzle") >= 0 ||
           s.indexOf("shower") >= 0)              snprintf(out, n, "ОСАДКИ");
  else if (s.indexOf("fog") >= 0 ||
           s.indexOf("mist") >= 0)                snprintf(out, n, "ТУМАН");
  else if (s.indexOf("overcast") >= 0)            snprintf(out, n, "ПАСМУРНО");
  else if (s.indexOf("cloudy") >= 0)              snprintf(out, n, "ОБЛАЧНО");
  else if (s.indexOf("clear") >= 0 ||
           s.indexOf("sunny") >= 0)               snprintf(out, n, "ЯСНО");
  else                                           snprintf(out, n, "ПЕРЕМЕННО");
}

bool fetchWeather() {
  String j;
  if (!httpGet(WEATHER_URL, j, 8000)) return false;
  j.trim();
  if (j.length() < 4) return false;

  // разбиваем по вертикальной черте
  String parts[4];
  int idx = 0;
  for (int i = 0; i < (int)j.length() && idx < 4; i++) {
    if (j[i] == '|') { idx++; continue; }
    parts[idx] += j[i];
  }
  if (idx < 3) return false;

  // температура: "+11°C" -> 11
  String t = parts[0];
  t.replace("+", "");
  int deg = t.indexOf((char)0xC2);       // начало "°C" в UTF-8
  if (deg > 0) t = t.substring(0, deg);
  deg = t.indexOf("C");
  if (deg > 0) t = t.substring(0, deg);
  wTemp = t.toFloat();

  // влажность: "86%"
  String h = parts[1];
  int pct = h.indexOf('%');
  if (pct > 0) h = h.substring(0, pct);
  wHum = h.toFloat();

  // ветер: "↑5km/h" -> 5
  String w = parts[2];
  String digits;
  for (int i = 0; i < (int)w.length(); i++) if (isDigit(w[i])) digits += w[i];
  wWind = digits.toFloat();

  wDescEn = parts[3];
  wDescEn.trim();

  wOk = true;
  wLast = millis();
  wTime = time(nullptr);
  Serial.printf("погода: %.0f C, %.0f%%, %.0f км/ч, %s\n",
                wTemp, wHum, wWind, wDescEn.c_str());
  return true;
}

// ---------------------------------------------------------------- SD
bool initSD() {
  sdSPI.begin(18, 19, 23, 5);
  if (!SD.begin(5, sdSPI, 4000000)) return false;
  if (SD.cardType() == CARD_NONE) return false;
  File f = SD.open("/energy.csv", FILE_READ);
  if (f) {
    gSdRows = 0;
    while (f.available()) { String l = f.readStringUntil('\n'); if (l.length() > 10) gSdRows++; }
    f.close();
  } else {
    File n = SD.open("/energy.csv", FILE_WRITE);
    if (n) { n.println("datetime,voltage,power,energy_kwh,relay,cost,temp"); n.close(); }
  }
  return true;
}

void writeSd() {
  if (!gSdOk || millis() - gSdLast < 30000) return;
  gSdLast = millis();
  struct tm ti;
  char stamp[24];
  if (getLocalTime(&ti, 100))
    snprintf(stamp, sizeof(stamp), "%04d-%02d-%02d %02d:%02d:%02d",
             ti.tm_year + 1900, ti.tm_mon + 1, ti.tm_mday, ti.tm_hour, ti.tm_min, ti.tm_sec);
  else
    snprintf(stamp, sizeof(stamp), "%lu", millis() / 1000);

  File f = SD.open("/energy.csv", FILE_APPEND);
  if (!f) return;
  // 7-я колонка — температура. Если её ещё нет (плата только включилась),
  // поле остаётся пустым: пустое значение разбор пропускает.
  if (gTempValid)
    f.printf("%s,%.0f,%.0f,%.2f,%d,%.2f,%.1f\n", stamp, gVoltage, gPower, gEnergy,
             gRelay ? 1 : 0, gCost, gTemp);
  else
    f.printf("%s,%.0f,%.0f,%.2f,%d,%.2f,\n", stamp, gVoltage, gPower, gEnergy,
             gRelay ? 1 : 0, gCost);
  f.close();
  gSdRows++;
}

// ---------------------------------------------------------------- обмен с ПК
unsigned long gLastStatus = 0;
void sendJsonStatus() {
  Serial.printf("{\"t\":\"status\",\"fw\":\"CYD-%s\",\"name\":\"CYD-DASH\",\"up\":%lu,"
                "\"wifi\":%s,\"rssi\":%d,\"ip\":\"%s\",\"tuya\":%s,\"sd\":%s,"
                "\"sdRows\":%d,\"heap\":%u,\"scr\":%d}\n",
                FW_VER, millis() / 1000,
                WiFi.status() == WL_CONNECTED ? "true" : "false",
                gRssi, WiFi.localIP().toString().c_str(),
                gTuyaOk ? "true" : "false", gSdOk ? "true" : "false",
                gSdRows, ESP.getFreeHeap(), gScreen);
}
void sendJsonData() {
  Serial.printf("{\"t\":\"data\",\"v\":%.0f,\"temp\":%.1f,\"p\":%.0f,\"e\":%.2f,"
                "\"r\":%s,\"a\":false,\"cost\":%.2f,\"tkwh\":%.3f,\"tcost\":%.2f,"
                "\"mkwh\":%.2f,\"mcost\":%.2f,\"ykwh\":%.3f,\"ykost\":%.2f,\"scr\":%d}\n",
                gVoltage, gTemp, gPower, gEnergy, gRelay ? "true" : "false",
                gCost, myTodayKwh, myTodayCost, mKwh, mCost, yKwh, yCost, gScreen);
}


void pushHistory() {
  if (gVoltage < 50) return;
  gHist[gHistPos] = gVoltage;
  gHistPos = (gHistPos + 1) % GH_SAMPLES;
  if (gHistCount < GH_SAMPLES) gHistCount++;
}


// ============================================================================
//  ИНТЕРФЕЙС — одна страница
// ============================================================================

// ---------------------------------------------------------------- текст
// Кириллица рисуется своим шрифтом 5x7. Горизонтальные серии точек
// объединяются в один прямоугольник — так заметно быстрее.
void drawTxt(int x, int y, const char* s, uint16_t fg, uint16_t bg, uint8_t scale) {
  int w = txtW(s, scale);
  uiFill(x, y, w + scale, 7 * scale, bg);

  int cx = x;
  const char* p = s;
  while (*p) {
    int len = utf8len((unsigned char)*p);
    const uint8_t* g = glyphFind(p, len);
    p += len;
    if (g) {
      for (int row = 0; row < 7; row++) {
        uint8_t bits = g[row];
        int col = 0;
        while (col < 5) {
          if (bits & (0b10000 >> col)) {
            int run = 1;
            while (col + run < 5 && (bits & (0b10000 >> (col + run)))) run++;
            uiFill(cx + col * scale, y + row * scale, run * scale, scale, fg);
            col += run;
          } else {
            col++;
          }
        }
      }
    }
    cx += 6 * scale;
  }
}

void drawTxtC(int cx, int y, const char* s, uint16_t fg, uint16_t bg, uint8_t scale) {
  drawTxt(cx - txtW(s, scale) / 2, y, s, fg, bg, scale);
}

void drawTxtR(int rx, int y, const char* s, uint16_t fg, uint16_t bg, uint8_t scale) {
  drawTxt(rx - txtW(s, scale), y, s, fg, bg, scale);
}

void hhmm(int minute, char* b, size_t n) {
  if (minute < 0) snprintf(b, n, "--:--");
  else snprintf(b, n, "%02d:%02d", (minute / 60) % 24, minute % 60);
}

void durStr(long sec, char* b, size_t n) {
  if (sec < 60)         snprintf(b, n, "%ld С", sec);
  else if (sec < 3600)  snprintf(b, n, "%ld МИН", sec / 60);
  else                  snprintf(b, n, "%ld Ч %02ld МИН", sec / 3600, (sec % 3600) / 60);
}

// ---------------------------------------------------------------- семисегментные цифры
const uint8_t SEG7_DIG[10] = {
  0b0111111,  // 0
  0b0000110,  // 1
  0b1011011,  // 2
  0b1001111,  // 3
  0b1100110,  // 4
  0b1101101,  // 5
  0b1111101,  // 6
  0b0000111,  // 7
  0b1111111,  // 8
  0b1101111,  // 9
};

bool gSegGhost = false;     // погасшие сегменты не рисуем — они мешают

int rndRad(int w, int h) {
  int r = h / 2;
  if (r > w / 2) r = w / 2;
  if (r < 1) r = 1;
  return r;
}

void segBar(int x, int y, int w, int h, uint16_t col, bool on) {
  if (w <= 0 || h <= 0) return;
  if (on)             uiFillRound(x, y, w, h, rndRad(w, h), col);
  else if (gSegGhost) uiFillRound(x, y, w, h, rndRad(w, h), cGhost);
}

void seg7(int x, int y, int w, int h, char ch, uint16_t on) {
  int t = h / 8;
  if (t < 2) t = 2;
  uint8_t m = 0;
  bool dot = false;

  if (ch >= '0' && ch <= '9') m = SEG7_DIG[ch - '0'];
  else if (ch == '-') m = 0b1000000;
  else if (ch == ' ') m = 0;
  else if (ch == '.') { dot = true; }

  int hh = h / 2;

  segBar(x + t, y, w - 2 * t, t, on, m & 0b0000001);                       // a
  segBar(x + w - t, y + t / 2, t, hh - t, on, m & 0b0000010);              // b
  segBar(x + w - t, y + hh + t / 2, t, hh - t, on, m & 0b0000100);         // c
  segBar(x + t, y + h - t, w - 2 * t, t, on, m & 0b0001000);               // d
  segBar(x, y + hh + t / 2, t, hh - t, on, m & 0b0010000);                 // e
  segBar(x, y + t / 2, t, hh - t, on, m & 0b0100000);                      // f
  segBar(x + t, y + hh - t / 2, w - 2 * t, t, on, m & 0b1000000);          // g

  if (dot) uiFill(x + w / 2 - t / 4, y + h - t, t / 2 + 1, t, on);
}

// строка семисегментными цифрами. gap — зазор между цифрами:
// для мощности он увеличен, иначе «18.0» слипается в «180».
void seg7Str(int x, int y, int dh, const char* s, uint16_t on, int gap) {
  gSegGhost = false;
  int dw = dh * 3 / 5;
  int cx = x;
  for (const char* p = s; *p; p++) {
    if (*p == '.') {
      int d = dh / 8;
      int lim = gap - 3;
      if (d > lim) d = lim;
      if (d < 3) d = 3;
      uiFill(cx - gap / 2 - d / 2, y + dh - d + 2, d, d, on);
      continue;
    }
    seg7(cx, y, dw, dh, *p, on);
    cx += dw + gap;
  }
}

int seg7W(const char* s, int dh, int gap) {
  int dw = dh * 3 / 5;
  int n = 0, dots = 0;
  for (const char* p = s; *p; p++) {
    if (*p == '.') { dots++; continue; }
    n++;
  }
  return n * (dw + gap) - gap + dots * (dh / 6);
}

// ---------------------------------------------------------------- каркас экрана
const int HDR_H = 46;

struct Block { int x, y, w, h, scr; };
const Block BLOCKS[5] = {
  {  6,  50, 228, 64, SCR_VOLT    },
  {  6, 118, 110, 56, SCR_TEMP    },
  {124, 118, 110, 56, SCR_POWER   },
  {  6, 178, 228, 66, SCR_TODAY   },
  {  6, 248, 228, 62, SCR_WEATHER },
};

void drawBlockFrame(int x, int y, int w, int h) {
  uiFill(x, y, w, h, cSurf);
  uiRect(x, y, w, h, cLine);
}

void drawTopBar() {
  uiFill(0, 0, 240, HDR_H, cSurf);
  uiFill(0, 0, 240, 1, cAccent);
  uiFill(0, HDR_H - 1, 240, 1, cLine);

  drawTxt(8, 6, "ДОМ", cText, cSurf, 3);
  drawTxt(8, 30, FW_VER, cMuted, cSurf, 1);

  struct tm ti;
  if (getLocalTime(&ti, 10)) {
    static const char* MON[] = { "ЯНВ", "ФЕВ", "МАР", "АПР", "МАЙ", "ИЮН",
                                 "ИЮЛ", "АВГ", "СЕН", "ОКТ", "НОЯ", "ДЕК" };
    char b[20];
    snprintf(b, sizeof(b), "%02d:%02d", ti.tm_hour, ti.tm_min);
    drawTxtR(232, 6, b, cText, cSurf, 3);
    snprintf(b, sizeof(b), "%d %s", ti.tm_mday, MON[ti.tm_mon]);
    drawTxtR(232, 30, b, cMuted, cSurf, 1);
  } else {
    drawTxtR(232, 10, "--:--", cMuted, cSurf, 3);
  }
}

void drawDetailHeader(const char* title) {
  uiFill(0, 0, 240, HDR_H, cSurf);
  uiFill(0, HDR_H - 1, 240, 1, cLine);
  uiFillRound(6, 4, 96, 38, 9, cElev);
  drawTxt(13, 15, "< НАЗАД", cText, cElev, 2);
  drawTxtR(234, 15, title, cText, cSurf, 2);
}

void drawStatusBar() {
  uiFill(0, 311, 240, 9, cSurf);
  bool net = (WiFi.status() == WL_CONNECTED);
  uiCircle(8, 315, 3, net ? cGood : cBadC);
  drawTxt(14, 312, net ? "СЕТЬ" : "НЕТ WI-FI", cMuted, cSurf, 1);

  char b[20];
  if (!gSdOk) drawTxt(72, 312, "НЕТ SD", cBadC, cSurf, 1);
  else {
    snprintf(b, sizeof(b), "SD %d", gSdRows);
    drawTxt(72, 312, b, cMuted, cSurf, 1);
  }
  snprintf(b, sizeof(b), "%d ДБ", gRssi);
  drawTxtR(232, 312, b, cMuted, cSurf, 1);
}

// ---------------------------------------------------------------- сторож сети
// Пример списка устройств домашней сети — адреса замените на свои.
// Проверяем TCP-подключением (порт открыт = устройство живо): это дешевле
// ICMP и не требует raw-сокетов. Таймаут 800 мс, как договаривались.
struct WdHost { const char* name; uint8_t ip[4]; uint16_t port; };

const WdHost WD_HOSTS[] = {
  { "РОУТЕР",  { 192, 168, 1, 1  }, 80 },   // роутер, веб-интерфейс
  { "NAS",     { 192, 168, 1, 10 }, 80 },   // накопитель, веб
  { "КАМЕРА",  { 192, 168, 1, 11 }, 80 },   // камера, веб
  { "ПРИНТЕР", { 192, 168, 1, 12 }, 80 },   // принтер, веб
};
const int WD_N = sizeof(WD_HOSTS) / sizeof(WD_HOSTS[0]);

bool     wdAlive[WD_N];        // результат последней проверки по адресу
bool     wdChecked[WD_N];      // адрес уже проверяли хотя бы раз
uint8_t  wdFail[WD_N];         // сколько кругов подряд адрес не отвечал
int      gWdBad = -1;          // первый неотвечающий, -1 — все живы
unsigned long gWdLast = 0;     // когда проверяли последний адрес
unsigned long gWdCycle = 0;    // начало текущего круга
uint8_t  gWdStep = 0;          // какой адрес проверяем следующим

// Одно TCP-подключение. Со второй попытки — чтобы не ловить случайный
// промах из-за занятого радио.
bool wdProbe(int i) {
  IPAddress ip(WD_HOSTS[i].ip[0], WD_HOSTS[i].ip[1],
               WD_HOSTS[i].ip[2], WD_HOSTS[i].ip[3]);
  for (int attempt = 0; attempt < 2; attempt++) {
    WiFiClient c;
    bool ok = (c.connect(ip, WD_HOSTS[i].port, 800) == 1);
    c.stop();
    if (ok) return true;
    delay(30);
    esp_task_wdt_reset();
  }
  return false;
}

// По одному адресу за проход, чтобы не держать тач. Полный круг — раз в 30 с.
void ledUpdate();               // определена ниже, в разделе светодиода
void wdTick() {
  if (WiFi.status() != WL_CONNECTED) {
    for (int i = 0; i < WD_N; i++) {
      wdAlive[i] = false; wdChecked[i] = false; wdFail[i] = 0;
    }
    gWdBad = -1;                    // про сеть скажет красный светодиод
    return;
  }
  unsigned long now = millis();
  if (gWdStep == 0 && gWdCycle != 0 && now - gWdCycle < 30000UL) return;
  if (gWdLast != 0 && now - gWdLast < 200UL) return;
  gWdLast = now;

  int i = gWdStep;
  bool ok = wdProbe(i);
  wdChecked[i] = true;
  if (ok) { wdFail[i] = 0; wdAlive[i] = true; }
  else if (++wdFail[i] >= 2) wdAlive[i] = false;   // два круга подряд — тревога
  gWdStep = (gWdStep + 1) % WD_N;

  if (gWdStep == 0) {               // круг закончен — подводим итог
    gWdCycle = millis();
    int bad = -1;
    for (int k = 0; k < WD_N; k++)
      if (wdChecked[k] && !wdAlive[k]) { bad = k; break; }
    ledUpdate();
    if (bad != gWdBad) {
      gWdBad = bad;
      gLastDraw = 0;                // сразу перерисовать плашку/светодиод
      if (bad < 0) Serial.println("сторож: все адреса отвечают");
      else         Serial.printf("сторож: %s не отвечает\n", WD_HOSTS[bad].name);
    }
  }
}

// ---------------------------------------------------------------- RGB-светодиод
// Трёхцветный: 4 — красный, 16 — зелёный, 17 — синий. Горит при низком уровне.
void ledInit() {
  pinMode(4, OUTPUT); pinMode(16, OUTPUT); pinMode(17, OUTPUT);
  digitalWrite(4, HIGH); digitalWrite(16, HIGH); digitalWrite(17, HIGH);
}
void ledSet(bool r, bool g, bool b) {
  digitalWrite(4,  r ? LOW : HIGH);
  digitalWrite(16, g ? LOW : HIGH);
  digitalWrite(17, b ? LOW : HIGH);
}
void ledUpdate() {
  // красный  — нет связи с реле/облаком (или нет Wi-Fi)
  // жёлтый   — кто-то из адресов сторожа не отвечает
  // зелёный  — всё в порядке
  bool net = (WiFi.status() == WL_CONNECTED);
  bool link = net && gLastOk != 0 && (millis() - gLastOk < 180000UL);
  if (!link)       { ledSet(true,  false, false); return; }
  if (gWdBad >= 0) { ledSet(true,  true,  false); return; }
  ledSet(false, true, false);
}

// ---------------------------------------------------------------- иконка погоды
void drawWeatherIcon(int cx, int cy, int r, bool hasData) {
  if (!hasData) { uiRing(cx, cy, r, cMuted); return; }

  String s = wDescEn;
  s.toLowerCase();
  bool thunder = s.indexOf("thunder") >= 0;
  bool rain    = s.indexOf("rain") >= 0 || s.indexOf("drizzle") >= 0 || s.indexOf("shower") >= 0;
  bool snow    = s.indexOf("snow") >= 0 || s.indexOf("sleet") >= 0 || s.indexOf("blizzard") >= 0;
  bool cloudy  = s.indexOf("cloud") >= 0 || s.indexOf("overcast") >= 0;
  bool clear   = s.indexOf("clear") >= 0 || s.indexOf("sunny") >= 0;
  bool cloud   = cloudy || rain || snow || thunder;

  if (clear || (!cloud && !rain && !snow && !thunder)) {
    uiCircle(cx, cy, r / 2, cWarn);
    for (int i = 0; i < 8; i++) {
      float a = i * PI / 4.0;
      int x1 = cx + (int)(cos(a) * (r / 2 + 3));
      int y1 = cy + (int)(sin(a) * (r / 2 + 3));
      int x2 = cx + (int)(cos(a) * r);
      int y2 = cy + (int)(sin(a) * r);
      uiLine(x1, y1, x2, y2, cWarn);
    }
    return;
  }

  int cy2 = cy - ((rain || snow || thunder) ? 4 : 0);
  uiCircle(cx - r / 3, cy2, r / 3, cText);
  uiCircle(cx + r / 3, cy2, r / 3, cText);
  uiCircle(cx, cy2 - r / 4, r / 2 - 1, cText);
  uiFill(cx - r / 3, cy2, (r * 2) / 3, r / 3, cText);

  if (thunder) {
    uiLine(cx + 2, cy2 + r / 3, cx - 3, cy2 + r / 2 + 2, cWarn);
    uiLine(cx - 3, cy2 + r / 2 + 2, cx + 3, cy2 + r / 2 + 2, cWarn);
    uiLine(cx + 3, cy2 + r / 2 + 2, cx - 2, cy + r - 1, cWarn);
  } else if (rain) {
    for (int i = -1; i <= 1; i++) {
      int x = cx + i * 7;
      uiLine(x + 2, cy2 + r / 3 + 2, x - 1, cy + r - 1, cSky);
    }
  } else if (snow) {
    for (int i = -1; i <= 1; i++) {
      int x = cx + i * 7;
      int yy = cy + r - 5;
      uiHLine(x - 3, yy, 7, cAccent);
      uiVLine(x, yy - 3, 7, cAccent);
    }
  } else {
    for (int i = -1; i <= 1; i++)
      uiHLine(cx + i * 8 - 3, cy2 + r / 2 + 6, 6, cMuted);
  }
}

// ---------------------------------------------------------------- график 24 ч
void drawChartAxis(int x, int y, int w, int h) {
  const char* lab[5] = { "00", "06", "12", "18", "24" };
  for (int i = 0; i < 5; i++) {
    int px = x + (w - 1) * i / 4;
    if (i == 0)      drawTxt(px, y + h + 2, lab[i], cMuted, cBg, 1);
    else if (i == 4) drawTxt(px - 11, y + h + 2, lab[i], cMuted, cBg, 1);
    else             drawTxt(px - 5, y + h + 2, lab[i], cMuted, cBg, 1);
  }
}

void drawChart24(int x, int y, int w, int h, const float* vals, uint8_t bit,
                 float lo, float hi, uint16_t col, bool band) {
  uiFill(x, y, w, h, cBg);
  if (hi <= lo) hi = lo + 1;
  int plotH = h - 2;

  if (band && V_NORM_HI > lo && V_NORM_LO < hi) {
    int y1 = y + h - 1 - (int)((V_NORM_HI - lo) / (hi - lo) * plotH);
    int y2 = y + h - 1 - (int)((V_NORM_LO - lo) / (hi - lo) * plotH);
    if (y1 < y + 1) y1 = y + 1;
    if (y2 > y + h - 1) y2 = y + h - 1;
    if (y2 > y1) {
      uint16_t cb = gNight ? tft.color565(18, 50, 40) : tft.color565(226, 243, 232);
      uiFill(x + 1, y1, w - 2, y2 - y1, cb);
    }
  }

  for (int i = 1; i < 4; i++) uiHLine(x + 1, y + h * i / 4, w - 2, cLine);

  int px = -1, py = 0, ps = -1;
  for (int i = 0; i < H24_N; i++) {
    if (!(h24Mask[i] & bit)) continue;
    float v = vals[i];
    if (v < lo) v = lo;
    if (v > hi) v = hi;
    int cx = x + 1 + (w - 2) * i / (H24_N - 1);
    int cy = y + h - 2 - (int)((v - lo) / (hi - lo) * (h - 4));
    if (ps >= 0 && i - ps <= 2) uiLine(px, py, cx, cy, col);
    px = cx; py = cy; ps = i;
  }
  if (ps >= 0) uiCircle(px, py, 2, col);

  uiRect(x, y, w, h, cLine);

  char b[12];
  snprintf(b, sizeof(b), "%.0f", hi);
  drawTxt(x + 3, y + 2, b, cMuted, cBg, 1);
  snprintf(b, sizeof(b), "%.0f", lo);
  drawTxt(x + 3, y + h - 9, b, cMuted, cBg, 1);
  if (band) {
    if (V_NORM_HI <= hi && V_NORM_HI >= lo) {
      snprintf(b, sizeof(b), "%.0f", V_NORM_HI);
      drawTxtR(x + w - 3, y + 2, b, cMuted, cBg, 1);
    }
    if (V_NORM_LO >= lo && V_NORM_LO <= hi) {
      snprintf(b, sizeof(b), "%.0f", V_NORM_LO);
      drawTxtR(x + w - 3, y + h - 9, b, cMuted, cBg, 1);
    }
  }
}

// ---------------------------------------------------------------- плашка тревоги
// Правый верхний угол главного экрана: «⚠ <имя> НЕ ОТВЕЧАЕТ».
// Пока все адреса отвечают — не рисуем ничего.
void drawAlertBadge() {
  if (gWdBad < 0 || gWdBad >= WD_N) return;
  const char* name = WD_HOSTS[gWdBad].name;

  int x0 = 68, y0 = 2, w = 168, h = 42;
  uiFillRound(x0, y0, w, h, 9, cWarn);

  // треугольник с восклицательным знаком
  int cx = x0 + 16, by = y0 + 32;
  for (int i = 0; i < 12; i++) uiHLine(cx - i, by - i, 2 * i + 1, cText);
  uiFill(cx, by - 9, 2, 5, cWarn);
  uiFill(cx, by - 3, 2, 2, cWarn);

  drawTxt(x0 + 34, y0 + 6, name, cText, cWarn, 2);
  drawTxt(x0 + 34, y0 + 28, "НЕ ОТВЕЧАЕТ", cText, cWarn, 1);
}

// ---------------------------------------------------------------- главный экран
void drawMain() {
  uiScreen(cBg);
  drawTopBar();

  char b[40];
  bool vok = gVoltage > 50;
  uint16_t vc = vok ? voltColor(gVoltage) : cMuted;

  // ---- напряжение
  const Block& vb = BLOCKS[0];
  drawBlockFrame(vb.x, vb.y, vb.w, vb.h);
  drawTxt(vb.x + 8, vb.y + 5, "НАПРЯЖЕНИЕ", cMuted, cSurf, 1);
  drawTxtR(vb.x + vb.w - 8, vb.y + 5, vok ? voltVerdict(gVoltage) : "НЕТ ДАННЫХ",
           vc, cSurf, 1);

  strcpy(b, vok ? "" : "---");
  if (vok) snprintf(b, sizeof(b), "%.0f", gVoltage);
  int vdh = 34, vgap = 6;
  seg7Str(vb.x + 10, vb.y + 16, vdh, b, vc, vgap);
  int vw = seg7W(b, vdh, vgap);
  drawTxt(vb.x + 10 + vw + 6, vb.y + 30, "В", cMuted, cSurf, 2);

  int rx = vb.x + 104;
  snprintf(b, sizeof(b), "МИН %.0f", (dayMinV < 900) ? dayMinV : 0);
  drawTxt(rx, vb.y + 20, b, cMuted, cSurf, 1);
  snprintf(b, sizeof(b), "МАКС %.0f", dayMaxV);
  drawTxt(rx, vb.y + 32, b, cMuted, cSurf, 1);
  uint16_t rc = gRelay ? cGood : cBadC;
  drawTxtR(vb.x + vb.w - 8, vb.y + 44, gRelay ? "ВКЛ" : "ВЫКЛ", rc, cSurf, 2);

  // ---- температура и мощность
  const Block& tb = BLOCKS[1];
  drawBlockFrame(tb.x, tb.y, tb.w, tb.h);
  drawTxt(tb.x + 7, tb.y + 5, "ТЕМПЕРАТУРА", cMuted, cSurf, 1);
  snprintf(b, sizeof(b), "%.1f", gTemp);
  seg7Str(tb.x + 7, tb.y + 19, 22, b, cTEMP_ACC, 7);
  drawTxt(tb.x + 7 + seg7W(b, 22, 7) + 4, tb.y + 28, "C", cMuted, cSurf, 1);
  snprintf(b, sizeof(b), "СР %.1f", dayTSec ? (float)(daySumT / dayTSec) : gTemp);
  drawTxt(tb.x + 7, tb.y + 45, b, cMuted, cSurf, 1);

  const Block& pb = BLOCKS[2];
  drawBlockFrame(pb.x, pb.y, pb.w, pb.h);
  drawTxt(pb.x + 6, pb.y + 5, "МОЩНОСТЬ", cMuted, cSurf, 1);
  if (gPower >= 1000) snprintf(b, sizeof(b), "%.0f", gPower);
  else                snprintf(b, sizeof(b), "%.1f", gPower);
  int pdh = 21, pgap = 9;
  int pw = seg7W(b, pdh, pgap);
  seg7Str(pb.x + pb.w - 8 - pw, pb.y + 17, pdh, b, cPOWER_ACC, pgap);
  float amps = (gVoltage > 50) ? gPower / gVoltage : 0;
  snprintf(b, sizeof(b), "%.2f А", amps);
  drawTxt(pb.x + 6, pb.y + 45, b, cMuted, cSurf, 1);
  drawTxtR(pb.x + pb.w - 6, pb.y + 45, "ВТ", cMuted, cSurf, 1);

  // ---- сегодня
  const Block& db = BLOCKS[3];
  drawBlockFrame(db.x, db.y, db.w, db.h);
  drawTxt(db.x + 8, db.y + 4, "СЕГОДНЯ", cMuted, cSurf, 1);
  snprintf(b, sizeof(b), "%.2f", myTodayKwh);
  drawTxt(db.x + 8, db.y + 14, b, cGood, cSurf, 3);
  drawTxt(db.x + 8, db.y + 38, "КВТ*Ч", cMuted, cSurf, 1);
  snprintf(b, sizeof(b), "%.2f", myTodayCost);
  drawTxtR(db.x + db.w - 8, db.y + 14, b, C_MONEY, cSurf, 3);
  drawTxtR(db.x + db.w - 8, db.y + 38, "РУБ", cMuted, cSurf, 1);
  snprintf(b, sizeof(b), "ВЧЕРА %.2f", yKwh);
  drawTxt(db.x + 8, db.y + 52, b, cMuted, cSurf, 1);
  snprintf(b, sizeof(b), "МЕСЯЦ %.1f", mKwh);
  drawTxtR(db.x + db.w - 8, db.y + 52, b, cMuted, cSurf, 1);

  // ---- погода
  const Block& wb = BLOCKS[4];
  drawBlockFrame(wb.x, wb.y, wb.w, wb.h);
  if (!wOk) {
    drawTxt(wb.x + 8, wb.y + 8, "ПОГОДА", cMuted, cSurf, 1);
    drawTxt(wb.x + 8, wb.y + 30, "НЕТ ДАННЫХ", cMuted, cSurf, 2);
  } else {
    drawWeatherIcon(wb.x + 24, wb.y + 34, 15, true);
    snprintf(b, sizeof(b), "%.0f", wTemp);
    seg7Str(wb.x + 46, wb.y + 12, 22, b, cSky, 5);
    drawTxt(wb.x + 46 + seg7W(b, 22, 5) + 4, wb.y + 20, "C", cMuted, cSurf, 1);

    char d[20];
    descToRus(wDescEn, d, sizeof(d));
    drawTxt(wb.x + 100, wb.y + 10, d, cText, cSurf, 1);
    snprintf(b, sizeof(b), "ВЕТЕР %.1f М/С", wWind / 3.6f);
    drawTxt(wb.x + 100, wb.y + 24, b, cMuted, cSurf, 1);
    snprintf(b, sizeof(b), "ВЛАЖН %.0f%%", wHum);
    drawTxt(wb.x + 100, wb.y + 36, b, cMuted, cSurf, 1);

    drawTxt(wb.x + 8, wb.y + 52, "ВАШ ГОРОД", cMuted, cSurf, 1);
    if (wTime) {
      struct tm wt;
      localtime_r(&wTime, &wt);
      snprintf(b, sizeof(b), "%02d:%02d", wt.tm_hour, wt.tm_min);
    } else strcpy(b, "--:--");
    drawTxtR(wb.x + wb.w - 8, wb.y + 52, b, cMuted, cSurf, 1);
  }

  drawStatusBar();
  drawAlertBadge();
}

// ---------------------------------------------------------------- окно напряжения
void drawVoltScreen() {
  uiScreen(cBg);
  drawDetailHeader("ВОЛЬТЫ");
  bool ok = gVoltage > 50;
  uint16_t vc = ok ? voltColor(gVoltage) : cMuted;
  char b[40];

  snprintf(b, sizeof(b), ok ? "%.0f" : "---", gVoltage);
  int dh = 46, gap = 6;
  int vw = seg7W(b, dh, gap);
  seg7Str(12, 54, dh, b, vc, gap);
  drawTxt(12 + vw + 8, 80, "В", cMuted, cBg, 2);

  const char* verdict = ok ? voltVerdict(gVoltage) : "НЕТ ДАННЫХ";
  int vw2 = txtW(verdict, 1);
  uiFillRound(226 - vw2 - 6, 58, vw2 + 12, 20, 6, cElev);
  drawTxt(226 - vw2, 65, verdict, vc, cElev, 1);
  drawTxt(150, 86, gRelay ? "РЕЛЕ ВКЛ" : "РЕЛЕ ВЫКЛ", gRelay ? cGood : cBadC, cBg, 1);

  drawTxt(12, 104, "НОРМА 207-253 В", cMuted, cBg, 1);
  drawTxt(12, 116, "ГОСТ 32144-2013", cMuted, cBg, 1);

  const int colX[3] = { 12, 88, 164 };
  const char* lab[3] = { "МИН", "СРЕД", "МАКС" };
  float val[3] = { (dayMinV < 900) ? dayMinV : 0,
                   (dayVSec > 0) ? (float)(daySumV / dayVSec) : 0,
                   dayMaxV };
  int mins[3] = { dayMinVMin, -1, dayMaxVMin };
  for (int i = 0; i < 3; i++) {
    drawTxt(colX[i], 132, lab[i], cMuted, cBg, 1);
    snprintf(b, sizeof(b), "%.0f", val[i]);
    drawTxt(colX[i], 142, b, cText, cBg, 2);
    hhmm(mins[i], b, sizeof(b));
    if (i == 1) strcpy(b, "24 Ч");
    drawTxt(colX[i], 160, b, cMuted, cBg, 1);
  }
  uiVLine(80, 130, 42, cLine);
  uiVLine(156, 130, 42, cLine);

  float pct = (dayVSec > 0) ? 100.0f * dayOutSec / dayVSec : 0;
  durStr(dayOutSec, b, sizeof(b));
  char line[64];
  snprintf(line, sizeof(line), "ВНЕ НОРМЫ %.0f%%  %s", pct, b);
  drawTxt(12, 176, line, (dayOutSec > 0) ? cWarn : cGood, cBg, 1);

  drawTxt(12, 192, "НАПРЯЖЕНИЕ 24 Ч", cMuted, cBg, 1);

  float lo = (dayMinV < 900) ? dayMinV : 225;
  float hi = (dayMaxV > 0) ? dayMaxV : 245;
  lo = floorf((lo - 10.0f) / 5.0f) * 5.0f;
  hi = ceilf((hi + 10.0f) / 5.0f) * 5.0f;
  if (hi - lo < 30.0f) { float mid = (lo + hi) / 2.0f; lo = mid - 15.0f; hi = mid + 15.0f; }
  drawChart24(10, 202, 220, 92, h24V, 1, lo, hi, cAccent, true);
  drawChartAxis(10, 202, 220, 92);
  drawTxt(10, 306, "ЗЕЛЁНАЯ ЗОНА 207-253 В", cMuted, cBg, 1);
}

// ---------------------------------------------------------------- окно температуры
void drawTempScreen() {
  uiScreen(cBg);
  drawDetailHeader("ТЕМП");

  char b[40];
  snprintf(b, sizeof(b), "%.1f", gTemp);
  int dh = 46, gap = 7;
  int tw = seg7W(b, dh, gap);
  seg7Str(12, 54, dh, b, cTEMP_ACC, gap);
  drawTxt(12 + tw + 8, 80, "C", cMuted, cBg, 2);

  drawTxt(12, 112, "МАКС ЗА СУТКИ", cMuted, cBg, 1);
  snprintf(b, sizeof(b), "%.1f", (dayMaxT > -100) ? dayMaxT : gTemp);
  drawTxt(12, 122, b, cTEMP_ACC, cBg, 3);
  hhmm(dayMaxTMin, b, sizeof(b));
  drawTxt(12, 148, b, cMuted, cBg, 1);

  drawTxt(100, 112, "СРЕДНЯЯ", cMuted, cBg, 1);
  snprintf(b, sizeof(b), "%.1f", dayTSec ? (float)(daySumT / dayTSec) : gTemp);
  drawTxt(100, 122, b, cText, cBg, 2);

  drawTxt(100, 146, "МИН", cMuted, cBg, 1);
  snprintf(b, sizeof(b), "%.1f", (dayMinT < 900) ? dayMinT : gTemp);
  drawTxt(148, 146, b, cMuted, cBg, 1);

  drawTxt(12, 176, "ТЕМПЕРАТУРА 24 Ч", cMuted, cBg, 1);

  float lo = dayMinT, hi = dayMaxT;
  if (hi < -100) { lo = 15; hi = 25; }
  lo = floorf(lo / 5.0f) * 5.0f - 5.0f;
  hi = ceilf(hi / 5.0f) * 5.0f + 5.0f;
  if (hi - lo < 10) hi = lo + 10;
  drawChart24(10, 186, 220, 106, h24T, 2, lo, hi, cTEMP_ACC, false);
  drawChartAxis(10, 186, 220, 106);
  drawTxt(10, 306, "ДАТЧИК TUYA", cMuted, cBg, 1);
}

// ---------------------------------------------------------------- окно мощности
void drawPowerScreen() {
  uiScreen(cBg);
  drawDetailHeader("МОЩНОСТЬ");

  char b[40];
  if (gPower >= 1000) snprintf(b, sizeof(b), "%.0f", gPower);
  else                snprintf(b, sizeof(b), "%.1f", gPower);
  int dh = 46, gap = 9;
  int pw = seg7W(b, dh, gap);
  seg7Str(12, 54, dh, b, cPOWER_ACC, gap);
  drawTxt(12 + pw + 8, 80, "ВТ", cMuted, cBg, 2);

  float amps = (gVoltage > 50) ? gPower / gVoltage : 0;

  drawTxt(12, 112, "СРЕДНЯЯ", cMuted, cBg, 1);
  snprintf(b, sizeof(b), "%.0f ВТ", (dayPSec > 0) ? (float)(daySumP / dayPSec) : gPower);
  drawTxt(12, 122, b, cText, cBg, 2);

  drawTxt(118, 112, "ПИК", cMuted, cBg, 1);
  snprintf(b, sizeof(b), "%.0f ВТ", dayMaxP);
  drawTxt(118, 122, b, cWarn, cBg, 2);
  hhmm(dayMaxPMin, b, sizeof(b));
  drawTxt(118, 140, b, cMuted, cBg, 1);

  drawTxt(12, 154, "ТОК", cMuted, cBg, 1);
  snprintf(b, sizeof(b), "%.2f А", amps);
  drawTxt(54, 154, b, cText, cBg, 2);

  drawTxt(12, 172, "НАПРЯЖЕНИЕ X ТОК", cMuted, cBg, 1);
  snprintf(b, sizeof(b), "%.0f В  %.2f А  %.0f ВТ", gVoltage, amps, gPower);
  drawTxt(12, 184, b, cMuted, cBg, 1);

  drawTxt(12, 200, "МОЩНОСТЬ 24 Ч", cMuted, cBg, 1);
  float hi = dayMaxP * 1.15f;
  if (hi < 30) hi = 30;
  hi = ceilf(hi / 10.0f) * 10.0f;
  drawChart24(10, 210, 220, 82, h24P, 4, 0, hi, cPOWER_ACC, false);
  drawChartAxis(10, 210, 220, 82);
  drawTxt(10, 306, "АКТИВНАЯ МОЩНОСТЬ", cMuted, cBg, 1);
}

// ---------------------------------------------------------------- окно итогов
void drawTodayScreen() {
  uiScreen(cBg);
  drawDetailHeader("ИТОГИ");

  char b[40];
  drawTxt(12, 54, "СЕГОДНЯ", cMuted, cBg, 1);
  snprintf(b, sizeof(b), "%.2f", myTodayKwh);
  drawTxt(12, 64, b, cGood, cBg, 4);
  drawTxt(12, 94, "КВТ*Ч", cMuted, cBg, 1);

  snprintf(b, sizeof(b), "%.2f", myTodayCost);
  drawTxtR(228, 64, b, C_MONEY, cBg, 4);
  drawTxtR(228, 94, "РУБ", cMuted, cBg, 1);

  uiHLine(12, 108, 216, cLine);
  drawTxt(120, 110, "КВТ*Ч", cMuted, cBg, 1);
  drawTxtR(228, 110, "РУБ", cMuted, cBg, 1);

  float dk = statDayKwh, nk = statNightKwh;
  if (dk + nk < 0.01f && myTodayKwh > 0) {
    nk = myTodayKwh * 7.0f / 24.0f;
    dk = myTodayKwh - nk;
  }

  struct tm ti;
  int sod = 0;
  if (getLocalTime(&ti, 10)) sod = ti.tm_hour * 3600 + ti.tm_min * 60 + ti.tm_sec;
  float elapsed = sod / 3600.0f;
  float projKwh = (elapsed > 0.15f) ? myTodayKwh * 24.0f / elapsed : myTodayKwh;
  if (projKwh < myTodayKwh) projKwh = myTodayKwh;
  float avgRub = (myTodayKwh > 0.01f) ? myTodayCost / myTodayKwh : measuredTariff();
  float projCost = projKwh * avgRub;

  struct Row { const char* name; float kwh; float rub; bool moneyOnly; };
  const char* tariffTxt = "8.39 / 4.49";
  Row rows[5] = {
    { "ВЧЕРА",  yKwh, yCost, false },
    { "МЕСЯЦ",  mKwh, mCost, false },
    { "ТАРИФ",  0, 0, true },
    { "ДЕНЬ",   dk, dk * TARIFF_DAY, false },
    { "НОЧЬ",   nk, nk * TARIFF_NIGHT, false },
  };

  int y = 122;
  for (int i = 0; i < 5; i++) {
    drawTxt(12, y + 4, rows[i].name, cMuted, cBg, 1);
    if (rows[i].moneyOnly) {
      drawTxt(76, y + 4, "ДЕНЬ/НОЧЬ", cMuted, cBg, 1);
      drawTxtR(228, y + 4, tariffTxt, cText, cBg, 1);
    } else {
      snprintf(b, sizeof(b), "%.2f", rows[i].kwh);
      drawTxtR(150, y, b, cText, cBg, 2);
      snprintf(b, sizeof(b), "%.2f", rows[i].rub);
      drawTxtR(228, y, b, cText, cBg, 2);
    }
    y += 24;
  }

  drawTxt(12, y + 4, "ПРОГНОЗ", cWarn, cBg, 1);
  snprintf(b, sizeof(b), "%.2f", projKwh);
  drawTxtR(150, y, b, cWarn, cBg, 2);
  snprintf(b, sizeof(b), "%.2f", projCost);
  drawTxtR(228, y, b, cWarn, cBg, 2);

  uiHLine(12, y + 24, 216, cLine);
  snprintf(b, sizeof(b), "СЕЙЧАС %.0f ВТ", gPower);
  drawTxt(12, y + 30, b, cMuted, cBg, 1);
  snprintf(b, sizeof(b), "СРЕДНИЙ ТАРИФ %.2f", measuredTariff());
  drawTxtR(228, y + 30, b, cMuted, cBg, 1);
}

// ---------------------------------------------------------------- окно погоды
void drawWeatherScreen() {
  uiScreen(cBg);
  drawDetailHeader("ПОГОДА");

  char b[40];
  drawTxt(12, 54, "ВАШ ГОРОД", cMuted, cBg, 1);
  if (!wOk) {
    drawTxt(12, 76, "НЕТ ДАННЫХ", cMuted, cBg, 3);
    drawTxt(12, 140, "ПРОВЕРЬТЕ WI-FI", cMuted, cBg, 1);
    return;
  }

  snprintf(b, sizeof(b), "%.1f", wTemp);
  int dh = 44, gap = 7;
  int tw = seg7W(b, dh, gap);
  seg7Str(12, 64, dh, b, cSky, gap);
  drawTxt(12 + tw + 8, 84, "C", cMuted, cBg, 2);

  drawWeatherIcon(192, 92, 34, true);

  char d[24];
  descToRus(wDescEn, d, sizeof(d));
  drawTxt(12, 124, d, cText, cBg, 2);

  uiHLine(12, 150, 216, cLine);

  drawTxt(12, 160, "ВЕТЕР", cMuted, cBg, 1);
  snprintf(b, sizeof(b), "%.1f М/С", wWind / 3.6f);
  drawTxtR(228, 158, b, cText, cBg, 2);

  drawTxt(12, 186, "ВЛАЖНОСТЬ", cMuted, cBg, 1);
  snprintf(b, sizeof(b), "%.0f%%", wHum);
  drawTxtR(228, 184, b, cText, cBg, 2);

  drawTxt(12, 212, "ОБНОВЛЕНО", cMuted, cBg, 1);
  if (wTime) {
    struct tm wt;
    localtime_r(&wTime, &wt);
    snprintf(b, sizeof(b), "%02d:%02d", wt.tm_hour, wt.tm_min);
  } else strcpy(b, "--:--");
  drawTxtR(228, 210, b, cText, cBg, 2);

  drawTxt(12, 238, "ИСТОЧНИК", cMuted, cBg, 1);
  drawTxtR(228, 236, "WTTR.IN", cText, cBg, 2);

  uiHLine(12, 262, 216, cLine);
  drawTxt(12, 270, "ТЕМПЕРАТУРА ВОЗДУХА", cMuted, cBg, 1);
  drawTxt(12, 284, "ВЕТЕР В М/С, ВЛАЖНОСТЬ В %", cMuted, cBg, 1);
}

// ---------------------------------------------------------------- диспетчер
void drawScreen() {
  switch (gScreen) {
    case SCR_VOLT:    drawVoltScreen();    break;
    case SCR_TEMP:    drawTempScreen();    break;
    case SCR_POWER:   drawPowerScreen();   break;
    case SCR_TODAY:   drawTodayScreen();   break;
    case SCR_WEATHER: drawWeatherScreen(); break;
    default:          drawMain();          break;
  }
}

// ---------------------------------------------------------------- обновление без мигания
// Отпечаток данных, которые видны на экране. Если он не изменился,
// перерисовывать нечего — экран не мигает.
uint32_t screenSig() {
  uint32_t h = 2166136261u;
  uint32_t v[14];
  int n = 0;
  v[n++] = (uint32_t)(int32_t)lroundf(gVoltage);
  v[n++] = gTempValid ? (uint32_t)(int32_t)lroundf(gTemp * 10.0f) : 0xFFFFFFu;
  v[n++] = (uint32_t)(int32_t)lroundf(gPower);
  v[n++] = gRelay ? 1u : 0u;
  v[n++] = (uint32_t)(int32_t)lroundf(((dayMinV < 900) ? dayMinV : 0) * 10.0f);
  v[n++] = (uint32_t)(int32_t)lroundf(dayMaxV * 10.0f);
  v[n++] = (uint32_t)(int32_t)((dayVSec > 0) ? (daySumV / dayVSec) * 10.0 : 0);
  v[n++] = (uint32_t)(int32_t)lroundf(myTodayKwh * 100.0f);
  v[n++] = (uint32_t)(int32_t)lroundf(myTodayCost * 100.0f);
  v[n++] = (uint32_t)(int32_t)lroundf(yKwh * 100.0f);
  v[n++] = (uint32_t)(int32_t)lroundf(mKwh * 100.0f);
  v[n++] = (uint32_t)(int32_t)lroundf(wTemp * 10.0f);
  v[n++] = (uint32_t)(int32_t)lroundf(wHum);
  v[n++] = (uint32_t)(int32_t)lroundf(wWind);
  for (int i = 0; i < n; i++) { h ^= v[i]; h *= 16777619u; }
  return h;
}

// Главный экран: стираем только содержимое между шапкой и нижней строкой,
// шапка остаётся на месте — полной заливки и вспышки нет.
void refreshMain() {
  if (gCap) { drawScreen(); return; }
  uiFill(0, HDR_H, 240, 311 - HDR_H, cBg);
  gNoClear = true;
  drawMain();
  gNoClear = false;
}

// Окно открыто: стираем ТОЛЬКО области с цифрами, остальное перерисовывается
// поверх теми же пикселями. Полной заливки экрана нет, тач не блокируется.
void refreshWindow() {
  if (gCap) { drawScreen(); return; }
  switch (gScreen) {
    case SCR_VOLT:
      uiFill(10,  50, 214, 52, cBg);    // цифры, вердикт, реле
      uiFill(10, 140, 216, 30, cBg);    // мин / сред / макс
      uiFill(12, 176, 216,  7, cBg);    // вне нормы
      break;
    case SCR_TEMP:
      uiFill(10,  50, 214, 52, cBg);    // цифры
      uiFill(10, 120, 216, 40, cBg);    // макс / средняя / мин
      break;
    case SCR_POWER:
      uiFill(10,  50, 214, 52, cBg);    // цифры
      uiFill(10, 120, 216, 72, cBg);    // средняя / пик / ток
      break;
    default:
      uiFill(10, 54, 220, 240, cBg);    // «ИТОГИ» и «ПОГОДА»
      break;
  }
  gNoClear = true;
  drawScreen();
  gNoClear = false;
}

// ---------------------------------------------------------------- тачскрин
bool inRect(int x, int y, int rx, int ry, int rw, int rh) {
  return x >= rx && x < rx + rw && y >= ry && y < ry + rh;
}

void handleTapAt(int x, int y) {
  if (gScreen != SCR_MAIN) {
    if (inRect(x, y, 6, 4, 100, 38)) {
      gScreen = SCR_MAIN;
      drawScreen();
      gLastDraw = millis();
      gLastSig = screenSig();
      gLastWinFull = millis();
    }
    return;
  }

  for (int i = 0; i < 5; i++) {
    if (inRect(x, y, BLOCKS[i].x, BLOCKS[i].y, BLOCKS[i].w, BLOCKS[i].h)) {
      gScreen = BLOCKS[i].scr;
      drawScreen();
      gLastDraw = millis();
      gLastSig = screenSig();
      gLastWinFull = millis();
      return;
    }
  }
}

void handleTouch() {
  static unsigned long lock = 0;
  if (millis() < lock) return;
  if (!tpDown()) return;

  int x = 0, y = 0;
  if (!tpPoint(&x, &y)) { lock = millis() + 300; return; }

  unsigned long t0 = millis();
  while (tpDown() && millis() - t0 < 2000UL) { delay(10); esp_task_wdt_reset(); }
  lock = millis() + 350;
  Serial.printf("TAP %d %d\n", x, y);
  handleTapAt(x, y);
}

// ---------------------------------------------------------------- импорт истории на карту
// По команде IMP плата открывает /energy.csv и дописывает строки до IMPEND.
void importStart() {
  if (!gSdOk) { Serial.println("IMP-ERR нет карты"); return; }
  Serial.println("IMP-READY");
  File f = SD.open("/energy.csv", FILE_APPEND);
  if (!f) { Serial.println("IMP-ERR файл"); return; }

  int n = 0;
  unsigned long t0 = millis();
  while (millis() - t0 < 180000) {
    if (!Serial.available()) { delay(10); esp_task_wdt_reset(); continue; }
    String line = Serial.readStringUntil('\n');
    line.trim();
    if (line == "IMPEND") break;
    if (line.length() > 12 && isDigit(line[0])) {
      f.println(line);
      n++;
      if (n % 100 == 0) f.flush();
    }
    esp_task_wdt_reset();
  }
  f.close();
  gSdRows += n;
  Serial.printf("IMP-DONE %d\n", n);
}

// ---------------------------------------------------------------- служебные команды
void doShot(int n) {
  if (gCap) return;
  int prev = gScreen;
  if (n >= 0 && n <= 5) gScreen = n;
  Serial.println("SHOT-BEGIN");
  gCap = true;
  gCapLines = 0;
  gCapStartMs = millis();
  drawScreen();
  gCap = false;
  Serial.println("SHOT-END");
  gScreen = prev;
  drawScreen();
  gLastDraw = millis();
  gLastSig = screenSig();
  gLastWinFull = millis();
}

void printCsvTail() {
  if (!gSdOk) { Serial.println("CSV-ERR нет карты"); return; }
  File f = SD.open("/energy.csv", FILE_READ);
  if (!f) { Serial.println("CSV-ERR файл"); return; }
  size_t sz = f.size();
  Serial.printf("CSV %u байт, строк %d\n", (unsigned)sz, gSdRows);
  size_t off = (sz > 1200) ? (sz - 1200) : 0;
  if (off > 0) { f.seek(off); f.readStringUntil('\n'); }
  String lines[8];
  int n = 0;
  while (f.available()) {
    String l = f.readStringUntil('\n');
    l.trim();
    if (l.length() > 10) {
      for (int i = 0; i < 7; i++) lines[i] = lines[i + 1];
      lines[7] = l;
      if (n < 8) n++;
    }
  }
  f.close();
  for (int i = 8 - n; i < 8; i++) Serial.println(lines[i]);
}

void printDiag() {
  char t1[8], t2[8];
  hhmm(dayMinVMin, t1, sizeof(t1));
  hhmm(dayMaxVMin, t2, sizeof(t2));
  Serial.printf("DIAG v=%.0f min=%.0f@%s max=%.0f@%s avg=%.1f out=%ld/%ld "
                "today=%.3f/%.2f y=%.3f/%.2f m=%.2f/%.2f tar=%.2f heap=%u\n",
                gVoltage,
                (dayMinV < 900) ? dayMinV : 0, t1,
                dayMaxV, t2,
                (dayVSec > 0) ? (float)(daySumV / dayVSec) : 0,
                dayOutSec, dayVSec,
                myTodayKwh, myTodayCost, yKwh, yCost, mKwh, mCost,
                measuredTariff(), ESP.getFreeHeap());
}

// Диагностика истории: где начинается сегодняшний день и сколько строк в нём.
// CSVDAY — по часам, чтобы было видно, за какие часы данные вообще есть.
void printCsvDay() {
  if (!gSdOk) { Serial.println("CSVDAY-ERR нет карты"); return; }
  File f = SD.open("/energy.csv", FILE_READ);
  if (!f) { Serial.println("CSVDAY-ERR файл"); return; }
  struct tm ti;
  if (!getLocalTime(&ti, 10)) { Serial.println("CSVDAY-ERR нет времени"); f.close(); return; }

  char dayPrefix[12];
  snprintf(dayPrefix, sizeof(dayPrefix), "%04d-%02d-%02d",
           ti.tm_year + 1900, ti.tm_mon + 1, ti.tm_mday);
  size_t sz = f.size();
  size_t off = findDayStartOffset(f, dayPrefix);
  f.seek(off);

  int perHour[24];
  for (int i = 0; i < 24; i++) perHour[i] = 0;
  int n = 0;
  String first, last;
  while (f.available()) {
    String l = f.readStringUntil('\n');
    l.trim();
    long t = lineEpoch(l);
    if (!t) continue;
    int minute = (int)((t % 86400L) / 60);
    perHour[minute / 60]++;
    if (n == 0) first = l;
    last = l;
    n++;
  }
  f.close();
  Serial.printf("CSVDAY файл %u байт, день с %u, строк за сегодня %d\n",
                (unsigned)sz, (unsigned)off, n);
  Serial.printf("CSVDAY первая: %s\n", first.c_str());
  Serial.printf("CSVDAY последняя: %s\n", last.c_str());
  Serial.print("CSVDAY по часам:");
  for (int i = 0; i < 24; i++) Serial.printf(" %d:%d", i, perHour[i]);
  Serial.println();
}

// Диагностика сторожа: какие адреса реально доступны с платы.
// Команда WD — три круга опроса подряд с тем же таймаутом 800 мс.
void printWd() {
  Serial.printf("WD с платы %s, адресов %d\n",
                WiFi.localIP().toString().c_str(), WD_N);
  for (int round = 0; round < 3; round++) {
    Serial.printf("WD круг %d:", round + 1);
    for (int i = 0; i < WD_N; i++) {
      IPAddress ip(WD_HOSTS[i].ip[0], WD_HOSTS[i].ip[1],
                   WD_HOSTS[i].ip[2], WD_HOSTS[i].ip[3]);
      unsigned long t0 = millis();
      WiFiClient c;
      bool ok = (c.connect(ip, WD_HOSTS[i].port, 800) == 1);
      unsigned long dt = millis() - t0;
      c.stop();
      Serial.printf(" %s(%u)=%s/%lums",
                    WD_HOSTS[i].name, WD_HOSTS[i].port,
                    ok ? "ok" : "НЕТ", dt);
      delay(200);
    }
    Serial.println();
  }
  Serial.print("WD состояние:");
  for (int i = 0; i < WD_N; i++)
    Serial.printf(" %s=%s/%d", WD_HOSTS[i].name,
                  wdAlive[i] ? "жив" : "нет", wdFail[i]);
  Serial.printf(", gWdBad=%d\n", gWdBad);
}

// Диагностика формата истории: сколько колонок в сегодняшних строках и
// как выглядят строки с 7+ полями. CSVT
void printCsvT() {
  if (!gSdOk) { Serial.println("CSVT-ERR нет карты"); return; }
  File f = SD.open("/energy.csv", FILE_READ);
  if (!f) { Serial.println("CSVT-ERR файл"); return; }
  struct tm ti;
  if (!getLocalTime(&ti, 10)) { Serial.println("CSVT-ERR нет времени"); f.close(); return; }
  char dayPrefix[12];
  snprintf(dayPrefix, sizeof(dayPrefix), "%04d-%02d-%02d",
           ti.tm_year + 1900, ti.tm_mon + 1, ti.tm_mday);
  size_t off = findDayStartOffset(f, dayPrefix);
  f.seek(off);
  int cols[12];
  for (int i = 0; i < 12; i++) cols[i] = 0;
  int shown = 0;
  while (f.available()) {
    String l = f.readStringUntil('\n');
    l.trim();
    if (l.length() < 10) continue;
    if (!l.startsWith(dayPrefix)) continue;
    int commas = 0;
    for (int i = 0; i < (int)l.length(); i++) if (l[i] == ',') commas++;
    if (commas < 12) cols[commas]++;
    if (commas >= 6 && shown < 6) { Serial.printf("CSVT %s\n", l.c_str()); shown++; }
  }
  f.close();
  Serial.print("CSVT полей в строке:");
  for (int i = 0; i < 12; i++) if (cols[i]) Serial.printf(" %d=%d", i + 1, cols[i]);
  Serial.println();
}

// ============================================================================
//  SETUP
// ============================================================================
void setup() {
  Serial.begin(115200);
  delay(1200);
  Serial.printf("\n=== ДОМ %s ===\n", FW_VER);

  pinMode(21, OUTPUT);
  analogWrite(21, 255);
  ledInit();

  tpInit();
  loadCal();
  loadWifi();

  tft.init();
  tft.setRotation(0);
  tft.fillScreen(TFT_BLACK);

  // заставка своим шрифтом
  drawTxtC(120, 120, "ДОМ", 0x05FF, 0x0000, 6);
  drawTxtC(120, 180, "ЭНЕРГИЯ", 0xFFFF, 0x0000, 3);
  drawTxtC(120, 215, "ПОДКЛЮЧЕНИЕ...", 0x5AEB, 0x0000, 1);

  if (!connectWifi()) {
    uiFill(0, 200, 240, 60, TFT_BLACK);
    drawTxtC(120, 210, "НЕТ WI-FI", 0xF800, 0x0000, 3);
    drawTxtC(120, 240, gSsid.c_str(), 0x5AEB, 0x0000, 1);
  }

  configTime(3 * 3600, 0, "pool.ntp.org", "time.nist.gov");
  struct tm ti;
  unsigned long t0 = millis();
  while (millis() - t0 < 8000) {
    if (getLocalTime(&ti, 300)) break;
    delay(200);
  }

  pickTheme();

  gSdOk = initSD();
  Serial.printf("SD: %s, строк %d\n", gSdOk ? "ok" : "нет", gSdRows);

  loadToday();
  loadPeriods();

  tuyaQuery();
  cloudStatus();
  updateToday();
  seedStatsFromSd();
  updatePeriods();
  fetchFromApi();
  pushHistory();
  fetchWeather();

  drawScreen();
  gLastDraw = millis();
  sendJsonStatus();
  sendJsonData();
}

// ============================================================================
//  LOOP
// ============================================================================
void loop() {
  static unsigned long lastPoll = 0;
  static unsigned long lastWeather = 0;
  static unsigned long lastToday = 0;
  static unsigned long lastPeriods = 0;
  static unsigned long lastSeed = 0;
  static uint8_t netStep = 0;

  // 1. Касание — САМЫМ ПЕРВЫМ, до любых тяжёлых операций: раньше HTTP,
  //    облако и SD держали цикл десятками секунд, и тач «умирал».
  handleTouch();
  statsTick();
  wdTick();                        // сторож сети: один адрес за проход

  // 2. «сегодня» — раз в 5 с (карта читается не чаще раза в минуту)
  if (millis() - lastToday > 5000UL) {
    lastToday = millis();
    updateToday();
  }
  if (millis() - lastPeriods > 60000UL) {
    lastPeriods = millis();
    updatePeriods();
  }
  // статистику дня засеваем из карты, пока не получилось
  // (например, при старте ещё не было времени от NTP)
  if (!statsSeeded && millis() - lastSeed > 30000UL) {
    lastSeed = millis();
    seedStatsFromSd();
  }

  // 3. Сеть: ОДНА тяжёлая операция за проход, между ними снова читаем тач.
  if (netStep == 0 && millis() - lastPoll > 15000UL) netStep = 1;
  if (netStep != 0) {
    if (WiFi.status() != WL_CONNECTED) {
      WiFi.reconnect();
      netStep = 0;
      lastPoll = millis();
    } else {
      switch (netStep) {
        case 1:
          gRssi = WiFi.RSSI();
          gTuyaOk = tuyaQuery();
          break;
        case 2:
          // раз в минуту — облако: даёт напряжение и мощность, даже если
          // локальный протокол реле их не прислал
          if (millis() - gCloudLast > 60000UL) {
            gCloudLast = millis();
            cloudStatus();
          }
          break;
        case 3:
          fetchFromApi();
          break;
        default:
          pushHistory();
          writeSd();
          ledUpdate();
          sendJsonData();
          lastPoll = millis();
          netStep = 0;
          break;
      }
      if (netStep != 0) netStep++;      // 1 -> 2 -> 3 -> 4(default) -> 0
      handleTouch();                    // между операциями — снова тач
    }
  }

  // 4. Погода раз в 10 минут
  if (millis() - lastWeather > 600000UL || (!wOk && millis() > 20000UL)) {
    lastWeather = millis();
    if (WiFi.status() == WL_CONNECTED) {
      handleTouch();
      fetchWeather();
      gLastDraw = 0;                // показать новые данные
    }
  }

  // 5. Перерисовка.
  //    Окно не открыто — главный экран не чаще раза в 30 с.
  //    Окно открыто — полной перерисовки нет: обновляем только цифры.
  if (gScreen == SCR_MAIN) {
    if (gLastDraw == 0 || millis() - gLastDraw > 30000UL) {
      gLastDraw = millis();
      refreshMain();
      gLastSig = screenSig();
      gLastWinFull = millis();
    }
  } else {
    uint32_t sig = screenSig();
    if (sig != gLastSig || millis() - gLastWinFull > 300000UL) {
      gLastSig = sig;
      gLastWinFull = millis();
      refreshWindow();
    }
    gLastDraw = millis();           // окно открыто — полную перерисовку не делаем
  }

  // статус в порт
  if (millis() - gLastStatus > 5000UL) {
    gLastStatus = millis();
    sendJsonStatus();
  }

  // команды: SHOT [0..5] — скриншот, CSV — хвост карты, DIAG — статистика
  if (Serial.available()) {
    String cmd = Serial.readStringUntil('\n');
    cmd.trim();
    String up = cmd;
    up.toUpperCase();
    if (up == "IMP") {
      importStart();
    } else if (up.startsWith("SHOT")) {
      int n = -1;
      if (up.length() > 4) n = up.substring(4).toInt();
      if (n < 0 || n > 5) n = -1;
      doShot(n);
    } else if (up == "CSV") {
      printCsvTail();
    } else if (up == "CSVDAY") {
      printCsvDay();
    } else if (up == "CSVT") {
      printCsvT();
    } else if (up == "WD") {
      printWd();
    } else if (up == "DIAG") {
      printDiag();
    } else if (up == "STATUS") {
      sendJsonStatus();
    } else if (up.startsWith("TAP")) {
      int x = 0, y = 0;
      sscanf(up.c_str(), "TAP %d %d", &x, &y);
      Serial.printf("TAPCMD %d %d -> %d\n", x, y, gScreen);
      handleTapAt(x, y);
    }
  }

  esp_task_wdt_reset();
  delay(30);
}
