#include "packet.h"

#include <string.h>

#include "crypto.h"

namespace proto {
namespace {

constexpr uint8_t FLAG_HOP_LIMIT = 0x07;
constexpr uint8_t FLAG_WANT_ACK  = 0x08;
constexpr uint8_t FLAG_VIA_MQTT  = 0x10;
constexpr uint8_t FLAG_HOP_START = 0xE0;
constexpr uint8_t SHIFT_HOP_START = 5;

constexpr size_t CABECERA_LEN   = 16;
constexpr size_t MAX_CIFRADO    = 239;   // 255 - 16
constexpr size_t MAX_DATA_PAYLD = 233;   // mesh.options

void escribirU32(uint8_t* p, uint32_t v) {
  p[0] = (uint8_t)(v      ); p[1] = (uint8_t)(v >>  8);
  p[2] = (uint8_t)(v >> 16); p[3] = (uint8_t)(v >> 24);
}
uint32_t leerU32(const uint8_t* p) {
  return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
         ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

// Lector de varint. Devuelve false si se sale del buffer.
bool leerVarint(const uint8_t* in, size_t len, size_t* pos, uint64_t* out) {
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

size_t escribirVarint(uint8_t* out, uint64_t v) {
  size_t n = 0;
  do {
    uint8_t b = (uint8_t)(v & 0x7F);
    v >>= 7;
    if (v) b |= 0x80;
    out[n++] = b;
  } while (v);
  return n;
}

}  // namespace

// =============================== cabecera ===================================

void empaquetarCabecera(const Cabecera& c, uint8_t out[16]) {
  escribirU32(out + 0, c.to);
  escribirU32(out + 4, c.from);
  escribirU32(out + 8, c.id);
  out[12] = (uint8_t)((c.hop_limit & FLAG_HOP_LIMIT) |
                      (c.want_ack ? FLAG_WANT_ACK : 0) |
                      (c.via_mqtt ? FLAG_VIA_MQTT : 0) |
                      (uint8_t)((c.hop_start & 0x07) << SHIFT_HOP_START));
  out[13] = c.canal;
  out[14] = c.next_hop;
  out[15] = c.relay_node;
}

bool desempaquetarCabecera(const uint8_t in[16], Cabecera* c) {
  c->to   = leerU32(in + 0);
  c->from = leerU32(in + 4);
  c->id   = leerU32(in + 8);

  const uint8_t f = in[12];
  c->hop_limit = (uint8_t)(f & FLAG_HOP_LIMIT);
  c->want_ack  = (f & FLAG_WANT_ACK) != 0;
  c->via_mqtt  = (f & FLAG_VIA_MQTT) != 0;
  c->hop_start = (uint8_t)((f & FLAG_HOP_START) >> SHIFT_HOP_START);

  c->canal      = in[13];
  c->next_hop   = in[14];
  c->relay_node = in[15];

  // Firmware anterior a 2.5: esos dos bytes no significaban nada.
  if (c->hop_start == 0) { c->next_hop = 0; c->relay_node = 0; }

  // from == 0 no es valido; id == 0 tampoco (no se registra ni se retransmite).
  return c->from != 0 && c->id != 0;
}

// ================================= Data =====================================

size_t codificarData(const Data& d, uint8_t* out, size_t out_cap) {
  if (d.payload_len > MAX_DATA_PAYLD) return 0;

  size_t n = 0;
  // campo 1, varint: portnum
  if (n + 1 > out_cap) return 0;
  out[n++] = 0x08;
  if (n + 5 > out_cap) return 0;
  n += escribirVarint(out + n, d.portnum);

  // campo 2, delimitado por longitud: payload
  if (d.payload_len) {
    if (n + 1 > out_cap) return 0;
    out[n++] = 0x12;
    if (n + 5 > out_cap) return 0;
    n += escribirVarint(out + n, d.payload_len);
    if (n + d.payload_len > out_cap) return 0;
    memcpy(out + n, d.payload, d.payload_len);
    n += d.payload_len;
  }

  // campo 3, varint: want_response. SOLO si es true -- proto3 omite los valores
  // por defecto, y de eso depende que los vectores de docs/VECTORES.md sigan
  // saliendo byte a byte iguales.
  if (d.want_response) {
    if (n + 2 > out_cap) return 0;
    out[n++] = 0x18;            // (campo 3 << 3) | varint
    out[n++] = 0x01;
  }

  // field 6, fixed32: request_id. Only when set, same reason as above.
  if (d.request_id) {
    if (n + 5 > out_cap) return 0;
    out[n++] = 0x35;            // (field 6 << 3) | fixed32
    out[n++] = (uint8_t)(d.request_id);
    out[n++] = (uint8_t)(d.request_id >> 8);
    out[n++] = (uint8_t)(d.request_id >> 16);
    out[n++] = (uint8_t)(d.request_id >> 24);
  }
  return n;
}

bool decodificarData(const uint8_t* in, size_t len, Data* d) {
  *d = Data{};
  size_t pos = 0;

  while (pos < len) {
    uint64_t etiqueta = 0;
    if (!leerVarint(in, len, &pos, &etiqueta)) return false;
    const uint32_t campo = (uint32_t)(etiqueta >> 3);
    const uint8_t  tipo  = (uint8_t)(etiqueta & 0x07);

    switch (tipo) {
      case 0: {                                  // varint
        uint64_t v = 0;
        if (!leerVarint(in, len, &pos, &v)) return false;
        if (campo == 1) d->portnum       = (uint32_t)v;
        if (campo == 3) d->want_response = (v != 0);
        break;
      }
      case 2: {                                  // delimitado por longitud
        uint64_t l = 0;
        if (!leerVarint(in, len, &pos, &l)) return false;
        if (pos + l > len) return false;
        if (campo == 2) {
          if (l > MAX_DATA_PAYLD) return false;
          memcpy(d->payload, in + pos, (size_t)l);
          d->payload_len = (size_t)l;
        }
        pos += (size_t)l;
        break;
      }
      case 5:                                    // fixed32: dest, source,
        if (pos + 4 > len) return false;         // request_id, reply_id, emoji
        if (campo == 6)
          d->request_id = (uint32_t)in[pos] | ((uint32_t)in[pos + 1] << 8) |
                          ((uint32_t)in[pos + 2] << 16) | ((uint32_t)in[pos + 3] << 24);
        pos += 4;
        break;
      case 1:                                    // fixed64
        if (pos + 8 > len) return false;
        pos += 8;
        break;
      default:
        return false;                            // grupos: no se usan
    }
  }
  return true;
}

// ============================ paquete completo ==============================

size_t construirPaquete(const Cabecera& c, const Data& d,
                        const uint8_t* key, size_t key_len,
                        uint8_t* out, size_t out_cap) {
  if (out_cap < CABECERA_LEN) return 0;

  uint8_t claro[256];
  const size_t n = codificarData(d, claro, sizeof(claro));
  if (n == 0 || n > MAX_CIFRADO) return 0;
  if (out_cap < CABECERA_LEN + n) return 0;

  empaquetarCabecera(c, out);

  uint8_t nonce[16];
  construirNonce(nonce, c.id, c.from);
  if (!ctrXcrypt(key, key_len, nonce, claro, out + CABECERA_LEN, n)) return 0;

  return CABECERA_LEN + n;
}

bool analizarPaquete(const uint8_t* in, size_t len,
                     const uint8_t* key, size_t key_len,
                     Cabecera* c, Data* d) {
  if (len <= CABECERA_LEN || len > CABECERA_LEN + MAX_CIFRADO) return false;
  if (!desempaquetarCabecera(in, c)) return false;

  const size_t n = len - CABECERA_LEN;
  uint8_t claro[256];

  uint8_t nonce[16];
  construirNonce(nonce, c->id, c->from);
  if (!ctrXcrypt(key, key_len, nonce, in + CABECERA_LEN, claro, n)) return false;

  if (!decodificarData(claro, n, d)) return false;

  // CTR no aporta integridad: si la clave no era la correcta, el descifrado da
  // basura. La unica senal de que acertamos es que el protobuf tenga sentido y
  // que el portnum no sea el desconocido.
  return d->portnum != 0;
}

void reescribirCabecera(uint8_t* paquete, const Cabecera& c) {
  empaquetarCabecera(c, paquete);
}

uint32_t generarId(uint32_t aleatorio_22bits) {
  static uint32_t contador = 0;
  contador = (contador + 1) & 0x3FF;                 // 10 bits
  uint32_t id = ((aleatorio_22bits & 0x3FFFFF) << 10) | contador;
  if (id == 0) id = 1;                               // nunca 0
  return id;
}

}  // namespace proto
