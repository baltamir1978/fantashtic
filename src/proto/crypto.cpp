#include "crypto.h"

#include <string.h>

#include "aes.h"

namespace proto {

const uint8_t PSK_POR_DEFECTO[16] = {
    0xD4, 0xF1, 0xBB, 0x3A, 0x20, 0x29, 0x07, 0x59,
    0xF0, 0xBC, 0xFF, 0xAB, 0xCF, 0x4E, 0x69, 0x01};

void construirNonce(uint8_t nonce[16], uint32_t packet_id, uint32_t from) {
  memset(nonce, 0, 16);
  // El id se escribe como uint64 little-endian: 4 bytes utiles y 4 ceros.
  nonce[0] = (uint8_t)(packet_id      );
  nonce[1] = (uint8_t)(packet_id >>  8);
  nonce[2] = (uint8_t)(packet_id >> 16);
  nonce[3] = (uint8_t)(packet_id >> 24);
  nonce[8]  = (uint8_t)(from      );
  nonce[9]  = (uint8_t)(from >>  8);
  nonce[10] = (uint8_t)(from >> 16);
  nonce[11] = (uint8_t)(from >> 24);
  // nonce[12..15] = contador CTR, arranca en cero.
}

bool ctrXcrypt(const uint8_t* key, size_t key_len, const uint8_t nonce[16],
               const uint8_t* entrada, uint8_t* salida, size_t len) {
  Aes aes;
  if (!aes.init(key, key_len)) return false;

  uint8_t contador[16];
  memcpy(contador, nonce, 16);

  uint8_t flujo[16];
  size_t hecho = 0;
  while (hecho < len) {
    aes.encryptBlock(contador, flujo);

    const size_t n = (len - hecho) < 16 ? (len - hecho) : 16;
    for (size_t i = 0; i < n; i++) salida[hecho + i] = (uint8_t)(entrada[hecho + i] ^ flujo[i]);
    hecho += n;

    // Incremento big-endian sobre los ultimos 4 bytes: setCounterSize(4).
    for (int i = 15; i >= 12; i--) {
      if (++contador[i] != 0) break;
    }
  }
  return true;
}

uint8_t xorHash(const uint8_t* p, size_t n) {
  uint8_t c = 0;
  for (size_t i = 0; i < n; i++) c ^= p[i];
  return c;
}

uint8_t hashCanal(const char* nombre, const uint8_t* key, size_t key_len) {
  return (uint8_t)(xorHash((const uint8_t*)nombre, strlen(nombre)) ^
                   xorHash(key, key_len));
}

uint32_t djb2(const char* s) {
  uint32_t h = 5381;
  for (const char* p = s; *p; p++) h = (h * 33u) + (uint8_t)(*p);
  return h;
}

float frecuenciaCanal(float freq_inicio, float freq_fin, float bw_khz,
                      const char* nombre_canal, int* num_canales) {
  const float ancho = bw_khz / 1000.0f;                 // en MHz
  int n = (int)((freq_fin - freq_inicio) / ancho);
  if (n < 1) n = 1;
  if (num_canales) *num_canales = n;
  const int idx = (int)(djb2(nombre_canal) % (uint32_t)n);
  return freq_inicio + (bw_khz / 2000.0f) + (float)idx * ancho;
}

uint8_t ultimoByte(uint32_t node_num) {
  const uint8_t b = (uint8_t)(node_num & 0xFF);
  return b ? b : 0xFF;
}

}  // namespace proto
