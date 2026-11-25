#include "Weather.h"
#include "Network.h"
#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <NetworkClientSecure.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <time.h>
#include <math.h>

namespace {
portMUX_TYPE lock = portMUX_INITIALIZER_UNLOCKED;
Weather::Snapshot state;
bool started = false;
double latitude=17.050, longitude=74.267;
uint32_t generation=0;
const char *condition(int code, float temperature, float wind, bool day) {
  if (code >= 95) return "Thunderstorms";
  if ((code >= 71 && code <= 77) || code == 85 || code == 86) return "Snowy";
  if (code >= 51 && code <= 82) return "Rainy";
  if (code == 45 || code == 48) return "Foggy";
  if (wind >= 35) return "Windy";
  if (code == 3) return "Cloudy";
  if (code == 2) return "Partly cloudy";
  if (code <= 1) return temperature >= 18 && temperature <= 28 ? (day ? "Sunny and pleasant" : "Clear and pleasant") : (day ? "Sunny" : "Clear");
  return "Mixed weather";
}
bool fetch(Weather::Snapshot &value, double lat, double lon) {
  NetworkClientSecure socket;
  socket.useBuiltinCACertBundle();
  socket.setHandshakeTimeout(4);
  HTTPClient http;
  http.setConnectTimeout(3000);
  http.setTimeout(5000);
  http.useHTTP10(true);
  char url[256];
  snprintf(url,sizeof(url),"https://api.open-meteo.com/v1/forecast?latitude=%.5f&longitude=%.5f&current=temperature_2m,weather_code,wind_speed_10m,is_day&timezone=auto",lat,lon);
  if (!http.begin(socket, url)) return false;
  JsonDocument doc;
  const int code = http.GET();
  bool ok = code == 200 && http.getSize() <= 8192 &&
            !deserializeJson(doc, http.getStream(), DeserializationOption::NestingLimit(6));
  http.end();
  ok = ok && doc["current"]["temperature_2m"].is<float>() &&
       doc["current"]["weather_code"].is<int>() && doc["current"]["wind_speed_10m"].is<float>() && doc["current"]["is_day"].is<int>();
  if (!ok) return false;
  const float temperature = doc["current"]["temperature_2m"], wind = doc["current"]["wind_speed_10m"];
  const int weatherCode = doc["current"]["weather_code"];
  if (!isfinite(temperature) || temperature < -90 || temperature > 65 || weatherCode < 0 || weatherCode > 99) return false;
  value.available = true;
  value.stale = false;
  snprintf(value.text, sizeof(value.text), "%s / %.0f C", condition(weatherCode, temperature, wind, doc["current"]["is_day"].as<int>() == 1), temperature);
  return true;
}
void worker(void *) {
  uint32_t seen=UINT32_MAX, nextFetch=0;
  for (;;) {
    double lat,lon;
    uint32_t version;
    portENTER_CRITICAL(&lock);
    lat=latitude; lon=longitude; version=generation;
    portEXIT_CRITICAL(&lock);
    if (CompanionNetwork::connected() && time(nullptr)>=1704067200 &&
        (version!=seen || int32_t(millis()-nextFetch)>=0)) {
      auto value=Weather::snapshot();
      const bool ok=fetch(value,lat,lon);
      if (!ok) {
        value.stale=value.available;
        if (!value.available) strlcpy(value.text,"Weather unavailable",sizeof(value.text));
      }
      portENTER_CRITICAL(&lock);
      if (generation==version) {
        value.revision=state.revision+1;
        state=value;
      }
      portEXIT_CRITICAL(&lock);
      seen=version;
      nextFetch=millis()+(ok?900000:60000);
    }
    vTaskDelay(pdMS_TO_TICKS(1000));
  }
}
}
namespace Weather {
Snapshot snapshot() {
  portENTER_CRITICAL(&lock);
  const Snapshot value = state;
  portEXIT_CRITICAL(&lock);
  return value;
}
void setLocation(double lat,double lon) {
  portENTER_CRITICAL(&lock);
  if (latitude!=lat || longitude!=lon) {
    latitude=lat; longitude=lon; ++generation;
    state.available=false; state.stale=false;
    strlcpy(state.text,"Updating weather...",sizeof(state.text)); ++state.revision;
  }
  portEXIT_CRITICAL(&lock);
}
void begin() {
  if (!started) started = xTaskCreate(worker, "weather", 8192, nullptr, 1, nullptr) == pdPASS;
}
}

