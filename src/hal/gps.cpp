#include "hal/gps.h"

#include <Arduino.h>
#include <TinyGPSPlus.h>
#include <time.h>

#include "config.h"

namespace hal {
namespace {

// El toolchain de xtensa NO trae timegm(): newlib solo ofrece mktime, que
// aplica la zona horaria local. Como el GPS da siempre UTC, convertimos a mano
// con el algoritmo de dias civiles de Howard Hinnant.
uint32_t epochUtc(const struct tm& t) {
  int32_t y = t.tm_year + 1900;
  const int32_t m = t.tm_mon + 1, d = t.tm_mday;
  y -= (m <= 2);
  const int32_t  era = (y >= 0 ? y : y - 399) / 400;
  const uint32_t yoe = (uint32_t)(y - era * 400);
  const uint32_t doy = (uint32_t)((153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1);
  const uint32_t doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
  const int32_t  dias = era * 146097 + (int32_t)doe - 719468;
  return (uint32_t)dias * 86400u + (uint32_t)t.tm_hour * 3600u +
         (uint32_t)t.tm_min * 60u + (uint32_t)t.tm_sec;
}

TinyGPSPlus g_gps;
Fix         g_fix;
bool        g_on       = false;
bool        g_begun    = false;
uint32_t    g_bytes    = 0;
uint32_t    g_arranque = 0;
uint32_t    g_ultimo_fix_ms = 0;

}  // namespace

void gpsBegin() {
  if (g_begun) return;
  // RX del ESP = 15 (recibe del TX del GPS), TX del ESP = 13.
  Serial1.begin(GPS_BAUD, SERIAL_8N1, PIN_GPS_RX, PIN_GPS_TX);
  g_begun    = true;
  g_on       = true;
  g_arranque = millis();
  Serial.printf("GPS: UART1 a %d baudios (rx %d, tx %d)\n",
                GPS_BAUD, PIN_GPS_RX, PIN_GPS_TX);
}

void gpsRitmo(uint16_t ms) {
  if (!g_begun || !g_on) return;
  if (ms < 100) ms = 100;            // el modulo no admite mas de 10 Hz

  // $PCAS02,<ms>*CS  -- el checksum es el XOR de todo lo que hay entre $ y *.
  // Se calcula aqui a proposito: una constante mal copiada haria que el modulo
  // ignore el comando sin decir nada, y eso es muy dificil de ver.
  char cuerpo[24];
  const int n = snprintf(cuerpo, sizeof(cuerpo), "PCAS02,%u", (unsigned)ms);
  if (n <= 0 || n >= (int)sizeof(cuerpo)) return;

  uint8_t cs = 0;
  for (int i = 0; i < n; i++) cs ^= (uint8_t)cuerpo[i];

  Serial1.printf("$%s*%02X\r\n", cuerpo, cs);
  Serial.printf("GPS: ritmo a %u ms ($%s*%02X)\n", (unsigned)ms, cuerpo, cs);
}

void gpsEncender(bool on) {
  if (on && !g_begun) { gpsBegin(); return; }
  g_on = on;
  if (!on) {
    g_fix = Fix{};
    Serial1.end();
    g_begun = false;
  }
}

bool gpsEncendido() { return g_on; }

void gpsLoop() {
  if (!g_on || !g_begun) return;

  while (Serial1.available()) {
    g_gps.encode((char)Serial1.read());
    g_bytes++;
  }

  if (g_gps.location.isValid() && g_gps.location.age() < 10000) {
    g_fix.valido = true;
    g_fix.lat_e7 = (int32_t)(g_gps.location.lat() * 1e7);
    g_fix.lon_e7 = (int32_t)(g_gps.location.lng() * 1e7);
    g_fix.edad_ms = g_gps.location.age();
    g_ultimo_fix_ms = millis();
  } else if (millis() - g_ultimo_fix_ms > 10000) {
    g_fix.valido = false;
  }

  if (g_gps.altitude.isValid())  g_fix.altitud_m = (int32_t)g_gps.altitude.meters();
  if (g_gps.satellites.isValid()) g_fix.satelites = (uint8_t)g_gps.satellites.value();
  if (g_gps.hdop.isValid())      g_fix.hdop = (float)g_gps.hdop.hdop();
  if (g_gps.course.isValid())    g_fix.rumbo = (float)g_gps.course.deg();
  if (g_gps.speed.isValid())     g_fix.velocidad_kmh = (float)g_gps.speed.kmph();

  if (g_gps.date.isValid() && g_gps.time.isValid() && g_gps.date.year() > 2020) {
    struct tm t = {};
    t.tm_year = g_gps.date.year() - 1900;
    t.tm_mon  = g_gps.date.month() - 1;
    t.tm_mday = g_gps.date.day();
    t.tm_hour = g_gps.time.hour();
    t.tm_min  = g_gps.time.minute();
    t.tm_sec  = g_gps.time.second();
    g_fix.epoch = epochUtc(t);
  }
}

const Fix& gpsFix() { return g_fix; }
bool gpsValido()    { return g_on && g_fix.valido; }

uint32_t gpsSegundosSinFix() {
  if (!g_on) return 0;
  const uint32_t ref = g_ultimo_fix_ms ? g_ultimo_fix_ms : g_arranque;
  return (millis() - ref) / 1000;
}

uint32_t gpsBytesRecibidos() { return g_bytes; }

}  // namespace hal
