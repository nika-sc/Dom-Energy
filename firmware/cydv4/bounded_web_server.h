#pragma once
#include <WebServer.h>
#include <lwip/sockets.h>
#include <errno.h>
#include <esp_task_wdt.h>
#include <esp_system.h>
#if __has_include("control-secret.h")
#include "control-secret.h"
#else
#define DOM_CONTROL_TOKEN ""
#endif

// WiFiClient::write in core 2.0.9 can wait 10 seconds. Use the socket's
// nonblocking send and enforce a deadline shared by headers and body.
inline size_t boundedSocketWrite(WiFiClient& client, const char* data, size_t length,
                                 unsigned long started, unsigned long& progress) {
  size_t sent=0;
  while (sent<length && client.connected()) {
    // P0.3 hardening: медленного клиента, который НЕ читает, отрезает 150 мс без
    // прогресса. Общий лимит — с запасом под размер ответа: страница выросла до
    // 72 КБ, при редких «провалах» Wi-Fi отдача не укладывалась в фиксированные
    // 3 с и обрывалась (pageCut, ERR_CONTENT_LENGTH_MISMATCH). 3 с + 50 мс на КБ.
    unsigned long limitMs = 3000UL + length / 20UL;
    // Для больших ответов (страница/ассеты) разрешаем более долгую паузу между
    // успешными отправками: на 72 КБ TCP-буфер заполняется и ACK приходят реже
    // 150 мс — это не «мёртвый» клиент. Малые ответы API по-прежнему отрезаем
    // через 150 мс, чтобы брошенный клиент не занимал сервер.
    // Крупные ответы (страница 86 КБ, ассеты) на LAN уходят за 200-400 мс, но при
    // заполненном TCP-буфере пауза между успешными send() бывает >600 мс — и
    // страница обрывалась (pageCut 10/20 в тесте). Для >32 КБ разрешаем 1500 мс;
    // API-ответы (мелкие) по-прежнему отрезаем через 150 мс.
    unsigned long stallMs = (length > 32768) ? 2500UL : ((length > 8192) ? 600UL : 150UL);
    if (millis()-started>=limitMs || millis()-progress>=stallMs) break;
    size_t chunk=length-sent; if (chunk>1460) chunk=1460;
    int count=::send(client.fd(),data+sent,chunk,MSG_DONTWAIT);
    if (count>0) { sent+=count; progress=millis(); }
    else if (count<0 && (errno==EAGAIN || errno==EWOULDBLOCK || errno==EINTR)) delay(1);
    else break;
    esp_task_wdt_reset();
  }
  if (sent!=length) client.stop();
  return sent;
}

extern bool gGapBusy;
class BoundedWebServer : public WebServer {
 public:
  unsigned long lastWriteMs = 0;   // время последней реальной отправки ответа
  explicit BoundedWebServer(int port):WebServer(port){}
  void handleClient() override { started_=0; progress_=0; WebServer::handleClient(); }
  void issueBrowserSession() {
    if (strlen(DOM_CONTROL_TOKEN)<32) return;
    if (!session_[0]) {
      uint8_t bytes[32]; esp_fill_random(bytes,sizeof(bytes));
      const char* hex="0123456789abcdef";
      for (size_t i=0;i<sizeof(bytes);++i) { session_[2*i]=hex[bytes[i]>>4]; session_[2*i+1]=hex[bytes[i]&15]; }
      session_[64]=0;
    }
    sendHeader("Set-Cookie",String("dom_session=")+session_+"; Path=/; HttpOnly; SameSite=Strict");
  }
  bool controlTokenValid() {
    const char* expected=DOM_CONTROL_TOKEN;
    size_t size=strlen(expected);
    if (size<32) return false;
    String token=header("X-Dom-Key");
    if (token.length()==size) {
      uint8_t difference=0;
      for (size_t i=0;i<size;++i) difference|=token[i]^expected[i];
      if (!difference) return true;
    }
    if (!session_[0]) return false;
    String cookie=header("Cookie"); int start=0;
    while (start<(int)cookie.length()) {
      while (start<(int)cookie.length() && (cookie[start]==' ' || cookie[start]==';')) ++start;
      int end=cookie.indexOf(';',start); if(end<0) end=cookie.length();
      String item=cookie.substring(start,end); item.trim();
      if (item.startsWith("dom_session=") && item.length()==76) {
        uint8_t difference=0;
        for (int i=0;i<64;++i) difference|=item[12+i]^session_[i];
        if (!difference) return true;
      }
      start=end+1;
    }
    return false;
  }
  bool needsControlAuth() {
    String path=uri();
    if (method()!=HTTP_GET) return true;
    if (path=="/data" && hasArg("reset")) return true;
    if (path=="/pwrhist") return true; // Even a GET without parameters starts a task.
    if (path=="/update" || path=="/reboot" || path=="/tap" || path=="/restorebak" ||
        path=="/rotate" || path=="/wifiset" || path=="/beep") return true;
    if (path=="/skintest") return hasArg("set") && arg("set")=="1";
    if (path=="/climate") return hasArg("clean");
    if (path=="/mqtt" || path=="/disp" || path=="/pwrhist" || path=="/gapfill" ||
        path=="/invert" || path=="/skin" || path=="/sound" || path=="/tariff" ||
        path=="/sched" || path=="/protect" || path=="/weather") return args()>0;
    return false;
  }
  bool authorize() {
    // During an SD transaction only the lightweight live state is served.
    if (gGapBusy && uri()!="/data") { send(503,"application/json","{\"ok\":false,\"err\":\"storage busy\"}"); return false; }
    if (!needsControlAuth() || controlTokenValid()) return true;
    sendHeader("Cache-Control","no-store");
    send(strlen(DOM_CONTROL_TOKEN)<32 ? 503 : 401,"application/json",
         "{\"ok\":false,\"err\":\"control key required\"}");
    return false;
  }
  void on(const Uri& path,THandlerFunction fn) { on(path,HTTP_ANY,fn); }
  void on(const Uri& path,HTTPMethod method,THandlerFunction fn) {
    WebServer::on(path,method,[this,fn](){if(authorize()) fn();});
  }
  void on(const Uri& path,HTTPMethod method,THandlerFunction fn,THandlerFunction uploadFn) {
    WebServer::on(path,method,[this,fn](){if(authorize()) fn();},
      [this,uploadFn](){esp_task_wdt_reset();if(controlTokenValid()) uploadFn();});
  }
 protected:
  size_t _currentClientWrite(const char* data,size_t size) override {
    if (!started_) { started_=millis(); progress_=started_; }
    lastWriteMs = millis();
    return boundedSocketWrite(_currentClient,data,size,started_,progress_);
  }
  size_t _currentClientWrite_P(PGM_P data,size_t size) override {
    return _currentClientWrite(data,size);
  }
 private:
  char session_[65]={};
  unsigned long started_=0,progress_=0;
};
