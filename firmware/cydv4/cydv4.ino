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
//  Тема: днём светлее, ночью приглушённая (по часам из NTP)
// ============================================================================

#include <WiFi.h>
#include <TFT_eSPI.h>
#include <Preferences.h>
#include <HTTPClient.h>
#include <SPI.h>
#include <SD.h>
#include <time.h>

#include "font5x7.h"
#include <esp_task_wdt.h>

#include <mbedtls/gcm.h>
#include <mbedtls/md.h>

// ---------------------------------------------------------------- настройки
const char* FW_VER = "4.0";

const char* DEF_SSID = "YOUR_WIFI_SSID";
const char* DEF_PASS = "YOUR_WIFI_PASSWORD";

const char* DEVICE_ID = "YOUR_TUYA_DEVICE_ID";
const char* LOCAL_KEY = "YOUR_TUYA_LOCAL_KEY";
const char* RELAY_IP  = "192.168.1.25";
const uint16_t RELAY_PORT = 6668;

const char* PC_API = "http://192.168.1.20:8090";

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

// ---------------------------------------------------------------- тема
bool gNight = false;

// Палитры из frontend-design-system (oikon48/cc-frontend-skills):
//   день  — Nordic Minimal : тёплая бумага, уголь, терракота
//   ночь  — Tech Noir      : глубокий navy, приглушённый текст, циан
uint16_t cBg, cSurf, cElev, cText, cMuted, cLine, cAccent, cAccent2;
uint16_t C_VOLT, C_TEMP, C_POWER, C_KWH, C_MONEY, C_OK, C_BAD, C_SKY;
uint16_t cSky, cWarn, cGood, cBadC;

void pickTheme() {
  struct tm ti;
  int h = 12;
  if (getLocalTime(&ti, 10)) h = ti.tm_hour;
  gNight = (h >= 21 || h < 7);

  if (gNight) {
    cBg      = tft.color565(15, 15, 35);
    cSurf    = tft.color565(26, 26, 46);
    cElev    = tft.color565(37, 37, 66);
    cText    = tft.color565(226, 232, 240);
    cMuted   = tft.color565(120, 132, 156);
    cLine    = tft.color565(48, 48, 82);
    cAccent  = tft.color565(34, 211, 238);
    cAccent2 = tft.color565(99, 102, 241);
    C_KWH    = tft.color565(52, 211, 153);
    C_OK     = C_KWH;
    C_BAD    = tft.color565(248, 113, 113);
    analogWrite(21, 55);
  } else {
    cBg      = tft.color565(247, 245, 243);
    cSurf    = tft.color565(255, 255, 255);
    cElev    = tft.color565(238, 234, 230);
    cText    = tft.color565(26, 32, 44);
    cMuted   = tft.color565(120, 128, 150);
    cLine    = tft.color565(214, 208, 202);
    cAccent  = tft.color565(193, 127, 89);
    cAccent2 = tft.color565(45, 55, 72);
    C_KWH    = tft.color565(34, 139, 94);
    C_OK     = C_KWH;
    C_BAD    = tft.color565(190, 60, 60);
    analogWrite(21, 255);
  }

  C_VOLT  = cAccent;
  C_TEMP  = tft.color565(139, 92, 246);
  C_POWER = tft.color565(245, 158, 11);
  C_MONEY = tft.color565(219, 39, 119);
  C_SKY   = tft.color565(56, 152, 224);
  cSky    = C_SKY;
  cGood   = C_KWH;
  cBadC   = C_BAD;
  cWarn   = (gNight ? tft.color565(245, 158, 11) : tft.color565(196, 132, 26));
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
bool  gRelay = false, gTuyaOk = false, gFromApi = false;
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
bool  wOk = false;

bool gSdOk = false;
int  gSdRows = 0;
unsigned long gSdLast = 0;
SPIClass sdSPI(HSPI);



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
  *sx = map(ay / n, calYleft, calYright, 0, 239);
  *sy = map(ax / n, calXtop, calXbottom, 0, 319);
  return (*sx >= 0 && *sx < 240 && *sy >= 0 && *sy < 320);
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

  drainSocket(150);
  uint8_t buf[512], payload[256];
  seqNo++;
  size_t n = buildFrame(CMD_DP_QUERY, (const uint8_t*)"{}", 2, sessionKey, buf);
  sock.write(buf, n);

  int got = readFrame(buf, sizeof(buf), 6000);
  if (got < 0) { sock.stop(); sessionReady = false; return false; }

  uint8_t cmd = 0;
  int plen = parseFrame(buf, got, sessionKey, &cmd, payload, sizeof(payload));
  if (plen <= 4) { sock.stop(); sessionReady = false; return false; }
  if (plen < (int)sizeof(payload)) payload[plen] = 0;

  uint32_t rc = ((uint32_t)payload[0] << 24) | ((uint32_t)payload[1] << 16) |
                ((uint32_t)payload[2] << 8) | payload[3];
  if (rc != 0) return false;

  const char* js = (const char*)(payload + 4);
  if (!strstr(js, "\"dps\"")) return false;

  long e = 0, t10 = 0;
  bool sw = false;
  findDpInt(js, "1", &e);
  findDpBool(js, "16", &sw);
  if (findDpInt(js, "101", &t10)) gTemp = t10 / 10.0f;

  gEnergy = e / 100.0f;
  gRelay  = sw;
  gCost   = gEnergy * 8.39f;
  gTuyaOk = true;
  gLastOk = millis();
  return true;
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
  float t = num("temperature"); if (t > -900 && t > -40 && t < 125) gTemp = t;
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
    if (n) { n.println("datetime,voltage,power,energy_kwh,relay,cost"); n.close(); }
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
  f.printf("%s,%.0f,%.0f,%.2f,%d,%.2f\n", stamp, gVoltage, gPower, gEnergy, gRelay ? 1 : 0, gCost);
  f.close();
  gSdRows++;
}

