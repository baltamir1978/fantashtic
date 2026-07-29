#pragma once
// GPS ATGM336H-6N (AT6668) del Cap LoRa-1262, por UART1 a 115200 8N1.
//
// Es el mayor consumidor de energia del aparato, asi que se puede apagar. La
// antena es ceramica e integrada: en interiores tarda o no engancha.

#include <stdint.h>

namespace hal {

struct Fix {
  bool     valido    = false;
  int32_t  lat_e7    = 0;
  int32_t  lon_e7    = 0;
  int32_t  altitud_m = 0;
  uint32_t epoch     = 0;
  uint8_t  satelites = 0;
  float    hdop      = 0.0f;
  float    rumbo     = 0.0f;   // solo fiable en movimiento
  float    velocidad_kmh = 0.0f;
  uint32_t edad_ms   = 0;      // desde la ultima trama valida
};

void gpsBegin();
void gpsEncender(bool on);
bool gpsEncendido();

// Llamar en cada tick: consume los caracteres que haya llegado por el UART.
void gpsLoop();

const Fix& gpsFix();

// true si hay fix valido y reciente (menos de 10 s).
bool gpsValido();

// Segundos desde el arranque sin conseguir fix, para el aviso de la barra.
uint32_t gpsSegundosSinFix();

// Caracteres NMEA recibidos: si es 0, el UART esta mal conectado, que es
// distinto de "hay UART pero no engancha satelites".
uint32_t gpsBytesRecibidos();

}  // namespace hal
