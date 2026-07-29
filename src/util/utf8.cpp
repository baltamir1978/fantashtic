#include "util/utf8.h"

#include <string.h>

namespace util {

uint8_t longitudCaracter(uint8_t primero) {
  if ((primero & 0x80) == 0x00) return 1;   // 0xxxxxxx
  if ((primero & 0xE0) == 0xC0) return 2;   // 110xxxxx
  if ((primero & 0xF0) == 0xE0) return 3;   // 1110xxxx
  if ((primero & 0xF8) == 0xF0) return 4;   // 11110xxx
  return 0;                                  // continuacion o invalido
}

size_t longitudSegura(const char* s, size_t max_bytes) {
  size_t i = 0;
  while (s[i]) {
    const uint8_t n = longitudCaracter((uint8_t)s[i]);
    if (n == 0) return i;                    // byte invalido: cortamos aqui
    if (i + n > max_bytes) return i;          // no cabe entero: no lo partimos
    // Los bytes de continuacion deben serlo de verdad.
    for (uint8_t k = 1; k < n; k++)
      if (((uint8_t)s[i + k] & 0xC0) != 0x80) return i;
    i += n;
  }
  return i;
}

void truncarSeguro(char* s, size_t max_bytes) {
  s[longitudSegura(s, max_bytes)] = '\0';
}

size_t contarCaracteres(const char* s) {
  size_t n = 0;
  for (size_t i = 0; s[i];) {
    const uint8_t k = longitudCaracter((uint8_t)s[i]);
    if (k == 0) { i++; continue; }
    i += k;
    n++;
  }
  return n;
}

bool esUtf8Valido(const char* s) {
  for (size_t i = 0; s[i];) {
    const uint8_t n = longitudCaracter((uint8_t)s[i]);
    if (n == 0) return false;
    for (uint8_t k = 1; k < n; k++) {
      if (!s[i + k]) return false;
      if (((uint8_t)s[i + k] & 0xC0) != 0x80) return false;
    }
    i += n;
  }
  return true;
}

}  // namespace util
