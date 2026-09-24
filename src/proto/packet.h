#pragma once
// Cabecera de 16 bytes y mensaje Data del protocolo Meshtastic.
//
// Disposicion en el aire (RadioInterface.h). Todo little-endian:
//
//   0x00  4  to           0xFFFFFFFF = difusion
//   0x04  4  from         si llega a 0, se descarta el paquete
//   0x08  4  id           nunca 0
//   0x0C  1  flags        hop_limit(0-2) want_ack(3) via_mqtt(4) hop_start(5-7)
//   0x0D  1  channel      hash del canal
//   0x0E  1  next_hop     ultimo byte del siguiente salto; 0 = sin preferencia
//   0x0F  1  relay_node   ultimo byte de quien retransmite; 0 = sin asignar
//   0x10  n  payload      Data protobuf, cifrado. Maximo 239 bytes.
//
// Sin dependencias de Arduino.

#include <stddef.h>
#include <stdint.h>

namespace proto {

struct Cabecera {
  uint32_t to         = 0;
  uint32_t from       = 0;
  uint32_t id         = 0;
  uint8_t  hop_limit  = 0;
  uint8_t  hop_start  = 0;
  bool     want_ack   = false;
  bool     via_mqtt   = false;
  uint8_t  canal      = 0;
  uint8_t  next_hop   = 0;
  uint8_t  relay_node = 0;
};

// Serializa y analiza los 16 bytes. desempaquetar() aplica la regla de
// compatibilidad: si hop_start llega a 0 el emisor es anterior a Meshtastic 2.5
// y sus bytes next_hop / relay_node no son de fiar, asi que se fuerzan a 0.
void empaquetarCabecera(const Cabecera& c, uint8_t out[16]);
bool desempaquetarCabecera(const uint8_t in[16], Cabecera* c);

// --- mensaje Data ------------------------------------------------------------
// Solo se implementan los campos que hacen falta:
//   campo 1  portnum        varint
//   campo 2  payload        bytes (max 233)
//   campo 3  want_response  varint (bool)
//   campo 6  request_id     fixed32 (el ACK de un mensaje directo)
// El analizador salta correctamente cualquier otro campo, incluidos los
// fixed32 (dest, source, request_id, reply_id, emoji), que NO son varint.
//
// Se escriben en orden ascendente de numero de campo, que es lo que hace el
// nanopb de Meshtastic: 1, luego 2, luego 3.

struct Data {
  uint32_t portnum = 0;
  uint8_t  payload[233] = {};
  size_t   payload_len = 0;

  // campo 3, varint: want_response (mesh.proto, message Data)
  //
  // "Contesta a esto." Lo usa el descubrimiento de nodos: un NODEINFO con esto
  // puesto significa "aqui estoy, y tu quien eres", y el que lo recibe devuelve
  // el suyo. Sin este campo el descubrimiento solo funciona por casualidad,
  // cuando los dos lados se desconocen a la vez.
  //
  // Se codifica SOLO si es true: proto3 omite los valores por defecto, asi que
  // los vectores de docs/VECTORES.md siguen saliendo byte a byte iguales.
  bool     want_response = false;

  // field 6, fixed32: request_id (mesh.proto, message Data)
  //
  // Id of the packet this one answers. A ROUTING_APP ACK carries the id of the
  // direct message it confirms here. Encoded only when non-zero, like
  // want_response, so the published vectors stay byte-identical.
  uint32_t request_id = 0;
};

// Devuelve los bytes escritos, o 0 si no cabe.
size_t codificarData(const Data& d, uint8_t* out, size_t out_cap);
bool   decodificarData(const uint8_t* in, size_t len, Data* d);

// --- paquete completo --------------------------------------------------------

// Construye cabecera + Data cifrado. Devuelve la longitud total o 0 si falla.
size_t construirPaquete(const Cabecera& c, const Data& d,
                        const uint8_t* key, size_t key_len,
                        uint8_t* out, size_t out_cap);

// Analiza un paquete recibido. Devuelve true solo si el protobuf resulta
// coherente Y portnum != 0: el modo contador no lleva comprobacion de
// integridad, asi que ese es el unico criterio para saber si la clave era la
// correcta.
bool analizarPaquete(const uint8_t* in, size_t len,
                     const uint8_t* key, size_t key_len,
                     Cabecera* c, Data* d);

// Reempaqueta una cabecera modificada sobre un paquete ya recibido, sin tocar
// la carga util. Es lo que hace un nodo al retransmitir: decrementar el
// contador de saltos y firmar el relevo, SIN descifrar nada.
void reescribirCabecera(uint8_t* paquete, const Cabecera& c);

// Genera un identificador de paquete: contador rodante de 10 bits mas 22 bits
// altos aleatorios. Router.cpp::generatePacketId. Nunca devuelve 0.
uint32_t generarId(uint32_t aleatorio_22bits);

}  // namespace proto
