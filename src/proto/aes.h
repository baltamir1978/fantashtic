#pragma once
// AES en C puro, solo cifrado de bloque (que es lo unico que necesita el modo
// contador). Sin dependencias: el mismo codigo compila en el ESP32 y en el
// ordenador, de modo que los tests del entorno 'native' ejercitan exactamente
// el codigo que se embarca.
//
// Se admiten claves de 16 bytes (AES-128) y de 32 (AES-256). Meshtastic elige
// una u otra segun la longitud de la clave del canal: la clave por defecto son
// 16 bytes, la nuestra 32.
//
// La tabla de sustitucion se genera al vuelo a partir de su definicion
// algebraica en GF(2^8) en vez de transcribir 256 valores a mano, que es la
// via clasica para colar una errata indetectable.

#include <stddef.h>
#include <stdint.h>

namespace proto {

class Aes {
 public:
  // key_len debe ser 16 o 32. Devuelve false en cualquier otro caso.
  bool init(const uint8_t* key, size_t key_len);

  // Cifra un bloque de 16 bytes. in y out pueden solaparse.
  void encryptBlock(const uint8_t in[16], uint8_t out[16]) const;

  bool listo() const { return rondas_ != 0; }

 private:
  uint8_t clave_[240] = {};   // 16*(14+1) para AES-256
  uint8_t rondas_     = 0;    // 10 o 14
};

}  // namespace proto
