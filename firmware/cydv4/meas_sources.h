#pragma once
// P0.1: источники и свежесть измерений (локальное реле vs облако).
// Вынесено в заголовок: в .ino Arduino сам добавляет прототипы функций в начало
// файла, и структура MeasCand оказывалась объявлена позже прототипов.
// ---------------------------------------------------------------- P0.1 источники
// Раньше у каждой величины была ОДНА метка времени, и по ней нельзя было
// отличить «локальное реле, 8 с» от «облако, 40 с»: обе пишут в gVoltage.
// Здесь — отдельный кандидат от локального реле и от облака. Поля /data не
// меняются, всё новое отдаётся через /measure.
#define MEAS_TTL_VP 90000UL      // напряжение/мощность/ток считаем свежими 90 с
#define MEAS_TTL_T  300000UL     // температура меняется медленно — 5 минут
struct MeasCand { float value; uint32_t at; bool valid; };
static MeasCand mVlocal = {0, 0, false}, mVcloud = {0, 0, false};
static MeasCand mPlocal = {0, 0, false}, mPcloud = {0, 0, false};
static MeasCand mIlocal = {0, 0, false}, mIcloud = {0, 0, false};
static MeasCand mTlocal = {0, 0, false}, mTcloud = {0, 0, false};
static void measSet(MeasCand& c, float v) { c.value = v; c.at = (uint32_t)millis(); c.valid = true; }
static bool measFresh(const MeasCand& c, uint32_t ttl) {
  return c.valid && (uint32_t)((uint32_t)millis() - c.at) <= ttl;   // верно и при wrap millis()
}
static long measAgeSec(const MeasCand& c) {
  // -1 = «значения не было ни разу». 0 значил бы «только что пришло» и скрывал
  // отсутствие данных (замечание ревью m1).
  return c.valid ? (long)(((uint32_t)millis() - c.at) / 1000UL) : -1L;
}
// Выбор: свежий локальный -> свежий облачный -> нет данных. Если оба свежие,
// честно берём тот, что пришёл позже (по метке millis, с учётом переполнения).
static const char* measPick(const MeasCand& lo, const MeasCand& cl, uint32_t ttl, const MeasCand** out) {
  bool fl = measFresh(lo, ttl), fc = measFresh(cl, ttl);
  if (fl && fc) {
    if ((uint32_t)((uint32_t)millis() - lo.at) <= (uint32_t)((uint32_t)millis() - cl.at)) { *out = &lo; return "local"; }
    *out = &cl; return "cloud";
  }
  if (fl) { *out = &lo; return "local"; }
  if (fc) { *out = &cl; return "cloud"; }
  *out = nullptr; return "none";
}
static void measOne(String& s, const char* unit, const MeasCand& lo, const MeasCand& cl, uint32_t ttl) {
  const MeasCand* pick = nullptr;
  const char* src = measPick(lo, cl, ttl, &pick);
  s += "{\"value\":";
  if (pick) s += String(pick->value, 2); else s += "null";
  s += ",\"unit\":\""; s += unit;
  s += "\",\"source\":\""; s += src;
  s += "\",\"ageSec\":"; s += pick ? String(measAgeSec(*pick)) : String(-1);
  s += ",\"valid\":"; s += pick ? "true" : "false";
  s += ",\"local\":{\"value\":"; s += lo.valid ? String(lo.value, 2) : String("null");
  s += ",\"ageSec\":"; s += String(measAgeSec(lo));
  s += "},\"cloud\":{\"value\":"; s += cl.valid ? String(cl.value, 2) : String("null");
  s += ",\"ageSec\":"; s += String(measAgeSec(cl));
  s += "},";
  s += "\"ttlSec\":"; s += String(ttl / 1000UL);
  s += "}";
}
static String measurementSourcesJson() {
  String s;
  s.reserve(900);
  s += "{\"timezone\":\"device-local\",\"clockOk\":";
  s += (time(nullptr) > 1700000000L) ? "true" : "false";
  s += ",\"ttl\":{\"vpSec\":"; s += String(MEAS_TTL_VP / 1000UL);
  s += ",\"tSec\":"; s += String(MEAS_TTL_T / 1000UL); s += "},";
  s += "\"voltage\":";    measOne(s, "V",  mVlocal, mVcloud, MEAS_TTL_VP); s += ",";
  s += "\"power\":";      measOne(s, "W",  mPlocal, mPcloud, MEAS_TTL_VP); s += ",";
  s += "\"current\":";    measOne(s, "A",  mIlocal, mIcloud, MEAS_TTL_VP); s += ",";
  s += "\"temperature\":"; measOne(s, "C", mTlocal, mTcloud, MEAS_TTL_T);
  s += ",\"note\":\"eventEpoch не хранится: облако отдаёт только момент получения\"}";
  return s;
}
