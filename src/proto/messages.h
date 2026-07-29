#pragma once
// Mensajes Position, User y Telemetry.
//
// Numeros de campo y tipos tomados de meshtastic/protobufs (mesh.proto y
// telemetry.proto). Los tipos importan: latitude_i es sfixed32, NO varint, y
// codificarlo mal produce coordenadas absurdas sin ningun aviso.
//
// Solo se implementan los campos que usamos. El analizador salta el resto,
// asi que interopera con nodos que envien mensajes completos.

#include <stddef.h>
#include <stdint.h>

namespace proto {

// --- Position (portnum 3) ----------------------------------------------------
//   1 latitude_i   sfixed32   grados * 1e7
//   2 longitude_i  sfixed32
//   3 altitude     int32      metros
//   4 time         fixed32    epoch
//   9 PDOP         uint32
//  11 sats_in_view uint32
struct Position {
  int32_t  lat_e7   = 0;
  int32_t  lon_e7   = 0;
  int32_t  altitud  = 0;
  uint32_t tiempo   = 0;
  uint32_t sats     = 0;
  bool     tiene_lat = false;
  bool     tiene_lon = false;
};

size_t codificarPosition(const Position& p, uint8_t* out, size_t cap);
bool   decodificarPosition(const uint8_t* in, size_t len, Position* p);

// --- User / NodeInfo (portnum 4) ---------------------------------------------
//   1 id          string   "!aabbccdd"
//   2 long_name   string   max 40 bytes
//   3 short_name  string   max 5 bytes (terminador incluido)
//   5 hw_model    enum
struct User {
  char     id[12]     = {};
  char     largo[40]  = {};
  char     corto[5]   = {};
  uint32_t hw_model   = 0;
};

size_t codificarUser(const User& u, uint8_t* out, size_t cap);
bool   decodificarUser(const uint8_t* in, size_t len, User* u);

// Rellena el campo id con el formato de Meshtastic: '!' y 8 digitos hex.
void idDesdeNodeNum(uint32_t num, char* out, size_t cap);

// --- Telemetry (portnum 67) --------------------------------------------------
//   1 time            fixed32
//   2 device_metrics  mensaje
// DeviceMetrics:
//   1 battery_level        uint32
//   2 voltage              float
//   3 channel_utilization  float
//   4 air_util_tx          float
//   5 uptime_seconds       uint32
struct Telemetry {
  uint32_t tiempo      = 0;
  uint32_t bateria_pct = 0;
  float    voltaje     = 0.0f;
  float    uso_canal   = 0.0f;
  float    uso_tx      = 0.0f;
  uint32_t uptime_s    = 0;
  bool     tiene_metrics = false;
};

size_t codificarTelemetry(const Telemetry& t, uint8_t* out, size_t cap);
bool   decodificarTelemetry(const uint8_t* in, size_t len, Telemetry* t);

}  // namespace proto
