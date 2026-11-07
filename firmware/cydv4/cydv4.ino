// ============================================================================
//  ДОМ ЭНЕРГИЯ 2.0 — прошивка для ESP32-2432S028 (экран 2.8" 240x320)
//
//  Экраны:
//    1. ЭНЕРГИЯ   — напряжение, температура, мощность, расход, стоимость
//    2. ГРАФИК    — напряжение за последние 30 минут
//    3. ПОГОДА    — Адлер: температура, ветер, влажность (open-meteo.com)
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
#include <StreamString.h>   // тело ответа с chunked -> String (прогноз OWM)
#include <ESPmDNS.h>
#include <Update.h>
#include <SPI.h>
#include <SD.h>

static void tarForEpoch(long a, float* day, float* night, uint8_t* h1, uint8_t* h2);

// --- структура дня для /range (объявлена до функций: иначе автопрототипы её не видят)
#define RNG_MAX 31
struct RngDay {
  uint32_t day;
  float kwh, cost, dayKwh, nightKwh;
  uint16_t vmin, vmax, pmax;
  int16_t tmin, tmax;
  uint16_t rows;
};

#include <time.h>
#include <ArduinoJson.h>

#include "secrets.h"      // MQTT и ключ OpenWeatherMap (OWM_KEY)

// ФОРВАРД-ОБЪЯВЛЕНИЯ. Arduino-препроцессор генерирует прототипы сам, но с
// большим raw-строковым шаблоном страницы он перестаёт это делать — тогда
// компилятор ругается «was not declared in this scope» на функции ниже.
// Объявляем явно, до первого использования.
static inline uint8_t pkdBucket(float w);
static long fileFirstEpoch(File& f, size_t maxBytes);
static void monthStampOfEpoch(long t, int* stamp);
static void historyName(int stamp, char* out, size_t n);
static long todayDayKey();
#include "font5x7.h"

// ---- GAPFILL: пределы и типы -----------------------------------------------
// Объявлены в самом начале файла, потому что Arduino вставляет прототипы
// функций перед первой функцией — типы должны быть известны уже там.
#define GF_SCOPE_DAYS 2        // какие дни смотрим (сегодня и вчера)
#define GF_MAX_GAPS   20       // больше не берём: лучше доделать в следующий раз
#define GF_MIN_GAP    120L     // < 2 минут — не пропуск, запись просто сдвинулась
#define GF_MAX_GAP    21600L   // 6 ч: больше облако не отдаёт (хранит ~5 суток)
                              // а облако за 6 дней эти данные реально отдаёт
#define GF_AGE        300L     // пропуск должен быть старше 5 минут
#define GF_WIN        1200L    // окно запроса, с (100 записей × 16 с ≈ 27 мин)
#define GF_MAX_WIN    24     // 24 окна; 40 не нужно, память дороже
#define GF_POOL       420     // 420 = ~7 ч; 1300 съедало 12 КБ и ломало TLS
#define GF_MAX_EN     48       // записей счётчика на пропуск (по окну на окно)
#define GF_MAX_TMP    64       // записей температуры на пропуск
#define GF_TEMP_NEAR  300L     // насколько близко искать температуру, с

enum { GF_IDLE = 0, GF_SCAN, GF_FETCH, GF_COMMIT };

struct GapRow {               // одна вставляемая строка
  uint32_t t;                 // время, epoch (местное)
  uint16_t v;                 // напряжение, В
  uint16_t p;                 // мощность, Вт
  int32_t  e;                 // счётчик, сотые кВт·ч
  int16_t  tc;                // температура, десятые °C (INT16_MIN — нет данных)
};

struct GapItem {
  long     a, b;              // последняя строка ДО и первая ПОСЛЕ (epoch)
  int32_t  ea, eb;            // их счётчики, сотые кВт·ч
  uint32_t line;              // номер строки, после которой вставлять
  uint16_t row0, rowN;        // место в пуле строк
  uint16_t w, wN;             // текущее окно и сколько всего окон
  int16_t  srcE;              // сколько строк получили счётчик из облака
  uint8_t  st;                // 0 новый, 1 качаем, 2 готов
};

struct GapRun {
  uint8_t  phase;
  uint8_t  run;               // 1 = заполнять, 0 = только показать
  uint8_t  pending;           // 1 = отложенный запуск после старта
  int      gaps;              // найдено пропусков
  long     gapSec;            // их суммарная длительность
  int      gi;                // текущий пропуск
  int      poolN;             // занято строк в пуле
  int      nEn, nTmp;         // записей счётчика/температуры текущего пропуска
  int      inserted;          // вставлено в последнем запуске
  int      insTotal;          // вставлено всего за сеанс
int      insLast;           // вставлено за последний проход (видно в /gapfill)
  int      retry;             // подряд неудачных запросов
  int      todayN, todayMin;  // пропуски СЕГОДНЯ (для экрана ИТОГИ и /data)
  int      yestN, yestMin;
  long     tzOff;             // местное минус UTC, с (для запросов в облако)
  unsigned long nextMs, started;
  char     msg[40];
};

int rndRad(int w, int h);
// сторож объявлен ниже, а нижняя полоса рисует его состояние
extern int      gWdBad;
extern bool     wdAlive[];
extern bool     wdChecked[];
extern uint16_t wdMs[];
void updatePeriods();
void dispApply();                 // подсветка: определена ниже (ШИМ)
#include <esp_task_wdt.h>
#include <esp_system.h>       // esp_reset_reason(): причина последней перезагрузки
#include "page_html.h"   // шаблон страницы (генерируется build_page.py)

#include <mbedtls/gcm.h>
#include <mbedtls/md.h>

// ---------------------------------------------------------------- настройки
const char* FW_VER = "4.11.0";

// ---------------------------------------------------------------- секреты
// Пароли и ключи НЕ хранятся в репозитории. Скопируйте secrets.example.h
// в secrets.h (он в .gitignore) и заполните своими значениями:
// Wi-Fi, ключи Tuya, IP реле и ПК, ключ OpenWeatherMap, координаты города.
#include "secrets.h"

const char* DEF_SSID  = WIFI_SSID;
const char* DEF_PASS  = WIFI_PASS;
const char* DEVICE_ID = TUYA_DEVICE_ID;
const char* LOCAL_KEY = TUYA_LOCAL_KEY;
const char* PC_API    = PC_API_URL;

const uint16_t RELAY_PORT = 6668;

// ---- Tuya Cloud: даёт напряжение, мощность и счётчик без компьютера ----
const char* TUYA_CID  = TUYA_CLIENT_ID;
const char* TUYA_HOST = "openapi.tuyaeu.com";

// ---- Погода: OpenWeatherMap ----
// Координаты города — в secrets.h (WEATHER_LAT / WEATHER_LON), строками.
// units=metric -> °C и м/с, lang=ru -> русское описание.
const char* WEATHER_URL =
  "https://api.openweathermap.org/data/2.5/weather?lat=" WEATHER_LAT "&lon=" WEATHER_LON
  "&appid=" OWM_KEY "&units=metric&lang=ru";

// Прогноз на 48 ч: 16 шагов по 3 ч. Из него берём вероятность дождя сейчас
// (list[0].pop) и «завтра» по МЕСТНОМУ времени платы: max pop, min/max temp.
// Ответ ~7 КБ (с фильтром ArduinoJson разбираем в ~1 КБ).
const char* FORECAST_URL =
  "https://api.openweathermap.org/data/2.5/forecast?lat=" WEATHER_LAT "&lon=" WEATHER_LON
  "&appid=" OWM_KEY "&units=metric&lang=ru&cnt=10";

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
int gChartHour = -1;
int gCalStep = 0;
bool gTapTest = false;   // экран тач-теста: видно, куда попадает палец         // 0 — нет калибровки, 1 — ждём левый верх, 2 — правый низ
uint16_t gCalR[4][2];   // сырые координаты четырёх угловых касаний
int gCalGood[4];      // 1 — угол принят      // выбранный нажатием час графика, -1 = текущий
int gPwrFillHour = -1;    // <0 простой, иначе какой час заполняем
// Какой час уже заполнен «до упора»: без этого после догона текущего часа
// gPwrFillHour прижимался к nowH, и каждый проход loop() снова и снова
// ходил в облако за тем же часом — 3 TLS-запроса за 3 прохода, бесконечно.
// Облако отвечало «log query is too frequent», и заодно отваливались
// cloudStatus/погода: напряжение не обновлялось (vAge = аптайм).
int gPwrFillStopH = -1;
int gPwrPart = 0;         // какое 20-минутное окно часа берём (0,1,2)
float gPwrSum = 0;        // сумма мощностей за час
int gPwrN = 0;            // сколько записей
int gPwrPeak = 0;         // пик за час
int gPwrPeakSec = 0;      // секунда внутри часа, когда был пик    // <0 — простой, иначе какой час заполняем из облака
int gTapRawX = 0, gTapRawY = 0, gTapMapX = 0, gTapMapY = 0;  // диагностика тача
// Журнал последних 8 касаний: нажал когда угодно, прочитать можно потом
uint16_t gTLraw[8][2];
int16_t  gTLmap[8][2];
uint32_t gTLsec[8];
int      gTLn = 0;
unsigned long gLastDraw = 0;
unsigned long gDrawN = 0;          // счётчик вызовов drawScreen() — для замера моргания
unsigned long gWinRefreshN = 0;    // счётчик точечных обновлений окна
uint32_t gLastSig = 0;          // отпечаток данных, по нему решаем, нужна ли перерисовка
unsigned long gLastWinFull = 0; // когда окно перерисовывали целиком (для графика)
unsigned long gLastWinRefresh = 0; // когда окно обновляли точечно (антиморгание)
unsigned long gTouchMs = 0;     // когда был последний тап (облако не лезет в этот момент)
bool gNoClear = false;          // не заливать весь экран — частичное обновление
bool gSkipChart = false;        // не перерисовывать график 24 ч (точечное обновление)
bool gDayLoading = false;       // идёт чтение прошлого дня с карты: экран не трогаем
bool gDemoToday = false;        // DEMO: ИТОГИ с широкими числами (проверка вёрстки)
unsigned long gTouchSkip = 0;   // до этого времени не начинать сетевые операции

// ---- замер затупаний: сколько длится один проход loop() ----------------------
// loopMaxMs — максимум за последнее окно 60 с (по нему и проверяем «не тупит ли»);
// loopMaxAll — максимум с момента загрузки/сброса (LOOPRESET). Цель — ≤ 350 мс.
unsigned long gLoopLast   = 0;   // длительность последнего прохода
unsigned long gLoopMaxWin = 0;   // максимум в текущем окне 60 с
unsigned long gLoopMaxAll = 0;   // максимум с загрузки
unsigned long gLoopWinAt  = 0;   // начало текущего окна
// Кто именно держал цикл дольше всех — имя места и его длительность.
// Без этого loopMaxAll ничего не объясняет: видно, что 3.4 с, а где — нет.
const char*   gBlkName = "-";
unsigned long gBlkMs   = 0;
inline void blkNote(const char* n, unsigned long d) {
  if (d > gBlkMs) { gBlkMs = d; gBlkName = n; }
}
// длительности отдельных тяжёлых мест — чтобы видеть, кто именно виноват
unsigned long gMsSd   = 0;       // последний scanSdRange() из updateToday()
unsigned long gMsGap  = 0;       // последний gapScan()
unsigned long gMsHist = 0;       // последний histScan()
unsigned long gMsCloud = 0;      // последний cloudStatus() из loop()
unsigned long gMsTuya  = 0;      // последний tuyaQuery() из loop()
unsigned long gMsWrite = 0;      // последняя запись строки на карту
unsigned long gApiN    = 0;      // сколько раз пробовали fetchFromApi()
unsigned long gApiLast = 0;      // когда пробовали в последний раз

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
  dispApply();                              // подсветка: ШИМ по сохранённой яркости

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

// ---- тарифы: ночь 23:00-06:00 (значения меняются из журнала тарифов)
float TARIFF_DAY   = 8.39f;
float TARIFF_NIGHT = 4.49f;

// границы дневного окна текущего тарифа (по умолчанию 06:00-23:00)
int gTarH1 = 6, gTarH2 = 23;

// ---------------------------------------------------------------- журнал тарифов
// Запись: с какой даты действует, цена день, цена ночь, границы дневного окна.
// Старые записи не пересчитываются: стоимость уже заморожена в energy.csv (колонка cost),
// а для периодов берётся тариф, действовавший в тот день.
#define TAR_N 12
struct TarRec { char from[11]; float day, night; uint8_t h1, h2; };
static TarRec sTar[TAR_N];
static uint8_t sTarCnt = 0;
static char sTarToday[11] = "";      // какой тариф применён сейчас

static void tarDump(String& out) {
  out = "";
  for (uint8_t i = 0; i < sTarCnt; i++) {
    if (i) out += ';';
    char b[48];
    snprintf(b, sizeof(b), "%s,%.2f,%.2f,%u,%u", sTar[i].from, sTar[i].day, sTar[i].night,
             (unsigned)sTar[i].h1, (unsigned)sTar[i].h2);
    out += b;
  }
}

static void tarParse(const String& src) {
  sTarCnt = 0;
  int start = 0;
  while (start < (int)src.length() && sTarCnt < TAR_N) {
    int semi = src.indexOf(';', start);
    if (semi < 0) semi = src.length();
    String one = src.substring(start, semi);
    start = semi + 1;
    one.trim();
    if (one.length() < 10) continue;
    int c1 = one.indexOf(',', 0);
    int c2 = one.indexOf(',', c1 + 1);
    int c3 = one.indexOf(',', c2 + 1);
    int c4 = one.indexOf(',', c3 + 1);
    if (c1 < 0 || c2 < 0) continue;
    String d = one.substring(0, c1);
    d.trim();
    if (d.length() != 10) continue;
    strncpy(sTar[sTarCnt].from, d.c_str(), 10);
    sTar[sTarCnt].from[10] = 0;
    sTar[sTarCnt].day = one.substring(c1 + 1, c2).toFloat();
    if (sTar[sTarCnt].day <= 0) continue;
    sTar[sTarCnt].night = (c2 > 0 && c3 > 0) ? one.substring(c2 + 1, c3).toFloat() : sTar[sTarCnt].day;
    sTar[sTarCnt].h1 = (c3 > 0 && c4 > 0) ? (uint8_t)constrain(one.substring(c3 + 1, c4).toInt(), 0, 23) : 6;
    sTar[sTarCnt].h2 = (c4 > 0) ? (uint8_t)constrain(one.substring(c4 + 1).toInt(), 1, 24) : 23;
    sTarCnt++;
  }
}

static void tarSave() {
  String s;
  tarDump(s);
  Preferences p;
  if (p.begin("tar", false)) { p.putString("log", s); p.end(); }
}

static void tarLoad() {
  Preferences p;
  if (!p.begin("tar", true)) return;
  String s = p.getString("log", "");
  p.end();
  if (s.length()) tarParse(s);
}

// последний тариф, чья дата начала не позже указанной
// (при одинаковой дате побеждает последняя запись — новая цена)
static bool tarPick(const char* date, float* day, float* night, uint8_t* h1, uint8_t* h2) {
  const TarRec* best = nullptr;
  for (uint8_t i = 0; i < sTarCnt; i++) {
    if (strcmp(sTar[i].from, date) <= 0) {
      if (!best || strcmp(sTar[i].from, best->from) >= 0) best = &sTar[i];
    }
  }
  if (!best) return false;
  *day = best->day; *night = best->night;
  if (h1) *h1 = best->h1;
  if (h2) *h2 = best->h2;
  return true;
}

// применить тариф на сегодня (вызывается на старте и при смене суток)
static void tarApplyToday() {
  struct tm ti;
  if (!getLocalTime(&ti, 20)) return;
  char d[11];
  snprintf(d, sizeof(d), "%04d-%02d-%02d", ti.tm_year + 1900, ti.tm_mon + 1, ti.tm_mday);
  if (!strcmp(d, sTarToday)) return;                 // уже применён
  float day = TARIFF_DAY, night = TARIFF_NIGHT;
  uint8_t h1 = gTarH1, h2 = gTarH2;
  if (tarPick(d, &day, &night, &h1, &h2)) {
    TARIFF_DAY = day; TARIFF_NIGHT = night;
    gTarH1 = h1; gTarH2 = h2;
  }
  strncpy(sTarToday, d, 10); sTarToday[10] = 0;
  Serial.printf("тариф на %s: день %.2f, ночь %.2f (записей %u)\n",
                d, TARIFF_DAY, TARIFF_NIGHT, (unsigned)sTarCnt);
}

// первая запись: если журнал пуст — заводим текущий тариф на сегодня
static void tarSeed() {
  if (sTarCnt) return;
  struct tm ti;
  if (!getLocalTime(&ti, 50)) return;
  snprintf(sTar[sTarCnt].from, sizeof(sTar[sTarCnt].from), "%04d-%02d-%02d",
           ti.tm_year + 1900, ti.tm_mon + 1, ti.tm_mday);
  sTar[sTarCnt].day = TARIFF_DAY;
  sTar[sTarCnt].night = TARIFF_NIGHT;
  sTar[sTarCnt].h1 = 6; sTar[sTarCnt].h2 = 23;
  sTarCnt++;
  tarSave();
  Serial.println("тариф: журнал создан");
}

// нормы напряжения, ГОСТ 32144-2013
const float V_NORM_LO = 207.0f, V_NORM_HI = 253.0f;
const float V_WARN_LO = 190.0f, V_WARN_HI = 265.0f;

float tariffForHour(int h) {
  return (h >= gTarH2 || h < gTarH1) ? TARIFF_NIGHT : TARIFF_DAY;
}

// средний тариф для интервала [a,b) с заданными ценами и окном
float tariffAvgD(long a, long b, float day, float night, int h1, int h2) {
  long nStart = (long)h2 * 3600L;      // начало ночи
  long nEnd   = (long)h1 * 3600L;      // конец ночи
  if (b <= a) {
    long sod = ((a % 86400L) + 86400L) % 86400L;
    return (sod >= nStart || sod < nEnd) ? night : day;
  }
  long total = b - a;
  long stop = b;
  if (total > 3L * 86400L) stop = a + 3L * 86400L;
  long nightSec = 0;
  long s = a;
  int guard = 0;
  while (s < stop && guard++ < 128) {
    long sod = ((s % 86400L) + 86400L) % 86400L;
    long next;
    if (sod < nEnd)              next = s + (nEnd - sod);          // до конца ночи
    else if (sod < nStart)       next = s + (nStart - sod);        // до начала ночи
    else                         next = s + (86400L - sod);        // до полуночи
    if (next > stop) next = stop;
    if (sod >= nStart || sod < nEnd) nightSec += next - s;
    s = next;
  }
  if (total > 3L * 86400L && stop > a)
    nightSec = (long)((double)nightSec * (double)total / (double)(stop - a));
  double nf = total > 0 ? (double)nightSec / (double)total : 0;
  return (float)(nf * night + (1.0 - nf) * day);
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
float   h24Pm[24];        // максимальная мощность за каждый час (из облака)
bool    h24PmOk[24];      // час с известным пиком
uint8_t h24PmMin[24];     // минута пика внутри часа
uint8_t h24Mask[H24_N];

// ---- выбранный день для графиков «по часам» (стрелки ‹ ›) ----
// 0 = сегодня: график рисуем из живых h24V/h24T/h24P (их ведёт statsTick).
// 1 = вчера, 2 = позавчера... Для прошлых дней сутки читаются с карты в
// отдельный буфер day* (≈3.7 КБ RAM). resetStats() при листании НЕ зовём:
// иначе стирается статистика сегодняшнего дня.
int      gDayOffset   = 0;      // сдвиг назад от сегодня
int      gDayMaxOffset = 30;    // сколько дней назад вообще есть строки
char     gDayLabel[8] = "";     // «29.10» — подпись выбранного дня
int      gDayLoaded   = -1;     // какой сдвиг сейчас в буфере, -1 = пусто
int      gDayRows     = 0;      // сколько строк дня прочитали
unsigned long gDayMs  = 0;      // сколько заняло чтение дня, мс
float    dayV[H24_N], dayT[H24_N], dayP[H24_N];
uint8_t  dayMask[H24_N];
// Сводка за выбранный прошлый день (для окон МИН/СРЕД/МАКС/ПИК). Взвешиваем
// каждую строку одинаково (~30 с между строками), для показа этого достаточно.
float  ddMinV = 999, ddMaxV = 0, ddOutV = 0;
double ddSumV = 0; long ddSecV = 0;
float  ddMinT = 999, ddMaxT = -999;
double ddSumT = 0; long ddSecT = 0;
float  ddMaxP = 0;
double ddSumP = 0; long ddSecP = 0;
int    ddMinVMin = -1, ddMaxVMin = -1, ddMinTMin = -1, ddMaxTMin = -1, ddPMin = -1;
// Гистограмма мощности дня — чтобы окно МОЩНОСТЬ показало ДЛИТЕЛЬНОСТЬ пика
// («ПИК 1840 Вт 14:23 (3 мин)»). В корзинах СЕКУНДЫ: строки CSV дают 30 с,
// живые отсчёты recordStats() — свой dt. Тип корзин — pkdBucket()/pkDurSec().
uint32_t gPkHist[33];

// GAPFILL: пока идёт подмена /energy.csv, writeSd() новые строки не пишет —
// иначе файл вырастет прямо под нами и перезапись отменится.
bool gGapBusy = false;

// ---- GAPFILL: состояние (типы и пределы — в начале файла) ----
static GapRun  sGF;
// ---- GAPFILL: с каким файлом работаем и за какой период ----
// По умолчанию — /energy.csv и последние 2 суток. Командой можно задать
// архив прошлого месяца (energy-YYYY-MM.csv) и точный диапазон дат: облако
// Tuya хранит 7 суток, и 25–28.10 уже лежат только в архиве.
static char  gGapPath[40] = "/energy.csv";
static int   gGapDays  = 0;          // 0 = GF_SCOPE_DAYS
static long  gGapFromE = 0;          // начало диапазона (epoch), 0 = не задан
static long  gGapToE   = 0;          // конец диапазона включительно
static GapItem sGf[GF_MAX_GAPS];
static GapRow  sGfRow[GF_POOL];
static struct { uint32_t t; int32_t e; } sGfEn[GF_MAX_EN];
static struct { uint32_t t; int16_t v; } sGfTmp[GF_MAX_TMP];

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
  for (int i = 0; i < 24; i++) { h24Pm[i] = 0; h24PmOk[i] = false; h24PmMin[i] = 0; }
  memset(gPkHist, 0, sizeof(gPkHist));       // длительность пика — с чистого листа
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
    // Длительность пика СЕГОДНЯ: живые отсчёты добавляются сюда же, иначе
    // гистограмма кончалась бы моментом загрузки (seedStatsFromSd).
    if (gPower > 0) gPkHist[pkdBucket(gPower)] += (uint32_t)dt;
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
    gPwrFillHour = 0;   // график мощности нового дня заполняем заново
    gPwrFillStopH = -1;
    gPwrPart = 0;
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
    unsigned long sdT0 = millis();
    bool sdOk1 = scanSdRange(start, 0, gEnergy, &k1, &k2, &dK, &nK);
    gMsSd = millis() - sdT0;
    if (sdOk1) {
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
uint8_t gRelayFailRun = 0;    // неудач опроса реле ПОДРЯД (для светодиода)
unsigned long gPhaseMs = 0;   // когда последний раз пришло напряжение от реле
unsigned long gTempMs = 0;    // когда последний раз пришла температура
unsigned long gPowerMs = 0;   // когда последний раз пришла мощность
unsigned long gEnergyMs = 0;  // когда последний раз пришёл счётчик
unsigned long gPhaseN = 0;     // всего обновлений напряжения
unsigned long gPhaseRelayN = 0;// из них от локального реле (остальное — облако)

// Возраст напряжения ОДНИМ правилом на всю программу. gPhaseMs == 0 значит
// «ещё ни разу не приходило», и тогда возраст = аптайм (иначе (millis()-0)
// даёт именно аптайм, и «vAge = 2 ч» выглядит как застывшее напряжение,
// хотя это просто «данных не было ни разу с загрузки»).
unsigned long voltAgeMs() { return gPhaseMs ? (millis() - gPhaseMs) : millis(); }
// Устарело ли напряжение сильнее lim мс — приоритет сети/облака.
bool voltStale(unsigned long lim) { return voltAgeMs() > lim; }
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
String wDescEn = "";     // английское (weather[0].main) — только для иконки
String wDescRu = "";     // русское (weather[0].description) — на экран
uint32_t wHeap = 0;      // heap сразу после последнего запроса — для /data
unsigned long wLast = 0;
time_t wTime = 0;
bool  wOk = false;

// ---- прогноз OWM /forecast: вероятность дождя и завтрашний день ----------
float    wPopNow = 0;      // вероятность дождя сейчас, % (list[0].pop * 100)
float    wPopTom = 0;      // максимум вероятности дождя на завтра, %
float    wTminTom = 0;     // минимум температуры завтра, °C
float    wTmaxTom = 0;     // максимум температуры завтра, °C
float    wRainTom = 0;     // сумма осадков за завтра, мм (rain.3h, если есть)
String   wDescTom = "";    // описание завтра, короткое русское (5x7)
int      wCodeTom = 0;     // код погоды завтра (самый «плохой» из шагов, 0 = нет)
uint32_t wForecastAt = 0;  // millis() успешного прогноза
uint32_t wfHeap = 0;       // heap после запроса прогноза — для /data
bool     wFcOk = false;    // прогноз получен хотя бы раз
uint16_t wFcFail = 0;      // неудач подряд (по нему ограничиваем повторы)

bool gSdOk = false;
uint16_t gSdWriteFail = 0;   // неудач записи на карту ПОДРЯД (для светодиода)
int  gSdRows = 0;
int  gSdMonth = -1;      // месяц рабочего файла: year*12 + (month-1)
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
static int sRecCap = 0;          // сколько записей реально выделено

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

// ============================================================================
//  /range — статистика за период: по дням, с делением день/ночь
// ----------------------------------------------------------------------------
//  Стоимость берётся из колонки cost в energy.csv (она пишется в момент записи,
//  поэтому старые дни сохраняют старый тариф). День/ночь делим по времени.
// ============================================================================

static long dateEpoch(const char* s) {
  int y = 0, mo = 0, d = 0;
  if (sscanf(s, "%4d-%2d-%2d", &y, &mo, &d) != 3) return 0;
  if (y < 2020 || y > 2100 || mo < 1 || mo > 12 || d < 1 || d > 31) return 0;
  return daysFromCivil(y, (unsigned)mo, (unsigned)d) * 86400L;
}

static String dateFromEpoch(long t) {
  long z = t / 86400L;
  int y = 0, mo = 0, d = 0;
  civilFromDays(z, &y, &mo, &d);
  char b[16];
  snprintf(b, sizeof(b), "%04d-%02d-%02d", y, mo, d);
  return String(b);
}

// доля ночных секунд в интервале [a,b) при окне «день с h1 до h2»
static float nightFrac(long a, long b, int h1, int h2) {
  if (b <= a) return 0;
  long nStart = (long)h2 * 3600L, nEnd = (long)h1 * 3600L;
  long total = b - a, nightSec = 0, s = a;
  int guard = 0;
  while (s < b && guard++ < 64) {
    long sod = ((s % 86400L) + 86400L) % 86400L;
    long next;
    if (sod < nEnd)        next = s + (nEnd - sod);
    else if (sod < nStart) next = s + (nStart - sod);
    else                   next = s + (86400L - sod);
    if (next > b) next = b;
    if (sod >= nStart || sod < nEnd) nightSec += next - s;
    s = next;
  }
  return (float)nightSec / (float)total;
}

// разбор строки CSV: дата-время, вольт, ватт, кВт·ч, реле, стоимость, темп
static bool csvRow(const String& l, long* t, float* v, float* p, float* e, float* c, float* tm) {
  if (l.length() < 24) return false;
  long ep = lineEpoch(l);
  if (!ep) return false;
  const char* s = l.c_str();
  const char* q = s;
  int comma = 0;
  float vals[7] = {0, 0, 0, 0, 0, 0, 0};
  while (*q && comma < 7) {
    if (*q == ',') {
      comma++;
      if (comma < 7) vals[comma] = atof(q + 1);
    }
    q++;
  }
  if (comma < 5) return false;
  *t = ep; *v = vals[1]; *p = vals[2]; *e = vals[3]; *c = vals[5];
  *tm = (comma >= 6) ? vals[6] : -999.0f;   // температуры может не быть в старой строке
  return true;
}

static int dayIndexOf(RngDay* arr, int n, uint32_t d) {
  for (int i = 0; i < n; i++) if (arr[i].day == d) return i;
  return -1;
}

static void rangeReset(RngDay* a, int n) {
  for (int i = 0; i < n; i++) {
    a[i].kwh = a[i].cost = a[i].dayKwh = a[i].nightKwh = 0;
    a[i].vmin = 0xFFFF; a[i].vmax = 0; a[i].pmax = 0;
    a[i].tmin = 999; a[i].tmax = -999; a[i].rows = 0;
  }
}

// пройти файл месяца и накопить в buckets
static void rangeScanFile(const char* path, long fromT, long toT, RngDay* b, int nb) {
  File f = SD.open(path, FILE_READ);
  if (!f) return;
  // читаем БЛОКАМИ и режем на строки: readStringUntil() читает по байту и на 9000
  // строк уходило 15 секунд, из-за чего страница ждала период целую вечность
  static char buf[1400];
  int len = 0;
  long pt = 0; float pe = 0, pc = 0;
  bool havePrev = false;
  int guard = 0;

  auto processLine = [&](char* s) {
    int sl = (int)strlen(s);
    if (sl < 24) return;
    long rt; float v, p, e, c, tm;
    String line(s);
    if (!csvRow(line, &rt, &v, &p, &e, &c, &tm)) return;
    if (rt < fromT - 86400L || rt > toT + 86400L) { havePrev = false; return; }
    if (e <= 0) return;                       // импорт/GAPFILL с нулём — пропускаем
    uint32_t day = (uint32_t)(rt / 86400L);
    int idx = dayIndexOf(b, nb, day);
    if (idx < 0) return;
    if (havePrev && (uint32_t)(pt / 86400L) != day) havePrev = false;   // не тянем через сутки
    if (havePrev && rt > pt && e >= pe) {
      float de = e - pe;
      if (de > 0 && de < 100) {
        b[idx].kwh += de;
        float dd, nn; uint8_t w1, w2;
        tarForEpoch(pt, &dd, &nn, &w1, &w2);
        float nf = nightFrac(pt, rt, (int)w1, (int)w2);
        b[idx].nightKwh += de * nf;
        b[idx].dayKwh += de * (1.0f - nf);
      }
    }
    if (v > 50) { if (v < b[idx].vmin) b[idx].vmin = (uint16_t)v; if (v > b[idx].vmax) b[idx].vmax = (uint16_t)v; }
    if (tm > 0.05f && tm < 120) {
      if (tm * 10 < b[idx].tmin) b[idx].tmin = (int16_t)(tm * 10);
      if (tm * 10 > b[idx].tmax) b[idx].tmax = (int16_t)(tm * 10);
    }
    if (p > b[idx].pmax) b[idx].pmax = (uint16_t)p;
    if (c > 0) b[idx].cost = c;               // накопленная за сутки — берём последнее
    b[idx].rows++;
    havePrev = true; pt = rt; pe = e; pc = c;
  };

  while (f.available()) {
    int r = f.read((uint8_t*)buf + len, (int)sizeof(buf) - 1 - len);
    if (r <= 0) break;
    len += r;
    int from = 0;
    for (int i = 0; i < len; i++) {
      if (buf[i] == '\n') {
        buf[i] = 0;
        processLine(buf + from);
        from = i + 1;
        if ((++guard & 255) == 0) { esp_task_wdt_reset(); yield(); }
      }
    }
    if (from > 0) { memmove(buf, buf + from, (size_t)(len - from)); len -= from; }
    if (len >= (int)sizeof(buf) - 1) len = 0;   // слишком длинная строка — выбрасываем
  }
  if (len > 0) { buf[len] = 0; processLine(buf); }
  f.close();
}



// Средний тариф на отрезке [a,b] с учётом ночных границ.
// тариф, действовавший в день начала интервала (для пересчёта старых суток)
static void tarForEpoch(long a, float* day, float* night, uint8_t* h1, uint8_t* h2) {
  *day = TARIFF_DAY; *night = TARIFF_NIGHT; *h1 = gTarH1; *h2 = gTarH2;
  time_t ts = (time_t)a;
  struct tm ti;
  localtime_r(&ts, &ti);
  char d[11];
  snprintf(d, sizeof(d), "%04d-%02d-%02d", ti.tm_year + 1900, ti.tm_mon + 1, ti.tm_mday);
  tarPick(d, day, night, h1, h2);
}

float tariffAvgOn(long a, long b) {
  float d, n; uint8_t h1, h2;
  tarForEpoch(a, &d, &n, &h1, &h2);
  return tariffAvgD(a, b, d, n, h1, h2);
}

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

// ---------------------------------------------------------------------------
//  Быстрый разбор строки CSV «дата,В,Вт,кВт·ч,реле,руб,°C» по индексам.
//  Раньше на каждой строке были readStringUntil + sscanf + четыре indexOf +
//  substring().toFloat() — по 6-8 объектов String на строку, ~2400 мс на сутки.
//  Теперь ни одного String и ни одного sscanf: только сравнение байтов.
// ---------------------------------------------------------------------------

// Эпоха из префикса "YYYY-MM-DD HH:MM:SS". 0 — не похоже на строку истории.
static long lineEpochRaw(const char* s, int len) {
  if (len < 19) return 0;
  if (s[4] != '-' || s[7] != '-' || s[10] != ' ' || s[13] != ':' || s[16] != ':') return 0;
  unsigned d0 = (unsigned)(s[0] - '0'), d1 = (unsigned)(s[1] - '0');
  unsigned d2 = (unsigned)(s[2] - '0'), d3 = (unsigned)(s[3] - '0');
  unsigned d4 = (unsigned)(s[5] - '0'), d5 = (unsigned)(s[6] - '0');
  unsigned d6 = (unsigned)(s[8] - '0'), d7 = (unsigned)(s[9] - '0');
  unsigned d8 = (unsigned)(s[11] - '0'), d9 = (unsigned)(s[12] - '0');
  unsigned dA = (unsigned)(s[14] - '0'), dB = (unsigned)(s[15] - '0');
  unsigned dC = (unsigned)(s[17] - '0'), dD = (unsigned)(s[18] - '0');
  if ((d0 | d1 | d2 | d3 | d4 | d5 | d6 | d7 | d8 | d9 | dA | dB | dC | dD) > 9) return 0;
  int y  = (int)(d0 * 1000 + d1 * 100 + d2 * 10 + d3);
  int mo = (int)(d4 * 10 + d5);
  int dd = (int)(d6 * 10 + d7);
  int hh = (int)(d8 * 10 + d9);
  int mi = (int)(dA * 10 + dB);
  int se = (int)(dC * 10 + dD);
  if (y < 2020 || y > 2100) return 0;
  if (mo < 1 || mo > 12 || dd < 1 || dd > 31) return 0;
  if (hh > 23 || mi > 59 || se > 59) return 0;
  return daysFromCivil(y, (unsigned)mo, (unsigned)dd) * 86400L
       + hh * 3600L + mi * 60L + se;
}

// Число из поля №want (0 = дата/время, 3 = энергия). false — поля нет/пустое.
static bool csvFieldFloat(const char* s, int len, int want, float* out) {
  const char* p = s;
  const char* end = s + len;
  for (int c = 0; c < want; c++) {
    while (p < end && *p != ',') p++;
    if (p >= end) return false;
    p++;
  }
  while (p < end && (*p == ' ' || *p == '\t')) p++;
  bool neg = false;
  if (p < end && *p == '-') { neg = true; p++; }
  long ip = 0;
  bool any = false;
  while (p < end && *p >= '0' && *p <= '9') { ip = ip * 10 + (*p - '0'); p++; any = true; }
  float f = (float)ip;
  if (p < end && *p == '.') {
    p++;
    float d = 0.1f;
    while (p < end && *p >= '0' && *p <= '9') { f += (float)(*p - '0') * d; d *= 0.1f; p++; any = true; }
  }
  if (!any) return false;
  *out = neg ? -f : f;
  return true;
}

// Расход и стоимость за [start, end). end=0 — до текущего показания.
// Кэш разбора: строки в /energy.csv только ДОПИСЫВАЮТСЯ, поэтому при
// повторном вызове с тем же диапазоном читаем лишь новые байты. Полный
// перечит — при смене диапазона, после перезаписи файла (gSdGen) и раз в
// 10 минут. Так ежеминутная проверка «сегодня» не читает 90 КБ заново.
static long   sScanStart = 0;
static long   sScanEnd   = 0;
static bool   sScanArch  = false;
static size_t sScanSize  = 0;
static int    sScanRecN  = 0;
static unsigned long sScanFullAt = 0;
static unsigned long sScanGen    = 0;

// Освобождает буфер истории при выходе из скана: держать 29 КБ постоянно нельзя —
// mbedTLS для облака ищет непрерывный блок и не находит.
struct RecGuard {
  ~RecGuard() {
    if (sRec) { free(sRec); sRec = nullptr; }
    sRecN = 0; sRecCap = 0;
    sScanRecN = 0; sScanStart = 0; sScanSize = 0;   // буфера нет — читаем заново
  }
};
unsigned long gSdGen = 0;          // меняется при перезаписи /energy.csv

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

  // Если запрошенный диапазон целиком лежит ДО первой строки рабочего файла
  // (например, «вчера» 1-го числа), читаем архив прошлого месяца.
  char path[40];
  strcpy(path, "/energy.csv");
  {
    File probe = SD.open(path, FILE_READ);
    if (!probe) return false;
    long firstT = fileFirstEpoch(probe, 4096);
    probe.close();
    if (firstT > 0 && rangeEnd <= firstT) {
      char arch[40];
      int stamp = 0;
      monthStampOfEpoch(start, &stamp);
      historyName(stamp, arch, sizeof(arch));
      if (SD.exists(arch)) strcpy(path, arch);
    }
  }

  File f = SD.open(path, FILE_READ);
  if (!f) return false;
  bool arch = (strcmp(path, "/energy.csv") != 0);
  long span = rangeEnd - start + 6L * 3600L;

  // Буфер держим только на время скана: размер по окну, освобождение — RecGuard.
  int needRows = (int)(span / 30L) + 300;
  if (needRows > SD_REC_MAX) needRows = SD_REC_MAX;
  if (needRows < 600) needRows = 600;
  if (!sRec) {
    sRec = (SdRec*)malloc(sizeof(SdRec) * needRows);
    if (!sRec) { Serial.println("карта: нет памяти под буфер истории"); return false; }
    sRecCap = needRows;
  }
  RecGuard recGuard;

  unsigned long need = (unsigned long)(span / 30L) * 52UL;
  if (need < 60000UL) need = 60000UL;
  if (need > 230000UL) need = 230000UL;
  size_t sz = f.size();
  size_t off = (sz > need) ? (sz - need) : 0;
  size_t from;
  bool full = !(sScanStart == start && sScanEnd == end && sScanArch == arch &&
                sScanRecN >= 2 && sScanGen == gSdGen &&
                millis() - sScanFullAt < 600000UL &&
                sz >= sScanSize && (sz - sScanSize) < 32768UL);
  if (full) {
    sRecN = 0;
    from = off;
    if (from > 0) {
      f.seek(from);
      f.readStringUntil('\n');        // первая строка может быть обрезана
      from = (size_t)f.position();
    }
  } else {
    from = sScanSize;                 // читаем только то, что дописалось
    f.seek(from);
  }

  long minT = start - 4L * 3600L;
  long maxT = rangeEnd + 4L * 3600L;
  int rd = 0;
  // Читаем БЛОКАМИ по 1024 байта и разбираем строку по индексам: без String,
  // без sscanf, без substring().toFloat(). Было ~2400 мс, стало ~300 мс.
  static uint8_t sbuf[1024 + 256];
  int carry = 0;
  while (f.available()) {
    int n = (int)f.read(sbuf + carry, 1024);
    if (n <= 0) break;
    int total = carry + n;
    int i = 0;
    while (i < total) {
      uint8_t* nl = (uint8_t*)memchr(sbuf + i, '\n', total - i);
      if (!nl) break;
      int len = (int)(nl - (sbuf + i));
      long t = lineEpochRaw((const char*)(sbuf + i), len);
      if (t != 0 && t >= minT && t <= maxT) {
        float e = 0;
        if (csvFieldFloat((const char*)(sbuf + i), len, 3, &e) && e > 0) {
          if (sRecN >= sRecCap) {
            // Переполнение: выбрасываем самую старую половину, новые строки важнее.
            // Иначе терялся конец окна и «вчера/сегодня» считались по обрезанным данным.
            int half = sRecCap / 2;
            memmove(sRec, sRec + half, sizeof(SdRec) * half);
            sRecN = half;
          }
          sRec[sRecN].t = (uint32_t)t;
          sRec[sRecN].e = e;
          sRecN++;
        }
      }
      i = (int)(nl - sbuf) + 1;
      if ((++rd & 1023) == 0) { esp_task_wdt_reset(); yield(); }
    }
    carry = total - i;
    if (carry > 0) memmove(sbuf, sbuf + i, carry);
    if (carry > 240) carry = 0;        // защита от строки без перевода строки
    esp_task_wdt_reset();
  }
  f.close();
  // Запоминаем, что именно разобрали: в следующий раз дочитаем только хвост.
  sScanStart = start;
  sScanEnd   = end;
  sScanArch  = arch;
  sScanSize  = sz;
  sScanRecN  = sRecN;
  sScanGen   = gSdGen;
  if (full) sScanFullAt = millis();
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
  sRecN = n;              // важно: дальше дописываем ровно с этого места
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
    float ta = tariffAvgOn(a, b);   // тариф ДНЯ записи
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
          float ta = tariffAvgOn(a, b);   // тариф ДНЯ записи
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

// ============================================================ РОТАЦИЯ ИСТОРИИ
// Рабочий файл /energy.csv — только текущий месяц. При смене месяца он
// переименовывается в /energy-YYYY-MM.csv, а новый начинается с заголовка.
// Старые месячные архивы (старше 3 месяцев) удаляются. Так файл не растёт
// бесконечно, sdMonthBase и scanSdRange остаются быстрыми, а «вчера» на
// 1-е число дочитывается из архива предыдущего месяца.

static long fileFirstEpoch(File& f, size_t maxBytes) {
  size_t sz = f.size();
  if (!sz) return 0;
  size_t lim = (sz < maxBytes) ? sz : maxBytes;
  f.seek(0);
  size_t used = 0;
  while (f.available() && used < lim) {
    String l = f.readStringUntil('\n');
    used += l.length() + 1;
    long t = lineEpoch(l);
    if (t) return t;
  }
  return 0;
}

static long fileLastEpoch(File& f) {
  size_t sz = f.size();
  if (sz < 20) return 0;
  size_t from = (sz > 400) ? sz - 400 : 0;
  f.seek(from);
  String last;
  while (f.available()) {
    String l = f.readStringUntil('\n');
    l.trim();
    if (l.length() > 10) last = l;
  }
  return lineEpoch(last);
}

static void monthStampOfEpoch(long t, int* stamp) {
  time_t tt = (time_t)t;
  struct tm st;
  gmtime_r(&tt, &st);
  *stamp = (st.tm_year + 1900) * 12 + st.tm_mon;
}

static void historyName(int stamp, char* out, size_t n) {
  int y = stamp / 12;
  int m = stamp % 12 + 1;
  snprintf(out, n, "/energy-%04d-%02d.csv", y, m);
}

// В КАКОМ ФАЙЛЕ ЛЕЖИТ ДАТА "YYYY-MM-DD": /energy.csv или архив прошлого месяца.
// 1 ноября рабочий файл начался заново, и весь октябрь (578 КБ в
// energy-2026-10.csv) стал невидим: /history?date=2026-10-31 отдавал 0 часов,
// /days знал только 2026-11-01. В scanSdRange этот откат был, а здесь — нет.
static void histPathForDate(const char* date, char* out, size_t n) {
  snprintf(out, n, "/energy.csv");
  if (!date || strlen(date) < 7) return;
  int y = (date[0] - '0') * 1000 + (date[1] - '0') * 100 + (date[2] - '0') * 10 + (date[3] - '0');
  int m = (date[5] - '0') * 10 + (date[6] - '0');
  if (y < 2020 || m < 1 || m > 12) return;
  int stamp = y * 12 + (m - 1);
  if (gSdMonth >= 0 && stamp >= gSdMonth) return;   // это рабочий месяц
  char arch[40];
  historyName(stamp, arch, sizeof(arch));
  if (SD.exists(arch)) snprintf(out, n, "%s", arch);
}

// Сколько дней назад уходит стрелка ‹: по САМОМУ СТАРОМУ архиву на карте,
// иначе после 1-го числа она упирается в 0 (в /energy.csv только ноябрь).
static int histOldestBack() {
  int best = -1;
  File dir = SD.open("/");
  if (dir) {
    File e = dir.openNextFile();
    while (e) {
      if (!e.isDirectory()) {
        String n = e.name();
        int slash = n.lastIndexOf('/');
        if (slash >= 0) n = n.substring(slash + 1);
        if (n.startsWith("energy-") && n.endsWith(".csv") && n.length() == 18) {
          int y = n.substring(7, 11).toInt();
          int m = n.substring(12, 14).toInt();
          if (y >= 2020 && m >= 1 && m <= 12) {
            int stamp = y * 12 + (m - 1);
            if (best < 0 || stamp < best) best = stamp;
          }
        }
      }
      e.close();
      e = dir.openNextFile();
    }
    dir.close();
  }
  if (best < 0) return -1;
  long k = daysFromCivil(best / 12, (unsigned)(best % 12 + 1), 1);
  int back = (int)(todayDayKey() - k);
  if (back < 0) back = 0;
  if (back > 120) back = 120;
  return back;
}

static void pruneArchives(int keepFrom) {
  File dir = SD.open("/");
  if (!dir) return;
  File e = dir.openNextFile();
  while (e) {
    if (!e.isDirectory()) {
      String n = e.name();
      int slash = n.lastIndexOf('/');
      if (slash >= 0) n = n.substring(slash + 1);
      if (n.startsWith("energy-") && n.endsWith(".csv") && n.length() == 18) {
        int y = n.substring(7, 11).toInt();
        int m = n.substring(12, 14).toInt();
        int stamp = y * 12 + (m - 1);
        if (y >= 2020 && m >= 1 && m <= 12 && stamp < keepFrom) {
          String path = "/" + n;
          e.close();
          if (SD.remove(path.c_str())) Serial.printf("архив удалён: %s\n", path.c_str());
          e = dir.openNextFile();
          continue;
        }
      }
    }
    e.close();
    e = dir.openNextFile();
  }
  dir.close();
}

void initHistoryMonth() {
  struct tm ti;
  if (getLocalTime(&ti, 100)) gSdMonth = (ti.tm_year + 1900) * 12 + ti.tm_mon;
  File f = SD.open("/energy.csv", FILE_READ);
  if (f) {
    long t = fileLastEpoch(f);
    f.close();
    if (t) {
      int stamp = 0;
      monthStampOfEpoch(t, &stamp);
      gSdMonth = stamp;
    }
  }
  if (gSdMonth < 0) return;
  pruneArchives(gSdMonth - 3);
  Serial.printf("история: рабочий месяц %04d-%02d, строк %d\n",
                gSdMonth / 12, gSdMonth % 12 + 1, gSdRows);
}

// dry=true — только показать решение, ничего не переименовывать.
bool rotateHistory(int newStamp, bool dry) {
  File f = SD.open("/energy.csv", FILE_READ);
  if (!f) return false;
  size_t sz = f.size();
  int archStamp = newStamp - 1;
  long firstT = fileFirstEpoch(f, 4096);
  f.close();
  if (firstT > 0) {
    int s = 0;
    monthStampOfEpoch(firstT, &s);
    archStamp = s;
  }
  char arch[40];
  historyName(archStamp, arch, sizeof(arch));
  Serial.printf("ротация: %u байт -> %s%s\n", (unsigned)sz, arch, dry ? " (dry)" : "");
  if (dry) return true;
  if (sz < 20) return false;

  if (SD.exists(arch)) {
    // Архив уже есть (сбой посреди ротации): дописываем рабочий файл в него.
    File src = SD.open("/energy.csv", FILE_READ);
    File dst = SD.open(arch, FILE_APPEND);
    if (!src || !dst) { if (src) src.close(); if (dst) dst.close(); return false; }
    while (src.available()) {
      String l = src.readStringUntil('\n');
      if (l.length() == 0 || l.startsWith("datetime,")) continue;
      dst.println(l);
    }
    src.close(); dst.close();
    SD.remove("/energy.csv");
  } else {
    if (!SD.rename("/energy.csv", arch)) {
      Serial.println("ротация: rename не удался");
      return false;
    }
  }

  File n = SD.open("/energy.csv", FILE_WRITE);
  if (n) {
    n.println("datetime,voltage,power,energy_kwh,relay,cost,temp");
    n.close();
  }
  gSdRows = 1;
  pruneArchives(newStamp - 3);
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

// Есть ли в конце файла хотя бы одна строка указанного дня? Нужно перед
// дозаписью вчерашних строк: файл обязан оставаться хронологическим, поэтому
// вчера можно дописывать только пока сегодняшних строк ещё нет.
static bool fileTailHasDay(const char* dayPrefix) {
  File f = SD.open("/energy.csv", FILE_READ);
  if (!f) return false;
  size_t sz = f.size();
  size_t from = (sz > 4096) ? sz - 4096 : 0;
  f.seek(from);
  bool has = false;
  while (f.available()) {
    String l = f.readStringUntil('\n');
    if (l.length() > 10 && l.startsWith(dayPrefix)) { has = true; break; }
  }
  f.close();
  return has;
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

  // Гистограмму длительности пика строим заново по строкам файла: они уже
  // содержат всё, что записали до загрузки, а живые отсчёты recordStats()
  // начнут добавлять с этого момента. Так перекрытие не удваивается.
  memset(gPkHist, 0, sizeof(gPkHist));
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
      if (p > 0) gPkHist[pkdBucket(p)] += (uint32_t)dt;   // длительность пика
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

// ---------------------------------------------------------------- выбранный день
// Обратная к daysFromCivil: номер дня -> год/месяц/число (Х. Хиннант).
static void civilFromDays(long z, int* yy, int* mm, int* dd) {
  z += 719468;
  long era = (z >= 0 ? z : z - 146096) / 146097;
  unsigned doe = (unsigned)(z - era * 146097L);
  unsigned yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
  long y = (long)yoe + era * 400;
  unsigned doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
  unsigned mp = (5 * doy + 2) / 153;
  unsigned d = doy - (153 * mp + 2) / 5 + 1;
  unsigned m = mp + (mp < 10 ? 3 : -9);
  *yy = (int)(y + (m <= 2 ? 1 : 0));
  *mm = (int)m;
  *dd = (int)d;
}

// Номер сегодняшнего дня по местному времени; 0 — часы ещё не синхронизированы.
static long todayDayKey() {
  struct tm ti;
  if (!getLocalTime(&ti, 10)) return 0;
  return daysFromCivil(ti.tm_year + 1900, ti.tm_mon + 1, ti.tm_mday);
}

// Дата выбранного дня: date = «2026-10-29» (фильтр строк CSV), label = «29.10».
static bool dayDate(int offset, char* date, size_t dn, char* label, size_t ln) {
  long key = todayDayKey() - offset;
  if (key <= 0) return false;
  int y = 0, m = 0, d = 0;
  civilFromDays(key, &y, &m, &d);
  snprintf(date, dn, "%04d-%02d-%02d", y, m, d);
  snprintf(label, ln, "%02d.%02d", d, m);
  return true;
}

// Быстрое число из строки CSV: без String, иначе на 2900 строк в сутках
// получается 2900*6 временных объектов (так было в seedStatsFromSd).
static float dayNum(const char*& p, const char* end) {
  while (p < end && (*p == ' ' || *p == '\t')) p++;
  bool neg = false;
  if (p < end && *p == '-') { neg = true; p++; }
  long ip = 0;
  bool any = false;
  while (p < end && *p >= '0' && *p <= '9') { ip = ip * 10 + (*p - '0'); p++; any = true; }
  float f = (float)ip;
  if (p < end && *p == '.') {
    p++;
    float k = 0.1f;
    while (p < end && *p >= '0' && *p <= '9') {
      f += (float)(*p - '0') * k;
      k *= 0.1f;
      p++;
      any = true;
    }
  }
  if (!any) f = 0;
  return neg ? -f : f;
}
static void daySkip(const char*& p, const char* end) {
  while (p < end && *p != ',') p++;
  if (p < end) p++;
}

// Похожа ли строка на «YYYY-MM-DD ...» — дешёвая проверка без String.
static bool isDateLine(const char* s, int len) {
  if (len < 11) return false;
  for (int i = 0; i < 10; i++) {
    char c = s[i];
    if (i == 4 || i == 7) { if (c != '-') return false; }
    else if (c < '0' || c > '9') return false;
  }
  return s[10] == ' ';
}

// Одна строка дня: дата,В,Вт,кВт·ч,реле,руб,°C — в 5-минутный слот часа.
// Строки 23–26.10 восстановлены из облака: у них нулевые В и Вт, поэтому
// график за такие дни честно пустой (энергия в файле есть, графика нет).
static void dayAddRow(const char* s, int len) {
  if (len < 20) return;
  int hh = (s[11] - '0') * 10 + (s[12] - '0');
  int mi = (s[14] - '0') * 10 + (s[15] - '0');
  if (hh < 0 || hh > 23 || mi < 0 || mi > 59) return;
  int minute = hh * 60 + mi;
  int slot = minute / 5;
  if (slot < 0) slot = 0;
  if (slot >= H24_N) slot = H24_N - 1;         // границы массива — всегда
  const char* p = s + 19;
  const char* end = s + len;
  if (p >= end || *p != ',') return;
  p++;
  float v  = dayNum(p, end); daySkip(p, end);
  float pw = dayNum(p, end); daySkip(p, end);
  daySkip(p, end);                              // кВт·ч
  daySkip(p, end);                              // реле
  daySkip(p, end);                              // рубли
  bool hasT = false;
  float tv = 0;
  while (p < end && (*p == ' ' || *p == '\t')) p++;
  if (p < end && *p != '\r' && *p != '\n' && *p != ',') { tv = dayNum(p, end); hasT = true; }

  const float dt = 30.0f;                       // шаг строки ~30 с — вес средней
  gDayRows++;
  if (v > 50.0f) {
    dayV[slot] = v; dayMask[slot] |= 1;
    if (v < ddMinV) { ddMinV = v; ddMinVMin = minute; }
    if (v > ddMaxV) { ddMaxV = v; ddMaxVMin = minute; }
    ddSumV += (double)v * dt; ddSecV += (long)dt;
    if (v < V_NORM_LO || v > V_NORM_HI) ddOutV += dt;
    // Мощность берём только там, где есть напряжение: у восстановленных
    // строк оба поля нулевые, и «ноль ватт» на графике был бы враньём.
    if (pw >= 0) {
      dayP[slot] = pw; dayMask[slot] |= 4;
      if (pw > ddMaxP) { ddMaxP = pw; ddPMin = minute; }
      if (pw > 0) gPkHist[pkdBucket(pw)] += 30;   // для длительности пика дня
      ddSumP += (double)pw * dt; ddSecP += (long)dt;
    }
  }
  if (hasT && tv > -40.0f && tv < 100.0f) {
    dayT[slot] = tv; dayMask[slot] |= 2;
    if (tv < ddMinT) { ddMinT = tv; ddMinTMin = minute; }
    if (tv > ddMaxT) { ddMaxT = tv; ddMaxTMin = minute; }
    ddSumT += (double)tv * dt; ddSecT += (long)dt;
  }
}

static void dayClear() {
  memset(dayV, 0, sizeof(dayV));
  memset(dayT, 0, sizeof(dayT));
  memset(dayP, 0, sizeof(dayP));
  memset(dayMask, 0, sizeof(dayMask));
  ddMinV = 999; ddMaxV = 0; ddOutV = 0;
  ddSumV = 0; ddSecV = 0;
  ddMinT = 999; ddMaxT = -999; ddSumT = 0; ddSecT = 0;
  ddMaxP = 0; ddSumP = 0; ddSecP = 0;
  ddMinVMin = -1; ddMaxVMin = -1; ddMinTMin = -1; ddMaxTMin = -1; ddPMin = -1;
  memset(gPkHist, 0, sizeof(gPkHist));
  gDayRows = 0;
}

// Читаем /energy.csv БЛОКАМИ по 512 байт и собираем один день в day*[288].
// Файл НЕ отсортирован по времени (23–26.10 вклеены в середину), поэтому
// читать только хвост нельзя — проходим файл целиком. 400 КБ с карты —
// примерно 1 с; на каждой 512-й строке кормим сторожевой таймер.
static bool dayLoadFromSd(int offset) {
  dayClear();
  gDayLoaded = -1;
  if (!gSdOk) return false;
  char date[12] = "", label[8] = "";
  if (!dayDate(offset, date, sizeof(date), label, sizeof(label))) return false;
  memcpy(gDayLabel, label, sizeof(label));

  // дата может лежать в архиве прошлого месяца (energy-YYYY-MM.csv)
  char dpath[40];
  histPathForDate(date, dpath, sizeof(dpath));
  File f = SD.open(dpath, FILE_READ);
  if (!f) return false;
  static uint8_t dbuf[512 + 256];
  int carry = 0;
  int guard = 0;
  unsigned long t0 = millis();
  char oldest[11];
  memset(oldest, '9', sizeof(oldest));       // ищем самый старый день файла
  oldest[10] = 0;
  while (f.available()) {
    int n = (int)f.read(dbuf + carry, 512);
    if (n <= 0) break;
    int total = carry + n;
    int i = 0;
    while (i < total) {
      uint8_t* nl = (uint8_t*)memchr(dbuf + i, '\n', total - i);
      if (!nl) break;
      int len = (int)(nl - (dbuf + i));
      if (len >= 20 && isDateLine((const char*)(dbuf + i), len)) {
        if (memcmp(dbuf + i, date, 10) == 0 && dbuf[i + 10] == ' ')
          dayAddRow((const char*)(dbuf + i), len);
        if (memcmp(dbuf + i, oldest, 10) < 0) memcpy(oldest, dbuf + i, 10);
      }
      i = (int)(nl - dbuf) + 1;
      if ((++guard & 511) == 0) { esp_task_wdt_reset(); yield(); }
    }
    carry = total - i;
    if (carry > 0) memmove(dbuf, dbuf + i, carry);
    if (carry > 240) carry = 0;              // строка без перевода строки
    esp_task_wdt_reset();
  }
  f.close();
  gDayMs = millis() - t0;
  gDayLoaded = offset;

  // Самый старый день файла: по нему ограничиваем стрелку ‹.
  int back = gDayMaxOffset;
  if (memcmp(oldest, "9999999999", 10) != 0) {
    long ok = daysFromCivil((oldest[0] - '0') * 1000 + (oldest[1] - '0') * 100 +
                            (oldest[2] - '0') * 10 + (oldest[3] - '0'),
                            (unsigned)((oldest[5] - '0') * 10 + (oldest[6] - '0')),
                            (unsigned)((oldest[8] - '0') * 10 + (oldest[9] - '0')));
    back = (int)(todayDayKey() - ok);
    if (back < 0) back = 0;
    if (back > 60) back = 60;
  }
  // ...но архивы прошлых месяцев лежат в отдельных файлах: без них стрелка ‹
  // 1-го числа упиралась в 0 и на 31.10 уйти было нельзя.
  int ab = histOldestBack();
  if (ab > back) back = ab;
  gDayMaxOffset = back;
  Serial.printf("день %s (%s): строк %d, %lu мс, самый старый %s%s\n",
                date, label, gDayRows, (unsigned long)gDayMs, oldest,
                gDayRows ? "" : " — НЕТ ДАННЫХ");
  return gDayRows > 0;
}

// ---------------------------------------------------------------- тачскрин
#define TP_CLK   25
#define TP_MOSI  32
#define TP_MISO  39
#define TP_CS    33
#define TP_IRQ   36

int calYleft = 3480, calYright = 586, calXtop = 916, calXbottom = 3010;

void loadCal() {
  // значения читаются из NVS; при первом запуске действуют заводские
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
  gTapRawX = (int)rx; gTapRawY = (int)ry;
  gTapMapX = *sx;     gTapMapY = *sy;
  // пишем в журнал (кольцо из 8)
  for (int k = 7; k > 0; k--) {
    gTLraw[k][0] = gTLraw[k-1][0]; gTLraw[k][1] = gTLraw[k-1][1];
    gTLmap[k][0] = gTLmap[k-1][0]; gTLmap[k][1] = gTLmap[k-1][1];
    gTLsec[k]    = gTLsec[k-1];
  }
  gTLraw[0][0] = rx; gTLraw[0][1] = ry;
  gTLmap[0][0] = *sx; gTLmap[0][1] = *sy;
  gTLsec[0] = millis() / 1000UL;
  if (gTLn < 8) gTLn++;
  return true;
}





// ---------------------------------------------------------------- график мощности из облака
// Локальный DP 6 у этого реле отдаёт УСТАРЕВШЕЕ значение (писал 19 Вт, когда
// работала болгарка на 1300 Вт). Поэтому график мощности за прошедшие часы
// восстанавливаем из облака: там phase_a с шагом 16 секунд.
// Один час за проход loop(), чтобы не держать цикл.

bool cloudCall(const String& method, const String& path, String& body, int maxRaw = 3500, const String& reqBody = "");  // объявлена ниже
extern unsigned long gCloudFailRun;   // неудач облака подряд (см. cloudTick)
unsigned long netBudgetLeft();        // остаток сетевого бюджета прохода (см. cloudCall)
int b64decode(const char* in, uint8_t* out, int maxOut);


// «Фоновый» облачный запрос — дозаполнение (PWRFILL/PWRHIST/GAPFILL).
// При отказе connect cloudCall() делает ВТОРУЮ попытку; для фона она не нужна:
// два TLS-коннекта в одном проходе loop() и давали затык до 10.7 с (blk=pwrfill).
// Флаг ставит CloudBgScope (снимается на любом return — деструктор).
bool gCloudBgCall = false;
struct CloudBgScope {
  bool prev;
  CloudBgScope() : prev(gCloudBgCall) { gCloudBgCall = true; }
  ~CloudBgScope() { gCloudBgCall = prev; }
};

// PWRHIST: дозаполнение мощности прошлых дней из облака (команда в serial).
// Реализация ниже, после всех функций карты и статистики.
bool pwrHistActive();
void pwrHistTick();
bool pwrHistStart(const String& args);

// GAPFILL: заполнение пропусков в /energy.csv данными из облака.
// Реализация ниже, после PWRHIST (нужны cloudCall/b64decode и всё про карту).
bool gapFillActive();
void gapFillTick();
void gapDailyTick();
bool gapFillStart(bool run);
void gapFillSchedule(unsigned long delayMs);
int  gapTodayCount();
int  gapTodayMinutes();

// ---------------------------------------------------------------- шлюз report-logs
// Облако Tuya режет частоту метода report-logs, а при злоупотреблении просто
// рвёт соединение (в /clog это этап connect или hdr). Раньше pwrFillTick
// отправлял по окну ЗА КАЖДЫЙ проход loop() — до 48 запросов за 7 секунд
// после загрузки. Теперь между любыми двумя запросами report-logs >= 1.8 с,
// независимо от того, кто их шлёт (pwrFill/hist/gapfill/PWRHIST).
unsigned long gRepLastMs = 0;
static bool repWait() { return (millis() - gRepLastMs) < 1800UL; }
static void repDone() { gRepLastMs = millis(); }
unsigned long gPwrRetryMs = 0;   // пауза перед повтором окна PWRFILL
int           gPwrRetry   = 0;   // сколько раз подряд не вышло
// диагностика последнего прохода PWRFILL (видно в /clog)
extern int gCliStage;            // этап последнего облачного запроса (объявлен ниже)
extern unsigned long gMsTls;     // ms TLS-рукопожатия облака (объявлен ниже)
unsigned long gPwrFillMs  = 0;   // сколько занял весь pwrFillTick
int           gPwrFillN    = 0;  // сколько облачных запросов он сделал
int           gPwrFillTls  = 0;  // ms TLS последнего запроса
int           gPwrFillSt   = 0;  // этап, на котором кончился последний запрос

void pwrFillTick() {
  CloudBgScope bg;               // фоновый режим: без второй попытки connect
  gPwrFillN = 0;
  if (pwrHistActive()) return;                  // не мешаем PWRHIST
  if (gapFillActive()) return;                  // и GAPFILL тоже
  if (gPwrFillHour < 0) return;
  // В этом проходе сетевой бюджет уже израсходован (облако/веб) — пропускаем
  // такт целиком, а не начинаем второй TLS-запрос и не считаем его отказом.
  if (netBudgetLeft() < 1800UL) return;
  // Напряжение устарело — сеть и облако отдаются cloudTick(). Дозаполнение
  // прошлых часов подождёт: свежие данные важнее (иначе PWRFILL занимает
  // очередь и напряжение остаётся старым).
  if (voltStale(180000UL)) return;
  if (WiFi.status() != WL_CONNECTED || !netOnline()) return;
  // Облако лежит (connect рвётся): каждая неудачная попытка — это 3–11 с
  // блокировки цикла и лишняя фрагментация кучи. При 5 отказах подряд
  // дозаполнение графика останавливается до восстановления связи.
  if (gCloudFailRun >= 5) return;
  if (millis() - gTouchMs < 1500) return;        // не мешаем тачу
  if (millis() < gPwrRetryMs) return;            // пауза после неудачи
  if (!cloudToken()) return;
  // Токен мог сам сходить в облако и занять проход — окно откладываем на
  // следующий loop(), второй TLS-запрос в этом же проходе не начинаем.
  if (netBudgetLeft() < 1500UL) return;

  struct tm ti;
  if (!getLocalTime(&ti, 10)) return;
  int nowH = ti.tm_hour;

  // Текущий час уже заполнен — до наступления следующего часа в облако не
  // ходим. Раньше здесь стоял gPwrFillHour = nowH, и цикл крутился вечно.
  if (gPwrFillStopH >= 0) {
    if (nowH <= gPwrFillStopH) return;
    gPwrFillStopH = -1;                 // наступил новый час — продолжаем
  }

  // Догоняем текущий час, но не останавливаемся навсегда:
  // если отстали — продолжаем со следующего прохода.
  if (gPwrFillHour > nowH) { gPwrFillHour = nowH; gPwrPart = 0; return; }

  int hh = gPwrFillHour;

  struct tm t0;
  memset(&t0, 0, sizeof(t0));
  t0.tm_year = ti.tm_year; t0.tm_mon = ti.tm_mon; t0.tm_mday = ti.tm_mday;
  t0.tm_hour = hh; t0.tm_min = 0; t0.tm_sec = 0; t0.tm_isdst = -1;
  time_t hourStart = mktime(&t0);
  if (hourStart < 1600000000) { gPwrFillHour = -1; return; }

  // ВАЖНО: облако отдаёт записи ОТ НОВЫХ К СТАРЫМ, и size=100 покрывает
  // только последние ~27 минут часа. Поэтому час режем на ТРИ окна по 20 минут
  // (в каждом не больше ~75 записей) и копим результат между проходами loop().
  if (gPwrPart == 0) { gPwrSum = 0; gPwrN = 0; gPwrPeak = 0; gPwrPeakSec = 0; }

  time_t a = hourStart + (time_t)(gPwrPart * 1200);
  time_t b = a + 1200;

  char path[240];
  snprintf(path, sizeof(path),
           "/v1.0/devices/%s/report-logs?codes=phase_a&end_time=%llu&size=100&start_time=%llu",
           DEVICE_ID, (unsigned long long)b * 1000ULL, (unsigned long long)a * 1000ULL);

  String body;
  if (repWait()) return;                        // не частим в report-logs
  // Перед самым запросом — ещё раз бюджет: он мог кончиться, пока считали окно.
  if (netBudgetLeft() < 1500UL) return;
  gPwrFillN++;
  bool okCall = cloudCall("GET", String(path), body, 12000);
  gPwrFillTls = (int)gMsTls;
  gPwrFillSt  = gCliStage;
  repDone();
  if (!okCall) {
    // Одна неудача не должна убивать весь график мощности (раньше ставился
    // gPwrFillHour = -1 и дозаполнение прекращалось до перезагрузки).
    esp_task_wdt_reset();                        // connect уже сжёг время цикла
    // Отказ connect (2) или кончившийся бюджет (8) — это СЕТЬ, а не «окно не
    // читается». Такие отказы час не пропускают: ждём и пробуем то же окно
    // снова, иначе серия обрывов на старте съедала час графика целиком.
    if (gCliStage == 2 || gCliStage == 8) { gPwrRetryMs = millis() + 3000UL; return; }
    if (++gPwrRetry < 4) { gPwrRetryMs = millis() + 3000UL; return; }
    gPwrRetry = 0; gPwrRetryMs = 0;
    gPwrPart = 0;
    gPwrFillHour++;
    if (gPwrFillHour > nowH) { gPwrFillHour = nowH; gPwrFillStopH = nowH; }
    return;
  }
  gPwrRetry = 0; gPwrRetryMs = 0;
  if (body.indexOf("\"success\":true") < 0) {
    if (body.indexOf("too frequent") >= 0) {     // лимит облака — подождать
      gPwrRetryMs = millis() + 15000UL;
      return;
    }
    gPwrPart = 0;
    gPwrFillHour++;
    if (gPwrFillHour > nowH) { gPwrFillHour = nowH; gPwrFillStopH = nowH; }
    return;
  }

  // разбор: берём event_time и value, считаем сумму, пик и его секунду
  int pos = 0;
  int len = body.length();
  while (true) {
    int i = body.indexOf("\"event_time\":", pos);
    if (i < 0) break;
    i += 13;
    while (i < len && (body[i] == ' ' || body[i] == '"')) i++;
    int j = i;
    while (j < len && isDigit(body[j])) j++;
    if (j == i) { pos = i + 1; continue; }
    unsigned long long ms = strtoull(body.substring(i, j).c_str(), NULL, 10);
    pos = j;

    int v = body.indexOf("\"value\":", pos);
    if (v < 0) break;
    v += 8;
    while (v < len && (body[v] == ' ' || body[v] == '"')) v++;
    int e = body.indexOf('"', v);
    if (e < 0) break;
    String b64 = body.substring(v, e);
    pos = e + 1;

    uint8_t raw[12];
    if (b64decode(b64.c_str(), raw, sizeof(raw)) < 8) continue;
    int p = ((uint16_t)raw[6] << 8) | raw[7];
    if (p < 0 || p > 20000) continue;

    gPwrSum += (float)p;
    gPwrN++;
    if (p > gPwrPeak) {
      gPwrPeak = p;
      time_t et = (time_t)(ms / 1000ULL);
      long rel = (long)(et - hourStart);
      if (rel < 0) rel = 0;
      if (rel > 3599) rel = 3599;
      gPwrPeakSec = (int)(rel / 60);
    }
    esp_task_wdt_reset();
  }

  if (++gPwrPart < 3) return;        // остальные два окна — следующими проходами

  // все три окна собраны — записываем час
  if (gPwrN > 0) {
    float avg = gPwrSum / (float)gPwrN;
    for (int s = 0; s < 12; s++) {
      int idx = hh * 12 + s;
      if (idx < 0 || idx >= H24_N) break;
      h24P[idx] = avg;
      h24Mask[idx] |= 4;
    }
    h24Pm[hh] = (float)gPwrPeak;
    h24PmOk[hh] = true;
    h24PmMin[hh] = (uint8_t)gPwrPeakSec;
    Serial.printf("мощность: час %02d = %.0f Вт, пик %d Вт в %02d:%02d (точек %d)\n",
                  hh, avg, gPwrPeak, hh, gPwrPeakSec, gPwrN);
  }

  gPwrPart = 0;
  gPwrFillHour++;
  if (gPwrFillHour > nowH) { gPwrFillHour = nowH; gPwrFillStopH = nowH; gLastDraw = 0; }
  esp_task_wdt_reset();
}

// ---------------------------------------------------------------- тач-тест
// Показывает, куда РЕАЛЬНО попадает палец: сырые координаты, пересчитанные
// и сетку зон карточек. Нужен, чтобы понять, почему карточка не нажимается.
void drawTapTest() {
  uiScreen(cBg);
  drawTxtC(120, 8, "ТАЧ-ТЕСТ", cAccent, cBg, 2);

  // сетка зон карточек (числа должны совпадать с BLOCKS)
  static const int Z[5][4] = { {0,44,240,72}, {0,112,116,64}, {116,112,124,64},
                               {0,174,240,72}, {0,244,240,74} };
  for (int i = 0; i < 5; i++) uiRect(Z[i][0], Z[i][1], Z[i][2], Z[i][3], cLine);

  char b[64];
  snprintf(b, sizeof(b), "СЫРОЕ  %u , %u", gTapRawX, gTapRawY);
  drawTxt(8, 288, b, cText, cBg, 1);
  snprintf(b, sizeof(b), "ЭКРАН  %d , %d", gTapMapX, gTapMapY);
  drawTxt(8, 300, b, cAccent, cBg, 1);

  // крестик там, куда попал палец
  if (gTapMapX > 0 || gTapMapY > 0) {
    uiHLine(gTapMapX - 12, gTapMapY, 25, cBadC);
    uiVLine(gTapMapX, gTapMapY - 12, 25, cBadC);
    uiCircle(gTapMapX, gTapMapY, 9, cBadC);
  }
  drawTxtR(232, 288, "НАЖМИ НА КАРТОЧКУ", cMuted, cBg, 1);
}

// ---------------------------------------------------------------- калибровка по 4 углам
// Четыре мишени в углах экрана. По ним считаем и масштаб, и сдвиг по обеим
// осям — надёжнее двух точек: одна неточная мишень не портит всю калибровку.
static const int CAL_X[4] = { 14, 226, 14, 226 };   // левый верх, правый верх,
static const int CAL_Y[4] = { 14, 14, 306, 306 };   // левый низ, правый низ
static const int CAL_IN = 14, CAL_IN2 = 226;        // отступ мишени от края
static const int CAL_MARGIN = 11;                   // 14 - 3 (запас на палец)

void drawCalTarget(int x, int y, int step) {
  uiScreen(cBg);
  drawTxtC(120, 150, "КАЛИБРОВКА", cAccent, cBg, 2);
  char b[64];
  snprintf(b, sizeof(b), "УГОЛ %d ИЗ 4", step);
  drawTxtC(120, 180, b, cText, cBg, 1);
  drawTxtC(120, 196, "НАЖМИ В ЦЕНТР КРУЖКА", cMuted, cBg, 1);

  // уже принятые углы помечаем зелёным
  for (int i = 0; i < step - 1; i++) uiCircle(CAL_X[i], CAL_Y[i], 6, cGood);

  // текущая мишень
  for (int r = 15; r > 0; r -= 3) uiCircle(x, y, r, (r % 2) ? cAccent : cText);
  uiHLine(x - 22, y, 45, cAccent);
  uiVLine(x, y - 22, 45, cAccent);
}

void calTouchStart() {
  gCalStep = 1;
  for (int i = 0; i < 4; i++) { gCalGood[i] = 0; gCalR[i][0] = gCalR[i][1] = 0; }
  gTouchSkip = millis() + 400;
  drawCalTarget(CAL_X[0], CAL_Y[0], 1);
}

void drawCalDone() {
  uiScreen(cBg);
  drawTxtC(120, 60, "ГОТОВО", cGood, cBg, 3);
  char b[80];
  snprintf(b, sizeof(b), "X: %d .. %d", calYleft, calYright);
  drawTxtC(120, 110, b, cText, cBg, 1);
  snprintf(b, sizeof(b), "Y: %d .. %d", calXtop, calXbottom);
  drawTxtC(120, 126, b, cText, cBg, 1);
  drawTxtC(120, 160, "СОХРАНЕНО В ПАМЯТЬ", cMuted, cBg, 1);
}

// Возвращает true, если касание использовано для калибровки
bool calTick(int x, int y) {
  if (gCalStep == 0) return false;

  int idx = gCalStep - 1;
  int tx = CAL_X[idx], ty = CAL_Y[idx];

  // Палец должен быть РЯДОМ с мишенью, иначе случайное касание всё сломает.
  // Допуск 90 px — с запасом на неточную калибровку.
  int dx0 = x - tx, dy0 = y - ty;
  if (dx0 * dx0 + dy0 * dy0 > 90 * 90) {
    drawTxtC(120, 214, "МИМО — НАЖМИ В КРУЖОК", cBadC, cBg, 1);
    return true;
  }

  gCalR[idx][0] = gTapRawX;
  gCalR[idx][1] = gTapRawY;
  gCalGood[idx] = 1;
  Serial.printf("калибровка: угол %d (%d,%d) = %u,%u\n",
                gCalStep, tx, ty, gTapRawX, gTapRawY);

  if (gCalStep < 4) {
    gCalStep++;
    delay(220);
    drawCalTarget(CAL_X[gCalStep - 1], CAL_Y[gCalStep - 1], gCalStep);
    return true;
  }

  // ---- все четыре собраны: считаем калибровку
  float xLeft  = (gCalR[0][0] + gCalR[2][0]) / 2.0f;   // левый верх + левый низ
  float xRight = (gCalR[1][0] + gCalR[3][0]) / 2.0f;   // правый верх + правый низ
  float yTop   = (gCalR[0][1] + gCalR[1][1]) / 2.0f;   // верх левый + верх правый
  float yBottom= (gCalR[2][1] + gCalR[3][1]) / 2.0f;   // низ левый + низ правый

  Serial.printf("калибровка: X %.0f..%.0f, Y %.0f..%.0f\n", xLeft, xRight, yTop, yBottom);
  Serial.printf("калибровка: разброс X %.0f, Y %.0f\n",
                fabs(gCalR[0][0] - gCalR[2][0]), fabs(gCalR[0][1] - gCalR[1][1]));

  // проверка: диапазоны должны быть широкими, а мишени по вертикали совпадать
  bool bad = (fabs(xLeft - xRight) < 900) || (fabs(yTop - yBottom) < 900) ||
             (fabs(gCalR[0][0] - gCalR[2][0]) > 700) ||
             (fabs(gCalR[0][1] - gCalR[1][1]) > 700);
  if (bad) {
    Serial.println("калибровка: НЕГОДНЫЕ ДАННЫЕ — повтор");
    uiScreen(cBg);
    drawTxtC(120, 140, "НЕ ПОЛУЧИЛОСЬ", cBadC, cBg, 2);
    drawTxtC(120, 170, "КАСАНИЯ НЕТОЧНЫЕ", cText, cBg, 1);
    drawTxtC(120, 186, "ПОПРОБУЙ СНОВА", cMuted, cBg, 1);
    delay(2400);
    calTouchStart();
    return true;
  }

  // экстраполяция с отступа 14 px на самые края 0 и 239 / 0 и 319
  float dxRaw = (xRight - xLeft) / (float)(CAL_IN2 - CAL_IN);
  calYleft  = (int)(xLeft  - dxRaw * CAL_IN);
  calYright = (int)(xLeft  - dxRaw * CAL_IN + dxRaw * 239);

  float dyRaw = (yBottom - yTop) / (float)(306 - 14);
  calXtop    = (int)(yTop - dyRaw * 14);
  calXbottom = (int)(calXtop + dyRaw * 319);

  saveCal();
  gCalStep = 0;
  Serial.printf("калибровка сохранена: yleft=%d yright=%d xtop=%d xbottom=%d\n",
                calYleft, calYright, calXtop, calXbottom);
  drawCalDone();
  delay(1600);
  gScreen = SCR_MAIN;

  drawScreen();
  gLastDraw = millis(); gLastSig = screenSig(); gLastWinFull = millis();
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
// карта, сторож камер) проверки не требуют и работают всегда.
bool     gNetForceOff = false;    // принудительно «без интернета» (команда NETOFF)
bool     gWifiOff     = false;    // радио выключено вручную (команда WIFIOFF)
bool     gNetOk       = false;    // результат последней проверки
unsigned long gNetProbeMs = 0;

#define NET_PROBE_MS 250          // таймаут одного подключения, мс
bool gProbe1 = false, gProbe2 = false;   // результаты зонда — видны в /data

bool netProbe() {
  if (gNetForceOff || gWifiOff || WiFi.status() != WL_CONNECTED) return false;
  // Проверяем TCP до публичного DNS: дешевле и быстрее HTTP.
  // 8.8.8.8 первым не ставим — в РФ он часто недоступен;
  // 77.88.8.8 (Яндекс DNS) доступен всегда, когда интернет есть.
  const uint8_t ips[2][4] = { { 77, 88, 8, 8 }, { 1, 1, 1, 1 } };
  gProbe1 = gProbe2 = false;
  for (int i = 0; i < 2; i++) {
    IPAddress ip(ips[i][0], ips[i][1], ips[i][2], ips[i][3]);
    WiFiClient c;
    bool ok = (c.connect(ip, 53, NET_PROBE_MS) == 1);
    c.stop();
    if (i == 0) gProbe1 = ok; else gProbe2 = ok;
    if (ok) return true;
  }
  return false;
}


// ===== МАСТЕР НАСТРОЙКИ: mDNS, IP, WIFISET =====================================
// Нужны программе настройки на ПК: она находит плату по USB, прошивает,
// записывает Wi-Fi и дальше работает по сети. См. DomMonitor.ps1.

bool mdnsBegin() {
  if (WiFi.status() != WL_CONNECTED) return false;
  MDNS.end();
  if (!MDNS.begin("cyd")) {
    Serial.println("mDNS: не запустился, cyd.local не работает");
    return false;
  }
  MDNS.addService("http", "tcp", 80);
  Serial.println("mDNS: http://cyd.local/");
  return true;
}

// Аргумент команды: "в кавычках с пробелами" или слово до пробела.
bool wfArg(const String& s, int& i, String& out) {
  int n = (int)s.length();
  while (i < n && s[i] == ' ') i++;
  if (i >= n) return false;
  out = "";
  if (s[i] == '"') {
    i++;
    while (i < n && s[i] != '"') { out += s[i]; i++; }
    if (i < n) i++;
  } else {
    while (i < n && s[i] != ' ') { out += s[i]; i++; }
  }
  return true;
}

// IP — текущий адрес, сеть и уровень сигнала одной строкой для мастера.
void printIp() {
  String ssid = (WiFi.status() == WL_CONNECTED) ? WiFi.SSID() : gSsid;
  if (ssid.length() == 0) ssid = gSsid;
  Serial.printf("IP: %s ssid=%s rssi=%d\n",
                WiFi.localIP().toString().c_str(), ssid.c_str(), (int)gRssi);
}

// WIFISET "ssid" "pass" — записать сеть в NVS и подключиться.
// Пароль в ответ НЕ печатаем: строка уходит в лог приложения.
// Если подключиться не удалось — возвращаемся на прежнюю сеть, чтобы плата
// не осталась без связи (проверено на неверном пароле 30.10.2026).
//
// ВАЖНО: раньше здесь был цикл while (millis()-t0 < 20000) с delay(250), и на
// это время тач и веб были МЁРТВЫ, а при неудаче добавлялся ещё откат
// connectWifi(15000) — до 35 с зависания. Теперь это автомат: cmdWifiSet()
// только СТАРТУЕТ процесс и сразу отвечает, а шаги делает wifiSetTick() из
// loop() — по одному за проход, без delay больше 50 мс.
int      gWifiSetState   = 0;   // 0=простой 1=подключаемся 2=проверяем 3=откат
unsigned long gWifiSetT0 = 0;   // начало текущего состояния
unsigned long gWifiSetTick = 0; // когда последний раз проверяли статус
String   gWifiSetSsid, gWifiSetPass;      // цель
String   gWifiSetOldSsid, gWifiSetOldPass;// куда вернуться при неудаче

#define WIFISET_CONNECT_MS 20000   // сколько ждём подключения к новой сети
#define WIFISET_VERIFY_MS   1500   // сколько держим связь, прежде чем сказать ОК
#define WIFISET_ROLLBACK_MS 15000  // сколько ждём возврата на прежнюю сеть

// true — смена сети идёт: тяжёлые сетевые операции в это время не запускаем,
// иначе они снова займут цикл и интерфейс замрёт.
bool wifiSetBusy() { return gWifiSetState != 0; }

// Отложенный запуск: веб-команда должна успеть отдать ответ ДО того, как
// плата уйдёт из сети. Иначе клиент видит таймаут, хотя всё сработало.
String gWifiSetPendingCmd;

void cmdWifiSet(const String& cmd) {
  if (gWifiSetState != 0) {
    Serial.printf("WIFISET: ERR уже идёт смена сети (состояние %d)\n", gWifiSetState);
    return;
  }
  int i = 7;                          // длина слова WIFISET
  String ssid, pass;
  if (!wfArg(cmd, i, ssid) || ssid.length() == 0) {
    Serial.println("WIFISET: ERR нужен SSID, пример: WIFISET \"сеть\" \"пароль\"");
    return;
  }
  wfArg(cmd, i, pass);

  // Запоминаем, куда вернуться. Пароль в лог не попадает.
  gWifiSetOldSsid = gSsid;
  gWifiSetOldPass = gPass;
  gWifiSetSsid = ssid;
  gWifiSetPass = pass;

  gWifiOff = false;
  gNetForceOff = false;
  gNetOk = false;           // после смены сети старый результат зонда неверен
  gNetProbeMs = 0;          // и кэш 30 с сбрасываем — иначе netOnline() врёт

  WiFi.disconnect(false, false);
  WiFi.begin(ssid.c_str(), pass.c_str());

  gWifiSetState = 1;
  gWifiSetT0 = millis();
  gWifiSetTick = millis();
  Serial.printf("WIFISET: подключаюсь к \"%s\" (пароль %d симв.)\n",
                ssid.c_str(), (int)pass.length());
  Serial.printf("WIFISET: START ssid=%s\n", ssid.c_str());
}

// Шаг автомата смены сети. Вызывается из loop() и НЕ блокирует цикл:
// за проход — максимум одна проверка WiFi.status() (без delay).
void wifiSetTick() {
  if (gWifiSetState == 0) return;
  esp_task_wdt_reset();                             // сторож: шаг дешёвый
  if (millis() - gWifiSetTick < 250UL) return;      // раз в 250 мс, не чаще
  gWifiSetTick = millis();

  switch (gWifiSetState) {
    case 1:                                        // подключаемся к новой сети
      if (WiFi.status() == WL_CONNECTED) {
        gWifiSetState = 2;                         // проверяем, что связь держится
        gWifiSetT0 = millis();
        return;
      }
      if (millis() - gWifiSetT0 > WIFISET_CONNECT_MS) {
        Serial.println("WIFISET: ERR timeout");
        Serial.printf("WIFISET: возвращаюсь на \"%s\"\n", gWifiSetOldSsid.c_str());
        WiFi.disconnect(false, false);
        gSsid = gWifiSetOldSsid;
        gPass = gWifiSetOldPass;
        WiFi.begin(gSsid.c_str(), gPass.c_str());
        gWifiSetState = 3;
        gWifiSetT0 = millis();
      }
      return;

    case 2:                                        // подключились — проверим
      if (WiFi.status() != WL_CONNECTED) {          // связь не удержалась
        if (millis() - gWifiSetT0 > WIFISET_CONNECT_MS) {
          Serial.println("WIFISET: ERR timeout");
          Serial.printf("WIFISET: возвращаюсь на \"%s\"\n", gWifiSetOldSsid.c_str());
          WiFi.disconnect(false, false);
          gSsid = gWifiSetOldSsid;
          gPass = gWifiSetOldPass;
          WiFi.begin(gSsid.c_str(), gPass.c_str());
          gWifiSetState = 3;
          gWifiSetT0 = millis();
        }
        return;
      }
      if (millis() - gWifiSetT0 < WIFISET_VERIFY_MS) return;
      // Связь есть и держится — только теперь пишем в память.
      gRssi = WiFi.RSSI();
      saveWifi(gWifiSetSsid, gWifiSetPass);
      mdnsBegin();
      gNetProbeMs = 0;                              // проверим интернет заново
      Serial.printf("WIFISET: OK ssid=%s\n", gWifiSetSsid.c_str());
      Serial.println("веб: http://" + WiFi.localIP().toString() + "/");
      gWifiSetState = 0;
      return;

    case 3:                                        // откат на прежнюю сеть
      if (WiFi.status() == WL_CONNECTED) {
        gRssi = WiFi.RSSI();
        gNetProbeMs = 0;
        Serial.printf("WIFISET: на связи снова \"%s\"\n", gWifiSetOldSsid.c_str());
        Serial.println("веб: http://" + WiFi.localIP().toString() + "/");
        gWifiSetState = 0;
        return;
      }
      if (millis() - gWifiSetT0 > WIFISET_ROLLBACK_MS) {
        Serial.println("WIFISET: прежняя сеть тоже не отвечает");
        gWifiSetState = 0;
      }
      return;

    default:
      gWifiSetState = 0;
      return;
  }
}
// ==============================================================================

// true — интернет есть. Результат кэшируется на 30 с, чтобы не тратить время.
bool netOnline() {
  if (gNetForceOff || gWifiOff) { gNetOk = false; return false; }
  // идёт смена сети — интернета заведомо нет, и тяжёлые запросы сейчас некстати
  if (gWifiSetState != 0) { gNetOk = false; return false; }
  if (WiFi.status() != WL_CONNECTED) { gNetOk = false; return false; }
  if (gNetProbeMs != 0 && millis() - gNetProbeMs < 45000UL) return gNetOk;
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

// ============================================================================
//  КАЧЕСТВО НАПРЯЖЕНИЯ: провалы/всплески + защита
// ----------------------------------------------------------------------------
//  Напряжение приходит от реле примерно раз в секунду, поэтому события
//  фиксируются с точностью до этого шага (не микросекунды — датчика на плате нет).
// ============================================================================
#define VEV_N 64
struct VEv { uint32_t t; uint16_t v; uint16_t sec; uint8_t kind; };  // kind 0 провал, 1 всплеск
static VEv sVev[VEV_N];
static uint8_t sVevHead = 0, sVevCnt = 0;

static bool  gProtOn    = false;   // защита включена (по умолчанию выключена)
static float gProtVMax  = 255.0f;
static float gProtVMin  = 190.0f;
static float gProtTMax  = 60.0f;
static float gProtPMax  = 3500.0f;
static bool  gProtCut   = false;   // разрешено физически отключать реле
static unsigned long gProtCool = 0;      // пауза после срабатывания
static unsigned long gProtCheckAt = 0;
static uint16_t gProtTrigN = 0;
static String gAlarm1, gAlarm2;   // alarm_set_1/2 из автомата Tuya (base64)
static char gProtWhy[40] = "";

static bool sInSag = false, sInSwell = false;
static unsigned long sSagAt = 0, sSwellAt = 0;
static uint16_t sSagMin = 0, sSwellMax = 0;

static void vevPush(uint8_t kind, uint16_t v, uint16_t sec, uint32_t t) {
  sVev[sVevHead].t = t;
  sVev[sVevHead].v = v;
  sVev[sVevHead].sec = sec;
  sVev[sVevHead].kind = kind;
  sVevHead = (uint8_t)((sVevHead + 1) % VEV_N);
  if (sVevCnt < VEV_N) sVevCnt++;
  // строка в /voltage.log на карте, чтобы события пережили перезагрузку
  if (gSdOk) {
    File f = SD.open("/voltage.log", FILE_APPEND);
    if (f) {
      struct tm ti;
      char when[24] = "-";
      if (getLocalTime(&ti, 20)) snprintf(when, sizeof(when), "%04d-%02d-%02d %02d:%02d:%02d",
                                          ti.tm_year + 1900, ti.tm_mon + 1, ti.tm_mday,
                                          ti.tm_hour, ti.tm_min, ti.tm_sec);
      f.printf("%s %s %u В %u с\n", when, kind ? "ВСПЛЕСК" : "ПРОВАЛ", (unsigned)v, (unsigned)sec);
      f.close();
    }
  }
  Serial.printf("качество: %s %u В, %u с\n", kind ? "всплеск" : "провал", (unsigned)v, (unsigned)sec);
}

static double sVSum = 0;
static uint32_t sVn = 0, sVbad = 0;
static float sVmin = 999, sVmax = 0;
#define GOST_LO 207.0f
#define GOST_HI 253.0f

static void volQuality(float v) {
  unsigned long now = millis();
  // статистика для отчёта по ГОСТ 32144 (п. 4.2.2: 207–253 В)
  sVSum += v; sVn++;
  if (v < sVmin) sVmin = v;
  if (v > sVmax) sVmax = v;
  if (v < GOST_LO || v > GOST_HI) sVbad++;
  if (v < gProtVMin) {
    if (!sInSag) { sInSag = true; sSagAt = now; sSagMin = (uint16_t)v; }
    else if (v < sSagMin) sSagMin = (uint16_t)v;
  } else if (sInSag) {
    sInSag = false;
    uint16_t dur = (uint16_t)((now - sSagAt) / 1000UL);
    if (dur < 1) dur = 1;
    if (dur <= 600) vevPush(0, sSagMin, dur, (uint32_t)(sSagAt / 1000UL));
  }
  if (v > gProtVMax) {
    if (!sInSwell) { sInSwell = true; sSwellAt = now; sSwellMax = (uint16_t)v; }
    else if (v > sSwellMax) sSwellMax = (uint16_t)v;
  } else if (sInSwell) {
    sInSwell = false;
    uint16_t dur = (uint16_t)((now - sSwellAt) / 1000UL);
    if (dur < 1) dur = 1;
    if (dur <= 600) vevPush(1, sSwellMax, dur, (uint32_t)(sSwellAt / 1000UL));
  }
}

// защита: раз в секунду проверяем уставки
static void protCheck() {
  unsigned long now = millis();
  if (now - gProtCheckAt < 1000UL) return;
  gProtCheckAt = now;
  if (!gProtOn) return;
  if (gProtCool && now - gProtCool < 60000UL) return;

  const char* why = "";
  if (gVoltage > 80 && gVoltage > gProtVMax) why = "перенапряжение";
  else if (gVoltage > 80 && gVoltage < gProtVMin) why = "просадка";
  else if (gTempValid && gTemp > gProtTMax) why = "перегрев";
  else if (gPower > gProtPMax) why = "перегруз";
  if (!why[0]) return;

  snprintf(gProtWhy, sizeof(gProtWhy), "%s %.0f", why,
           (strcmp(why, "перегрев") == 0) ? gTemp : (strcmp(why, "перегруз") == 0 ? gPower : gVoltage));
  gProtTrigN++;
  gProtCool = now;
  Serial.printf("ЗАЩИТА: %s (реле %s)\n", gProtWhy, gProtCut ? "выключаю" : "только уведомление");
  mqttPublish("protect", gProtWhy, false);
  if (gProtCut) {
    bool ok = tuyaRelaySet(false);
    mqttPublish("protect_act", ok ? "relay_off" : "relay_off_fail", false);
  }
}


// ============================================================================
//  ПЛАНИРОВЩИК РЕЛЕ
// ----------------------------------------------------------------------------
//  До 8 правил: время ЧЧ:ММ, действие (вкл/выкл), маска дней, включено ли правило.
//  Правило «выкл» сработает только если разрешено отключение (allowOff).
// ============================================================================
#define SCH_N 8
struct SchRule { uint8_t h, m, on, days, en; };
static SchRule sSch[SCH_N];
static uint8_t  sSchCnt = 0;
static bool     gAllowOff = false;   // разрешено ли выключать реле (в т.ч. MQTT relay_off)
static uint32_t sSchFired = 0;       // ключ последней минуты, чтобы не сработать дважды

static String schDump() {
  String s = "";
  for (uint8_t i = 0; i < sSchCnt; i++) {
    if (i) s += ';';
    char b[40];
    snprintf(b, sizeof(b), "%02u:%02u,%s,%u,%u", (unsigned)sSch[i].h, (unsigned)sSch[i].m,
             sSch[i].on ? "on" : "off", (unsigned)sSch[i].days, (unsigned)sSch[i].en);
    s += b;
  }
  return s;
}

static bool schParse(const String& src) {
  sSchCnt = 0;
  int start = 0;
  while (start < (int)src.length() && sSchCnt < SCH_N) {
    int semi = src.indexOf(';', start);
    if (semi < 0) semi = src.length();
    String one = src.substring(start, semi);
    start = semi + 1;
    one.trim();
    if (!one.length()) continue;
    int c1 = one.indexOf(',');
    int c2 = one.indexOf(',', c1 + 1);
    int c3 = one.indexOf(',', c2 + 1);
    if (c1 < 0 || c2 < 0 || c3 < 0) continue;
    String hhmm = one.substring(0, c1); hhmm.trim();
    String act  = one.substring(c1 + 1, c2); act.trim();
    int days = one.substring(c2 + 1, c3).toInt();
    int en   = one.substring(c3 + 1).toInt();
    int colon = hhmm.indexOf(':');
    if (colon < 0) continue;
    int h = hhmm.substring(0, colon).toInt();
    int m = hhmm.substring(colon + 1).toInt();
    if (h < 0 || h > 23 || m < 0 || m > 59) continue;
    sSch[sSchCnt].h = (uint8_t)h;
    sSch[sSchCnt].m = (uint8_t)m;
    sSch[sSchCnt].on = act.equalsIgnoreCase("on") ? 1 : 0;
    sSch[sSchCnt].days = (uint8_t)constrain(days, 0, 127);
    sSch[sSchCnt].en = en ? 1 : 0;
    sSchCnt++;
  }
  return true;
}

static void schSave() {
  Preferences p;
  if (!p.begin("prot", false)) return;
  p.putString("sched", schDump());
  p.putBool("allowOff", gAllowOff);
  p.end();
}

static void schLoad() {
  Preferences p;
  if (!p.begin("prot", true)) return;
  String s = p.getString("sched", "");
  gAllowOff = p.getBool("allowOff", false);
  p.end();
  if (s.length()) schParse(s);
}

// раз в 20 с проверяем минуту; срабатываем один раз на минуту
static void schTick() {
  static unsigned long last = 0;
  unsigned long now = millis();
  if (now - last < 20000UL) return;
  last = now;
  if (!sSchCnt) return;
  struct tm ti;
  if (!getLocalTime(&ti, 5)) return;
  uint32_t key = (uint32_t)(ti.tm_yday * 1440 + ti.tm_hour * 60 + ti.tm_min);
  if (key == sSchFired) return;
  int wd = (ti.tm_wday + 6) % 7;              // 0 = понедельник
  uint8_t mask = (uint8_t)(1 << wd);
  for (uint8_t i = 0; i < sSchCnt; i++) {
    SchRule& r = sSch[i];
    if (!r.en) continue;
    if (r.h != ti.tm_hour || r.m != ti.tm_min) continue;
    if (r.days && !(r.days & mask)) continue;
    if (!r.on && !gAllowOff) {
      Serial.println("расписание: выключение запрещено (allowOff=0)");
      mqttPublish("result", "schedule_off_blocked", false);
      continue;
    }
    bool ok = tuyaRelaySet(r.on ? true : false);
    sSchFired = key;
    Serial.printf("расписание: %02u:%02u -> %s (%s)\n", (unsigned)r.h, (unsigned)r.m,
                  r.on ? "on" : "off", ok ? "ok" : "fail");
    mqttPublish("result", ok ? (r.on ? "schedule_on" : "schedule_off") : "schedule_fail", false);
    mqttPublishState(true);
    break;
  }
}

static void protSave() {
  Preferences p;
  if (!p.begin("prot", false)) return;
  p.putBool("on", gProtOn);
  p.putFloat("vmax", gProtVMax);
  p.putFloat("vmin", gProtVMin);
  p.putFloat("tmax", gProtTMax);
  p.putFloat("pmax", gProtPMax);
  p.putBool("cut", gProtCut);
  p.end();
}

static void protLoad() {
  Preferences p;
  if (!p.begin("prot", true)) return;
  gProtOn   = p.getBool("on", false);
  gProtVMax = p.getFloat("vmax", 255.0f);
  gProtVMin = p.getFloat("vmin", 190.0f);
  gProtTMax = p.getFloat("tmax", 60.0f);
  gProtPMax = p.getFloat("pmax", 3500.0f);
  gProtCut  = p.getBool("cut", false);
  p.end();
}

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
  if (v > 80 && v < 400) volQuality(v);
  if (pwr >= 0 && pwr < 20000) { gPower = pwr; gPowerMs = millis(); got = true; }
  if (a >= 0 && a < 100) gCurrent = a;      // реальный ток, а не P/V
  if (got) gPhaseMs = millis();
  if (got) { gPhaseN++; gPhaseRelayN++; }   // сколько раз напряжение пришло ОТ РЕЛЕ
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
// счётчики для /data: видно, доходит ли дело до облака и чем кончается
unsigned long gCloudTry = 0;     // попыток cloudStatus()
unsigned long gCloudOkN = 0;     // из них удачных
unsigned long gTokenN   = 0;     // сколько раз брали токен
int           gNetStep  = 0;     // шаг сетевого автомата в loop() (для /data)

// ---------------------------------------------------------------- диагностика облака
// Раньше облачные отказы были ПОЛНОСТЬЮ немыми: cloudCall() возвращал false на
// шести разных этапах, и понять, где рвётся, было нечем. Отсюда «напряжение
// застыло на 2 часа» при живом Wi-Fi. Теперь каждый отказ помечен.
int           gCloudWhy    = 0;    // 0 ок, 1 нет Wi-Fi, 2 нет интернета, 3 нет токена,
                                   // 4 cloudCall==false, 5 success=false, 6 в ответе нет phase_a
unsigned long gCloudTryMs  = 0;    // millis() последней попытки cloudStatus()
unsigned long gCloudFailRun = 0;   // неудач ПОДРЯД (сбрасывается успехом)
int           gCliStage    = 0;    // где кончился последний cloudCall:
                                   // 0 ок, 1 часы, 2 connect, 3 отправка, 4 заголовки,
                                   // 5 нет "HTTP/1.", 6 тело короче Content-Length, 7 тело пустое
int           gCliHttp     = 0;    // HTTP-код ответа (200, 400, 429 ...)
int           gCliLen      = 0;    // сколько байт тела прочитано
int           gCliWant     = 0;    // Content-Length ответа (-1 = не было)
const char* cliStageName(int s) {
  switch (s) {
    case 0: return "ok";
    case 1: return "clock";
    case 2: return "connect";
    case 3: return "print";
    case 4: return "hdr";
    case 5: return "http";
    case 6: return "short";
    case 7: return "empty";
    case 8: return "budget";
    case 9: return "dns";
    default: return "?";
  }
}
// Кольцо последних облачных запросов — видно историю, а не только «сейчас»:
// /clog отдаёт его текстом. 8 записей по 16 байт.
struct ClRec { unsigned long at; uint8_t stage; uint8_t why; int16_t http; int16_t len; };
ClRec gClRing[8];
int   gClRingI = 0;
void clRingAdd(uint8_t stage, uint8_t why, int http, int len) {
  ClRec& r = gClRing[gClRingI % 8];
  r.at = millis(); r.stage = stage; r.why = why;
  r.http = (int16_t)http; r.len = (int16_t)len;
  gClRingI++;
}

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
//
// ВАЖНО (затупания): раньше тело читалось ПОБАЙТНО и потом ещё ждало 400 мс
// тишины — это лишние ~600 мс на каждый запрос. Теперь заголовки читаются до
// пустой строки, а тело — ровно по Content-Length (и кусками, а не по байту).
static unsigned long gCloudReuse = 0;   // оставлен для совместимости /data
unsigned long gMsDns = 0;               // последний getaddrinfo облака
unsigned long gMsTls = 0;               // последний TLS-handshake облака

// Заголовки ответа до пустой строки. false — не дождались.
static bool cloudReadHeaders(WiFiClientSecure& c, String& hdr, unsigned long timeoutMs) {
  hdr = "";
  hdr.reserve(512);
  unsigned long t0 = millis();
  while (millis() - t0 < timeoutMs) {
    while (c.available()) {
      int ch = c.read();
      if (ch < 0) break;
      hdr += (char)ch;
      int n = (int)hdr.length();
      if (n >= 4 && hdr[n - 1] == '\n' && hdr[n - 2] == '\r' &&
                    hdr[n - 3] == '\n' && hdr[n - 4] == '\r') return true;
      if (n > 1600) return false;
    }
    if (!c.connected() && !c.available()) break;
    esp_task_wdt_reset();
    delay(5);
  }
  return false;
}

static int cloudContentLength(const String& hdr) {
  String low = hdr;
  low.toLowerCase();
  int i = low.indexOf("\r\ncontent-length:");
  if (i < 0) return -1;
  int j = i + 17;
  while (j < (int)low.length() && (low[j] == ' ' || low[j] == '\t')) j++;
  long v = 0;
  bool any = false;
  while (j < (int)low.length() && low[j] >= '0' && low[j] <= '9') {
    v = v * 10 + (low[j] - '0');
    j++;
    any = true;
    if (v > 400000) return -1;
  }
  return any ? (int)v : -1;
}

// ------------------------------------------------------------------ chunked
// ОБЛАКО ОТВЕЧАЕТ CHUNKED (проверено /cloudtest: в теле было "915\r\n{...}").
// Раньше служебные "<hex>\r\n" оставались ВНУТРИ тела, и разбор JSON рвался:
// граница чанка (~2325 байт) могла разрезать "code":"phase_a","value": или
// "success":true. Тогда облако «ответило», а напряжение не обновлялось —
// и так часами, потому что длина ответа почти не меняется. Теперь чанки
// разбираются как положено, в тело попадает только JSON.
static bool cloudIsChunked(const String& hdr) {
  String low = hdr;
  low.toLowerCase();
  return low.indexOf("transfer-encoding: chunked") >= 0;
}

// Прочитать ровно n байт тела. false — дедлайн вышел или соединение оборвалось.
static bool cloudReadN(WiFiClientSecure& c, int n, String& out, unsigned long deadline) {
  while (n > 0) {
    if ((long)(millis() - deadline) >= 0) return false;
    // Ждём данные через available(), а НЕ блокирующим read(): read() на пустом
    // сокете держит до socket-таймаута (1 с), и дедлайн тела растягивался.
    int av = c.available();
    if (av <= 0) {
      if (!c.connected()) return false;
      delay(2);
      esp_task_wdt_reset();
      continue;
    }
    int want = n > 256 ? 256 : n;
    if (want > av) want = av;
    uint8_t buf[257];
    int k = c.read(buf, want);
    if (k > 0) { buf[k] = 0; out += (char*)buf; n -= k; }
    else if (!c.connected()) return false;
    else delay(2);
    esp_task_wdt_reset();
  }
  return true;
}

// Разбор "размер_в_hex\r\n<данные>\r\n...0\r\n\r\n" в чистый JSON.
static bool cloudReadChunked(WiFiClientSecure& c, String& raw, int maxRaw, unsigned long deadline) {
  for (int guard = 0; guard < 4000; guard++) {
    // строка размера чанка (hex, может быть с ";ext")
    String sz;
    bool gotNl = false;
    while (sz.length() < 24) {
      if ((long)(millis() - deadline) >= 0) return false;
      int ch = c.read();
      if (ch < 0) {
        if (!c.connected() && !c.available()) return false;
        delay(1);
        continue;
      }
      if (ch == '\n') { gotNl = true; break; }
      if (ch != '\r') sz += (char)ch;
    }
    if (!gotNl) return false;
    sz.trim();
    int semi = sz.indexOf(';');
    if (semi >= 0) sz = sz.substring(0, semi);
    long n = strtol(sz.c_str(), NULL, 16);
    if (n <= 0) return raw.length() > 0;           // "0" — конец тела
    if ((int)raw.length() + (int)n > maxRaw) return raw.length() > 0;
    if (!cloudReadN(c, (int)n, raw, deadline)) return false;
    // после данных идёт CRLF
    String crlf;
    cloudReadN(c, 2, crlf, deadline);
    esp_task_wdt_reset();
  }
  return raw.length() > 0;
}

// Кэш веб-страницы (30 КБ) объявлен ниже, но отдавать эту память нужно уметь
// отсюда: именно из-за неё падал TLS к облаку (см. cloudCall).
void pageCacheFree();

// Бюджет сети на ОДИН проход loop(). Раньше cloudCall мог держать цикл
// 2×2500 мс (connect) + 3000 (заголовки) + 6000 (тело) = 19.9 с — тач и веб
// «замирали». Теперь все TLS-запросы одного прохода делят NET_BUDGET_MS.
#define NET_BUDGET_MS 2000UL
static unsigned long sNetBudgetAt = 0;
static bool          gWebInHandler = false;   // true — уже внутри handleClient
void handleTouch();                            // объявлена ниже
extern WebServer gWeb;                         // объявлен ниже

// DNS облака — в КОРОТКОЖИВУЩЕЙ задаче (создаётся на один резолв и сама
// удаляется). Причина: WiFi.hostByName() внутри connect(host,...) не ограничен
// таймаутом и при сбое DNS держал loop() 15–19 с (ровно те 18642 мс в
// loopMaxAll). Постоянную задачу держать нельзя — она отнимает 6 КБ кучи,
// а mbedTLS на этой плате живёт на пределе.
static IPAddress sTuyaIp;
static volatile bool          sTuyaIpOk    = false;
static volatile unsigned long sTuyaDnsAt   = 0;
static volatile bool          sTuyaDnsBusy = false;

static void tuyaDnsOnce(void*) {
  IPAddress ip;
  unsigned long t0 = millis();
  bool ok = WiFi.hostByName(TUYA_HOST, ip);
  gMsDns = millis() - t0;
  if (ok) { sTuyaIp = ip; sTuyaIpOk = true; }
  sTuyaDnsAt = millis();
  sTuyaDnsBusy = false;
  vTaskDelete(NULL);
}
static void tuyaDnsKick(bool force) {
  if (sTuyaDnsBusy) return;
  if (!force) {
    if (!sTuyaIpOk) { /* первый раз — резолвим сразу */ }
    else if (millis() - sTuyaDnsAt < 600000UL) return;   // раз в 10 минут
    else if (millis() - sTuyaDnsAt < 60000UL) return;    // не чаще раза в минуту
  }
  sTuyaDnsBusy = true;
  if (xTaskCreatePinnedToCore(tuyaDnsOnce, "dns1", 4096, NULL, 1, NULL, 0) != pdPASS)
    sTuyaDnsBusy = false;
}

void netBudgetReset() { sNetBudgetAt = millis(); }
unsigned long netBudgetLeft() {
  if (sNetBudgetAt == 0) return NET_BUDGET_MS;
  unsigned long used = millis() - sNetBudgetAt;
  return (used >= NET_BUDGET_MS) ? 0 : NET_BUDGET_MS - used;
}
// (connect идёт по IP с SNI-хостом, поэтому таймаут задаётся setTimeout.)


// ---------------------------------------------------------------- запись настроек в автомат Tuya
static String b64enc(const uint8_t* d, int n) {
  static const char* T = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
  String o = "";
  int i = 0;
  while (i < n) {
    uint32_t v = (uint32_t)d[i] << 16;
    int rem = n - i;
    if (rem > 1) v |= (uint32_t)d[i + 1] << 8;
    if (rem > 2) v |= (uint32_t)d[i + 2];
    o += T[(v >> 18) & 63];
    o += T[(v >> 12) & 63];
    o += (rem > 1) ? T[(v >> 6) & 63] : '=';
    o += (rem > 2) ? T[v & 63] : '=';
    i += 3;
  }
  return o;
}

// правка одной записи (код, вкл, значение) в base64-наборе alarm_set_*
static String alarmPatch(const String& curB64, uint8_t code, bool en, uint16_t value) {
  uint8_t raw[32];
  int n = b64decode(curB64.c_str(), raw, sizeof(raw));
  if (n < 0) n = 0;
  bool found = false;
  for (int i = 0; i + 3 < n; i += 4) {
    if (raw[i] == code) {
      raw[i + 1] = en ? 1 : 0;
      raw[i + 2] = (uint8_t)(value >> 8);
      raw[i + 3] = (uint8_t)(value & 0xFF);
      found = true;
    }
  }
  if (!found && n + 4 <= (int)sizeof(raw)) {
    raw[n] = code; raw[n + 1] = en ? 1 : 0;
    raw[n + 2] = (uint8_t)(value >> 8); raw[n + 3] = (uint8_t)(value & 0xFF);
    n += 4;
  }
  return b64enc(raw, n);
}

// отправить оба alarm_set одним запросом (второй POST не влезает в бюджет прохода)
static bool tuyaWriteAlarms(const String& a1, const String& a2) {
  String body = "{\"commands\":[";
  bool first = true;
  if (a2.length()) { body += "{\"code\":\"alarm_set_2\",\"value\":\"" + a2 + "\"}"; first = false; }
  if (a1.length()) { if (!first) body += ','; body += "{\"code\":\"alarm_set_1\",\"value\":\"" + a1 + "\"}"; }
  body += "]}";
  String resp;
  bool ok = cloudCall("POST", String("/v1.0/devices/") + DEVICE_ID + "/commands", resp, 1200, body);
  bool good = ok && resp.indexOf("\"success\":true") >= 0;
  Serial.printf("автомат: запись -> %s (%s)\n", good ? "ok" : "fail", resp.substring(0, 120).c_str());
  return good;
}

bool cloudCall(const String& method, const String& path, String& body, int maxRaw, const String& reqBody) {
  gCliStage = 0; gCliHttp = 0; gCliLen = 0; gCliWant = -1;
  time_t now = time(nullptr);
  if (now < 1600000000) { gCliStage = 1; return false; }   // часы не выставлены
  unsigned long budget = netBudgetLeft();
  if (budget < 1500UL) { gCliStage = 8; return false; }    // бюджет прохода кончился
  // IP облака: резолв идёт в фоне, здесь только берём готовый. Нет IP —
  // пропускаем проход, следующий повторит (DNS не держит цикл).
  tuyaDnsKick(false);
  if (!sTuyaIpOk) { gCliStage = 9; return false; }

  String t = String((unsigned long)(now * 1000ULL % 10000000000ULL));
  String sha = sha256hex(reqBody);
  String sts = method + "\n" + sha + "\n\n" + path;

  // Подпись по документации Tuya v1.0:
  //   client_id + access_token + t + stringToSign
  // Раньше здесь был ещё nonce (t + nonce + sts) — до 31.10.2026 облако его
  // принимало, потом начало отвечать 1004 sign invalid, и напряжение с
  // мощностью перестали приходить. Проверено: без nonce запрос проходит,
  // с nonce — 1004. Заголовок nonce отправлять можно, в подпись — нельзя.
  String strSign = String(TUYA_CID);
  if (gTuyaToken.length()) strSign += gTuyaToken;
  strSign += t + sts;

  String sig = hmacHex(String(TUYA_SECRET), strSign);

  String req = method + " " + path + " HTTP/1.1\r\n";
  req += String("Host: ") + TUYA_HOST + "\r\n";
  req += String("client_id: ") + TUYA_CID + "\r\n";
  req += "sign: " + sig + "\r\n";
  req += "t: " + t + "\r\n";
  req += "sign_method: HMAC-SHA256\r\n";
  if (gTuyaToken.length()) req += "access_token: " + gTuyaToken + "\r\n";
  req += "Content-Type: application/json\r\n";
  if (reqBody.length()) req += "Content-Length: " + String(reqBody.length()) + "\r\n";
  req += "Connection: close\r\n\r\n";
  if (reqBody.length()) req += reqBody;

  // IP облака НЕ кэшируем надолго: адрес короткоживущий — фоновый резолв
  // обновляет его раз в 10 минут, connect идёт по актуальному IP с SNI.
  WiFiClientSecure client;
  client.setInsecure();                            // без проверки сертификата
  client.setTimeout(1);                            // TCP: не ждать дольше 1 с
  client.setHandshakeTimeout(2);                   // TLS-рукопожатие: максимум 2 с
  unsigned long d0 = millis();
  esp_task_wdt_reset();                            // сторож: TLS-коннект ниже блокирует
  bool conn = client.connect(sTuyaIp, 443, TUYA_HOST, NULL, NULL, NULL);
  gMsTls = millis() - d0;
  esp_task_wdt_reset();
  if (!conn) {
    // ГЛАВНАЯ причина «облако живо, а напряжения нет»: mbedTLS просит под TLS
    // 16 КБ на буфер приёма и передачи. Если в куче уже лежит кэш веб-страницы
    // (30 КБ), блока не находится — connect падает за ~70 мс с
    // "SSL - Memory allocation failed", хотя TCP до облака открывается.
    // Проверено на живой плате: сразу после GET / tls=0, после /pagefree tls=1.
    // Лечим на месте: отдаём кэш страницы и пробуем снова.
    pageCacheFree();
    client.stop();
    esp_task_wdt_reset();
    // Между попытками не «замираем»: отдаём такт тачу и вебу. Именно этот
    // двухсекундный затык делал плату неотзывчивой при отказе облака.
    handleTouch();
    if (!gWebInHandler) gWeb.handleClient();
    esp_task_wdt_reset();
    // Фоновый запрос (дозаполнение): второй коннект в ЭТОМ проходе не делаем —
    // он удваивал блокировку цикла (до 10.7 с), а данные не срочные. Выход,
    // повтор — следующим проходом loop() (pwrFillTick держит паузу 3 с).
    if (gCloudBgCall) { gCliStage = 2; clRingAdd(2, gCloudWhy, 0, 0); return false; }
    // Бюджет прохода: на вторую попытку должно остаться хотя бы 1.5 с.
    if (netBudgetLeft() < 1500UL) { gCliStage = 8; clRingAdd(8, gCloudWhy, 0, 0); return false; }
    delay(10);
    esp_task_wdt_reset();
    unsigned long left2 = netBudgetLeft();
    if (left2 < 900UL) { gCliStage = 8; clRingAdd(8, gCloudWhy, 0, 0); return false; }
    conn = client.connect(sTuyaIp, 443, TUYA_HOST, NULL, NULL, NULL);
    gMsTls = millis() - d0;
    esp_task_wdt_reset();
    if (!conn) tuyaDnsKick(true);                  // IP мог устареть — резолв заново
  }
  if (!conn) { gCliStage = 2; clRingAdd(2, gCloudWhy, 0, 0); esp_task_wdt_reset(); client.stop(); return false; }
  client.setTimeout(2);                            // чтение: не дольше 2 с на вызов

  if (client.print(req) == 0) { gCliStage = 3; clRingAdd(3, gCloudWhy, 0, 0); client.stop(); return false; }
  esp_task_wdt_reset();                            // сторож: до чтения заголовков

  String hdr;
  unsigned long hb = netBudgetLeft();
  int hto = (int)(hb > 600UL ? 600UL : (hb > 150UL ? hb : 150UL));
  if (!cloudReadHeaders(client, hdr, hto)) { gCliStage = 4; clRingAdd(4, gCloudWhy, 0, 0); client.stop(); return false; }
  // HTTP-код — из первой строки ответа ("HTTP/1.1 200 OK"), для диагностики
  {
    int sp = hdr.indexOf(' ');
    if (sp > 0 && sp + 4 < (int)hdr.length())
      gCliHttp = hdr.substring(sp + 1, sp + 4).toInt();
  }
  if (!hdr.startsWith("HTTP/1.")) { gCliStage = 5; clRingAdd(5, gCloudWhy, gCliHttp, 0); client.stop(); return false; }

  String raw;
  int cl = cloudContentLength(hdr);
  bool chunked = cloudIsChunked(hdr);
  gCliWant = chunked ? -1 : cl;
  // Тело — тоже в рамках бюджета прохода: 6 с на всё тело больше не даём,
  // иначе один «зависший» ответ снова растянет цикл.
  unsigned long bb = netBudgetLeft();
  if (bb > 1200UL) bb = 1200UL;
  if (bb < 300UL) bb = 300UL;
  unsigned long deadline = millis() + bb;
  esp_task_wdt_reset();
  if (chunked) {
    if (!cloudReadChunked(client, raw, maxRaw, deadline)) {
      gCliStage = 6; gCliLen = (int)raw.length();
      clRingAdd(6, gCloudWhy, gCliHttp, gCliLen);
      client.stop(); return false;
    }
  } else if (cl >= 0 && cl <= maxRaw) {
    raw.reserve(cl + 8);
    if (!cloudReadN(client, cl, raw, deadline)) {
      gCliStage = 6; gCliLen = (int)raw.length();
      clRingAdd(6, gCloudWhy, gCliHttp, gCliLen);
      client.stop(); return false;
    }
  } else {
    // без Content-Length и без chunked: читаем, пока не замолчит (150 мс тишины)
    unsigned long t1 = millis();
    while (millis() - t1 < 4000UL) {
      if (client.available()) {
        raw += (char)client.read();
        t1 = millis();
        if ((int)raw.length() > maxRaw) break;
      } else if (!client.connected()) {
        break;
      } else if (millis() - t1 > 150UL) {
        break;
      }
      esp_task_wdt_reset();
    }
  }
  client.stop();

  body = raw;
  gCliLen = (int)body.length();
  if (body.length() == 0) {
    // Пустой ответ — частый вид отказа: HTTP 200 без тела, 204, 429 без тела.
    gCliStage = 7; clRingAdd(7, gCloudWhy, gCliHttp, 0);
    return false;
  }
  clRingAdd(0, 0, gCliHttp, gCliLen);
  return true;
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
  gTokenN++;
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
  gCloudTry++;
  gCloudWhy = 0;
  gCloudTryMs = millis();
  if (WiFi.status() != WL_CONNECTED) { gCloudWhy = 1; return false; }
  // Зонд TCP:53 — грубая проверка и иногда врёт: DNS-сервер перестаёт
  // принимать TCP, а интернет жив. Раньше это наглухо отключало облако
  // (net=false в /data — и напряжение «застывало» на часы). Теперь максимум
  // каждая третья попытка идёт в облако даже при «нет интернета»: без сети
  // это 3 с таймаута, зато свежесть напряжения не теряется.
  bool netOk = netOnline();
  if (!netOk && (gCloudTry % 3) != 0) { gCloudWhy = 2; return false; }
  if (!cloudToken()) { gCloudWhy = (gCliStage == 1) ? 1 : ((gCliStage == 2) ? 2 : 3); if (gCliStage == 8) gCloudTryMs = millis() - 19000UL; return false; }

  String body;
  String path = String("/v1.0/devices/") + DEVICE_ID;
  if (!cloudCall("GET", path, body)) { gCloudWhy = 4; if (gCliStage == 8) gCloudTryMs = millis() - 19000UL; return false; }
  if (body.indexOf("\"success\":true") < 0) {
    gTuyaToken = "";                 // токен мог протухнуть — обновим в следующий раз
    gCloudWhy = 5;
    return false;
  }

  String v;
  bool gotV = false;                 // пришло ли в этом ответе напряжение

  // счётчик энергии (в сотых)
  if (cloudValue(body, "total_forward_energy", v)) {
    float e = v.toFloat() / 100.0f;
    if (e > 0 && e < 100000) { gEnergy = e; gEnergyMs = millis(); }
  }

  // напряжение и мощность в одной строке base64
  if (cloudValue(body, "alarm_set_1", v)) gAlarm1 = v;
  if (cloudValue(body, "alarm_set_2", v)) gAlarm2 = v;

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
      if (volt > 80 && volt < 400) { gVoltage = volt; gPhaseMs = millis(); gPhaseN++; gotV = true; }
      if (pwr >= 0 && pwr < 20000) { gPower = pwr; gPowerMs = millis(); }
      if (ampCA >= 0 && ampCA < 100) gCurrent = ampCA;   // реальный ток из облака
    }
  }

  // температура (в десятых)
  if (cloudValue(body, "temp_current", v)) {
    float t = v.toFloat() / 10.0f;
    if (t > -40 && t < 125) { gTemp = t; gTempValid = true; gTempMs = millis(); }
  }

  // реле
  if (cloudValue(body, "switch", v)) {
    gRelay = (v == "true" || v == "1");
  }

  if (!gotV) { gCloudWhy = 6; return false; }   // ответ есть, напряжения в нём нет
  gCloudOk = true;
  gTuyaOk = true;
  gCloudOkN++;
  gCloudFailRun = 0;
  gLastOk = millis();
  gCost = myTodayCost;
  return true;
}

// ============================================================================
//  ОБЛАКО ПО СВОЕМУ ТАЙМЕРУ — вне сетевого автомата и вне PWRFILL/GAPFILL.
//  Причина: cloudStatus() стоял в gNetStep==2 и делил очередь с tuyaQuery,
//  PWRFILL, GAPFILL и histTick. Любая заминка в соседе (или сброс gNetStep)
//  оставляла напряжение застывшим на часы — ровно то, на что жаловались.
//  Теперь напряжение — приоритет №1: пока vAge > 60 с, облако зовём каждые
//  20 с, что бы ни делали остальные задачи.
// ============================================================================
void cloudTick() {
  static unsigned long lastLog = 0;
  unsigned long vAge = voltAgeMs();

  // ---- эскалация после подряд идущих отказов (лечим залипшие состояния)
  if (gCloudFailRun != 0) {
    if (gCloudFailRun == 3) {              // ~50 с: переспросить зонд и токен
      gNetProbeMs = 0;
      gTuyaToken  = "";
      Serial.printf("облако: 3 отказа подряд (причина %d, этап %s) — сброс зонда и токена\n",
                    gCloudWhy, cliStageName(gCliStage));
    }
    if (gCloudFailRun == 6) {              // ~2 мин: переподключить Wi-Fi
      gNetProbeMs = 0; gTuyaToken = "";
      if (!gWifiOff && gWifiSetState == 0 && WiFi.status() == WL_CONNECTED) {
        Serial.println("облако: 6 отказов подряд — WiFi.reconnect()");
        WiFi.reconnect();
      }
    }
    if (gCloudFailRun == 10) {             // ~3.5 мин: перезапустить станцию целиком
      gNetProbeMs = 0; gTuyaToken = "";
      if (!gWifiOff && gWifiSetState == 0) {
        Serial.println("облако: 10 отказов подряд — перезапуск Wi-Fi-станции");
        WiFi.disconnect();
        WiFi.begin();
      }
    }
  }

  if (gWifiOff || gNetForceOff) return;
  // не чаще раза в 20 с; после 20 отказов подряд — раз в минуту, чтобы не злить облако
  unsigned long period = (gCloudFailRun >= 20) ? 60000UL : 20000UL;
  if (millis() - gCloudTryMs < period) return;
  if (gPhaseMs != 0 && vAge < 60000UL) return;   // напряжение свежее — не тратим запрос

  unsigned long q0 = millis();
  bool ok = cloudStatus();
  gMsCloud = millis() - q0;
  if (ok) return;
  // Бюджет сети на этот проход кончился — это НЕ отказ облака: не считаем его
  // в gCloudFailRun и не запускаем эскалацию, просто повторим в следующем loop.
  if (gCliStage == 8) return;

  gCloudFailRun++;
  if (millis() - lastLog > 60000UL) {      // не заливаем serial одной и той же строкой
    lastLog = millis();
    Serial.printf("облако-ERR: причина %d, этап %s, HTTP %d, тело %d/%d, подряд %lu, vAge %lu с\n",
                  gCloudWhy, cliStageName(gCliStage), gCliHttp, gCliLen, gCliWant,
                  gCloudFailRun, (millis() - gPhaseMs) / 1000UL);
  }
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

long  histDayDone = -2;             // день, за который уже восстанавливали (сегодня)
long  histYestDone = -2;            // вчерашний день, который уже проверяли
bool  gHistWant = false;            // нужно восстановление
bool  gHistForce = false;           // команда CSVHIST — принудительно
int   gHistStep = 0;                // автомат: 0 — простой
int   gHistAdded = 0;               // сколько часов дописано
unsigned long gHistRetry = 0;       // пауза после неудачи
// Файл /energy.csv должен оставаться в хронологическом порядке: все читатели
// (findDayStartOffset, /day, seedStatsFromSd) ищут сегодняшние строки с конца.
// Поэтому вчерашние строки можно дописывать только ПОКА сегодняшних ещё нет.
// На время проверки/дозаполнения вчера придерживаем writeSd (не дольше 2 мин).
bool  gHistHold = false;
unsigned long gHistHoldMs = 0;
#define HIST_HOLD_MS 120000UL

// Цель текущего прохода: сегодня или вчера.
static bool hTargetYest = false;
static int  hTargetY = 0, hTargetM = 0, hTargetD = 0;
static int  hEndHour = 0;           // проверяем часы [0..hEndHour)

void loadHistDay() {
  Preferences p; p.begin("hist", true);
  histDayDone = p.getLong("day", -2);
  histYestDone = p.getLong("ydone", -2);
  p.end();
}
void saveHistDay(long d) {
  Preferences p; p.begin("hist", false);
  p.putLong("day", d);
  p.end();
  histDayDone = d;
}
void saveHistYest(long d) {
  Preferences p; p.begin("hist", false);
  p.putLong("ydone", d);
  p.end();
  histYestDone = d;
}

// Один шаг автомата. Вызывается из loop() каждый проход и сразу выходит,
// если восстанавливать нечего или интернета нет.
void histTick() {
  if (pwrHistActive()) return;                  // PWRHIST сам ходит в облако
  if (gapFillActive()) return;                  // GAPFILL тоже (иначе лимит облака)
  // Напряжение устарело — приоритет у cloudTick(), история подождёт.
  if (voltStale(180000UL)) return;
  // касание в любой момент — откладываем любой шаг истории
  if (millis() < gTouchSkip) return;
  // ---- шаг 0: условия и «надо ли»
  if (gHistStep == 0) {
    struct tm ti;
    if (!getLocalTime(&ti, 5)) return;
    long todayKey = daysFromCivil(ti.tm_year + 1900, ti.tm_mon + 1, ti.tm_mday);
    if (todayKey <= 0) return;
    // Новый день — снова надо. Вчерашний день проверяем отдельно: если он
    // ещё не проверялся, тоже надо (например, плата сутки стояла).
    if (histDayDone != todayKey || histYestDone != todayKey - 1)
      gHistWant = true;
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
    // Придерживаем запись сегодняшних строк ТОЛЬКО пока их ещё нет: тогда
    // вчерашние (если их не хватает) лягут до них и файл останется в порядке
    // возрастания дат. Если сегодняшние уже есть, вчера всё равно пропустим
    // (см. шаг 7) — держать запись незачем.
    char tp[12];
    snprintf(tp, sizeof(tp), "%04d-%02d-%02d",
             ti.tm_year + 1900, ti.tm_mon + 1, ti.tm_mday);
    if (!fileTailHasDay(tp)) {
      gHistHold = true;
      gHistHoldMs = millis();
    }
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
      gHistHold = false;
      return;
    }
    struct tm ti;
    long todayKey = 0;
    if (getLocalTime(&ti, 5))
      todayKey = daysFromCivil(ti.tm_year + 1900, ti.tm_mon + 1, ti.tm_mday);
    // Сегодня уже восстановлен (или CSVHIST) — сразу к вчерашнему дню.
    gHistStep = (gHistForce || todayKey <= 0 || histDayDone != todayKey) ? 4 : 7;
    return;
  }

  // ---- шаг 2: ОДИН недостающий час за проход, не больше 100 записей
  if (gHistStep == 2) {
    if (millis() - gTouchMs < 1500UL) return;          // не мешаем тачу
    if (hListI >= hListN) { gHistStep = 5; return; }
    int hh = hList[hListI];

    // Границы часа — по местному времени (как в CSV), в запрос — UTC-мс.
    // Дата берётся из цели: сегодня или вчера.
    struct tm t0;
    memset(&t0, 0, sizeof(t0));
    t0.tm_year = hTargetY - 1900; t0.tm_mon = hTargetM - 1; t0.tm_mday = hTargetD;
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
    if (repWait()) return;                        // общий лимит report-logs
    bool okH = cloudCall("GET", String(path), hBody, 12000);
    repDone();
    if (!okH ||
        hBody.indexOf("\"success\":true") < 0) {
      Serial.println("история: запрос не удался — повтор позже");
      gTuyaToken = "";                                 // токен мог протухнуть
      gHistRetry = millis() + 600000UL;
      gHistStep = 0;
      gHistWant = false;
      gHistHold = false;
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
      // Первая ПРИНЯТАЯ запись часа: по ней и будет время восстановленной
      // строки. Считаем до инкрементов n/nT — иначе условие уже не сработает.
      bool firstInHour = (hSlot[hh].n == 0 && hSlot[hh].nT == 0);

      // Температура приходит просто числом («214», «-50»), напряжение — base64
      // («CUcAAJYAABM=»). Различаем по содержимому: цифры, минус и точка =
      // температура. Раньше минус не считался цифрой, и «-50» уходило в
      // base64-ветку, где терялось.
      bool digitsOnly = b64.length() > 0;
      for (int k = 0; k < (int)b64.length(); k++)
        if (!isDigit(b64[k]) && b64[k] != '-' && b64[k] != '.') { digitsOnly = false; break; }

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
      // Время начала часа: ts пишем только с ПЕРВОЙ записи часа, иначе
      // восстановленная строка получала время ПОСЛЕДНЕЙ записи (14:55 вместо
      // 14:00).
      if (firstInHour)
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
    if (!getLocalTime(&ti, 5)) { gHistStep = 0; gHistHold = false; return; }
    hTargetYest = false;
    hTargetY = ti.tm_year + 1900;
    hTargetM = ti.tm_mon + 1;
    hTargetD = ti.tm_mday;
    hEndHour = ti.tm_hour;                 // текущий час не трогаем — его пишем вживую
    char dayPrefix[12];
    snprintf(dayPrefix, sizeof(dayPrefix), "%04d-%02d-%02d",
             ti.tm_year + 1900, ti.tm_mon + 1, ti.tm_mday);
    File f = SD.open("/energy.csv", FILE_READ);
    if (!f) { gHistStep = 0; gHistHold = false; return; }
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
    for (int h = 0; h < hEndHour && h < 24; h++) {
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
    char dayPrefix[12];
    snprintf(dayPrefix, sizeof(dayPrefix), "%04d-%02d-%02d",
             hTargetY, hTargetM, hTargetD);
    // Страховка для вчера: пока шли облачные запросы, в файле могли появиться
    // сегодняшние строки. Тогда вчерашние дописывать нельзя — порядок дат
    // в /energy.csv нарушится, и поиск сегодняшних строк с конца сломается.
    if (hTargetYest) {
      struct tm nt;
      char tp[12] = "";
      bool okN = getLocalTime(&nt, 5);
      if (okN)
        snprintf(tp, sizeof(tp), "%04d-%02d-%02d", nt.tm_year + 1900, nt.tm_mon + 1, nt.tm_mday);
      if (!okN || fileTailHasDay(tp)) {
        Serial.println("история: вчера НЕ дописываю — в файле уже есть сегодня");
        gHistWant = false; gHistForce = false; gHistStep = 0; gHistHold = false;
        return;
      }
    }
    int nowHour = hEndHour;
    int fill[24], nf = 0, empty = 0;
    for (int h = 0; h < nowHour && h < 24; h++) {
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
    if (!hTargetYest && gHistAdded > 0) {
      resetStats();
      statsSeeded = false;
      seedStatsFromSd();          // мин/макс/среднее/график — заново по карте
      gLastDraw = 0;
    }
    struct tm ti;
    long todayKey = 0;
    if (getLocalTime(&ti, 5))
      todayKey = daysFromCivil(ti.tm_year + 1900, ti.tm_mon + 1, ti.tm_mday);

    if (hTargetYest) {
      if (todayKey > 1) saveHistYest(todayKey - 1);
      Serial.printf("история: вчера готово, дописано часов %d\n", gHistAdded);
      gHistWant = false;
      gHistForce = false;
      gHistStep = 0;
      gHistHold = false;
      return;
    }

    if (todayKey > 0) saveHistDay(todayKey);
    // Сегодня закончили — проверяем вчера. Запись сегодняшних строк ещё
    // придержана (gHistHold), чтобы вчерашние легли до них.
    if (todayKey > 1 && histYestDone != todayKey - 1) {
      gHistStep = 7;
      Serial.println("история: сегодня готово, проверяю вчера");
      return;
    }
    gHistWant = false;
    gHistForce = false;
    gHistStep = 0;
    gHistHold = false;
    Serial.printf("история: готово, дописано часов %d\n", gHistAdded);
    return;
  }

  // ---- шаг 7: вчерашний день — есть ли пропущенные часы
  // Файл /energy.csv хронологический, поэтому вчерашние строки можно
  // дописывать только пока сегодняшних строк нет (writeSd придержан).
  // Если сегодняшние уже есть — честно пропускаем: вставка в середину
  // файла на SD требует перезаписи мегабайтов и опасна для карты.
  if (gHistStep == 7) {
    if (millis() - gTouchMs < 1500UL) return;          // не мешаем тачу
    struct tm ti;
    if (!getLocalTime(&ti, 5)) {
      gHistStep = 0; gHistWant = false; gHistHold = false; return;
    }
    long todayKey = daysFromCivil(ti.tm_year + 1900, ti.tm_mon + 1, ti.tm_mday);
    if (todayKey <= 1) {
      gHistWant = false; gHistStep = 0; gHistHold = false; return;
    }
    if (histYestDone == todayKey - 1) {
      gHistWant = false; gHistForce = false; gHistStep = 0; gHistHold = false; return;
    }

    char todayPrefix[12];
    snprintf(todayPrefix, sizeof(todayPrefix), "%04d-%02d-%02d",
             ti.tm_year + 1900, ti.tm_mon + 1, ti.tm_mday);
    if (fileTailHasDay(todayPrefix)) {
      Serial.println("история: вчера пропущено — в файле уже есть сегодня");
      saveHistYest(todayKey - 1);
      gHistWant = false; gHistForce = false; gHistStep = 0; gHistHold = false;
      return;
    }

    // Вчерашняя календарная дата (через местное время, а не арифметикой дней).
    time_t nowT = time(nullptr);
    struct tm ty;
    localtime_r(&nowT - 86400, &ty);
    hTargetYest = true;
    hTargetY = ty.tm_year + 1900;
    hTargetM = ty.tm_mon + 1;
    hTargetD = ty.tm_mday;
    hEndHour = 24;
    char dayPrefix[12];
    snprintf(dayPrefix, sizeof(dayPrefix), "%04d-%02d-%02d", hTargetY, hTargetM, hTargetD);

    memset(hSlot, 0, sizeof(hSlot));
    memset(hHave, 0, sizeof(hHave));
    hListN = 0; hListI = 0;
    gHistAdded = 0;

    File f = SD.open("/energy.csv", FILE_READ);
    if (f) {
      size_t off = findDayStartOffset(f, dayPrefix);
      if (off > 0 || fileTailIsToday(f, dayPrefix)) {
        f.seek(off);
        int rd = 0;
        while (f.available()) {
          String l = f.readStringUntil('\n');
          if ((++rd & 255) == 0) esp_task_wdt_reset();
          if (!l.startsWith(dayPrefix)) continue;
          long t = lineEpoch(l);
          if (!t) continue;
          hHave[(int)((t % 86400L) / 3600)] = 1;
        }
      }
      f.close();
    }
    int haveN = 0;
    for (int h = 0; h < 24; h++) if (hHave[h]) haveN++;
    Serial.printf("история: вчера %s — часов в карте %d, пустых %d\n",
                  dayPrefix, haveN, 24 - haveN);
    if (haveN >= 24) {
      saveHistYest(todayKey - 1);
      gHistWant = false; gHistForce = false; gHistStep = 0; gHistHold = false;
      return;
    }
    for (int h = 0; h < 24; h++)
      if (!hHave[h]) hList[hListN++] = (uint8_t)h;
    gHistStep = 2;
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

// Применить dps из JSON к нашим переменным. Возвращает true, если пришло
// напряжение/мощность (DP 6).
// ВАЖНО: каждый DP применяем ТОЛЬКО если он реально есть в кадре — реле
// присылает отчёты по одному полю, и «switch» в таком кадре нет.
bool applyDps(const char* js) {
  long e = 0, t10 = 0;
  bool sw = false;
  if (findDpInt(js, "1", &e) && e > 0) { gEnergy = e / 100.0f; gEnergyMs = millis(); }
  if (findDpBool(js, "16", &sw)) gRelay = sw;
  if (findDpInt(js, "101", &t10)) { gTemp = t10 / 10.0f; gTempValid = true; gTempMs = millis(); }
  return parsePhase(js);
}

// Разобрать УЖЕ ПРИШЕДШИЕ кадры от реле, ничего не выбрасывая.
// Раньше здесь стоял drainSocket(), который просто читал и ВЫБРАСЫВАЛ байты:
// вместе с ними терялись и push-отчёты с DP 6 (напряжение/мощность). Из-за
// этого напряжение приходило только из облака — раз в минуту и через TLS.
// Теперь ждём только если в сокете уже есть заголовок кадра: без данных
// функция выходит сразу и цикл не тормозит.
int tuyaPollPushes(unsigned long budgetMs, bool* gotPhase) {
  int frames = 0;
  unsigned long t0 = millis();
  while (millis() - t0 <= budgetMs && sock.available() >= (int)HEADER_LEN) {
    uint8_t buf[1024], payload[512], cmd = 0;
    int got = readFrame(buf, sizeof(buf), 120);
    if (got < 0) {
      // Кадр не разобрался (обрезан/больше буфера) — поток сбит, начинаем заново.
      sock.stop();
      sessionReady = false;
      break;
    }
    int plen = parseFrame(buf, got, sessionKey, &cmd, payload, sizeof(payload));
    if (plen <= 4 || plen >= (int)sizeof(payload)) continue;
    payload[plen] = 0;
    const char* js = (const char*)(payload + 4);
    if (!strstr(js, "\"dps\"")) continue;
    frames++;
    if (applyDps(js) && gotPhase) *gotPhase = true;
    esp_task_wdt_reset();
  }
  return frames;
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

  // Сначала забираем то, что реле уже прислало само (push-отчёты с DP 6).
  // Если напряжения ещё нет — спрашиваем явно.
  bool polledPhase = false;
  int pushN = tuyaPollPushes(150UL, &polledPhase);
  if (pushN > 0) { gotDps = true; gotPhase = polledPhase; }

  // DP 6 (напряжение и мощность) приходит не на каждый запрос, поэтому раньше
  // делали три попытки с таймаутом 5 с — это 23 с блокировки цикла. Теперь
  // одна попытка и короткий таймаут: не пришло — спросим в следующем проходе.
  for (int attempt = 0; attempt < (gotPhase ? 0 : 1); attempt++) {
    seqNo++;
    const char* req = "{}";
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
    if (applyDps(js)) gotPhase = true;
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
  // HTTPClient блокирует цикл целиком (connect + чтение). Один вызов не должен
  // быть длиннее пары секунд, и перед ним/после него кормим сторожа.
  esp_task_wdt_reset();
  if (!h.begin(url)) return false;
  int code = h.GET();
  esp_task_wdt_reset();
  if (code != 200) { h.end(); return false; }
  out = h.getString();
  esp_task_wdt_reset();
  h.end();
  return true;
}

bool fetchFromApi() {
  gApiN++;
  gApiLast = millis();
  String body;
  // ПК-API (192.168.1.20:8090) давно мёртв, но раз в 5 минут всё равно
  // опрашивался с таймаутом 2.5 с: 01.11.2026 на живой плате это давало ровно
  // один проход loop() на 5.0-5.6 с каждые 5 минут (loopMaxAll 5014, 5566),
  // никак не связанный с pwrfill. 0.9 с на коннект и чтение: живой сервер в
  // локальной сети отвечает за десятки миллисекунд, мёртвый — не держит цикл.
  if (!httpGet(String(PC_API) + "/relay", body, 900)) return false;

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
  if (t > -900 && t > -40 && t < 125) { gTemp = t; gTempValid = true; gTempMs = millis(); }
  float ek = num("energy_kwh"); if (ek > 0) { gEnergy = ek; gEnergyMs = millis(); }

  // Второй запрос (/stats) — не чаще раза в 5 минут: он нужен только для
  // сверки «сегодня» с компьютером, а висит столько же, сколько /relay.
  static unsigned long lastStats = 0;
  String st;
  if ((lastStats == 0 || millis() - lastStats > 300000UL) &&
      httpGet(String(PC_API) + "/stats", st, 900)) {
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

// Кириллица в шрифте 5x7 только ЗАГЛАВНАЯ: OWM с lang=ru отдаёт описание
// строчными («пасмурно»), поэтому перед выводом поднимаем регистр.
static void upperRus(char* s) {
  for (unsigned char* p = (unsigned char*)s; *p; p++) {
    if (*p >= 'a' && *p <= 'z') { *p -= 32; continue; }
    // а-п (D0 B0..BF) -> А-П (D0 90..9F)
    if (p[0] == 0xD0 && p[1] >= 0xB0 && p[1] <= 0xBF) { p[1] -= 0x20; p++; continue; }
    // р-я (D1 80..8F) -> Р-Я (D0 A0..AF)
    if (p[0] == 0xD1 && p[1] >= 0x80 && p[1] <= 0x8F) { p[0] = 0xD0; p[1] += 0x20; p++; continue; }
    // ё (D1 91) -> Ё (D0 81)
    if (p[0] == 0xD1 && p[1] == 0x91) { p[0] = 0xD0; p[1] = 0x81; p++; }
  }
}

// Текст описания для экрана: русское из OWM, иначе перевод английского.
// maxChars — предел по СИМВОЛАМ (UTF-8), а не по байтам.
static void weatherDesc(char* out, size_t n, int maxChars) {
  if (wDescRu.length()) snprintf(out, n, "%s", wDescRu.c_str());
  else                  descToRus(wDescEn, out, n);
  upperRus(out);
  int k = 0;
  for (char* p = out; *p; ) {
    if (k >= maxChars) { *p = 0; break; }
    p += utf8len((unsigned char)*p);
    k++;
  }
}


// ============================================================================
//  История погоды: одна строка на день в /weather.csv
//  Формат: ГГГГ-ММ-ДД,tmin,tmax,описание,вероятность дождя,осадки
//  Нужна, чтобы в статистике за период была погода за каждый день.
// ============================================================================
#define WLOG_PATH "/weather.csv"
static float    wLogTMin = 999, wLogTMax = -999;   // фактические мин/макс за сегодня
static uint32_t wLogDay = 0;                        // день, к которому относятся

static uint32_t wDayKey(int y, int mo, int d) {
  return (uint32_t)daysFromCivil(y, (unsigned)mo, (unsigned)d);
}

// обновить строку за сегодня (перезапись файла: он маленький, ~40 байт на день)
static void weatherLogTouch() {
  struct tm ti;
  if (!getLocalTime(&ti, 10)) return;
  int y = ti.tm_year + 1900, mo = ti.tm_mon + 1, d = ti.tm_mday;
  uint32_t key = wDayKey(y, mo, d);
  if (key != wLogDay) { wLogDay = key; wLogTMin = 999; wLogTMax = -999; }
  if (gTempValid && gTemp > -60 && gTemp < 80) {
    if (gTemp < wLogTMin) wLogTMin = gTemp;
    if (gTemp > wLogTMax) wLogTMax = gTemp;
  }
  // описание: текущее, иначе прогноз на завтра
  const char* desc = wDescRu.length() ? wDescRu.c_str() : "—";
  char line[128];
  snprintf(line, sizeof(line), "%04d-%02d-%02d,%.1f,%.1f,%s,%.0f,%.1f",
           y, mo, d,
           (wLogTMin == 999 ? 0.0f : wLogTMin),
           (wLogTMax == -999 ? 0.0f : wLogTMax),
           desc, wPopNow, 0.0f);

  // читаем старый файл, заменяем/дописываем строку этого дня
  File in = SD.open(WLOG_PATH, FILE_READ);
  File out = SD.open("/weather.tmp", FILE_WRITE);
  if (!out) { if (in) in.close(); return; }
  bool replaced = false;
  if (in) {
    while (in.available()) {
      String l = in.readStringUntil('\n');
      l.trim();
      if (!l.length()) continue;
      if (l.startsWith(line, 10)) {           // та же дата — заменяем
        out.println(line);
        replaced = true;
      } else {
        out.println(l);
      }
    }
    in.close();
  }
  if (!replaced) out.println(line);
  out.close();
  SD.remove(WLOG_PATH);
  SD.rename("/weather.tmp", WLOG_PATH);
}

// найти строку погоды за день (возвращает false, если нет)
static bool weatherLogFind(const char* date, char* desc, size_t dn, float* tmin, float* tmax) {
  File f = SD.open(WLOG_PATH, FILE_READ);
  if (!f) return false;
  bool found = false;
  int guard = 0;
  while (f.available() && guard++ < 2000) {
    String l = f.readStringUntil('\n');
    l.trim();
    if (l.length() < 12) continue;
    if (!l.startsWith(date)) continue;
    int c1 = l.indexOf(',');
    int c2 = l.indexOf(',', c1 + 1);
    int c3 = l.indexOf(',', c2 + 1);
    int c4 = l.indexOf(',', c3 + 1);
    if (c1 > 0 && c2 > c1 && c3 > c2) {
      *tmin = l.substring(c1 + 1, c2).toFloat();
      *tmax = l.substring(c2 + 1, c3).toFloat();
      if (c4 > c3) {
        String d = l.substring(c3 + 1, c4);
        strncpy(desc, d.c_str(), dn - 1);
        desc[dn - 1] = 0;
      }
      found = true;
    }
    break;
  }
  f.close();
  return found;
}

bool fetchWeather() {
  // Погода — OpenWeatherMap по HTTPS. wttr.in в сети пользователя заблокирован.
  // TLS-клиент живёт на стеке: после запроса stop() и деструктор возвращают
  // память mbedTLS (без этого течёт ~40 КБ на каждый запрос).
  if (!netOnline()) return false;
  uint32_t heap0 = ESP.getFreeHeap();
  String j;
  {
    WiFiClientSecure tls;
    tls.setInsecure();              // сертификат не проверяем — как у Tuya
    tls.setHandshakeTimeout(3);     // секунды; иначе зависший TLS держит цикл 120 с
    HTTPClient h;
    h.setTimeout(2500);
    esp_task_wdt_reset();
    if (!h.begin(tls, WEATHER_URL)) {
      Serial.println("погода: begin не принял URL");
      return false;
    }
    int code = h.GET();
    if (code == 200) j = h.getString();
    h.end();
    tls.stop();                     // освободить mbedTLS до разбора JSON
    esp_task_wdt_reset();
    if (code != 200) {
      Serial.printf("погода: HTTP %d\n", code);
      return false;
    }
  }

  if (j.length() < 20) { wCode = 0; return false; }

  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, j);
  if (err) {
    wCode = 0;                      // не распарсилось -> повтор через 2 минуты
    Serial.printf("погода: JSON %s (%u Б)\n", err.c_str(), (unsigned)j.length());
    return false;
  }

  JsonObject m  = doc["main"];
  JsonArray  wa = doc["weather"];
  int id = (wa.isNull() || wa.size() == 0) ? 0 : (int)(wa[0]["id"] | 0);
  if (m.isNull() || id == 0) {
    wCode = 0;
    Serial.println("погода: в JSON нет main/weather[0].id");
    return false;
  }

  wCode   = id;
  wTemp   = m["temp"] | 0.0f;
  wHum    = m["humidity"] | 0;
  // OWM units=metric отдаёт ветер в М/С; на экране и в сигнатуре — км/ч.
  wWind   = (doc["wind"]["speed"] | 0.0f) * 3.6f;
  wDescRu = wa[0]["description"].as<String>();
  wDescRu.trim();
  wDescEn = wa[0]["main"].as<String>();   // английское — только для иконки
  if (wDescEn.length() == 0) wDescEn = "Clouds";

  wOk   = true;
  wLast = millis();
  wTime = time(nullptr);
  wHeap = ESP.getFreeHeap();
  Serial.printf("погода: %.1f C, %.0f%%, %.1f км/ч, id=%d, %s (heap %u -> %u)\n",
                wTemp, wHum, wWind, wCode, wDescRu.c_str(), heap0, wHeap);
  return true;
}

// ---------------------------------------------------------------- прогноз
// «Плохость» погоды: гроза > снег > дождь > облачно > ясно. По этому числу
// выбираем описание завтра — самое мрачное из встреченных за день.
static int wxSeverity(int id) {
  if (id >= 200 && id < 300) return 5;   // гроза
  if (id >= 600 && id < 700) return 4;   // снег
  if (id >= 300 && id < 600) return 3;   // дождь, морось, ливень
  if (id >= 700 && id < 800) return 2;   // туман, дымка
  if (id == 800)             return 0;   // ясно
  if (id <= 802)             return 1;   // малооблачно / переменная облачность
  return 2;                              // 803/804 — пасмурно
}

// Короткое русское описание по коду OWM (в шрифте 5x7 только заглавные).
static void wxShortRus(int id, char* out, size_t n) {
  if (id >= 200 && id < 300)      snprintf(out, n, "ГРОЗА");
  else if (id >= 300 && id < 400) snprintf(out, n, "МОРОСЬ");
  else if (id >= 500 && id < 505) snprintf(out, n, "ДОЖДЬ");
  else if (id >= 505 && id < 600) snprintf(out, n, "ЛИВЕНЬ");
  else if (id >= 600 && id < 700) snprintf(out, n, "СНЕГ");
  else if (id >= 700 && id < 800) snprintf(out, n, "ТУМАН");
  else if (id == 800)             snprintf(out, n, "ЯСНО");
  else if (id <= 802)             snprintf(out, n, "МАЛООБЛАЧНО");
  else                            snprintf(out, n, "ПАСМУРНО");
}

bool fetchForecast() {
  // Прогноз OWM (16 шагов по 3 ч). Без фильтра ArduinoJson документ на 7-20 КБ
  // съедает heap целиком, поэтому разбираем ТОЛЬКО нужные поля.
  // TLS-клиент на стеке + stop(): mbedTLS возвращает память сразу.
  if (!netOnline()) return false;
  // mbedTLS нужно ~35 КБ ПОДРЯД: смотрим максимальный непрерывный блок,
  // а не свободную кучу. Если его нет — запрос даже не начинаем.
  if (ESP.getMaxAllocHeap() < 40000) { wFcFail++; return false; }

  uint32_t heap0 = ESP.getFreeHeap();
  String j;
  {
    WiFiClientSecure tls;
    tls.setInsecure();              // сертификат не проверяем — как у Tuya
    tls.setHandshakeTimeout(15);
    HTTPClient h;
    h.setTimeout(25000);   // 7 КБ тела: 2.5 с не хватало, getString() возвращал пусто
    esp_task_wdt_reset();
    if (!h.begin(tls, FORECAST_URL)) {
      Serial.println("прогноз: begin не принял URL");
      wFcFail++;
      return false;
    }
    h.setReuse(false);
    int code = h.GET();
    if (code == 200) {
      // getString() читает поток «как есть»: у ответа с Transfer-Encoding:
      // chunked в строку попадали служебные байты размера чанков — JSON не
      // разбирался (InvalidInput/IncompleteInput), а иногда тело было пустым.
      // writeToStream() снимает chunked-обёртку сам.
      StreamString ss;
      h.writeToStream(&ss);
      j = ss;
      if (j.length() < 40)
        Serial.printf("прогноз: тело %u Б (size=%d code=%d)\n",
                      (unsigned)j.length(), (int)h.getSize(), code);
    }
    h.end();
    tls.stop();                     // освободить mbedTLS до разбора JSON
    esp_task_wdt_reset();
    if (code != 200) {
      Serial.printf("прогноз: HTTP %d\n", code);
      wFcFail++;
      return false;
    }
  }
  if (j.length() < 40) { wFcFail++; Serial.println("прогноз: пустой ответ"); return false; }

  // Без часов «завтра» не посчитать: границы дня берём по местному времени.
  struct tm ti;
  if (!getLocalTime(&ti, 10)) {
    wFcFail++;
    Serial.println("прогноз: нет времени (NTP)");
    return false;
  }

  // Фильтр: массив в фильтре задаётся через [0] и применяется ко ВСЕМ
  // элементам (проверено — ArduinoJson 7).
  JsonDocument filter;
  filter["list"][0]["dt"]                  = true;
  filter["list"][0]["main"]["temp_min"]    = true;
  filter["list"][0]["main"]["temp_max"]    = true;
  filter["list"][0]["weather"][0]["id"]    = true;
  filter["list"][0]["pop"]                 = true;
  filter["list"][0]["rain"]["3h"]          = true;

  JsonDocument doc;
  DeserializationError err =
      deserializeJson(doc, j, DeserializationOption::Filter(filter));
  j = String();                    // строка ответа больше не нужна
  if (err) {
    wFcFail++;
    Serial.printf("прогноз: JSON %s\n", err.c_str());
    return false;
  }

  JsonArray list = doc["list"].as<JsonArray>();
  if (list.isNull() || list.size() == 0) {
    wFcFail++;
    Serial.println("прогноз: в JSON нет list");
    return false;
  }

  // Границы «завтра» по местному времени: сегодня 00:00 + 1 сутки.
  struct tm t0 = ti;
  t0.tm_hour = 0; t0.tm_min = 0; t0.tm_sec = 0; t0.tm_isdst = 0;
  time_t dayStart = mktime(&t0);
  time_t tomStart = dayStart + 86400;
  time_t tomEnd   = tomStart + 86400;

  float pop  = 0;          // максимум вероятности осадков за завтра
  float tmin = 999, tmax = -999;
  float rain = 0;          // сумма rain.3h за завтра, мм
  int   worst = -1;        // самый «плохой» код погоды за завтра
  int   n = 0;             // сколько шагов попало в завтра

  for (JsonObject it : list) {
    time_t dt = (time_t)(long)(it["dt"] | 0L);
    if (dt < tomStart || dt >= tomEnd) continue;   // не завтра — пропускаем
    n++;
    float mn = it["main"]["temp_min"] | 0.0f;
    float mx = it["main"]["temp_max"] | 0.0f;
    float pp = it["pop"] | 0.0f;
    if (mn < tmin) tmin = mn;
    if (mx > tmax) tmax = mx;
    if (pp > pop)  pop  = pp;
    rain += it["rain"]["3h"] | 0.0f;
    int id = it["weather"][0]["id"] | 0;            // 0 — поля нет
    if (id && (worst < 0 || wxSeverity(id) > wxSeverity(worst))) worst = id;
  }

  if (n == 0) {                    // прогноз короче суток — такое почти невозможно
    wFcFail++;
    Serial.println("прогноз: завтра не попало в 16 шагов");
    return false;
  }

  wPopNow  = (list[0]["pop"] | 0.0f) * 100.0f;
  wPopTom  = pop * 100.0f;
  wTminTom = tmin;
  wTmaxTom = tmax;
  wRainTom = rain;
  wCodeTom = (worst > 0) ? worst : 0;   // для иконки «завтра» на главном экране
  char d[20];
  if (worst > 0) wxShortRus(worst, d, sizeof(d));
  else           snprintf(d, sizeof(d), "НЕТ ДАННЫХ");
  wDescTom = d;

  wFcOk   = true;
  wFcFail = 0;
  wForecastAt = millis();
  wfHeap  = ESP.getFreeHeap();
  Serial.printf("прогноз: дождь %.0f%%, завтра %.1f..%.1f C, осадки %.0f%%, %s, %.1f мм, шагов %d (heap %u -> %u)\n",
                wPopNow, wTminTom, wTmaxTom, wPopTom, wDescTom.c_str(), wRainTom, n,
                heap0, wfHeap);
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
  esp_task_wdt_reset();            // сторож: запись на карту бывает медленной
  // GAPFILL переписывает /energy.csv целиком: пока идёт подмена, новую строку
  // не пишем — иначе файл вырастет между чтением и записью.
  if (gGapBusy) return;
  // Пока histTick проверяет/дозаполняет вчерашний день, сегодняшние строки
  // не пишем: иначе вчерашние окажутся после них и порядок дат сломается.
  // Дольше двух минут историю не держим — это важнее одной проверки.
  if (gHistHold) {
    if (millis() - gHistHoldMs < HIST_HOLD_MS) return;
    gHistHold = false;
  }
  if (millis() - gSdLast < 30000) return;        // не чаще раза в 30 с
  // Нет напряжения — строку не пишем вообще. Раньше при gEnergy>0 писалась
  // строка с v=0, и она ломала мин/среднее (23-26.10 таких было 100 %).
  // Лучше честный пропуск, чем нуль в истории.
  if (gVoltage < 50) return;
  if (gEnergy <= 0) return;
  // Без времени строка бесполезна: её потом не разобрать по часам.
  struct tm ti;
  if (!getLocalTime(&ti, 100)) return;

  // Смена месяца: старый рабочий файл уходит в /energy-YYYY-MM.csv,
  // новый начинается с заголовка. Пока ротация не удалась — не пишем,
  // чтобы не смешать два месяца в одном файле.
  int monthStamp = (ti.tm_year + 1900) * 12 + ti.tm_mon;
  if (gSdMonth < 0) {
    gSdMonth = monthStamp;
  } else if (monthStamp != gSdMonth) {
    if (!rotateHistory(monthStamp, false)) return;
    gSdMonth = monthStamp;
  }

  char stamp[24];
  snprintf(stamp, sizeof(stamp), "%04d-%02d-%02d %02d:%02d:%02d",
           ti.tm_year + 1900, ti.tm_mon + 1, ti.tm_mday, ti.tm_hour, ti.tm_min, ti.tm_sec);

  File f = SD.open("/energy.csv", FILE_APPEND);
  if (!f) { if (gSdWriteFail < 1000) gSdWriteFail++; return; }
  // 7-я колонка — температура. Если её ещё нет (плата только включилась),
  // поле остаётся пустым: пустое значение разбор пропускает.
  if (gTempValid)
    f.printf("%s,%.0f,%.0f,%.2f,%d,%.2f,%.1f\n", stamp, gVoltage, gPower, gEnergy,
             gRelay ? 1 : 0, gCost, gTemp);
  else
    f.printf("%s,%.0f,%.0f,%.2f,%d,%.2f,\n", stamp, gVoltage, gPower, gEnergy,
             gRelay ? 1 : 0, gCost);
  f.close();
  gSdWriteFail = 0;
  gSdLast = millis();      // отсчёт 30 с — от реальной записи, а не от попытки
  gSdRows++;
}

// ============================================================================
//  ПРИЧИНА ПЕРЕЗАГРУЗКИ: rr / rrn и файл /boots.log на карте
// ============================================================================
// 01.11.2026 в 09:38 плата перезагрузилась сама, и узнать почему было нечем:
// причина нигде не сохранялась. Теперь при каждом старте читаем
// esp_reset_reason() и дописываем строку в /boots.log. CSV (/energy.csv) НЕ
// трогаем: у него строгие 7 полей, строка-маркер сломала бы разбор.
//   rr — код: 1 POWERON, 3 SW_RESET (REBOOT), 4 PANIC, 5 INT_WDT, 6 TASK_WDT,
//             8 DEEPSLEEP, 9 BROWNOUT, 10 SDIO (по esp_reset_reason())
//   rrn — сколько раз плата стартовала за сутки
int  gRRcode = -1;          // esp_reset_reason() этой загрузки (-1 — не читали)
int  gRRraw  = -1;          // rtc_get_reset_reason(0): что видел ROM (до сопоставления)
unsigned long gRRn = 0;     // перезагрузок за сутки
unsigned long gPrevUp = 0;  // сколько проработала ПРОШЛАЯ загрузка, с

RTC_DATA_ATTR static uint32_t gBootDayRtc = 0;   // 20261101 — переживает сброс
RTC_DATA_ATTR static uint16_t gBootCntRtc = 0;   // стартов за этот день
RTC_DATA_ATTR static uint32_t gPrevUpRtc  = 0;   // uptime прошлой загрузки, с
RTC_NOINIT_ATTR static uint8_t gOtaMark;        // 1 = перезагрузка после OTA/reboot (не обнуляется при старте)
static unsigned long gRestartAt = 0;   // отложенный рестарт: не из обработчика HTTP

const char* rrName(int c) {
  switch (c) {
    case 1:  return "POWERON";
    case 2:  return "EXT_PIN";
    case 3:  return "SW_RESET";
    case 4:  return "PANIC";
    case 5:  return "INT_WDT";
    case 6:  return "TASK_WDT";
    case 7:  return "WDT";
    case 8:  return "DEEPSLEEP";
    case 9:  return "BROWNOUT";
    case 10: return "SDIO";
    case 11: return "OTA";
    default: return "UNKNOWN";
  }
}

// Первым делом в setup(): запомнить причину и uptime прошлой загрузки.
// uptime прошлой загрузки живёт в RTC-памяти (переживает REBOOT), а в NVS
// дублируется, чтобы он не пропал и при отключении питания.
static void bootSavePrevUp(unsigned long v) {
  Preferences p;
  if (p.begin("boots", false)) { p.putUInt("prevUp", (uint32_t)v); p.end(); }
}

// перезагрузка по /update или /reboot: помечаем в NVS, чтобы в boots.log
// было rr=OTA, а не PANIC
static void bootMarkOta() {
  gOtaMark = 1;                      // RTC_NOINIT — переживает программный сброс
  Preferences p;
  if (p.begin("boots", false)) { p.putUChar("otaMark", 1); p.end(); }
}

void bootReasonBegin() {
  gRRcode = (int)esp_reset_reason();
  {
    Preferences p;
    if (p.begin("boots", false)) {
      if (p.getUChar("otaMark", 0)) { p.putUChar("otaMark", 0); gRRcode = 11; }
      else if (gOtaMark == 1) gRRcode = 11;
      gOtaMark = 0;
      p.end();
    }
  }
  gRRraw  = (int)esp_rom_get_reset_reason(0);   // «сырой» код RTC
  gPrevUp = gPrevUpRtc;
  if (gPrevUp == 0) {                            // RTC не пережил сброс питания
    Preferences p;
    if (p.begin("boots", true)) { gPrevUp = p.getUInt("prevUp", 0); p.end(); }
  }
  gPrevUpRtc = 0;
}

// rrn: счётчик стартов ЗА СУТКИ. Быстрый путь — RTC-память (без записи во
// флеш), NVS трогаем только при смене дня, т.е. раз в сутки.
void bootCountTick() {
  struct tm ti;
  if (!getLocalTime(&ti, 50)) { gRRn = gBootCntRtc ? gBootCntRtc : 1; return; }
  uint32_t d = (uint32_t)(ti.tm_year + 1900) * 10000UL +
               (uint32_t)(ti.tm_mon + 1) * 100UL + (uint32_t)ti.tm_mday;
  if (gBootDayRtc == d && gBootCntRtc > 0) {
    if (gBootCntRtc < 60000) gBootCntRtc++;
  } else {
    // RTC-память пуста (было отключение питания) — берём счётчик из NVS
    uint16_t nc = 0;
    Preferences p;
    if (p.begin("boots", false)) {
      uint32_t nd = p.getUInt("day", 0);
      if (nd == d) nc = p.getUShort("cnt", 0);
      p.putUInt("day", d);
      p.putUShort("cnt", (uint16_t)(nc + 1));
      p.end();
    }
    gBootDayRtc = d;
    gBootCntRtc = (uint16_t)(nc + 1);
  }
  gRRn = gBootCntRtc;
}

// Строка в /boots.log: дата, причина, номер старта, сколько жила прошлая
// загрузка, свободная куча. Формат — как просили:
//   2026-11-01 09:38:12 rr=TASK_WDT(6) rrn=3 up=486 heap=112936 raw=16
void writeBootLog() {
  if (!gSdOk) return;
  struct tm ti;
  char stamp[24] = "0000-00-00 00:00:00";
  if (getLocalTime(&ti, 50))
    snprintf(stamp, sizeof(stamp), "%04d-%02d-%02d %02d:%02d:%02d",
             ti.tm_year + 1900, ti.tm_mon + 1, ti.tm_mday,
             ti.tm_hour, ti.tm_min, ti.tm_sec);
  // Файл не растим бесконечно: 40 КБ (~600 строк) — потом в .1.log
  File fr = SD.open("/boots.log", FILE_READ);
  size_t sz = fr ? fr.size() : 0;
  if (fr) fr.close();
  if (sz > 40000) {
    SD.remove("/boots.1.log");
    SD.rename("/boots.log", "/boots.1.log");
  }
  File f = SD.open("/boots.log", FILE_APPEND);
  if (!f) return;
  // Перед записью строки фиксируем прошлый uptime в NVS: даже если плата
  // обесточится сразу после старта, при следующей загрузке up_prev не станет 0.
  // Ноль НЕ пишем: иначе первая же строка после прошивки затирала бы прошлое.
  if (gPrevUp) bootSavePrevUp(gPrevUp);
  // up_prev — то же число, что up; отдельное имя, чтобы не путать с текущим
  // uptime и видеть, что запись сделана после перезагрузки.
  f.printf("%s rr=%s(%d) rrn=%lu up=%lu up_prev=%lu heap=%u raw=%d\n", stamp,
           rrName(gRRcode), gRRcode, gRRn, gPrevUp, gPrevUp, ESP.getFreeHeap(), gRRraw);
  f.close();
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

// ---------------------------------------------------------------- метки получения
// «12:31» — если показатель получен сегодня, «ВЧЕРА 23:45» — вчера,
// «30.10 23:45» — раньше. Данным больше 5 минут — метка жёлтая.
bool stampStale(unsigned long ms) {
  return ms != 0 && (millis() - ms) > 300000UL;
}

void stampStr(unsigned long ms, char* b, size_t n) {
  if (ms == 0) { snprintf(b, n, "—"); return; }             // ещё не приходило
  time_t now = time(nullptr);
  if (now < 1700000000L) { snprintf(b, n, "—"); return; }   // часы не синхронизированы
  time_t at = now - (time_t)((millis() - ms) / 1000UL);
  struct tm tn, ta;
  localtime_r(&now, &tn);
  localtime_r(&at, &ta);
  // Разницу в днях считаем от местной полуночи: tm_yday не переживает смену года.
  time_t midN = now - (tn.tm_hour * 3600L + tn.tm_min * 60L + tn.tm_sec);
  time_t midA = at  - (ta.tm_hour * 3600L + ta.tm_min * 60L + ta.tm_sec);
  long days = (long)((midN - midA) / 86400L);
  if (days <= 0)      snprintf(b, n, "%02d:%02d", ta.tm_hour, ta.tm_min);
  else if (days == 1) snprintf(b, n, "ВЧЕРА %02d:%02d", ta.tm_hour, ta.tm_min);
  else                snprintf(b, n, "%02d.%02d %02d:%02d",
                               ta.tm_mday, ta.tm_mon + 1, ta.tm_hour, ta.tm_min);
}

// Строка «ПОЛУЧЕНО 12:31» мелким шрифтом под значением окна.
void drawStamp(int x, int y, unsigned long ms, uint16_t bg) {
  char t[24], b[40];
  stampStr(ms, t, sizeof(t));
  snprintf(b, sizeof(b), "ПОЛУЧЕНО %s", t);
  drawTxt(x, y, b, stampStale(ms) ? cWarn : cMuted, bg, 1);
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
const int TAP_SLOP = 10;          // допуск попадания по тачу, px

struct Block { int x, y, w, h, scr; };
const Block BLOCKS[5] = {
  // Зоны доведены ДО КРАЁВ экрана. Раньше касание у кромки (x=239 при крае
  // карточки 234) промахивалось мимо всех карточек — и «ничего не открывалось».
  {  0,  44, 240, 72, SCR_VOLT    },
  {  0, 112, 116, 64, SCR_TEMP    },
  {116, 112, 124, 64, SCR_POWER   },   // примыкает к ТЕМП без щели
  {  0, 174, 240, 72, SCR_TODAY   },
  {  0, 244, 240, 74, SCR_WEATHER },
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
// Адреса взяты из программы-сторожа (watchdog\cctv-watchdog.ps1).
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
  { "РЕГИСТРАТОР", { 192, 168, 0, 17  }, 34567 },   // Dahua NVR (порт DVRIP)
  { "NVR 1 ЭТАЖ",  { 192, 168, 0, 186 }, 80    },   // Dahua NVR 1 этаж, веб
  { "NAS",         { 192, 168, 0, 222 }, 80    },   // накопитель, веб
  { "КАМЕРА",      { 192, 168, 0, 44  }, 80    },   // камера, веб
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
// конца круга. Тревога — после ДВУХ неудач подряд (10 с): камера/регистратор
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

// ============================================================================
//  ПОДСВЕТКА ЭКРАНА — GPIO 21, ШИМ (канал 0). Команда DISP, эндпоинт /disp.
// ============================================================================
// Было analogWrite(21, 255): всегда на полную. Стало: яркость 0..255 через
// ledc (5 кГц, 8 бит) и запись в NVS. Ночью экран гаснет, тач работает;
// касание включает подсветку на 20 с (DISP NOTOUCH отключает эту реакцию).
#define BL_PIN      21
#define BL_CH       0
#define BL_WAKE_MS  20000UL

static bool parseIntStrict(const String& s, long& out);   // определена ниже

bool     gDispOn     = true;     // подсветка включена
uint8_t  gDispBright = 255;      // рабочая яркость 1..255
bool     gDispTouch  = true;     // касание при погашенном экране включает свет
unsigned long gDispWakeUntil = 0;

// Энергосбережение «только данные»: экран сам гаснет после бездействия, а
// Wi-Fi/MQTT/SD продолжают работать. Таймер и ручное выключение — РАЗНЫЕ
// состояния: ручное (gDispUserOff) таймер не включает и не выключает.
uint32_t gDispIdleMin = 2;       // минут без касания до гашения, 0 = не гасить
uint32_t gDispOffAt   = 0;       // когда погасили таймером (millis), 0 — не гасили
uint32_t gDispOffN    = 0;       // сколько раз гасили по таймеру
bool     gDispUserOff = false;   // погашено ВРУЧНУЮ
bool     gDispAutoOff = false;   // погашено ТАЙМЕРОМ бездействия
unsigned long gLastTouchMs  = 0; // когда было последнее касание (для таймера)
unsigned long gLastDarkDraw = 0; // последняя перерисовка при погашенном экране

bool dispWakeActive() { return gDispWakeUntil && (long)(millis() - gDispWakeUntil) < 0; }
bool dispIsDark()     { return !gDispOn && !dispWakeActive(); }

void dispApply() {
  uint8_t v = gDispOn ? gDispBright : (dispWakeActive() ? gDispBright : 0);
  ledcWrite(BL_CH, v);
}
void dispPwmBegin() {
  ledcSetup(BL_CH, 5000, 8);
  ledcAttachPin(BL_PIN, BL_CH);
  dispApply();
}
void dispLoad() {
  Preferences p;
  if (!p.begin("disp", true)) return;
  uint8_t v = p.getUChar("bright", 255);
  gDispTouch = p.getBool("touch", true);
  gDispIdleMin = p.getUChar("idle", 2);
  if (gDispIdleMin > 60) gDispIdleMin = 2;   // мусор в NVS — безопасный дефолт
  p.end();
  if (v == 0) { gDispOn = false; gDispBright = 255; gDispUserOff = true; }
  else        { gDispOn = true;  gDispBright = v;   }
}
void dispSave() {
  Preferences p;
  if (!p.begin("disp", false)) return;
  p.putUChar("bright", gDispOn ? gDispBright : 0);
  p.putBool("touch", gDispTouch);
  p.putUChar("idle", (uint8_t)gDispIdleMin);
  p.end();
}
void dispSet(bool on, uint8_t bright) {
  if (bright > 0) gDispBright = bright;
  gDispOn = on;
  gDispUserOff = !on;            // ручное выключение: таймер бездействия его не трогает
  gDispAutoOff = false;
  gDispOffAt = 0;
  gDispWakeUntil = 0;
  gLastTouchMs = millis();
  dispApply();
  dispSave();
}
void dispWake() {
  if (gDispOn || !gDispTouch || gDispBright == 0) return;
  gDispWakeUntil = millis() + BL_WAKE_MS;
  ledcWrite(BL_CH, gDispBright);
}
void dispTick() {
  if (gDispWakeUntil && !dispWakeActive()) {
    gDispWakeUntil = 0;
    if (!gDispOn) ledcWrite(BL_CH, 0);
  }
}
// --- энергосбережение: автогашение по бездействию ---------------------------
// Погасить ТАЙМЕРОМ: NVS не трогаем (bright остаётся рабочим), ручное
// выключение не перебиваем — при gDispUserOff таймер вообще не работает.
void dispAutoDim() {
  if (!gDispOn) return;
  gDispOn = false;
  gDispAutoOff = true;
  gDispWakeUntil = 0;
  gDispOffAt = millis();
  gDispOffN++;
  dispApply();
  Serial.println("DISP: таймер бездействия — подсветка погашена");
}
// Касание погашенного ТАЙМЕРОМ экрана: сразу свет + немедленная перерисовка
// (gLastDraw=0 — иначе экран остался бы со старыми данными до 2-5 минут).
void dispTouchOn() {
  if (!gDispAutoOff) return;
  gDispAutoOff = false;
  gDispOffAt = 0;
  gDispOn = true;
  gDispWakeUntil = 0;
  gLastTouchMs = millis();
  dispApply();
  gLastDraw = 0;
  gLastDarkDraw = 0;
}
void dispIdleTick() {
  if (tpDown()) {
    gLastTouchMs = millis();
    if (gDispAutoOff && gDispTouch) dispTouchOn();
  }
  if (gDispIdleMin == 0 || !gDispOn || gDispAutoOff || gDispUserOff) return;
  if (millis() - gLastTouchMs >= gDispIdleMin * 60000UL) dispAutoDim();
}
// Сколько мс осталось до автогашения; 0 — таймер неактивен.
uint32_t dispIdleLeftMs() {
  if (gDispIdleMin == 0 || !gDispOn || gDispUserOff) return 0;
  uint32_t total = gDispIdleMin * 60000UL;
  uint32_t used = millis() - gLastTouchMs;
  return (used >= total) ? 0UL : (total - used);
}
String dispState() {
  char b[96];
  snprintf(b, sizeof(b), "DISP: on=%d bright=%u idle=%lu%s",
           gDispOn ? 1 : 0, (unsigned)(gDispOn ? gDispBright : 0),
           (unsigned long)gDispIdleMin,
           gDispTouch ? "" : " notouch");
  return String(b);
}
void dispCommand(const String& up) {
  String a = up.substring(4); a.trim();
  if (a.length() == 0) { Serial.println(dispState()); return; }
  if (a == "ON")           { dispSet(true, 255); Serial.println("DISP: OK on"); }
  else if (a == "OFF")     { dispSet(false, 0);  Serial.println("DISP: OK off"); }
  else if (a == "TOGGLE")  {     // приложение шлёт это по кнопке «Экран платы»
    if (gDispOn) dispSet(false, 0);
    else         dispSet(true, 0);   // вернуть последнюю яркость, не обязательно 255
    Serial.println(gDispOn ? "DISP: OK on" : "DISP: OK off");
  }
  else if (a == "TOUCH")   { gDispTouch = true;  dispSave(); Serial.println("DISP: OK touch=1"); }
  else if (a == "NOTOUCH") { gDispTouch = false; dispSave(); Serial.println("DISP: OK touch=0"); }
  else if (a.startsWith("IDLE")) {          // DISP IDLE n — автогашение, 0..60 мин
    String s = a.substring(4); s.trim();
    if (s.length() == 0) { Serial.println(dispState()); return; }
    long v = 0;
    if (!parseIntStrict(s, v) || v < 0 || v > 60) { Serial.println("DISP: ERR idle 0..60"); return; }
    gDispIdleMin = (uint32_t)v;
    gLastTouchMs = millis();
    dispSave();
    char o[40];
    snprintf(o, sizeof(o), "DISP: OK idle=%lu", (unsigned long)gDispIdleMin);
    Serial.println(o);
  }
  else {
    int v = a.toInt();
    if (v <= 0) { dispSet(false, 0); Serial.println("DISP: OK off"); return; }
    if (v > 100) v = 100;
    uint8_t b = (uint8_t)((v * 255 + 50) / 100);
    dispSet(true, b);
    char o[40];
    snprintf(o, sizeof(o), "DISP: OK bright=%u", (unsigned)b);
    Serial.println(o);
  }
}

// ============================================================================
//  ДИНАМИК — GPIO 26 (DAC2), ШИМ-тон (канал 1). Команда BEEP.
// ============================================================================
// Подключение: GPIO 26 -> «+» динамика, GND -> «−». Пьезоизлучатель или
// маленький динамик 8 Ом; громкость маленькая (не более 50 % ШИМ).
// По умолчанию звук ВЫКЛЮЧЕН: включается командой BEEP ON.
#define BEEP_PIN   26
#define BEEP_CH    1

bool     gBeepOn    = false;
uint8_t  gBeepVol   = 50;        // 0..100
bool     gBeepTouch = true;      // пик при касании
bool     gBeepHour  = false;     // пик в начале часа (по умолчанию нет)

static uint8_t  bpLeft = 0;      // сколько пиков ещё осталось
static uint16_t bpFreq = 2200, bpOn = 40, bpOff = 60;
static bool     bpSounding = false;
static unsigned long bpAt = 0;

void beepTone(bool on) {
  if (on) {
    ledcWriteTone(BEEP_CH, bpFreq);
    uint16_t duty = (uint16_t)((uint32_t)gBeepVol * 127 / 100);  // 127 = 50 % ШИМ
    ledcWrite(BEEP_CH, duty);
  } else {
    ledcWriteTone(BEEP_CH, 0);
    ledcWrite(BEEP_CH, 0);
  }
}
void beepStop() { bpLeft = 0; bpSounding = false; beepTone(false); }
void beepInit() {
  ledcSetup(BEEP_CH, 2000, 8);
  ledcAttachPin(BEEP_PIN, BEEP_CH);
  beepTone(false);
  Preferences p;
  if (!p.begin("beep", true)) return;
  gBeepOn    = p.getBool("on", false);
  gBeepVol   = p.getUChar("vol", 50);
  gBeepTouch = p.getBool("touch", true);
  gBeepHour  = p.getBool("hour", false);
  p.end();
  if (gBeepVol > 100) gBeepVol = 100;
}
void beepSave() {
  Preferences p;
  if (!p.begin("beep", false)) return;
  p.putBool("on", gBeepOn);
  p.putUChar("vol", gBeepVol);
  p.putBool("touch", gBeepTouch);
  p.putBool("hour", gBeepHour);
  p.end();
}
void beepPlay(uint8_t n, uint16_t freq, uint16_t onMs, uint16_t offMs) {
  if (gBeepVol == 0 || n == 0) return;
  bpLeft = n; bpFreq = freq; bpOn = onMs; bpOff = offMs;
  bpSounding = true; bpAt = millis();
  bpLeft--;                       // первый пик пошёл сразу
  beepTone(true);
}
void beepStart(uint8_t n, uint16_t freq, uint16_t onMs, uint16_t offMs) {
  if (!gBeepOn) return;
  beepPlay(n, freq, onMs, offMs);
}
void beepStartForce(uint8_t n, uint16_t freq, uint16_t onMs, uint16_t offMs) {
  beepPlay(n, freq, onMs, offMs);   // BEEP TEST — даже при выключенном звуке
}
void beepTick() {
  if (!bpSounding && bpLeft == 0) return;
  unsigned long now = millis();
  if (bpSounding) {
    if (now - bpAt >= bpOn) { beepTone(false); bpSounding = false; bpAt = now; }
  } else if (bpLeft > 0 && now - bpAt >= bpOff) {
    bpLeft--; bpSounding = true; bpAt = now; beepTone(true);
  }
}
String beepState() {
  char b[64];
  snprintf(b, sizeof(b), "BEEP: on=%d vol=%u touch=%d hour=%d",
           gBeepOn ? 1 : 0, (unsigned)gBeepVol,
           gBeepTouch ? 1 : 0, gBeepHour ? 1 : 0);
  return String(b);
}
void beepCommand(const String& up) {
  String a = up.substring(4); a.trim();
  if (a.length() == 0) { Serial.println(beepState()); return; }
  if (a == "ON")   { gBeepOn = true;  beepSave(); Serial.println("BEEP: OK on"); }
  else if (a == "OFF") { gBeepOn = false; beepStop(); beepSave(); Serial.println("BEEP: OK off"); }
  else if (a == "TEST") { Serial.println("BEEP: OK test"); beepStartForce(1, 2200, 80, 0); }
  else if (a.startsWith("VOL")) {
    int v = a.substring(3).toInt();
    if (v < 0) v = 0;
    if (v > 100) v = 100;
    gBeepVol = (uint8_t)v;
    beepSave();
    char o[32];
    snprintf(o, sizeof(o), "BEEP: OK vol=%d", v);
    Serial.println(o);
    beepStartForce(1, 2200, 60, 0);
  }
  else if (a == "TOUCH") { gBeepTouch = !gBeepTouch; beepSave();
    Serial.printf("BEEP: OK touch=%d\n", gBeepTouch ? 1 : 0); }
  else if (a == "HOUR")  { gBeepHour = !gBeepHour; beepSave();
    Serial.printf("BEEP: OK hour=%d\n", gBeepHour ? 1 : 0); }
  else Serial.println("BEEP: ON|OFF|TEST|VOL n|TOUCH|HOUR");
}

// Сигналы тревоги: напряжение вне 207..253 В дольше 10 с — три пика;
// перегрев реле > 55 °C — длинный пик раз в 5 минут; пропала карта — два пика;
// начало часа — один пик (если BEEP HOUR).
void beepEvents() {
  static unsigned long last = 0;
  static unsigned long badSince = 0, alarmAt = 0, tempAt = 0;
  static bool sdWas = true;
  static int lastHour = -1;
  if (millis() - last < 500UL) return;
  last = millis();

  if (!gSdOk && sdWas) beepStart(2, 1200, 100, 90);   // карта только что пропала
  sdWas = gSdOk;
  if (!gBeepOn) return;

  bool fresh = gPhaseMs && (millis() - gPhaseMs < 30000UL);
  bool bad = fresh && (gVoltage < V_NORM_LO || gVoltage > V_NORM_HI);
  if (bad) { if (!badSince) badSince = millis(); }
  else badSince = 0;
  if (badSince && millis() - badSince > 10000UL && millis() - alarmAt > 300000UL) {
    alarmAt = millis();
    beepStart(3, 1800, 120, 100);
  }
  if (gTempValid && gTemp > 55.0f && millis() - tempAt > 300000UL) {
    tempAt = millis();
    beepStart(1, 1500, 600, 0);
  }
  struct tm ti;
  if (getLocalTime(&ti, 10)) {
    if (lastHour != ti.tm_hour) {
      if (gBeepHour && lastHour >= 0 && ti.tm_min < 2) beepStart(1, 2400, 50, 0);
      lastHour = ti.tm_hour;
    }
  }
}

// ============================================================================
//  RGB-СВЕТОДИОД — 4 (R), 16 (G), 17 (B), горят при НИЗКОМ уровне.
// ============================================================================
// v4.4: светодиод показывает состояние платы, яркость — ШИМ (каналы 2,3,4),
// чтобы ночью гореть тускло. Приоритет: красный > оранжевый > жёлтый > зелёный.
#define LED_R_PIN 4
#define LED_G_PIN 16
#define LED_B_PIN 17
#define LED_R_CH  2
#define LED_G_CH  3
#define LED_B_CH  4

enum { LEDS_BOOT = 0, LEDS_RED, LEDS_ORANGE, LEDS_YELLOW, LEDS_GREEN, LEDS_DIM, LEDS_OFF };

int           gLedState = LEDS_GREEN;
uint8_t       gLedR = 255, gLedG = 255, gLedB = 255;   // что уже записано в ШИМ
unsigned long gLedAt = 0;
bool          gLedBlinkOn = true;
unsigned long gLedBootUntil = 0;
int           gLedOverride = -1;
unsigned long gLedOverrideUntil = 0;

void ledSetRaw(uint8_t r, uint8_t g, uint8_t b) {
  if (r == gLedR && g == gLedG && b == gLedB) return;
  gLedR = r; gLedG = g; gLedB = b;
  ledcWrite(LED_R_CH, 255 - r);      // светодиод горит при НИЗКОМ уровне
  ledcWrite(LED_G_CH, 255 - g);
  ledcWrite(LED_B_CH, 255 - b);
}
void ledSet(bool r, bool g, bool b) {
  ledSetRaw(r ? 255 : 0, g ? 255 : 0, b ? 255 : 0);
}
void ledInit() {
  ledcSetup(LED_R_CH, 5000, 8); ledcAttachPin(LED_R_PIN, LED_R_CH);
  ledcSetup(LED_G_CH, 5000, 8); ledcAttachPin(LED_G_PIN, LED_G_CH);
  ledcSetup(LED_B_CH, 5000, 8); ledcAttachPin(LED_B_PIN, LED_B_CH);
  gLedR = gLedG = gLedB = 255;       // «всё выключено» (ШИМ = 0)
  gLedState = LEDS_BOOT;
  ledSetRaw(0, 0, 255);              // синий на время загрузки
}
const char* ledName(int st) {
  switch (st) {
    case LEDS_BOOT:   return "blue";
    case LEDS_RED:    return "red";
    case LEDS_ORANGE: return "orange";
    case LEDS_YELLOW: return "yellow";
    case LEDS_GREEN:  return "green";
    case LEDS_DIM:    return "green-dim";
    default:          return "off";
  }
}
int ledWant() {
  if (gLedOverride >= 0 && (long)(millis() - gLedOverrideUntil) < 0) return gLedOverride;
  if (gLedBootUntil && (long)(millis() - gLedBootUntil) < 0) return LEDS_BOOT;
  bool wifi = (WiFi.status() == WL_CONNECTED);
  if (!wifi || (gRelayFailRun >= 2 && gCloudFailRun >= 3)) return LEDS_RED;  // красный: нет Wi-Fi ИЛИ (реле молчит И облако молчит)
  if (!gSdOk || gSdWriteFail >= 2) return LEDS_ORANGE;       // карта/запись
  if (!gNetOk || gCloudFailRun >= 3) return LEDS_YELLOW;     // интернета нет
  if (dispIsDark()) return LEDS_DIM;                         // экран погашен
  return LEDS_GREEN;
}
void ledTick() {
  unsigned long now = millis();
  int st = ledWant();
  if (st != gLedState) { gLedState = st; gLedAt = now; gLedBlinkOn = true; }
  if (st == LEDS_ORANGE) {
    if (now - gLedAt >= 500UL) { gLedAt = now; gLedBlinkOn = !gLedBlinkOn; }
  } else gLedBlinkOn = true;
  switch (st) {
    case LEDS_BOOT:   ledSetRaw(0, 0, 255); break;
    case LEDS_RED:    ledSetRaw(255, 0, 0); break;
    case LEDS_ORANGE: ledSetRaw(gLedBlinkOn ? 255 : 0, gLedBlinkOn ? 90 : 0, 0); break;
    case LEDS_YELLOW: ledSetRaw(255, 190, 0); break;
    case LEDS_GREEN:  ledSetRaw(0, 255, 0); break;
    case LEDS_DIM:    ledSetRaw(0, 51, 0); break;     // 20 % — ночью не слепит
    default:          ledSetRaw(0, 0, 0); break;
  }
}
void ledUpdate() { ledTick(); }

// Проверка глазами: LED TEST red|orange|yellow|green|blue|dim на 15 секунд.
void ledCommand(const String& up) {
  String a = up.substring(3); a.trim();
  if (a.length() == 0) {
    Serial.printf("LED: %s (net=%d relayFail=%u sd=%d sdw=%u dark=%d)\n",
                  ledName(gLedState), gNetOk ? 1 : 0, (unsigned)gRelayFailRun,
                  gSdOk ? 1 : 0, (unsigned)gSdWriteFail, dispIsDark() ? 1 : 0);
    return;
  }
  if (a.startsWith("TEST")) {
    String c = a.substring(4); c.trim(); c.toLowerCase();
    int st = -1;
    if (c == "red")         st = LEDS_RED;
    else if (c == "orange") st = LEDS_ORANGE;
    else if (c == "yellow") st = LEDS_YELLOW;
    else if (c == "green")  st = LEDS_GREEN;
    else if (c == "dim")    st = LEDS_DIM;
    else if (c == "blue")   st = LEDS_BOOT;
    else if (c == "off")    st = LEDS_OFF;
    if (st < 0) { Serial.println("LED: TEST red|orange|yellow|green|dim|blue|off"); return; }
    gLedOverride = st;
    gLedOverrideUntil = millis() + 15000UL;
    Serial.printf("LED: OK test %s 15 s\n", ledName(st));
  } else if (a == "RESET") {
    gLedOverride = -1; gLedOverrideUntil = 0;
    Serial.println("LED: OK auto");
  } else {
    Serial.println("LED: TEST <цвет> | RESET");
  }
}

// ---------------------------------------------------------------- иконки погоды по коду OWM
// Рисуем примитивами TFT: картинок во flash нет, код дешевле и надёжнее.
// Тема СВЕТЛАЯ (карточка белая), поэтому чистые TFT_YELLOW и TFT_WHITE на ней
// не видны: солнце — жёлтое с янтарным контуром, снег — голубой.
static void wxPuffs(int cx, int cy, int r, int g, uint16_t col) {
  uiCircle(cx - r * 40 / 100, cy + r * 5 / 100,  r * 30 / 100 + g, col);
  uiCircle(cx + r * 38 / 100, cy - r * 2 / 100,  r * 35 / 100 + g, col);
  uiCircle(cx - r * 2 / 100,  cy - r * 30 / 100, r * 45 / 100 + g, col);
  uiFill(cx - r * 70 / 100 - g, cy - g,
         r * 143 / 100 + 2 * g, r * 35 / 100 + 2 * g, col);
}

// Облако: тёмный контур + светлая заливка (g=1 — контур, g=0 — заливка).
static void wxCloud(int cx, int cy, int r, uint16_t fill, uint16_t edge) {
  wxPuffs(cx, cy, r, 1, edge);
  wxPuffs(cx, cy, r, 0, fill);
}

static void wxSun(int cx, int cy, int r, uint16_t fill, uint16_t ray) {
  for (int i = 0; i < 8; i++) {
    float a = i * PI / 4.0f;
    float sa = sin(a), ca = cos(a);
    int x1 = cx + (int)(ca * r * 0.70f);
    int y1 = cy + (int)(sa * r * 0.70f);
    int x2 = cx + (int)(ca * r);
    int y2 = cy + (int)(sa * r);
    uiLine(x1, y1, x2, y2, ray);
    // второй штрих перпендикулярно — лучи толщиной 2 px, иначе теряются
    uiLine(x1 - (int)(sa * 1.2f), y1 + (int)(ca * 1.2f),
           x2 - (int)(sa * 1.2f), y2 + (int)(ca * 1.2f), ray);
  }
  uiCircle(cx, cy, r * 46 / 100 + 1, ray);    // контур
  uiCircle(cx, cy, r * 46 / 100, fill);
}

static void wxFlake(int cx, int cy, int r, uint16_t col) {
  uiLine(cx - r, cy, cx + r, cy, col);
  uiLine(cx, cy - r, cx, cy + r, col);
  int d = r * 7 / 10;
  uiLine(cx - d, cy - d, cx + d, cy + d, col);
  uiLine(cx - d, cy + d, cx + d, cy - d, col);
}

// Код OWM -> иконка. x,y — левый верхний угол, size — сторона квадрата (28..34).
void drawWxIcon(int x, int y, int size, int code) {
  if (size < 16) size = 16;
  if (code <= 0) return;          // нет данных: «НЕТ ДАННЫХ» рисует вызывающий

  const uint16_t cSunBody = tft.color565(253, 224, 71);   // солнце (TFT_YELLOW)
  const uint16_t cSunRay  = tft.color565(217, 119, 6);    // лучи (TFT_ORANGE)
  const uint16_t cCloudF  = tft.color565(203, 213, 225);  // облако (TFT_LIGHTGREY)
  const uint16_t cCloudE  = tft.color565(100, 116, 139);  // контур (TFT_DARKGREY)
  const uint16_t cDrop    = tft.color565(2, 132, 199);    // дождь (TFT_BLUE)
  const uint16_t cFlake   = tft.color565(96, 165, 250);   // снег (TFT_WHITE не виден)
  const uint16_t cFog     = tft.color565(100, 116, 139);  // туман (TFT_LIGHTGREY не виден)

  const int s  = size;
  const int cx = x + s / 2;
  const int cy = y + s / 2;

  const bool thunder = (code >= 200 && code < 300);
  const bool drizzle = (code >= 300 && code < 400);
  const bool rain    = (code >= 500 && code < 600);
  const bool snow    = (code >= 600 && code < 700);
  const bool fog     = (code >= 700 && code < 800);
  const bool clear   = (code == 800);
  const bool few     = (code > 800 && code <= 802);

  if (clear) { wxSun(cx, cy, s * 38 / 100, cSunBody, cSunRay); return; }

  if (few) {     // 801/802: солнце + облако
    wxSun(x + s * 30 / 100, y + s * 30 / 100, s * 26 / 100, cSunBody, cSunRay);
    wxCloud(x + s * 58 / 100, y + s * 60 / 100, s * 38 / 100, cCloudF, cCloudE);
    return;
  }

  if (fog) {     // 7xx: полосы тумана, без облака — чтобы не путать с пасмурно
    const int w0 = s * 62 / 100;
    for (int i = 0; i < 4; i++) {
      int yy = y + s * (28 + i * 18) / 100;
      int ww = w0 - ((i & 1) ? s * 12 / 100 : 0);
      int xx = x + (s - ww) / 2 + ((i & 1) ? -s * 3 / 100 : 0);
      uiHLine(xx, yy, ww, cFog);
      uiHLine(xx, yy + 1, ww, cFog);          // 2 px
    }
    return;
  }

  // 803/804 и все осадки: облако сверху, под ним — знак осадков.
  wxCloud(cx, y + s * 40 / 100, s * 46 / 100, cCloudF, cCloudE);

  if (thunder) {
    int zx = cx;
    int p1x = zx + s * 9 / 100,  p1y = y + s * 60 / 100;
    int p2x = zx - s * 9 / 100,  p2y = y + s * 72 / 100;
    int p3x = zx + s * 6 / 100,  p3y = y + s * 72 / 100;
    int p4x = zx - s * 12 / 100, p4y = y + s * 94 / 100;
    uiLine(p1x, p1y, p2x, p2y, cSunRay);
    uiLine(p2x, p2y, p3x, p3y, cSunRay);
    uiLine(p3x, p3y, p4x, p4y, cSunRay);
    uiLine(p1x + 1, p1y, p2x + 1, p2y, cSunRay);
    uiLine(p2x + 1, p2y, p3x + 1, p3y, cSunRay);
    uiLine(p3x + 1, p3y, p4x + 1, p4y, cSunRay);
  } else if (rain || drizzle) {
    int n = rain ? 3 : 2;
    for (int i = 0; i < n; i++) {
      int xx = cx + (i * 2 - (n - 1)) * s * 10 / 100;
      int xa = xx + s * 6 / 100, xb = xx - s * 6 / 100;
      uiLine(xa, y + s * 62 / 100, xb, y + s * 88 / 100, cDrop);
      uiLine(xa + 1, y + s * 62 / 100, xb + 1, y + s * 88 / 100, cDrop);
    }
  } else if (snow) {
    int fx[3] = { cx - s * 22 / 100, cx + s * 2 / 100,  cx + s * 24 / 100 };
    int fy[3] = { y + s * 70 / 100,  y + s * 85 / 100,  y + s * 68 / 100 };
    for (int i = 0; i < 3; i++) wxFlake(fx[i], fy[i], s * 10 / 100, cFlake);
  }
}

// ---------------------------------------------------------------- иконка погоды (описание)
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
// Маска точек — от выбранного дня: сегодня это h24Mask (живые замеры),
// прошлый день — dayMask (прочитано с карты).
static inline const uint8_t* maskCur() { return (gDayOffset > 0) ? dayMask : h24Mask; }

float hourAverage(const float* vals, uint8_t bit, int h, int* samples) {
  if (samples) *samples = 0;
  if (h < 0 || h > 23) return 0;
  const uint8_t* m = maskCur();
  float sum = 0;
  int n = 0;
  for (int s = 0; s < 12; s++) {
    int i = h * 12 + s;
    if (i < 0 || i >= H24_N) continue;
    if (!(m[i] & bit)) continue;
    float v = vals[i];
    if (v != v) continue;                     // NaN — пропускаем
    sum += v;
    n++;
  }
  if (samples) *samples = n;
  return n ? sum / n : 0;
}

// Стрелка ‹ / › в строке заголовка: две линии, 9 px высотой.
static void drawChevron(int cx, int cy, bool left, uint16_t col) {
  int tip  = left ? cx - 2 : cx + 2;      // вершина
  int back = left ? cx + 3 : cx - 3;      // хвост
  uiLine(back, cy - 4, tip, cy, col);
  uiLine(tip, cy, back, cy + 4, col);
}

// Заголовок над столбиками: слева стрелки ‹ › и дата выбранного дня,
// справа — час и его среднее. Крупное мгновенное значение остаётся сверху окна.
// Зоны нажатий: 0..44 — ‹, 44..116 — дата (возврат к сегодня), 116..156 — ›.
void drawHourTitle(int y, const float* vals, uint8_t bit, const char* unit, int dec) {
  uiFill(0, y - 1, 240, 10, cBg);            // полоса под стрелки и дату
  drawChevron(16, y + 4, true,  (gDayOffset < gDayMaxOffset) ? cAccent : cLine);
  drawChevron(126, y + 4, false, (gDayOffset > 0) ? cAccent : cLine);
  drawTxtC(80, y, (gDayOffset == 0) ? "СЕГОДНЯ" : gDayLabel,
           (gDayOffset == 0) ? cGood : cText, cBg, 1);

  char b[24];
  struct tm ti;
  int nowH = getLocalTime(&ti, 5) ? ti.tm_hour : -1;
  // Нажали на столбик — показываем ТОТ час, иначе текущий.
  // У прошлого дня «текущего часа» нет: показываем «--», пока не нажали столбик.
  if (gChartHour >= 0)      nowH = gChartHour;
  else if (gDayOffset > 0)  nowH = -1;
  if (nowH < 0) {
    snprintf(b, sizeof(b), "--");
  } else {
    int n = 0;
    float a = hourAverage(vals, bit, nowH, &n);
    if (n == 0)     snprintf(b, sizeof(b), "%02d: --", nowH);
    else if (dec)   snprintf(b, sizeof(b), "%02d: %.1f %s", nowH, a, unit);
    else            snprintf(b, sizeof(b), "%02d: %.0f %s", nowH, a, unit);
  }
  drawTxtR(234, y, b, cMuted, cBg, 1);
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
    // если пользователь нажал на столбик — выделяем выбранный час
    // если пользователь нажал на столбик — выделяем выбранный час,
    // иначе у прошлого дня текущего часа нет и выделять нечего
    if (gChartHour >= 0)     nowH = gChartHour;
    else if (gDayOffset > 0) nowH = -1;
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
// v4.3: с экрана убрана совсем — сторож камер отключён, чтобы не тратить
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

  // ---- погода: две колонки «СЕГОДНЯ | ЗАВТРА» (v4.6)
  // Ветер и влажность с главного убраны — они есть на экране ПОГОДА.
  // В колонке 120 px: иконка 32x32, температура, короткое описание, дождь.
  const Block& wb = BLOCKS[4];
  drawBlockFrame(wb.x, wb.y, wb.w, wb.h);
  drawTxtC(wb.x + 60,  wb.y + 6, "СЕГОДНЯ", cMuted, cSurf, 1);
  drawTxtC(wb.x + 180, wb.y + 6, "ЗАВТРА",  cMuted, cSurf, 1);
  uiVLine(wb.x + 120, wb.y + 5, wb.h - 10, cLine);

  char dsc[24];
  // ---- сегодня
  if (!wOk) {
    drawTxt(wb.x + 10, wb.y + 30, "НЕТ", cMuted, cSurf, 1);
    drawTxt(wb.x + 10, wb.y + 42, "ДАННЫХ", cMuted, cSurf, 1);
  } else {
    drawWxIcon(wb.x + 5, wb.y + 20, 32, wCode);
    snprintf(b, sizeof(b), "%.1f", wTemp);
    drawTxt(wb.x + 42, wb.y + 19, b, cSky, cSurf, 2);
    drawTxt(wb.x + 42 + txtW(b, 2) + 2, wb.y + 26, "C", cMuted, cSurf, 1);
    wxShortRus(wCode, dsc, sizeof(dsc));
    drawTxt(wb.x + 42, wb.y + 38, dsc, cText, cSurf, 1);
    if (wFcOk) snprintf(b, sizeof(b), "ДОЖДЬ %.0f%%", wPopNow);
    else       strcpy(b, "ДОЖДЬ --");
    drawTxt(wb.x + 42, wb.y + 49, b,
            (wFcOk && wPopNow >= 50.0f) ? cAccent : cMuted, cSurf, 1);
  }

  // ---- завтра
  if (!wFcOk) {
    drawTxt(wb.x + 130, wb.y + 30, "НЕТ", cMuted, cSurf, 1);
    drawTxt(wb.x + 130, wb.y + 42, "ДАННЫХ", cMuted, cSurf, 1);
  } else {
    drawWxIcon(wb.x + 125, wb.y + 20, 32, wCodeTom);
    snprintf(b, sizeof(b), "%.0f-%.0f", wTminTom, wTmaxTom);
    drawTxt(wb.x + 162, wb.y + 19, b, cSky, cSurf, 2);
    drawTxt(wb.x + 162 + txtW(b, 2) + 2, wb.y + 26, "C", cMuted, cSurf, 1);
    drawTxt(wb.x + 162, wb.y + 38, wDescTom.c_str(), cText, cSurf, 1);
    snprintf(b, sizeof(b), "ДОЖДЬ %.0f%%", wPopTom);
    drawTxt(wb.x + 162, wb.y + 49, b,
            (wPopTom >= 50.0f) ? cAccent : cMuted, cSurf, 1);
  }

  drawStatusBar();
  // Плашка тревоги убрана (v4.3): сторож камер отключён.
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

  drawStamp(12, 104, gPhaseMs, cBg);        // когда напряжение реально пришло
  drawTxt(12, 116, "НОРМА 207-253 В", cMuted, cBg, 1);
  drawTxt(12, 128, "ГОСТ 32144-2013", cMuted, cBg, 1);

  const int colX[3] = { 12, 88, 164 };
  const char* lab[3] = { "МИН", "СРЕД", "МАКС" };
  // Показатели — за ВЫБРАННЫЙ день: сегодня из живых day*, прошлый из dd*.
  bool past = (gDayOffset > 0);
  float val[3] = { past ? ((ddMinV < 900) ? ddMinV : 0) : ((dayMinV < 900) ? dayMinV : 0),
                   past ? (ddSecV ? (float)(ddSumV / ddSecV) : 0)
                        : (dayVSec > 0 ? (float)(daySumV / dayVSec) : 0),
                   past ? ddMaxV : dayMaxV };
  int mins[3] = { past ? ddMinVMin : dayMinVMin, -1, past ? ddMaxVMin : dayMaxVMin };
  // У 23–26.10 в файле только строки из облака: напряжения нет вообще.
  // Честнее показать «--», чем нули.
  bool noV = past && ddSecV == 0;
  for (int i = 0; i < 3; i++) {
    drawTxt(colX[i], 140, lab[i], cMuted, cBg, 1);
    if (noV) strcpy(b, "--");
    else     snprintf(b, sizeof(b), "%.0f", val[i]);
    drawTxt(colX[i], 150, b, cText, cBg, 2);
    hhmm(mins[i], b, sizeof(b));
    if (i == 1) strcpy(b, "24 Ч");
    drawTxt(colX[i], 168, b, cMuted, cBg, 1);
  }
  uiVLine(80, 138, 44, cLine);
  uiVLine(156, 138, 44, cLine);

  long  outSec = past ? (long)ddOutV : dayOutSec;
  long  secAll = past ? ddSecV : dayVSec;
  float pct = (secAll > 0) ? 100.0f * outSec / secAll : 0;
  durStr(outSec, b, sizeof(b));
  char line[64];
  if (noV) strcpy(line, "ВНЕ НОРМЫ --   ЗАМЕРОВ НЕТ");
  else     snprintf(line, sizeof(line), "ВНЕ НОРМЫ %.0f%%  %s", pct, b);
  drawTxt(12, 180, line, (!noV && outSec > 0) ? cWarn : (noV ? cMuted : cGood), cBg, 1);

  // График — самая дорогая часть окна. При точечном обновлении его не
  // перерисовываем: столбики за прошедшие часы не меняются.
  // Сегодня — живые h24V, прошлый день — прочитанный с карты dayV.
  const float* arrV = past ? dayV : h24V;
  drawHourTitle(192, arrV, 1, "В", 0);
  if (!gSkipChart)
    drawHourBars(10, 202, 220, 86, arrV, 1, cAccent, tft.color565(191, 219, 254), false);
  // Кириллица в UTF-8 — 2 байта на букву: «СРЕДНЕЕ НАПРЯЖЕНИЕ ЗА КАЖДЫЙ ЧАС»
  // это 67 байт, поэтому буфер с запасом, а не 48 (было переполнение стека).
  char cap[80];
  if (past) snprintf(cap, sizeof(cap), "СРЕДНЕЕ НАПРЯЖЕНИЕ ЗА %s", gDayLabel);
  else      snprintf(cap, sizeof(cap), "СРЕДНЕЕ НАПРЯЖЕНИЕ ЗА КАЖДЫЙ ЧАС");
  drawTxt(10, 301, cap, cMuted, cBg, 1);
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
  drawStamp(12, 104, gTempMs, cBg);         // когда температура реально пришла
  drawTxt(12, 116, "ТЕМПЕРАТУРА КОРПУСА РЕЛЕ", cMuted, cBg, 1);

  // Показатели — за ВЫБРАННЫЙ день (сегодня из day*, прошлый из dd*).
  bool past = (gDayOffset > 0);
  char lab[40];
  if (past) snprintf(lab, sizeof(lab), "МАКС ЗА %s", gDayLabel);
  else      snprintf(lab, sizeof(lab), "МАКС ЗА СУТКИ");
  drawTxt(12, 126, lab, cMuted, cBg, 1);
  float tMax = past ? ddMaxT : dayMaxT;
  if (past && ddSecT == 0) strcpy(b, "--");
  else                     snprintf(b, sizeof(b), "%.1f", (tMax > -100) ? tMax : gTemp);
  drawTxt(12, 136, b, cTEMP_ACC, cBg, 3);
  hhmm(past ? ddMaxTMin : dayMaxTMin, b, sizeof(b));
  drawTxt(12, 160, b, cMuted, cBg, 1);

  drawTxt(100, 126, "СРЕДНЯЯ", cMuted, cBg, 1);
  if (past && ddSecT == 0) strcpy(b, "--");
  else snprintf(b, sizeof(b), "%.1f", past ? (ddSecT ? (float)(ddSumT / ddSecT) : gTemp)
                                           : (dayTSec ? (float)(daySumT / dayTSec) : gTemp));
  drawTxt(100, 136, b, cText, cBg, 2);

  drawTxt(100, 162, "МИН", cMuted, cBg, 1);
  float tMin = past ? ddMinT : dayMinT;
  if (past && ddSecT == 0) strcpy(b, "--");
  else                     snprintf(b, sizeof(b), "%.1f", (tMin < 900) ? tMin : gTemp);
  drawTxt(148, 162, b, cMuted, cBg, 1);

  const float* arrT = past ? dayT : h24T;
  drawHourTitle(176, arrT, 2, "C", 1);
  if (!gSkipChart)
    drawHourBars(10, 186, 220, 86, arrT, 2, cTEMP_ACC, tft.color565(221, 214, 254), false);
  char cap[80];
  if (past) snprintf(cap, sizeof(cap), "СРЕДНЯЯ ТЕМПЕРАТУРА ЗА %s", gDayLabel);
  else      snprintf(cap, sizeof(cap), "СРЕДНЯЯ ТЕМПЕРАТУРА ЗА КАЖДЫЙ ЧАС");
  drawTxt(10, 284, cap, cMuted, cBg, 1);
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

  drawStamp(12, 104, gPowerMs, cBg);        // когда мощность реально пришла
  // Именно мгновенная нагрузка через реле: по ней и видно включение перфоратора.
  drawTxt(12, 116, "МГНОВЕННАЯ НАГРУЗКА ЧЕРЕЗ РЕЛЕ", cWarn, cBg, 1);

  // СРЕДНЯЯ за сутки: считаем по часовым средним из графика, а не по живым
  // замерам — иначе после сброса или простоя цифра занижена.
  // Средняя: НЕ учитываем текущий неполный час и взвешиваем по числу отсчётов.
  // Для прошлого дня берём сводку, посчитанную при чтении карты.
  bool past = (gDayOffset > 0);
  const float* arrP = past ? dayP : h24P;
  struct tm tiAvg;
  int nowHAvg = getLocalTime(&tiAvg, 5) ? tiAvg.tm_hour : -1;
  float avgP = 0;
  if (past) {
    avgP = ddSecP ? (float)(ddSumP / ddSecP) : 0;
  } else {
    int wAvg = 0;
    for (int i = 0; i < 24; i++) {
      if (i == nowHAvg) continue;                 // час ещё не прожит
      int cnt = 0;
      float hAvg = hourAverage(arrP, 4, i, &cnt);
      if (cnt > 0) { avgP += hAvg * cnt; wAvg += cnt; }
    }
    if (wAvg > 0) avgP /= (float)wAvg;
    else if (dayPSec > 0) avgP = (float)(daySumP / dayPSec);
    else avgP = gPower;
  }

  drawTxt(12, 126, past ? "СРЕДНЯЯ ЗА ДЕНЬ" : "СРЕДНЯЯ С 00:00", cMuted, cBg, 1);
  if (past && ddSecP == 0) strcpy(b, "--");
  else                     snprintf(b, sizeof(b), "%.0f ВТ", avgP);
  drawTxt(12, 136, b, cText, cBg, 2);

  // ПИК берём из почасовых максимумов (их заполняет PWRFILL из облака),
  // а не из dayMaxP: тот считается только по живым замерам и после сброса
  // показывает ерунду. Для прошлого дня почасовых пиков из облака нет —
  // берём максимум строк этого дня с карты.
  float peakP = 0;
  int peakH = -1;
  if (past) {
    peakP = ddMaxP;
  } else {
    for (int i = 0; i < 24; i++) {
      if (h24PmOk[i] && h24Pm[i] > peakP) { peakP = h24Pm[i]; peakH = i; }
    }
    if (peakP < dayMaxP) { peakP = dayMaxP; peakH = -1; }   // живой максимум больше
  }

  drawTxt(118, 126, "ПИК", cMuted, cBg, 1);
  if (past && ddSecP == 0) strcpy(b, "--");
  else                     snprintf(b, sizeof(b), "%.0f ВТ", peakP);
  drawTxt(118, 136, b, cWarn, cBg, 2);
  char pkT[12];
  if (peakH >= 0) snprintf(pkT, sizeof(pkT), "%02d:%02d", peakH, (int)h24PmMin[peakH]);
  else            hhmm(past ? ddPMin : dayMaxPMin, pkT, sizeof(pkT));
  // ДЛИТЕЛЬНОСТЬ пика: сколько записей дня было не ниже 80 % пика (шаг ~30 с).
  // Оценка по гистограмме gPkHist — см. pkDurSec(); точнее без второго
  // прохода по файлу не посчитать.
  int pkMin = 0;
  if (!(past && ddSecP == 0) && peakP > 0.5f) {
    pkMin = (pkDurSec(gPkHist, peakP) + 59) / 60;   // 20 с — уже «1 МИН»
    if (pkMin > 999) pkMin = 999;
  }
  if (pkMin > 0) snprintf(b, sizeof(b), "%s  %d МИН", pkT, pkMin);
  else           snprintf(b, sizeof(b), "%s", pkT);
  drawTxt(118, 154, b, cMuted, cBg, 1);

  drawTxt(12, 166, "ТОК", cMuted, cBg, 1);
  snprintf(b, sizeof(b), "%.2f А", gCurrent);
  drawTxt(54, 166, b, cText, cBg, 2);

  drawTxt(12, 180, "НАПРЯЖЕНИЕ X ТОК", cMuted, cBg, 1);
  snprintf(b, sizeof(b), "%.0f В  %.2f А  %.0f ВТ", gVoltage, gCurrent, gPower);
  drawTxt(12, 192, b, cMuted, cBg, 1);

  drawHourTitle(204, arrP, 4, "ВТ", 0);
  if (!gSkipChart)
    drawHourBars(10, 214, 220, 72, arrP, 4, cPOWER_ACC, tft.color565(253, 230, 138), true);
  char cap[80];
  if (past) snprintf(cap, sizeof(cap), "СРЕДНЯЯ МОЩНОСТЬ ЗА %s", gDayLabel);
  else      snprintf(cap, sizeof(cap), "СРЕДНЯЯ МОЩНОСТЬ ЗА КАЖДЫЙ ЧАС");
  drawTxt(10, 299, cap, cMuted, cBg, 1);
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
  // Время последнего получения счётчика: и «сегодня», и счётчик считаются
  // из него, поэтому метка стоит у обоих.
  char et[24];
  stampStr(gEnergyMs, et, sizeof(et));
  uint16_t ec = stampStale(gEnergyMs) ? cWarn : cMuted;
  drawTxtR(228, 52, et, ec, cBg, 1);

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
  uiVLine(120, 50, 44, cLine);

  // Счётчик целиком и время его последнего получения — одной строкой.
  snprintf(b, sizeof(b), "СЧЁТЧИК %.2f  %s", gEnergy, et);
  drawTxt(12, 96, b, ec, cBg, 1);

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

  // GAPFILL: если сегодня в истории были пропуски — мелкая жёлтая пометка.
  // Мин/макс напряжения и пик мощности могли быть посчитаны без этих точек.
  if (gapTodayCount() > 0) {
    char gb[40];
    snprintf(gb, sizeof(gb), "ПРОПУСКИ %d МИН", gapTodayMinutes());
    drawTxt(12, y + 46, gb, cWarn, cBg, 1);
  }
}

// ---------------------------------------------------------------- окно погоды
void drawWeatherScreen() {
  uiScreen(cBg);
  drawDetailHeader("ПОГОДА");

  char b[40];
  drawTxt(12, 54, "АДЛЕР", cMuted, cBg, 1);
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

  char d[64];
  weatherDesc(d, sizeof(d), 34);
  drawTxt(12, 122, d, cText, cBg, fitScale(d, 216, 2));

  uiHLine(12, 145, 216, cLine);

  drawTxt(12, 152, "ВЕТЕР", cMuted, cBg, 1);
  snprintf(b, sizeof(b), "%.1f М/С", wWind / 3.6f);
  drawTxtR(228, 150, b, cText, cBg, 2);

  drawTxt(12, 174, "ВЛАЖНОСТЬ", cMuted, cBg, 1);
  snprintf(b, sizeof(b), "%.0f%%", wHum);
  drawTxtR(228, 172, b, cText, cBg, 2);

  // Вероятность дождя сейчас: list[0].pop из прогноза OWM (0..100 %).
  drawTxt(12, 196, "ДОЖДЬ", cMuted, cBg, 1);
  if (wFcOk) {
    snprintf(b, sizeof(b), "%.0f%%", wPopNow);
    drawTxtR(228, 194, b, (wPopNow >= 50.0f) ? cAccent : cText, cBg, 2);
  } else {
    drawTxtR(228, 194, "--", cMuted, cBg, 2);
  }

  drawTxt(12, 218, "ОБНОВЛЕНО", cMuted, cBg, 1);
  if (wTime) {
    struct tm wt;
    localtime_r(&wTime, &wt);
    snprintf(b, sizeof(b), "%02d:%02d", wt.tm_hour, wt.tm_min);
  } else strcpy(b, "--:--");
  drawTxtR(228, 216, b, cText, cBg, 2);

  uiHLine(12, 240, 216, cLine);

  // ---- завтра: диапазон температур, описание и вероятность дождя
  drawTxt(12, 248, "ЗАВТРА", cMuted, cBg, 1);
  if (wFcOk) {
    snprintf(b, sizeof(b), "%.0f-%.0f C", wTminTom, wTmaxTom);
    drawTxtR(228, 246, b, cSky, cBg, 2);

    drawTxt(12, 268, wDescTom.c_str(), cText, cBg, 1);
    snprintf(b, sizeof(b), "ДОЖДЬ %.0f%%", wPopTom);
    drawTxtR(228, 268, b, (wPopTom >= 50.0f) ? cAccent : cText, cBg, 1);

    if (wRainTom > 0.05f) {
      snprintf(b, sizeof(b), "ОСАДКИ %.1f ММ", wRainTom);
      drawTxt(12, 282, b, cMuted, cBg, 1);
    }
  } else {
    drawTxtR(228, 246, "--", cMuted, cBg, 2);
    drawTxt(12, 268, "ПРОГНОЗА ПОКА НЕТ", cMuted, cBg, 1);
  }

  drawTxt(12, 298, "ИСТОЧНИК OPENWEATHERMAP", cMuted, cBg, 1);
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
void drawCalTarget(int x, int y, int step);   // объявлена ниже

void drawScreen() {
  gDrawN++;
  gLastWinRefresh = millis();
  if (gTapTest) { drawTapTest(); return; }
  // Пока идёт калибровка тача — на экране только мишень
  if (gCalStep > 0) {
    if (gCalStep == 1) drawCalTarget(20, 20, 1);
    else               drawCalTarget(220, 300, 2);
    return;
  }
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
  // Выбранный день и час тоже видны на экране. Раньше их здесь не было:
  // после dayShift() сигнатура не менялась, но это лечилось ручным сбросом
  // gLastSig. Теперь отпечаток честно учитывает, ЧТО нарисовано.
  h ^= (uint32_t)(gDayOffset * 2654435761u); h *= 16777619u;
  h ^= (uint32_t)(gChartHour + 1);           h *= 16777619u;
  uint32_t v[24];
  int n = 0;
  switch (gScreen) {
    case SCR_VOLT:
      v[n++] = (uint32_t)(int32_t)(time(nullptr) / 60);   // метка «получено» видна
      v[n++] = (uint32_t)(int32_t)(lroundf(gVoltage / 2.0f) * 2);
      v[n++] = gRelay ? 1u : 0u;
      // Для прошлого дня на экране dd* (сводка из карты), а не живые day*.
      // Раньше сюда попадали ЖИВЫЕ daySumV/dayMaxV: statsTick() менял их
      // каждые 5 с, сигнатура «менялась», и окно заливалось белым — то самое
      // частое моргание при просмотре прошлых дней.
      if (gDayOffset > 0) {
        v[n++] = (uint32_t)(int32_t)lroundf(((ddMinV < 900) ? ddMinV : 0) * 10.0f);
        v[n++] = (uint32_t)(int32_t)lroundf(ddMaxV * 10.0f);
        v[n++] = (uint32_t)(int32_t)((ddSecV > 0) ? (ddSumV / ddSecV) * 10.0 : 0);
        v[n++] = (uint32_t)ddOutV;
      } else {
        v[n++] = (uint32_t)(int32_t)lroundf(((dayMinV < 900) ? dayMinV : 0) * 10.0f);
        v[n++] = (uint32_t)(int32_t)lroundf(dayMaxV * 10.0f);
        v[n++] = (uint32_t)(int32_t)((dayVSec > 0) ? (daySumV / dayVSec) * 10.0 : 0);
        v[n++] = (uint32_t)dayOutSec;
      }
      break;
    case SCR_TEMP:
      v[n++] = (uint32_t)(int32_t)(time(nullptr) / 60);   // метка «получено» видна
      v[n++] = gTempValid ? (uint32_t)(int32_t)(lroundf(gTemp) * 10) : 0xFFFFFFu;
      if (gDayOffset > 0) {
        v[n++] = (uint32_t)(int32_t)lroundf(((ddMinT < 900) ? ddMinT : 0) * 10.0f);
        v[n++] = (uint32_t)(int32_t)lroundf(ddMaxT * 10.0f);
        v[n++] = (uint32_t)(int32_t)((ddSecT > 0) ? (ddSumT / ddSecT) * 10.0 : 0);
      } else {
        v[n++] = (uint32_t)(int32_t)lroundf(((dayMinT < 900) ? dayMinT : 0) * 10.0f);
        v[n++] = (uint32_t)(int32_t)lroundf(dayMaxT * 10.0f);
        v[n++] = (uint32_t)(int32_t)((dayTSec > 0) ? (daySumT / dayTSec) * 10.0 : 0);
      }
      break;
    case SCR_POWER:
      v[n++] = (uint32_t)(int32_t)(time(nullptr) / 60);   // метка «получено» видна
      // Округляем ГРУБО: иначе каждое изменение на 1 Вт заливало область
      // окна белым — это и выглядело как «мини-вспышка» каждые 30 секунд.
      v[n++] = (uint32_t)(int32_t)(lroundf(gPower / 25.0f) * 25);
      v[n++] = (uint32_t)(int32_t)(lroundf(gVoltage / 2.0f) * 2);
      if (gDayOffset > 0) {
        v[n++] = (uint32_t)(int32_t)fmodf((float)((ddSecP > 0) ? ddSumP / ddSecP : 0), 1000.0f);
        v[n++] = (uint32_t)(int32_t)lroundf(ddMaxP);
      } else {
        v[n++] = (uint32_t)(int32_t)fmodf((float)((dayPSec > 0) ? daySumP / dayPSec : 0), 1000.0f);
        v[n++] = (uint32_t)(int32_t)lroundf(dayMaxP);
      }
      break;
    case SCR_TODAY:
      v[n++] = (uint32_t)(int32_t)(time(nullptr) / 60);   // метка счётчика видна
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
      // Карточка «СЕГОДНЯ | ЗАВТРА»: код виден иконкой, завтрашние поля — текстом.
      v[n++] = (uint32_t)wCode;
      v[n++] = (uint32_t)wCodeTom;
      v[n++] = (uint32_t)(int32_t)lroundf(wTminTom * 10.0f);
      v[n++] = (uint32_t)(int32_t)lroundf(wTmaxTom * 10.0f);
      v[n++] = (uint32_t)(int32_t)lroundf(wPopTom);
      v[n++] = wFcOk ? 1u : 0u;
      // rssi и число строк карты сюда НЕ входят: они меняются каждые
      // 15-60 с и вызывали полную перерисовку пяти карточек (~139 КБ SPI).
      // Статус-бар обновляется отдельным таймером раз в 60 с (loop).
      break;
  }
  for (int i = 0; i < n; i++) { h ^= v[i]; h *= 16777619u; }
  // строки (описание погоды) — по байтам, они тоже видны на экране
  if (gScreen == SCR_MAIN || gScreen == SCR_WEATHER) {
    for (int i = 0; i < (int)wDescRu.length(); i++) { h ^= (uint8_t)wDescRu[i]; h *= 16777619u; }
    // wDescEn меняет иконку (Clouds/Rain/Snow), даже если русский текст тот же
    for (int i = 0; i < (int)wDescEn.length(); i++) { h ^= (uint8_t)wDescEn[i]; h *= 16777619u; }
    // на главной видно ещё и описание «завтра»
    if (gScreen == SCR_MAIN) {
      for (int i = 0; i < (int)wDescTom.length(); i++) { h ^= (uint8_t)wDescTom[i]; h *= 16777619u; }
    }
  }
  return h;
}

// Главный экран: фон НЕ заливаем целиком (это 63 600 px, ~25 мс). Каждый
// блок и шапка заливаются сами в drawBlockFrame/drawTopBar, а промежутки
// между блоками никогда не рисуются — там и так фон.
unsigned long gRedrawN = 0;        // счётчик полных перерисовок главного экрана
void refreshMain() {
  if (gCap) { drawScreen(); return; }
  Serial.printf("перерисовка #%lu\n", ++gRedrawN);
  gNoClear = true;
  drawMain();
  gNoClear = false;
}

// Окно открыто: стираем ТОЛЬКО области с цифрами, остальное перерисовывается
// поверх теми же пикселями. Полной заливки экрана нет, тач не блокируется.
void refreshWindow() {
  if (gCap) { drawScreen(); return; }
  gWinRefreshN++;
  switch (gScreen) {
    case SCR_WATCH:
      uiFill(0, 50, 240, 168, cBg);     // весь список
      break;
    case SCR_VOLT:
      uiFill(10,  50, 214, 60, cBg);    // цифры, метка получения, вердикт, реле
      if (gDayOffset == 0) {            // для прошлого дня сводка dd* статична
        uiFill(10, 136, 216, 44, cBg);  // мин / сред / макс
        uiFill(12, 178, 216,  8, cBg);  // вне нормы
      }
      break;
    case SCR_TEMP:
      uiFill(10,  50, 214, 60, cBg);    // цифры и метка получения
      if (gDayOffset == 0) uiFill(10, 116, 216, 56, cBg);  // макс / средняя / мин
      break;
    case SCR_POWER:
      uiFill(10,  50, 214, 60, cBg);    // цифры и метка получения
      if (gDayOffset == 0) uiFill(10, 116, 216, 88, cBg);  // средняя / пик / ток / U*I
      break;
    default:
      uiFill(10, 50, 220, 244, cBg);    // «ИТОГИ» и «ПОГОДА»
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

// Области графиков: {x, y, w, h}. Столбики идут от x шагом 9 px, 24 штуки.
static const int CHART_VOLT[4]  = { 10, 202, 220, 86 };
static const int CHART_TEMP[4]  = { 10, 186, 220, 86 };
static const int CHART_POWER[4] = { 10, 214, 220, 72 };

// ---------------------------------------------------------------- листание дней
// Строка заголовка графика (там, где нарисованы ‹ дата ›) — по экранам.
static int chartTitleY() {
  if (gScreen == SCR_VOLT)  return 192;
  if (gScreen == SCR_TEMP)  return 176;
  if (gScreen == SCR_POWER) return 204;
  return -1;
}
static const int* chartRect() {
  if (gScreen == SCR_VOLT)  return CHART_VOLT;
  if (gScreen == SCR_TEMP)  return CHART_TEMP;
  if (gScreen == SCR_POWER) return CHART_POWER;
  return NULL;
}

// «ЗАГРУЗКА…» на месте графика: чтение суток с карты занимает 1–3 с, и
// человек должен видеть, что плата работает, а не что она зависла.
static void drawDayLoading() {
  const int* ch = chartRect();
  if (!ch) return;
  gDrawN++;                                  // это тоже перерисовка (загрузка)
  uiFill(ch[0], ch[1], ch[2], ch[3], cBg);
  drawTxtC(ch[0] + ch[2] / 2, ch[1] + ch[3] / 2 - 12, "ЗАГРУЗКА...", cAccent, cBg, 2);
  drawTxtC(ch[0] + ch[2] / 2, ch[1] + ch[3] / 2 + 6, gDayLabel, cText, cBg, 2);
  drawTxtC(ch[0] + ch[2] / 2, ch[1] + ch[3] / 2 + 22, "ЧИТАЮ КАРТУ", cMuted, cBg, 1);
}

// Сменить день: delta = +1 — на день назад (‹), -1 — ближе к сегодня (›).
// resetStats() здесь НЕ вызывается: сегодняшняя статистика (мин/сред/макс и
// h24*) остаётся целой, работаем только с буфером day*.
void dayShift(int delta) {
  int n = gDayOffset + delta;
  if (n < 0) n = 0;
  if (n > gDayMaxOffset) n = gDayMaxOffset;
  if (n == gDayOffset) return;                    // дальше некуда — ничего не делаем
  gDayOffset = n;
  gChartHour = -1;                                // выбранный час сбрасываем
  if (n == 0) {
    drawScreen();                                 // сегодня — живые h24*, читать нечего
  } else {
    if (gDayLoaded != n) {
      char date[12], label[8];
      if (dayDate(n, date, sizeof(date), label, sizeof(label)))
        memcpy(gDayLabel, label, sizeof(label));
      // Ровно ОДНА перерисовка «ЗАГРУЗКА…» и ровно ОДНА — результат.
      // Пока флаг поднят, loop() не имеет права запускать обновление окна.
      gDayLoading = true;
      drawDayLoading();                           // 1–3 с чтения — показать, что живы
      dayLoadFromSd(n);                           // внутри обновит gDayMaxOffset
      gDayLoading = false;
    }
    drawScreen();
  }
  gLastDraw = millis();
  gLastSig = screenSig();
  gLastWinFull = millis();
}

void handleTapAt(int x, int y) {
  if (gScreen != SCR_MAIN) {
    // Стрелки ‹ › и дата в строке заголовка графика (левая треть, центр,
    // правая треть полосы). Проверяем ДО графика: полоса вплотную над ним.
    int ty = chartTitleY();
    if (ty > 0 && y >= ty - 6 && y <= ty + 10) {
      if (x < 44)       { dayShift(+1); return; }        // ‹ — прошлый день
      else if (x < 116) {                                // дата — назад к сегодня
        if (gDayOffset != 0) {
          gDayOffset = 0; gChartHour = -1;
          drawScreen();
          gLastDraw = millis(); gLastSig = screenSig(); gLastWinFull = millis();
        }
        return;
      }
      else if (x < 156) { dayShift(-1); return; }        // › — следующий день
    }
    // Нажатие на столбик графика — показать значение этого часа
    const int* ch = chartRect();
    if (ch && inRect(x, y, ch[0], ch[1], ch[2], ch[3])) {
      int hour = (x - ch[0]) / 9;
      if (hour < 0)  hour = 0;
      if (hour > 23) hour = 23;
      gChartHour = (gChartHour == hour) ? -1 : hour;   // повторно — назад к текущему
      drawScreen();
      gLastDraw = millis();
      gLastSig = screenSig();
      gLastWinFull = millis();
      return;
    }
    if (inRect(x, y, 0, 0, 240, 52)) {          // вся верхняя полоса — «назад»
      gScreen = SCR_MAIN;
      gChartHour = -1;
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
  // Экран погашен: ЛЮБОЕ касание включает подсветку на 20 с (DISP NOTOUCH — отключить).
  if (!gDispOn && tpDown()) dispWake();
  if (gTapTest) {
    if (millis() < lock) return;
    if (!tpDown()) return;
    int tx = 0, ty = 0;
    if (tpPoint(&tx, &ty)) { lock = millis() + 250; drawTapTest(); }
    return;
  }
  if (millis() < lock) return;
  if (gCalStep > 0) {
    if (!tpDown()) return;
    int cx = 0, cy = 0;
    if (tpPoint(&cx, &cy)) {
      lock = millis() + 400;
      calTick(cx, cy);
    }
    return;
  }
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
  if (gBeepOn && gBeepTouch) beepStart(1, 2600, 25, 0);   // тихий пик подтверждения
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
  wOk = false; wTemp = 0; wHum = 0; wWind = 0; wCode = -1; wDescEn = ""; wDescRu = "";
  wCodeTom = 0; wDescTom = ""; wFcOk = false; wPopNow = 0; wPopTom = 0;
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
//  ВЕБ-СТРАНИЦА — текущие значения, история по дням, графики
// ============================================================================
//  http://192.168.1.51/                    — страница (светлая, с графиками)
//  http://192.168.1.51/data                — текущие значения в JSON
//  http://192.168.1.51/history?date=ДЕНЬ   — почасовая история за день
//  http://192.168.1.51/days                — даты, за которые есть данные
//  http://192.168.1.51/events              — последние переключения реле
//  /csv /day /full /files /tap /rotate     — диагностика (как раньше)
WebServer gWeb(80);

// Строгий разбор целого: "abc" и "12x" — не число. String::toInt() вернул бы 0,
// и /disp?on=abc молча гасил подсветку.
static bool parseIntStrict(const String& s, long& out) {
  if (!s.length() || s.length() > 8) return false;
  int i = 0;
  bool neg = false;
  if (s[0] == '-') { neg = true; i = 1; if (s.length() == 1) return false; }
  long v = 0;
  for (; i < (int)s.length(); i++) {
    char c = s[i];
    if (c < '0' || c > '9') return false;
    v = v * 10 + (c - '0');
    if (v > 100000L) return false;
  }
  out = neg ? -v : v;
  return true;
}

// Проверка календарной даты "YYYY-MM-DD" (месяц 1-12, день с учётом месяца,
// год 2020..2100). Раньше 0000-00-00, 2026-13-45 и 2030-12-31 давали 200 с нулями.
static bool dateValid(const char* d, int len) {
  if (len != 10 || d[4] != '-' || d[7] != '-') return false;
  for (int i = 0; i < 10; i++) {
    if (i == 4 || i == 7) continue;
    if (d[i] < '0' || d[i] > '9') return false;
  }
  int y = (d[0]-'0')*1000 + (d[1]-'0')*100 + (d[2]-'0')*10 + (d[3]-'0');
  int m = (d[5]-'0')*10 + (d[6]-'0');
  int day = (d[8]-'0')*10 + (d[9]-'0');
  if (y < 2020 || y > 2100) return false;
  if (m < 1 || m > 12) return false;
  static const uint8_t dim[12] = {31,28,31,30,31,30,31,31,30,31,30,31};
  int maxd = dim[m-1];
  if (m == 2 && ((y % 4 == 0 && y % 100 != 0) || y % 400 == 0)) maxd = 29;
  if (day < 1 || day > maxd) return false;
  return true;
}

// ---- гистограмма мощности для ДЛИТЕЛЬНОСТИ пика ----
// Задача: «сколько времени мощность держалась не ниже 80 % пика часа» — без
// второго прохода по файлу (поток один, вернуться назад нельзя). Поэтому
// копим гистограмму мощности: 33 корзины, шаг 100 Вт до 2 кВт, 500 Вт до 6 кВт,
// 1 кВт до 10 кВт, последняя — «10 кВт и выше». 33 × 4 байта = 132 байта на час.
// В корзинах лежат СЕКУНДЫ (строка CSV = 30 с, живой отсчёт = его dt), поэтому
// длительность не зависит от того, ровные ли шаги у записей.
// Точность: порог 0.8·пик округляется к ближайшей границе корзины (100 Вт до
// 2 кВт) — длительность может отличаться на одну-две записи (±30-60 с).
// Это оценка, а не секундомер; см. комментарий в histJson.
#define PKD_BUCKETS 33

static inline uint8_t pkdBucket(float w) {
  if (!(w > 0)) return 0;
  if (w < 2000.0f) { int b = (int)(w * 0.01f); return b > 19 ? 19 : (uint8_t)b; }
  if (w < 6000.0f) { int b = (int)((w - 2000.0f) * 0.002f); return (uint8_t)(20 + (b > 7 ? 7 : b)); }
  if (w < 10000.0f) { int b = (int)((w - 6000.0f) * 0.001f); return (uint8_t)(28 + (b > 3 ? 3 : b)); }
  return 32;
}
// нижняя граница корзины, Вт
static inline float pkdBound(uint8_t b) {
  if (b < 20) return b * 100.0f;
  if (b < 28) return 2000.0f + (b - 20) * 500.0f;
  if (b < 32) return 6000.0f + (b - 28) * 1000.0f;
  return 10000.0f;
}
// Длительность, секунды: сумма секунд в корзинах не ниже 80 % пика.
static int pkDurSec(const uint32_t* hist, float peak) {
  if (!(peak > 0.5f)) return 0;
  float th = peak * 0.8f;
  if (th <= 0) return 0;
  uint8_t b = pkdBucket(th);
  // ближе к нижней границе своей корзины или к следующей?
  if (b < 32) {
    float lo = pkdBound(b), hi = pkdBound((uint8_t)(b + 1));
    if ((th - lo) > (hi - th)) b++;
  }
  uint32_t sec = 0;
  for (int k = b; k < PKD_BUCKETS; k++) sec += hist[k];
  if (sec > 86400UL) sec = 86400UL;        // больше суток в часе быть не может
  return (int)sec;
}

// ---- агрегат одного часа для /history ----
struct HourAgg {
  uint16_t n;          // всего строк за час
  uint16_t vN;         // строк с напряжением
  uint16_t pN;         // строк с мощностью
  uint16_t tN;         // строк с температурой
  float pSum, pMax, vSum, tSum;
  int32_t eFirstT;     // ЧЧММСС первого счётчика в часе
  int32_t eLastT;      // ЧЧММСС последнего счётчика в часе
  int32_t pMaxTod;     // ЧЧММСС ПИКА часа (-1 — данных не было)
  float eFirstE, eLastE;
  uint32_t pk[PKD_BUCKETS];  // секунды по корзинам мощности — для pmaxDur
};
static HourAgg sHA[24];
// Пики часов из облака (файл /energy.peak, его пишет PWRHIST). Нужны, чтобы
// после замены мощности на среднюю /history всё равно показывал реальный пик.
static uint16_t sPeakP[24];
static bool sPeakOk[24];
static uint8_t sPeakMin[24];     // минута пика внутри часа (0..59)
static uint8_t sPeakDur[24];     // минут выше 80 % пика
static bool sPeakTValid[24];     // минута пика известна (новый формат файла)

// "2026-10-30 " — дата в начале строки CSV.
static bool isDatePrefix(const char* s, int len) {
  if (len < 11) return false;
  for (int i = 0; i < 10; i++) {
    char c = s[i];
    if (i == 4 || i == 7) { if (c != '-') return false; }
    else if (c < '0' || c > '9') return false;
  }
  return s[10] == ' ';
}

// /energy.peak: строки "YYYY-MM-DD,HH,ПИК[,МИНУТА,МИНУТ_ВЫШЕ_80%]". PWRHIST
// дописывает их после успешной перезаписи CSV. Читаем перед разбором дня;
// последняя строка для пары (дата,час) побеждает.
// Старые файлы без минуты тоже читаем: тогда pmaxT остаётся null.
static void loadPeakOverlay(const char* date) {
  memset(sPeakP, 0, sizeof(sPeakP));
  memset(sPeakOk, 0, sizeof(sPeakOk));
  memset(sPeakMin, 0, sizeof(sPeakMin));
  memset(sPeakDur, 0, sizeof(sPeakDur));
  memset(sPeakTValid, 0, sizeof(sPeakTValid));
  if (!gSdOk) return;
  File f = SD.open("/energy.peak", FILE_READ);
  if (!f) return;
  int rd = 0;
  while (f.available()) {
    String l = f.readStringUntil('\n');
    if ((++rd & 63) == 0) esp_task_wdt_reset();
    l.trim();
    if (l.length() < 16 || memcmp(l.c_str(), date, 10) != 0 || l[10] != ',') continue;
    int c = l.indexOf(',', 11);
    if (c < 0) continue;
    int hh = l.substring(11, c).toInt();
    int c2 = l.indexOf(',', c + 1);
    int pk, mi = -1, du = 0;
    if (c2 < 0) {
      pk = l.substring(c + 1).toInt();
    } else {
      pk = l.substring(c + 1, c2).toInt();
      int c3 = l.indexOf(',', c2 + 1);
      if (c3 < 0) {
        mi = l.substring(c2 + 1).toInt();
      } else {
        mi = l.substring(c2 + 1, c3).toInt();
        du = l.substring(c3 + 1).toInt();
      }
    }
    if (hh >= 0 && hh < 24 && pk > 0 && pk <= 20000) {
      sPeakP[hh] = (uint16_t)pk;
      sPeakOk[hh] = true;
      sPeakMin[hh] = (mi >= 0 && mi < 60) ? (uint8_t)mi : 0;
      sPeakDur[hh] = (du >= 0 && du < 60) ? (uint8_t)du : 0;
      sPeakTValid[hh] = (mi >= 0 && mi < 60);
    }
  }
  f.close();
}

// Быстрое число из CSV — без String и без atof (в сутках ~2900 строк).
static float csvNum(const char*& p, const char* end) {
  while (p < end && (*p == ' ' || *p == '\t')) p++;
  bool neg = false;
  if (p < end && *p == '-') { neg = true; p++; }
  long ip = 0;
  bool any = false;
  while (p < end && *p >= '0' && *p <= '9') { ip = ip * 10 + (*p - '0'); p++; any = true; }
  float f = (float)ip;
  if (p < end && *p == '.') {
    p++;
    float d = 0.1f;
    while (p < end && *p >= '0' && *p <= '9') {
      f += (float)(*p - '0') * d;
      d *= 0.1f;
      p++;
      any = true;
    }
  }
  if (!any) f = 0;
  return neg ? -f : f;
}
static void csvSkip(const char*& p, const char* end) {
  while (p < end && *p != ',') p++;
  if (p < end) p++;
}

// Разбор одной строки дня: дата,В,Вт,кВт·ч,реле,руб,°C
// Температура (7-е поле) может отсутствовать или быть пустой.
static void histAdd(const char* s, int len) {
  if (len < 20) return;
  int hh = (s[11] - '0') * 10 + (s[12] - '0');
  if (hh < 0 || hh > 23) return;
  int mm = (s[14] - '0') * 10 + (s[15] - '0');
  int ss = (s[17] - '0') * 10 + (s[18] - '0');
  int tod = hh * 10000 + mm * 100 + ss;
  const char* p = s + 19;
  const char* end = s + len;
  if (p >= end || *p != ',') return;
  p++;
  float v  = csvNum(p, end); csvSkip(p, end);
  float pw = csvNum(p, end); csvSkip(p, end);
  float en = csvNum(p, end); csvSkip(p, end);
  csvSkip(p, end);                 // реле
  csvSkip(p, end);                 // рубли
  bool hasT = false;
  float tv = 0;
  while (p < end && (*p == ' ' || *p == '\t')) p++;
  if (p < end && *p != '\r' && *p != '\n' && *p != ',') { tv = csvNum(p, end); hasT = true; }

  HourAgg& a = sHA[hh];
  a.n++;
  // Строки, восстановленные из облака, помечены нулевым напряжением:
  // в мощность и напряжение они не идут (иначе средняя занижается).
  if (v > 50.0f) {
    a.vSum += v; a.vN++;
  }
  // Мощность считаем и для восстановленных строк (v=0), если PWRHIST уже
  // подставил туда среднюю из облака. Строки с pw=0 по-прежнему не шумят.
  if (v > 50.0f || pw > 0.5f) {
    a.pSum += pw; a.pN++;
    // Время пика часа: раньше histAdd хранил только pMax, и /history не мог
    // сказать, КОГДА был пик у локальных записей (было только у облачных).
    if (pw > a.pMax) { a.pMax = pw; a.pMaxTod = tod; }
    // гистограмма для длительности (см. pkDurSec): сколько СЕКУНД в часе
    // мощность была не ниже 80 % пика — строки CSV идут с шагом 30 с
    a.pk[pkdBucket(pw)] += 30;
  }
  // Счётчик: первый и последний ПО ВРЕМЕНИ. Файл не отсортирован
  // (архивные дни вклеены в середину), поэтому нельзя брать «как идёт».
  if (en > 0.0f) {
    if (a.eFirstT < 0 || tod < a.eFirstT) { a.eFirstT = tod; a.eFirstE = en; }
    if (a.eLastT < 0 || tod > a.eLastT)  { a.eLastT = tod;  a.eLastE = en; }
  }
  if (hasT && tv > -40.0f && tv < 100.0f) { a.tSum += tv; a.tN++; }
}

// Чтение файла БЛОКАМИ по 512 байт (а не readStringUntil на каждую строку).
// Цель — ответ быстрее 3 секунд; при переполнении времени отдаём что успели.
static bool histScan(const char* date, unsigned long* msOut, bool* truncOut) {
  memset(sHA, 0, sizeof(sHA));
  for (int i = 0; i < 24; i++) { sHA[i].eFirstT = -1; sHA[i].eLastT = -1; sHA[i].pMaxTod = -1; }
  loadPeakOverlay(date);
  if (msOut) *msOut = 0;
  if (truncOut) *truncOut = false;
  if (!gSdOk) return false;
  // дата может лежать в архиве прошлого месяца (energy-YYYY-MM.csv)
  char hpath[40];
  histPathForDate(date, hpath, sizeof(hpath));
  File f = SD.open(hpath, FILE_READ);
  if (!f) return false;

  size_t sz = f.size();
  size_t start = 0;
  bool today = false;
  struct tm ti;
  if (getLocalTime(&ti, 10)) {
    char td[12];
    snprintf(td, sizeof(td), "%04d-%02d-%02d",
             ti.tm_year + 1900, ti.tm_mon + 1, ti.tm_mday);
    if (strcmp(td, date) == 0) {
      today = true;
      // Сегодняшние строки всегда в конце — не читаем весь файл.
      size_t o = findDayStartOffset(f, date);
      if (o > start) start = o;
      if (o == 0 && !fileTailIsToday(f, date)) { f.close(); return true; }
    }
  }  // Файл НЕ отсортирован по времени: 23–26.10 вклеены в середину.
  // Поэтому читаем целиком, а для очень больших файлов — последние 400 КБ.
  if (!today && sz > 1048576UL) start = sz - 1000000UL;   // как в /days
  if (start >= sz) { f.close(); return true; }
  f.seek(start);

  static uint8_t hbuf[512 + 256];
  int carry = 0;
  int guard = 0;
  unsigned long t0 = millis();
  bool trunc = false;
  while (f.available()) {
    int n = (int)f.read(hbuf + carry, 512);
    if (n <= 0) break;
    int total = carry + n;
    int i = 0;
    while (i < total) {
      uint8_t* nl = (uint8_t*)memchr(hbuf + i, '\n', total - i);
      if (!nl) break;
      int len = (int)(nl - (hbuf + i));
      if (len >= 20 && memcmp(hbuf + i, date, 10) == 0 &&
          hbuf[i + 10] == ' ' && hbuf[i + 13] == ':')
        histAdd((const char*)(hbuf + i), len);
      i = (int)(nl - hbuf) + 1;
      if ((++guard & 1023) == 0) { esp_task_wdt_reset(); yield(); }
    }
    carry = total - i;
    if (carry > 0) memmove(hbuf, hbuf + i, carry);
    if (carry > 240) carry = 0;          // защита от строки без перевода строки
    esp_task_wdt_reset();
    // Бюджет снижен 3000 → 2500 мс: /history собирается ВНУТРИ handleClient,
    // и его время входит в blk=web. С ним проход loop() подбирался к 3.5 с.
    if (millis() - t0 > 2500UL) { trunc = true; break; }
  }
  f.close();
  if (msOut) *msOut = millis() - t0;
  if (truncOut) *truncOut = trunc;
  return true;
}

// Показание счётчика на границе часа h (0..24).
//   h < 24  — первая запись часа h (начало часа)
//   h == 24 — последняя запись последнего непустого часа (конец суток)
// Возвращает -1, если данных для этой границы нет.
static float edgeE(int h) {
  if (h < 24) return sHA[h].eFirstT >= 0 ? sHA[h].eFirstE : -1.0f;
  for (int k = 23; k >= 0; k--)
    if (sHA[k].eLastT >= 0) return sHA[k].eLastE;
  return -1.0f;
}

static String histJson(const char* date, unsigned long ms, bool trunc) {
  // E2 (исправлено 01.11.2026): раньше kwh часа = eLastE − eFirstE, то есть
  // разница между ПЕРВОЙ и ПОСЛЕДНЕЙ записью часа. При записи раз в 30 с терялись
  // до 90 секунд на каждом часе: от 00:00 до первой записи и от последней до :59.
  // При 2 кВт это 0.05 кВт·ч в час, а сумма по суткам не сходилась со счётчиком.
  //
  // Теперь считаем по СТЫКАМ: kwh(H) = edge(H+1) − edge(H), где edge(H) —
  // показание счётчика на первой записи часа H, edge(24) — на последней записи
  // последнего часа. Сумма по 24 часам телескопируется и даёт ровно суточный
  // прирост счётчика, без потерь на границах.
  String out;
  out.reserve(3600);
  char b[200];
  snprintf(b, sizeof(b), "{\"date\":\"%s\",\"ms\":%lu,\"trunc\":%s,\"hours\":[",
           date, ms, trunc ? "true" : "false");
  out += b;
  for (int i = 0; i < 24; i++) {
    HourAgg& a = sHA[i];
    float kwh = 0;
    float e0 = edgeE(i);
    if (e0 >= 0) {
      float e1 = -1;
      for (int j = i + 1; j <= 24; j++) { e1 = edgeE(j); if (e1 >= 0) break; }
      if (e1 >= e0) {
        kwh = e1 - e0;
        if (kwh < 0 || kwh > 100) kwh = 0;
      }
    }
    char tbuf[12];
    if (a.tN) snprintf(tbuf, sizeof(tbuf), "%.1f", a.tSum / a.tN);
    else      strcpy(tbuf, "null");
    float pmax = a.pMax;
    bool tOk = false;
    int  pmin = 0, pdur = 0;
    // Сначала — локальный пик с карты: время записи и длительность по
    // гистограмме (записи ~раз в 30 с). Если пик из облака (/energy.peak)
    // не меньше — берём его: он посчитан по 20-минутным окнам, точнее.
    if (a.pMax > 0.5f && a.pMaxTod >= 0) {
      pmin = (int)((a.pMaxTod / 100) % 100);
      pdur = pkDurSec(a.pk, a.pMax);
      tOk = true;
    }
    if (sPeakOk[i] && (float)sPeakP[i] >= pmax) {
      bool bigger = ((float)sPeakP[i] > a.pMax + 1.0f);
      pmax = (float)sPeakP[i];
      if (sPeakTValid[i]) { tOk = true; pmin = sPeakMin[i]; }
      // Пик из облака выше локального, а минуты в старом /energy.peak нет:
      // время неизвестно — честнее отдать null, чем минуту от другого пика.
      else if (bigger)    { tOk = false; pmin = 0; }
      int du = (int)sPeakDur[i] * 60;   // в файле МИНУТЫ, в JSON — СЕКУНДЫ
      if (du > pdur) pdur = du;         // иначе остаётся локальная оценка
    }
    if (i) out += ',';
    if (tOk)
      snprintf(b, sizeof(b),
               "{\"h\":%d,\"kwh\":%.2f,\"p\":%.0f,\"pmax\":%.0f,\"pmaxT\":\"%02d:%02d\","
               "\"pmaxDur\":%d,\"v\":%.0f,\"t\":%s,\"n\":%u}",
               i, kwh, a.pN ? a.pSum / a.pN : 0.0f, pmax, i, pmin, pdur,
               a.vN ? a.vSum / a.vN : 0.0f, tbuf, (unsigned)a.n);
    else
      snprintf(b, sizeof(b),
               "{\"h\":%d,\"kwh\":%.2f,\"p\":%.0f,\"pmax\":%.0f,\"pmaxT\":null,"
               "\"pmaxDur\":0,\"v\":%.0f,\"t\":%s,\"n\":%u}",
               i, kwh, a.pN ? a.pSum / a.pN : 0.0f, pmax,
               a.vN ? a.vSum / a.vN : 0.0f, tbuf, (unsigned)a.n);
    out += b;
  }
  out += "]}";
  return out;
}

// ---- кэш /history ----------------------------------------------------------
// Раньше КАЖДЫЙ заход читал до 1 МБ с карты внутри handleClient — интерфейс
// стоял больше секунды. Теперь готовый JSON держим в памяти: он годен, пока
// дата та же и размер файла не изменился (как eventsFresh у /events).
// Прошлые дни меняются только после PWRHIST/GAPFILL (там кэш сбрасывается),
// поэтому им хватает пяти минут. У сегодняшнего дня запись идёт раз в 30 с —
// размер файла меняется, и кэш сам себя сбрасывает; 30 с — верхняя граница.
#define HIST_TTL_TODAY 30000UL
#define HIST_TTL_PAST  300000UL
static String sHistJsonOut;
static char   sHistDate[12] = "";
static size_t sHistFileSize = 0;
static unsigned long sHistAt = 0;
static void histCacheDrop() { sHistAt = 0; sHistDate[0] = 0; sHistFileSize = 0; }

// ---------------------------------------------------------------------------
//  /days — список дат, за которые есть строки (свежие первыми, до 30)
// ---------------------------------------------------------------------------
static char sDayList[46][11];
static int sDayN = 0;
static String sDaysJson;
static size_t sDaysFileSize = 0;
static unsigned long sDaysAt = 0;
// Пропуски по дням (параллельно sDayList): сколько и минут. Нужны веб-странице,
// чтобы пометить день жёлтым «есть пропуски».
static uint16_t sDayGapN[46];
static uint16_t sDayGapMi[46];

static int dayAdd(const char* d) {
  for (int i = 0; i < sDayN; i++) if (memcmp(sDayList[i], d, 10) == 0) return i;
  if (sDayN >= 46) {
    memmove(sDayList[0], sDayList[1], 10 * 45);
    memmove(sDayGapN, sDayGapN + 1, sizeof(uint16_t) * 45);
    memmove(sDayGapMi, sDayGapMi + 1, sizeof(uint16_t) * 45);
    sDayN = 45;
  }
  memcpy(sDayList[sDayN], d, 10);
  sDayList[sDayN][10] = 0;
  sDayGapN[sDayN] = 0;
  sDayGapMi[sDayN] = 0;
  sDayN++;
  return sDayN - 1;
}

static void daySortDesc() {
  for (int i = 0; i < sDayN; i++)
    for (int j = i + 1; j < sDayN; j++)
      if (strcmp(sDayList[j], sDayList[i]) > 0) {
        char t[11];
        uint16_t a = sDayGapN[i], b = sDayGapMi[i];
        memcpy(t, sDayList[i], 11);
        memcpy(sDayList[i], sDayList[j], 11);
        memcpy(sDayList[j], t, 11);
        sDayGapN[i] = sDayGapN[j]; sDayGapMi[i] = sDayGapMi[j];
        sDayGapN[j] = a;           sDayGapMi[j] = b;
      }
}

static long gfLineEpoch(const char* s, int len);   // объявлена в блоке GAPFILL

// Один файл истории -> дни и пропуски в sDayN/sDayGapN. Вызывается и для
// рабочего /energy.csv, и для каждого архива /energy-YYYY-MM.csv: 1 ноября
// ноябрьский файл начался с нуля, и октябрь просто исчез из /days.
static void scanDaysOne(const char* path) {
  File f = SD.open(path, FILE_READ);
  if (!f) return;
  size_t sz = f.size();
  size_t start = (sz > 1048576UL) ? sz - 1048576UL : 0;
  f.seek(start);
  if (start > 0) f.readStringUntil('\n');     // первая строка может быть обрезана
  static uint8_t dbuf[4096 + 128];
  int carry = 0;
  unsigned long t0 = millis();
  long prevT = 0;
  while (f.available()) {
    int n = (int)f.read(dbuf + carry, 4096);
    if (n <= 0) break;
    int total = carry + n;
    int i = 0;
    while (i < total) {
      uint8_t* nl = (uint8_t*)memchr(dbuf + i, '\n', total - i);
      if (!nl) break;
      int len = (int)(nl - (dbuf + i));
      if (isDatePrefix((const char*)(dbuf + i), len)) {
        int di = dayAdd((const char*)(dbuf + i));
        long t = gfLineEpoch((const char*)(dbuf + i), len);
        if (t > 0) {
          if (prevT > 0) {
            long dd = t - prevT;
            // Разрыв > 2 минут — пропуск. Больше 6 часов и «назад по времени»
            // не считаем: это вклеенные задним числом строки, а не дыра.
            if (dd > 120 && dd <= 21600 && di >= 0) {
              if (sDayGapN[di] < 65000) sDayGapN[di]++;
              int mins = (int)((dd + 59) / 60);
              if (sDayGapMi[di] < 60000) sDayGapMi[di] = (uint16_t)(sDayGapMi[di] + mins);
            }
          }
          prevT = t;
        }
      }
      i = (int)(nl - dbuf) + 1;
    }
    carry = total - i;
    if (carry > 0) memmove(dbuf, dbuf + i, carry);
    if (carry > 120) carry = 0;
    esp_task_wdt_reset();
    if (millis() - t0 > 5000UL) break;
  }
  f.close();
}

static bool scanDays(size_t* sizeOut, unsigned long* msOut) {
  sDayN = 0;
  memset(sDayGapN, 0, sizeof(sDayGapN));
  memset(sDayGapMi, 0, sizeof(sDayGapMi));
  if (!gSdOk) return false;
  if (sizeOut) {
    File f = SD.open("/energy.csv", FILE_READ);
    if (!f) return false;
    *sizeOut = f.size();
    f.close();
  }
  unsigned long t0 = millis();
  scanDaysOne("/energy.csv");
  // Архивы прошлых месяцев: без них /days после 1-го числа знает только
  // текущий месяц, а «‹» на экране не может уйти в октябрь.
  File dir = SD.open("/");
  if (dir) {
    File e = dir.openNextFile();
    while (e) {
      bool isArch = false;
      char ap[40] = "";
      if (!e.isDirectory()) {
        String n = e.name();
        int slash = n.lastIndexOf('/');
        if (slash >= 0) n = n.substring(slash + 1);
        if (n.startsWith("energy-") && n.endsWith(".csv") && n.length() == 18) {
          snprintf(ap, sizeof(ap), "/%s", n.c_str());
          isArch = true;
        }
      }
      e.close();
      if (isArch) scanDaysOne(ap);
      e = dir.openNextFile();
    }
    dir.close();
  }
  daySortDesc();
  if (msOut) *msOut = millis() - t0;
  return true;
}

static void buildDaysJson(unsigned long ms) {
  String out;
  out.reserve(46 * 26 + 64);
  out = "{\"days\":[";
  int shown = 0;
  for (int i = 0; i < sDayN && shown < 30; i++, shown++) {
    if (i) out += ',';
    out += '"';
    out += sDayList[i];
    out += '"';
  }
  out += "],\"gaps\":[";
  shown = 0;
  for (int i = 0; i < sDayN && shown < 30; i++, shown++) {
    if (i) out += ',';
    out += '[';
    out += String((unsigned)sDayGapN[i]);
    out += ',';
    out += String((unsigned)sDayGapMi[i]);
    out += ']';
  }
  out += "],\"ms\":";
  out += String(ms);
  out += '}';
  sDaysJson = out;
}

// Дата последней строки файла — дешёвая проверка «не начался ли новый день».
static bool tailDate(char* out) {
  File f = SD.open("/energy.csv", FILE_READ);
  if (!f) return false;
  size_t sz = f.size();
  if (sz < 30) { f.close(); return false; }
  size_t from = sz > 300 ? sz - 300 : 0;
  f.seek(from);
  char b[320];
  int n = (int)f.read((uint8_t*)b, 300);
  f.close();
  if (n < 20) return false;
  b[n] = 0;
  int end = n;
  while (end > 0 && (b[end - 1] == '\n' || b[end - 1] == '\r')) end--;
  int s = end;
  while (s > 0 && b[s - 1] != '\n') s--;
  if (end - s < 11) return false;
  if (!isDatePrefix(b + s, end - s)) return false;
  memcpy(out, b + s, 10);
  out[10] = 0;
  return true;
}

static bool daysFresh(size_t szNow) {
  if (sDaysJson.length() == 0) return false;
  if (millis() - sDaysAt > 6UL * 3600UL * 1000UL) return false;
  if (szNow < sDaysFileSize || szNow - sDaysFileSize > 65536UL) return false;
  char td[11];
  if (tailDate(td) && sDaysJson.indexOf(td) < 0) return false;
  return true;
}

// ---------------------------------------------------------------------------
//  /events — последние переключения реле (5-е поле CSV)
// ---------------------------------------------------------------------------
// Сколько строк истории просматривать при поиске событий реле.
// Раньше здесь был malloc(8000*4) = 32 КБ: когда heap просел до 62 КБ,
// свободного БЛОКА нужного размера не находилось и /events отдавал 500
// навсегда. Теперь массив статический и меньше — 16 КБ в BSS.
#define EV_MAX 4000
static String sEvJson;
static size_t sEvFileSize = 0;
static unsigned long sEvAt = 0;

static int cmpU32(const void* a, const void* b) {
  uint32_t x = *(const uint32_t*)a, y = *(const uint32_t*)b;
  return x < y ? -1 : (x > y ? 1 : 0);
}
static int dig2(const char* s) { return (s[0] - '0') * 10 + (s[1] - '0'); }
static int dig4(const char* s) {
  return (s[0] - '0') * 1000 + (s[1] - '0') * 100 + (s[2] - '0') * 10 + (s[3] - '0');
}

// Возвращает до 10 событий (время + новое состояние) в хронологическом порядке.
// Время — локальный «эпохальный» счётчик (как lineEpoch), поэтому gmtime_r.
static int scanEvents(uint32_t* outT, uint8_t* outR, size_t* sizeOut, unsigned long* msOut) {
  if (msOut) *msOut = 0;
  if (!gSdOk) return -1;
  size_t szNow = 0;
  struct tm ti;
  if (!getLocalTime(&ti, 10)) return -1;
  long now = daysFromCivil(ti.tm_year + 1900, ti.tm_mon + 1, ti.tm_mday) * 86400L
           + ti.tm_hour * 3600L + ti.tm_min * 60L + ti.tm_sec;
  time_t cutT = (time_t)(now - 3L * 86400L);
  struct tm ct;
  gmtime_r(&cutT, &ct);
  char cutDate[12];
  snprintf(cutDate, sizeof(cutDate), "%04d-%02d-%02d",
           ct.tm_year + 1900, ct.tm_mon + 1, ct.tm_mday);

  static uint32_t arr[EV_MAX];      // статический: malloc 32 КБ не проходит при просадке heap
  File f = SD.open("/energy.csv", FILE_READ);
  if (!f) return -1;
  size_t sz = f.size();
  if (sizeOut) *sizeOut = sz;
  size_t start = (sz > 1258291UL) ? sz - 800000UL : 0;
  f.seek(start);
  if (start > 0) f.readStringUntil('\n');

  static uint8_t ebuf[4096 + 256];
  int carry = 0;
  int nEv = 0;
  unsigned long t0 = millis();
  while (f.available() && nEv < EV_MAX) {
    int n = (int)f.read(ebuf + carry, 4096);
    if (n <= 0) break;
    int total = carry + n;
    int i = 0;
    while (i < total && nEv < EV_MAX) {
      uint8_t* nl = (uint8_t*)memchr(ebuf + i, '\n', total - i);
      if (!nl) break;
      int len = (int)(nl - (ebuf + i));
      const char* s = (const char*)(ebuf + i);
      if (len >= 20 && isDatePrefix(s, len) && memcmp(s, cutDate, 10) >= 0) {
        const char* p = s + 19;
        const char* end = s + len;
        if (p < end && *p == ',') {
          p++;
          float v = csvNum(p, end); csvSkip(p, end);
          csvSkip(p, end);               // ватты
          float en = csvNum(p, end); csvSkip(p, end);
          float rel = csvNum(p, end);
          // Только настоящие строки: у восстановленных из облака счётчик
          // нулевой, а реле в них всегда 0 — иначе получаются ложные
          // «выключения» каждый час.
          if (v > 50.0f && en > 0.0f) {
            long ep = daysFromCivil(dig4(s), (unsigned)dig2(s + 5), (unsigned)dig2(s + 8)) * 86400L
                    + dig2(s + 11) * 3600L + dig2(s + 14) * 60L + dig2(s + 17);
            arr[nEv++] = ((uint32_t)ep << 1) | (rel > 0.5f ? 1u : 0u);
          }
        }
      }
      i = (int)(nl - ebuf) + 1;
    }
    carry = total - i;
    if (carry > 0) memmove(ebuf, ebuf + i, carry);
    if (carry > 250) carry = 0;
    esp_task_wdt_reset();
    if (millis() - t0 > 4000UL) break;
  }
  f.close();

  qsort(arr, nEv, sizeof(uint32_t), cmpU32);
  // Кольцо на 10 событий: нужно ПОСЛЕДНИЕ переключения, а не первые.
  int cnt = 0, head = 0;
  bool have = false;
  uint8_t prevR = 0;
  uint32_t prevT = 0;
  for (int k = 0; k < nEv; k++) {
    uint32_t t = arr[k] >> 1;
    uint8_t r = (uint8_t)(arr[k] & 1u);
    if (have && t == prevT) continue;          // дубль того же времени
    if (have && r != prevR) {
      outT[head] = t;
      outR[head] = r;
      head = (head + 1) % 10;
      if (cnt < 10) cnt++;
    }
    have = true;
    prevR = r;
    prevT = t;
  }
  if (cnt == 10 && head != 0) {
    // Кольцо переполнено — разворачиваем в хронологический порядок.
    uint32_t tt[10];
    uint8_t rr[10];
    for (int k = 0; k < 10; k++) {
      tt[k] = outT[(head + k) % 10];
      rr[k] = outR[(head + k) % 10];
    }
    memcpy(outT, tt, sizeof(tt));
    memcpy(outR, rr, sizeof(rr));
  }
  if (msOut) *msOut = millis() - t0;
  return cnt;
}

static void buildEventsJson(const uint32_t* ts, const uint8_t* rs, int n, unsigned long ms) {
  String out;
  out.reserve(900);
  out = "{\"events\":[";
  for (int k = n - 1; k >= 0; k--) {
    time_t tt = (time_t)ts[k];
    struct tm et;
    gmtime_r(&tt, &et);
    char b[80];
    snprintf(b, sizeof(b),
             "%s{\"t\":\"%04d-%02d-%02d %02d:%02d:%02d\",\"r\":%d}",
             (k == n - 1) ? "" : ",",
             et.tm_year + 1900, et.tm_mon + 1, et.tm_mday,
             et.tm_hour, et.tm_min, et.tm_sec, (int)rs[k]);
    out += b;
  }
  out += "],\"ms\":";
  out += String(ms);
  out += '}';
  sEvJson = out;
}

static bool eventsFresh(size_t szNow) {
  if (sEvJson.length() == 0) return false;
  if (millis() - sEvAt > 120000UL) return false;
  if (szNow < sEvFileSize || szNow - sEvFileSize > 65536UL) return false;
  return true;
}

// ---------------------------------------------------------------------------
//  HTML-страница
// ---------------------------------------------------------------------------
static size_t csvFileSize() {
  File f = SD.open("/energy.csv", FILE_READ);
  if (!f) return 0;
  size_t sz = f.size();
  f.close();
  return sz;
}

// ---------------------------------------------------------------- страница
// ЗАЧЕМ ПОТОКОВАЯ СБОРКА (02.11.2026). Раньше страница собиралась в String:
// копия шаблона (30 КБ) + 22 × String::replace = 86 мс и 30 КБ кучи на каждый
// запрос. Эти 30 КБ отнимали память у mbedTLS, и кэш приходилось отдавать
// куче перед каждым TLS-коннектом к облаку.
// Теперь значения подстановок лежат в маленьком массиве (22 × ~16 байт),
// длина ответа считается заранее по шаблону, а сама страница пишется в сокет
// ОДНИМ проходом по PAGE_TMPL (PROGMEM) кусками по 4 КБ. Памяти — сотни байт,
// сборка ~5 мс, кэш не нужен вовсе.
//
// ПОЧЕМУ БЫЛО 19 СЕКУНД (исправлено 02.11.2026): в /data было blk=web,
// blkMs=18 953, loopMaxAll=19 174. Сборка тут не виновата — 86 мс это 0.3 %
// от 19 с. Виноват цикл отдачи: если клиент перестал читать (закрыли вкладку,
// моргнул Wi-Fi), c.write() возвращает 0, а c.connected() ещё true — цикл ждал
// страховку 20 000 мс, держа loop(). Теперь отдача ограничена бюджетом
// PAGE_SEND_BUDGET и сторожем «нет прогресса» PAGE_SEND_STALL: проход loop()
// не стоит дольше ~4 с ни при каком клиенте (было 20 с).

#define PAGE_SEND_BUDGET 4000UL   // вся отдача — не дольше 4 с
#define PAGE_SEND_STALL   800UL   // нет прогресса 0.8 с — клиент мёртв, рвём
#define PAGE_CHUNK       4096     // мельче — больше задержек на TCP
#define PAGE_MAX_TAGS      22     // столько подстановок в web/page.html
#define PAGE_CAP        56000     // предел ответа: страница выросла (качество, защита)

struct PgVal {
  const char* tag;      // "__FW__" и т.п.
  uint8_t     n;        // длина значения
  char        v[40];    // само значение
};
static PgVal  sPg[PAGE_MAX_TAGS];
static int    sPgN = 0;
static unsigned long sPageBuildN  = 0;   // сколько раз собирали страницу
static unsigned long sPageBuildMs = 0;   // сколько заняла последняя сборка
static unsigned long sPageSendMs  = 0;   // сколько заняла последняя отдача
static unsigned long sPageCutN    = 0;   // сколько отдач оборвано по бюджету
static unsigned long sPageAt      = 0;   // когда страницу отдавали в последний раз
static size_t        sPageBytes   = 0;   // сколько байт ушло клиенту

static void pgAdd(const char* tag, const char* val) {
  if (sPgN >= PAGE_MAX_TAGS) return;
  size_t n = val ? strlen(val) : 0;
  if (n > sizeof(sPg[0].v) - 1) n = sizeof(sPg[0].v) - 1;
  sPg[sPgN].tag = tag;
  sPg[sPgN].n = (uint8_t)n;
  if (n) memcpy(sPg[sPgN].v, val, n);
  sPg[sPgN].v[n] = 0;
  sPgN++;
}
static void pgAddI(const char* tag, const char* fmt, long v) {
  char b[48];
  snprintf(b, sizeof(b), fmt, v);
  pgAdd(tag, b);
}
static void pgAddD(const char* tag, const char* fmt, double v) {
  char b[48];
  snprintf(b, sizeof(b), fmt, v);
  pgAdd(tag, b);
}

// Значения всех подстановок. Порядок не важен — поиск идёт по тегам.
static void pgFill() {
  sPgN = 0;
  pgAdd("__FW__", FW_VER);
  pgAdd("__NETCLS__", gNetOk ? "ok" : "bad");
  pgAdd("__NETTXT__", gNetOk ? "сеть ok" : "нет сети");
  pgAddI("__RSSI__", "%d", (long)gRssi);
  pgAddD("__TKWH__", "%.2f", (double)myTodayKwh);
  pgAddD("__TCOST__", "%.2f", (double)myTodayCost);
  pgAddD("__MV__", "%.0f", (double)gVoltage);
  pgAddD("__MP__", "%.0f", (double)gPower);
  pgAddD("__MA__", "%.2f", (double)gCurrent);
  pgAddD("__MT__", "%.1f", (double)gTemp);
  pgAdd("__RELCLS__", gRelay ? "ok" : "bad");
  pgAdd("__RELTXT__", gRelay ? "ВКЛ" : "ВЫКЛ");
  pgAddD("__TAR__", "%.2f", (double)measuredTariff());
  pgAddD("__PE__", "%.2f", (double)gEnergy);
  pgAddD("__YKWH__", "%.2f", (double)yKwh);      // было String(yKwh, 2)
  pgAddD("__YKOST__", "%.2f", (double)yCost);
  pgAddD("__MKWH__", "%.2f", (double)mKwh);
  pgAddD("__MCOST__", "%.2f", (double)mCost);
  pgAddD("__TARDAY__", "%.2f", (double)TARIFF_DAY);
  pgAddD("__TARNIGHT__", "%.2f", (double)TARIFF_NIGHT);
  char dt[12] = "1970-01-01";
  int hh = 0;
  struct tm ti;
  if (getLocalTime(&ti, 10)) {
    snprintf(dt, sizeof(dt), "%04d-%02d-%02d",
             ti.tm_year + 1900, ti.tm_mon + 1, ti.tm_mday);
    hh = ti.tm_hour;
  }
  pgAdd("__DATE__", dt);
  pgAddI("__HOUR__", "%d", (long)hh);
}

// Ближайший тег в [p,end): возвращает указатель на него и его индекс.
// Проверяем только позиции, где стоит "__", — иначе это был бы полный перебор
// 22 тегов на каждый байт 30-КБ шаблона.
static const char* pgNextTag(const char* p, const char* end, int* idx) {
  while (p + 3 < end) {
    if (p[0] == '_' && p[1] == '_') {
      for (int i = 0; i < sPgN; i++) {
        size_t tl = strlen(sPg[i].tag);
        if ((size_t)(end - p) >= tl && memcmp(p, sPg[i].tag, tl) == 0) {
          *idx = i;
          return p;
        }
      }
    }
    p++;
  }
  return NULL;
}

// Длина готового ответа: шаблон минус теги плюс значения.
static size_t pgOutLen(const char* tmpl, const char* end) {
  size_t len = (size_t)(end - tmpl);
  int idx = 0;
  const char* p = tmpl;
  const char* q;
  while ((q = pgNextTag(p, end, &idx)) != NULL) {
    size_t tl = strlen(sPg[idx].tag);
    len -= tl;
    len += sPg[idx].n;
    p = q + tl;
  }
  return len;
}

// Кусок в сокет — с теми же страховками, что были в pageSend.
// false = клиент ушёл / встал / вышли за бюджет прохода.
static bool pgWrite(WiFiClient& c, const char* p, size_t n,
                    unsigned long t1, unsigned long* progAt) {
  size_t off = 0;
  while (off < n) {
    size_t k = n - off;
    if (k > PAGE_CHUNK) k = PAGE_CHUNK;
    size_t w = c.write((const uint8_t*)p + off, k);
    if (w == 0) {
      if (!c.connected()) return false;                 // клиент ушёл
      if (millis() - *progAt > PAGE_SEND_STALL) return false;  // встал
      delay(1);
      esp_task_wdt_reset();
      continue;
    }
    off += w;
    *progAt = millis();
    esp_task_wdt_reset();
    if (millis() - t1 > PAGE_SEND_BUDGET) return false;  // не держим loop()
  }
  return true;
}

// Кэша страницы больше нет: собираем на лету, память не занимаем.
// Функция оставлена, потому что её зовут cloudCall и /pagefree.
void pageCacheFree() {}

static void pageSend() {
  unsigned long t0 = millis();
  esp_task_wdt_reset();
  pgFill();
  const char* tmpl = PAGE_TMPL;
  const char* end  = tmpl + strlen(PAGE_TMPL);
  size_t total = pgOutLen(tmpl, end);
  sPageBuildMs = millis() - t0;
  sPageBuildN++;
  if (total == 0 || total > PAGE_CAP) {
    Serial.printf("веб: страница не собралась (len=%u), отдаю 503\n", (unsigned)total);
    // 503, а не 200: клиент/мониторинг должен видеть, что страница не готова,
    // и повторить запрос, а не считать заглушку настоящей страницей.
    gWeb.sendHeader("Retry-After", "2");
    gWeb.send(503, "text/html; charset=utf-8",
      "<!DOCTYPE html><meta charset=utf-8><meta http-equiv=refresh content=2>"
      "<p style=\"font:16px system-ui,sans-serif;padding:20px\">"
      "Плата занята, страница обновится сама…</p>");
    return;
  }
  gWeb.setContentLength(total);
  gWeb.sendHeader("Cache-Control", "no-store");
  gWeb.send(200, "text/html; charset=utf-8", "");
  WiFiClient c = gWeb.client();        // копия — тот же сокет, но можно connected()
  // Без TCP_NODELAY каждый чанк ждал подтверждения предыдущего (задержка
  // 40-200 мс), и 30 КБ уходили почти секунду. С ним — десятки миллисекунд.
  c.setNoDelay(true);
  unsigned long t1 = millis();
  unsigned long progAt = t1;
  size_t sent = 0;
  bool cut = false;
  const char* p = tmpl;
  for (;;) {
    int idx = 0;
    const char* q = pgNextTag(p, end, &idx);
    const char* stop = q ? q : end;
    size_t n = (size_t)(stop - p);
    if (n) {
      if (!pgWrite(c, p, n, t1, &progAt)) { cut = true; break; }
      sent += n;
    }
    if (!q) break;
    size_t tl = strlen(sPg[idx].tag);
    if (sPg[idx].n) {
      if (!pgWrite(c, sPg[idx].v, sPg[idx].n, t1, &progAt)) { cut = true; break; }
      sent += sPg[idx].n;
    }
    p = q + tl;
  }
  sPageSendMs = millis() - t1;
  sPageBytes = sent;
  sPageAt = millis();
  if (cut) {
    sPageCutN++;
    Serial.printf("веб: / отдача оборвана на %u из %u байт (%lu мс)\n",
                  (unsigned)sent, (unsigned)total, millis() - t1);
  } else if (sent != total) {
    Serial.printf("веб: / недодано %u из %u байт\n", (unsigned)sent, (unsigned)total);
  }
}

// ============================================================================
//  PWRHIST — дозаполнение колонки мощности в /energy.csv из облака Tuya
// ============================================================================
// Зачем: до исправления gPower локальный DP 6 отдавал устаревшие 19 Вт, и в
// истории за 23–29.10 мощность врёт. Энергия (4-е поле) при этом верная — её
// не трогаем. Для каждого часа берём phase_a тремя окнами по 20 минут (как
// pwrFillTick), считаем среднее и пик, затем ОДИН раз переписываем файл:
//   /energy.csv -> /energy.bak (проверенная копия)
//   /energy.csv + правки -> /energy.tmp
//   /energy.csv -> /energy.old, /energy.tmp -> /energy.csv, old удаляем.
// При любой ошибке /energy.csv остаётся прежним, /energy.bak — целым.
// Пики часов дополнительно пишем в /energy.peak: после замены мощности на
// среднюю /history иначе потерял бы реальный пик часа.
#define PH_MAX_DAYS 8

struct PwrHistRun {
  bool      active;
  int       days;                 // сколько дней в заказе
  int       day;                  // текущий день 0..days-1
  int       hour;                 // текущий час 0..23
  int       part;                 // окно часа 0..2
  int       retry;                // подряд неудачных запросов
  time_t    dayEpoch[PH_MAX_DAYS];// местная полночь дня
  char      dayStr[PH_MAX_DAYS][11];  // "YYYY-MM-DD"
  float     avg[PH_MAX_DAYS][24];
  uint16_t  peak[PH_MAX_DAYS][24];
  uint8_t   peakMin[PH_MAX_DAYS][24];   // минута пика внутри часа (0..59)
  uint8_t   peakDur[PH_MAX_DAYS][24];   // сколько минут было выше 80 % пика
  uint16_t  rows[PH_MAX_DAYS][24];   // сколько строк этого часа есть в CSV
  uint8_t   ok[PH_MAX_DAYS][24];
  float     sum;
  int       n;
  int       peakCur;
  uint16_t  mmin[60];             // максимум мощности по минутам текущего часа
  int       hoursOk;
  int       hoursMissing;
  unsigned long nextMs;           // пауза между запросами к report-logs
  unsigned long started;
  char      msg[80];              // последний итог/ошибка (для /pwrhist/status)
};
static PwrHistRun sPH;

bool pwrHistActive() { return sPH.active; }

// Один проход по CSV: сколько строк у каждого (день,час). Часы без строк
// пропускаем — незачем 72 раза ходить в облако за тем, чего нет в файле.
static int pwrHistCountRows() {
  memset(sPH.rows, 0, sizeof(sPH.rows));
  File f = SD.open("/energy.csv", FILE_READ);
  if (!f) return 0;
  static uint8_t buf[512 + 256];
  int carry = 0;
  while (f.available()) {
    int n = f.read(buf + carry, 512);
    if (n <= 0) break;
    int total = carry + n;
    int i = 0;
    while (i < total) {
      uint8_t* nl = (uint8_t*)memchr(buf + i, '\n', total - i);
      if (!nl) break;
      int len = (int)(nl - (buf + i));
      if (len >= 20 && isDatePrefix((const char*)(buf + i), len)) {
        for (int d = 0; d < sPH.days; d++) {
          if (memcmp(buf + i, sPH.dayStr[d], 10) != 0) continue;
          int hh = (buf[i + 11] - '0') * 10 + (buf[i + 12] - '0');
          if (hh >= 0 && hh < 24 && sPH.rows[d][hh] < 65535) sPH.rows[d][hh]++;
          break;
        }
      }
      i = (int)(nl - buf) + 1;
    }
    carry = total - i;
    if (carry > 0) memmove(buf, buf + i, carry);
    if (carry > 240) carry = 0;
    esp_task_wdt_reset();
  }
  f.close();
  int total = 0;
  for (int d = 0; d < sPH.days; d++)
    for (int h = 0; h < 24; h++) total += sPH.rows[d][h];
  return total;
}

static void pwrHistAbort(const char* why) {
  if (!sPH.active) return;
  sPH.active = false;
  // ГЛАВНОЕ для диагностики: не «не ответило», а ГДЕ именно порвалось и что
  // пришло по HTTP. Без этого PWRHIST падал с непонятной ошибкой.
  snprintf(sPH.msg, sizeof(sPH.msg), "%s: этап %s, HTTP %d, тело %d из %d",
           why, cliStageName(gCliStage), gCliHttp, gCliLen, gCliWant);
  Serial.printf("PWRHIST-ERR: %s; /energy.csv НЕ тронут\n", sPH.msg);
}

// Разбор одного ответа report-logs. Складываем мощность в sPH.sum, пик — в
// sPH.peakCur/peakMin. Формат phase_a: 8 байт, мощность — байты 6..7.
static void pwrHistParse(const String& body, time_t hourStart) {
  int pos = 0;
  int len = body.length();
  while (true) {
    int i = body.indexOf("\"event_time\":", pos);
    if (i < 0) break;
    i += 13;
    while (i < len && (body[i] == ' ' || body[i] == '"')) i++;
    int j = i;
    while (j < len && isDigit(body[j])) j++;
    if (j == i) { pos = i + 1; continue; }
    unsigned long long ms = strtoull(body.substring(i, j).c_str(), NULL, 10);
    pos = j;

    int v = body.indexOf("\"value\":", pos);
    if (v < 0) break;
    v += 8;
    while (v < len && (body[v] == ' ' || body[v] == '"')) v++;
    int e = body.indexOf('"', v);
    if (e < 0) break;
    String b64 = body.substring(v, e);
    pos = e + 1;

    uint8_t raw[12];
    if (b64decode(b64.c_str(), raw, sizeof(raw)) < 8) continue;
    int p = ((uint16_t)raw[6] << 8) | raw[7];
    if (p < 0 || p > 20000) continue;

    sPH.sum += (float)p;
    sPH.n++;
    time_t et = (time_t)(ms / 1000ULL);
    long rel = (long)(et - hourStart);
    if (rel < 0) rel = 0;
    if (rel > 3599) rel = 3599;
    // Копим максимум по минутам: только так после прогона можно посчитать,
    // сколько минут мощность держалась выше 80 % пика (пик-то известен в конце).
    int mi = (int)(rel / 60);
    if (mi >= 0 && mi < 60 && p > (int)sPH.mmin[mi]) sPH.mmin[mi] = (uint16_t)p;
    if (p > sPH.peakCur) sPH.peakCur = p;
    esp_task_wdt_reset();
  }
}

// Копирование файла с проверкой размера. Используется для /energy.bak.
static bool pwrHistCopy(const char* from, const char* to, size_t expect, size_t* got) {
  if (SD.exists(to)) SD.remove(to);
  File s = SD.open(from, FILE_READ);
  if (!s) return false;
  File d = SD.open(to, FILE_WRITE);
  if (!d) { s.close(); return false; }
  uint8_t buf[512];
  size_t all = 0;
  bool ok = true;
  while (s.available()) {
    int n = s.read(buf, sizeof(buf));
    if (n <= 0) break;
    if (d.write(buf, n) != (size_t)n) { ok = false; break; }
    all += (size_t)n;
    esp_task_wdt_reset();
  }
  s.close();
  d.flush();
  d.close();
  if (got) *got = all;
  if (!ok || all != expect) {
    SD.remove(to);
    return false;
  }
  return true;
}

// Одна строка CSV: заменить 3-е поле (мощность) на среднее, если дата/час
// попали в заказ. Остальные поля, включая энергию, копируются байт в байт.
static int pwrHistMakeLine(const char* s, int len, char* out, int cap, bool* changed) {
  *changed = false;
  if (len < 0 || len + 1 > cap) return -1;

  bool hit = false;
  int di = -1;
  int hh = -1;
  if (len >= 20 && isDatePrefix(s, len)) {
    for (int i = 0; i < sPH.days; i++) {
      if (memcmp(s, sPH.dayStr[i], 10) == 0) { di = i; break; }
    }
    if (di >= 0 && s[10] == ' ' && s[13] == ':') {
      hh = (s[11] - '0') * 10 + (s[12] - '0');
      if (hh >= 0 && hh < 24 && sPH.ok[di][hh]) hit = true;
    }
  }

  const char* c1 = NULL;
  const char* c2 = NULL;
  const char* c3 = NULL;
  if (hit) {
    c1 = (const char*)memchr(s, ',', len);
    if (c1) c2 = (const char*)memchr(c1 + 1, ',', len - (int)(c1 + 1 - s));
    if (c2) c3 = (const char*)memchr(c2 + 1, ',', len - (int)(c2 + 1 - s));
    if (!c1 || !c2 || !c3) hit = false;
  }

  if (!hit) {
    memcpy(out, s, len);
    out[len] = '\n';
    return len + 1;
  }

  int prefix = (int)(c2 - s) + 1;          // "...дата,В,"
  char num[16];
  int nl = snprintf(num, sizeof(num), "%.0f", sPH.avg[di][hh]);
  int rest = len - (int)(c3 - s);          // ",энергия,реле,рубль,темп"
  int ol = prefix + nl + rest;
  if (ol + 1 > cap) return -1;
  memcpy(out, s, prefix);
  memcpy(out + prefix, num, nl);
  memcpy(out + prefix + nl, c3, rest);
  out[ol++] = '\n';
  *changed = true;
  return ol;
}

// Перезапись /energy.csv -> /energy.tmp. Возвращает число строк и замен.
static bool pwrHistRewrite(size_t expected, size_t* changedOut, size_t* linesOut, size_t* tmpSizeOut) {
  File src = SD.open("/energy.csv", FILE_READ);
  if (!src) return false;
  File dst = SD.open("/energy.tmp", FILE_WRITE);
  if (!dst) { src.close(); return false; }

  static uint8_t buf[512 + 256];
  char out[400];
  int carry = 0;
  size_t lines = 0, changed = 0, readTotal = 0;
  bool ok = true;

  while (src.available() && ok) {
    int n = src.read(buf + carry, 512);
    if (n <= 0) break;
    readTotal += (size_t)n;
    int total = carry + n;
    int i = 0;
    while (i < total) {
      uint8_t* nl = (uint8_t*)memchr(buf + i, '\n', total - i);
      if (!nl) break;
      int len = (int)(nl - (buf + i));
      if (len > 0 && buf[i + len - 1] == '\r') len--;
      bool ch = false;
      int ol = pwrHistMakeLine((const char*)(buf + i), len, out, sizeof(out), &ch);
      if (ol < 0 || dst.write((uint8_t*)out, ol) != (size_t)ol) { ok = false; break; }
      if (ch) changed++;
      lines++;
      i = (int)(nl - buf) + 1;
      if ((lines & 127) == 0) { esp_task_wdt_reset(); yield(); }
    }
    if (!ok) break;
    carry = total - i;
    if (carry > 0) memmove(buf, buf + i, carry);
    if (carry > 240) { ok = false; break; }     // строка длиннее буфера
    esp_task_wdt_reset();
  }

  // Последняя строка без перевода строки.
  if (ok && carry > 0) {
    int len = carry;
    if (len > 0 && buf[len - 1] == '\r') len--;
    bool ch = false;
    int ol = pwrHistMakeLine((const char*)buf, len, out, sizeof(out), &ch);
    if (ol < 0 || dst.write((uint8_t*)out, ol) != (size_t)ol) ok = false;
    else { if (ch) changed++; lines++; }
  }

  // Если карта отдала меньше байт, чем размер файла, — не подменяем оригинал.
  if (readTotal != expected) ok = false;

  dst.flush();
  dst.close();
  src.close();

  if (!ok || lines == 0) {
    SD.remove("/energy.tmp");
    return false;
  }

  File t = SD.open("/energy.tmp", FILE_READ);
  size_t ts = t ? t.size() : 0;
  t.close();
  if (ts == 0) { SD.remove("/energy.tmp"); return false; }

  *changedOut = changed;
  *linesOut = lines;
  *tmpSizeOut = ts;
  return true;
}

// Формат строки: дата,час,пик,минута_пика,минут_выше_80%
// Минута и длительность нужны веб-странице (/history: pmaxT и pmaxDur):
// после PWRHIST колонка мощности в CSV равна средней за час, и восстановить
// по ней время пика уже нельзя.
static void pwrHistWritePeaks() {
  File f = SD.open("/energy.peak", FILE_APPEND);
  if (!f) {
    Serial.println("PWRHIST: /energy.peak не открылся — пики не сохранены");
    return;
  }
  int n = 0;
  for (int d = 0; d < sPH.days; d++)
    for (int h = 0; h < 24; h++)
      if (sPH.ok[d][h]) {
        f.printf("%s,%02d,%u,%u,%u\n", sPH.dayStr[d], h, (unsigned)sPH.peak[d][h],
                 (unsigned)sPH.peakMin[d][h], (unsigned)sPH.peakDur[d][h]);
        n++;
      }
  f.close();
  Serial.printf("PWRHIST: пики часов дописаны в /energy.peak (%d)\n", n);
}

// Финальная стадия: бэкап, перезапись, подмена файла, пересчёт статистики.
// Вызывается из pwrHistTick() один раз, когда все часы собраны.
static void pwrHistCommit() {
  int have = 0;
  for (int d = 0; d < sPH.days; d++)
    for (int h = 0; h < 24; h++) if (sPH.ok[d][h]) have++;
  Serial.printf("PWRHIST: собрано часов %d, пропущено %d\n", have, sPH.hoursMissing);
  if (have == 0) {
    Serial.println("PWRHIST: в облаке нет данных за эти дни — /energy.csv не трогаю");
    return;
  }
  if (!gSdOk) {
    Serial.println("PWRHIST-ERR: карта недоступна — /energy.csv не трогаю");
    return;
  }

  // Хвосты прошлого запуска: если .old остался, а .csv нет — вернуть .old.
  if (SD.exists("/energy.old") && !SD.exists("/energy.csv"))
    SD.rename("/energy.old", gGapPath);
  if (SD.exists("/energy.old")) SD.remove("/energy.old");
  if (SD.exists("/energy.tmp")) SD.remove("/energy.tmp");

  size_t sz = csvFileSize();
  if (sz < 20) {
    Serial.printf("PWRHIST-ERR: /energy.csv пуст или недоступен (размер %u) — не трогаю\n",
                  (unsigned)sz);
    return;
  }

  // Место: оригинал + .bak + .tmp (в момент подмены ещё и .old).
  uint64_t total = SD.totalBytes();
  uint64_t used  = SD.usedBytes();
  uint64_t freeB = (total > used) ? (uint64_t)(total - used) : 0;
  uint64_t need  = (uint64_t)sz * 4ULL + 65536ULL;  // .bak.tmp + старый .bak + .tmp
  if (total > 0) {
    Serial.printf("PWRHIST: место на карте: свободно %llu, нужно %llu\n",
                  (unsigned long long)freeB, (unsigned long long)need);
    if (freeB < need) {
      Serial.println("PWRHIST-ERR: МАЛО МЕСТА для .bak и .tmp — /energy.csv не трогаю");
      return;
    }
  }

  // Новую копию сначала пишем в .bak.tmp: старый .bak не трогаем, пока новая
  // копия не проверена по размеру.
  if (SD.exists("/energy.bak.tmp")) SD.remove("/energy.bak.tmp");
  Serial.printf("PWRHIST: делаю резервную копию /energy.bak (%u байт)\n", (unsigned)sz);
  size_t copied = 0;
  if (!pwrHistCopy("/energy.csv", "/energy.bak.tmp", sz, &copied)) {
    Serial.printf("PWRHIST-ERR: копия не удалась (записано %u из %u) — /energy.csv не трогаю\n",
                  (unsigned)copied, (unsigned)sz);
    return;
  }
  File b = SD.open("/energy.bak.tmp", FILE_READ);
  size_t bs = b ? b.size() : 0;
  b.close();
  if (bs != sz) {
    SD.remove("/energy.bak.tmp");
    Serial.printf("PWRHIST-ERR: размер копии %u != %u — /energy.csv не трогаю\n",
                  (unsigned)bs, (unsigned)sz);
    return;
  }
  if (SD.exists("/energy.bak.old")) SD.remove("/energy.bak.old");
  bool oldMoved = true;
  if (SD.exists("/energy.bak")) oldMoved = SD.rename("/energy.bak", "/energy.bak.old");
  if (!oldMoved) {
    SD.remove("/energy.bak.tmp");
    Serial.println("PWRHIST-ERR: не смог отодвинуть старый /energy.bak — /energy.csv не трогаю");
    return;
  }
  if (!SD.rename("/energy.bak.tmp", "/energy.bak")) {
    if (SD.exists("/energy.bak.old")) SD.rename("/energy.bak.old", "/energy.bak");
    Serial.println("PWRHIST-ERR: не смог поставить /energy.bak — /energy.csv не трогаю");
    return;
  }
  if (SD.exists("/energy.bak.old")) SD.remove("/energy.bak.old");
  Serial.println("PWRHIST: копия проверена, переписываю строки");

  size_t changed = 0, lines = 0, tmpSz = 0;
  if (!pwrHistRewrite(sz, &changed, &lines, &tmpSz)) {
    Serial.println("PWRHIST-ERR: перезапись не удалась — /energy.csv не тронут, /energy.bak цел");
    return;
  }
  Serial.printf("PWRHIST: строк %u, заменено %u, /energy.tmp %u байт\n",
                (unsigned)lines, (unsigned)changed, (unsigned)tmpSz);
  if (changed == 0) {
    SD.remove("/energy.tmp");
    Serial.println("PWRHIST: ни одна строка не попала в диапазон — /energy.csv не трогаю");
    return;
  }

  // Подмена: сначала оригинал -> .old (он остаётся на диске), затем .tmp ->
  // /energy.csv. Если второй шаг не удался, возвращаем .old на место.
  if (!SD.rename("/energy.csv", "/energy.old")) {
    SD.remove("/energy.tmp");
    Serial.println("PWRHIST-ERR: не смог переименовать /energy.csv — файл не тронут");
    return;
  }
  if (!SD.rename("/energy.tmp", "/energy.csv")) {
    SD.rename("/energy.old", gGapPath);
    SD.remove("/energy.tmp");
    Serial.println("PWRHIST-ERR: не смог поставить новый файл — вернул оригинал из .old");
    return;
  }
  if (SD.exists("/energy.old")) SD.remove("/energy.old");

  File chk = SD.open("/energy.csv", FILE_READ);
  size_t newSz = chk ? chk.size() : 0;
  chk.close();
  if (newSz != tmpSz) {
    Serial.printf("PWRHIST-ERR: размер после подмены %u != %u; оригинал в /energy.bak\n",
                  (unsigned)newSz, (unsigned)tmpSz);
  } else {
    Serial.printf("PWRHIST: /energy.csv обновлён, %u байт (было %u)\n",
                  (unsigned)newSz, (unsigned)sz);
  }

  pwrHistWritePeaks();

  // Кэши карты и статистика — заново, иначе /days и экран покажут старое.
  sDaysFileSize = 0; sDaysAt = 0; histCacheDrop(); gSdGen++;
  sEvFileSize = 0;   sEvAt = 0;
  gDayLoaded = -1;
  resetStats();
  statsSeeded = false;
  seedStatsFromSd();
  if (gDayOffset > 0) dayLoadFromSd(gDayOffset);   // выбранный прошлый день — заново
  gLastDraw = 0; gLastWinFull = 0; gLastSig = 0;
  snprintf(sPH.msg, sizeof(sPH.msg), "готово: часов ok %d, пусто %d",
           sPH.hoursOk, sPH.hoursMissing);
  Serial.printf("PWRHIST-DONE: мощность дозаполнена; бэкап /energy.bak (%u байт)\n",
                (unsigned)sz);
}

// Один шаг автомата за проход loop(): один HTTP-запрос к облаку. Так тач и
// веб остаются живыми, а 72 запроса на день не превращаются в одну паузу.
void pwrHistTick() {
  CloudBgScope bg;                               // фон: без второй попытки connect
  if (!sPH.active) return;
  if (netBudgetLeft() < 1800UL) return;          // бюджет прохода уже занят облаком
  // Tuya ограничивает частоту report-logs («The log query is too frequent»).
  // Держим не меньше 1,3 с между запросами, иначе 72 запроса в минуту
  // упираются в лимит и часть часов остаётся пустой.
  if (millis() < sPH.nextMs) return;
  if (WiFi.status() != WL_CONNECTED) { pwrHistAbort("Wi-Fi не подключён"); return; }
  // netOnline() здесь НЕ проверяем как запрет: зонд TCP:53 иногда врёт, и
  // тогда PWRHIST падал «нет интернета» при живом облаке.
  if (millis() - gTouchMs < 1500UL) return;      // человек работает с экраном
  if (millis() < gTouchSkip) return;

  if (sPH.day >= sPH.days) {
    sPH.active = false;
    pwrHistCommit();
    return;
  }

  time_t nowE = time(nullptr);
  time_t hs = sPH.dayEpoch[sPH.day] + (time_t)sPH.hour * 3600;
  if (hs + 3600 > nowE) {
    // Час ещё не прожит (или заказ включает сегодня) — среднее будет неполным.
    if (sPH.part == 0) {
      sPH.ok[sPH.day][sPH.hour] = 0;
      sPH.hoursMissing++;
      Serial.printf("PWRHIST: %s час %02d — ещё не прожит, пропуск\n",
                    sPH.dayStr[sPH.day], sPH.hour);
    }
    sPH.hour++; sPH.part = 0; sPH.retry = 0;
    if (sPH.hour >= 24) { sPH.hour = 0; sPH.day++; }
    return;
  }

  if (sPH.rows[sPH.day][sPH.hour] == 0) {
    // В CSV нет ни одной строки этого часа — заменять нечего, облако не зовём.
    sPH.ok[sPH.day][sPH.hour] = 0;
    sPH.hoursMissing++;
    Serial.printf("PWRHIST: %s час %02d — нет строк в CSV, пропуск\n",
                  sPH.dayStr[sPH.day], sPH.hour);
    sPH.hour++; sPH.part = 0; sPH.retry = 0;
    if (sPH.hour >= 24) { sPH.hour = 0; sPH.day++; }
    return;
  }

  if (!cloudToken()) { pwrHistAbort("облако не дало токен"); return; }
  // Токен мог занять проход — окно переносим на следующий loop().
  if (netBudgetLeft() < 1500UL) return;
  if (sPH.part == 0) { sPH.sum = 0; sPH.n = 0; sPH.peakCur = 0; memset(sPH.mmin, 0, sizeof(sPH.mmin)); }

  time_t a = hs + (time_t)sPH.part * 1200;
  time_t b = a + 1200;
  char path[240];
  snprintf(path, sizeof(path),
           "/v1.0/devices/%s/report-logs?codes=phase_a&end_time=%llu&size=100&start_time=%llu",
           DEVICE_ID, (unsigned long long)b * 1000ULL, (unsigned long long)a * 1000ULL);

  String body;
  if (repWait()) return;                          // общий лимит report-logs
  bool okPh = cloudCall("GET", String(path), body, 12000);
  repDone();
  if (!okPh) {
    // Облако изредка не отвечает одним запросом из двадцати (этап connect —
    // видно в /pwrhist/status). Три отказа подряд не повод бросать прогон
    // на 24 часа: ждём дольше и пробуем ещё.
    sPH.retry++;
    if (sPH.retry >= 6) { pwrHistAbort("облако не ответило 6 раз подряд"); return; }
    sPH.nextMs = millis() + (sPH.retry <= 3 ? 2500UL : 10000UL);
    return;
  }
  if (body.indexOf("\"success\":true") < 0) {
    // Лимит частоты — не ошибка данных: ждём и повторяем этот же запрос.
    if (body.indexOf("too frequent") >= 0) {
      sPH.retry = 0;
      sPH.nextMs = millis() + 10000UL;
      return;
    }
    sPH.nextMs = millis() + 3000UL;
    if (++sPH.retry >= 3) pwrHistAbort("облако вернуло success=false");
    return;
  }
  sPH.retry = 0;
  sPH.nextMs = millis() + 1300UL;
  pwrHistParse(body, hs);

  if (++sPH.part < 3) return;

  int d = sPH.day, h = sPH.hour;
  int dd = (sPH.dayStr[d][8] - '0') * 10 + (sPH.dayStr[d][9] - '0');
  int mm = (sPH.dayStr[d][5] - '0') * 10 + (sPH.dayStr[d][6] - '0');
  if (sPH.n > 0) {
    float avg = sPH.sum / (float)sPH.n;
    sPH.avg[d][h] = avg;
    sPH.peak[d][h] = (uint16_t)sPH.peakCur;
    // Минута пика и число минут выше 80 % пика — из минутных максимумов часа.
    int pk = 0, pm = 0;
    for (int m = 0; m < 60; m++)
      if (sPH.mmin[m] > pk) { pk = (int)sPH.mmin[m]; pm = m; }
    int dur = 0;
    if (pk > 0) {
      float lim = (float)pk * 0.8f;
      for (int m = 0; m < 60; m++)
        if ((float)sPH.mmin[m] > lim) dur++;
    }
    sPH.peakMin[d][h] = (uint8_t)pm;
    sPH.peakDur[d][h] = (uint8_t)(dur > 255 ? 255 : dur);
    sPH.ok[d][h] = 1;
    sPH.hoursOk++;
    Serial.printf("PWRHIST: %02d.%02d час %02d = %.0f Вт, пик %d Вт в %02d:%02d, >80%% %d мин (%d точек)\n",
                  dd, mm, h, avg, sPH.peakCur, h, pm, dur, sPH.n);
  } else {
    sPH.ok[d][h] = 0;
    sPH.hoursMissing++;
    Serial.printf("PWRHIST: %02d.%02d час %02d — облако пусто\n", dd, mm, h);
  }

  sPH.part = 0;
  sPH.hour++;
  if (sPH.hour >= 24) { sPH.hour = 0; sPH.day++; }
  esp_task_wdt_reset();
}

// Разбор аргументов:
//   PWRHIST                         — последние 7 дней (сегодня-7 .. сегодня-1)
//   PWRHIST 2026-10-23 2026-10-29   — диапазон дат включительно
//   PWRHIST 23 29                   — числа текущего месяца
//   PWRHIST 29                      — один день текущего месяца
bool pwrHistStart(const String& args) {
  if (sPH.active) { Serial.println("PWRHIST: уже выполняется"); return false; }
  if (!gSdOk) { Serial.println("PWRHIST-ERR: нет карты"); return false; }
  long todayKey = todayDayKey();
  if (todayKey <= 0) {
    Serial.println("PWRHIST-ERR: часы не синхронизированы");
    return false;
  }

  String a = args;
  a.trim();
  struct tm ti;
  bool haveNow = getLocalTime(&ti, 10);
  int cy = haveNow ? ti.tm_year + 1900 : 2026;
  int cm = haveNow ? ti.tm_mon + 1 : 1;
  int y1 = 0, m1 = 0, d1 = 0, y2 = 0, m2 = 0, d2 = 0;

  int n6 = sscanf(a.c_str(), "%d-%d-%d %d-%d-%d", &y1, &m1, &d1, &y2, &m2, &d2);
  if (a.length() == 0) {
    long k = todayKey;
    int yy, mm, dd;
    civilFromDays(k - 7, &yy, &mm, &dd); y1 = yy; m1 = mm; d1 = dd;
    civilFromDays(k - 1, &yy, &mm, &dd); y2 = yy; m2 = mm; d2 = dd;
  } else if (n6 == 6) {
    // полные даты уже разобраны
  } else if (n6 == 1 || n6 == 2) {
    int dd1 = 0, dd2 = 0;
    int n2 = sscanf(a.c_str(), "%d %d", &dd1, &dd2);
    if (n2 == 2)      { y1 = y2 = cy; m1 = m2 = cm; d1 = dd1; d2 = dd2; }
    else if (n2 == 1) { y1 = y2 = cy; m1 = m2 = cm; d1 = d2 = dd1; }
    else {
      Serial.println("PWRHIST: формат: PWRHIST [ГГГГ-ММ-ДД ГГГГ-ММ-ДД | ДД ДД]");
      return false;
    }
  } else {
    Serial.println("PWRHIST: формат: PWRHIST [ГГГГ-ММ-ДД ГГГГ-ММ-ДД | ДД ДД]");
    return false;
  }

  if (y1 < 2020 || y1 > 2100 || m1 < 1 || m1 > 12 || d1 < 1 || d1 > 31 ||
      y2 < 2020 || y2 > 2100 || m2 < 1 || m2 > 12 || d2 < 1 || d2 > 31) {
    Serial.println("PWRHIST-ERR: неверная дата");
    return false;
  }

  long k1 = daysFromCivil(y1, (unsigned)m1, (unsigned)d1);
  long k2 = daysFromCivil(y2, (unsigned)m2, (unsigned)d2);
  if (k1 > k2) { long t = k1; k1 = k2; k2 = t; }
  long today = todayKey;
  if (k2 > today) {
    Serial.println("PWRHIST-ERR: конец диапазона в будущем");
    return false;
  }
  int days = (int)(k2 - k1 + 1);
  if (days < 1 || days > PH_MAX_DAYS) {
    Serial.printf("PWRHIST-ERR: дней %d, а можно 1..%d\n", days, PH_MAX_DAYS);
    return false;
  }

  memset(&sPH, 0, sizeof(sPH));
  sPH.days = days;
  for (int i = 0; i < days; i++) {
    int yy, mm, dd;
    civilFromDays(k1 + i, &yy, &mm, &dd);
    snprintf(sPH.dayStr[i], sizeof(sPH.dayStr[i]), "%04d-%02d-%02d", yy, mm, dd);
    struct tm t;
    memset(&t, 0, sizeof(t));
    t.tm_year = yy - 1900; t.tm_mon = mm - 1; t.tm_mday = dd;
    t.tm_hour = 0; t.tm_min = 0; t.tm_sec = 0; t.tm_isdst = -1;
    time_t e = mktime(&t);
    if (e < 1600000000) {
      Serial.println("PWRHIST-ERR: не смог посчитать время дня");
      return false;
    }
    sPH.dayEpoch[i] = e;
  }

  int rowsTotal = pwrHistCountRows();
  if (rowsTotal == 0) {
    Serial.println("PWRHIST: в /energy.csv нет строк за эти дни — нечего заполнять");
    return false;
  }
  Serial.printf("PWRHIST: строк в диапазоне %d\n", rowsTotal);

  sPH.active = true;
  sPH.day = 0; sPH.hour = 0; sPH.part = 0; sPH.retry = 0;
  sPH.started = millis();
  strcpy(sPH.msg, "идёт");
  Serial.printf("PWRHIST: %s..%s, дней %d, часов %d; облако Tuya\n",
                sPH.dayStr[0], sPH.dayStr[days - 1], days, days * 24);
  if (k1 < today - 7)
    Serial.println("PWRHIST: внимание, часть дней старше 7 суток — облако может не отдать");
  Serial.println("PWRHIST: идёт в фоне, прогресс в serial; файл трогаю только в конце");
  return true;
}

// ============================================================================
//  GAPFILL — заполнение пропусков в /energy.csv данными из облака Tuya
// ============================================================================
// Зачем: плата перезагружается при прошивке, уходит в бутлоадер, теряет Wi-Fi —
// и в /energy.csv появляются дыры в несколько минут. В дыре нет ни напряжения,
// ни мощности, ни температуры, поэтому МИН/МАКС напряжения за сутки и ПИК
// мощности могут быть неверными. Энергия при этом НЕ теряется: счётчик
// непрерывный, расход считается по разнице концов.
//
// Схема (как PWRHIST, чтобы не изобретать второй способ порчи файла):
//   1. проход по /energy.csv блоками 512 байт -> список пропусков > 120 с;
//   2. на каждый пропуск — облако (phase_a + temp_current + счётчик) окнами
//      по 20 минут, по одному HTTP-запросу за проход loop();
//   3. строки копятся в пуле, вставляются ОДНИМ проходом перезаписи:
//      /energy.csv -> /energy.bak.tmp (проверка) -> /energy.bak
//      /energy.csv + вставки -> /energy.tmp
//      /energy.csv -> /energy.old, /energy.tmp -> /energy.csv, .old удаляем.
// При любой ошибке /energy.csv остаётся прежним, /energy.bak — целым.
// Пределы, типы и состояние (sGF/sGf/sGfRow) объявлены в начале файла —
// Arduino строит прототипы функций до этого места.

bool gapFillActive() { return sGF.phase != GF_IDLE; }
int  gapTodayCount() { return sGF.todayN; }
int  gapTodayMinutes() { return sGF.todayMin; }

// ============================================================================
//  MQTT + АВТООБНАРУЖЕНИЕ HOME ASSISTANT   (CYD-4.4, добавлено 01.11.2026)
//
//  Где живёт: отдельный тик mqttTick() в loop() сразу после cloudTick().
//  Облако Tuya (cloudTick) и MQTT друг о друге не знают — MQTT-команды
//  реле идут своим путём (tuyaRelayOn), состояние берётся из тех же
//  переменных, что отдаёт /data.
//
//  Правила, которые нельзя нарушать (плата и так работает стабильно):
//    • MQTT_HOST пустой  ->  фича выключена, ни одного сетевого вызова;
//    • подключение НЕ блокирует цикл: TCP с таймаутом 2 с, ожидание CONNACK
//      3 с (MQTT_SOCKET_TIMEOUT), повтор не чаще раза в 30 с;
//    • esp_task_wdt_reset() перед/после каждого блокирующего шага;
//    • relay_off НЕ реализован намеренно: удалённое выключение реле
//      обесточило бы дом. Ответ — off_blocked в <base>/result.
//    • discovery-конфиги (13 шт.) уходят по одному за проход, не пачкой:
//      иначе разом съедается несколько КБ буфера.
// ============================================================================

#ifndef MQTT_HOST
#define MQTT_HOST   ""
#endif
#ifndef MQTT_PORT
#define MQTT_PORT   1883
#endif
#ifndef MQTT_USER
#define MQTT_USER   ""
#endif
#ifndef MQTT_PASS
#define MQTT_PASS   ""
#endif
#ifndef MQTT_BASE
#define MQTT_BASE   "dom_energy"
#endif

// Таймаут сокета у PubSubClient по умолчанию 15 с — это заметный затык в
// loop() при мёртвом брокере. 3 с хватает с запасом: брокер в локальной сети
// отвечает за десятки миллисекунд.
#define MQTT_SOCKET_TIMEOUT 3
#include <PubSubClient.h>



#define MQ_HA_PREFIX  "homeassistant"
#define MQ_STATE_MS   30000UL    // публикация состояния: раз в 30 с
#define MQ_MIN_MS      5000UL    // при изменении — но не чаще раза в 5 с
#define MQ_RETRY_MS   30000UL    // повтор подключения после отказа
#define MQ_DISC_MS  1800000UL    // переотправка discovery раз в 30 мин
                                 // (брокер мог перезапуститься без сохранения)
#define MQ_CFG_NS     "mqtt"     // раздел NVS для настроек
#define MQ_DFLT_PORT  1883

// ---- настройки (по умолчанию — из secrets.h, поверх — из NVS) --------------
static char     mqHost[40] = "";
static char     mqUser[40] = "";
static char     mqPass[68] = "";
static char     mqBase[24] = MQTT_BASE;
static uint16_t mqPort     = MQTT_PORT;
static bool     mqCfgLoaded = false;

// ---- сеть и счётчики ------------------------------------------------------
static WiFiClient   mqNet;
static PubSubClient mqCli(mqNet);

static bool          mqUp   = false;   // подключены к брокеру
static unsigned long mqMsgN = 0;       // принято сообщений (команд)
static unsigned long mqFailN = 0;      // отказов подключения
static unsigned long mqPubN = 0;       // опубликовано сообщений
static unsigned long mqOkN  = 0;       // успешных подключений
static unsigned long mqLastTry = 0;    // когда последний раз пробовали
static unsigned long mqLastPub = 0;    // когда последний раз публиковали
static unsigned long mqLastOkMs = 0;   // когда последняя публикация удалась
static unsigned long mqLastDisc = 0;   // когда начали рассылку discovery
static int           mqDiscIdx = 0;    // сколько конфигов уже отправлено
static bool          mqForcePub = false;   // publish принудительно (refresh)
static char          mqErr[28] = "";   // последняя ошибка, для /mqtt
static char          mqLastTop[72] = "";   // последний топик
static char          mqLastState[512] = ""; // последний опубликованный JSON

// Прототипы: тела ниже, а вызываются из mqttCfgLoad()/loop().
void mqttTick();
void mqttCallback(char* topic, byte* payload, unsigned int len);
void mqttPrintStatus();
void cmdMqttSet(const String& cmd);

static void mqSetStr(char* dst, size_t cap, const char* src) {
  if (!src) src = "";
  strncpy(dst, src, cap - 1);
  dst[cap - 1] = 0;
}

static void mqSetErr(const char* e) { mqSetStr(mqErr, sizeof(mqErr), e); }

// ---- настройки: secrets.h + NVS -------------------------------------------
void mqCfgLoad() {
  mqSetStr(mqHost, sizeof(mqHost), MQTT_HOST);
  mqSetStr(mqUser, sizeof(mqUser), MQTT_USER);
  mqSetStr(mqPass, sizeof(mqPass), MQTT_PASS);
  mqSetStr(mqBase, sizeof(mqBase), MQTT_BASE);
  mqPort = MQTT_PORT;
  {
    Preferences p;
    if (p.begin(MQ_CFG_NS, true)) {          // только чтение
      String h = p.getString("host", mqHost);
      mqSetStr(mqHost, sizeof(mqHost), h.c_str());
      mqSetStr(mqUser, sizeof(mqUser), p.getString("user", mqUser).c_str());
      mqSetStr(mqPass, sizeof(mqPass), p.getString("pass", mqPass).c_str());
      mqSetStr(mqBase, sizeof(mqBase), p.getString("base", mqBase).c_str());
      long pt = p.getLong("port", (long)mqPort);
      if (pt > 0 && pt < 65536) mqPort = (uint16_t)pt;
      p.end();
    }
  }
  if (mqPort == 0) mqPort = MQ_DFLT_PORT;
  if (!mqBase[0]) mqSetStr(mqBase, sizeof(mqBase), MQTT_BASE);
  mqCli.setCallback(mqttCallback);
  mqCli.setKeepAlive(30);
  mqCli.setSocketTimeout(3);
  // 1024: discovery-конфиг одной сущности — ~520 байт, состояние — ~260.
  // По умолчанию у PubSubClient 256 — не хватает, публикация молча падает.
  mqCli.setBufferSize(1024);
  mqCfgLoaded = true;
  Serial.printf("MQTT: %s host=\"%s\" port=%u base=%s user=%s\n",
                mqHost[0] ? "включён" : "выключен", mqHost, (unsigned)mqPort,
                mqBase, mqUser[0] ? mqUser : "-");
}

// ---- описание сущностей для Home Assistant --------------------------------
// value_template намеренно через 'ON'/'OFF' там, где binary_sensor: Jinja
// печатает True/False, а HA ждёт payload_on/payload_off по умолчанию ON/OFF.
struct HaEnt {
  const char* key;
  const char* name;
  const char* unit;     // "" — поля нет
  const char* dclass;
  const char* sclass;
  const char* tpl;
  const char* comp;     // sensor | binary_sensor
  const char* icon;     // mdi-иконка; у старых строк не задана -> nullptr
};

static const HaEnt HA_ENT[] = {
  { "v",    "Напряжение",        "V",     "voltage",         "measurement",      "{{ value_json.v }}",                      "sensor" },
  { "p",    "Мощность",          "W",     "power",           "measurement",      "{{ value_json.p }}",                      "sensor" },
  { "amp",  "Ток",               "A",     "current",         "measurement",      "{{ value_json.amp }}",                    "sensor" },
  { "temp", "Температура реле",  "°C",    "temperature",     "measurement",      "{{ value_json.temp }}",                   "sensor" },
  { "e",    "Счётчик всего",     "kWh",   "energy",          "total_increasing", "{{ value_json.e }}",                      "sensor" },
  { "tkwh", "Расход сегодня",    "kWh",   "energy",          "total_increasing", "{{ value_json.tkwh }}",                   "sensor" },
  { "tcost","Стоимость сегодня", "₽",     "monetary",        "total",            "{{ value_json.tcost }}",                  "sensor" },
  { "ykwh", "Расход вчера",      "kWh",   "energy",          "total",            "{{ value_json.ykwh }}",                   "sensor" },
  { "mkwh", "Расход за месяц",   "kWh",   "energy",          "total",            "{{ value_json.mkwh }}",                   "sensor" },
  { "rssi", "Уровень Wi-Fi",     "dBm",   "signal_strength", "measurement",      "{{ value_json.rssi }}",                   "sensor" },
  { "up",   "Работает",          "s",     "duration",        "total_increasing", "{{ value_json.up }}",                     "sensor" },
  { "r",    "Реле",              "",      "power",           "",                 "{{ 'ON' if value_json.r else 'OFF' }}",   "binary_sensor" },
  { "net",  "Сеть",              "",      "connectivity",    "",                 "{{ 'ON' if value_json.net else 'OFF' }}", "binary_sensor" },
  // Погода (OpenWeatherMap): те же числа, что в /data (wT/wH/wW/wPop/wPopT/
  // wTminT/wTmaxT). Раньше они были только в /data и на экране.
  { "wT",    "Температура на улице", "°C",  "temperature", "measurement", "{{ value_json.wT }}",     "sensor", "mdi:thermometer" },
  { "wH",    "Влажность на улице",   "%",   "humidity",    "measurement", "{{ value_json.wH }}",     "sensor", "mdi:water-percent" },
  { "wW",    "Ветер",                "km/h","wind_speed",  "measurement", "{{ value_json.wW }}",     "sensor", "mdi:weather-windy" },
  { "wPop",  "Дождь сейчас",         "%",   "",            "measurement", "{{ value_json.wPop }}",   "sensor", "mdi:weather-rainy" },
  { "wPopT", "Дождь завтра",         "%",   "",            "measurement", "{{ value_json.wPopT }}",  "sensor", "mdi:weather-pouring" },
  { "wTminT","Завтра минимум",       "°C",  "temperature", "measurement", "{{ value_json.wTminT }}", "sensor", "mdi:thermometer-arrow-down" },
  { "wTmaxT","Завтра максимум",      "°C",  "temperature", "measurement", "{{ value_json.wTmaxT }}", "sensor", "mdi:thermometer-arrow-down" },
};
#define HA_N ((int)(sizeof(HA_ENT) / sizeof(HA_ENT[0])))

// JSON конфига одной сущности. Аргумент — индекс, а не struct: Arduino
// вставляет прототипы функций перед первым телом, и свой тип в подписи
// там был бы ещё не объявлен.
int mqttHaConfig(int i, char* b, size_t n) {
  if (i < 0 || i >= HA_N) return -1;
  const HaEnt& e = HA_ENT[i];
  int w = snprintf(b, n,
    "{\"name\":\"%s\",\"unique_id\":\"%s_%s\",\"state_topic\":\"%s/state\","
    "\"value_template\":\"%s\",",
    e.name, mqBase, e.key, mqBase, e.tpl);
  if (w < 0 || w >= (int)n) return -1;
  if (e.unit && e.unit[0])
    w += snprintf(b + w, n - w, "\"unit_of_measurement\":\"%s\",", e.unit);
  if (w < 0 || w >= (int)n) return -1;
  if (e.dclass && e.dclass[0])
    w += snprintf(b + w, n - w, "\"device_class\":\"%s\",", e.dclass);
  if (w < 0 || w >= (int)n) return -1;
  if (e.sclass && e.sclass[0])
    w += snprintf(b + w, n - w, "\"state_class\":\"%s\",", e.sclass);
  if (w < 0 || w >= (int)n) return -1;
  if (e.icon && e.icon[0])
    w += snprintf(b + w, n - w, "\"icon\":\"%s\",", e.icon);
  if (w < 0 || w >= (int)n) return -1;
  w += snprintf(b + w, n - w,
    "\"availability_topic\":\"%s/status\","
    "\"device\":{\"identifiers\":[\"%s\"],\"name\":\"Дом — энергия\","
    "\"manufacturer\":\"NIKA SERVICE\","
    "\"model\":\"CYD-4.4 (ESP32-2432S028)\",\"sw_version\":\"4.4\"}}",
    mqBase, mqBase);
  if (w < 0 || w >= (int)n) return -1;
  return w;
}

// ---- состояние -------------------------------------------------------------
// Ровно те же значения и в тех же единицах, что в /data.
void mqttBuildState(char* b, size_t n) {
  snprintf(b, n,
    "{\"v\":%.0f,\"p\":%.1f,\"amp\":%.2f,\"temp\":%.1f,\"e\":%.2f,\"r\":%s,"
    "\"tkwh\":%.3f,\"tcost\":%.2f,\"ykwh\":%.3f,\"mkwh\":%.3f,"
    "\"tariff\":%.2f,\"tday\":%.2f,\"tnight\":%.2f,"
    "\"rssi\":%d,\"up\":%lu,\"net\":%s,\"led\":%d,\"gaps\":%d,"
    // Погода: без этих полей сенсоры wT..wTmaxT в HA оставались пустыми.
    "\"wT\":%.1f,\"wH\":%.0f,\"wW\":%.1f,\"wPop\":%.0f,\"wPopT\":%.0f,"
    "\"wTminT\":%.1f,\"wTmaxT\":%.1f}",
    gVoltage, gPower, gCurrent, gTemp, gEnergy,
    gRelay ? "true" : "false",
    myTodayKwh, myTodayCost, yKwh, mKwh,
    measuredTariff(), TARIFF_DAY, TARIFF_NIGHT,
    gRssi, millis() / 1000UL, gNetOk ? "true" : "false",
    gLedState, gapTodayCount(),
    wTemp, wHum, wWind, wPopNow, wPopTom, wTminTom, wTmaxTom);
}

bool mqttPublish(const char* sub, const char* payload, bool retain) {
  if (!mqUp || !mqCli.connected()) return false;
  char t[64];
  snprintf(t, sizeof(t), "%s/%s", mqBase, sub);
  bool ok = mqCli.publish(t, payload, retain);
  if (ok) {
    mqPubN++;
    mqLastOkMs = millis();
    mqSetStr(mqLastTop, sizeof(mqLastTop), t);
  } else {
    mqSetErr("publish");
  }
  return ok;
}

bool mqttPublishState(bool force) {
  char b[512];
  mqttBuildState(b, sizeof(b));
  if (!force) {
    bool changed = (strcmp(b, mqLastState) != 0);
    if (!changed && millis() - mqLastPub < MQ_STATE_MS) return false;
    if (changed && millis() - mqLastPub < MQ_MIN_MS) return false;
  }
  if (!mqttPublish("state", b, false)) return false;
  mqSetStr(mqLastState, sizeof(mqLastState), b);
  mqLastPub = millis();
  mqForcePub = false;
  return true;
}

// ---- реле: включить (ВЫКЛЮЧЕНИЕ ЗАБЛОКИРОВАНО) ------------------------------
// Локальный протокол реле уже реализован для опроса (tuyaQuery/negotiate).
// Здесь тот же кадр, но с командой управления DP 16 = true.
#define CMD_DP_CONTROL 7

bool tuyaRelaySet(bool on) {
  esp_task_wdt_reset();
  if (WiFi.status() != WL_CONNECTED) { mqSetErr("relay:нет wifi"); return false; }
  if (!sock.connected()) {
    sock.stop();
    sock.setTimeout(8);
    if (!sock.connect(RELAY_IP, RELAY_PORT, 2000)) { mqSetErr("relay:tcp"); return false; }
    sessionReady = false;
  }
  if (!sessionReady && !negotiate()) { sock.stop(); sessionReady = false; mqSetErr("relay:sess"); return false; }

  char req[48];
  snprintf(req, sizeof(req), on ? "{\"dps\":{\"16\":true}}" : "{\"dps\":{\"16\":false}}");
  uint8_t buf[512], payload[256];
  seqNo++;
  size_t n = buildFrame(CMD_DP_CONTROL, (const uint8_t*)req, strlen(req), sessionKey, buf);
  sock.write(buf, n);

  // Ответ может быть не ACK, а push-отчёт — читаем до двух кадров.
  for (int k = 0; k < 2; k++) {
    int got = readFrame(buf, sizeof(buf), 1200);
    esp_task_wdt_reset();
    if (got < 0) { sock.stop(); sessionReady = false; mqSetErr("relay:нет ответа"); return false; }
    uint8_t cmd = 0;
    int plen = parseFrame(buf, got, sessionKey, &cmd, payload, sizeof(payload));
    if (plen <= 4) continue;
    payload[plen] = 0;
    uint32_t rc = ((uint32_t)payload[0] << 24) | ((uint32_t)payload[1] << 16) |
                  ((uint32_t)payload[2] << 8) | payload[3];
    const char* js = (const char*)(payload + 4);
    if (strstr(js, "\"dps\"")) applyDps(js);        // gRelay обновится сам
    if (cmd == CMD_DP_CONTROL) {
      if (rc != 0) { mqSetErr("relay:rc"); return false; }
      return true;
    }
  }
  mqSetErr("relay:без ack");
  return false;
}

// включаем реле (старое имя, чтобы не править вызовы)
bool tuyaRelayOn() { return tuyaRelaySet(true); }

// ---- команды из MQTT -------------------------------------------------------
void mqttCallback(char* topic, byte* payload, unsigned int len) {
  mqMsgN++;
  esp_task_wdt_reset();
  char cmd[32];
  unsigned int k = 0;
  // Пробелы и табы по краям раньше попадали в команду (" relay_on" != "relay_on"),
  // и HA получало unknown. Обрезаем их ДО сравнения.
  unsigned int b = 0, e = len;
  while (b < e && (payload[b] == ' ' || payload[b] == '\t' ||
                   payload[b] == '\r' || payload[b] == '\n' || payload[b] == 0)) b++;
  while (e > b && (payload[e-1] == ' ' || payload[e-1] == '\t' ||
                   payload[e-1] == '\r' || payload[e-1] == '\n' || payload[e-1] == 0)) e--;
  for (unsigned int i = b; i < e && k < sizeof(cmd) - 1; i++) {
    byte c = payload[i];
    if (c == 0) continue;
    cmd[k++] = (char)tolower(c);
  }
  cmd[k] = 0;
  Serial.printf("MQTT: команда \"%s\" из %s\n", cmd, topic);

  // Выключение реле разрешено ТОЛЬКО если владелец включил allowOff в настройках.
  if (strcmp(cmd, "relay_off") == 0) {
    if (!gAllowOff) {
      mqttPublish("result", "off_blocked", false);
      Serial.println("MQTT: relay_off заблокирован (allowOff=0)");
      return;
    }
    bool ok = tuyaRelaySet(false);
    mqttPublish("result", ok ? "relay_off" : "relay_off_fail", false);
    Serial.printf("MQTT: relay_off -> %s\n", ok ? "ok" : "fail");
    mqttPublishState(true);
    return;
  }
  if (strcmp(cmd, "relay_on") == 0) {
    bool was = gRelay;
    bool ok = tuyaRelayOn();
    mqttPublish("result", ok ? "relay_on" : "relay_on_fail", false);
    Serial.printf("MQTT: relay_on -> %s (было r=%d)\n", ok ? "ok" : "fail", (int)was);
    mqttPublishState(true);
    return;
  }
  if (strcmp(cmd, "refresh") == 0) {
    mqForcePub = true;
    // Внеочередной опрос реле: сетевой автомат в loop() сделан ровно для этого.
    if (gWifiSetState == 0 && WiFi.status() == WL_CONNECTED) gNetStep = 1;
    mqttPublishState(true);
    mqttPublish("result", "refresh", false);
    return;
  }
  // уставки защиты из Home Assistant: vmax=255, tmax=60, protect_on ...
  if (strncmp(cmd, "vmax=", 5) == 0) { gProtVMax = constrain((float)atof(cmd + 5), 210.0f, 300.0f); protSave(); mqttPublish("result", "vmax_ok", false); return; }
  if (strncmp(cmd, "vmin=", 5) == 0) { gProtVMin = constrain((float)atof(cmd + 5), 120.0f, 220.0f); protSave(); mqttPublish("result", "vmin_ok", false); return; }
  if (strncmp(cmd, "tmax=", 5) == 0) { gProtTMax = constrain((float)atof(cmd + 5), 40.0f, 90.0f);   protSave(); mqttPublish("result", "tmax_ok", false); return; }
  if (strncmp(cmd, "pmax=", 5) == 0) { gProtPMax = constrain((float)atof(cmd + 5), 100.0f, 10000.0f); protSave(); mqttPublish("result", "pmax_ok", false); return; }
  if (strcmp(cmd, "protect_on") == 0)  { gProtOn = true;  protSave(); mqttPublish("result", "protect_on", false);  return; }
  if (strcmp(cmd, "protect_off") == 0) { gProtOn = false; protSave(); mqttPublish("result", "protect_off", false); return; }
  if (strcmp(cmd, "allow_off_on") == 0)  { gAllowOff = true;  schSave(); mqttPublish("result", "allow_off_on", false);  return; }
  if (strcmp(cmd, "allow_off_off") == 0) { gAllowOff = false; schSave(); mqttPublish("result", "allow_off_off", false); return; }
  if (strncmp(cmd, "sched=", 6) == 0)    { schParse(String(cmd + 6)); schSave(); mqttPublish("result", "sched_ok", false); return; }

  mqttPublish("result", "unknown", false);
}

// ---- подключение -----------------------------------------------------------
bool mqttConnect() {
  mqSetErr("");
  if (WiFi.status() != WL_CONNECTED) { mqSetErr("нет wifi"); return false; }
  esp_task_wdt_reset();
  mqNet.stop();
  mqNet.setTimeout(3);
  // TCP отдельно и с коротким таймаутом: иначе PubSubClient блокирует цикл
  // своим MQTT_SOCKET_TIMEOUT на DNS+connect+CONNACK сразу.
  if (!mqNet.connect(mqHost, mqPort, 2000)) {
    mqFailN++; mqSetErr("tcp"); esp_task_wdt_reset();
    return false;
  }
  char cid[40], st[40], ct[40];
  snprintf(cid, sizeof(cid), "%s-%06X", mqBase,
           (unsigned)((uint32_t)(ESP.getEfuseMac() & 0xFFFFFF)));
  snprintf(st, sizeof(st), "%s/status", mqBase);
  snprintf(ct, sizeof(ct), "%s/cmd", mqBase);

  esp_task_wdt_reset();
  bool ok;
  if (mqUser[0])
    ok = mqCli.connect(cid, mqUser, mqPass, st, 0, true, "offline");
  else
    ok = mqCli.connect(cid, st, 0, true, "offline");
  esp_task_wdt_reset();
  if (!ok) {
    mqFailN++;
    mqSetErr(mqCli.state() == MQTT_CONNECT_BAD_CREDENTIALS ? "логин/пароль"
             : (mqCli.state() == MQTT_CONNECT_UNAUTHORIZED ? "запрещено" : "connect"));
    mqNet.stop();
    return false;
  }
  mqUp = true;
  mqOkN++;
  mqErr[0] = 0;
  mqCli.publish(st, "online", true);        // availability, retained
  mqCli.subscribe(ct, 1);
  mqDiscIdx = 0;                            // discovery заново после реконнекта
  mqLastDisc = millis();
  mqttPublishState(true);
  Serial.printf("MQTT: подключён к %s:%u, топик %s/state\n", mqHost, (unsigned)mqPort, mqBase);
  return true;
}

// ---- тик: вызывается из loop() --------------------------------------------
void mqttTick() {
  if (!mqCfgLoaded) mqCfgLoad();
  if (!mqHost[0]) return;                   // MQTT выключен — ни одного вызова

  if (WiFi.status() != WL_CONNECTED) {
    if (mqUp) { mqUp = false; mqSetErr("нет wifi"); }
    return;
  }

  if (!mqUp || !mqCli.connected()) {
    if (mqUp) { mqUp = false; mqSetErr("обрыв"); }   // связь пропала без LWT
    if (mqLastTry != 0 && millis() - mqLastTry < MQ_RETRY_MS) return;
    mqLastTry = millis();
    mqttConnect();
    return;
  }

  mqCli.loop();

  // discovery: по одному конфигу за проход, потом пауза 30 мин
  if (mqDiscIdx < HA_N) {
    char cfg[768], t[80];
    if (mqttHaConfig(mqDiscIdx, cfg, sizeof(cfg)) > 0) {
      snprintf(t, sizeof(t), "%s/%s/%s/%s/config",
               MQ_HA_PREFIX, HA_ENT[mqDiscIdx].comp, mqBase, HA_ENT[mqDiscIdx].key);
      if (mqCli.publish(t, cfg, true)) {
        mqPubN++;
        mqLastOkMs = millis();
        mqSetStr(mqLastTop, sizeof(mqLastTop), t);
      } else {
        mqSetErr("disc");
      }
    }
    mqDiscIdx++;
    if (mqDiscIdx >= HA_N) {
      mqLastDisc = millis();
      Serial.printf("MQTT: discovery отправлен (%d сущностей)\n", HA_N);
    }
  } else if (millis() - mqLastDisc > MQ_DISC_MS) {
    mqDiscIdx = 0;
    mqLastDisc = millis();
  }

  if (mqForcePub) mqttPublishState(true);
  else mqttPublishState(false);
}

// ---- диагностика -----------------------------------------------------------
void mqttPrintStatus() {
  Serial.printf("MQTT: %s, сервер %s:%u, состояние %s\n",
                mqHost[0] ? "включён" : "выключен",
                mqHost[0] ? mqHost : "-", (unsigned)mqPort,
                mqUp ? "подключён" : "нет связи");
  Serial.printf("MQTT: подкл=%lu отказ=%lu сообщ=%lu публ=%lu discovery=%d/%d heap=%u\n",
                mqOkN, mqFailN, mqMsgN, mqPubN, mqDiscIdx, HA_N, ESP.getFreeHeap());
  if (mqErr[0]) Serial.printf("MQTT: последняя ошибка: %s\n", mqErr);
  if (mqLastTop[0])
    Serial.printf("MQTT: последняя публикация %s, %.1f с назад\n",
                  mqLastTop, (millis() - mqLastOkMs) / 1000.0);
}

// MQTTSET "host" [port] ["user"] ["pass"]   — настроить и сохранить в NVS
// MQTTSET ""                                — выключить MQTT
// MQTTSET                                   — показать настройки
void cmdMqttSet(const String& cmd) {
  int i = 7;                                  // длина слова MQTTSET
  String host;
  if (!wfArg(cmd, i, host)) {                 // без аргументов — только показать
    mqttPrintStatus();
    Serial.printf("MQTT: host=\"%s\" port=%u base=%s user=%s pass=%d симв.\n",
                  mqHost, (unsigned)mqPort, mqBase,
                  mqUser[0] ? mqUser : "-", (int)strlen(mqPass));
    return;
  }
  String portS, user, pass;
  wfArg(cmd, i, portS);
  wfArg(cmd, i, user);
  wfArg(cmd, i, pass);

  Preferences p;
  if (!p.begin(MQ_CFG_NS, false)) { Serial.println("MQTTSET: ERR NVS недоступен"); return; }
  p.putString("host", host);
  if (portS.length()) p.putLong("port", portS.toInt());
  p.putString("user", user);
  p.putString("pass", pass);
  p.end();

  mqCfgLoaded = false;                        // перечитать при следующем тике
  mqCfgLoad();
  mqUp = false;
  mqNet.stop();
  mqLastTry = 0;                              // попытка сразу, без 30 с ожидания
  mqDiscIdx = 0;
  mqLastState[0] = 0;
  Serial.printf("MQTTSET: %s host=\"%s\" port=%u user=%s\n",
                mqHost[0] ? "включён" : "выключен", mqHost, (unsigned)mqPort,
                mqUser[0] ? mqUser : "-");
}


// Время строки CSV в epoch. Ручной разбор быстрее sscanf и не тянет String.
static long gfLineEpoch(const char* s, int len) {
  if (len < 19) return 0;
  if (s[4] != '-' || s[7] != '-' || s[10] != ' ' || s[13] != ':' || s[16] != ':') return 0;
  for (int i = 0; i < 19; i++) {
    if (i == 4 || i == 7 || i == 10 || i == 13 || i == 16) continue;
    if (s[i] < '0' || s[i] > '9') return 0;
  }
  int y  = (s[0] - '0') * 1000 + (s[1] - '0') * 100 + (s[2] - '0') * 10 + (s[3] - '0');
  int mo = (s[5] - '0') * 10 + (s[6] - '0');
  int d  = (s[8] - '0') * 10 + (s[9] - '0');
  int h  = (s[11] - '0') * 10 + (s[12] - '0');
  int mi = (s[14] - '0') * 10 + (s[15] - '0');
  int se = (s[17] - '0') * 10 + (s[18] - '0');
  if (y < 2020 || y > 2100 || mo < 1 || mo > 12 || d < 1 || d > 31) return 0;
  if (h > 23 || mi > 59 || se > 59) return 0;
  return daysFromCivil(y, (unsigned)mo, (unsigned)d) * 86400L
       + h * 3600L + mi * 60L + se;
}

// 4-е поле строки (счётчик) в сотых кВт·ч. 0 — поля нет.
static int32_t gfFieldE(const char* s, int len) {
  const char* end = s + len;
  const char* p = (const char*)memchr(s, ',', len);
  if (!p) return 0;
  p = (const char*)memchr(p + 1, ',', (int)(end - p) - 1);
  if (!p) return 0;
  p = (const char*)memchr(p + 1, ',', (int)(end - p) - 1);
  if (!p) return 0;
  p++;
  int32_t ip = 0;
  int frac = 0, fdiv = 1;
  bool any = false;
  while (p < end && *p >= '0' && *p <= '9') { ip = ip * 10 + (*p - '0'); p++; any = true; }
  if (p < end && *p == '.') {
    p++;
    while (p < end && *p >= '0' && *p <= '9' && fdiv < 1000) {
      frac = frac * 10 + (*p - '0'); fdiv *= 10; p++;
    }
  }
  if (!any) return 0;
  return ip * 100 + (fdiv > 1 ? (int32_t)((frac * 100) / fdiv) : 0);
}

static void gfStamp(long t, char* out, int cap) {
  // В файле и во всей прошивке время «местное» (см. lineEpoch/nowLocalEpoch),
  // поэтому разбираем его через gmtime_r, а не localtime_r: иначе +3 часа.
  time_t et = (time_t)t;
  struct tm lt;
  gmtime_r(&et, &lt);
  snprintf(out, cap, "%02d.%02d %02d:%02d:%02d",
           lt.tm_mon + 1, lt.tm_mday, lt.tm_hour, lt.tm_min, lt.tm_sec);
}

// Смещение «местное минус UTC» в секундах. Облако живёт по UTC, файл — по
// местному, поэтому в запрос время уходит с вычетом, а из ответа — с прибавкой.
static long gfTzOffset() {
  long L = nowLocalEpoch();
  if (L <= 0) return 0;
  time_t t = (time_t)L;
  struct tm lt;
  gmtime_r(&t, &lt);            // поля = местные (L уже сдвинут на смещение)
  lt.tm_isdst = -1;
  time_t u = mktime(&lt);       // обратно в настоящий UTC
  if (u < 1600000000) return 0;
  long off = L - (long)u;
  if (off < -12L * 3600L || off > 14L * 3600L) return 0;
  return off;
}

// ---------------------------------------------------------------- шаг 1: поиск
// Один проход по файлу: пары соседних строк с разрывом > 2 минут. Заодно
// считаем пропуски за сегодня — их показывает экран ИТОГИ и поле /data.
// Возвращает число пропусков (или -1 при ошибке чтения).
static int gapScan() {
  sGF.gaps = 0; sGF.gapSec = 0;
  sGF.todayN = 0; sGF.todayMin = 0; sGF.yestN = 0; sGF.yestMin = 0;
  if (!gSdOk) return -1;
  long nowE = nowLocalEpoch();
  if (nowE <= 0) return -1;
  long todayKey = nowE / 86400L;
  int  days = gGapDays > 0 ? gGapDays : GF_SCOPE_DAYS;
  long scopeStart = (todayKey - (days - 1)) * 86400L;
  long scopeLo = gGapFromE ? gGapFromE : scopeStart;   // диапазон из команды
  long scopeHi = gGapToE ? gGapToE : 0x7FFFFFFFL;

  File f = SD.open(gGapPath, FILE_READ);
  if (!f) return -1;

  static uint8_t buf[512 + 256];
  int carry = 0;
  uint32_t li = 0;                 // номер строки в файле (с 0)
  long prevT = 0, prevE = 0;
  uint32_t prevLine = 0;
  bool bad = false;

  while (f.available()) {
    int n = f.read(buf + carry, 512);
    if (n <= 0) break;
    int total = carry + n;
    int i = 0;
    while (i < total) {
      uint8_t* nl = (uint8_t*)memchr(buf + i, '\n', total - i);
      if (!nl) break;
      int len = (int)(nl - (buf + i));
      if (len > 0 && buf[i + len - 1] == '\r') len--;
      long t = 0;
      long e = 0;
      if (len >= 20 && isDatePrefix((const char*)(buf + i), len)) {
        t = gfLineEpoch((const char*)(buf + i), len);
        if (t > 0) e = gfFieldE((const char*)(buf + i), len);
      }
      if (t > 0) {
        if (prevT > 0) {
          long d = t - prevT;
          if (d > GF_MIN_GAP) {
            int mins = (int)((d + 59) / 60);
            long dk = t / 86400L;
            if (dk == todayKey)            { sGF.todayN++; sGF.todayMin += mins; }
            else if (dk == todayKey - 1)   { sGF.yestN++;  sGF.yestMin += mins; }
            // Заполняем только: не старьше scope, не длиннее 6 ч, старше 5 мин.
            if (t >= scopeLo && t <= scopeHi && d <= GF_MAX_GAP && (nowE - t) >= GF_AGE &&
                sGF.gaps < GF_MAX_GAPS) {
              GapItem& g = sGf[sGF.gaps];
              memset(&g, 0, sizeof(g));
              g.a = prevT; g.b = t;
              g.ea = (int32_t)prevE; g.eb = (int32_t)e;
              g.line = prevLine;
              sGF.gaps++;
              sGF.gapSec += d;
            }
          }
        }
        prevT = t; prevE = e; prevLine = li;
      }
      li++;
      i = (int)(nl - buf) + 1;
      if ((li & 1023) == 0) esp_task_wdt_reset();
    }
    carry = total - i;
    if (carry > 0) memmove(buf, buf + i, carry);
    if (carry > 240) { bad = true; break; }     // строка длиннее буфера
    esp_task_wdt_reset();
  }
  f.close();
  if (bad) { Serial.println("GAPFILL-ERR: строка в CSV длиннее буфера"); sGF.gaps = 0; return -1; }
  return sGF.gaps;
}

// ---------------------------------------------------------------- шаг 2: облако
// Разбор одного ответа report-logs. Записи разных кодов идут вперемешку,
// поэтому смотрим на "code": phase_a — байты, temp_current — десятые,
// total_forward_energy — сотые.
static void gfParseWindow(const String& body, const GapItem& g,
                          int* nPh, int* nT, int* nE) {
  int pos = 0;
  int len = body.length();
  while (true) {
    int ei = body.indexOf("\"event_time\":", pos);
    if (ei < 0) break;
    int ci = body.indexOf("\"code\":\"", pos);
    String code;
    if (ci >= 0 && ci < ei) {
      int cs = ci + 8;
      int ce = body.indexOf('"', cs);
      if (ce > cs) code = body.substring(cs, ce);
    }
    int i = ei + 13;
    while (i < len && (body[i] == ' ' || body[i] == '"')) i++;
    int j = i;
    while (j < len && isDigit(body[j])) j++;
    if (j == i) { pos = ei + 13; break; }
    unsigned long long ms = strtoull(body.substring(i, j).c_str(), NULL, 10);
    pos = j;

    int v = body.indexOf("\"value\":", pos);
    if (v < 0) break;
    v += 8;
    while (v < len && body[v] == ' ') v++;
    int e2 = -1;
    String val;
    if (v < len && body[v] == '"') {
      int vs = v + 1;
      e2 = body.indexOf('"', vs);
      if (e2 < 0) break;
      val = body.substring(vs, e2);
    } else {
      e2 = v;
      while (e2 < len && (isDigit(body[e2]) || body[e2] == '-' || body[e2] == '.')) e2++;
      val = body.substring(v, e2);
    }
    pos = e2 + 1;

    time_t et = (time_t)(ms / 1000ULL);
    // облако отдаёт UTC — переводим в «местное» время файла
    long etLoc = (long)et + sGF.tzOff;
    if (code.length() == 0) {
      // облако не сказало код: цифры — температура, иначе phase_a (как в histTick)
      bool digits = val.length() > 0;
      for (int k = 0; k < (int)val.length(); k++)
        if (!isDigit(val[k]) && val[k] != '-' && val[k] != '.') { digits = false; break; }
      code = digits ? "temp_current" : "phase_a";
    }

    if (code == "phase_a") {
      // только внутри пропуска: границы уже есть в файле
      if (etLoc <= g.a || etLoc >= g.b) continue;
      if (sGF.poolN >= GF_POOL) continue;
      uint8_t raw[12];
      if (b64decode(val.c_str(), raw, sizeof(raw)) < 8) continue;
      int volt = ((uint16_t)raw[0] << 8) | raw[1];
      int pwr  = ((uint16_t)raw[6] << 8) | raw[7];
      if (volt < 800 || volt > 4000) continue;      // 80.0..400.0 В
      if (pwr < 0 || pwr > 20000) continue;
      GapRow& r = sGfRow[sGF.poolN++];
      r.t = (uint32_t)etLoc;
      r.v = (uint16_t)((volt + 5) / 10);
      r.p = (uint16_t)pwr;
      r.e = 0;
      r.tc = INT16_MIN;
      (*nPh)++;
    } else if (code == "temp_current") {
      if (etLoc < g.a - 120 || etLoc > g.b + 120) continue;
      // temp_current приходит в десятых градуса («223» = 22.3)
      float tv = val.toFloat();
      int t10 = (int)(tv + (tv >= 0 ? 0.5f : -0.5f));
      if (t10 < -400 || t10 > 1000) continue;
      if (sGF.nTmp < GF_MAX_TMP) {
        sGfTmp[sGF.nTmp].t = (uint32_t)etLoc;
        sGfTmp[sGF.nTmp].v = (int16_t)t10;
        sGF.nTmp++;
        (*nT)++;
      }
    } else if (code == "total_forward_energy") {
      if (etLoc < g.a - 3600 || etLoc > g.b + 60) continue;
      int32_t ev = (int32_t)(val.toFloat() + 0.5f);   // сотые кВт·ч
      if (ev <= 0) continue;
      if (sGF.nEn < GF_MAX_EN) {
        sGfEn[sGF.nEn].t = (uint32_t)etLoc;
        sGfEn[sGF.nEn].e = ev;
        sGF.nEn++;
        (*nE)++;
      }
    }
    esp_task_wdt_reset();
  }
}

static int gfRowCmp(const void* a, const void* b) {
  const GapRow* x = (const GapRow*)a;
  const GapRow* y = (const GapRow*)b;
  if (x->t < y->t) return -1;
  if (x->t > y->t) return 1;
  return 0;
}

// ---------------------------------------------------------------- шаг 3: значения
// Счётчик: облачные записи берём, только если они попадают в диапазон
// [до пропуска .. после пропуска]. Иначе — линейная интерполяция между
// соседними строками. Так счётчик внутри пропуска не убывает и энергия
// за сутки не меняется — она и раньше считалась по разнице концов.
static void gfFinalize(GapItem& g) {
  if (g.rowN > 1) {
    qsort(sGfRow + g.row0, g.rowN, sizeof(GapRow), gfRowCmp);
    int w = 1;                                    // убрать дубли по времени
    for (int k = 1; k < g.rowN; k++)
      if (sGfRow[g.row0 + k].t != sGfRow[g.row0 + w - 1].t) {
        if (w != k) sGfRow[g.row0 + w] = sGfRow[g.row0 + k];
        w++;
      }
    g.rowN = (uint16_t)w;
  }
  bool interp = (g.ea > 0 && g.eb >= g.ea && g.b > g.a);
  int32_t prevE = g.ea;
  g.srcE = 0;
  for (int k = 0; k < g.rowN; k++) {
    GapRow& r = sGfRow[g.row0 + k];
    // счётчик
    int32_t e = 0;
    if (interp) {
      long long num = (long long)(g.eb - g.ea) * (long long)((long)r.t - g.a);
      e = g.ea + (int32_t)(num / (long long)(g.b - g.a));
      if (sGF.nEn > 0) {
        int best = -1;
        uint32_t bd = 0xFFFFFFFFUL;
        for (int j = 0; j < sGF.nEn; j++) {
          uint32_t d = (sGfEn[j].t > r.t) ? (sGfEn[j].t - r.t) : (r.t - sGfEn[j].t);
          if (d < bd) { bd = d; best = j; }
        }
        if (best >= 0 && sGfEn[best].e >= g.ea && sGfEn[best].e <= g.eb) {
          e = sGfEn[best].e;
          g.srcE++;
        }
      }
      if (e < g.ea) e = g.ea;
      if (e > g.eb) e = g.eb;
      if (e < prevE) e = prevE;             // монотонно внутри пропуска
      prevE = e;
    }
    r.e = e;
    // температура: ближайшая запись, если она рядом
    r.tc = INT16_MIN;
    if (sGF.nTmp > 0) {
      int best = -1;
      uint32_t bd = 0xFFFFFFFFUL;
      for (int j = 0; j < sGF.nTmp; j++) {
        uint32_t d = (sGfTmp[j].t > r.t) ? (sGfTmp[j].t - r.t) : (r.t - sGfTmp[j].t);
        if (d < bd) { bd = d; best = j; }
      }
      if (best >= 0 && bd <= (uint32_t)GF_TEMP_NEAR) r.tc = sGfTmp[best].v;
    }
  }
}

// ---------------------------------------------------------------- шаг 4: запись
// Одна строка CSV: время,В,Вт,кВт·ч,реле,0.00,°C
// Рубли 0.00 — метка «восстановлено облаком».
static int gfMakeRow(const GapRow* r, char* out, int cap) {
  time_t et = (time_t)r->t;
  struct tm lt;
  gmtime_r(&et, &lt);            // местное время, как во всём файле
  char et2[12];
  if (r->e > 0) snprintf(et2, sizeof(et2), "%d.%02d", (int)(r->e / 100), (int)(r->e % 100));
  else          strcpy(et2, "0");
  int relay = (r->p > 0) ? 1 : 0;
  int ol;
  if (r->tc == INT16_MIN)
    ol = snprintf(out, cap, "%04d-%02d-%02d %02d:%02d:%02d,%u,%u,%s,%d,0.00,\n",
                  lt.tm_year + 1900, lt.tm_mon + 1, lt.tm_mday,
                  lt.tm_hour, lt.tm_min, lt.tm_sec,
                  (unsigned)r->v, (unsigned)r->p, et2, relay);
  else
    ol = snprintf(out, cap, "%04d-%02d-%02d %02d:%02d:%02d,%u,%u,%s,%d,0.00,%.1f\n",
                  lt.tm_year + 1900, lt.tm_mon + 1, lt.tm_mday,
                  lt.tm_hour, lt.tm_min, lt.tm_sec,
                  (unsigned)r->v, (unsigned)r->p, et2, relay, r->tc / 10.0f);
  if (ol < 0 || ol >= cap) return -1;
  return ol;
}

// Перезапись /energy.csv -> /energy.tmp с вставкой строк в нужные места.
// Вставляем ПОСЛЕ строки с номером g.line — номера посчитаны при сканировании,
// а файл с тех пор только дописывался в конец, поэтому номера не съехали.
// Если байт прочитано не столько, сколько было, — отказ (файл изменился).
static bool gfRewrite(size_t expect, int* outIns, size_t* outSz, size_t* outLines) {
  File src = SD.open(gGapPath, FILE_READ);
  if (!src) return false;
  File dst = SD.open("/energy.tmp", FILE_WRITE);
  if (!dst) { src.close(); return false; }

  static uint8_t buf[512 + 256];
  char line[200];
  int carry = 0;
  uint32_t li = 0;
  int gi = 0;
  int ins = 0;
  size_t readTotal = 0, wrote = 0, lines = 0;
  bool ok = true;

  while (src.available() && ok) {
    int n = src.read(buf + carry, 512);
    if (n <= 0) break;
    readTotal += (size_t)n;
    int total = carry + n;
    int i = 0;
    while (i < total) {
      uint8_t* nl = (uint8_t*)memchr(buf + i, '\n', total - i);
      if (!nl) break;
      int len = (int)(nl - (buf + i));
      if (len > 0 && buf[i + len - 1] == '\r') len--;
      if (dst.write(buf + i, (size_t)len) != (size_t)len) { ok = false; break; }
      if (dst.write((uint8_t*)"\n", 1) != 1) { ok = false; break; }
      wrote += (size_t)len + 1;
      lines++;

      while (gi < sGF.gaps && (int32_t)sGf[gi].line == (int32_t)li) {
        GapItem& g = sGf[gi];
        for (int k = 0; k < g.rowN; k++) {
          int ol = gfMakeRow(&sGfRow[g.row0 + k], line, sizeof(line));
          if (ol <= 0 || dst.write((uint8_t*)line, (size_t)ol) != (size_t)ol) { ok = false; break; }
          wrote += (size_t)ol;
          ins++;
        }
        gi++;
        if (!ok) break;
      }
      li++;
      i = (int)(nl - buf) + 1;
      if ((li & 255) == 0) { esp_task_wdt_reset(); yield(); }
    }
    if (!ok) break;
    carry = total - i;
    if (carry > 0) memmove(buf, buf + i, carry);
    if (carry > 240) { ok = false; break; }      // строка длиннее буфера
    esp_task_wdt_reset();
  }

  // Последняя строка без перевода строки.
  if (ok && carry > 0) {
    int len = carry;
    if (len > 0 && buf[len - 1] == '\r') len--;
    if (dst.write(buf, (size_t)len) != (size_t)len ||
        dst.write((uint8_t*)"\n", 1) != 1) ok = false;
    else { wrote += (size_t)len + 1; lines++; }
  }

  if (readTotal != expect) ok = false;         // файл вырос/подменён под нами

  dst.flush();
  dst.close();
  src.close();

  if (!ok || lines == 0 || ins == 0) {
    SD.remove("/energy.tmp");
    Serial.printf("GAPFILL-ERR: перезапись не удалась (прочитано %u из %u, строк %u, вставок %d)\n",
                  (unsigned)readTotal, (unsigned)expect, (unsigned)lines, ins);
    return false;
  }
  File t = SD.open("/energy.tmp", FILE_READ);
  size_t ts = t ? t.size() : 0;
  t.close();
  if (ts == 0 || ts != wrote) {
    SD.remove("/energy.tmp");
    Serial.printf("GAPFILL-ERR: размер .tmp %u != записано %u\n", (unsigned)ts, (unsigned)wrote);
    return false;
  }
  *outIns = ins;
  *outSz = ts;
  *outLines = lines;
  return true;
}

// Бэкап, перезапись, подмена файла. Вызывается один раз, когда все окна
// облака собраны. Любая ошибка -> /energy.csv остаётся прежним.
static bool gfCommit(int* outIns) {
  *outIns = 0;
  int total = 0;
  for (int i = 0; i < sGF.gaps; i++) total += sGf[i].rowN;
  Serial.printf("GAPFILL: пропусков %d, строк к вставке %d\n", sGF.gaps, total);
  if (total == 0) {
    Serial.println("GAPFILL: облако не дало строк — /energy.csv не трогаю");
    return false;
  }
  if (!gSdOk) { Serial.println("GAPFILL-ERR: карта недоступна"); return false; }

  // Хвосты прошлого запуска.
  if (SD.exists("/energy.old") && !SD.exists(gGapPath))
    SD.rename("/energy.old", gGapPath);
  if (SD.exists("/energy.old")) SD.remove("/energy.old");
  if (SD.exists("/energy.tmp")) SD.remove("/energy.tmp");

  size_t sz = csvFileSize();
  if (sz < 20) {
    Serial.println("GAPFILL-ERR: /energy.csv пуст или недоступен");
    return false;
  }

  uint64_t totalB = SD.totalBytes();
  uint64_t used   = SD.usedBytes();
  uint64_t freeB  = (totalB > used) ? (uint64_t)(totalB - used) : 0;
  uint64_t need   = (uint64_t)sz * 4ULL + 65536ULL;
  if (totalB > 0) {
    Serial.printf("GAPFILL: место: свободно %llu, нужно %llu\n",
                  (unsigned long long)freeB, (unsigned long long)need);
    if (freeB < need) {
      Serial.println("GAPFILL-ERR: МАЛО МЕСТА для .bak и .tmp — файл не трогаю");
      return false;
    }
  }

  // 1. Проверенная копия в /energy.bak (сначала .bak.tmp, потом подмена).
  if (SD.exists("/energy.bak.tmp")) SD.remove("/energy.bak.tmp");
  size_t copied = 0;
  Serial.printf("GAPFILL: копия /energy.bak (%u байт)\n", (unsigned)sz);
  if (!pwrHistCopy(gGapPath, "/energy.bak.tmp", sz, &copied)) {
    Serial.printf("GAPFILL-ERR: копия не удалась (%u из %u) — файл не трогаю\n",
                  (unsigned)copied, (unsigned)sz);
    return false;
  }
  File b = SD.open("/energy.bak.tmp", FILE_READ);
  size_t bs = b ? b.size() : 0;
  b.close();
  if (bs != sz) {
    SD.remove("/energy.bak.tmp");
    Serial.printf("GAPFILL-ERR: размер копии %u != %u — файл не трогаю\n",
                  (unsigned)bs, (unsigned)sz);
    return false;
  }
  if (SD.exists("/energy.bak.old")) SD.remove("/energy.bak.old");
  bool moved = true;
  if (SD.exists("/energy.bak")) moved = SD.rename("/energy.bak", "/energy.bak.old");
  if (!moved) {
    SD.remove("/energy.bak.tmp");
    Serial.println("GAPFILL-ERR: не смог отодвинуть старый /energy.bak");
    return false;
  }
  if (!SD.rename("/energy.bak.tmp", "/energy.bak")) {
    if (SD.exists("/energy.bak.old")) SD.rename("/energy.bak.old", "/energy.bak");
    Serial.println("GAPFILL-ERR: не смог поставить /energy.bak");
    return false;
  }
  if (SD.exists("/energy.bak.old")) SD.remove("/energy.bak.old");
  Serial.println("GAPFILL: копия проверена, собираю новый файл");

  // 2. Новый файл с вставками.
  int ins = 0;
  size_t tmpSz = 0, lines = 0;
  if (!gfRewrite(sz, &ins, &tmpSz, &lines)) {
    Serial.println("GAPFILL-ERR: перезапись не удалась — /energy.csv не тронут, /energy.bak цел");
    return false;
  }
  Serial.printf("GAPFILL: строк %u, вставлено %d, /energy.tmp %u байт\n",
                (unsigned)lines, ins, (unsigned)tmpSz);
  if (ins != total) {
    SD.remove("/energy.tmp");
    Serial.printf("GAPFILL-ERR: вставлено %d из %d — файл не трогаю\n", ins, total);
    return false;
  }

  // 3. Подмена: оригинал -> .old, .tmp -> /energy.csv.
  if (!SD.rename(gGapPath, "/energy.old")) {
    SD.remove("/energy.tmp");
    Serial.println("GAPFILL-ERR: не смог переименовать /energy.csv — файл не тронут");
    return false;
  }
  if (!SD.rename("/energy.tmp", gGapPath)) {
    SD.rename("/energy.old", gGapPath);
    SD.remove("/energy.tmp");
    Serial.println("GAPFILL-ERR: не смог поставить новый файл — вернул оригинал из .old");
    return false;
  }
  if (SD.exists("/energy.old")) SD.remove("/energy.old");

  File chk = SD.open(gGapPath, FILE_READ);
  size_t newSz = chk ? chk.size() : 0;
  chk.close();
  if (newSz != tmpSz) {
    Serial.printf("GAPFILL-ERR: размер после подмены %u != %u; оригинал в /energy.bak\n",
                  (unsigned)newSz, (unsigned)tmpSz);
  } else {
    Serial.printf("GAPFILL: /energy.csv обновлён, %u байт (было %u)\n",
                  (unsigned)newSz, (unsigned)sz);
  }

  // 4. Кэши карты и статистика — заново: мин/макс/пик считаются с учётом
  //    восстановленных строк.
  sDaysFileSize = 0; sDaysAt = 0; histCacheDrop(); gSdGen++;
  sEvFileSize = 0;   sEvAt = 0;
  gDayLoaded = -1;
  resetStats();
  statsSeeded = false;
  seedStatsFromSd();
  if (gDayOffset > 0) dayLoadFromSd(gDayOffset);
  gLastDraw = 0; gLastWinFull = 0; gLastSig = 0;
  *outIns = ins;
  return true;
}

// Отмена: вернуть /energy.csv из проверенной копии /energy.bak.
// Нужна, если заполнение оказалось неверным (например, облако отдало чужое
// время). Схема та же: временная копия -> проверка размера -> подмена.
static bool gfRestoreBak() {
  if (!gSdOk) { Serial.println("GAPFILL-ERR: нет карты"); return false; }
  if (!SD.exists("/energy.bak")) { Serial.println("GAPFILL-ERR: файла /energy.bak нет"); return false; }
  File b = SD.open("/energy.bak", FILE_READ);
  size_t bs = b ? b.size() : 0;
  b.close();
  if (bs < 1000) {
    Serial.printf("GAPFILL-ERR: /energy.bak подозрительно мал (%u байт)\n", (unsigned)bs);
    return false;
  }
  uint64_t totalB = SD.totalBytes();
  uint64_t used   = SD.usedBytes();
  uint64_t freeB  = (totalB > used) ? (uint64_t)(totalB - used) : 0;
  if (totalB > 0 && freeB < (uint64_t)bs + 65536ULL) {
    Serial.println("GAPFILL-ERR: мало места для восстановления из .bak");
    return false;
  }
  if (SD.exists("/energy.rest.tmp")) SD.remove("/energy.rest.tmp");
  size_t copied = 0;
  if (!pwrHistCopy("/energy.bak", "/energy.rest.tmp", bs, &copied)) {
    Serial.printf("GAPFILL-ERR: копия из .bak не удалась (%u из %u)\n",
                  (unsigned)copied, (unsigned)bs);
    return false;
  }
  if (SD.exists("/energy.old")) SD.remove("/energy.old");
  if (!SD.rename(gGapPath, "/energy.old")) {
    SD.remove("/energy.rest.tmp");
    Serial.println("GAPFILL-ERR: не смог отодвинуть /energy.csv");
    return false;
  }
  if (!SD.rename("/energy.rest.tmp", gGapPath)) {
    SD.rename("/energy.old", gGapPath);
    SD.remove("/energy.rest.tmp");
    Serial.println("GAPFILL-ERR: не смог поставить файл из .bak — вернул как было");
    return false;
  }
  if (SD.exists("/energy.old")) SD.remove("/energy.old");
  File chk = SD.open(gGapPath, FILE_READ);
  size_t newSz = chk ? chk.size() : 0;
  chk.close();
  Serial.printf("GAPFILL: /energy.csv восстановлен из /energy.bak, %u байт\n", (unsigned)newSz);
  sDaysFileSize = 0; sDaysAt = 0; histCacheDrop(); gSdGen++;
  sEvFileSize = 0;   sEvAt = 0;
  gDayLoaded = -1;
  resetStats();
  statsSeeded = false;
  seedStatsFromSd();
  if (gDayOffset > 0) dayLoadFromSd(gDayOffset);
  gLastDraw = 0; gLastWinFull = 0; gLastSig = 0;
  return newSz == bs;
}

// ---------------------------------------------------------------- GAPFILL: команда
//   GAPFILL                        последние 2 суток в /energy.csv
//   GAPFILL 7                      последние 7 суток (облако хранит 7)
//   GAPFILL 2026-10-25 2026-10-28  точный диапазон (может быть в архиве)
//   GAPFILL LIST                   только показать, не писать
static bool gfParseDate(const char* s, long* outE) {
  int y = 0, m = 0, d = 0;
  if (!s || sscanf(s, "%d-%d-%d", &y, &m, &d) != 3) return false;
  if (y < 2020 || y > 2100 || m < 1 || m > 12 || d < 1 || d > 31) return false;
  // Важно: в прошивке epoch «местный» (nowLocalEpoch = epoch + смещение зоны),
  // поэтому границы дня считаем через daysFromCivil, а НЕ через mktime —
  // mktime даёт UTC-полночь и сдвигал диапазон на 3 часа назад.
  long e = daysFromCivil(y, (unsigned)m, (unsigned)d) * 86400L;
  if (e <= 1600000000L) return false;
  *outE = e;
  return true;
}

bool gapFillCommand(String args) {
  args.trim();
  String u = args;
  u.toUpperCase();
  // LIST можно совмещать с диапазоном: "GAPFILL LIST 2026-10-25 2026-10-28"
  bool dry = (u.indexOf("LIST") >= 0 || u == "DRY");
  if (u.indexOf("LIST") >= 0) { u.replace("LIST", ""); u.trim(); }
  gGapFromE = 0; gGapToE = 0; gGapDays = 0;
  strcpy(gGapPath, "/energy.csv");
  if (u.length() == 0) {
    Serial.println("GAPFILL: последние 2 суток, файл /energy.csv");
  } else if (u.length() <= 2 && isDigit(u[0])) {          // GAPFILL 7
    int n = u.toInt();
    if (n < 1) n = 1;
    if (n > 7) n = 7;
    gGapDays = n;
    Serial.printf("GAPFILL: последние %d суток, файл /energy.csv\n", n);
  } else {
    int sp = u.indexOf(' ');
    String d1 = (sp > 0) ? u.substring(0, sp) : u;
    String d2 = (sp > 0) ? u.substring(sp + 1) : u;
    d1.trim(); d2.trim();
    long a = 0, b = 0;
    if (!gfParseDate(d1.c_str(), &a) || !gfParseDate(d2.c_str(), &b)) {
      Serial.println("GAPFILL-ERR: дата в формате ГГГГ-ММ-ДД (или LIST / число дней)");
      return false;
    }
    if (b < a) { long t = a; a = b; b = t; d1 = d2; }
    char pa[40], pb[40];
    histPathForDate(d1.c_str(), pa, sizeof(pa));
    histPathForDate(d2.c_str(), pb, sizeof(pb));
    if (strcmp(pa, pb) != 0) {
      // один проход = один файл, иначе сложно гарантировать целостность
      Serial.printf("GAPFILL-ERR: диапазон через два файла (%s и %s) — запустите по отдельности\n",
                    pa, pb);
      return false;
    }
    strcpy(gGapPath, pa);
    gGapFromE = a;
    gGapToE = b + 86400L - 1;
    Serial.printf("GAPFILL: %s..%s, файл %s\n", d1.c_str(), d2.c_str(), gGapPath);
  }
  if (dry) Serial.println("GAPFILL: режим LIST — только покажу пропуски, писать не буду");
  return gapFillStart(!dry);
}

// ---------------------------------------------------------------- автомат
bool gapFillStart(bool run) {
  if (sGF.phase != GF_IDLE) { Serial.println("GAPFILL: уже выполняется"); return false; }
  if (!gSdOk) { Serial.println("GAPFILL-ERR: нет карты"); return false; }
  if (pwrHistActive()) { Serial.println("GAPFILL: ждёт PWRHIST"); return false; }
  if (gHistStep != 0) { Serial.println("GAPFILL: ждёт histTick"); return false; }
  if (nowLocalEpoch() <= 0) { Serial.println("GAPFILL-ERR: часы не выставлены"); return false; }
  Serial.printf("GAPFILL: файл %s\n", gGapPath);
  sGF.run = run ? 1 : 0;
  sGF.phase = GF_SCAN;
  sGF.gi = 0;
  sGF.poolN = 0;
  sGF.inserted = 0;
  sGF.retry = 0;
  sGF.nextMs = millis();
  sGF.started = millis();
  strcpy(sGF.msg, run ? "поиск пропусков" : "проверка пропусков");
  return true;
}

void gapFillSchedule(unsigned long delayMs) {
  sGF.phase = GF_IDLE;
  sGF.run = 1;  sGF.gi = 0;
  sGF.poolN = 0;
  sGF.inserted = 0;
  sGF.retry = 0;
  sGF.nextMs = millis() + delayMs;
  sGF.started = millis();
  sGF.pending = 1;
  strcpy(sGF.msg, "запланировано");
  Serial.printf("GAPFILL: запуск через %lu с\n", delayMs / 1000UL);
}

// ---------------------------------------------------------------- раз в сутки
// 01.11.2026: GAPFILL запускается сам РАЗ В СУТКИ в GAP_HOUR:00 по местному времени,
// а не через 60 с после каждого старта. Причина: GAPFILL делает десятки TLS-запросов
// к облаку (каждый ~0.9 с), это пик потребления. На слабом питании автозапуск после
// каждой загрузки давал петлю: сброс → 60 с → GAPFILL → сброс → … (24 включения за утро).
// Ночью нагрузка минимальна, и если плата всё же сбросится — это никому не помешает.
#define GAP_HOUR        4        // 04:00 местного времени
#define GAP_DAY_GAP     72000UL  // не чаще раза в 20 часов
static unsigned long sGapDailyLast = 0;   // millis() последнего автозапуска
// День последнего автозапуска (20261101). Раньше это был static int — он
// обнулялся при каждом REBOOT, и в 04:xx GAPFILL запускался заново после
// каждого сброса (петля: сброс → GAPFILL → сброс). Теперь RTC-память
// переживает сброс, а NVS — отключение питания (пишем раз в сутки).
RTC_DATA_ATTR static uint32_t sGapDayRtc = 0;
static uint32_t sGapDayNvs = 0;
static bool     sGapDayNvsLoaded = false;

static void gapDayNvsLoad() {
  if (sGapDayNvsLoaded) return;
  sGapDayNvsLoaded = true;
  Preferences p;
  if (p.begin("gapday", true)) { sGapDayNvs = p.getUInt("d", 0); p.end(); }
}
static void gapDayNvsSave(uint32_t d) {
  sGapDayNvs = d;
  Preferences p;
  if (p.begin("gapday", false)) { p.putUInt("d", d); p.end(); }
}
static uint32_t gapDayStamp(const struct tm& t) {
  return (uint32_t)(t.tm_year + 1900) * 10000UL +
         (uint32_t)(t.tm_mon + 1) * 100UL + (uint32_t)t.tm_mday;
}

void gapDailyTick() {
  if (sGF.pending || sGF.phase != GF_IDLE) return;   // уже что-то идёт
  if (pwrHistActive() || gHistStep != 0) return;
  if (voltStale(180000UL)) return;                   // нет свежих данных — не начинаем

  struct tm ti;
  if (!getLocalTime(&ti, 20)) return;                // часы не выставлены
  if (ti.tm_hour != GAP_HOUR) return;                // не наш час
  gapDayNvsLoad();
  uint32_t today = gapDayStamp(ti);
  if (sGapDayRtc == today) return;                   // сегодня уже запускали
  if (sGapDayNvs == today) { sGapDayRtc = today; return; }  // NVS: после сброса питания
  if (sGapDailyLast && (millis() - sGapDailyLast) < GAP_DAY_GAP) return;

  sGapDayRtc    = today;
  gapDayNvsSave(today);                              // раз в сутки — флеш не изнашиваем
  sGapDailyLast = millis();
  Serial.printf("GAPFILL: плановый запуск %02d:%02d\n", ti.tm_hour, ti.tm_min);
  gapFillSchedule(3000);                             // через 3 с, чтобы не мешать циклу
}

void gapFillTick() {
  CloudBgScope bg;                                   // фон: без второй попытки connect
  // Сетевой бюджет прохода уже израсходован (облако/веб) — не начинаем TLS.
  if (netBudgetLeft() < 1800UL) return;
  // Напряжение устарело — сеть отдаётся облаку (cloudTick). GAPFILL — самая
  // тяжёлая задача (десятки запросов report-logs) и ждёт.
  if (voltStale(180000UL) && sGF.phase != GF_IDLE && sGF.phase != GF_COMMIT) return;
  if (sGF.phase == GF_IDLE) {
    // Отложенный запуск (после старта платы, чтобы не мешать загрузке).
    if (sGF.pending && (long)(millis() - sGF.nextMs) >= 0) {
      sGF.pending = 0;
      if (pwrHistActive() || gHistStep != 0) { sGF.pending = 1; sGF.nextMs = millis() + 15000UL; }
      else gapFillStart(true);
    }
    return;
  }
  if ((long)(millis() - sGF.nextMs) < 0) return;
  if (millis() < gTouchSkip) return;
  if (millis() - gTouchMs < 1500UL) return;

  // ---- шаг 1: поиск пропусков
  if (sGF.phase == GF_SCAN) {
    int n = gapScan();
    if (n < 0) {
      sGF.phase = GF_IDLE;
      strcpy(sGF.msg, "ошибка чтения карты");
      Serial.println("GAPFILL-ERR: не смог прочитать /energy.csv");
      return;
    }
    Serial.printf("GAPFILL: пропусков %d, суммарно %ld мин, сегодня %d (%d мин)\n",
                  n, sGF.gapSec / 60, sGF.todayN, sGF.todayMin);
    for (int i = 0; i < n; i++) {
      char s1[24], s2[24];
      gfStamp(sGf[i].a, s1, sizeof(s1));
      gfStamp(sGf[i].b, s2, sizeof(s2));
      Serial.printf("GAPFILL:   %d) %s -> %s, %.1f мин\n",
                    i + 1, s1, s2, (sGf[i].b - sGf[i].a) / 60.0);
    }
    gLastDraw = 0;                    // на экране ИТОГИ появится пометка
    if (n == 0) {
      sGF.phase = GF_IDLE;
      strcpy(sGF.msg, "пропусков нет");
      if (sGF.run) Serial.println("GAPFILL: пропусков нет — заполнять нечего");
      return;
    }
    if (!sGF.run) {
      sGF.phase = GF_IDLE;
      strcpy(sGF.msg, "dry-run: показал, не писал");
      return;
    }
    if (WiFi.status() != WL_CONNECTED) { sGF.phase = GF_IDLE; strcpy(sGF.msg, "нет Wi-Fi"); return; }
    if (!netOnline()) { sGF.phase = GF_IDLE; strcpy(sGF.msg, "нет интернета"); return; }
    sGF.tzOff = gfTzOffset();
    sGF.gi = 0;
    sGF.poolN = 0;
    sGF.phase = GF_FETCH;
    sGF.nextMs = millis() + 300UL;
    strcpy(sGF.msg, "облако: окна");
    return;
  }

  // ---- шаг 2: окна облака
  if (sGF.phase == GF_FETCH) {
    if (WiFi.status() != WL_CONNECTED) { sGF.phase = GF_IDLE; strcpy(sGF.msg, "нет Wi-Fi"); return; }
    if (!netOnline()) { sGF.phase = GF_IDLE; strcpy(sGF.msg, "нет интернета"); return; }
    if (sGF.gi >= sGF.gaps) {
      sGF.phase = GF_COMMIT;
      sGF.nextMs = millis() + 100UL;
      return;
    }
    GapItem& g = sGf[sGF.gi];
    if (g.st == 0) {                       // начать пропуск
      g.row0 = (uint16_t)sGF.poolN;
      g.rowN = 0;
      g.srcE = 0;
      g.st = 1;
      g.w = 0;
      long span = g.b - g.a;
      long wn = (span + GF_WIN - 1) / GF_WIN;
      if (wn < 1) wn = 1;
      if (wn > GF_MAX_WIN) wn = GF_MAX_WIN;
      g.wN = (uint16_t)wn;
      sGF.nEn = 0;
      sGF.nTmp = 0;
      sGF.retry = 0;
      char s1[24], s2[24];
      gfStamp(g.a, s1, sizeof(s1));
      gfStamp(g.b, s2, sizeof(s2));
      Serial.printf("GAPFILL: %d/%d %s -> %s, окон %d\n", sGF.gi + 1, sGF.gaps, s1, s2, (int)g.wN);
      sGF.nextMs = millis();
      return;
    }

    if (!cloudToken()) {
      sGF.phase = GF_IDLE;
      strcpy(sGF.msg, "облако не дало токен");
      Serial.println("GAPFILL-ERR: облако не дало токен");
      return;
    }
    // Токен мог занять весь бюджет — окно берём следующим проходом loop().
    if (netBudgetLeft() < 1500UL) return;

    long ws = g.a + (long)g.w * GF_WIN;
    long we = ws + GF_WIN;
    if (we > g.b) we = g.b;
    // В запрос — UTC: вычитаем местное смещение, иначе окно уедет на +3 часа.
    long wsU = ws - sGF.tzOff;
    long weU = we - sGF.tzOff;
    char path[280];
    snprintf(path, sizeof(path),
             "/v1.0/devices/%s/report-logs?codes=phase_a,temp_current,total_forward_energy"
             "&end_time=%llu&size=100&start_time=%llu",
             DEVICE_ID, (unsigned long long)weU * 1000ULL, (unsigned long long)wsU * 1000ULL);
    String body;
    if (repWait()) return;                        // общий лимит report-logs
    bool okGf = cloudCall("GET", String(path), body, 14000);
    repDone();
    if (!okGf) {
      if (++sGF.retry >= 3) {
        sGF.phase = GF_IDLE;
        strcpy(sGF.msg, "облако не ответило 3 раза");
        Serial.println("GAPFILL-ERR: облако не ответило 3 раза");
        return;
      }
      sGF.nextMs = millis() + 2500UL;
      return;
    }
    if (body.indexOf("\"success\":true") < 0) {
      if (body.indexOf("too frequent") >= 0) {
        sGF.retry = 0;
        sGF.nextMs = millis() + 10000UL;
        return;
      }
      if (++sGF.retry >= 3) {
        sGF.phase = GF_IDLE;
        strcpy(sGF.msg, "облако вернуло ошибку");
        return;
      }
      sGF.nextMs = millis() + 3000UL;
      return;
    }
    sGF.retry = 0;

    int nph = 0, nt = 0, ne = 0;
    int before = sGF.poolN;
    gfParseWindow(body, g, &nph, &nt, &ne);
    g.rowN = (uint16_t)(sGF.poolN - g.row0);
    int hm = body.indexOf("\"has_more\":");
    if (hm >= 0) {
      int hp = hm + 11;
      while (hp < (int)body.length() && (body[hp] == ' ' || body[hp] == '"')) hp++;
      if (body.substring(hp, hp + 4) == "true")
        Serial.println("GAPFILL: внимание, окно обрезано облаком (has_more)");
    }
    Serial.printf("GAPFILL:   окно %d/%d — phase_a %d, темп %d, счётчик %d, строк %d\n",
                  (int)g.w + 1, (int)g.wN, nph, nt, ne, sGF.poolN - before);
    sGF.nextMs = millis() + 1300UL;
    if (++g.w >= g.wN) {
      gfFinalize(g);
      g.st = 2;
      char s1[24], s2[24];
      gfStamp(g.a, s1, sizeof(s1));
      gfStamp(g.b, s2, sizeof(s2));
      Serial.printf("GAPFILL: %s -> %s готово, строк %d (счётчик из облака у %d)\n",
                    s1, s2, (int)g.rowN, (int)g.srcE);
      sGF.gi++;
      sGF.nextMs = millis() + 700UL;
    }
    return;
  }

  // ---- шаг 3: запись в файл
  if (sGF.phase == GF_COMMIT) {
    gGapBusy = true;
    int ins = 0;
    bool ok = gfCommit(&ins);
    gGapBusy = false;
    sGF.inserted = ins;
  sGF.insLast   = ins;
    sGF.phase = GF_IDLE;
    if (ok) {
      sGF.insTotal += ins;
      strcpy(sGF.msg, "готово");
      Serial.printf("GAPFILL-DONE: вставлено %d строк; /energy.bak цел\n", ins);
      gapScan();                      // пометка о пропусках — по новому файлу
    } else {
      strcpy(sGF.msg, "не вышло, файл не тронут");
      Serial.println("GAPFILL: заполнение не удалось — /energy.csv не тронут");
    }
    gLastDraw = 0;
    return;
  }
}

// Отчёт о найденных пропусках в JSON — для /gapfill (dry-run).
// Кэш: gapScan() читает весь файл (~0.5-1.1 с) прямо в обработчике. Отчёт
// зависит только от содержимого файла, поэтому годен, пока размер не изменился.
// 10 секунд — чтобы веб-страница, дёргающая /gapfill при каждом открытии,
// не заставляла плату читать 0.5 МБ заново.
static String sGapJsonCache;
static size_t sGapJsonSize = 0;
static unsigned long sGapJsonAt = 0;
#define GAPJSON_TTL 10000UL

String gapFillJson() {
  String out;
  long nowE = nowLocalEpoch();
  out.reserve(1200);
  out = "{\"ok\":true,\"n\":";
  out += String(sGF.gaps);
  out += ",\"sec\":";
  out += String(sGF.gapSec);
  out += ",\"min\":";
  out += String((int)((sGF.gapSec + 59) / 60));
  out += ",\"todayN\":";
  out += String(sGF.todayN);
  out += ",\"todayMin\":";
  out += String(sGF.todayMin);
  out += ",\"active\":";
  out += String(gapFillActive() ? 1 : 0);
  out += ",\"inserted\":";
  out += String(sGF.insTotal);
  out += ",\"gaps\":[";
  for (int i = 0; i < sGF.gaps; i++) {
    char s1[24], s2[24];
    gfStamp(sGf[i].a, s1, sizeof(s1));
    gfStamp(sGf[i].b, s2, sizeof(s2));
    if (i) out += ',';
    out += "{\"a\":\"";
    out += s1;
    out += "\",\"b\":\"";
    out += s2;
    out += "\",\"min\":";
    out += String((int)((sGf[i].b - sGf[i].a + 30) / 60));
    // can=1 — пропуск уже старше 5 минут, его можно заполнять (пауза свежая
    // дыра может быть ещё в процессе записи)
    out += ",\"age\":";
    out += String(nowE > sGf[i].b ? (long)(nowE - sGf[i].b) : 0L);
    out += ",\"can\":";
    out += String((nowE - sGf[i].b) >= GF_AGE ? 1 : 0);
    out += '}';
  }
  out += "]}";
  return out;
}

void webSetup() {
  gWeb.on("/", []() { pageSend(); });

  gWeb.on("/data", []() {
    char b[2400];   // запас: ответ ~800 Б, +новые поля
    char nows[24] = "";
    struct tm ti;
    if (getLocalTime(&ti, 10))
      snprintf(nows, sizeof(nows), "%04d-%02d-%02d %02d:%02d:%02d",
               ti.tm_year + 1900, ti.tm_mon + 1, ti.tm_mday,
               ti.tm_hour, ti.tm_min, ti.tm_sec);
    // Возраст и epoch-время получения для КАЖДОГО показателя. Пока значение
    // ни разу не приходило (gXxxMs == 0) — возраст и время равны нулю.
    unsigned long aV = (millis() - gPhaseMs) / 1000UL;
    unsigned long aT = gTempMs   ? (millis() - gTempMs)   / 1000UL : 0;
    unsigned long aP = gPowerMs  ? (millis() - gPowerMs)  / 1000UL : 0;
    unsigned long aE = gEnergyMs ? (millis() - gEnergyMs) / 1000UL : 0;
    // cAge — сколько секунд назад была последняя ПОПЫТКА облака (не удача).
    unsigned long aC = gCloudTryMs ? (millis() - gCloudTryMs) / 1000UL : 0;
    time_t nowT = time(nullptr);
    bool clockOk = (nowT > 1700000000L);        // время синхронизировано по NTP
    long atV = (gPhaseMs  && clockOk) ? (long)(nowT - aV) : 0;
    long atT = (gTempMs   && clockOk) ? (long)(nowT - aT) : 0;
    long atP = (gPowerMs  && clockOk) ? (long)(nowT - aP) : 0;
    long atE = (gEnergyMs && clockOk) ? (long)(nowT - aE) : 0;
    snprintf(b, sizeof(b),
      "{\"v\":%.0f,\"temp\":%.1f,\"p\":%.1f,\"amp\":%.2f,\"e\":%.2f,"
      "\"r\":%s,\"tkwh\":%.3f,\"tcost\":%.2f,\"ykwh\":%.3f,\"ykost\":%.2f,"
      "\"mkwh\":%.2f,\"mcost\":%.2f,\"tariff\":%.2f,\"tday\":%.2f,\"tnight\":%.2f,\"rssi\":%d,\"sdRows\":%d,"
      "\"vAge\":%lu,\"tAge\":%lu,\"pAge\":%lu,\"eAge\":%lu,"
      "\"vAt\":%ld,\"tAt\":%ld,\"pAt\":%ld,\"eAt\":%ld,\"stale\":%s,"
      "\"tapRawX\":%d,\"tapRawY\":%d,\"tapX\":%d,\"tapY\":%d,\"chartHour\":%d,"
      "\"calYL\":%d,\"calYR\":%d,\"calXT\":%d,\"calXB\":%d,\"tlN\":%d,\"tl\":[[%d,%d,%d,%d],[%d,%d,%d,%d],[%d,%d,%d,%d],[%d,%d,%d,%d],[%d,%d,%d,%d],[%d,%d,%d,%d],[%d,%d,%d,%d],[%d,%d,%d,%d]],"
      "\"net\":%s,\"heap\":%u,\"up\":%lu,\"redraw\":%lu,\"drawN\":%lu,\"winRefresh\":%lu,"
      "\"ph\":%d,\"phDay\":%d,\"phHour\":%d,\"phOk\":%d,\"phMiss\":%d,"
      "\"gaps\":%d,\"gapMin\":%d,\"gapN\":%d,\"gapFillOk\":%d,\"gapAct\":%d,\"gapPh\":%d,"
      "\"day\":%d,\"dayMs\":%lu,\"dayRows\":%d,\"pageN\":%lu,\"pageMs\":%lu,"
      "\"pageSend\":%lu,\"pageCut\":%lu,"
      "\"pageAge\":%lu,"
      "\"loopMs\":%lu,\"loopMaxMs\":%lu,\"loopMaxAll\":%lu,"
      "\"sdMs\":%lu,\"gapMs\":%lu,\"histMs\":%lu,\"apiN\":%lu,"
      "\"cTry\":%lu,\"cOk\":%lu,\"tk\":%lu,\"ns\":%d,\"phf\":%d,\"wfs\":%d,"
      // Диагностика цепочки облака: почему не пришло напряжение.
      "\"cWhy\":%d,\"cAge\":%lu,\"cFail\":%lu,\"cliSt\":%d,\"cliHttp\":%d,"
      "\"cliLen\":%d,\"cliWant\":%d,\"vSeen\":%d,\"maxAlloc\":%u,\"free\":%u,"
      // rr — причина последней перезагрузки (esp_reset_reason), rrn — сколько
      // стартов за сутки, rrR — «сырой» код RTC (для сверки с rr).
      "\"rr\":%d,\"rrn\":%lu,\"rrR\":%d,"
      "\"blk\":\"%s\",\"blkMs\":%lu,"
      "\"msCloud\":%lu,\"msTuya\":%lu,\"msWrite\":%lu,\"creuse\":%lu,"
      "\"p1\":%d,\"p2\":%d,\"ty\":%d,\"msDns\":%lu,\"msTls\":%lu,\"phN\":%lu,\"phR\":%lu,"
      "\"disp\":%d,\"bright\":%d,\"wake\":%lu,\"dtouch\":%d,"
      // Энергосбережение: idle — минут до гашения, idleMs — сколько осталось
      // до гашения, dispOff — сколько раз экран гас по таймеру.
      "\"idle\":%d,\"idleMs\":%lu,\"dispOff\":%lu,"
      "\"beep\":%d,\"vol\":%d,\"led\":%d,"
      "\"mq\":%d,\"mqN\":%lu,\"mqFail\":%lu,"
      // Погода (OpenWeatherMap): видно с ПК, что пришло и сколько осталось heap
      "\"wOk\":%d,\"wT\":%.1f,\"wH\":%.0f,\"wW\":%.1f,\"wCode\":%d,\"wCodeT\":%d,\"wHeap\":%u,\"wDesc\":\"%s\","
      // Прогноз OWM: вероятность дождя сейчас и завтра, диапазон температур
      // завтра, короткое описание, осадки за завтра и heap после запроса.
      "\"wPop\":%.0f,\"wPopT\":%.0f,\"wTminT\":%.1f,\"wTmaxT\":%.1f,"
      "\"wDescT\":\"%s\",\"wRainT\":%.1f,\"wfHeap\":%u,"
      "\"now\":\"%s\"}",
      gVoltage, gTemp, gPower, gCurrent, gEnergy,
      gRelay ? "true" : "false", myTodayKwh, myTodayCost, yKwh, yCost,
      mKwh, mCost, measuredTariff(), TARIFF_DAY, TARIFF_NIGHT, gRssi, gSdRows,
      aV, aT, aP, aE, atV, atT, atP, atE,
      ((millis() - gPhaseMs) > 180000UL) ? "true" : "false",
      gTapRawX, gTapRawY, gTapMapX, gTapMapY, gChartHour,
      calYleft, calYright, calXtop, calXbottom, gTLn,
      gTLraw[0][0],gTLraw[0][1],gTLmap[0][0],gTLmap[0][1],
      gTLraw[1][0],gTLraw[1][1],gTLmap[1][0],gTLmap[1][1],
      gTLraw[2][0],gTLraw[2][1],gTLmap[2][0],gTLmap[2][1],
      gTLraw[3][0],gTLraw[3][1],gTLmap[3][0],gTLmap[3][1],
      gTLraw[4][0],gTLraw[4][1],gTLmap[4][0],gTLmap[4][1],
      gTLraw[5][0],gTLraw[5][1],gTLmap[5][0],gTLmap[5][1],
      gTLraw[6][0],gTLraw[6][1],gTLmap[6][0],gTLmap[6][1],
      gTLraw[7][0],gTLraw[7][1],gTLmap[7][0],gTLmap[7][1],
      gNetOk ? "true" : "false", ESP.getFreeHeap(),
      millis() / 1000UL, gRedrawN, gDrawN, gWinRefreshN,
      sPH.active ? 1 : 0, sPH.day, sPH.hour, sPH.hoursOk, sPH.hoursMissing,
      // gaps/gapMin — пропуски СЕГОДНЯ (метка на экране ИТОГИ и в вебе),
      // gapN — сколько нашёл последний проход, gapFillOk — сколько вставлено.
      gapTodayCount(), gapTodayMinutes(), sGF.gaps, sGF.insTotal,
      gapFillActive() ? 1 : 0, (int)sGF.phase,
      gDayOffset, (unsigned long)gDayMs, (gDayOffset > 0) ? gDayRows : 0,
      sPageBuildN, sPageBuildMs,
      sPageSendMs,
      sPageCutN,
      sPageAt ? (millis() - sPageAt) / 1000UL : 0UL,
      gLoopLast, gLoopMaxWin, gLoopMaxAll,
      gMsSd, gMsGap, gMsHist, gApiN,
      gCloudTry, gCloudOkN, gTokenN, gNetStep, gPwrFillHour, gWifiSetState,
      gCloudWhy, aC, gCloudFailRun, gCliStage, gCliHttp,
      gCliLen, gCliWant, gPhaseMs ? 1 : 0, ESP.getMaxAllocHeap(), ESP.getFreeHeap(),
      gRRcode, gRRn, gRRraw,
      gBlkName, gBlkMs,
      gMsCloud, gMsTuya, gMsWrite, gCloudReuse,
      gProbe1 ? 1 : 0, gProbe2 ? 1 : 0, gTuyaOk ? 1 : 0, gMsDns, gMsTls, gPhaseN, gPhaseRelayN,
      gDispOn ? 1 : 0, gDispOn ? gDispBright : 0,
      dispWakeActive() ? (unsigned long)((gDispWakeUntil - millis()) / 1000UL) : 0,
      gDispTouch ? 1 : 0, (int)gDispIdleMin, (unsigned long)dispIdleLeftMs(),
      (unsigned long)gDispOffN,
      gBeepOn ? 1 : 0, gBeepVol, gLedState,
      mqUp ? 1 : 0, mqMsgN, mqFailN,
      wOk ? 1 : 0, wTemp, wHum, wWind, wCode, wCodeTom, wHeap, wDescRu.c_str(),
      wPopNow, wPopTom, wTminTom, wTmaxTom, wDescTom.c_str(), wRainTom, wfHeap,
      nows);
    gWeb.send(200, "application/json", String(b));
  });

  // /mqtt — состояние MQTT: сервер, подключение, счётчики, последняя публикация.
  // cfg=1 — настройки прочитаны (secrets.h + NVS), disc — сколько конфигов HA
  // отправлено из discN; lastAge — сколько секунд назад была последняя публикация.
  //
  // Настройка БЕЗ перепрошивки и без serial (порт занят монитором):
  //   /mqtt?host=192.168.1.72&port=1883&user=mqtt&pass=***&base=dom_energy
  //   /mqtt?off=1  — выключить MQTT (host в NVS пустой, поведение прежнее)
  gWeb.on("/mqtt", []() {
    if (gWeb.hasArg("off") || gWeb.hasArg("host")) {
      // Пустой host раньше молча ВЫКЛЮЧАЛ MQTT (в NVS писался ""). Теперь это
      // ошибка 400, настройки не меняются; выключение — только /mqtt?off=1.
      String hNew = gWeb.hasArg("host") ? gWeb.arg("host") : String();
      hNew.trim();
      if (!gWeb.hasArg("off") && hNew.length() == 0) {
        gWeb.send(400, "application/json; charset=utf-8", "{\"ok\":false,\"err\":\"empty host\"}");
        return;
      }
      Preferences p;
      if (!p.begin(MQ_CFG_NS, false)) {
        gWeb.send(500, "application/json", "{\"error\":\"nvs\"}");
        return;
      }
      if (gWeb.hasArg("off")) {
        p.putString("host", "");
        Serial.println("MQTTSET(web): выключен");
      } else {
        p.putString("host", hNew);
        if (gWeb.hasArg("port")) p.putLong("port", gWeb.arg("port").toInt());
        if (gWeb.hasArg("user")) p.putString("user", gWeb.arg("user"));
        if (gWeb.hasArg("pass")) p.putString("pass", gWeb.arg("pass"));
        if (gWeb.hasArg("base")) p.putString("base", gWeb.arg("base"));
        Serial.printf("MQTTSET(web): host=%s\n", hNew.c_str());
      }
      p.end();
      mqCfgLoaded = false;
      mqUp = false;
      mqNet.stop();
      mqLastTry = 0;          // попытка сразу, без ожидания 30 с
      mqDiscIdx = 0;
      mqLastState[0] = 0;
      mqCfgLoad();
    }
    char b[640];
    unsigned long ago = mqLastOkMs ? (millis() - mqLastOkMs) / 1000UL : 0;
    snprintf(b, sizeof(b),
      "{\"on\":%d,\"host\":\"%s\",\"port\":%u,\"base\":\"%s\",\"user\":\"%s\","
      "\"up\":%d,\"cfg\":%d,\"ok\":%lu,\"fail\":%lu,\"msg\":%lu,\"pub\":%lu,"
      "\"disc\":%d,\"discN\":%d,\"last\":\"%s\",\"lastAge\":%lu,"
      "\"err\":\"%s\",\"heap\":%u}",
      mqHost[0] ? 1 : 0, mqHost, (unsigned)mqPort, mqBase, mqUser[0] ? mqUser : "",
      mqUp ? 1 : 0, mqCfgLoaded ? 1 : 0, mqOkN, mqFailN, mqMsgN, mqPubN,
      mqDiscIdx, HA_N, mqLastTop, ago, mqErr, ESP.getFreeHeap());
    gWeb.send(200, "application/json; charset=utf-8", String(b));
  });

  // Почасовая история за день: /history?date=YYYY-MM-DD
  gWeb.on("/history", []() {
    // мусорная дата не должна молча отдавать сегодняшние данные
    char date[12];
    bool ok = false;
    if (gWeb.hasArg("date")) {
      String d = gWeb.arg("date");
      // Полная календарная проверка: 0000-00-00, 2026-13-45, 2030-12-31 — 400.
      if (!dateValid(d.c_str(), d.length())) {
        gWeb.send(400, "application/json; charset=utf-8", "{\"ok\":false,\"err\":\"bad date\"}");
        return;
      }
      memcpy(date, d.c_str(), 10);
      date[10] = 0;
      ok = true;
    }
    if (!ok) {
      struct tm ti;
      if (getLocalTime(&ti, 10)) {
        snprintf(date, sizeof(date), "%04d-%02d-%02d",
                 ti.tm_year + 1900, ti.tm_mon + 1, ti.tm_mday);
        ok = true;
      }
    }
    if (!ok) {
      gWeb.send(400, "application/json; charset=utf-8", "{\"error\":\"date\"}");
      return;
    }
    unsigned long ms = 0;
    bool trunc = false;
    // Кэш: тот же день и тот же размер файла — отдаём готовый JSON, карту не
    // трогаем. Иначе каждый заход = чтение до 1 МБ внутри handleClient.
    size_t csz = csvFileSize();
    bool isToday = false;
    {
      struct tm ti2;
      if (getLocalTime(&ti2, 5)) {
        char td[12];
        snprintf(td, sizeof(td), "%04d-%02d-%02d",
                 ti2.tm_year + 1900, ti2.tm_mon + 1, ti2.tm_mday);
        isToday = (strcmp(td, date) == 0);
      }
    }
    unsigned long ttl = isToday ? HIST_TTL_TODAY : HIST_TTL_PAST;
    if (sHistAt != 0 && sHistJsonOut.length() > 0 && sHistFileSize == csz &&
        strcmp(sHistDate, date) == 0 && millis() - sHistAt < ttl) {
      gWeb.send(200, "application/json; charset=utf-8", sHistJsonOut);
      return;
    }
    if (!histScan(date, &ms, &trunc)) {
      gWeb.send(500, "application/json; charset=utf-8", "{\"error\":\"sd\"}");
      return;
    }
    gMsHist = ms;
    String outHist = histJson(date, ms, trunc);
    if (!trunc) {
      sHistJsonOut = outHist;
      memcpy(sHistDate, date, 10);
      sHistDate[10] = 0;
      sHistFileSize = csz;
      sHistAt = millis();
    } else {
      // Усечённый ответ (?trunc) НЕ кэшируем: иначе он на 5 минут залипнет
      // и вернётся на запрос без trunc.
      histCacheDrop();
    }
    gWeb.send(200, "application/json; charset=utf-8", outHist);
  });

  // Список дат с данными (свежие первыми, до 30)
  gWeb.on("/days", []() {
    if (!gSdOk) {
      gWeb.send(500, "application/json; charset=utf-8", "{\"days\":[]}");
      return;
    }
    size_t sz = csvFileSize();
    if (!daysFresh(sz)) {
      unsigned long ms = 0;
      size_t scanned = 0;
      if (!scanDays(&scanned, &ms)) {
        gWeb.send(500, "application/json; charset=utf-8", "{\"days\":[]}");
        return;
      }
      sDaysFileSize = scanned;
      sDaysAt = millis();
      buildDaysJson(ms);
    }
    gWeb.send(200, "application/json; charset=utf-8", sDaysJson);
  });

  // Последние переключения реле (по 5-му полю истории)
  // История перезагрузок: /boots -> последние 20 строк /boots.log.
  // Отдельный файл специально: /energy.csv не трогаем, у него строгие 7 полей.
  gWeb.on("/boots", []() {
    String out;
    out += "rr=" + String(rrName(gRRcode)) + "(" + String(gRRcode) + ") rrn=" +
           String(gRRn) + " raw=" + String(gRRraw) + "\n";
    if (!gSdOk) { out += "нет карты\n"; gWeb.send(200, "text/plain; charset=utf-8", out); return; }
    File f = SD.open("/boots.log", FILE_READ);
    if (!f) { out += "нет /boots.log\n"; gWeb.send(200, "text/plain; charset=utf-8", out); return; }
    String ring[20];
    int n = 0, pos = 0;
    while (f.available()) {
      String l = f.readStringUntil('\n');
      l.trim();
      if (!l.length()) continue;
      ring[pos] = l;
      pos = (pos + 1) % 20;
      if (n < 20) n++;
      esp_task_wdt_reset();
    }
    f.close();
    for (int i = 0; i < n; i++) {
      int k = (n == 20) ? (pos + i) % 20 : i;
      out += ring[k];
      out += "\n";
    }
    gWeb.send(200, "text/plain; charset=utf-8", out);
  });

  gWeb.on("/events", []() {
    if (!gSdOk) {
      gWeb.send(500, "application/json; charset=utf-8", "{\"events\":[]}");
      return;
    }
    size_t sz = csvFileSize();
    if (!eventsFresh(sz)) {
      uint32_t ts[10];
      uint8_t rs[10];
      size_t scanned = 0;
      unsigned long ms = 0;
      int n = scanEvents(ts, rs, &scanned, &ms);
      if (n < 0) {
        gWeb.send(500, "application/json; charset=utf-8", "{\"events\":[]}");
        return;
      }
      sEvFileSize = scanned;
      sEvAt = millis();
      buildEventsJson(ts, rs, n, ms);
    }
    gWeb.send(200, "application/json; charset=utf-8", sEvJson);
  });

  // Диагностика тача без COM-порта: /tap?x=120&y=82 возвращает номер экрана.
  // Нужна для проверки диспетчера, когда USB-порт занят монитором.
  gWeb.on("/tap", []() {
    if (!gWeb.hasArg("x") || !gWeb.hasArg("y")) {
      gWeb.send(400, "application/json", "{\"err\":\"x,y\"}");
      return;
    }
    int x = gWeb.arg("x").toInt();
    int y = gWeb.arg("y").toInt();
    handleTapAt(x, y);
    char b[32];
    snprintf(b, sizeof(b), "{\"scr\":%d}", gScreen);
    gWeb.send(200, "application/json", String(b));
  });

  // Смена сети БЕЗ USB: /wifiset?ssid=...&pass=...
  // Нужно для проверки: чтобы отправить команду по serial, компьютер открывает
  // COM5, а это дёргает DTR/RTS и ПЕРЕЗАГРУЖАЕТ плату — потом это выглядит как
  // «плата перезагрузилась при смене сети», хотя виноват USB, а не прошивка.
  gWeb.on("/wifiset", []() {
    if (!gWeb.hasArg("ssid") || gWeb.arg("ssid").length() == 0) {
      gWeb.send(200, "application/json; charset=utf-8",
                "{\"ok\":false,\"err\":\"нужен ?ssid=...&pass=...\"}");
      return;
    }
    if (gWifiSetState != 0) {
      char b[80];
      snprintf(b, sizeof(b), "{\"ok\":false,\"err\":\"busy\",\"state\":%d}", gWifiSetState);
      gWeb.send(200, "application/json; charset=utf-8", String(b));
      return;
    }
    String cmd = "WIFISET \"" + gWeb.arg("ssid") + "\" \"" + gWeb.arg("pass") + "\"";
    gWifiSetPendingCmd = cmd;          // запустим СЛЕДУЮЩИМ проходом loop()
    gWeb.send(200, "application/json; charset=utf-8",
              "{\"ok\":true,\"started\":true,\"ssid\":\"" + gWeb.arg("ssid") + "\"}");
  });

  // Запуск PWRHIST без USB: /pwrhist?range=2026-10-23%202026-10-29  // Прогресс — в /data (ph, phDay, phHour, phOk, phMiss) и в serial.
  gWeb.on("/pwrhist", []() {
    String args = gWeb.hasArg("range") ? gWeb.arg("range") : "";
    bool ok = pwrHistStart(args);
    if (!ok) {
      String why = sPH.msg[0] ? String(sPH.msg) : String("см. serial");
      gWeb.send(200, "application/json; charset=utf-8",
                "{\"ok\":false,\"err\":\"" + why + "\"}");
      return;
    }
    gWeb.send(200, "application/json", "{\"ok\":true}");
  });

  // Итог PWRHIST без COM-порта: чем кончилось и на каком этапе облако.
  // Обработчик ОДИН (см. ниже, у /gapfill/status) — здесь только заготовка
  // комментария, чтобы не плодить второй gWeb.on на тот же путь.

  // Один запрос report-logs напрямую — проверить облако без COM-порта.
  // /cloudtest                — окно 20 мин, закончившееся 10 минут назад
  // /cloudtest?code=phase_a&size=100&t0=<epoch>&t1=<epoch>
  gWeb.on("/cloudtest", []() {
    time_t nowE = time(nullptr);
    if (nowE < 1600000000) {
      gWeb.send(200, "text/plain; charset=utf-8", "часы не выставлены");
      return;
    }
    long t1 = gWeb.hasArg("t1") ? gWeb.arg("t1").toInt() : (long)(nowE - 600);
    long t0 = gWeb.hasArg("t0") ? gWeb.arg("t0").toInt() : (t1 - 1200);
    String code = gWeb.hasArg("code") ? gWeb.arg("code") : "phase_a";
    int size = gWeb.hasArg("size") ? gWeb.arg("size").toInt() : 100;
    char path[280];
    snprintf(path, sizeof(path),
             "/v1.0/devices/%s/report-logs?codes=%s&end_time=%llu&size=%d&start_time=%llu",
             DEVICE_ID, code.c_str(), (unsigned long long)t1 * 1000ULL, size,
             (unsigned long long)t0 * 1000ULL);
    String body;
    unsigned long q0 = millis();
    bool ok = cloudCall("GET", String(path), body, 16000);
    unsigned long qm = millis() - q0;
    char h[220];
    snprintf(h, sizeof(h),
             "ok=%d stage=%d http=%d len=%d want=%d ms=%lu\npath=%s\n---\n",
             ok ? 1 : 0, gCliStage, gCliHttp, (int)body.length(), gCliWant, qm, path);
    String out = h;
    out += body.substring(0, 800);
    gWeb.send(200, "text/plain; charset=utf-8", out);
  });

  // История последних облачных запросов (кольцо из 8) — после 2 часов работы
  // видно, были ли отказы между опросами /data.
  gWeb.on("/clog", []() {
    String out = "uptime_ms stage why http len\n";
    int n = gClRingI < 8 ? gClRingI : 8;
    for (int k = 0; k < n; k++) {
      ClRec& r = gClRing[(gClRingI - n + k) & 7];
      char l[80];
      snprintf(l, sizeof(l), "%lu %s %d %d %d\n", r.at, cliStageName(r.stage),
               (int)r.why, (int)r.http, (int)r.len);
      out += l;
    }
    out += "cTry="; out += String(gCloudTry);
    out += " cOk=";  out += String(gCloudOkN);
    out += " cFail="; out += String(gCloudFailRun);
    out += " cAge="; out += String(gCloudTryMs ? (millis() - gCloudTryMs) / 1000UL : 0);
    out += " vAge="; out += String(voltAgeMs() / 1000UL);
    out += " vSeen="; out += String(gPhaseMs ? 1 : 0);
    out += "\n";
    // PWRFILL: сколько занял последний проход и на чём кончился — по этому
    // видно, бюджет соблюдён (не больше одного TLS-запроса за проход).
    out += "pwrfill ms="; out += String(gPwrFillMs);
    out += " calls=";     out += String(gPwrFillN);
    out += " tls=";       out += String(gPwrFillTls);
    out += " stage=";     out += String(gPwrFillSt);
    out += " budget=";    out += String(netBudgetLeft());
    out += "\n";
    gWeb.send(200, "text/plain; charset=utf-8", out);
  });

  // ГДЕ именно рвётся облако: DNS -> TCP:443 -> TLS. Без этого «connect failed»
  // не отличить отказ TCP от нехватки памяти под TLS (mbedTLS просит ~16 КБ
  // одним куском, а фрагментированная куча его не даёт).
  gWeb.on("/netdiag", []() {
    String out;
    char b[300];
    IPAddress ip;
    unsigned long t0 = millis();
    int dn = WiFi.hostByName(TUYA_HOST, ip);
    snprintf(b, sizeof(b), "dns=%d ip=%s ms=%lu free=%u maxAlloc=%u\n",
             dn, ip.toString().c_str(), millis() - t0,
             ESP.getFreeHeap(), ESP.getMaxAllocHeap());
    out += b;
    // проба: можно ли взять блоки, которые нужны TLS (16 и 32 КБ)
    {
      char pr[80] = "";
      // Раньше p1 держался, пока брался p2 — тест показывал malloc32=0
      // даже когда 32 КБ были доступны (16+32=48 КБ > maxAlloc).
      void* p1 = malloc(16384);
      int ok16 = p1 ? 1 : 0;
      free(p1);
      void* p2 = malloc(32768);
      int ok32 = p2 ? 1 : 0;
      free(p2);
      void* p3 = malloc(49152);
      int ok48 = p3 ? 1 : 0;
      free(p3);
      snprintf(pr, sizeof(pr), "malloc16=%d malloc32=%d malloc48=%d\n", ok16, ok32, ok48);
      out += pr;
    }
    WiFiClient pc;
    t0 = millis();
    bool tcp = (pc.connect(ip, 443, 3000) == 1);
    snprintf(b, sizeof(b), "tcp443=%d ms=%lu\n", tcp ? 1 : 0, millis() - t0);
    out += b;
    pc.stop();
    {
      WiFiClientSecure sc;
      sc.setInsecure(); sc.setTimeout(10);
      t0 = millis();
      bool tls = (sc.connect(TUYA_HOST, 443, 3000) == 1);
      char eb[100] = "";
      sc.lastError(eb, sizeof(eb));
      snprintf(b, sizeof(b), "tls=%d ms=%lu free=%u maxAlloc=%u err=%s\n",
               tls ? 1 : 0, millis() - t0, ESP.getFreeHeap(), ESP.getMaxAllocHeap(), eb);
      out += b;
      sc.stop();
    }
    gWeb.send(200, "text/plain; charset=utf-8", out);
  });

  // Освободить кэш страницы (30 КБ) — проверить, влияет ли он на TLS.
  // sPage = String() память НЕ отдаёт (строка оставляет буфер), поэтому
  // пересоздаём объект: сначала деструктор, потом новый на том же месте.
  gWeb.on("/pagefree", []() {
    unsigned free0 = ESP.getFreeHeap();
    unsigned max0 = ESP.getMaxAllocHeap();
    pageCacheFree();
    char b[180];
    snprintf(b, sizeof(b), "free %u -> %u, maxAlloc %u -> %u\n",
             free0, ESP.getFreeHeap(), max0, ESP.getMaxAllocHeap());
    gWeb.send(200, "text/plain; charset=utf-8", String(b));
  });

  // GAPFILL: /gapfill — показать найденные пропуски (dry-run, ничего не пишет),
  // /gapfill?run=1 — запустить заполнение в фоне, /gapfill/status — прогресс.
  gWeb.on("/gapfill", []() {
    if (!gSdOk) {
      gWeb.send(500, "application/json; charset=utf-8", "{\"ok\":false,\"err\":\"sd\"}");
      return;
    }
    // Управление без COM-порта (порт занимает другая программа):
    //   /gapfill?run=0&from=2026-10-26&to=2026-10-26   — только показать
    //   /gapfill?run=1&from=2026-10-26&to=2026-10-26   — заполнить
    //   /gapfill?run=1                                  — как GAPFILL (2 суток)
    if (gWeb.hasArg("run")) {
      String a1 = gWeb.hasArg("from") ? gWeb.arg("from") : "";
      String a2 = gWeb.hasArg("to") ? gWeb.arg("to") : "";
      String args = a1;
      if (a2.length()) { args += " "; args += a2; }
      args.trim();
      bool dry = (gWeb.arg("run") != "1");
      bool ok;
      if (args.length() == 0) ok = gapFillStart(!dry);
      else if (dry) ok = gapFillCommand("LIST " + args);
      else ok = gapFillCommand(args);
      sGapJsonAt = 0;
      char b[220];
      snprintf(b, sizeof(b), "{\"ok\":%s,\"started\":%s,\"dry\":%d,\"file\":\"%s\",\"gaps\":%d,\"active\":%d}",
               ok ? "true" : "false", ok ? "true" : "false", dry ? 1 : 0,
               gGapPath, sGF.gaps, gapFillActive() ? 1 : 0);
      gWeb.send(200, "application/json; charset=utf-8", String(b));
      return;
    }
    if (gWeb.hasArg("run") && gWeb.arg("run") == "1") {
      bool ok = gapFillStart(true);
      sGapJsonAt = 0;
      char b[160];
      snprintf(b, sizeof(b), "{\"ok\":%s,\"started\":%s,\"gaps\":%d,\"active\":%d}",
               ok ? "true" : "false", ok ? "true" : "false", sGF.gaps,
               gapFillActive() ? 1 : 0);
      gWeb.send(200, "application/json; charset=utf-8", String(b));
      return;
    }
    if (gWeb.hasArg("undo") && gWeb.arg("undo") == "1") {
      // отмена: вернуть файл из проверенной копии /energy.bak
      gGapBusy = true;
      bool ok = gfRestoreBak();
      gGapBusy = false;
      sGapJsonAt = 0;
      gWeb.send(ok ? 200 : 500, "application/json; charset=utf-8",
                ok ? "{\"ok\":true,\"restored\":true}"
                   : "{\"ok\":false,\"err\":\"см. serial\"}");
      return;
    }
    // dry-run: отчёт зависит только от файла — не читаем его заново, если
    // размер не изменился и отчёту меньше 10 с.
    size_t gsz = csvFileSize();
    if (sGapJsonAt != 0 && sGapJsonCache.length() > 0 && sGapJsonSize == gsz &&
        millis() - sGapJsonAt < GAPJSON_TTL) {
      gWeb.send(200, "application/json; charset=utf-8", sGapJsonCache);
      return;
    }
    unsigned long gapT0 = millis();
    int n = gapScan();       // блокирующе, ~0.5 с: файл 400 КБ читается один раз
    gMsGap = millis() - gapT0;
    if (n < 0) {
      gWeb.send(500, "application/json; charset=utf-8", "{\"ok\":false,\"err\":\"read\"}");
      return;
    }
    gLastDraw = 0;
    sGapJsonCache = gapFillJson();
    sGapJsonSize = gsz;
    sGapJsonAt = millis();
    gWeb.send(200, "application/json; charset=utf-8", sGapJsonCache);
  });

  // Откат одним адресом: /restorebak (то же, что /gapfill?undo=1).
  gWeb.on("/restorebak", []() {
    gGapBusy = true;
    bool ok = gfRestoreBak();
    gGapBusy = false;
    gWeb.send(ok ? 200 : 500, "application/json; charset=utf-8",
              ok ? "{\"ok\":true,\"restored\":true}"
                 : "{\"ok\":false,\"err\":\"см. serial\"}");
  });

  gWeb.on("/gapfill/status", []() {
    File f = SD.open("/energy.csv", FILE_READ);
    size_t csvSz = f ? f.size() : 0; f.close();
    f = SD.open("/energy.bak", FILE_READ);
    size_t bakSz = f ? f.size() : 0; f.close();
    // размер ЦЕЛЕВОГО файла (может быть архив energy-YYYY-MM.csv)
    File tf = SD.open(gGapPath, FILE_READ);
    size_t tgtSz = tf ? tf.size() : 0; tf.close();
    char b[400];
    snprintf(b, sizeof(b),
             "{\"active\":%d,\"phase\":%d,\"gap\":%d,\"gaps\":%d,\"rows\":%d,"
             "\"ins\":%d,\"insLast\":%d,\"insTotal\":%d,\"ok\":%d,\"todayN\":%d,\"todayMin\":%d,"
             "\"csv\":%u,\"bak\":%u,\"file\":\"%s\",\"fileSz\":%u,\"msg\":\"%s\"}",
             gapFillActive() ? 1 : 0, (int)sGF.phase, sGF.gi, sGF.gaps, sGF.poolN,
             sGF.inserted, sGF.insLast, sGF.insTotal, sGF.phase == GF_IDLE && sGF.insLast > 0 ? 1 : 0,
             sGF.todayN, sGF.todayMin, (unsigned)csvSz, (unsigned)bakSz,
             gGapPath, (unsigned)tgtSz, sGF.msg);
    gWeb.send(200, "application/json; charset=utf-8", String(b));
  });

  // Прогресс/итог PWRHIST без COM-порта. Один обработчик на путь: поля
  // msg/cliSt/... добавлены к прежним csv/bak/peak, контракт не сломан.
  gWeb.on("/pwrhist/status", []() {
    File f = SD.open("/energy.csv", FILE_READ);
    size_t csvSz = f ? f.size() : 0; f.close();
    f = SD.open("/energy.bak", FILE_READ);
    size_t bakSz = f ? f.size() : 0; f.close();
    f = SD.open("/energy.peak", FILE_READ);
    size_t peakSz = f ? f.size() : 0; f.close();
    char b[520];
    snprintf(b, sizeof(b),
             "{\"active\":%d,\"day\":%d,\"hour\":%d,\"ok\":%d,\"miss\":%d,"
             "\"csv\":%u,\"bak\":%u,\"peak\":%u,"
             "\"msg\":\"%s\",\"cliSt\":%d,\"cliHttp\":%d,\"cliLen\":%d,\"cliWant\":%d}",
             sPH.active ? 1 : 0, sPH.day, sPH.hour, sPH.hoursOk, sPH.hoursMissing,
             (unsigned)csvSz, (unsigned)bakSz, (unsigned)peakSz,
             sPH.msg, gCliStage, gCliHttp, gCliLen, gCliWant);
    gWeb.send(200, "application/json; charset=utf-8", String(b));
  });

  // Полная выгрузка истории потоком — для бэкапа на компьютер/NAS
  // Выгрузка истории. Поддерживает диапазон: /full?from=<байт>&len=<байт>
  // Это снимает прежнее ограничение 17 МБ (обрезка по 120 с): большой файл
  // качается кусками по 4 МБ, каждый кусок укладывается в таймаут.
  gWeb.on("/full", []() {
    if (!gSdOk) { gWeb.send(500, "text/plain", "нет карты"); return; }
    // ?file=energy-2026-09.csv — выгрузка месячного архива.
    // Пускаем только имена energy.csv и energy-YYYY-MM.csv (без слэшей).
    String fname = "/energy.csv";
    bool explicitFile = false;
    if (gWeb.hasArg("file")) {
      String a = gWeb.arg("file");
      // Раньше неизвестное имя молча отдавало 264 КБ дефолта. Теперь — 404,
      // никакого fallback: клиент должен знать, что файла нет.
      // energy.peak / energy.bak / boots.log — чтобы полный архив снимался по HTTP
      if (a == "energy.csv" || a == "energy.peak" || a == "energy.bak" ||
          a == "boots.log" ||
          (a.startsWith("energy-") && a.endsWith(".csv") && a.length() == 18)) {
        fname = "/" + a;
        explicitFile = true;
      } else {
        gWeb.send(404, "application/json; charset=utf-8", "{\"ok\":false,\"err\":\"unknown file\"}");
        return;
      }
    }
    // По умолчанию (без ?file/from/len) отдаём ДВОЕ СУТОК: хвост архива
    // прошлого месяца + рабочий файл. Иначе 1-го числа /full — это только
    // текущий месяц, и проверка «все часы за 2 суток» не видит вчера.
    bool plain = !explicitFile && !gWeb.hasArg("from") && !gWeb.hasArg("len");
    struct Part { char path[40]; size_t from; size_t want; };
    Part parts[2];
    int np = 0;
    size_t totalBytes = 0;
    if (plain) {
      char arch[40] = "";
      if (gSdMonth >= 0) {
        char a2[40];
        historyName(gSdMonth - 1, a2, sizeof(a2));
        if (SD.exists(a2)) snprintf(arch, sizeof(arch), "%s", a2);
      }
      if (arch[0]) {
        File a = SD.open(arch, FILE_READ);
        if (a) {
          size_t asz = a.size();
          size_t off = (asz > 150000UL) ? (asz - 150000UL) : 0;
          if (off > 0) {                     // выравниваем по началу строки
            a.seek(off);
            int cnt = 0;
            while (a.available() && cnt < 300) { int c = a.read(); cnt++; if (c == '\n') break; }
            off += (size_t)cnt;
          }
          a.close();
          snprintf(parts[np].path, sizeof(parts[np].path), "%s", arch);
          parts[np].from = off;
          parts[np].want = asz - off;
          if (parts[np].want) np++;
        }
      }
      File c = SD.open("/energy.csv", FILE_READ);
      if (!c) { gWeb.send(500, "text/plain", "нет файла"); return; }
      size_t csz = c.size();
      c.close();
      snprintf(parts[np].path, sizeof(parts[np].path), "/energy.csv");
      parts[np].from = 0;
      parts[np].want = csz;
      np++;
      for (int i = 0; i < np; i++) totalBytes += parts[i].want;
    } else {
      File f0 = SD.open(fname, FILE_READ);
      if (!f0) { gWeb.send(500, "text/plain", "нет файла"); return; }
      size_t total = f0.size();
      f0.close();
      size_t from = 0, want = total;
      if (gWeb.hasArg("from")) from = (size_t)strtoul(gWeb.arg("from").c_str(), NULL, 10);
      if (gWeb.hasArg("len"))  want = (size_t)strtoul(gWeb.arg("len").c_str(), NULL, 10);
      if (from > total) from = total;
      size_t remain = total - from;
      if (want > remain) want = remain;
      snprintf(parts[0].path, sizeof(parts[0].path), "%s", fname.c_str());
      parts[0].from = from;
      parts[0].want = want;
      np = 1;
      totalBytes = want;
      char hdr[80];
      snprintf(hdr, sizeof(hdr), "%u/%u", (unsigned)total, (unsigned)(from + want));
      gWeb.sendHeader("X-File-Size", hdr);
      gWeb.sendHeader("Content-Disposition", "attachment; filename=" + fname.substring(1));
    }
    gWeb.setContentLength(totalBytes);
    gWeb.send(200, "text/csv; charset=utf-8", "");

    unsigned long t0 = millis();
    size_t sentAll = 0;
    for (int pi = 0; pi < np; pi++) {
      File f = SD.open(parts[pi].path, FILE_READ);
      if (!f) break;
      if (parts[pi].from > 0) f.seek(parts[pi].from);
      uint8_t buf[512];
      size_t want = parts[pi].want;
      size_t sent = 0;
      while (sent < want) {
        size_t n = f.read(buf, sizeof(buf));
        if (n == 0) break;
        if (n > want - sent) n = want - sent;
        // write() может принять МЕНЬШЕ, чем просили (буфер TCP полон, Wi-Fi
        // просел). Раньше результат не проверялся — и выгрузка молча теряла
        // хвост: IncompleteRead 292 209 из 293 233 байт.
        size_t off = 0;
        while (off < n) {
          size_t w = gWeb.client().write(buf + off, n - off);
          if (w == 0) {
            if (!gWeb.client().connected()) break;
            delay(1);
            esp_task_wdt_reset();
            continue;
          }
          off += w;
        }
        sent += off;
        esp_task_wdt_reset();
        if (off < n) break;                    // клиент отвалился на середине
        if (((sentAll + sent) & 0x7FFF) < 512) yield();   // каждые 32 КБ
        if (millis() - t0 > 100000UL) break;   // кусок не больше 100 с
      }
      f.close();
      sentAll += sent;
      if (sent < want) break;
    }
    bool conn = gWeb.client().connected();
    if (!conn || sentAll != totalBytes)
      Serial.printf("веб: выгрузка ОБОРВАНА %u из %u байт (connected=%d)\n",
                    (unsigned)sentAll, (unsigned)totalBytes, conn ? 1 : 0);
    Serial.printf("веб: выгрузка %u байт из %d файлов\n", (unsigned)sentAll, np);
  });

  gWeb.on("/csv", []() {
    if (!gSdOk) { gWeb.send(500, "text/plain", "нет карты"); return; }
    // Раньше отдавались только последние 3.5 КБ — это ~1 час, и проверка
    // «все часы за 2 суток заполнены» видела лишь сегодняшний хвост. Теперь
    // отдаём хвост АРХИВА прошлого месяца + рабочий файл, потоком: 130 КБ
    // строк в String не влезают (свободно ~110 КБ кучи), поэтому шлём чанками
    // по 8 КБ.
    char prevArch[40] = "";
    if (gSdMonth >= 0) {
      char a2[40];
      historyName(gSdMonth - 1, a2, sizeof(a2));
      if (SD.exists(a2)) snprintf(prevArch, sizeof(prevArch), "%s", a2);
    }
    const char* paths[2];
    paths[0] = prevArch[0] ? prevArch : NULL;
    paths[1] = "/energy.csv";
    gWeb.setContentLength(CONTENT_LENGTH_UNKNOWN);
    gWeb.send(200, "text/plain; charset=utf-8", "");
    String chunk = "datetime,voltage,power,energy_kwh,relay,cost,temp\n";
    chunk.reserve(9000);
    const size_t TAIL = 200000UL;      // ~2 суток строк в каждом файле
    for (int pi = 0; pi < 2; pi++) {
      if (!paths[pi]) continue;
      File f = SD.open(paths[pi], FILE_READ);
      if (!f) continue;
      size_t sz = f.size();
      size_t off = (sz > TAIL) ? (sz - TAIL) : 0;
      f.seek(off);
      bool skipFirst = (off > 0);      // первая строка может быть обрезана
      int rd = 0;
      while (f.available()) {
        String l = f.readStringUntil('\n');
        if ((++rd & 511) == 0) { esp_task_wdt_reset(); yield(); }
        if (skipFirst) { skipFirst = false; continue; }
        if (!l.length() || l[0] != '2') continue;   // только строки данных
        chunk += l;
        chunk += '\n';
        if (chunk.length() > 8000) {
          gWeb.sendContent(chunk);
          chunk = "";
          esp_task_wdt_reset();
        }
      }
      f.close();
      esp_task_wdt_reset();
    }
    if (chunk.length()) gWeb.sendContent(chunk);
    gWeb.sendContent("");
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
    // Только хвост файла (~200 КБ): сутки — это ~130-150 КБ строк. Читаем
    // БЛОКАМИ, а не readStringUntil: построчный разбор 200 КБ занимал 3.2 с
    // (String на каждую строку), блочный — доли секунды.
    size_t sz = f.size();
    size_t off = (sz > 200000UL) ? (sz - 200000UL) : 0;
    f.seek(off);
    if (off > 0) f.readStringUntil('\n');     // первая строка может быть обрезана
    static char buf[4096 + 160];
    int carry = 0, rd = 0;
    while (f.available()) {
      int n = (int)f.read((uint8_t*)(buf + carry), 4096);
      if (n <= 0) break;
      int total = carry + n;
      int i = 0;
      while (i < total) {
        int nl = -1;
        for (int j = i; j < total; j++) if (buf[j] == '\n') { nl = j; break; }
        if (nl < 0) break;
        int len = nl - i;
        if (len >= 14 && memcmp(buf + i, pre, 10) == 0 &&
            buf[i + 10] == ' ' && buf[i + 13] == ':') {
          int hh = (buf[i + 11] - '0') * 10 + (buf[i + 12] - '0');
          if (hh >= 0 && hh < 24) per[hh]++;
        }
        i = nl + 1;
      }
      carry = total - i;
      if (carry > 0) memmove(buf, buf + i, carry);
      if (carry > 150) carry = 0;
      if ((++rd & 15) == 0) { esp_task_wdt_reset(); yield(); }
    }
    f.close();
    for (int i = 0; i < 24; i++) { out += String(i) + ":" + String(per[i]) + " "; }
    gWeb.send(200, "text/plain; charset=utf-8", out);
  });

  // Список файлов истории — для бэкапа рабочих и месячных архивов.
  gWeb.on("/files", []() {
    if (!gSdOk) { gWeb.send(500, "text/plain", "нет карты"); return; }
    String out = "{\"ok\":true,\"files\":[";
    bool first = true;
    File dir = SD.open("/");
    if (dir) {
      File e = dir.openNextFile();
      while (e) {
        if (!e.isDirectory()) {
          String n = e.name();
          int slash = n.lastIndexOf('/');
          if (slash >= 0) n = n.substring(slash + 1);
          if (n == "energy.csv" || n == "energy.bak" || n == "energy.peak" ||
              n == "boots.log" || n == "boots.1.log" ||
              (n.startsWith("energy-") && n.endsWith(".csv") && n.length() == 18)) {
            if (!first) out += ",";
            first = false;
            out += "{\"name\":\"" + n + "\",\"size\":" +
                   String((unsigned long)e.size()) + "}";
          }
        }
        e.close();
        e = dir.openNextFile();
      }
      dir.close();
    }
    out += "]}";
    gWeb.send(200, "application/json", out);
  });

  // Диагностика ротации. По умолчанию dry-run: только показывает,
  // нужна ли ротация. ?force=1 — выполнить (использовать осознанно).
  gWeb.on("/rotate", []() {
    if (!gSdOk) { gWeb.send(500, "text/plain", "нет карты"); return; }
    struct tm ti;
    if (!getLocalTime(&ti, 100)) { gWeb.send(500, "text/plain", "нет времени"); return; }
    int stamp = (ti.tm_year + 1900) * 12 + ti.tm_mon;
    int fileMonth = gSdMonth;
    bool need = (fileMonth >= 0 && stamp != fileMonth);
    bool force = gWeb.hasArg("force") && gWeb.arg("force") == "1";
    bool rotated = false;
    if (need && force) {
      rotated = rotateHistory(stamp, false);
      if (rotated) gSdMonth = stamp;
    }
    char b[240];
    snprintf(b, sizeof(b),
      "{\"ok\":true,\"month\":\"%04d-%02d\",\"file_month\":\"%04d-%02d\","
      "\"need\":%s,\"force\":%s,\"rotated\":%s}",
      ti.tm_year + 1900, ti.tm_mon + 1,
      fileMonth >= 0 ? fileMonth / 12 : 0, fileMonth >= 0 ? fileMonth % 12 + 1 : 0,
      need ? "true" : "false", force ? "true" : "false", rotated ? "true" : "false");
    gWeb.send(200, "application/json", String(b));
  });

  // Подсветка без USB: /disp?on=0, /disp?bright=30, /disp?touch=0, /disp?wake=1
  gWeb.on("/disp", []() {
    // Сначала ПРОВЕРЯЕМ всё, потом меняем. Раньше toInt() превращал мусор в 0:
    // /disp?on=abc гасил подсветку, ?bright=-5 уходил в off, ?touch=abc
    // отключал wake. Теперь любое плохое значение — 400 и ни одной записи в NVS.
    long vBright = 0, vOn = 0, vTouch = 0, vIdle = 0;
    bool hasBright = gWeb.hasArg("bright");
    bool hasOn     = gWeb.hasArg("on");
    bool hasTouch  = gWeb.hasArg("touch");
    bool hasIdle   = gWeb.hasArg("idle");
    bool bad = false;
    if (hasBright && (!parseIntStrict(gWeb.arg("bright"), vBright) || vBright < 0 || vBright > 100)) bad = true;
    if (hasOn     && (!parseIntStrict(gWeb.arg("on"), vOn)         || (vOn != 0 && vOn != 1)))             bad = true;
    if (hasTouch  && (!parseIntStrict(gWeb.arg("touch"), vTouch)   || (vTouch != 0 && vTouch != 1)))       bad = true;
    // idle — минуты бездействия до автогашения (энергосбережение), 0..60.
    if (hasIdle   && (!parseIntStrict(gWeb.arg("idle"), vIdle)     || vIdle < 0 || vIdle > 60))            bad = true;
    if (bad) {
      gWeb.send(400, "application/json; charset=utf-8", "{\"ok\":false,\"err\":\"bad value\"}");
      return;
    }
    if (hasBright) {
      int v = (int)vBright;
      if (v == 0) dispSet(false, 0);
      else        dispSet(true, (uint8_t)((v * 255 + 50) / 100));
    }
    if (hasOn) {
      if (vOn != 0) {
        if (gDispBright == 0) gDispBright = 255;
        dispSet(true, 0);
      } else dispSet(false, 0);
    }
    if (hasTouch) {
      gDispTouch = (vTouch != 0);
      dispSave();
    }
    if (hasIdle) {
      gDispIdleMin = (uint32_t)vIdle;
      gLastTouchMs = millis();          // отсчёт бездействия — заново
      dispSave();
    }
    if (gWeb.hasArg("wake") && gWeb.arg("wake") == "1") dispWake();
    char b[192];
    snprintf(b, sizeof(b),
      "{\"ok\":true,\"on\":%d,\"bright\":%d,\"touch\":%d,\"wake\":%lu,"
      "\"idle\":%lu,\"idleMs\":%lu,\"dispOff\":%lu}",
      gDispOn ? 1 : 0, gDispOn ? gDispBright : 0, gDispTouch ? 1 : 0,
      dispWakeActive() ? (unsigned long)((gDispWakeUntil - millis()) / 1000UL) : 0,
      (unsigned long)gDispIdleMin, (unsigned long)dispIdleLeftMs(),
      (unsigned long)gDispOffN);
    gWeb.send(200, "application/json", String(b));
  });

  // ---------------------------------------------------------------- OTA по Wi-Fi
  // POST /update с телом = .bin. После успешной записи — перезагрузка.
  gWeb.on("/update", HTTP_POST,
    []() {
      bool ok = !Update.hasError();
      gWeb.send(200, "text/plain", ok ? "OK" : "FAIL");
      if (ok) { gRestartAt = millis() + 300; }
    },
    []() {
      HTTPUpload &up = gWeb.upload();
      if (up.status == UPLOAD_FILE_START) {
        Serial.printf("OTA: старт, ожидается %u байт\n", (unsigned)up.totalSize);
        if (!Update.begin(UPDATE_SIZE_UNKNOWN)) { Update.printError(Serial); }
      } else if (up.status == UPLOAD_FILE_WRITE) {
        if (Update.write(up.buf, up.currentSize) != up.currentSize) { Update.printError(Serial); }
      } else if (up.status == UPLOAD_FILE_END) {
        if (Update.end(true)) { Serial.printf("OTA: принято %u байт\n", (unsigned)up.totalSize); }
        else { Update.printError(Serial); }
      }
    });

  gWeb.on("/reboot", []() {
    gWeb.send(200, "text/plain", "rebooting");
    gRestartAt = millis() + 200;
  });


  // ---------------------------------------------------- качество напряжения
  gWeb.on("/vlog", []() {
    String out = "{\"n\":" + String((unsigned)sVevCnt) + ",\"items\":[";
    for (uint8_t k = 0; k < sVevCnt; k++) {
      uint8_t idx = (uint8_t)((sVevHead + VEV_N - 1 - k) % VEV_N);
      if (k) out += ',';
      out += "{\"t\":" + String(sVev[idx].t) + ",\"v\":" + String(sVev[idx].v) +
             ",\"s\":" + String(sVev[idx].sec) + ",\"k\":" + String(sVev[idx].kind) + "}";
    }
    out += "]}";
    gWeb.send(200, "application/json; charset=utf-8", out);
  });

  gWeb.on("/gost", []() {
    float avg = sVn ? (float)(sVSum / sVn) : 0;
    float bad = sVn ? (100.0f * (float)sVbad / (float)sVn) : 0;
    String out = "{";
    out += "\"n\":" + String((unsigned long)sVn);
    out += ",\"vmin\":" + String(sVn ? sVmin : 0, 1);
    out += ",\"vmax\":" + String(sVn ? sVmax : 0, 1);
    out += ",\"vavg\":" + String(avg, 1);
    out += ",\"lo\":" + String(GOST_LO, 0) + ",\"hi\":" + String(GOST_HI, 0);
    out += ",\"bad\":" + String((unsigned long)sVbad);
    out += ",\"badPct\":" + String(bad, 2);
    out += ",\"events\":" + String((unsigned)sVevCnt);
    out += ",\"tuya\":\"см. /tuya\"";
    out += "}";
    gWeb.send(200, "application/json; charset=utf-8", out);
  });

  gWeb.on("/range", []() {
    if (!gWeb.hasArg("from") || !gWeb.hasArg("to")) {
      gWeb.send(400, "application/json", "{\"ok\":false,\"err\":\"from/to\"}");
      return;
    }
    String f = gWeb.arg("from"), tt = gWeb.arg("to");
    long fE = dateEpoch(f.c_str()), tE = dateEpoch(tt.c_str());
    if (!fE || !tE || tE < fE) {
      gWeb.send(400, "application/json", "{\"ok\":false,\"err\":\"bad date\"}");
      return;
    }
    long days = (tE - fE) / 86400L + 1;
    if (days > RNG_MAX) {
      gWeb.send(400, "application/json", "{\"ok\":false,\"err\":\"max 31 days\",\"days\":" + String(days) + "}");
      return;
    }
    static RngDay agg[RNG_MAX];
    // кэш: тот же диапазон и файл не вырос — отдаём без сканирования (скан дорогой)
    static char cKey[40] = "";
    static String cJson;
    static unsigned long cSize = 0, cAt = 0;
    char kbuf[40];
    snprintf(kbuf, sizeof(kbuf), "%s|%s", f.c_str(), tt.c_str());
    File probe = SD.open("/energy.csv", FILE_READ);
    unsigned long curSize = probe ? (unsigned long)probe.size() : 0;
    if (probe) probe.close();
    if (!strcmp(kbuf, cKey) && cJson.length() && curSize < cSize + 8192UL && millis() - cAt < 120000UL) {
      gWeb.send(200, "application/json; charset=utf-8", cJson);
      return;
    }
    int n = (int)days;
    for (int i = 0; i < n; i++) { agg[i].day = (uint32_t)((fE / 86400L) + i); }
    rangeReset(agg, n);

    // проходим по месяцам: текущий файл + архивы
    int y1, m1, d1, y2, m2, d2;
    sscanf(f.c_str(), "%4d-%2d-%2d", &y1, &m1, &d1);
    sscanf(tt.c_str(), "%4d-%2d-%2d", &y2, &m2, &d2);
    int stamp = y1 * 100 + m1, stampEnd = y2 * 100 + m2;
    struct tm nowTi;
    int curStamp = 0;
    if (getLocalTime(&nowTi, 20)) curStamp = (nowTi.tm_year + 1900) * 100 + nowTi.tm_mon + 1;
    int sy = stamp / 100, sm = stamp % 100;
    int ey = stampEnd / 100, em = stampEnd % 100;
    while (sy < ey || (sy == ey && sm <= em)) {
      int s = sy * 100 + sm;
      char path[40];
      if (s == curStamp) strcpy(path, "/energy.csv");
      else snprintf(path, sizeof(path), "/energy-%04d-%02d.csv", s / 100, s % 100);
      if (SD.exists(path)) rangeScanFile(path, fE, tE + 86400L - 1, agg, n);
      sm++;
      if (sm > 12) { sm = 1; sy++; }
    }

    String out = "{\"from\":\"" + f + "\",\"to\":\"" + tt + "\",\"days\":" + String(n);
    float tK = 0, tC = 0, tD = 0, tN = 0;
    uint16_t vmin = 0xFFFF, vmax = 0, pmaxAll = 0;
    for (int i = 0; i < n; i++) {
      tK += agg[i].kwh; tC += agg[i].cost; tD += agg[i].dayKwh; tN += agg[i].nightKwh;
      if (agg[i].vmin < vmin) vmin = agg[i].vmin;
      if (agg[i].vmax > vmax) vmax = agg[i].vmax;
      if (agg[i].pmax > pmaxAll) pmaxAll = agg[i].pmax;
    }
    if (vmin == 0xFFFF) vmin = 0;
    out += ",\"totals\":{\"kwh\":" + String(tK, 2) + ",\"cost\":" + String(tC, 2);
    out += ",\"dayKwh\":" + String(tD, 2) + ",\"nightKwh\":" + String(tN, 2);
    float tDC = 0, tNC = 0;
    for (int i = 0; i < n; i++) {
      float dPr, nPr; uint8_t w1, w2;
      tarForEpoch((long)agg[i].day * 86400L, &dPr, &nPr, &w1, &w2);
      tDC += agg[i].dayKwh * dPr;
      tNC += agg[i].nightKwh * nPr;
    }
    out += ",\"dayCost\":" + String(tDC, 2);
    out += ",\"nightCost\":" + String(tNC, 2);
    out += ",\"avgDayKwh\":" + String(n ? tK / n : 0, 2);
    out += ",\"vmin\":" + String(vmin) + ",\"vmax\":" + String(vmax) + ",\"pmax\":" + String(pmaxAll) + "}";
    out += ",\"rows\":[";
    for (int i = 0; i < n; i++) {
      if (i) out += ',';
      uint32_t d = agg[i].day;
      out += "{\"d\":\"" + dateFromEpoch((long)d * 86400L) + "\"";
      out += ",\"kwh\":" + String(agg[i].kwh, 3) + ",\"cost\":" + String(agg[i].cost, 2);
      float dPr, nPr; uint8_t w1, w2;
      tarForEpoch((long)agg[i].day * 86400L, &dPr, &nPr, &w1, &w2);
      out += ",\"dayKwh\":" + String(agg[i].dayKwh, 3) + ",\"nightKwh\":" + String(agg[i].nightKwh, 3);
      out += ",\"dayCost\":" + String(agg[i].dayKwh * dPr, 2) + ",\"nightCost\":" + String(agg[i].nightKwh * nPr, 2);
      out += ",\"vmin\":" + String(agg[i].vmin == 0xFFFF ? 0 : agg[i].vmin);
      out += ",\"vmax\":" + String(agg[i].vmax) + ",\"pmax\":" + String(agg[i].pmax);
      out += ",\"tmin\":" + String(agg[i].tmin == 999 ? 0 : agg[i].tmin / 10.0f, 1);
      out += ",\"tmax\":" + String(agg[i].tmax == -999 ? 0 : agg[i].tmax / 10.0f, 1);
      {
        char wdesc[40]; float wtmin = 0, wtmax = 0;
        bool okw = weatherLogFind(dateFromEpoch((long)agg[i].day * 86400L).c_str(), wdesc, sizeof(wdesc), &wtmin, &wtmax);
        out += ",\"w\":\"";
        if (okw) { for (char* c = wdesc; *c; c++) { if (*c == '"') *c = '\''; } out += wdesc; }
        out += "\",\"wtmin\":" + String(okw ? wtmin : 0, 1) + ",\"wtmax\":" + String(okw ? wtmax : 0, 1);
      }
      out += ",\"rows\":" + String(agg[i].rows) + "}";
    }
    out += "]}";
    cJson = out;
    strncpy(cKey, kbuf, sizeof(cKey) - 1);
    cKey[sizeof(cKey) - 1] = 0;
    cSize = curSize;
    cAt = millis();
    gWeb.send(200, "application/json; charset=utf-8", out);
  });

  gWeb.on("/tariff", []() {
    bool changed = false;
    if (gWeb.hasArg("reset")) {
      sTarCnt = 0;
      sTarToday[0] = 0;
      tarSeed();
      tarApplyToday();
      changed = true;
    }
    if (gWeb.hasArg("set")) {
      String s = gWeb.arg("set");          // "8.39,4.49,6,23"
      String from = gWeb.hasArg("from") ? gWeb.arg("from") : String(sTarToday);
      int c1 = s.indexOf(','); int c2 = s.indexOf(',', c1 + 1);
      int c3 = s.indexOf(',', c2 + 1);
      bool dateOk = from.length() == 10 && from[4] == '-' && from[7] == '-';
      for (int k = 0; dateOk && k < 10; k++) {
        if (k == 4 || k == 7) continue;
        if (from[k] < '0' || from[k] > '9') dateOk = false;
      }
      int mm = from.substring(5, 7).toInt(), dd = from.substring(8, 10).toInt();
      if (dateOk && (mm < 1 || mm > 12 || dd < 1 || dd > 31)) dateOk = false;
      if (!dateOk) { gWeb.send(400, "application/json", "{\"ok\":false,\"err\":\"bad date\"}"); return; }
      // запись с такой же датой заменяем, а не плодим дубликаты
      for (uint8_t i = 0; i < sTarCnt; i++) {
        if (!strcmp(sTar[i].from, from.c_str())) {
          String sv = gWeb.arg("set");
          int q1 = sv.indexOf(','), q2 = sv.indexOf(',', q1 + 1), q3 = sv.indexOf(',', q2 + 1);
          float nd = sv.substring(0, q1).toFloat();
          if (nd <= 0) { gWeb.send(400, "application/json", "{\"ok\":false,\"err\":\"bad price\"}"); return; }
          sTar[i].day = nd;
          sTar[i].night = (q2 > 0) ? sv.substring(q1 + 1, q2).toFloat() : sTar[i].day;
          sTar[i].h1 = (q2 > 0 && q3 > 0) ? (uint8_t)constrain(sv.substring(q2 + 1, q3).toInt(), 0, 23) : 6;
          sTar[i].h2 = (q3 > 0) ? (uint8_t)constrain(sv.substring(q3 + 1).toInt(), 1, 24) : 23;
          sTarToday[0] = 0; tarSave(); tarApplyToday();
          gWeb.send(200, "application/json", "{\"ok\":true,\"replaced\":1}");
          return;
        }
      }
      if (sTarCnt >= TAR_N) { gWeb.send(400, "application/json", "{\"ok\":false,\"err\":\"log full\"}"); return; }
      if (c1 > 0 && sTarCnt < TAR_N) {
        TarRec& r = sTar[sTarCnt];
        strncpy(r.from, from.c_str(), 10); r.from[10] = 0;
        r.day = s.substring(0, c1).toFloat();
        r.night = (c2 > 0) ? s.substring(c1 + 1, c2).toFloat() : r.day;
        r.h1 = (c2 > 0 && c3 > 0) ? (uint8_t)constrain(s.substring(c2 + 1, c3).toInt(), 0, 23) : 6;
        r.h2 = (c3 > 0) ? (uint8_t)constrain(s.substring(c3 + 1).toInt(), 1, 24) : 23;
        if (r.day > 0) { sTarCnt++; sTarToday[0] = 0; tarSave(); tarApplyToday(); changed = true; }
      }
    }
    String out = "{\"day\":";
    out += String(TARIFF_DAY, 2);
    out += ",\"night\":";
    out += String(TARIFF_NIGHT, 2);
    out += ",\"today\":\"" + String(sTarToday) + "\"";
    out += ",\"cnt\":" + String((unsigned)sTarCnt);
    out += ",\"log\":[";
    for (uint8_t i = 0; i < sTarCnt; i++) {
      if (i) out += ',';
      char b[96];
      snprintf(b, sizeof(b), "{\"from\":\"%s\",\"day\":%.2f,\"night\":%.2f,\"h1\":%u,\"h2\":%u}",
               sTar[i].from, sTar[i].day, sTar[i].night, (unsigned)sTar[i].h1, (unsigned)sTar[i].h2);
      out += b;
    }
    out += "]}";
    gWeb.send(200, "application/json; charset=utf-8", out);
  });

  gWeb.on("/tuya", []() {
    String out = "{\"a1\":\"" + gAlarm1 + "\",\"a2\":\"" + gAlarm2 + "\"";
    out += ",\"temp\":" + String(gTemp, 1);
    out += ",\"relay\":" + String(gRelay ? 1 : 0);
    out += ",\"e\":" + String(gEnergy, 2);
    out += ",\"note\":\"настройки автомата Tuya (alarm_set_1/2)\"}";
    gWeb.send(200, "application/json; charset=utf-8", out);
  });

  gWeb.on("/sched", []() {
    if (gWeb.hasArg("set")) {
      schParse(gWeb.arg("set"));
      schSave();
      Serial.printf("расписание: сохранено, правил %u\n", (unsigned)sSchCnt);
    }
    if (gWeb.hasArg("allow")) {
      gAllowOff = gWeb.arg("allow").toInt() != 0;
      schSave();
    }
    String out = "{\"cnt\":" + String((unsigned)sSchCnt);
    out += ",\"allowOff\":" + String(gAllowOff ? 1 : 0);
    out += ",\"rules\":[";
    for (uint8_t i = 0; i < sSchCnt; i++) {
      if (i) out += ',';
      char b[64];
      snprintf(b, sizeof(b), "{\"h\":%u,\"m\":%u,\"on\":%u,\"days\":%u,\"en\":%u}",
               (unsigned)sSch[i].h, (unsigned)sSch[i].m, (unsigned)sSch[i].on,
               (unsigned)sSch[i].days, (unsigned)sSch[i].en);
      out += b;
    }
    out += "]}";
    gWeb.send(200, "application/json; charset=utf-8", out);
  });

  gWeb.on("/protect", []() {
    bool changed = false;
    if (gWeb.hasArg("on"))    { gProtOn   = gWeb.arg("on").toInt() != 0; changed = true; }
    if (gWeb.hasArg("cut"))   { gProtCut  = gWeb.arg("cut").toInt() != 0; changed = true; }
    if (gWeb.hasArg("vmax"))  { gProtVMax = constrain(gWeb.arg("vmax").toFloat(), 210.0f, 300.0f); changed = true; }
    if (gWeb.hasArg("vmin"))  { gProtVMin = constrain(gWeb.arg("vmin").toFloat(), 120.0f, 220.0f); changed = true; }
    if (gWeb.hasArg("tmax"))  { gProtTMax = constrain(gWeb.arg("tmax").toFloat(), 40.0f, 90.0f);    changed = true; }
    if (gWeb.hasArg("pmax"))  { gProtPMax = constrain(gWeb.arg("pmax").toFloat(), 100.0f, 10000.0f); changed = true; }
    if (changed) { protSave(); Serial.println("защита: уставки сохранены"); }

    // ---- запись уставок в сам автомат Tuya (работает даже если плата выключена)
    bool applied1 = false, applied2 = false;
    bool tried = false;
    if (gWeb.hasArg("apply")) {
      tried = true;
      float vm = constrain(gWeb.arg("vmax").toFloat(), 210.0f, 300.0f);
      float vn = constrain(gWeb.arg("vmin").toFloat(), 120.0f, 220.0f);
      float tm = constrain(gWeb.arg("tmax").toFloat(), 40.0f, 90.0f);
      float im = constrain(gWeb.arg("imax").toFloat(), 1.0f, 63.0f);
      String na1 = gAlarm1, na2 = gAlarm2;
      if (na2.length()) {
        na2 = alarmPatch(na2, 3, true, (uint16_t)vm);
        na2 = alarmPatch(na2, 4, true, (uint16_t)vn);
        na2 = alarmPatch(na2, 1, true, (uint16_t)im);
      }
      if (na1.length()) na1 = alarmPatch(na1, 5, true, (uint16_t)tm);
      bool sent = tuyaWriteAlarms(na1, na2);
      if (sent) { applied1 = true; applied2 = true; gAlarm1 = na1; gAlarm2 = na2; }
      // и в нашу локальную защиту — вторым слоем
      gProtVMax = vm; gProtVMin = vn; gProtTMax = tm; gProtPMax = im * 230.0f;
      protSave();
    }

    String out = "{";
    out += "\"on\":" + String(gProtOn ? 1 : 0);
    out += ",\"cut\":" + String(gProtCut ? 1 : 0);
    out += ",\"vmax\":" + String(gProtVMax, 0);
    out += ",\"vmin\":" + String(gProtVMin, 0);
    out += ",\"tmax\":" + String(gProtTMax, 0);
    out += ",\"pmax\":" + String(gProtPMax, 0);
    out += ",\"trig\":" + String((unsigned)gProtTrigN);
    out += ",\"why\":\"" + String(gProtWhy) + "\"";
    if (tried) out += ",\"apply\":{\"a1\":" + String(applied1 ? 1 : 0) +
                         ",\"a2\":" + String(applied2 ? 1 : 0) + "}";
    out += ",\"a1\":\"" + gAlarm1 + "\",\"a2\":\"" + gAlarm2 + "\"";
    out += ",\"v\":" + String(gVoltage, 1);
    out += ",\"t\":" + String(gTemp, 1);
    out += ",\"p\":" + String(gPower, 0);
    // пробный прогон: /protect?test=270 — что было бы при таком напряжении
    if (gWeb.hasArg("test")) {
      float vv = gWeb.arg("test").toFloat();
      const char* act = "нет";
      if (vv > gProtVMax) act = "перенапряжение";
      else if (vv < gProtVMin) act = "просадка";
      out += ",\"test\":{\"v\":" + String(vv, 0) + ",\"action\":\"" + String(act) + "\"}";
    }
    out += "}";
    gWeb.send(200, "application/json; charset=utf-8", out);
  });

  gWeb.begin();
  Serial.println("веб: http://" + WiFi.localIP().toString() + "/");
  mdnsBegin();                       // плата = cyd.local
}

// ============================================================================
//  SETUP
// ============================================================================
void setup() {
  Serial.begin(115200);
  delay(1200);
  bootReasonBegin();               // причина сброса — до всего остального
  Serial.printf("\n=== ДОМ %s ===\n", FW_VER);
  Serial.printf("сброс: rr=%s(%d) raw=%d, прошлая загрузка %lu с\n",
                rrName(gRRcode), gRRcode, gRRraw, gPrevUp);

  // ---- сторож ----
  // В Arduino-core 2.0.9 сторож настроен на 5 с с panic, но подписана только
  // IDLE0, а loop() крутится на CPU1: блокировка цикла на 5 с сброса НЕ даёт
  // (проверено на живой плате 01.11.2026 — loopMaxAll вырос до 5014 мс, сброса
  // не было). Поэтому подписываем сам цикл, а порог поднимаем до 20 с: пока
  // loop() кормит сторожа — ложных сбросов нет, настоящий зависон ловится.
  // 60 с, а не 20: pwrFillTick в одном проходе уходит на 14 с (облако), и
  // порог 20 с ловил «сброс на ровном месте» прямо во время загрузки.
  // Сам цикл подписывается В КОНЦЕ setup() — иначе медленный старт (Wi-Fi,
  // NTP, карта) успевал сработать до первого loop().
  esp_task_wdt_deinit();
  esp_err_t ew = esp_task_wdt_init(60, true);
  Serial.printf("сторож: %s\n", ew == ESP_OK ? "60 с, подписка в конце setup" : "не настроен");
  esp_task_wdt_reset();

  dispLoad();          // яркость/вкл из NVS — до экрана, чтобы ночью не вспыхнуть
  protLoad();          // уставки защиты из NVS
  schLoad();           // правила планировщика
  tarLoad();          // журнал тарифов
  tarSeed();          // если пуст — заводим текущий
  tarApplyToday();    // применяем тариф на сегодня
  dispPwmBegin();      // ШИМ подсветки до tft.init()
  ledInit();           // синий на время загрузки
  gLedBootUntil = millis() + 3000;
  beepInit();          // динамик: тишина (звук по умолчанию выключен)

  tpInit();
  loadCal();
  loadWifi();

  tft.init();
  tft.setRotation(0);
  dispPwmBegin();      // TFT_eSPI трогает TFT_BL (21) — привязываем ШИМ заново
  tft.fillScreen(TFT_BLACK);

  // заставка своим шрифтом
  drawTxtC(120, 120, "ДОМ", 0x05FF, 0x0000, 6);
  drawTxtC(120, 180, "ЭНЕРГИЯ", 0xFFFF, 0x0000, 3);
  drawTxtC(120, 215, "ПОДКЛЮЧЕНИЕ...", 0x5AEB, 0x0000, 1);
  const unsigned long splashAt = millis();   // заставка держится минимум 2.5 с

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
    esp_task_wdt_reset();          // сторож: ожидание NTP до 20 с
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
  bootCountTick();                 // rrn: сколько стартов за сутки
  Serial.printf("rr=%s(%d) rrn=%lu\n", rrName(gRRcode), gRRcode, gRRn);

  pickTheme();

  gSdOk = initSD();
  if (gSdOk) {
    initHistoryMonth(); writeBootLog();   // /boots.log: по строке на старт
  }
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

  // если плата стартовала быстро, заставку всё равно держим 2.5 с
  while (millis() - splashAt < 2500) { esp_task_wdt_reset(); delay(50); }

  drawScreen();
  gLastDraw = millis();
  gPwrFillHour = 0;          // график мощности за прошлые часы — из облака
  gPwrFillStopH = -1;
  // GAPFILL: через минуту после старта добираем пропуски в /energy.csv из
  // облака (перезагрузка при прошивке, бутлоадер, потеря Wi-Fi). Именно через
  // минуту: загрузка экрана, счётчика и погоды важнее.
  // 01.11.2026: автозапуск GAPFILL УБРАН.
  // GAPFILL — самая тяжёлая операция (десятки TLS-запросов к облаку). При
  // автозапуске через 60 с после каждого старта плата попадала в петлю:
  // старт → 60 с → GAPFILL → сброс → старт → 60 с → GAPFILL → …
  // По /boots было видно 24 включения за утро. Теперь GAPFILL запускается
  // ТОЛЬКО вручную: командой GAPFILL по serial или /gapfill?run=1 по HTTP.
  // gapFillSchedule(60000);   // см. gapDailyTick() ниже: раз в сутки в 04:00
  webSetup();                 // веб-страница для проверки без USB
  sendJsonStatus();
  sendJsonData();

  // Подписываем цикл на сторожа только теперь: пока шёл setup, сторож не
  // должен валить плату за медленный Wi-Fi/NTP/карту.
  esp_task_wdt_add(NULL);
  esp_task_wdt_reset();
}

// ============================================================================
//  LOOP
// ============================================================================
void loop() {
  unsigned long gLoopT0 = millis();      // замер затупаний (см. конец loop)
  netBudgetReset();                      // сетевые TLS-запросы делят 2.6 с на проход
  static unsigned long lastPoll = 0;
  static unsigned long lastWeather = 0;
  static unsigned long lastForecast = 0;
  static unsigned long lastToday = 0;
  static unsigned long lastPeriods = 0;
  static unsigned long lastSeed = 0;
  static unsigned long lastSb = 0;
  // gNetStep теперь глобальный (gNetStep) — его видно в /data

  // 1. Касание — САМЫМ ПЕРВЫМ, до любых тяжёлых операций: раньше HTTP,
  //    облако и SD держали цикл десятками секунд, и тач «умирал».
  handleTouch();
  dispTick();               // подсветка: погасить после 20 с «касание — свет»
  dispIdleTick();           // энергосбережение: автогашение по бездействию
  ledTick();                // светодиод: цвет по состоянию + мигание 1 Гц
  beepTick();               // проигрывание пиков
  beepEvents();             // тревоги: напряжение, перегрев, карта, час
  wifiSetTick();            // WIFISET: смена сети шаг за шагом, без delay 20 с
  // ОБЛАКО — ПЕРВЫМ, до любых дозаполнений: свежее напряжение важнее
  // истории. Свой таймер (20 с), не зависит от gNetStep/PWRFILL/GAPFILL.
  { unsigned long b0 = millis(); cloudTick(); blkNote("cloud", millis() - b0); }
  // MQTT — своим тиком, чтобы не мешать облаку Tuya
  { unsigned long b0 = millis(); mqttTick(); blkNote("mqtt", millis() - b0); }
  { unsigned long b0 = millis(); pwrHistTick(); blkNote("pwrhist", millis() - b0); }
  { unsigned long b0 = millis(); pwrFillTick(); gPwrFillMs = millis() - b0; blkNote("pwrfill", gPwrFillMs); }
  { unsigned long b0 = millis(); gapDailyTick(); gapFillTick(); blkNote("gapfill", millis() - b0); }
  { // раз в 10 минут запоминаем время: пригодится после перезагрузки без сети
    static unsigned long lastClk = 0;
    if (millis() - lastClk > 600000UL) { lastClk = millis();
      unsigned long b0 = millis(); saveClock(); blkNote("nvs", millis() - b0); }
  }
  { unsigned long b0 = millis(); gWebInHandler = true; gWeb.handleClient(); gWebInHandler = false; blkNote("web", millis() - b0); }
  // Страница теперь собирается потоково и память не занимает, поэтому
  // освобождать нечего: кэш-правило (30 КБ / 15 с) больше не нужно.
  // отложенная смена сети: ответ уже ушёл клиенту, теперь можно уходить из сети
  if (gWifiSetPendingCmd.length()) {
    String c2 = gWifiSetPendingCmd;
    gWifiSetPendingCmd = "";
    cmdWifiSet(c2);
  }
  { unsigned long b0 = millis(); statsTick(); blkNote("stats", millis() - b0); }
  // История на карту — ВСЕГДА: без Wi-Fi, без интернета, без облака.
  // Данные с реле идут по локальной сети, карта от интернета не зависит.
  // histTick — ДО writeSd: если надо дозаполнить вчера, он успевает
  // придержать запись сегодняшней строки (см. gHistHold).
  { unsigned long b0 = millis(); histTick(); blkNote("hist", millis() - b0); }
  { unsigned long w0 = millis(); writeSd(); unsigned long dw = millis() - w0;
    if (dw > gMsWrite) gMsWrite = dw; blkNote("sdwrite", dw); }
  // v4.3: сторож камер отключён — wdTick() не вызываем, чтобы не
  // тратить время на TCP. Функция осталась ниже для большого экрана.
  // wdTick();
  { unsigned long b0 = millis(); netOnline(); blkNote("net", millis() - b0); }

  // 2. «сегодня» — раз в 5 с (карта читается не чаще раза в минуту)
  if (millis() - lastToday > 5000UL) {
    lastToday = millis();
    unsigned long b0 = millis(); updateToday(); blkNote("today", millis() - b0);
  }
  if (millis() - lastPeriods > 60000UL) {
    lastPeriods = millis();
    unsigned long b0 = millis(); updatePeriods(); blkNote("periods", millis() - b0);
  }
  // статистику дня засеваем из карты, пока не получилось
  // (например, при старте ещё не было времени от NTP)
  if (!statsSeeded && millis() - lastSeed > 30000UL) {
    lastSeed = millis();
    unsigned long b0 = millis(); seedStatsFromSd(); blkNote("seed", millis() - b0);
  }

  // 3. Сеть: ОДНА тяжёлая операция за проход, между ними снова читаем тач.
  //    Пока идёт смена сети (WIFISET) — сетевые шаги не запускаем вовсе:
  //    обрыв связи не должен занимать цикл.
  //    5 с вместо 15: опрос реле локальный и дешёвый (~250 мс), зато
  //    напряжение приходит чаще и vAge не растёт до минут.
  if (gWifiSetState != 0) { gNetStep = 0; lastPoll = millis(); }
  if (gNetStep == 0 && millis() - lastPoll > 5000UL) gNetStep = 1;
  if (gNetStep != 0) {
    if (WiFi.status() != WL_CONNECTED) {
      if (!gWifiOff) WiFi.reconnect();   // WIFIOFF — не трогаем радио вообще
      gNetStep = 0;
      lastPoll = millis();
    } else {
      switch (gNetStep) {
        case 1:
          gRssi = WiFi.RSSI();
          { unsigned long q0 = millis(); bool okq = tuyaQuery(); gMsTuya = millis() - q0;
            blkNote("tuya", gMsTuya);
            gTuyaOk = okq;
            if (okq) gRelayFailRun = 0;
            else if (gRelayFailRun < 10) gRelayFailRun++; }
          break;
        case 2:
          // ОБЛАКО ЗДЕСЬ БОЛЬШЕ НЕ ВЫЗЫВАЕТСЯ. Раньше стояло именно тут, и это
          // была главная поломка: cloudStatus() получал управление только если
          // автомат доходил до шага 2 (после tuyaQuery на шаге 1), а gNetStep
          // сбрасывался при смене сети и на каждом «нет Wi-Fi». Плюс проверка
          // millis()-gPhaseMs > 80000 выполнялась лишь раз в 30 с — стоило
          // одному проходу застрять, и напряжение не обновлялось часами.
          // Теперь облако живёт в cloudTick() первым делом в loop().
          break;
        case 3:
          // ПК-API (192.168.1.20:8090) уже мёртв. Раньше здесь на КАЖДОМ
          // проходе было два таймаута по 2,5 с — это и были затупания.
          // 01.11.2026: и «раз в 5 минут» не спасает — коннект к выключенному
          // хосту ждёт ~5 с на уровне lwIP/ARP (таймаут HTTPClient на это не
          // влияет), и один проход loop() занимал 5002-5566 мс каждые 5 минут.
          // Пока ПК-API выключен, в цикле его не опрашиваем: fetchFromApi()
          // остаётся для setup() и ручной проверки. Включить — PC_API_POLL=1.
          #ifndef PC_API_POLL
          #define PC_API_POLL 0
          #endif
          if (PC_API_POLL && millis() - gApiLast > 300000UL && netOnline()) {
            unsigned long b0 = millis(); fetchFromApi(); blkNote("api", millis() - b0);
          }
          break;
        default: {
          unsigned long b0 = millis();
          pushHistory();
          ledUpdate();
          sendJsonData();
          blkNote("netjson", millis() - b0);
          lastPoll = millis();
          gNetStep = 0;
          break;
        }
      }
      if (gNetStep != 0) gNetStep++;      // 1 -> 2 -> 3 -> 4(default) -> 0
      handleTouch();                    // между операциями — снова тач
    }
  }

  // 4. Погода: первый раз через 5 с, потом раз в 10 минут.
  //    Пока данных нет — не чаще раза в минуту (а не каждый проход!).
  //    Нет интернета — на экране просто «НЕТ ДАННЫХ», цикл не тормозит.
  {
    unsigned long period = wOk ? 600000UL : 60000UL;
    if (wCode == 0) period = 120000UL;   // JSON не разобрался: повтор через 2 мин
    bool due = (lastWeather == 0) ? (millis() > 5000UL)
                                  : (millis() - lastWeather > period);
    // Не начинаем TLS в проходе, где сетевой бюджет уже израсходован
    // (облако/страница): иначе один проход выходит за 4 с.
    if (due && netBudgetLeft() >= 1500UL) {
      lastWeather = millis();
      if (netOnline()) {
        handleTouch();
        unsigned long b0 = millis();
        bool wxk = fetchWeather();
        blkNote("weather", millis() - b0);
        // Перерисовка — ТОЛЬКО по факту обновления, а не каждый проход loop.
        if (wxk) gLastDraw = 0;       // показать новые данные
      }
    }
  }

  // 4b. Прогноз (дождь сейчас + завтра). 16 шагов по 3 ч = 48 ч, чаще не
  //     нужно: раз в 30 минут. Неудача — повтор через 5 минут, но после трёх
  //     подряд возвращаемся к 30 минутам, иначе мёртвый API будет держать TLS
  //     каждую минуту. В проходе, где сетевой бюджет уже съеден (облако,
  //     страница), запрос не начинаем — иначе проход выходит за 4 с.
  {
    unsigned long period = wFcOk ? 1800000UL : 300000UL;
    if (wFcFail >= 3) period = 1800000UL;
    bool due = (lastForecast == 0) ? (millis() > 12000UL)
                                   : (millis() - lastForecast > period);
    if (due && netBudgetLeft() >= 1500UL) {
      lastForecast = millis();
      if (netOnline()) {
        handleTouch();
        unsigned long b0 = millis();
        bool fck = fetchForecast();
        blkNote("forecast", millis() - b0);
        weatherLogTouch();
        if (fck) gLastDraw = 0;       // карточка «ЗАВТРА» обновилась
      }
    }
  }

  // 5. Перерисовка — по СИГНАТУРЕ видимых данных, но НЕ чаще раза в 2 минуты:
  //    напряжение/мощность меняются раз в 20-40 с, и без порога каждая такая
  //    мелочь заливала все пять карточек (~139 КБ SPI) — 5 перерисовок за 3 мин.
  //    Статус-бар при этом обновляется отдельно раз в 60 с.
  //
  //    Энергосбережение: при ПОГАШЕННОМ экране полная перерисовка — не чаще
  //    раза в 60 с (SPI-обмен и заливка кадра — заметная часть расхода).
  //    Данные (/data, MQTT, SD) при этом идут своим темпом, как и раньше.
  if (!gDayLoading && dispIsDark()) {
    // В проходе, где сеть уже потратила время (TLS), кадр не заливаем:
    // экран всё равно погашен, а loopMaxAll иначе выходит за 4 с.
    if (millis() - gLastDarkDraw >= 60000UL && netBudgetLeft() >= 1500UL) {
      gLastDarkDraw = millis();
      gLastDraw = millis();
      if (gScreen == SCR_MAIN) { refreshMain(); gLastWinFull = millis(); }
      else { gNoClear = false; drawScreen(); }
      gLastSig = screenSig();
    }
  } else if (!gDayLoading) {
  if (gScreen == SCR_MAIN) {
    uint32_t sig = screenSig();
    bool changed = (sig != gLastSig);
    bool due = (millis() - gLastDraw > 300000UL);
    if (gLastDraw == 0 || due ||
        (changed && millis() - gLastDraw > 120000UL)) {
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
      // Прошлый день: живые ватты/вольты НЕ повод перерисовывать окно.
      // Раньше statsTick() менял daySumP каждые 5 с, сигнатура «менялась»,
      // и область 216x82 заливалась белым — это и было частое моргание.
      // Историю обновляем только полным кадром раз в 5 минут (или при
      // переключении дня). Для сегодняшнего дня — не чаще раза в 10 с.
      if (gDayOffset > 0) {
        gLastSig = screenSig();          // изменение запоминаем, но не рисуем
      } else if (millis() - gLastWinRefresh > 10000UL) {
        gLastSig = screenSig();
        refreshWindow();
      }
    }
    gLastDraw = millis();           // окно открыто — полную перерисовку не делаем
  }
  }

  // Статус-бар (сеть / SD / сигнал) — отдельно от сигнатуры: rssi и число
  // строк меняются постоянно, но перерисовывать из-за них пять карточек
  // нельзя. Достаточно обновить нижнюю полоску раз в минуту.
  if (gScreen == SCR_MAIN && millis() - lastSb > 60000UL) {
    lastSb = millis();
    drawStatusBar();
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
    } else if (up == "WX") {
      // Принудительный запрос погоды: проверка OpenWeatherMap и памяти без
      // ожидания 10-минутного периода.
      uint32_t h0 = ESP.getFreeHeap();
      bool wxok = fetchWeather();
        if (!wxok) {
          Serial.println("WX: первая попытка не прошла, пауза 3 с и повтор");
          for (int k = 0; k < 15; k++) { delay(200); esp_task_wdt_reset(); }
          wxok = fetchWeather();
        }
        Serial.printf("WX: %s, heap %u -> %u, wCode=%d\n",
                    wxok ? "ok" : "fail", h0, ESP.getFreeHeap(), wCode);
      gLastDraw = 0;
    } else if (up == "WXF") {
      // Принудительный запрос ПРОГНОЗА (дождь/завтра) — для проверки без
      // ожидания 30-минутного периода.
      uint32_t h0 = ESP.getFreeHeap();
      bool fok = fetchForecast();
        if (!fok) {
          Serial.println("WXF: первая попытка не прошла, пауза 3 с и повтор");
          for (int k = 0; k < 15; k++) { delay(200); esp_task_wdt_reset(); }
          fok = fetchForecast();
        }
        Serial.printf("WXF: %s, heap %u -> %u, pop=%.0f%% tom=%.0f%% %.1f..%.1f C %s\n",
                    fok ? "ok" : "fail", h0, ESP.getFreeHeap(),
                    wPopNow, wPopTom, wTminTom, wTmaxTom, wDescTom.c_str());
      gLastDraw = 0;
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
    } else if (up.startsWith("MQTTSET")) {
      // MQTTSET "host" [port] ["user"] ["pass"] | MQTTSET "" | MQTTSET
      cmdMqttSet(cmd);
    } else if (up == "MQTT") {
      mqttPrintStatus();
    } else if (up == "PWRFILL") {
      // ВАЖНО: resetStats() стирает ВСЕ массивы. Без повторного заполнения
      // из карты графики напряжения и температуры остаются пустыми.
      resetStats();
      statsSeeded = false;      // без этого seedStatsFromSd() выходит сразу
      seedStatsFromSd();
      gPwrFillHour = 0;
      gPwrFillStopH = -1;
      Serial.println("мощность: начинаю дозаполнение графика из облака");
    } else if (up.startsWith("PWRHIST")) {
      // PWRHIST [ГГГГ-ММ-ДД ГГГГ-ММ-ДД | ДД ДД] — мощность прошлых дней
      String args = cmd.substring(7);
      pwrHistStart(args);
    } else if (up.startsWith("GAPFILL")) {
      // GAPFILL [N | ГГГГ-ММ-ДД ГГГГ-ММ-ДД | LIST]
      String gargs = cmd.substring(7);
      gargs.trim();
      if (!gapFillCommand(gargs)) Serial.println("GAPFILL: не запустился (см. выше)");
    } else if (up == "TAPTEST") {
      gTapTest = !gTapTest;
      gScreen = SCR_MAIN;
      gChartHour = -1;
      drawScreen();
      gLastDraw = millis(); gLastSig = screenSig(); gLastWinFull = millis();
      Serial.println(gTapTest ? "тач-тест ВКЛ" : "тач-тест ВЫКЛ");
    } else if (up == "CALTOUCH") {
      calTouchStart();
    } else if (up == "CAL") {
      Serial.printf("калибровка: yleft=%d yright=%d xtop=%d xbottom=%d\n",
                    calYleft, calYright, calXtop, calXbottom);
    } else if (up == "CALRESET") {
      { Preferences p; p.begin("touchcal", false); p.clear(); p.end(); }
      Serial.println("калибровка сброшена, перезагрузка...");
      ledSetRaw(0, 0, 255);        // синий: плата уходит в перезагрузку
      delay(300); ESP.restart();
    } else if (up == "REBOOT") {
      Serial.println("перезагрузка...");
      ledSetRaw(0, 0, 255);
      delay(300); ESP.restart();
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
  } else if (up.startsWith("WIFISET")) {
      cmdWifiSet(cmd);
  } else if (up == "IP") {
      printIp();
  } else if (up == "STATUS") {
      sendJsonStatus();
    } else if (up == "DISP" || up.startsWith("DISP ")) {
      dispCommand(up);
    } else if (up == "BEEP" || up.startsWith("BEEP ")) {
      beepCommand(up);
    } else if (up == "LED" || up.startsWith("LED ")) {
      ledCommand(up);
    } else if (up.startsWith("TAP")) {
      int x = 0, y = 0;
      sscanf(up.c_str(), "TAP %d %d", &x, &y);
      Serial.printf("TAPCMD %d %d -> %d\n", x, y, gScreen);
      handleTapAt(x, y);
    }
  }

  esp_task_wdt_reset();

  // ---- замер: сколько длился этот проход (без delay внизу) -------------------
  gLoopLast = millis() - gLoopT0;
  if (gLoopLast > gLoopMaxWin) gLoopMaxWin = gLoopLast;
  if (gLoopLast > gLoopMaxAll) gLoopMaxAll = gLoopLast;
  if (gLoopWinAt == 0) gLoopWinAt = millis();
  else if (millis() - gLoopWinAt > 60000UL) { gLoopWinAt = millis(); gLoopMaxWin = 0; }

  // uptime для /boots.log: если плата уйдёт в сброс, при старте мы узнаем,
  // сколько она прожила. RTC-память — раз в 20 с (бесплатно), NVS — раз в
  // 10 минут: флеш не изнашиваем, но и после потери питания up_prev не 0.
  {
    static unsigned long lastUpSave = 0;
    static unsigned long lastUpNvs  = 0;
    if (millis() - lastUpSave > 20000UL) {
      lastUpSave = millis();
      gPrevUpRtc = millis() / 1000UL;
      // Первое сохранение в NVS — через 30 с (чтобы короткая сессия тоже
      // запомнилась), дальше раз в 5 минут: флеш не изнашиваем.
      if (millis() > 30000UL && (lastUpNvs == 0 || millis() - lastUpNvs > 300000UL)) {
        lastUpNvs = millis();
        { unsigned long b0 = millis(); bootSavePrevUp(gPrevUpRtc); blkNote("nvs", millis() - b0); }
      }
    }
  }

  // раз в секунду снимаем текущее напряжение в статистику и в события
  // (напряжение приходит и от реле, и из облака — считаем по факту значения)
  {
    static unsigned long sLastV = 0;
    if (millis() - sLastV > 1000UL) {
      sLastV = millis();
      if (gVoltage > 80 && gVoltage < 400) volQuality(gVoltage);
    }
  }

  // раз в 10 минут: не сменились ли сутки/тариф
  {
    static unsigned long sTarAt = 0;
    if (millis() - sTarAt > 600000UL) {
      sTarAt = millis();
      tarSeed();        // если журнал пуст (например, на старте не было времени) — заведём
      tarApplyToday();
    }
  }

  protCheck();      // защита по напряжению/температуре/мощности
  schTick();        // планировщик реле

  // отложенная перезагрузка (после /update или /reboot): не из обработчика HTTP —
  // иначе Reset reason пишется как PANIC
  if (gRestartAt && millis() >= gRestartAt) {
    gRestartAt = 0;
    bootMarkOta();
    delay(150);
    ESP.restart();
  }

  delay(5);          // 30 мс → 5 мс: цикл чаще, тач и сеть отзывчивее
}
