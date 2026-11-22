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
#include <WebServer.h>
#include <WiFiClientSecure.h>
#include <TFT_eSPI.h>
#include <Preferences.h>
#include <HTTPClient.h>
#include <SPI.h>
#include <SD.h>
#include <time.h>

#include "font5x7.h"

int rndRad(int w, int h);
// сторож объявлен ниже, а нижняя полоса рисует его состояние
extern int      gWdBad;
extern bool     wdAlive[];
extern bool     wdChecked[];
extern uint16_t wdMs[];
void updatePeriods();
#include <esp_task_wdt.h>

#include <mbedtls/gcm.h>
#include <mbedtls/md.h>

// ---------------------------------------------------------------- настройки
const char* FW_VER = "4.3";

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
// Координаты ВАШЕГО ГОРОДА: YOUR_LAT, YOUR_LON. По названию wttr.in путает город
// («your city» давало +4 °C вместо +14 °C), по координатам — верно.
const char* WEATHER_URL =
  "http://wttr.in/~YOUR_LAT,YOUR_LON?format=%t|%h|%w|%C";

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
enum { SCR_MAIN = 0, SCR_VOLT, SCR_TEMP, SCR_POWER, SCR_TODAY, SCR_WEATHER, SCR_WATCH };
int gScreen = SCR_MAIN;
unsigned long gLastDraw = 0;
uint32_t gLastSig = 0;          // отпечаток данных, по нему решаем, нужна ли перерисовка
unsigned long gLastWinFull = 0; // когда окно перерисовывали целиком (для графика)
unsigned long gTouchMs = 0;     // когда был последний тап (облако не лезет в этот момент)
bool gNoClear = false;          // не заливать весь экран — частичное обновление
bool gSkipChart = false;        // не перерисовывать график 24 ч (точечное обновление)
bool gDemoToday = false;        // DEMO: ИТОГИ с широкими числами (проверка вёрстки)
unsigned long gTouchSkip = 0;   // до этого времени не начинать сетевые операции

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
unsigned long prevTimeVal = 0; // когда было предыдущее показание
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

  // Сброс счётчика (замена реле, обнуление): иначе «сегодня» застрянет на нуле
  // навсегда, потому что prevEnergyVal не даст накопить заново.
  if (prevEnergyVal >= 0 && gEnergy + 0.05f < prevEnergyVal) {
    Serial.printf("счётчик уменьшился %.2f -> %.2f: сбрасываю базу\n", prevEnergyVal, gEnergy);
    dayStartKwh = gEnergy;
    prevEnergyVal = gEnergy;
    myTodayKwh = 0;
    myTodayCost = 0;
    saveToday();
    return;
  }

  // Прирост счётчика. Большую дельту НЕ выбрасываем (раньше так терялась
  // энергия многoчасовой дыры), а начисляем по среднему тарифу интервала.
  if (prevEnergyVal < 0) {
    prevEnergyVal = gEnergy;
  } else if (gEnergy > prevEnergyVal) {
    float d = gEnergy - prevEnergyVal;
    if (d < 5.0f) {                       // до 5 кВт·ч за один опрос — правдоподобно
      float tariff = (d > 0.5f) ? tariffAvg(prevTimeVal, millis() / 1000UL)
                                : tariffForHour(ti.tm_hour);
      myTodayKwh += d;
      myTodayCost += d * tariff;
      if (ti.tm_hour >= 23 || ti.tm_hour < 6) statNightKwh += d;
      else                                   statDayKwh += d;
      prevEnergyVal = gEnergy;
    }
  }
  prevTimeVal = millis() / 1000UL;

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
float gCurrent = 0;           // реальный ток из phase_a (байты 2..4, мА), НЕ P/V
// Возраст данных нужен, чтобы потребитель отличил свежие 239 В от 239 В часовой давности
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
#define SD_REC_MAX 3600          // 28 КБ. Сутки = 2880 строк; 2048 обрезало день
                                 // и занижало «вчера» на 45 % (регрессия 28.10)
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
  // Один sscanf вместо шести substring+toInt: в сутках таких строк ~2900,
  // и на каждой раньше создавалось по шесть String (лишняя работа и heap).
  int y = 0, mo = 0, d = 0, h = 0, mi = 0, se = 0;
  if (sscanf(line.c_str(), "%4d-%2d-%2d %2d:%2d:%2d",
             &y, &mo, &d, &h, &mi, &se) != 6) return 0;
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
    if (!sRec) { Serial.println("карта: нет памяти под буфер истории"); return false; }
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
  int rd = 0;
  while (f.available()) {
    String line = f.readStringUntil('\n');
    if ((++rd & 255) == 0) esp_task_wdt_reset();      // WDT: файл длинный
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
    if (sRecN >= SD_REC_MAX) {
      // Переполнение: выбрасываем самую старую половину, новые строки важнее.
      // Иначе терялся конец окна и «вчера/сегодня» считались по обрезанным данным.
      int half = SD_REC_MAX / 2;
      memmove(sRec, sRec + half, sizeof(SdRec) * half);
      sRecN = half;
    }
    sRec[sRecN].t = (uint32_t)t;
    sRec[sRecN].e = e;
    sRecN++;
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
    // Делим ЛЮБОЙ разрыв пропорционально времени: иначе энергия
    // многочасового пропуска (ночь) просто исчезала из расчёта.
    if (gap > 0 && b.e >= a.e)
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
      if (gap > 0 && b.e >= a.e)
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
  int rd = 0;
  while (f.available()) {
    String line = f.readStringUntil('\n');
    if ((++rd & 255) == 0) esp_task_wdt_reset();      // WDT: сутки — это ~2900 строк
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
    // Строка, восстановленная из облака, помечена нулевым счётчиком (3-е поле):
    // ватты и реле в ней тоже нули. В статистику мощности такие строки не
    // берём — иначе средняя мощность за день занижается.
    bool restored = (line.substring(p3 + 1, p4).toFloat() <= 0);
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
    if (p >= 0 && !restored) {
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
  pinMode(TP_IRQ, INPUT);        // у GPIO34-39 нет внутренних подтяжек — PULLUP бесполезен
  digitalWrite(TP_CS, HIGH); digitalWrite(TP_CLK, LOW);
}
bool tpDown() { return digitalRead(TP_IRQ) == LOW; }

// Один замер: 5 отсчётов за 15 мс, медиана. Первая пара после смены канала
// (0x90 <-> 0xD0) отбрасывается — АЦП тачскрина после переключения «звонит».
static uint16_t tpMedian5(uint16_t* a, int n) {
  // сортировка вставками — n=5, дешевле любой библиотечной
  for (int i = 1; i < n; i++) {
    uint16_t x = a[i]; int j = i - 1;
    while (j >= 0 && a[j] > x) { a[j + 1] = a[j]; j--; }
    a[j + 1] = x;
  }
  return a[n / 2];
}

bool tpPoint(int* sx, int* sy) {
  uint16_t ax[5], ay[5];
  int n = 0;
  tpRaw(0x90); tpRaw(0xD0);              // первый отсчёт — в мусор
  unsigned long t0 = millis();
  while (n < 5 && millis() - t0 < 15UL) {
    ax[n] = tpRaw(0x90);
    ay[n] = tpRaw(0xD0);
    n++;
    if (!tpDown()) break;
  }
  if (n == 0) return false;
  long rx = tpMedian5(ay, n), ry = tpMedian5(ax, n);   // 0xD0 — X, 0x90 — Y
  if (rx < 100 || rx > 4050 || ry < 100 || ry > 4050) return false;
  *sx = constrain((int)map(rx, calYleft, calYright, 0, 239), 0, 239);
  *sy = constrain((int)map(ry, calXtop, calXbottom, 0, 319), 0, 319);
  return true;
}


// ---------------------------------------------------------------- время без интернета
// ESP32 не имеет батарейной памяти часов. Если интернета при включении нет,
// NTP молчит, getLocalTime() возвращает false — и на карту НИЧЕГО не пишется.
// Поэтому храним последнее известное время в NVS и восстанавливаем его.
void saveClock() {
  struct tm ti;
  if (!getLocalTime(&ti, 20)) return;
  time_t now = mktime(&ti);
  if (now < 1600000000) return;
  Preferences p;
  p.begin("clock", false);
  p.putLong("epoch", (long)now);
  p.end();
}

bool restoreClock() {
  Preferences p;
  p.begin("clock", true);
  long e = p.getLong("epoch", 0);
  p.end();
  if (e < 1600000000) return false;

  // Считаем, что с момента сохранения прошло немного: точнее всё равно не узнать
  // без сети. Даже с ошибкой в минуты строка попадёт в правильный час.
  e += 60;
  struct timeval tv = { (time_t)e, 0 };
  settimeofday(&tv, NULL);
  struct tm ti;
  if (getLocalTime(&ti, 50)) {
    Serial.printf("время: восстановлено из памяти (%04d-%02d-%02d %02d:%02d), NTP не ответил\n",
                  ti.tm_year + 1900, ti.tm_mon + 1, ti.tm_mday, ti.tm_hour, ti.tm_min);
    return true;
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


// ---------------------------------------------------------------- интернет
// Плата живёт в ЛОКАЛЬНОЙ сети. Всё, что требует интернета (облако Tuya,
// погода), спрашиваем только если он реально есть: иначе каждый такой запрос
// висит по несколько секунд и тач «замирает». Локальные вещи (реле по 6668,
// карта, сторож устройств) проверки не требуют и работают всегда.
bool     gNetForceOff = false;    // принудительно «без интернета» (команда NETOFF)
bool     gWifiOff     = false;    // радио выключено вручную (команда WIFIOFF)
bool     gNetOk       = false;    // результат последней проверки
unsigned long gNetProbeMs = 0;

#define NET_PROBE_MS 350          // таймаут одного подключения, мс

bool netProbe() {
  if (gNetForceOff || gWifiOff || WiFi.status() != WL_CONNECTED) return false;
  // Проверяем TCP до публичного DNS: дешевле и быстрее HTTP.
  // 8.8.8.8 первым не ставим — в РФ он часто недоступен;
  // 77.88.8.8 (Яндекс DNS) доступен всегда, когда интернет есть.
  const uint8_t ips[2][4] = { { 77, 88, 8, 8 }, { 1, 1, 1, 1 } };
  for (int i = 0; i < 2; i++) {
    IPAddress ip(ips[i][0], ips[i][1], ips[i][2], ips[i][3]);
    WiFiClient c;
    bool ok = (c.connect(ip, 53, NET_PROBE_MS) == 1);
    c.stop();
    if (ok) return true;
  }
  return false;
}

// true — интернет есть. Результат кэшируется на 30 с, чтобы не тратить время.
bool netOnline() {
  if (gNetForceOff || gWifiOff) { gNetOk = false; return false; }
  if (WiFi.status() != WL_CONNECTED) { gNetOk = false; return false; }
  if (gNetProbeMs != 0 && millis() - gNetProbeMs < 30000UL) return gNetOk;
  gNetProbeMs = millis();
  bool was = gNetOk;
  gNetOk = netProbe();
  if (was != gNetOk)
    Serial.printf("интернет: %s\n", gNetOk ? "есть" : "НЕТ — работаем локально");
  return gNetOk;
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

  uint16_t w0 = ((uint16_t)raw[0] << 8) | raw[1];       // напряжение ×10
  uint32_t amps = ((uint32_t)raw[2] << 16) | ((uint32_t)raw[3] << 8) | raw[4];  // ток, мА (24 бита)
  uint16_t w3 = ((uint16_t)raw[6] << 8) | raw[7];       // мощность, Вт

  float v   = w0 / 10.0f;
  float a   = amps / 1000.0f;
  float pwr = (float)w3;                                 // мощность — как есть

  // отсекаем мусор
  bool got = false;
  if (v > 80 && v < 400) { gVoltage = v; got = true; }
  if (pwr >= 0 && pwr < 20000) { gPower = pwr; got = true; }
  if (a >= 0 && a < 100) gCurrent = a;      // реальный ток, а не P/V
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
// maxRaw — предел размера ответа (история за сутки бывает заметно длиннее).
bool cloudCall(const String& method, const String& path, String& body, int maxRaw = 3500) {
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
  if (!client.connect(TUYA_HOST, 443, 2000)) return false;   // 2 с — и хватит

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
  while (client.connected() && !client.available() && millis() - t0 < 2000) {
    esp_task_wdt_reset();
    delay(20);
  }

  String raw;
  raw.reserve(maxRaw + 64);
  unsigned long t1 = millis();
  while (millis() - t1 < 2500UL) {
    if (client.available()) {
      raw += (char)client.read();
      t1 = millis();
      if ((int)raw.length() > maxRaw) break;
    } else if (!client.connected()) {
      break;
    } else if (millis() - t1 > 400UL) {
      break;                          // 400 мс тишины — ответ закончился
    }
    esp_task_wdt_reset();
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
  if (!netOnline()) return false;          // облако — только при живом интернете
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
      uint32_t ampsCA = ((uint32_t)raw[2] << 16) | ((uint32_t)raw[3] << 8) | raw[4];
      float ampCA = ampsCA / 1000.0f;
      if (volt > 80 && volt < 400) { gVoltage = volt; gPhaseMs = millis(); }
      if (pwr >= 0 && pwr < 20000) gPower = pwr;
      if (ampCA >= 0 && ampCA < 100) gCurrent = ampCA;   // реальный ток из облака
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



// ---------------------------------------------------------------- база месяца из облака
// Облако хранит историю не с начала месяца, а с какого-то дня (у нас — с 23.10).
// Поэтому «показание на 1-е число» не существует. Ищем БИНАРНЫМ ПОИСКОМ первый
// день месяца, за который в облаке есть записи, и берём минимальное значение —
// счётчик только растёт, значит минимум и есть самое раннее показание.
// Запрос истории за окно. size=5 — ответ маленький и быстрый, не упирается
// в лимит чтения cloudCall (3.5 КБ) и в таймаут.
// Возвращает: 1 — есть записи, 0 — пусто, -1 — ошибка запроса.
int cloudProbe(int year, int mon, int day, int h0, int h1, float* minVal) {
  struct tm t0;
  memset(&t0, 0, sizeof(t0));
  t0.tm_year = year - 1900; t0.tm_mon = mon - 1; t0.tm_mday = day;
  t0.tm_hour = h0; t0.tm_min = 0; t0.tm_sec = 0; t0.tm_isdst = -1;
  time_t a = mktime(&t0);
  t0.tm_hour = h1;
  time_t b = mktime(&t0);
  if (a < 1600000000 || b <= a) return -1;

  char path[220];
  snprintf(path, sizeof(path),
           "/v1.0/devices/%s/report-logs?codes=total_forward_energy"
           "&end_time=%llu&size=30&start_time=%llu",
           DEVICE_ID, (unsigned long long)b * 1000ULL, (unsigned long long)a * 1000ULL);

  String body;
  if (!cloudCall("GET", String(path), body)) return -1;
  if (body.indexOf("success") < 0) return -1;
  if (body.indexOf("value") < 0) return 0;

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
    while (j < (int)body.length() && (isDigit(body[j]) || body[j] == '.')) j++;
    if (j > i) {
      float v = body.substring(i, j).toFloat() / 100.0f;
      if (v > 0 && (best < 0 || v < best)) best = v;
    }
    pos = j > i ? j : i + 1;
  }
  if (best <= 0) return 0;
  if (minVal) *minVal = best;
  return 1;
}

// Показание счётчика в САМОЕ РАННЕЕ время, за которое облако хранит историю.
// Облако не хранит начало месяца (у нас — только с 23.10 06:00), поэтому
// ищем двумя бинарными поисками: сначала день, потом час внутри дня.
bool cloudMonthBase(int year, int mon, float* outKwh) {
  if (!cloudToken()) return false;

  struct tm ti;
  if (!getLocalTime(&ti, 10)) return false;
  int lastDay = ti.tm_mday;

  float v;
  // --- день: пробуем полдень каждого дня (маленький ответ, быстро)
  int lo = 1, hi = lastDay, found = -1;
  while (lo <= hi) {
    int mid = (lo + hi) / 2;
    int r = cloudProbe(year, mon, mid, 12, 13, &v);
    esp_task_wdt_reset();
    if (r == 1) { found = mid; hi = mid - 1; }     // есть данные — ищем раньше
    else if (r == 0) { lo = mid + 1; }             // пусто — позже
    else { hi = mid - 1; }                          // ошибка — пробуем раньше
  }
  if (found < 0) return false;

  // --- час внутри найденного дня
  int hFound = -1;
  for (int h = 0; h < 24 && hFound < 0; h += 2) {
    int r = cloudProbe(year, mon, found, h, h + 2, &v);
    esp_task_wdt_reset();
    if (r == 1) hFound = h;
  }
  if (hFound < 0) hFound = 0;

  int r = cloudProbe(year, mon, found, hFound, hFound + 2, &v);
  if (r != 1) return false;

  *outKwh = v;
  Serial.printf("месяц: облако знает с %d.%02d %02d:00, база %.2f кВт·ч\n",
                found, mon, hFound, v);
  return true;
}

// ---------------------------------------------------------------- база на полночь из облака
// Берём историю счётчика за 6 часов до полуночи и МАКСИМУМ — это показание
// ровно в 00:00. Счётчик только растёт, порядок записей в ответе не важен.
// Параметры в подписи идут строго по алфавиту и без URL-кодирования.
bool cloudDayStart(int year, int mon, int day, float* outKwh) {
  if (WiFi.status() != WL_CONNECTED) return false;
  if (!netOnline()) return false;          // без интернета не зовём
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

// ---------------------------------------------------------------- история из облака
// НЕОБЯЗАТЕЛЬНОЕ дополнение к своей истории на карте. Если интернет есть и за
// сегодня в /energy.csv не хватает часов (был обрыв, плата стояла), их можно
// дозаполнить напряжением из облака Tuya.
// Работает шагами: один запрос за проход loop(), не больше 100 записей и
// ~10 с на запрос; в момент касания экрана запрос не начинается. Без
// интернета не запускается вообще — история пишется локально и без него.
#define HIST_MAX_REC   100          // записей в одном ответе (больше облако не даёт)

struct HistSlot { uint8_t n; float sumV; unsigned long ts;
                  uint8_t nP; float sumP;      // мощность (phase_a, байты 6-7)
                  uint8_t nT; float sumT; };   // температура (temp_current /10)
static HistSlot hSlot[24];
static uint8_t  hHave[24];          // часы, которые в CSV уже есть
static uint8_t  hList[24];          // часы, которых в CSV нет (их и спрашиваем)
static int      hListN = 0, hListI = 0;
static String   hBody;              // тело последнего ответа облака

long  histDayDone = -2;             // день, за который уже восстанавливали
bool  gHistWant = false;            // нужно восстановление
bool  gHistForce = false;           // команда CSVHIST — принудительно
int   gHistStep = 0;                // автомат: 0 — простой
int   gHistAdded = 0;               // сколько часов дописано
unsigned long gHistRetry = 0;       // пауза после неудачи

void loadHistDay() {
  Preferences p; p.begin("hist", true);
  histDayDone = p.getLong("day", -2);
  p.end();
}
void saveHistDay(long d) {
  Preferences p; p.begin("hist", false);
  p.putLong("day", d);
  p.end();
  histDayDone = d;
}

// Один шаг автомата. Вызывается из loop() каждый проход и сразу выходит,
// если восстанавливать нечего или интернета нет.
void histTick() {
  // касание в любой момент — откладываем любой шаг истории
  if (millis() < gTouchSkip) return;
  // ---- шаг 0: условия и «надо ли»
  if (gHistStep == 0) {
    struct tm ti;
    if (!getLocalTime(&ti, 5)) return;
    long todayKey = daysFromCivil(ti.tm_year + 1900, ti.tm_mon + 1, ti.tm_mday);
    if (todayKey <= 0) return;
    if (histDayDone != todayKey) gHistWant = true;     // новый день — снова надо
    if (!gHistWant) return;
    if (!gSdOk) { gHistWant = false; return; }
    if (gHistRetry != 0 && millis() < gHistRetry) return;
    if (!netOnline()) return;                          // нет интернета — не сейчас
    if (millis() - gTouchMs < 1500UL) return;          // человек работает с экраном
    if (millis() < gTouchSkip) return;
    memset(hSlot, 0, sizeof(hSlot));
    memset(hHave, 0, sizeof(hHave));
    hListN = 0; hListI = 0;
    gHistAdded = 0;
    gHistStep = 1;
    Serial.println("история: проверяю пропущенные часы за сегодня");
    return;
  }

  // ---- шаг 1: токен
  if (gHistStep == 1) {
    if (!cloudToken()) {
      Serial.println("история: облако не ответило — повтор позже");
      gHistRetry = millis() + 600000UL;
      gHistStep = 0;
      gHistWant = false;
      return;
    }
    gHistStep = 4;               // сначала смотрим, каких часов нет в карте
    return;
  }

  // ---- шаг 2: ОДИН недостающий час за проход, не больше 100 записей
  if (gHistStep == 2) {
    if (millis() - gTouchMs < 1500UL) return;          // не мешаем тачу
    if (hListI >= hListN) { gHistStep = 5; return; }
    struct tm ti;
    if (!getLocalTime(&ti, 5)) { gHistStep = 0; return; }
    int hh = hList[hListI];

    // Границы часа — по местному времени (как в CSV), в запрос — UTC-мс.
    struct tm t0;
    memset(&t0, 0, sizeof(t0));
    t0.tm_year = ti.tm_year; t0.tm_mon = ti.tm_mon; t0.tm_mday = ti.tm_mday;
    t0.tm_hour = hh; t0.tm_min = 0; t0.tm_sec = 0; t0.tm_isdst = -1;
    time_t hs = mktime(&t0);
    if (hs < 1600000000) { hListI++; return; }
    unsigned long long st = (unsigned long long)hs * 1000ULL;
    unsigned long long en = st + 3600000ULL;

    // параметры — строго по алфавиту и без URL-кодирования
    char path[240];
    snprintf(path, sizeof(path),
             "/v1.0/devices/%s/report-logs?codes=phase_a,temp_current&end_time=%llu&size=%d&start_time=%llu",
             DEVICE_ID, en, HIST_MAX_REC, st);

    hBody = "";
    if (!cloudCall("GET", String(path), hBody, 12000) ||
        hBody.indexOf("\"success\":true") < 0) {
      Serial.println("история: запрос не удался — повтор позже");
      gTuyaToken = "";                                 // токен мог протухнуть
      gHistRetry = millis() + 600000UL;
      gHistStep = 0;
      gHistWant = false;
      return;
    }

    // разбираем: value — base64, первые 2 байта = напряжение × 10,
    // event_time — время записи (мс UTC).
    int len = hBody.length();
    int pos = 0, nrec = 0;
    while (nrec < HIST_MAX_REC) {
      int i = hBody.indexOf("\"event_time\":", pos);
      if (i < 0) break;
      i += 13;
      while (i < len && (hBody[i] == ' ' || hBody[i] == '"')) i++;
      int j = i;
      while (j < len && isDigit(hBody[j])) j++;
      if (j == i) { pos = i + 1; continue; }
      unsigned long long ms = strtoull(hBody.substring(i, j).c_str(), NULL, 10);
      pos = j;
      int v = hBody.indexOf("\"value\":", pos);
      if (v < 0) break;
      v += 8;
      while (v < len && hBody[v] == ' ') v++;
      if (v < len && hBody[v] == '"') v++;
      int e = hBody.indexOf('"', v);
      if (e < 0) break;
      String b64 = hBody.substring(v, e);
      pos = e + 1;

      time_t et = (time_t)(ms / 1000ULL);
      if (et < hs || et >= hs + 3600) continue;         // не наш час

      struct tm lt;
      localtime_r(&et, &lt);

      // Температура приходит просто числом («214»), напряжение — base64
      // («CUcAAJYAABM=»). Различаем по содержимому: только цифры = температура.
      bool digitsOnly = b64.length() > 0;
      for (int k = 0; k < (int)b64.length(); k++)
        if (!isDigit(b64[k])) { digitsOnly = false; break; }

      if (digitsOnly) {
        float t = b64.toFloat() / 10.0f;
        if (t > -40 && t < 125) {
          hSlot[hh].sumT += t;
          hSlot[hh].nT++;
        }
      } else {
        uint8_t rawb[12];
        if (b64decode(b64.c_str(), rawb, sizeof(rawb)) < 8) continue;
        float volt = (float)(((uint16_t)rawb[0] << 8) | rawb[1]) / 10.0f;
        float pwr  = (float)(((uint16_t)rawb[6] << 8) | rawb[7]);
        if (volt < 80 || volt > 400) continue;            // мусор/нет фазы
        hSlot[hh].sumV += volt;
        hSlot[hh].n++;
        if (pwr >= 0 && pwr < 20000) {
          hSlot[hh].sumP += pwr;
          hSlot[hh].nP++;
        }
      }
      hSlot[hh].ts = (unsigned long)(daysFromCivil(lt.tm_year + 1900, lt.tm_mon + 1,
                                                  lt.tm_mday) * 86400L
                                     + lt.tm_hour * 3600L + lt.tm_min * 60L + lt.tm_sec);
      nrec++;
    }
    Serial.printf("история: час %02d — записей %d, ответ %d байт\n", hh, nrec, len);
    hListI++;
    if (hListI >= hListN) gHistStep = 5;
    return;
  }

  // ---- шаг 4: какие часы сегодня уже есть в CSV
  if (gHistStep == 4) {
    memset(hHave, 0, sizeof(hHave));
    struct tm ti;
    if (!getLocalTime(&ti, 5)) { gHistStep = 0; return; }
    char dayPrefix[12];
    snprintf(dayPrefix, sizeof(dayPrefix), "%04d-%02d-%02d",
             ti.tm_year + 1900, ti.tm_mon + 1, ti.tm_mday);
    File f = SD.open("/energy.csv", FILE_READ);
    if (!f) { gHistStep = 0; return; }
    size_t off = findDayStartOffset(f, dayPrefix);
    if (off > 0 || fileTailIsToday(f, dayPrefix)) {
      f.seek(off);
      while (f.available()) {
        long t = lineEpoch(f.readStringUntil('\n'));
        if (!t) continue;
        hHave[(int)((t % 86400L) / 3600)] = 1;
      }
    }                                              // иначе сегодняшних строк нет
    f.close();

    // Список часов, которых нет в карте (текущий час не трогаем — его пишем
    // вживую каждые 30 с). Именно их и спрашиваем по одному.
    hListN = 0; hListI = 0;
    int haveN = 0;
    for (int h = 0; h < ti.tm_hour && h < 24; h++) {
      if (hHave[h]) { haveN++; continue; }
      hList[hListN++] = (uint8_t)h;
    }
    Serial.printf("история: часов в карте %d, пустых %d\n", haveN, hListN);
    if (hListN == 0) { gHistStep = 6; return; }
    gHistStep = 2;
    return;
  }

  // ---- шаг 5: дописываем недостающие часы
  if (gHistStep == 5) {
    struct tm ti;
    if (!getLocalTime(&ti, 5)) { gHistStep = 0; return; }
    char dayPrefix[12];
    snprintf(dayPrefix, sizeof(dayPrefix), "%04d-%02d-%02d",
             ti.tm_year + 1900, ti.tm_mon + 1, ti.tm_mday);
    int nowHour = ti.tm_hour;
    int fill[24], nf = 0, empty = 0;
    for (int h = 0; h < nowHour; h++) {
      if (hHave[h]) continue;
      if (hSlot[h].n == 0) { empty++; continue; }   // в облаке этого часа тоже нет
      fill[nf++] = h;
    }
    if (nf > 0) {
      File f = SD.open("/energy.csv", FILE_APPEND);
      if (f) {
        for (int k = 0; k < nf; k++) {
          int h = fill[k];
          float v = hSlot[h].sumV / (float)hSlot[h].n;              // среднее по часу
          float pw = (hSlot[h].nP > 0) ? hSlot[h].sumP / (float)hSlot[h].nP : 0;
          float tp = (hSlot[h].nT > 0) ? hSlot[h].sumT / (float)hSlot[h].nT : 0;
          unsigned long t = hSlot[h].ts;
          // кВт·ч и реле — нули: счётчик в этих строках не используется,
          // чтобы не задвоить энергию. Видно, что запись восстановленная.
          if (hSlot[h].nT > 0)
            f.printf("%s %02d:%02d:%02d,%.0f,%.0f,0,0,0.00,%.1f\n",
                     dayPrefix, (int)((t % 86400UL) / 3600),
                     (int)((t % 3600UL) / 60), (int)(t % 60), v, pw, tp);
          else
            f.printf("%s %02d:%02d:%02d,%.0f,%.0f,0,0,0.00,\n",
                     dayPrefix, (int)((t % 86400UL) / 3600),
                     (int)((t % 3600UL) / 60), (int)(t % 60), v, pw);
          gSdRows++;
          Serial.printf("история: +%02d:00 %.0f В %.0f Вт %.1f C (точек V%d P%d T%d)\n",
                        h, v, pw, tp, hSlot[h].n, hSlot[h].nP, hSlot[h].nT);
        }
        f.close();
        gHistAdded = nf;
      }
    }
    Serial.printf("история: пустых часов %d, в облаке нашлось %d, дописано %d\n",
                  nf + empty, nf, (int)gHistAdded);
    gHistStep = 6;
    return;
  }

  // ---- шаг 6: пересобрать суточную статистику с учётом восстановленных часов
  if (gHistStep == 6) {
    if (gHistAdded > 0) {
      resetStats();
      statsSeeded = false;
      seedStatsFromSd();          // мин/макс/среднее/график — заново по карте
      gLastDraw = 0;
    }
    struct tm ti;
    if (getLocalTime(&ti, 5))
      saveHistDay(daysFromCivil(ti.tm_year + 1900, ti.tm_mon + 1, ti.tm_mday));
    gHistWant = false;
    gHistForce = false;
    gHistStep = 0;
    Serial.printf("история: готово, дописано часов %d\n", gHistAdded);
    return;
  }
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
// Средний тариф для оценки периода (месяц/вчера).
// ВАЖНО: сегодняшний сплит день/ночь на роль «тарифа периода» не годится —
// ранним утром он почти целиком ночной (5.09) и занижает месяц на 36 %.
// Поэтому берём накопленный сплит, а если его мало — типовую оценку.
float measuredTariff() {
  float tot = statDayKwh + statNightKwh;
  if (tot > 0.5f)                       // меньше 0.5 кВт·ч — статистики не хватает
    return (statDayKwh * TARIFF_DAY + statNightKwh * TARIFF_NIGHT) / tot;
  return avgTariffEstimate();           // 7.25 — безопасная середина
}

// Тариф для оценки ДЛИННОГО периода (месяц): сегодняшний сплит не подходит
// в принципе, поэтому всегда типовая оценка по суточному профилю.
float periodTariff() {
  // Для длинного периода важен не суточный профиль ПО ВРЕМЕНИ (7 ч ночи из 24),
  // а профиль ПО ПОТРЕБЛЕНИЮ: почти вся энергия тратится днём.
  // По фактической истории (день 7.02 / ночь 0.77 кВт·ч) доля дня = 90 %.
  return 0.90f * TARIFF_DAY + 0.10f * TARIFF_NIGHT;   // ≈ 8.00
}

void updateMonth(struct tm& ti) {
  int stamp = (ti.tm_year + 1900) * 12 + ti.tm_mon;

  if (mMonthStamp != stamp || mBaseKwh < 0) {
    // База месяца — из СВОЕЙ истории на карте (sdMonthBase). Облако — только
    // запасной вариант, если на карте этого месяца ещё нет (первый запуск).
    // База месяца: у облака история ГЛУБЖЕ, чем у карты (карта началась 27.10,
    // облако знает с 23.10). Поэтому сначала облако, карта — запасной вариант.
    float base = -1;
    float cloudBase = -1;
    if (WiFi.status() == WL_CONNECTED && netOnline())
      cloudMonthBase(ti.tm_year + 1900, ti.tm_mon + 1, &cloudBase);
    float sdBase = -1;
    bool sdOk = sdMonthBase(ti.tm_year + 1900, ti.tm_mon + 1, &sdBase);
    if (cloudBase > 0 && sdOk) base = (cloudBase < sdBase) ? cloudBase : sdBase;  // меньшая = раньше
    else if (cloudBase > 0)    base = cloudBase;
    else if (sdOk)             base = sdBase;
    if (base < 0) {
      if (gEnergy <= 0) return;      // счётчик ещё не прочитан — попробуем позже
      base = gEnergy;
    }

    mBaseKwh = base;
    mMonthStamp = stamp;
    mKwh = (gEnergy > base) ? gEnergy - base : 0;
    mCost = mKwh * periodTariff();      // месяц — по тарифу периода, не сегодняшнему
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
  if (diff >= 0 && diff + 0.05f < mKwh) {
    // Счётчик сбросили (замена реле): уменьшаем и расход, и стоимость
    // пропорционально, иначе завышенная стоимость висит до конца месяца.
    float k = (mKwh > 0.01f) ? diff / mKwh : 0;
    mCost *= k;
    mKwh = diff;
    savePeriods();
  }   // счётчик сбросили

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
  if (!netOnline()) return;              // без интернета считаем только по карте
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
  int got = readFrame(buf, sizeof(buf), 1500);
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
    if (!sock.connect(RELAY_IP, RELAY_PORT, 2000)) return false;
    sessionReady = false;
  }
  if (!sessionReady && !negotiate()) { sock.stop(); return false; }

  uint8_t buf[512], payload[256];
  bool gotDps = false;
  bool gotPhase = false;

  // DP 6 (напряжение и мощность) приходит не на каждый запрос, поэтому раньше
  // делали три попытки с таймаутом 5 с — это 23 с блокировки цикла. Теперь
  // одна попытка и короткий таймаут: не пришло — спросим в следующем проходе.
  for (int attempt = 0; attempt < 1; attempt++) {
    drainSocket(80);

    seqNo++;
    const char* req = (attempt == 0)
                      ? "{}"
                      : "{}";
    size_t n = buildFrame(CMD_DP_QUERY, (const uint8_t*)req, strlen(req), sessionKey, buf);
    sock.write(buf, n);

    int got = readFrame(buf, sizeof(buf), 1200);
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
bool httpGet(const String& url, String& out, int timeout = 2500) {
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

  // ВНИМАНИЕ: напряжение и мощность отсюда НЕ берём.
  // У этого реле локальный DP 6 отдаёт УСТАРЕВШЕЕ значение: проверено
  // одновременно — плата 19 Вт против 145 Вт в облаке при той же минуте.
  // Источник истины: облачный phase_a (свежий каждые ~16 с),
  // а локальный DP 6 — только запасной вариант с пометкой возраста.
  float t = num("temperature");
  if (t > -900 && t > -40 && t < 125) { gTemp = t; gTempValid = true; }
  float ek = num("energy_kwh"); if (ek > 0) gEnergy = ek;

  // Второй запрос (/stats) — не чаще раза в 5 минут: он нужен только для
  // сверки «сегодня» с компьютером, а висит столько же, сколько /relay.
  static unsigned long lastStats = 0;
  String st;
  if ((lastStats == 0 || millis() - lastStats > 300000UL) &&
      httpGet(String(PC_API) + "/stats", st)) {
    lastStats = millis();
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
  // Погода — из интернета. Нет интернета — просто «НЕТ ДАННЫХ» на экране,
  // ничего не ждём и не блокируем.
  if (!netOnline()) return false;
  String j;
  if (!httpGet(WEATHER_URL, j, 5000)) return false;
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
    while (f.available()) {
      String l = f.readStringUntil('\n');
      if (l.length() > 10) gSdRows++;
      if ((gSdRows & 255) == 0) esp_task_wdt_reset();   // файл на 5-10 тыс. строк
    }
    f.close();
  } else {
    File n = SD.open("/energy.csv", FILE_WRITE);
    if (n) { n.println("datetime,voltage,power,energy_kwh,relay,cost,temp"); n.close(); }
  }
  return true;
}

// История на карту пишется ВСЕГДА — и с интернетом, и без него, и даже без
// Wi-Fi: напряжение/мощность приходят от реле по локальной сети, карта от
// интернета не зависит. Раньше вызов стоял только в ветке «Wi-Fi подключён»,
// из-за этого при обрыве канала в /energy.csv появлялись дыры.
void writeSd() {
  if (!gSdOk) return;
  if (millis() - gSdLast < 30000) return;        // не чаще раза в 30 с
  // Нет напряжения — строку не пишем вообще. Раньше при gEnergy>0 писалась
  // строка с v=0, и она ломала мин/среднее (23-26.10 таких было 100 %).
  // Лучше честный пропуск, чем нуль в истории.
  if (gVoltage < 50) return;
  if (gEnergy <= 0) return;
  // Без времени строка бесполезна: её потом не разобрать по часам.
  struct tm ti;
  if (!getLocalTime(&ti, 100)) return;

  char stamp[24];
  snprintf(stamp, sizeof(stamp), "%04d-%02d-%02d %02d:%02d:%02d",
           ti.tm_year + 1900, ti.tm_mon + 1, ti.tm_mday, ti.tm_hour, ti.tm_min, ti.tm_sec);

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
  gSdLast = millis();      // отсчёт 30 с — от реальной записи, а не от попытки
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
const int TAP_SLOP = 4;          // допуск попадания по тачу, px

struct Block { int x, y, w, h, scr; };
const Block BLOCKS[5] = {
  {  6,  50, 228, 64, SCR_VOLT    },
  {  6, 118, 110, 56, SCR_TEMP    },
  // x=118, а не 120: щель между карточками 116..119 больше не «мёртвая».
  // Раньше в ней всегда выигрывала ТЕМПЕРАТУРА, и МОЩНОСТЬ «не нажималась».
  {118, 118, 116, 56, SCR_POWER   },
  {  6, 178, 228, 66, SCR_TODAY   },
  {  6, 248, 228, 62, SCR_WEATHER },
};
// Экран сторожа оставлен в коде, но недоступен: вернём, когда будет
// большой экран. Пока вместо него на нижней полосе сеть/карта/сигнал.

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

// Нижняя полоса — она же КНОПКА сторожа: нажал → открылось окно со списком.
#define WD_N_HOSTS 4   // столько же, сколько в WD_HOSTS

// Нижняя полоса: сеть, карта, сигнал. Сторож сюда вернётся позже,
// когда будет большой экран — сейчас его интерфейс скрыт (см. WD_UI).
#define WD_UI 0

void drawStatusBar() {
  uiFill(0, 311, 240, 9, cSurf);
  bool net = (WiFi.status() == WL_CONNECTED);
  uiCircle(8, 315, 3, net ? cGood : cBadC);
  drawTxt(14, 312, net ? "СЕТЬ" : "НЕТ WI-FI", cMuted, cSurf, 1);

  char b[24];
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
// ICMP и не требует raw-сокетов.
// ПОЛНЫЙ КРУГ — КАЖДЫЕ 10 СЕКУНД (было 30). Один адрес за проход, чтобы тач
// не страдал; таймаут одного подключения 350 мс, попыток две.
// Тревога — после двух кругов подряд (≈10 с): при переподключении Wi-Fi
// устройство может не принять TCP с первого раза, и одиночный промах не
// должен поднимать плашку. Реальный обрыв видно через 10 с, а не через 60.
struct WdHost { const char* name; uint8_t ip[4]; uint16_t port; };

#define WD_CYCLE_MS 10000UL    // полный круг опроса — каждые 10 секунд
#define WD_HOST_MS  2500UL     // один адрес раз в 2.5 с (4 адреса = круг 10 с)
#define WD_CONN_MS  250        // таймаут одного TCP-подключения, мс

const WdHost WD_HOSTS[] = {
  { "РОУТЕР",  { 192, 168, 1, 1  }, 80 },   // роутер, веб-интерфейс
  { "NAS",     { 192, 168, 1, 10 }, 80 },   // накопитель, веб
  { "КАМЕРА",  { 192, 168, 1, 11 }, 80 },   // камера, веб
  { "ПРИНТЕР", { 192, 168, 1, 12 }, 80 },   // принтер, веб
};
const int WD_N = sizeof(WD_HOSTS) / sizeof(WD_HOSTS[0]);

bool     wdAlive[WD_N];        // результат последней проверки по адресу
uint16_t wdMs[WD_N] = {0};     // время отклика, мс (для окна сторожа)
unsigned long gWdLastCycle = 0;  // когда завершился последний полный круг
bool     wdChecked[WD_N];      // адрес уже проверяли хотя бы раз
uint8_t  wdFail[WD_N];         // сколько проверок подряд адрес не отвечал
int      gWdBad = -1;          // первый неотвечающий, -1 — все живы
unsigned long gWdCycle = 0;    // начало текущего круга
unsigned long gWdHost = 0;     // когда проверяли последний адрес
uint8_t  gWdStep = 0;          // какой адрес проверяем следующим

// Одно TCP-подключение с коротким таймаутом (350 мс). Вторая попытка — только
// если первая не удалась: так «моргнувший» Wi-Fi не поднимает ложную тревогу,
// а полный провал виден за ~0.7 с. Возвращает и время проверки в *ms.
bool wdProbe(int i, uint16_t* ms) {
  IPAddress ip(WD_HOSTS[i].ip[0], WD_HOSTS[i].ip[1],
               WD_HOSTS[i].ip[2], WD_HOSTS[i].ip[3]);
  unsigned long t0 = millis();
  for (int attempt = 0; attempt < 2; attempt++) {
    WiFiClient c;
    bool ok = (c.connect(ip, WD_HOSTS[i].port, WD_CONN_MS) == 1);
    c.stop();
    if (ok) { *ms = (uint16_t)(millis() - t0); return true; }
    esp_task_wdt_reset();
  }
  *ms = (uint16_t)(millis() - t0);
  return false;
}

void ledUpdate();               // определена ниже, в разделе светодиода

// Кто не отвечает — пересчитываем СРАЗУ после каждого адреса, не дожидаясь
// конца круга. Тревога — после ДВУХ неудач подряд (10 с): устройства сети
// иногда не принимают TCP в момент переподключения Wi-Fi, и одиночный промах
// не должен поднимать плашку. Изменилось — тут же экран и светодиод.
static void wdSummary() {
  int bad = -1;
  for (int k = 0; k < WD_N; k++)
    if (wdChecked[k] && wdFail[k] >= 2) { bad = k; break; }
  ledUpdate();
  if (bad == gWdBad) return;
  gWdBad = bad;
  gLastDraw = 0;                // сразу перерисовать плашку/светодиод
  if (bad < 0) Serial.println("сторож: все адреса отвечают");
  else         Serial.printf("сторож: %s НЕ ОТВЕЧАЕТ\n", WD_HOSTS[bad].name);
}

// По одному адресу за проход, чтобы не держать тач. Полный круг — раз в 10 с.
void wdTick() {
  // человек только что нажимал — не блокируем цикл опросом сети
  if (millis() < gTouchSkip) return;
  if (gWifiOff || WiFi.status() != WL_CONNECTED) {
    for (int i = 0; i < WD_N; i++) {
      wdAlive[i] = false; wdChecked[i] = false; wdFail[i] = 0;
    }
    if (gWdBad != -1) { gWdBad = -1; gLastDraw = 0; }
    gWdStep = 0;
    gWdCycle = 0;
    gWdHost = 0;
    return;                    // про сеть скажет красный светодиод
  }
  unsigned long now = millis();
  // Новый круг — раз в 10 с. Внутри круга адреса идут по одному раз в 2.5 с:
  // за один проход loop() проверяется максимум один адрес (≤0.5 с), тач не
  // замирает на 2.8 с, как было при четырёх подряд.
  if (gWdCycle == 0 || now - gWdCycle >= WD_CYCLE_MS) {
    gWdCycle = now;
    gWdStep = 0;
  }
  if (gWdHost != 0 && now - gWdHost < WD_HOST_MS) return;
  gWdHost = now;

  int i = gWdStep;
  uint16_t ms = 0;
  bool ok = wdProbe(i, &ms);
  wdMs[i] = ok ? ms : 0;
  wdChecked[i] = true;
  if (ok) { wdFail[i] = 0; wdAlive[i] = true; }
  else {
    if (wdFail[i] < 255) wdFail[i]++;
    wdAlive[i] = false;          // две попытки уже были, ждём второй круг
  }
  gWdStep = (gWdStep + 1) % WD_N;

  wdSummary();
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
  // v4.3: сторож отключён — мигать больше нечем. Зелёный всегда.
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
  // ВАЖНО: подписей ПЯТЬ (00, 06, 12, 18, 24), поэтому цикл до 5, а не до 6.
  // Раньше стояло i < 6 -> lab[5] читался за границей массива, и плата
  // падала с LoadProhibited (EXCVADDR 0xDB) при открытии окна ВОЛЬТЫ.
  const int LAB_N = 5;
  const char* lab[LAB_N] = { "00", "06", "12", "18", "24" };
  for (int i = 0; i < LAB_N; i++) {
    int px = x + (w - 1) * i / (LAB_N - 1);
    if (i == 0)             drawTxt(px,      y + h + 2, lab[i], cMuted, cBg, 1);
    else if (i == LAB_N - 1) drawTxt(px - 11, y + h + 2, lab[i], cMuted, cBg, 1);
    else                    drawTxt(px - 5,  y + h + 2, lab[i], cMuted, cBg, 1);
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
    else uiFill(cx, cy, 2, 2, col);      // одиночная точка (например, час,
                                         // восстановленный из облака) — видна
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

// ---------------------------------------------------------------- числа в колонках
// Компактная запись: под узкую колонку убираем дробную часть, когда число
// большое. 1.91 -> 1.91, 123.45 -> 123.4, 1234.5 -> 1234.
void fmtVal(char* b, size_t n, float v) {
  if (v != v) v = 0;                              // NaN
  float a = fabsf(v);
  if (a >= 1000.0f)     snprintf(b, n, "%.0f", v);
  else if (a >= 100.0f) snprintf(b, n, "%.1f", v);
  else                  snprintf(b, n, "%.2f", v);
}

// Наибольший масштаб шрифта 5x7, при котором строка влезает в maxW px.
int fitScale(const char* s, int maxW, int maxScale) {
  int sc = maxScale;
  while (sc > 1 && txtW(s, sc) > maxW) sc--;
  return sc;
}

// ---------------------------------------------------------------- столбики по часам
// Как в приложении Tuya: 24 столбика, по одному на час. В h24*[288] лежат
// отсчёты каждые 5 минут, значит час h — это индексы h*12 .. h*12+11.
// Границы проверяются всегда: соседний график уже падал с LoadProhibited,
// когда цикл шёл до 6 при массиве из 5.
float hourAverage(const float* vals, uint8_t bit, int h, int* samples) {
  if (samples) *samples = 0;
  if (h < 0 || h > 23) return 0;
  float sum = 0;
  int n = 0;
  for (int s = 0; s < 12; s++) {
    int i = h * 12 + s;
    if (i < 0 || i >= H24_N) continue;
    if (!(h24Mask[i] & bit)) continue;
    float v = vals[i];
    if (v != v) continue;                     // NaN — пропускаем
    sum += v;
    n++;
  }
  if (samples) *samples = n;
  return n ? sum / n : 0;
}

// Заголовок над столбиками: слева «ПО ЧАСАМ», справа текущий час и его
// среднее. Крупное мгновенное значение остаётся сверху окна.
void drawHourTitle(int y, const float* vals, uint8_t bit, const char* unit, int dec) {
  drawTxt(10, y, "ПО ЧАСАМ", cMuted, cBg, 1);
  char b[40];
  struct tm ti;
  int nowH = getLocalTime(&ti, 5) ? ti.tm_hour : -1;
  if (nowH < 0) {
    snprintf(b, sizeof(b), "ЧАС --");
  } else {
    int n = 0;
    float a = hourAverage(vals, bit, nowH, &n);
    if (n == 0)     snprintf(b, sizeof(b), "ЧАС %02d: --", nowH);
    else if (dec)   snprintf(b, sizeof(b), "ЧАС %02d: %.1f %s", nowH, a, unit);
    else            snprintf(b, sizeof(b), "ЧАС %02d: %.0f %s", nowH, a, unit);
  }
  drawTxtR(230, y, b, cMuted, cBg, 1);
}

// 24 столбика: width 8 px, зазор 1 px, шаг 9 px -> 24*9 = 216 px.
// accent — текущий час, light — остальные. zeroBase=true — ось от нуля
// (мощность), false — от минимума (напряжение/температура: иначе столбики
// 236 и 239 В выглядели бы одинаково).
void drawHourBars(int x, int y, int w, int h, const float* vals, uint8_t bit,
                  uint16_t accent, uint16_t light, bool zeroBase) {
  uiFill(x, y, w, h, cBg);

  float avg[24];
  bool has[24];
  float lo = 0, hi = 0;
  bool any = false;
  for (int i = 0; i < 24; i++) {
    int n = 0;
    avg[i] = hourAverage(vals, bit, i, &n);
    has[i] = (n > 0);
    if (!has[i]) continue;
    if (!any) { lo = hi = avg[i]; any = true; }
    else {
      if (avg[i] < lo) lo = avg[i];
      if (avg[i] > hi) hi = avg[i];
    }
  }

  const int PITCH = 9, BW = 8;
  const int mk[5] = { 0, 6, 12, 18, 24 };
  int ly = y + h + 2;

  if (!any) {
    drawTxtC(x + w / 2, y + h / 2 - 3, "НЕТ ДАННЫХ", cMuted, cBg, 1);
  } else {
    if (zeroBase) lo = 0;
    else lo -= 2.0f;
    hi += (hi - lo) * 0.10f;                  // запас над самым высоким
    if (hi <= lo) hi = lo + 1.0f;

    for (int i = 1; i < 4; i++) uiHLine(x, y + h * i / 4, w, cLine);

    struct tm ti;
    int nowH = getLocalTime(&ti, 5) ? ti.tm_hour : -1;
    int base = y + h - 1;
    int plotH = h - 4;
    for (int i = 0; i < 24; i++) {
      if (!has[i]) continue;
      int bx = x + i * PITCH;
      if (bx < x || bx + BW > x + w) continue;   // не выходим за область
      float v = avg[i];
      if (v < lo) v = lo;
      if (v > hi) v = hi;
      int bh = (int)((v - lo) / (hi - lo) * plotH);
      if (bh < 2) bh = 2;
      if (bh > plotH) bh = plotH;
      uiFill(bx, base - bh, BW, bh, (i == nowH) ? accent : light);
    }
    uiHLine(x, base, 24 * PITCH - 2, cLine);
  }

  // подписи часов: 00, 06, 12, 18, 24 — ровно под столбиками
  for (int k = 0; k < 5; k++) {
    char b[4];
    snprintf(b, sizeof(b), "%02d", mk[k]);
    int px = x + mk[k] * PITCH;
    if (k == 0)      drawTxt(px, ly, b, cMuted, cBg, 1);
    else if (k == 4) drawTxt(px - 11, ly, b, cMuted, cBg, 1);
    else             drawTxt(px - 5, ly, b, cMuted, cBg, 1);
  }
}

// ---------------------------------------------------------------- плашка тревоги
// v4.3: с экрана убрана совсем — сторож устройств отключён, чтобы не тратить
// время на TCP. Код оставлен под #if 0: чтобы вернуть, снять 0 и вернуть
// вызов drawAlertBadge() в drawMain().
#if 0
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
#endif

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
  snprintf(b, sizeof(b), "%.2f А", gCurrent);
  drawTxt(pb.x + 6, pb.y + 45, b, cMuted, cSurf, 1);
  drawTxtR(pb.x + pb.w - 6, pb.y + 45, "ВТ", cMuted, cSurf, 1);

  // ---- сегодня
  const Block& db = BLOCKS[3];
  drawBlockFrame(db.x, db.y, db.w, db.h);
  drawTxt(db.x + 8, db.y + 4, "СЕГОДНЯ", cMuted, cSurf, 1);
  // Те же правила, что в окне ИТОГИ: компактное число и подгонка масштаба,
  // иначе 100 кВт*ч и 1000 руб. налезали друг на друга.
  fmtVal(b, sizeof(b), myTodayKwh);
  drawTxt(db.x + 8, db.y + 14, b, cGood, cSurf, fitScale(b, 100, 3));
  drawTxt(db.x + 8, db.y + 38, "КВТ*Ч", cMuted, cSurf, 1);
  fmtVal(b, sizeof(b), myTodayCost);
  drawTxtR(db.x + db.w - 8, db.y + 14, b, C_MONEY, cSurf, fitScale(b, 100, 3));
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
  // Плашка тревоги убрана (v4.3): сторож устройств отключён.
}

// ---------------------------------------------------------------- окно напряжения
void drawVoltScreen() {
  uiScreen(cBg);
  drawDetailHeader("НАПРЯЖЕНИЕ");   // было «ВОЛЬТЫ»: Ы в 5x7 читалась как «ВI»
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

  // График — самая дорогая часть окна. При точечном обновлении его не
  // перерисовываем: столбики за прошедшие часы не меняются.
  drawHourTitle(192, h24V, 1, "В", 0);
  if (!gSkipChart)
    drawHourBars(10, 202, 220, 86, h24V, 1, cAccent, tft.color565(191, 219, 254), false);
  drawTxt(10, 301, "СРЕДНЕЕ НАПРЯЖЕНИЕ ЗА КАЖДЫЙ ЧАС", cMuted, cBg, 1);
}

// ---------------------------------------------------------------- окно температуры
void drawTempScreen() {
  uiScreen(cBg);
  drawDetailHeader("ТЕМП РЕЛЕ");    // ясно, что это датчик реле, а не воздух

  char b[40];
  snprintf(b, sizeof(b), "%.1f", gTemp);
  int dh = 46, gap = 7;
  int tw = seg7W(b, dh, gap);
  seg7Str(12, 54, dh, b, cTEMP_ACC, gap);
  drawTxt(12 + tw + 8, 80, "C", cMuted, cBg, 2);
  drawTxt(12, 104, "ТЕМПЕРАТУРА КОРПУСА РЕЛЕ", cMuted, cBg, 1);

  drawTxt(12, 118, "МАКС ЗА СУТКИ", cMuted, cBg, 1);
  snprintf(b, sizeof(b), "%.1f", (dayMaxT > -100) ? dayMaxT : gTemp);
  drawTxt(12, 128, b, cTEMP_ACC, cBg, 3);
  hhmm(dayMaxTMin, b, sizeof(b));
  drawTxt(12, 154, b, cMuted, cBg, 1);

  drawTxt(100, 118, "СРЕДНЯЯ", cMuted, cBg, 1);
  snprintf(b, sizeof(b), "%.1f", dayTSec ? (float)(daySumT / dayTSec) : gTemp);
  drawTxt(100, 128, b, cText, cBg, 2);

  drawTxt(100, 152, "МИН", cMuted, cBg, 1);
  snprintf(b, sizeof(b), "%.1f", (dayMinT < 900) ? dayMinT : gTemp);
  drawTxt(148, 152, b, cMuted, cBg, 1);

  drawHourTitle(176, h24T, 2, "C", 1);
  if (!gSkipChart)
    drawHourBars(10, 186, 220, 86, h24T, 2, cTEMP_ACC, tft.color565(221, 214, 254), false);
  drawTxt(10, 284, "СРЕДНЯЯ ТЕМПЕРАТУРА ЗА КАЖДЫЙ ЧАС", cMuted, cBg, 1);
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

  // Именно мгновенная нагрузка через реле: по ней и видно включение перфоратора.
  drawTxt(12, 104, "МГНОВЕННАЯ НАГРУЗКА ЧЕРЕЗ РЕЛЕ", cWarn, cBg, 1);

  drawTxt(12, 118, "СРЕДНЯЯ", cMuted, cBg, 1);
  snprintf(b, sizeof(b), "%.0f ВТ", (dayPSec > 0) ? (float)(daySumP / dayPSec) : gPower);
  drawTxt(12, 128, b, cText, cBg, 2);

  drawTxt(118, 118, "ПИК", cMuted, cBg, 1);
  snprintf(b, sizeof(b), "%.0f ВТ", dayMaxP);
  drawTxt(118, 128, b, cWarn, cBg, 2);
  hhmm(dayMaxPMin, b, sizeof(b));
  drawTxt(118, 146, b, cMuted, cBg, 1);

  drawTxt(12, 160, "ТОК", cMuted, cBg, 1);
  snprintf(b, sizeof(b), "%.2f А", gCurrent);
  drawTxt(54, 160, b, cText, cBg, 2);

  drawTxt(12, 178, "НАПРЯЖЕНИЕ X ТОК", cMuted, cBg, 1);
  snprintf(b, sizeof(b), "%.0f В  %.2f А  %.0f ВТ", gVoltage, gCurrent, gPower);
  drawTxt(12, 190, b, cMuted, cBg, 1);

  drawHourTitle(204, h24P, 4, "ВТ", 0);
  if (!gSkipChart)
    drawHourBars(10, 214, 220, 72, h24P, 4, cPOWER_ACC, tft.color565(253, 230, 138), true);
  drawTxt(10, 299, "СРЕДНЯЯ МОЩНОСТЬ ЗА КАЖДЫЙ ЧАС", cMuted, cBg, 1);
}

// ---------------------------------------------------------------- окно итогов
// Две строгие колонки: кВт·ч прижаты к правому краю 114, рубли — к 228.
// Между числами всегда ≥14 px, подписи стоят ровно под своими числами.
// Ширина чисел ограничена fitScale(), поэтому даже 12345 не налезает.
void drawTodayScreen() {
  uiScreen(cBg);
  drawDetailHeader("ИТОГИ");

  char b[40];
  drawTxt(12, 52, "СЕГОДНЯ", cMuted, cBg, 1);

  float dKwh  = gDemoToday ? 123.45f  : myTodayKwh;
  float dCost = gDemoToday ? 1234.56f : myTodayCost;

  fmtVal(b, sizeof(b), dKwh);
  int s1 = fitScale(b, 102, 3);
  drawTxtR(114, 64, b, cGood, cBg, s1);
  drawTxtR(114, 64 + 7 * s1 + 2, "КВТ*Ч", cMuted, cBg, 1);

  fmtVal(b, sizeof(b), dCost);
  int s2 = fitScale(b, 100, 3);
  drawTxtR(228, 64, b, C_MONEY, cBg, s2);
  drawTxtR(228, 64 + 7 * s2 + 2, "РУБ", cMuted, cBg, 1);
  uiVLine(120, 50, 48, cLine);

  uiHLine(12, 104, 216, cLine);
  drawTxtR(136, 108, "КВТ*Ч", cMuted, cBg, 1);
  drawTxtR(228, 108, "РУБ", cMuted, cBg, 1);

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
  if (gDemoToday) {                    // DEMO: широкие числа для проверки вёрстки
    for (int i = 0; i < 5; i++) { rows[i].kwh *= 50.0f; rows[i].rub *= 50.0f; }
    projKwh *= 50.0f;
    projCost *= 50.0f;
  }

  int y = 120;
  for (int i = 0; i < 5; i++) {
    drawTxt(12, y + 4, rows[i].name, cMuted, cBg, 1);
    if (rows[i].moneyOnly) {
      drawTxt(70, y + 4, "ДЕНЬ/НОЧЬ", cMuted, cBg, 1);
      drawTxtR(228, y + 4, tariffTxt, cText, cBg, 1);
    } else {
      fmtVal(b, sizeof(b), rows[i].kwh);
      int sc = fitScale(b, 76, 2);
      drawTxtR(136, y, b, cText, cBg, sc);
      fmtVal(b, sizeof(b), rows[i].rub);
      sc = fitScale(b, 82, 2);
      drawTxtR(228, y, b, cText, cBg, sc);
    }
    y += 26;
  }

  drawTxt(12, y + 4, "ПРОГНОЗ", cWarn, cBg, 1);
  fmtVal(b, sizeof(b), projKwh);
  int sc = fitScale(b, 76, 2);
  drawTxtR(136, y, b, cWarn, cBg, sc);
  fmtVal(b, sizeof(b), projCost);
  sc = fitScale(b, 82, 2);
  drawTxtR(228, y, b, cWarn, cBg, sc);

  uiHLine(12, y + 26, 216, cLine);
  snprintf(b, sizeof(b), "СЕЙЧАС %.0f ВТ", gPower);
  drawTxt(12, y + 32, b, cMuted, cBg, 1);
  snprintf(b, sizeof(b), "СРЕДНИЙ ТАРИФ %.2f", measuredTariff());
  drawTxtR(228, y + 32, b, cMuted, cBg, 1);
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


// ---------------------------------------------------------------- окно сторожа
void drawWatchScreen() {
  uiScreen(cBg);            // БЕЗ ЭТОГО окно было «прозрачным»
  drawDetailHeader("СТОРОЖ");

  int y = 56;
  int okN = 0;
  for (int i = 0; i < WD_N; i++) {
    bool alive  = wdAlive[i] && wdChecked[i];
    bool failed = wdChecked[i] && !wdAlive[i];
    if (alive) okN++;

    uint16_t col = !wdChecked[i] ? cMuted : (alive ? cGood : cBadC);
    uiCircle(14, y + 8, 4, col);

    drawTxt(26, y, WD_HOSTS[i].name, cText, cBg, 2);

    char b[24];
    snprintf(b, sizeof(b), "%d.%d.%d.%d:%d",
             WD_HOSTS[i].ip[0], WD_HOSTS[i].ip[1],
             WD_HOSTS[i].ip[2], WD_HOSTS[i].ip[3], WD_HOSTS[i].port);
    drawTxt(26, y + 18, b, cMuted, cBg, 1);

    if (!wdChecked[i])        drawTxtR(232, y + 6, "ЖДЁМ", cMuted, cBg, 1);
    else if (failed)          drawTxtR(232, y + 6, "НЕТ ОТВЕТА", cBadC, cBg, 1);
    else if (wdMs[i] > 0) {
      snprintf(b, sizeof(b), "%d МС", wdMs[i]);
      drawTxtR(232, y + 6, b, (wdMs[i] > 300) ? cWarn : cMuted, cBg, 1);
    } else drawTxtR(232, y + 6, "ОК", cGood, cBg, 1);

    y += 38;
    if (i < WD_N - 1) uiHLine(12, y - 6, 216, cLine);
  }

  uiHLine(0, 218, 240, cLine);

  char b[40];
  snprintf(b, sizeof(b), "%d ИЗ %d ОТВЕЧАЮТ", okN, WD_N);
  drawTxt(12, 228, b, (okN == WD_N) ? cGood : cWarn, cBg, 2);

  snprintf(b, sizeof(b), "ОПРОС КАЖДЫЕ %d С", (int)(WD_CYCLE_MS / 1000));
  drawTxt(12, 248, b, cMuted, cBg, 1);

  struct tm ti;
  if (getLocalTime(&ti, 10)) {
    snprintf(b, sizeof(b), "ПОСЛЕДНИЙ КРУГ %02d:%02d:%02d",
             ti.tm_hour, ti.tm_min, ti.tm_sec);
  } else strcpy(b, "ПОСЛЕДНИЙ КРУГ --:--");
  drawTxt(12, 262, b, cMuted, cBg, 1);

  drawTxt(12, 284, "ПРОВЕРКА TCP-ПОДКЛЮЧЕНИЕМ", cMuted, cBg, 1);
  drawTxt(12, 298, "ПОРТ ОТКРЫТ — УСТРОЙСТВО ЖИВО", cMuted, cBg, 1);
}

// ---------------------------------------------------------------- диспетчер
void drawScreen() {
  switch (gScreen) {
    case SCR_VOLT:    drawVoltScreen();    break;
    case SCR_TEMP:    drawTempScreen();    break;
    case SCR_POWER:   drawPowerScreen();   break;
    case SCR_TODAY:   drawTodayScreen();   break;
    case SCR_WEATHER: drawWeatherScreen(); break;
    case SCR_WATCH:   drawWatchScreen();   break;
    default:          drawMain();          break;
  }
}

// ---------------------------------------------------------------- обновление без мигания
// Отпечаток данных, которые видны на ТЕКУЩЕМ экране. Если он не изменился,
// перерисовывать нечего. Раньше сигнатура была общей для всех экранов и не
// учитывала половину полей: смена мощности перерисовывала окно ВОЛЬТЫ целиком.
uint32_t screenSig() {
  uint32_t h = 2166136261u ^ (uint32_t)(gScreen * 2654435761u);
  uint32_t v[20];
  int n = 0;
  switch (gScreen) {
    case SCR_VOLT:
      v[n++] = (uint32_t)(int32_t)lroundf(gVoltage);
      v[n++] = gRelay ? 1u : 0u;
      v[n++] = (uint32_t)(int32_t)lroundf(((dayMinV < 900) ? dayMinV : 0) * 10.0f);
      v[n++] = (uint32_t)(int32_t)lroundf(dayMaxV * 10.0f);
      v[n++] = (uint32_t)(int32_t)((dayVSec > 0) ? (daySumV / dayVSec) * 10.0 : 0);
      v[n++] = (uint32_t)dayOutSec;
      break;
    case SCR_TEMP:
      v[n++] = gTempValid ? (uint32_t)(int32_t)lroundf(gTemp * 10.0f) : 0xFFFFFFu;
      v[n++] = (uint32_t)(int32_t)lroundf(((dayMinT < 900) ? dayMinT : 0) * 10.0f);
      v[n++] = (uint32_t)(int32_t)lroundf(dayMaxT * 10.0f);
      v[n++] = (uint32_t)(int32_t)((dayTSec > 0) ? (daySumT / dayTSec) * 10.0 : 0);
      break;
    case SCR_POWER:
      v[n++] = (uint32_t)(int32_t)lroundf(gPower);
      v[n++] = (uint32_t)(int32_t)lroundf(gVoltage);
      v[n++] = (uint32_t)(int32_t)fmodf((float)((dayPSec > 0) ? daySumP / dayPSec : 0), 1000.0f);
      v[n++] = (uint32_t)(int32_t)lroundf(dayMaxP);
      break;
    case SCR_TODAY:
      v[n++] = (uint32_t)(int32_t)lroundf(myTodayKwh * 100.0f);
      v[n++] = (uint32_t)(int32_t)lroundf(myTodayCost * 100.0f);
      v[n++] = (uint32_t)(int32_t)lroundf(yKwh * 100.0f);
      v[n++] = (uint32_t)(int32_t)lroundf(yCost * 100.0f);
      v[n++] = (uint32_t)(int32_t)lroundf(mKwh * 100.0f);
      v[n++] = (uint32_t)(int32_t)lroundf(mCost * 100.0f);
      v[n++] = (uint32_t)(int32_t)lroundf(statDayKwh * 100.0f);
      v[n++] = (uint32_t)(int32_t)lroundf(statNightKwh * 100.0f);
      break;
    case SCR_WEATHER:
      v[n++] = wOk ? 1u : 0u;
      v[n++] = (uint32_t)(int32_t)lroundf(wTemp * 10.0f);
      v[n++] = (uint32_t)(int32_t)lroundf(wHum);
      v[n++] = (uint32_t)(int32_t)lroundf(wWind);
      v[n++] = (uint32_t)wCode;
      break;
    default:   // главный: всё, что на нём нарисовано
      v[n++] = (uint32_t)(int32_t)lroundf(gVoltage);
      v[n++] = gTempValid ? (uint32_t)(int32_t)lroundf(gTemp * 10.0f) : 0xFFFFFFu;
      v[n++] = (uint32_t)(int32_t)lroundf(gPower);
      v[n++] = gRelay ? 1u : 0u;
      v[n++] = (uint32_t)(int32_t)lroundf(((dayMinV < 900) ? dayMinV : 0) * 10.0f);
      v[n++] = (uint32_t)(int32_t)lroundf(dayMaxV * 10.0f);
      v[n++] = (uint32_t)(int32_t)((dayTSec > 0) ? (daySumT / dayTSec) * 10.0 : 0);
      v[n++] = (uint32_t)(int32_t)lroundf(myTodayKwh * 100.0f);
      v[n++] = (uint32_t)(int32_t)lroundf(myTodayCost * 100.0f);
      v[n++] = (uint32_t)(int32_t)lroundf(yKwh * 100.0f);
      v[n++] = (uint32_t)(int32_t)lroundf(mKwh * 100.0f);
      v[n++] = (uint32_t)(int32_t)lroundf(wTemp * 10.0f);
      v[n++] = (uint32_t)(int32_t)lroundf(wHum);
      v[n++] = (uint32_t)(int32_t)lroundf(wWind);
      v[n++] = wOk ? 1u : 0u;
      v[n++] = (uint32_t)gRssi;
      v[n++] = (uint32_t)gSdRows;
      break;
  }
  for (int i = 0; i < n; i++) { h ^= v[i]; h *= 16777619u; }
  // строки (описание погоды) — по символам, они тоже видны на экране
  if (gScreen == SCR_MAIN || gScreen == SCR_WEATHER)
    for (int i = 0; i < (int)wDescEn.length(); i++) { h ^= (uint8_t)wDescEn[i]; h *= 16777619u; }
  return h;
}

// Главный экран: фон НЕ заливаем целиком (это 63 600 px, ~25 мс). Каждый
// блок и шапка заливаются сами в drawBlockFrame/drawTopBar, а промежутки
// между блоками никогда не рисуются — там и так фон.
void refreshMain() {
  if (gCap) { drawScreen(); return; }
  gNoClear = true;
  drawMain();
  gNoClear = false;
}

// Окно открыто: стираем ТОЛЬКО области с цифрами, остальное перерисовывается
// поверх теми же пикселями. Полной заливки экрана нет, тач не блокируется.
void refreshWindow() {
  if (gCap) { drawScreen(); return; }
  switch (gScreen) {
    case SCR_WATCH:
      uiFill(0, 50, 240, 168, cBg);     // весь список
      break;
    case SCR_VOLT:
      uiFill(10,  50, 214, 52, cBg);    // цифры, вердикт, реле
      uiFill(10, 128, 216, 44, cBg);    // мин / сред / макс
      uiFill(12, 176, 216,  8, cBg);    // вне нормы
      break;
    case SCR_TEMP:
      uiFill(10,  50, 214, 52, cBg);    // цифры
      uiFill(10, 116, 216, 48, cBg);    // макс / средняя / мин
      break;
    case SCR_POWER:
      uiFill(10,  50, 214, 52, cBg);    // цифры
      uiFill(10, 116, 216, 82, cBg);    // средняя / пик / ток / U*I
      break;
    default:
      uiFill(10, 54, 220, 240, cBg);    // «ИТОГИ» и «ПОГОДА»
      break;
  }
  gNoClear = true;
  gSkipChart = true;                    // график 24 ч не трогаем
  drawScreen();
  gSkipChart = false;
  gNoClear = false;
}

// ---------------------------------------------------------------- тачскрин
bool inRect(int x, int y, int rx, int ry, int rw, int rh) {
  return x >= rx && x < rx + rw && y >= ry && y < ry + rh;
}

void handleTapAt(int x, int y) {
  if (gScreen != SCR_MAIN) {
    if (inRect(x, y, 0, 0, 240, 52)) {          // вся верхняя полоса — «назад»
      gScreen = SCR_MAIN;
      drawScreen();
      gLastDraw = millis();
      gLastSig = screenSig();
      gLastWinFull = millis();
    }
    return;
  }

  // Сначала точное попадание в карточку: оно всегда важнее допуска TAP_SLOP.
  // Допуск нужен только для промаха мимо края — при нахлёсте двух зон берём
  // ту карточку, чей центр ближе (иначе в щели 116..119 всегда выигрывала
  // ТЕМПЕРАТУРА, и казалось, что МОЩНОСТЬ не нажимается).
  int best = -1;
  long bestD = 0;
  for (int i = 0; i < 5; i++) {
    if (inRect(x, y, BLOCKS[i].x, BLOCKS[i].y, BLOCKS[i].w, BLOCKS[i].h)) { best = i; break; }
  }
  if (best < 0) {
    for (int i = 0; i < 5; i++) {
      if (!inRect(x, y, BLOCKS[i].x - TAP_SLOP, BLOCKS[i].y - TAP_SLOP,
                  BLOCKS[i].w + 2 * TAP_SLOP, BLOCKS[i].h + 2 * TAP_SLOP)) continue;
      long dx = x - (BLOCKS[i].x + BLOCKS[i].w / 2);
      long dy = y - (BLOCKS[i].y + BLOCKS[i].h / 2);
      long d = dx * dx + dy * dy;
      if (best < 0 || d < bestD) { best = i; bestD = d; }
    }
  }
  if (best >= 0) {
    gScreen = BLOCKS[best].scr;
    drawScreen();
    gLastDraw = millis();
    gLastSig = screenSig();
    gLastWinFull = millis();
  }
}

void handleTouch() {
  static unsigned long lock = 0;
  if (millis() < lock) return;
  if (!tpDown()) return;

  // до трёх попыток прочитать точку: первый замер иногда шумный
  int x = 0, y = 0;
  bool ok = false;
  for (int a = 0; a < 3 && !ok; a++) {
    ok = tpPoint(&x, &y);
    if (!ok) delay(3);
  }
  if (!ok) { lock = millis() + 150; return; }

  unsigned long t0 = millis();
  while (tpDown() && millis() - t0 < 250UL) { delay(5); esp_task_wdt_reset(); }
  lock = millis() + 200;
  Serial.printf("TAP %d %d\n", x, y);
  gTouchMs = millis();
  gTouchSkip = millis() + 1200;      // 1.2 с не начинаем тяжёлые сетевые дела
  handleTapAt(x, y);
}

// Восстановленные из облака строки за сегодня: у них счётчик (4-е поле) = 0.
// Команда CSVH — чтобы видеть, что именно дописала плата.
void printCsvHist() {
  if (!gSdOk) { Serial.println("CSVH-ERR нет карты"); return; }
  File f = SD.open("/energy.csv", FILE_READ);
  if (!f) { Serial.println("CSVH-ERR файл"); return; }
  struct tm ti;
  if (!getLocalTime(&ti, 10)) { Serial.println("CSVH-ERR нет времени"); f.close(); return; }
  char dayPrefix[12];
  snprintf(dayPrefix, sizeof(dayPrefix), "%04d-%02d-%02d",
           ti.tm_year + 1900, ti.tm_mon + 1, ti.tm_mday);
  size_t off = findDayStartOffset(f, dayPrefix);
  f.seek(off);
  int n = 0;
  int rd = 0;
  while (f.available()) {
    String l = f.readStringUntil('\n');
    l.trim();
    if ((++rd & 255) == 0) esp_task_wdt_reset();
    if (l.length() < 20) continue;
    int c1 = l.indexOf(',');
    if (c1 < 0) continue;
    int c2 = l.indexOf(',', c1 + 1);
    if (c2 < 0) continue;
    int c3 = l.indexOf(',', c2 + 1);
    if (c3 < 0) continue;
    int c4 = l.indexOf(',', c3 + 1);
    if (c4 < 0) continue;
    if (l.substring(c3 + 1, c4).toFloat() > 0) continue;   // живая строка
    Serial.printf("CSVH %s\n", l.c_str());
    n++;
  }
  f.close();
  Serial.printf("CSVH восстановленных строк за сегодня: %d\n", n);
}

// Сырые значения тачскрина: 0x90 (Y), 0xD0 (X) и что из них получается.
// Команда RAW — чтобы проверять точность тракта без экрана калибровки.
void printRaw() {
  Serial.printf("RAW нажат=%d, калибровка X %d..%d, Y %d..%d\n",
                tpDown() ? 1 : 0, calYleft, calYright, calXtop, calXbottom);
  for (int i = 0; i < 5; i++) {
    uint16_t a = tpRaw(0x90), b = tpRaw(0xD0);
    int x = 0, y = 0;
    bool ok = tpPoint(&x, &y);
    Serial.printf("RAW %d: 0x90=%u 0xD0=%u -> %d,%d %s\n",
                  i, a, b, x, y, ok ? "ok" : "нет");
    delay(80);
  }
}

// Диагностика: забыть погоду — проверить, что экран покажет «НЕТ ДАННЫХ»
// и цикл не начнёт дёргать интернет каждый проход.
void clearWeather() {
  wOk = false; wTemp = 0; wHum = 0; wWind = 0; wCode = -1; wDescEn = "";
  gLastDraw = 0;
  Serial.println("погода сброшена (НЕТ ДАННЫХ)");
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
  // Предел 60 с (было 180): импорт не должен держать цикл минутами.
  // Касание экрана прерывает импорт — человек важнее.
  while (millis() - t0 < 60000UL) {
    if (tpDown()) { Serial.println("IMP-ABORT тач"); break; }
    if (!Serial.available()) { delay(5); esp_task_wdt_reset(); continue; }
    String line = Serial.readStringUntil('\n');
    line.trim();
    if (line == "IMPEND") break;
    if (line.length() > 12 && isDigit(line[0])) {
      f.println(line);
      n++;
      if (n % 100 == 0) { f.flush(); esp_task_wdt_reset(); }
    }
    esp_task_wdt_reset();
  }
  f.close();
  gSdRows += n;
  Serial.printf("IMP-DONE %d\n", n);
}

// ---------------------------------------------------------------- служебные команды
void doShot(int n) {
  if (gCap) { Serial.println("SHOT-BUSY"); return; }   // хост не должен ждать 45 с
  int prev = gScreen;
  if (n >= 0 && n <= 6) gScreen = n;
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
  int rd = 0;
  while (f.available()) {
    String l = f.readStringUntil('\n');
    l.trim();
    if ((++rd & 63) == 0) esp_task_wdt_reset();
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
  // сколько часов суток вообще есть на графике 24 ч
  int h24h = 0;
  for (int h = 0; h < 24; h++) {
    for (int s = 0; s < 12; s++)
      if (h24Mask[h * 12 + s] & 1) { h24h++; break; }
  }
  Serial.printf("DIAG v=%.0f min=%.0f@%s max=%.0f@%s avg=%.1f out=%ld/%ld "
                "today=%.3f/%.2f y=%.3f/%.2f m=%.2f/%.2f tar=%.2f heap=%u inet=%d hist=%d h24=%d\n",
                gVoltage,
                (dayMinV < 900) ? dayMinV : 0, t1,
                dayMaxV, t2,
                (dayVSec > 0) ? (float)(daySumV / dayVSec) : 0,
                dayOutSec, dayVSec,
                myTodayKwh, myTodayCost, yKwh, yCost, mKwh, mCost,
                measuredTariff(), ESP.getFreeHeap(), gNetOk ? 1 : 0, gHistAdded, h24h);
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
    if ((++n & 255) == 0) esp_task_wdt_reset();
    long t = lineEpoch(l);
    if (!t) continue;
    int minute = (int)((t % 86400L) / 60);
    perHour[minute / 60]++;
    if (n == 1) first = l;
    last = l;
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
// Команда WD — три круга опроса подряд с тем же таймаутом, что и в работе.
void printWd() {
  Serial.printf("WD с платы %s, адресов %d, круг %lu мс, таймаут %d мс\n",
                WiFi.localIP().toString().c_str(), WD_N,
                (unsigned long)WD_CYCLE_MS, (int)WD_CONN_MS);
  for (int round = 0; round < 3; round++) {
    Serial.printf("WD круг %d:", round + 1);
    for (int i = 0; i < WD_N; i++) {
      IPAddress ip(WD_HOSTS[i].ip[0], WD_HOSTS[i].ip[1],
                   WD_HOSTS[i].ip[2], WD_HOSTS[i].ip[3]);
      unsigned long t0 = millis();
      WiFiClient c;
      bool ok = (c.connect(ip, WD_HOSTS[i].port, WD_CONN_MS) == 1);
      unsigned long dt = millis() - t0;
      c.stop();
      Serial.printf(" %s(%u)=%s/%lums",
                    WD_HOSTS[i].name, WD_HOSTS[i].port,
                    ok ? "ok" : "НЕТ", dt);
      esp_task_wdt_reset();          // WDT: 3 круга по 4 адреса с таймаутами
      delay(40);
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
  int rd = 0;
  while (f.available()) {
    String l = f.readStringUntil('\n');
    l.trim();
    if ((++rd & 255) == 0) esp_task_wdt_reset();
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
//  ВЕБ-СТРАНИЦА — проверка без USB
// ============================================================================
//  http://192.168.1.51/         — все цифры, как на экране
//  http://192.168.1.51/data     — те же данные в JSON
//  http://192.168.1.51/csv      — последние 40 строк истории
//  http://192.168.1.51/day      — строки истории по часам
WebServer gWeb(80);

String webPage() {
  char b[64];
  String h = F("<!DOCTYPE html><meta charset=utf-8>"
               "<meta name=viewport content='width=device-width,initial-scale=1'>"
               "<title>Дом — энергия</title><style>"
               "body{font:16px/1.5 system-ui,sans-serif;background:#F8FAFF;color:#0F172A;"
               "margin:0;padding:16px;max-width:520px}"
               "h1{font-size:22px;margin:0 0 4px}"
               ".s{color:#5A6478;font-size:13px;margin-bottom:14px}"
               ".c{background:#fff;border:1px solid #DBE4F5;border-radius:14px;"
               "padding:14px 16px;margin:10px 0}"
               ".l{font-size:12px;color:#5A6478;text-transform:uppercase;letter-spacing:.06em}"
               ".v{font-size:30px;font-weight:700;letter-spacing:-.02em}"
               ".b{color:#2563EB}.g{color:#0F9F6E}.r{color:#DB2777}.m{color:#94A3B8}"
               "table{width:100%;border-collapse:collapse;font-size:15px}"
               "td{padding:5px 0;border-bottom:1px solid #EDF1F9}"
               "td:last-child{text-align:right;font-weight:600}"
               "</style>");

  h += "<h1>Дом — энергия</h1>";
  snprintf(b, sizeof(b), "<div class=s>%s · %d dBm · карта %d строк</div>",
           WiFi.localIP().toString().c_str(), gRssi, gSdRows);
  h += b;

  // напряжение
  snprintf(b, sizeof(b), "%.0f", gVoltage);
  h += "<div class=c><div class=l>Напряжение</div>";
  h += "<div class='v b'>" + String(b) + " В</div>";
  snprintf(b, sizeof(b), "<div class=s>%s · норма 207–253 В</div>", voltVerdict(gVoltage));
  h += b;
  h += "</div>";

  // температура и мощность
  h += "<div class=c><table>";
  snprintf(b, sizeof(b), "<tr><td>Температура реле</td><td>%.1f °C</td></tr>", gTemp);
  h += b;
  snprintf(b, sizeof(b), "<tr><td>Мощность</td><td>%.1f Вт · %.2f А</td></tr>", gPower, gCurrent);
  h += b;
  h += "</table></div>";

  // расход
  h += "<div class=c><table>";
  snprintf(b, sizeof(b), "<tr><td><b>Сегодня</b></td><td>%.2f кВт·ч · %.2f ₽</td></tr>",
           myTodayKwh, myTodayCost);
  h += b;
  snprintf(b, sizeof(b), "<tr><td>Вчера</td><td>%.2f кВт·ч · %.2f ₽</td></tr>", yKwh, yCost);
  h += b;
  snprintf(b, sizeof(b), "<tr><td>Месяц</td><td>%.2f кВт·ч · %.2f ₽</td></tr>", mKwh, mCost);
  h += b;
  snprintf(b, sizeof(b), "<tr><td>Счётчик всего</td><td>%.2f кВт·ч</td></tr>", gEnergy);
  h += b;
  snprintf(b, sizeof(b), "<tr><td>Средний тариф</td><td>%.2f ₽/кВт·ч</td></tr>", measuredTariff());
  h += b;
  h += "</table></div>";

  // погода
  h += "<div class=c><div class=l>Погода · ВАШЕГО ГОРОДА</div>";
  if (wOk) {
    char d[20]; descToRus(wDescEn, d, sizeof(d));
    snprintf(b, sizeof(b), "<div class='v b'>%.0f °C</div>", wTemp);
    h += b;
    snprintf(b, sizeof(b), "<div class=s>%s · ветер %.0f м/с · влажность %.0f %%</div>",
             d, wWind, wHum);
    h += b;
  } else {
    h += "<div class=m>нет данных</div>";
  }
  h += "</div>";

  // сторож
  h += "<div class=c><div class=l>Сторож сети</div>";
  if (gWdBad >= 0) {
    snprintf(b, sizeof(b), "<div class=s>%s</div>",
             gWdBad ? "есть недоступные устройства" : "все устройства отвечают");
    h += b;
  }
  snprintf(b, sizeof(b), "<div class=s>интернет: %s · история: %s</div>",
           gNetOk ? "есть" : "нет", gHistAdded ? "дополнена" : "не требуется");
  h += b;
  h += "</div>";

  snprintf(b, sizeof(b), "<div class=s>%s · %s</div>",
           WiFi.status() == WL_CONNECTED ? "Wi-Fi ok" : "нет Wi-Fi",
           gNetOk ? "интернет ok" : "без интернета");
  h += b;
  return h;
}

void webSetup() {
  gWeb.on("/", []() { gWeb.send(200, "text/html; charset=utf-8", webPage()); });

  gWeb.on("/data", []() {
    char b[320];
    snprintf(b, sizeof(b),
      "{\"v\":%.0f,\"temp\":%.1f,\"p\":%.1f,\"amp\":%.2f,\"e\":%.2f,"
      "\"r\":%s,\"tkwh\":%.3f,\"tcost\":%.2f,\"ykwh\":%.3f,\"ykost\":%.2f,"
      "\"mkwh\":%.2f,\"mcost\":%.2f,\"tariff\":%.2f,\"rssi\":%d,\"sdRows\":%d,"
      "\"vAge\":%lu,\"stale\":%s,"
      "\"net\":%s,\"heap\":%u}",
      gVoltage, gTemp, gPower, gCurrent, gEnergy,
      gRelay ? "true" : "false", myTodayKwh, myTodayCost, yKwh, yCost,
      mKwh, mCost, measuredTariff(), gRssi, gSdRows,
      (unsigned long)((millis() - gPhaseMs) / 1000UL),
      ((millis() - gPhaseMs) > 120000UL) ? "true" : "false",
      gNetOk ? "true" : "false", ESP.getFreeHeap());
    gWeb.send(200, "application/json", String(b));
  });

  // Полная выгрузка истории потоком — для бэкапа на компьютер/NAS
  // Выгрузка истории. Поддерживает диапазон: /full?from=<байт>&len=<байт>
  // Это снимает прежнее ограничение 17 МБ (обрезка по 120 с): большой файл
  // качается кусками по 4 МБ, каждый кусок укладывается в таймаут.
  gWeb.on("/full", []() {
    if (!gSdOk) { gWeb.send(500, "text/plain", "нет карты"); return; }
    File f = SD.open("/energy.csv", FILE_READ);
    if (!f) { gWeb.send(500, "text/plain", "нет файла"); return; }
    size_t total = f.size();

    size_t from = 0, want = total;
    if (gWeb.hasArg("from")) from = (size_t)strtoul(gWeb.arg("from").c_str(), NULL, 10);
    if (gWeb.hasArg("len"))  want = (size_t)strtoul(gWeb.arg("len").c_str(), NULL, 10);
    if (from > total) from = total;
    size_t remain = total - from;
    if (want > remain) want = remain;

    char hdr[80];
    snprintf(hdr, sizeof(hdr), "%u/%u", (unsigned)total, (unsigned)(from + want));
    gWeb.sendHeader("X-File-Size", hdr);
    gWeb.sendHeader("Content-Disposition", "attachment; filename=energy.csv");
    gWeb.setContentLength(want);
    gWeb.send(200, "text/csv; charset=utf-8", "");

    if (from > 0) f.seek(from);
    uint8_t buf[512];
    size_t sent = 0;
    unsigned long t0 = millis();
    while (sent < want) {
      size_t n = f.read(buf, sizeof(buf));
      if (n == 0) break;
      if (n > want - sent) n = want - sent;
      gWeb.client().write(buf, n);
      sent += n;
      esp_task_wdt_reset();
      if (millis() - t0 > 100000UL) break;   // кусок не больше 100 с
    }
    f.close();
    Serial.printf("веб: выгрузка %u..%u из %u байт\n",
                  (unsigned)from, (unsigned)(from + sent), (unsigned)total);
  });

  gWeb.on("/csv", []() {
    if (!gSdOk) { gWeb.send(500, "text/plain", "нет карты"); return; }
    File f = SD.open("/energy.csv", FILE_READ);
    if (!f) { gWeb.send(500, "text/plain", "нет файла"); return; }
    size_t sz = f.size();
    size_t off = (sz > 3500) ? (sz - 3500) : 0;
    f.seek(off);
    String out = "datetime,voltage,power,energy_kwh,relay,cost,temp\n";
    bool first = (off > 0);
    while (f.available()) {
      String l = f.readStringUntil('\n');
      if (first) { first = false; continue; }
      out += l; out += '\n';
    }
    f.close();
    gWeb.send(200, "text/plain; charset=utf-8", out);
  });

  gWeb.on("/day", []() {
    if (!gSdOk) { gWeb.send(500, "text/plain", "нет карты"); return; }
    File f = SD.open("/energy.csv", FILE_READ);
    if (!f) { gWeb.send(500, "text/plain", "нет файла"); return; }
    struct tm ti;
    if (!getLocalTime(&ti, 100)) { f.close(); gWeb.send(500, "text/plain", "нет времени"); return; }
    char pre[12];
    snprintf(pre, sizeof(pre), "%04d-%02d-%02d", ti.tm_year + 1900, ti.tm_mon + 1, ti.tm_mday);
    int per[24];
    memset(per, 0, sizeof(per));
    String out = String("строки по часам за ") + pre + ": ";
    while (f.available()) {
      String l = f.readStringUntil('\n');
      if (!l.startsWith(pre)) continue;
      int hh = l.substring(11, 13).toInt();
      if (hh >= 0 && hh < 24) per[hh]++;
    }
    f.close();
    for (int i = 0; i < 24; i++) { out += String(i) + ":" + String(per[i]) + " "; }
    gWeb.send(200, "text/plain; charset=utf-8", out);
  });

  gWeb.begin();
  Serial.println("веб: http://" + WiFi.localIP().toString() + "/");
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
  // 20 с: раньше на 8 с не успевали, время не приходило, и суточная
  // статистика не засевалась — на главном висели «МИН 0 / МАКС 0».
  while (millis() - t0 < 20000) {
    if (getLocalTime(&ti, 300)) break;
    delay(200);
  }
  // NTP не ответил (нет интернета) — берём время из памяти платы,
  // иначе не сможем писать историю на карту
  if (!getLocalTime(&ti, 50)) {
    if (!restoreClock()) Serial.println("время: нет ни NTP, ни памяти — история не пишется");
  } else {
    saveClock();
  }

  pickTheme();

  gSdOk = initSD();
  Serial.printf("SD: %s, строк %d\n", gSdOk ? "ok" : "нет", gSdRows);

  loadToday();
  loadPeriods();
  loadHistDay();
  seedStatsFromSd();     // сразу после карты и часов: мин/макс есть уже на 1-м экране

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
  webSetup();                 // веб-страница для проверки без USB
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
  { // раз в 10 минут запоминаем время: пригодится после перезагрузки без сети
    static unsigned long lastClk = 0;
    if (millis() - lastClk > 600000UL) { lastClk = millis(); saveClock(); }
  }
  gWeb.handleClient();          // веб-страница: проверка без USB
  statsTick();
  // История на карту — ВСЕГДА: без Wi-Fi, без интернета, без облака.
  // Данные с реле идут по локальной сети, карта от интернета не зависит.
  writeSd();
  // v4.3: сторож устройств отключён — wdTick() не вызываем, чтобы не
  // тратить время на TCP. Функция осталась ниже для большого экрана.
  // wdTick();
  netOnline();                     // интернет? (проверка не чаще раза в 30 с)
  histTick();                      // дозаполнение пропущенных часов из облака

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
      if (!gWifiOff) WiFi.reconnect();   // WIFIOFF — не трогаем радио вообще
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
          // локальный протокол реле их не прислал.
          // Если реле только что ответило (меньше 30 с назад), облако не нужно:
          // это лишний TLS-запрос на 2-6 с.
          // Без интернета cloudStatus() выходит сразу (netOnline()).
          if (millis() - gCloudLast > 60000UL) {
            gCloudLast = millis();
            if (millis() - gPhaseMs > 30000UL) cloudStatus();
          }
          break;
        case 3:
          fetchFromApi();
          break;
        default:
          pushHistory();
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

  // 4. Погода: первый раз через 5 с, потом раз в 10 минут.
  //    Пока данных нет — не чаще раза в минуту (а не каждый проход!).
  //    Нет интернета — на экране просто «НЕТ ДАННЫХ», цикл не тормозит.
  {
    unsigned long period = wOk ? 600000UL : 60000UL;
    bool due = (lastWeather == 0) ? (millis() > 5000UL)
                                  : (millis() - lastWeather > period);
    if (due) {
      lastWeather = millis();
      if (netOnline()) {
        handleTouch();
        fetchWeather();
        gLastDraw = 0;                // показать новые данные
      }
    }
  }

  // 5. Перерисовка — по СИГНАТУРЕ видимых данных, а не вслепую раз в 30 с.
  if (gScreen == SCR_MAIN) {
    uint32_t sig = screenSig();
    if (gLastDraw == 0 || sig != gLastSig || millis() - gLastDraw > 60000UL) {
      gLastDraw = millis();
      refreshMain();
      gLastSig = screenSig();
      gLastWinFull = millis();
    }
  } else {
    // раз в 5 минут — полная перерисовка окна (чтобы обновился график),
    // в остальное время перерисовываем только цифры
    if (millis() - gLastWinFull > 300000UL) {
      gLastWinFull = millis();
      gNoClear = false;
      gSkipChart = false;
      drawScreen();
      gLastSig = screenSig();
    } else if (screenSig() != gLastSig) {
      gLastSig = screenSig();
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
      if (n < 0 || n > 6) n = -1;
      doShot(n);
    } else if (up == "CSV") {
      printCsvTail();
    } else if (up == "CSVDAY") {
      printCsvDay();
    } else if (up == "CSVT") {
      printCsvT();
    } else if (up == "CSVHIST") {
      gHistWant = true; gHistForce = true; gHistStep = 0; gHistRetry = 0;
      Serial.println("история: запускаю восстановление из облака");
    } else if (up == "CSVH") {
      printCsvHist();
    } else if (up == "WCLR") {
      clearWeather();
    } else if (up == "RAW") {
      printRaw();
    } else if (up == "WD") {
      printWd();
    } else if (up == "DIAG") {
      printDiag();
    } else if (up == "NET") {
      Serial.printf("NET wifi=%d(%d ДБ) ip=%s internet=%s forceOff=%d probe=%lus ago\n",
                    WiFi.status(), gRssi, WiFi.localIP().toString().c_str(),
                    gNetOk ? "есть" : "НЕТ",
                    gNetForceOff ? 1 : 0,
                    (unsigned long)(gNetProbeMs ? (millis() - gNetProbeMs) / 1000 : 0));
    } else if (up == "NETOFF") {
      gNetForceOff = true; gNetOk = false;
      Serial.println("NET принудительно БЕЗ интернета — облако и погода отключены");
    } else if (up == "NETON") {
      gNetForceOff = false; gNetProbeMs = 0;
      Serial.println("NET проверяем интернет заново");
    } else if (up == "WIFIOFF") {
      gWifiOff = true; gNetOk = false;
      WiFi.disconnect(true, false);
      Serial.println("WiFi ВЫКЛЮЧЕН — проверяем запись на карту без сети");
    } else if (up == "WIFION") {
      gWifiOff = false; gNetProbeMs = 0;
      bool okw = connectWifi(15000);
      Serial.printf("WiFi ВКЛЮЧЁН: %s\n", okw ? "подключился" : "нет связи");
    } else if (up == "PERIODRESET") {
    // Сброс базы месяца/вчера/сегодня в памяти платы.
    // Нужен после исправлений расчёта: иначе висят старые закэшированные цифры.
    { Preferences p; p.begin("periods", false); p.clear(); p.end(); }
    { Preferences p; p.begin("today",   false); p.clear(); p.end(); }
    mMonthStamp = -2; mBaseKwh = -1; prevMonthE = -1;
    yDayKey = -2; yKwh = 0; yCost = 0; mKwh = 0; mCost = 0;
    dayStamp = -1; dayStartKwh = -1; myTodayKwh = 0; myTodayCost = 0;
    prevEnergyVal = -1;
    Serial.println("PERIOD-RESET: базы сброшены, пересчитаю заново");
  } else if (up == "CSVHEAD") {
    // Первые строки файла и первая строка текущего месяца — чтобы видеть базу
    if (!gSdOk) { Serial.println("CSV-ERR нет карты"); }
    else {
      File f = SD.open("/energy.csv", FILE_READ);
      if (!f) { Serial.println("CSV-ERR файл"); }
      else {
        Serial.println("CSVHEAD: начало файла");
        for (int i = 0; i < 3 && f.available(); i++)
          Serial.println("  " + f.readStringUntil('\n'));
        f.seek(0);
        char pre[8]; snprintf(pre, sizeof(pre), "2026-10");
        String first;
        while (f.available()) {
          String l = f.readStringUntil('\n');
          if (l.startsWith(pre)) { first = l; break; }
        }
        Serial.println("CSVHEAD: первая строка месяца " + first);
        f.close();
      }
    }
  } else if (up == "DEMO") {
      // Проверка вёрстки ИТОГОВ: 123.45 кВт*ч и 1234.56 руб.
      gDemoToday = !gDemoToday;
      Serial.printf("DEMO %s\n", gDemoToday ? "ВКЛ 123.45/1234.56" : "выкл");
      gLastWinFull = 0;
      gLastDraw = 0;
      if (gScreen == SCR_TODAY) { drawScreen(); gLastSig = screenSig(); }
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
  delay(5);          // 30 мс → 5 мс: цикл чаще, тач и сеть отзывчивее
}
