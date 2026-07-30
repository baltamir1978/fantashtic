#include "hal/gps.h"

#include <Arduino.h>
#include <TinyGPSPlus.h>
#include <stdlib.h>
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

// Satelites A LA VISTA: campo 3 de $GPGSV. TinyGPSPlus no parsea GSV por su
// cuenta, solo GGA/RMC, asi que hace falta este enganche. Se declara DESPUES de
// g_gps a proposito: el constructor de TinyGPSCustom se registra en la lista del
// objeto, asi que el objeto tiene que existir ya. Dentro de una misma unidad de
// traduccion el orden es el de arriba abajo, asi que esto es correcto y no
// depende del enlazador (ver la trampa del "cuelgue en el primer dibujo").
//
// Es solo GPS: GSV es una trama POR CONSTELACION, asi que si el modulo tambien
// saca $BDGSV esos no se cuentan. El numero mostrado es un suelo, no un total, y
// por eso la pantalla nunca promete mas de lo que sabe.
TinyGPSCustom g_gsv_vista(g_gps, "GPGSV", 3);

Fix         g_fix;
bool        g_on       = false;
bool        g_begun    = false;
uint32_t    g_bytes    = 0;
uint32_t    g_arranque = 0;
uint32_t    g_ultimo_fix_ms = 0;

// Ritmo de las rafagas NMEA, para que el light sleep no duerma encima de ellas.
// Ver GPS_TRAMA_* en config.h para el por que.
//
// Hay DOS numeros y no uno solo, y la diferencia importa:
//   g_periodo_ms    lo que se le PIDIO al modulo con $PCAS02.
//   g_intervalo_ms  lo que de verdad se MIDE entre el comienzo de dos rafagas.
// Medido el 30-jul-2026: con el ritmo puesto a 5000 ms el caudal baja de ~630 a
// ~400 B/s, no a los ~40 B/s que saldrian si el modulo hubiera hecho caso del
// todo. O sea que el AT6668 sigue soltando tramas (GSV mientras busca, por lo
// menos) a su aire. Fiarse del periodo pedido haria dormir encima de cuatro
// rafagas de cada cinco -- justo lo que esto viene a evitar. Se usa el MENOR de
// los dos: equivocarse por despertar antes no cuesta nada, equivocarse por
// despertar tarde pierde la trama.
uint32_t g_periodo_ms      = 1000;   // el defecto del modulo, antes de tocarlo
uint32_t g_intervalo_ms    = 1000;   // medido; arranca en el defecto
uint32_t g_ultimo_byte_ms  = 0;
uint32_t g_inicio_trama_ms = 0;      // 0 = todavia no se ha medido ninguna

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

  // La prevision de la siguiente rafaga depende de esto. Se invalida la fase
  // medida: durante el cambio de ritmo el modulo no tiene por que respetar la
  // cadencia anterior, y una fase vieja haria dormir justo encima de una rafaga.
  g_periodo_ms      = ms;
  g_inicio_trama_ms = 0;
}

uint32_t gpsMsHastaProximaTrama() {
  if (!g_on || !g_begun)   return UINT32_MAX;   // sin GPS no hay nada que respetar
  if (!g_ultimo_byte_ms)   return UINT32_MAX;   // nunca llego un byte: idem

  const uint32_t ahora   = millis();
  const uint32_t callado = ahora - g_ultimo_byte_ms;

  // Esta hablando ahora mismo: hay que seguir despierto para no cortar la rafaga.
  if (callado <= GPS_TRAMA_QUIETO_MS) return 0;

  // El menor de "lo pedido" y "lo medido": ver el comentario de g_intervalo_ms.
  const uint32_t periodo = g_periodo_ms < g_intervalo_ms ? g_periodo_ms : g_intervalo_ms;

  // Se ha callado del todo (se quedo sin alimentacion, o su cadencia no es la que
  // creemos). Sin esta salida se dejaria de dormir para siempre esperando una
  // rafaga que no llega, que es un fallo silencioso peor que perder una trama: el
  // light sleep parece roto cuando lo que falla es el GPS. Si vuelve, se pierde
  // una rafaga y se recupera la fase sola.
  if (callado > periodo * 3) return UINT32_MAX;

  if (!g_inicio_trama_ms) return 0;   // fase sin medir (recien cambiado el ritmo)

  const uint32_t despertar = g_inicio_trama_ms + periodo - GPS_TRAMA_MARGEN_MS;
  // Con signo: si la rafaga prevista ya deberia estar llegando, no dormir.
  if ((int32_t)(ahora - despertar) >= 0) return 0;
  return despertar - ahora;
}

void gpsEncender(bool on) {
  if (on && !g_begun) { gpsBegin(); return; }
  g_on = on;
  if (!on) {
    g_fix = Fix{};
    Serial1.end();
    g_begun = false;
    g_ultimo_byte_ms = g_inicio_trama_ms = 0;   // la fase medida ya no vale
  }
}

bool gpsEncendido() { return g_on; }

void gpsLoop() {
  if (!g_on || !g_begun) return;

  if (Serial1.available()) {
    const uint32_t ahora = millis();
    // Un silencio largo antes de estos bytes significa que empieza una rafaga
    // nueva. Es lo que fija la fase del modulo, y de ahi sale la prevision de
    // cuando llega la siguiente (gpsMsHastaProximaTrama).
    if (ahora - g_ultimo_byte_ms > GPS_TRAMA_QUIETO_MS) {
      const uint32_t hueco = ahora - g_inicio_trama_ms;
      // El suelo descarta las rafagas partidas: si un hueco interno pasa de
      // GPS_TRAMA_QUIETO_MS, la segunda mitad parece una rafaga nueva y el
      // intervalo medido saldria absurdamente corto.
      if (g_inicio_trama_ms && hueco >= GPS_TRAMA_MIN_INTERVALO_MS) g_intervalo_ms = hueco;
      g_inicio_trama_ms = ahora;
    }
    g_ultimo_byte_ms = ahora;

    while (Serial1.available()) {
      g_gps.encode((char)Serial1.read());
      g_bytes++;
    }
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
  if (g_gsv_vista.isValid()) {
    const int v = atoi(g_gsv_vista.value());
    if (v >= 0 && v <= 64) g_fix.satelites_vista = (uint8_t)v;
  }
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

uint32_t gpsIntervaloTramaMs() { return g_intervalo_ms; }

}  // namespace hal
