#include "aes.h"

#include <string.h>

namespace proto {
namespace {

uint8_t g_sbox[256];
bool    g_sbox_listo = false;

inline uint8_t rotl8(uint8_t x, uint8_t s) {
  return (uint8_t)((uint8_t)(x << s) | (uint8_t)(x >> (8 - s)));
}

// Genera la S-box: inverso multiplicativo en GF(2^8) seguido de la
// transformacion afin. Recorre el cuerpo con p = 3^i y q = 3^-i.
void initSbox() {
  if (g_sbox_listo) return;
  uint8_t p = 1, q = 1;
  do {
    p = (uint8_t)(p ^ (uint8_t)(p << 1) ^ ((p & 0x80) ? 0x1B : 0x00));
    q ^= (uint8_t)(q << 1);
    q ^= (uint8_t)(q << 2);
    q ^= (uint8_t)(q << 4);
    if (q & 0x80) q ^= 0x09;
    const uint8_t x = (uint8_t)(q ^ rotl8(q, 1) ^ rotl8(q, 2) ^ rotl8(q, 3) ^ rotl8(q, 4));
    g_sbox[p] = (uint8_t)(x ^ 0x63);
  } while (p != 1);
  g_sbox[0] = 0x63;
  g_sbox_listo = true;
}

const uint8_t RCON[11] = {0x8D, 0x01, 0x02, 0x04, 0x08, 0x10,
                          0x20, 0x40, 0x80, 0x1B, 0x36};

inline uint8_t xtime(uint8_t x) {
  return (uint8_t)((uint8_t)(x << 1) ^ ((x & 0x80) ? 0x1B : 0x00));
}

typedef uint8_t estado_t[4][4];   // estado[i] es la columna i (bytes i*4..i*4+3)

void subBytes(estado_t* s) {
  for (int i = 0; i < 4; i++)
    for (int j = 0; j < 4; j++) (*s)[i][j] = g_sbox[(*s)[i][j]];
}

void shiftRows(estado_t* s) {
  uint8_t t;
  // fila 1: rotacion de 1
  t = (*s)[0][1]; (*s)[0][1] = (*s)[1][1]; (*s)[1][1] = (*s)[2][1];
  (*s)[2][1] = (*s)[3][1]; (*s)[3][1] = t;
  // fila 2: rotacion de 2
  t = (*s)[0][2]; (*s)[0][2] = (*s)[2][2]; (*s)[2][2] = t;
  t = (*s)[1][2]; (*s)[1][2] = (*s)[3][2]; (*s)[3][2] = t;
  // fila 3: rotacion de 3
  t = (*s)[0][3]; (*s)[0][3] = (*s)[3][3]; (*s)[3][3] = (*s)[2][3];
  (*s)[2][3] = (*s)[1][3]; (*s)[1][3] = t;
}

void mixColumns(estado_t* s) {
  for (int i = 0; i < 4; i++) {
    const uint8_t t   = (*s)[i][0];
    const uint8_t tot = (uint8_t)((*s)[i][0] ^ (*s)[i][1] ^ (*s)[i][2] ^ (*s)[i][3]);
    uint8_t m;
    m = xtime((uint8_t)((*s)[i][0] ^ (*s)[i][1])); (*s)[i][0] ^= (uint8_t)(m ^ tot);
    m = xtime((uint8_t)((*s)[i][1] ^ (*s)[i][2])); (*s)[i][1] ^= (uint8_t)(m ^ tot);
    m = xtime((uint8_t)((*s)[i][2] ^ (*s)[i][3])); (*s)[i][2] ^= (uint8_t)(m ^ tot);
    m = xtime((uint8_t)((*s)[i][3] ^ t));          (*s)[i][3] ^= (uint8_t)(m ^ tot);
  }
}

void addRoundKey(estado_t* s, const uint8_t* rk, uint8_t ronda) {
  for (int i = 0; i < 4; i++)
    for (int j = 0; j < 4; j++) (*s)[i][j] ^= rk[ronda * 16 + i * 4 + j];
}

}  // namespace

bool Aes::init(const uint8_t* key, size_t key_len) {
  if (key_len != 16 && key_len != 32) { rondas_ = 0; return false; }
  initSbox();

  const int nk = (int)(key_len / 4);      // 4 o 8 palabras
  rondas_ = (uint8_t)(nk + 6);            // 10 o 14

  memcpy(clave_, key, key_len);

  uint8_t tmp[4];
  for (int i = nk; i < 4 * (rondas_ + 1); i++) {
    const int k = (i - 1) * 4;
    tmp[0] = clave_[k + 0]; tmp[1] = clave_[k + 1];
    tmp[2] = clave_[k + 2]; tmp[3] = clave_[k + 3];

    if (i % nk == 0) {
      const uint8_t t = tmp[0];           // RotWord
      tmp[0] = tmp[1]; tmp[1] = tmp[2]; tmp[2] = tmp[3]; tmp[3] = t;
      for (int j = 0; j < 4; j++) tmp[j] = g_sbox[tmp[j]];   // SubWord
      tmp[0] ^= RCON[i / nk];
    } else if (nk > 6 && i % nk == 4) {
      for (int j = 0; j < 4; j++) tmp[j] = g_sbox[tmp[j]];
    }

    const int j = i * 4, m = (i - nk) * 4;
    for (int b = 0; b < 4; b++) clave_[j + b] = (uint8_t)(clave_[m + b] ^ tmp[b]);
  }
  return true;
}

void Aes::encryptBlock(const uint8_t in[16], uint8_t out[16]) const {
  if (!rondas_) return;

  estado_t s;
  memcpy(s, in, 16);

  addRoundKey(&s, clave_, 0);
  for (uint8_t r = 1; r < rondas_; r++) {
    subBytes(&s);
    shiftRows(&s);
    mixColumns(&s);
    addRoundKey(&s, clave_, r);
  }
  subBytes(&s);
  shiftRows(&s);
  addRoundKey(&s, clave_, rondas_);

  memcpy(out, s, 16);
}

}  // namespace proto
