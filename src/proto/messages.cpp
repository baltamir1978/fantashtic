#include "proto/messages.h"

#include <stdio.h>
#include <string.h>

namespace proto {
namespace {

// --- primitivas de protobuf --------------------------------------------------

size_t ponVarint(uint8_t* out, uint64_t v) {
  size_t n = 0;
  do {
    uint8_t b = (uint8_t)(v & 0x7F);
    v >>= 7;
    if (v) b |= 0x80;
    out[n++] = b;
  } while (v);
  return n;
}

bool tomaVarint(const uint8_t* in, size_t len, size_t* pos, uint64_t* out) {
  uint64_t v = 0;
  int desp = 0;
  while (*pos < len) {
    const uint8_t b = in[(*pos)++];
    v |= (uint64_t)(b & 0x7F) << desp;
    if (!(b & 0x80)) { *out = v; return true; }
    desp += 7;
    if (desp > 63) return false;
  }
  return false;
}

void ponFixed32(uint8_t* out, uint32_t v) {
  out[0] = (uint8_t)(v);        out[1] = (uint8_t)(v >> 8);
  out[2] = (uint8_t)(v >> 16);  out[3] = (uint8_t)(v >> 24);
}
uint32_t tomaFixed32(const uint8_t* p) {
  return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
         ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

// int32 negativo en varint ocupa siempre 10 bytes en protobuf: se codifica
// como uint64 con extension de signo. No es zigzag salvo en los tipos sint.
uint64_t comoUint64(int32_t v) { return (uint64_t)(int64_t)v; }

float bitsAFloat(uint32_t b) { float f; memcpy(&f, &b, 4); return f; }
uint32_t floatABits(float f)  { uint32_t b; memcpy(&b, &f, 4); return b; }

size_t ponCampoFixed32(uint8_t* out, uint8_t campo, uint32_t v) {
  out[0] = (uint8_t)((campo << 3) | 5);
  ponFixed32(out + 1, v);
  return 5;
}

size_t ponCampoVarint(uint8_t* out, uint8_t campo, uint64_t v) {
  out[0] = (uint8_t)((campo << 3) | 0);
  return 1 + ponVarint(out + 1, v);
}

size_t ponCampoCadena(uint8_t* out, uint8_t campo, const char* s) {
  const size_t n = strlen(s);
  out[0] = (uint8_t)((campo << 3) | 2);
  const size_t k = ponVarint(out + 1, n);
  memcpy(out + 1 + k, s, n);
  return 1 + k + n;
}

// Recorre los campos llamando a 'fn'. Devuelve false si el buffer es invalido.
template <typename F>
bool recorrer(const uint8_t* in, size_t len, F fn) {
  size_t pos = 0;
  while (pos < len) {
    uint64_t etiqueta = 0;
    if (!tomaVarint(in, len, &pos, &etiqueta)) return false;
    const uint32_t campo = (uint32_t)(etiqueta >> 3);
    const uint8_t  tipo  = (uint8_t)(etiqueta & 0x07);

    switch (tipo) {
      case 0: {
        uint64_t v = 0;
        if (!tomaVarint(in, len, &pos, &v)) return false;
        fn(campo, tipo, (const uint8_t*)nullptr, (size_t)0, v);
        break;
      }
      case 5: {
        if (pos + 4 > len) return false;
        fn(campo, tipo, in + pos, (size_t)4, (uint64_t)tomaFixed32(in + pos));
        pos += 4;
        break;
      }
      case 1: {
        if (pos + 8 > len) return false;
        pos += 8;
        break;
      }
      case 2: {
        uint64_t l = 0;
        if (!tomaVarint(in, len, &pos, &l)) return false;
        if (pos + l > len) return false;
        fn(campo, tipo, in + pos, (size_t)l, (uint64_t)0);
        pos += (size_t)l;
        break;
      }
      default:
        return false;
    }
  }
  return true;
}

void copiarCadena(char* destino, size_t cap, const uint8_t* p, size_t n) {
  if (cap == 0) return;
  if (n > cap - 1) n = cap - 1;      // truncado obligatorio
  memcpy(destino, p, n);
  destino[n] = '\0';
}

}  // namespace

// ================================ Position ==================================

size_t codificarPosition(const Position& p, uint8_t* out, size_t cap) {
  if (cap < 32) return 0;
  size_t n = 0;
  // sfixed32: 4 bytes en complemento a dos, NO varint.
  n += ponCampoFixed32(out + n, 1, (uint32_t)p.lat_e7);
  n += ponCampoFixed32(out + n, 2, (uint32_t)p.lon_e7);
  if (p.altitud) n += ponCampoVarint(out + n, 3, comoUint64(p.altitud));
  if (p.tiempo)  n += ponCampoFixed32(out + n, 4, p.tiempo);
  if (p.sats)    n += ponCampoVarint(out + n, 11, p.sats);
  return n;
}

bool decodificarPosition(const uint8_t* in, size_t len, Position* p) {
  *p = Position{};
  Position* q = p;
  return recorrer(in, len, [q](uint32_t campo, uint8_t tipo,
                               const uint8_t*, size_t, uint64_t v) {
    switch (campo) {
      case 1:  if (tipo == 5) { q->lat_e7 = (int32_t)(uint32_t)v; q->tiene_lat = true; } break;
      case 2:  if (tipo == 5) { q->lon_e7 = (int32_t)(uint32_t)v; q->tiene_lon = true; } break;
      case 3:  if (tipo == 0) q->altitud = (int32_t)(int64_t)v; break;
      case 4:  if (tipo == 5) q->tiempo  = (uint32_t)v; break;
      case 11: if (tipo == 0) q->sats    = (uint32_t)v; break;
      default: break;
    }
  });
}

// ================================== User ====================================

void idDesdeNodeNum(uint32_t num, char* out, size_t cap) {
  snprintf(out, cap, "!%08x", (unsigned)num);
}

size_t codificarUser(const User& u, uint8_t* out, size_t cap) {
  if (cap < 96) return 0;
  size_t n = 0;
  if (u.id[0])    n += ponCampoCadena(out + n, 1, u.id);
  if (u.largo[0]) n += ponCampoCadena(out + n, 2, u.largo);
  if (u.corto[0]) n += ponCampoCadena(out + n, 3, u.corto);
  if (u.hw_model) n += ponCampoVarint(out + n, 5, u.hw_model);
  return n;
}

bool decodificarUser(const uint8_t* in, size_t len, User* u) {
  *u = User{};
  User* q = u;
  return recorrer(in, len, [q](uint32_t campo, uint8_t tipo,
                               const uint8_t* p, size_t n, uint64_t v) {
    switch (campo) {
      // Truncado obligatorio: long_name admite 40 bytes en el protocolo.
      case 1: if (tipo == 2) copiarCadena(q->id,    sizeof(q->id),    p, n); break;
      case 2: if (tipo == 2) copiarCadena(q->largo, sizeof(q->largo), p, n); break;
      case 3: if (tipo == 2) copiarCadena(q->corto, sizeof(q->corto), p, n); break;
      case 5: if (tipo == 0) q->hw_model = (uint32_t)v; break;
      default: break;
    }
  });
}

// ================================ Telemetry =================================

size_t codificarTelemetry(const Telemetry& t, uint8_t* out, size_t cap) {
  if (cap < 48) return 0;

  // Primero el submensaje DeviceMetrics, para saber cuanto ocupa.
  uint8_t dm[40];
  size_t m = 0;
  if (t.bateria_pct) m += ponCampoVarint(dm + m, 1, t.bateria_pct);
  if (t.voltaje != 0.0f)   m += ponCampoFixed32(dm + m, 2, floatABits(t.voltaje));
  if (t.uso_canal != 0.0f) m += ponCampoFixed32(dm + m, 3, floatABits(t.uso_canal));
  if (t.uso_tx != 0.0f)    m += ponCampoFixed32(dm + m, 4, floatABits(t.uso_tx));
  if (t.uptime_s)    m += ponCampoVarint(dm + m, 5, t.uptime_s);

  size_t n = 0;
  if (t.tiempo) n += ponCampoFixed32(out + n, 1, t.tiempo);
  if (m) {
    out[n++] = (uint8_t)((2 << 3) | 2);
    n += ponVarint(out + n, m);
    memcpy(out + n, dm, m);
    n += m;
  }
  return n;
}

bool decodificarTelemetry(const uint8_t* in, size_t len, Telemetry* t) {
  *t = Telemetry{};
  Telemetry* q = t;
  bool ok = true;
  const bool r = recorrer(in, len, [q, &ok](uint32_t campo, uint8_t tipo,
                                            const uint8_t* p, size_t n, uint64_t v) {
    if (campo == 1 && tipo == 5) { q->tiempo = (uint32_t)v; return; }
    if (campo != 2 || tipo != 2) return;

    q->tiene_metrics = true;
    ok = recorrer(p, n, [q](uint32_t c, uint8_t ti, const uint8_t*, size_t, uint64_t w) {
      switch (c) {
        case 1: if (ti == 0) q->bateria_pct = (uint32_t)w; break;
        case 2: if (ti == 5) q->voltaje     = bitsAFloat((uint32_t)w); break;
        case 3: if (ti == 5) q->uso_canal   = bitsAFloat((uint32_t)w); break;
        case 4: if (ti == 5) q->uso_tx      = bitsAFloat((uint32_t)w); break;
        case 5: if (ti == 0) q->uptime_s    = (uint32_t)w; break;
        default: break;
      }
    }) && ok;
  });
  return r && ok;
}

// --- Routing ------------------------------------------------------------------

size_t codificarRoutingAck(uint8_t* out, size_t cap) {
  if (cap < 2) return 0;
  out[0] = 0x18;               // (field 3 << 3) | varint: error_reason
  out[1] = (uint8_t)ROUTING_ERROR_NONE;
  return 2;
}

bool decodificarRoutingError(const uint8_t* in, size_t len, uint32_t* error) {
  bool hay = false;
  uint32_t e = 0;
  const bool r = recorrer(in, len, [&hay, &e](uint32_t campo, uint8_t tipo,
                                              const uint8_t*, size_t, uint64_t v) {
    if (campo == 3 && tipo == 0) { hay = true; e = (uint32_t)v; }
  });
  if (!r || !hay) return false;
  if (error) *error = e;
  return true;
}

}  // namespace proto
