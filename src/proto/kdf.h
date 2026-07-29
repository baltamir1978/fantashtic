#pragma once
// SHA-256, HMAC-SHA256 y PBKDF2, en C++ puro y sin dependencias.
//
// Por que no mbedTLS: existe en el ESP32 pero no en el ordenador, y entonces
// los tests probarian una implementacion distinta de la que se embarca. Aqui
// el mismo codigo corre en los dos sitios y los vectores lo validan.
//
// Se usa para derivar la clave del canal a partir de una frase. Teclear 64
// caracteres hexadecimales en un teclado de 56 teclas diminutas no es una
// opcion realista; una frase que se escribe igual en los cuatro aparatos, si.

#include <stddef.h>
#include <stdint.h>

namespace proto {

constexpr size_t SHA256_LEN = 32;

void sha256(const uint8_t* datos, size_t len, uint8_t out[SHA256_LEN]);

void hmacSha256(const uint8_t* clave, size_t clave_len,
                const uint8_t* datos, size_t datos_len,
                uint8_t out[SHA256_LEN]);

// PBKDF2-HMAC-SHA256. out_len puede ser cualquiera; para nosotros 32.
void pbkdf2Sha256(const uint8_t* pass, size_t pass_len,
                  const uint8_t* salt, size_t salt_len,
                  uint32_t iteraciones, uint8_t* out, size_t out_len);

// Deriva de la frase del grupo:
//   psk    32 bytes, con PBKDF2(frase, "fantashtic-v1", 20000)
//   nombre "FT-" + los 5 primeros caracteres de base32(SHA256(frase))
//
// El nombre se deriva tambien de la frase para que dos grupos con frases
// distintas no colisionen en el byte de canal de la cabecera.
//
// nombre_cap debe ser >= 9. Devuelve false si la frase esta vacia.
bool derivarCanal(const char* frase, uint8_t psk[32],
                  char* nombre, size_t nombre_cap);

}  // namespace proto