// ---------------------------------------------------------------- обмен с ПК
unsigned long gLastStatus = 0;
void sendJsonStatus() {
  Serial.printf("{\"t\":\"status\",\"fw\":\"CYD-%s\",\"name\":\"CYD-DASH\",\"up\":%lu,"
                "\"wifi\":%s,\"rssi\":%d,\"ip\":\"%s\",\"tuya\":%s,\"sd\":%s,"
                "\"sdRows\":%d,\"heap\":%u}\n",
                FW_VER, millis() / 1000,
                WiFi.status() == WL_CONNECTED ? "true" : "false",
                gRssi, WiFi.localIP().toString().c_str(),
                gTuyaOk ? "true" : "false", gSdOk ? "true" : "false",
                gSdRows, ESP.getFreeHeap());
}
void sendJsonData() {
  Serial.printf("{\"t\":\"data\",\"v\":%.0f,\"temp\":%.1f,\"p\":%.0f,\"e\":%.2f,"
                "\"r\":%s,\"a\":false,\"cost\":%.2f,\"minV\":%.0f,\"maxV\":%.0f}\n",
                gVoltage, gTemp, gPower, gEnergy, gRelay ? "true" : "false",
                gCost, gVoltage, gVoltage);
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
// Рисуем кириллицу своим шрифтом 5x7. Горизонтальные отрезки — по одному
// прямоугольнику на серию подряд идущих точек, так заметно быстрее.
void drawTxt(int x, int y, const char* s, uint16_t fg, uint16_t bg, uint8_t scale) {
  int w = txtW(s, scale);
  tft.fillRect(x, y, w + scale, 7 * scale, bg);

  int cx = x;
  const char* p = s;
  while (*p) {
    int len = utf8len((unsigned char)*p);
    const uint8_t* g = glyphFind(p, len);     // ищем по UTF-8, а не по байту
    p += len;
    if (g) {
      for (int row = 0; row < 7; row++) {
        uint8_t bits = g[row];
        int col = 0;
        while (col < 5) {
          if (bits & (0b10000 >> col)) {
            int run = 1;
            while (col + run < 5 && (bits & (0b10000 >> (col + run)))) run++;
            tft.fillRect(cx + col * scale, y + row * scale, run * scale, scale, fg);
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
  if (!gTuyaOk && !gFromApi) { ledSet(false, false, true); return; }
  if (gRelay) ledSet(false, true, false);
  else        ledSet(true, false, false);
}

// ---------------------------------------------------------------- иконка погоды
void drawWeatherIcon(int cx, int cy, int r, bool hasData) {
  if (!hasData) { tft.drawCircle(cx, cy, r, cMuted); return; }

  String s = wDescEn;
  s.toLowerCase();
  bool thunder = s.indexOf("thunder") >= 0;
  bool rain    = s.indexOf("rain") >= 0 || s.indexOf("drizzle") >= 0 || s.indexOf("shower") >= 0;
  bool snow    = s.indexOf("snow") >= 0 || s.indexOf("sleet") >= 0 || s.indexOf("blizzard") >= 0;
  bool cloudy  = s.indexOf("cloud") >= 0 || s.indexOf("overcast") >= 0;
  bool clear   = s.indexOf("clear") >= 0 || s.indexOf("sunny") >= 0;
  bool cloud   = cloudy || rain || snow || thunder;

  if (clear || (!cloud && !rain && !snow && !thunder)) {
    tft.fillCircle(cx, cy, r / 2, cWarn);
    for (int i = 0; i < 8; i++) {
      float a = i * PI / 4.0;
      int x1 = cx + (int)(cos(a) * (r / 2 + 3));
      int y1 = cy + (int)(sin(a) * (r / 2 + 3));
      int x2 = cx + (int)(cos(a) * r);
      int y2 = cy + (int)(sin(a) * r);
      tft.drawLine(x1, y1, x2, y2, cWarn);
    }
    return;
  }

  int cy2 = cy - ((rain || snow || thunder) ? 4 : 0);
  tft.fillCircle(cx - r / 3, cy2, r / 3, cText);
  tft.fillCircle(cx + r / 3, cy2, r / 3, cText);
  tft.fillCircle(cx, cy2 - r / 4, r / 2 - 1, cText);
  tft.fillRect(cx - r / 3, cy2, (r * 2) / 3, r / 3, cText);

  if (thunder) {
    tft.drawLine(cx + 2, cy2 + r / 3, cx - 3, cy2 + r / 2 + 2, cWarn);
    tft.drawLine(cx - 3, cy2 + r / 2 + 2, cx + 3, cy2 + r / 2 + 2, cWarn);
    tft.drawLine(cx + 3, cy2 + r / 2 + 2, cx - 2, cy + r - 1, cWarn);
  } else if (rain) {
    for (int i = -1; i <= 1; i++) {
      int x = cx + i * 7;
      tft.drawLine(x + 2, cy2 + r / 3 + 2, x - 1, cy + r - 1, cSky);
    }
  } else if (snow) {
    for (int i = -1; i <= 1; i++) {
      int x = cx + i * 7;
      int yy = cy + r - 5;
      tft.drawFastHLine(x - 3, yy, 7, cAccent);
      tft.drawFastVLine(x, yy - 3, 7, cAccent);
    }
  } else {
    for (int i = -1; i <= 1; i++)
      tft.drawFastHLine(cx + i * 8 - 3, cy2 + r / 2 + 6, 6, cMuted);
  }
}

// текст по центру
void drawTxtC(int cx, int y, const char* s, uint16_t fg, uint16_t bg, uint8_t scale) {
  drawTxt(cx - txtW(s, scale) / 2, y, s, fg, bg, scale);
}

// текст справа
void drawTxtR(int rx, int y, const char* s, uint16_t fg, uint16_t bg, uint8_t scale) {
  drawTxt(rx - txtW(s, scale), y, s, fg, bg, scale);
}

// ---------------------------------------------------------------- экран
void drawScreen() {
  tft.fillScreen(cBg);
  char b[40];

  // ---------- шапка ----------
  tft.drawFastHLine(0, 0, 240, cAccent);
  drawTxt(10, 9, "ДОМ", cText, cBg, 2);

  struct tm ti;
  if (getLocalTime(&ti, 10)) {
    static const char* MON[] = { "ЯНВ", "ФЕВ", "МАР", "АПР", "МАЙ", "ИЮН",
                                 "ИЮЛ", "АВГ", "СЕН", "ОКТ", "НОЯ", "ДЕК" };
    snprintf(b, sizeof(b), "%02d:%02d", ti.tm_hour, ti.tm_min);
    drawTxtR(232, 4, b, cText, cBg, 2);
    snprintf(b, sizeof(b), "%d %s", ti.tm_mday, MON[ti.tm_mon]);
    drawTxtR(232, 20, b, cMuted, cBg, 1);
  } else {
    drawTxtR(232, 9, "--:--", cMuted, cBg, 2);
  }
  tft.drawFastHLine(0, 34, 240, cLine);

  // ---------- герой: напряжение ----------
  drawTxt(12, 42, "НАПРЯЖЕНИЕ", cMuted, cBg, 1);

  snprintf(b, sizeof(b), "%.0f", gVoltage);
  drawTxt(10, 54, b, cText, cBg, 6);
  drawTxt(10 + txtW(b, 6) + 8, 74, "В", cMuted, cBg, 2);

  // состояние реле справа
  uint16_t rc = gRelay ? cGood : cBadC;
  tft.fillCircle(214, 58, 5, rc);
  drawTxtR(228, 66, gRelay ? "ВКЛ" : "ВЫКЛ", rc, cBg, 1);

  // полоска качества напряжения 190..260 В
  int bx = 12, by = 104, bw = 216;
  tft.fillRect(bx, by, bw, 4, cLine);
  int fill = constrain((int)((gVoltage - 190) / 70.0 * bw), 0, bw);
  uint16_t q = (gVoltage > 200 && gVoltage < 245) ? cGood : cWarn;
  if (fill > 0) tft.fillRect(bx, by, fill, 4, q);
  drawTxt(bx, by + 7, "190", cMuted, cBg, 1);
  drawTxtR(bx + bw, by + 7, "260", cMuted, cBg, 1);

  tft.drawFastHLine(0, 122, 240, cLine);

  // ---------- температура и мощность ----------
  snprintf(b, sizeof(b), "%.1f", gTemp);
  drawTxt(12, 130, b, cText, cBg, 4);
  drawTxt(12 + txtW(b, 4) + 5, 138, "~C", cMuted, cBg, 1);
  drawTxt(12, 166, "ТЕМП", cMuted, cBg, 1);

  tft.drawFastVLine(122, 130, 44, cLine);

  snprintf(b, sizeof(b), "%.0f", gPower);
  drawTxt(134, 130, b, cText, cBg, 4);
  drawTxt(134 + txtW(b, 4) + 5, 138, "ВТ", cMuted, cBg, 1);
  drawTxt(134, 166, "СЕЙЧАС", cMuted, cBg, 1);

  tft.drawFastHLine(0, 182, 240, cLine);

  // ---------- сегодня ----------
  drawTxt(12, 190, "СЕГОДНЯ", cMuted, cBg, 1);
  snprintf(b, sizeof(b), "%.2f", gFromApi ? gTodayKwh : gEnergy);
  drawTxt(12, 202, b, cGood, cBg, 3);
  drawTxt(12 + txtW(b, 3) + 6, 210, "КВТ*Ч", cMuted, cBg, 1);

  snprintf(b, sizeof(b), "%.2f", gFromApi ? gTodayCost : gCost);
  drawTxtR(228, 202, b, C_MONEY, cBg, 3);
  drawTxtR(228, 226, "РУБ", cMuted, cBg, 1);

  // ---------- погода ----------
  tft.drawFastHLine(0, 240, 240, cLine);
  drawTxt(12, 246, "ВАШ ГОРОД", cMuted, cBg, 1);

  drawWeatherIcon(30, 272, 17, wOk);

  if (wOk) {
    snprintf(b, sizeof(b), "%.0f~", wTemp);
    drawTxt(56, 258, b, cSky, cBg, 3);

    char d[20];
    descToRus(wDescEn, d, sizeof(d));
    drawTxt(56 + txtW(b, 3) + 6, 262, d, cText, cBg, 2);

    snprintf(b, sizeof(b), "%.0f М/С", wWind);
    drawTxtR(230, 258, b, cMuted, cBg, 1);
    snprintf(b, sizeof(b), "%.0f%%", wHum);
    drawTxtR(230, 272, b, cMuted, cBg, 1);
  } else {
    drawTxt(56, 262, "НЕТ ДАННЫХ", cMuted, cBg, 2);
  }

  // ---------- статус ----------
  tft.drawFastHLine(0, 292, 240, cLine);
  snprintf(b, sizeof(b), "WIFI %d", gRssi);
  drawTxt(10, 300, b, cMuted, cBg, 1);
  snprintf(b, sizeof(b), "SD %d", gSdRows);
  drawTxt(100, 300, b, gSdOk ? cMuted : cBadC, cBg, 1);
  drawTxtR(230, 300, gFromApi ? "ПК" : "РЕЛЕ", cMuted, cBg, 1);
}

// ============================================================================
//  SETUP
// ============================================================================
void setup() {
  Serial.begin(115200);
  delay(1200);
  Serial.println("\n=== ДОМ 4.0 ===");

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
    tft.fillRect(0, 200, 240, 60, TFT_BLACK);
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

  tuyaQuery();
  fetchFromApi();
  pushHistory();
  fetchWeather();

  drawScreen();
  sendJsonStatus();
  sendJsonData();
}

// ============================================================================
//  LOOP
// ============================================================================
void loop() {
  static unsigned long lastPoll = 0;
  static unsigned long lastWeather = 0;
  static unsigned long lastTheme = 0;
  static unsigned long lastDraw = 0;

  // опрос реле и ПК
  if (millis() - lastPoll > 15000) {
    lastPoll = millis();
    if (WiFi.status() != WL_CONNECTED) {
      WiFi.reconnect();
    } else {
      gRssi = WiFi.RSSI();
      gTuyaOk = tuyaQuery();
      fetchFromApi();
      ledUpdate();
      pushHistory();
      writeSd();
      lastDraw = 0;             // перерисовать сразу
      sendJsonData();
    }
  }

  // погода раз в 10 минут
  if (millis() - lastWeather > 600000 || (!wOk && millis() > 20000)) {
    lastWeather = millis();
    if (WiFi.status() == WL_CONNECTED) {
      fetchWeather();
      lastDraw = 0;
    }
  }

  // тема по часам
  if (millis() - lastTheme > 60000) {
    lastTheme = millis();
    bool was = gNight;
    struct tm ti;
    if (getLocalTime(&ti, 10)) gNight = (ti.tm_hour >= 21 || ti.tm_hour < 7);
    if (was != gNight) { pickTheme(); lastDraw = 0; }
  }

  // перерисовка раз в 30 секунд (часы) или по флагу
  if (lastDraw == 0 || millis() - lastDraw > 30000) {
    lastDraw = millis();
    drawScreen();
  }

  // статус в порт
  if (millis() - gLastStatus > 5000) {
    gLastStatus = millis();
    sendJsonStatus();
  }

  esp_task_wdt_reset();
  delay(50);
}

