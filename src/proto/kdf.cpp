#include "kdf.h"

#include <string.h>

#include "config.h"   // PBKDF2_SALT, PBKDF2_ITERS

namespace proto {
namespace {

// --- SHA-256 -----------------------------------------------------------------
const uint32_t K[64] = {
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1,
    0x923f82a4, 0xab1c5ed5, 0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3,
    0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174, 0xe49b69c1, 0xefbe4786,
    0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
    0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147,
    0x06ca6351, 0x14292967, 0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13,
    0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85, 0xa2bfe8a1, 0xa81a664b,
    0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
    0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a,
    0x5b9cca4f, 0x682e6ff3, 0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208,
    0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2};

inline uint32_t rotr(uint32_t x, int n) { return (x >> n) | (x << (32 - n)); }

struct Sha256Ctx {
  uint32_t h[8];
  uint8_t  buf[64];
  size_t   buf_n;
  uint64_t total;
};

void shaInit(Sha256Ctx* c) {
  c->h[0] = 0x6a09e667; c->h[1] = 0xbb67ae85; c->h[2] = 0x3c6ef372;
  c->h[3] = 0xa54ff53a; c->h[4] = 0x510e527f; c->h[5] = 0x9b05688c;
  c->h[6] = 0x1f83d9ab; c->h[7] = 0x5be0cd19;
  c->buf_n = 0; c->total = 0;
}

void shaBloque(Sha256Ctx* c, const uint8_t* p) {
  uint32_t w[64];
  for (int i = 0; i < 16; i++)
    w[i] = ((uint32_t)p[i * 4] << 24) | ((uint32_t)p[i * 4 + 1] << 16) |
           ((uint32_t)p[i * 4 + 2] << 8) | (uint32_t)p[i * 4 + 3];
  for (int i = 16; i < 64; i++) {
    const uint32_t s0 = rotr(w[i - 15], 7) ^ rotr(w[i - 15], 18) ^ (w[i - 15] >> 3);
    const uint32_t s1 = rotr(w[i - 2], 17) ^ rotr(w[i - 2], 19) ^ (w[i - 2] >> 10);
    w[i] = w[i - 16] + s0 + w[i - 7] + s1;
  }

  uint32_t a = c->h[0], b = c->h[1], cc = c->h[2], d = c->h[3];
  uint32_t e = c->h[4], f = c->h[5], g = c->h[6], hh = c->h[7];

  for (int i = 0; i < 64; i++) {
    const uint32_t S1  = rotr(e, 6) ^ rotr(e, 11) ^ rotr(e, 25);
    const uint32_t ch  = (e & f) ^ ((~e) & g);
    const uint32_t t1  = hh + S1 + ch + K[i] + w[i];
    const uint32_t S0  = rotr(a, 2) ^ rotr(a, 13) ^ rotr(a, 22);
    const uint32_t maj = (a & b) ^ (a & cc) ^ (b & cc);
    const uint32_t t2  = S0 + maj;
    hh = g; g = f; f = e; e = d + t1;
    d = cc; cc = b; b = a; a = t1 + t2;
  }
  c->h[0] += a; c->h[1] += b; c->h[2] += cc; c->h[3] += d;
  c->h[4] += e; c->h[5] += f; c->h[6] += g;  c->h[7] += hh;
}

void shaUpdate(Sha256Ctx* c, const uint8_t* p, size_t n) {
  c->total += n;
  while (n) {
    const size_t k = (64 - c->buf_n) < n ? (64 - c->buf_n) : n;
    memcpy(c->buf + c->buf_n, p, k);
    c->buf_n += k; p += k; n -= k;
    if (c->buf_n == 64) { shaBloque(c, c->buf); c->buf_n = 0; }
  }
}

void shaFinal(Sha256Ctx* c, uint8_t out[32]) {
  const uint64_t bits = c->total * 8;
  uint8_t pad = 0x80;
  shaUpdate(c, &pad, 1);
  pad = 0x00;
  while (c->buf_n != 56) shaUpdate(c, &pad, 1);
  uint8_t len[8];
  for (int i = 0; i < 8; i++) len[i] = (uint8_t)(bits >> (56 - i * 8));
  shaUpdate(c, len, 8);
  for (int i = 0; i < 8; i++) {
    out[i * 4]     = (uint8_t)(c->h[i] >> 24);
    out[i * 4 + 1] = (uint8_t)(c->h[i] >> 16);
    out[i * 4 + 2] = (uint8_t)(c->h[i] >> 8);
    out[i * 4 + 3] = (uint8_t)(c->h[i]);
  }
}

const char BASE32[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ234567";

}  // namespace

void sha256(const uint8_t* datos, size_t len, uint8_t out[SHA256_LEN]) {
  Sha256Ctx c;
  shaInit(&c);
  shaUpdate(&c, datos, len);
  shaFinal(&c, out);
}

void hmacSha256(const uint8_t* clave, size_t clave_len,
                const uint8_t* datos, size_t datos_len,
                uint8_t out[SHA256_LEN]) {
  uint8_t k[64] = {};
  if (clave_len > 64) sha256(clave, clave_len, k);
  else                memcpy(k, clave, clave_len);

  uint8_t ipad[64], opad[64];
  for (int i = 0; i < 64; i++) {
    ipad[i] = (uint8_t)(k[i] ^ 0x36);
    opad[i] = (uint8_t)(k[i] ^ 0x5C);
  }

  Sha256Ctx c;
  uint8_t interno[32];
  shaInit(&c);
  shaUpdate(&c, ipad, 64);
  shaUpdate(&c, datos, datos_len);
  shaFinal(&c, interno);

  shaInit(&c);
  shaUpdate(&c, opad, 64);
  shaUpdate(&c, interno, 32);
  shaFinal(&c, out);
}

void pbkdf2Sha256(const uint8_t* pass, size_t pass_len,
                  const uint8_t* salt, size_t salt_len,
                  uint32_t iteraciones, uint8_t* out, size_t out_len) {
  uint8_t bloque[64 + 4];
  if (salt_len > 64) salt_len = 64;

  uint32_t indice = 1;
  size_t escrito = 0;

  while (escrito < out_len) {
    memcpy(bloque, salt, salt_len);
    bloque[salt_len + 0] = (uint8_t)(indice >> 24);
    bloque[salt_len + 1] = (uint8_t)(indice >> 16);
    bloque[salt_len + 2] = (uint8_t)(indice >> 8);
    bloque[salt_len + 3] = (uint8_t)(indice);

    uint8_t u[32], acum[32];
    hmacSha256(pass, pass_len, bloque, salt_len + 4, u);
    memcpy(acum, u, 32);

    for (uint32_t it = 1; it < iteraciones; it++) {
      hmacSha256(pass, pass_len, u, 32, u);
      for (int i = 0; i < 32; i++) acum[i] ^= u[i];
    }

    const size_t n = (out_len - escrito) < 32 ? (out_len - escrito) : 32;
    memcpy(out + escrito, acum, n);
    escrito += n;
    indice++;
  }
}

bool derivarCanal(const char* frase, uint8_t psk[32],
                  char* nombre, size_t nombre_cap) {
  if (!frase || !*frase || nombre_cap < 9) return false;

  const size_t n = strlen(frase);
  pbkdf2Sha256((const uint8_t*)frase, n,
               (const uint8_t*)PBKDF2_SALT, strlen(PBKDF2_SALT),
               PBKDF2_ITERS, psk, 32);

  uint8_t h[32];
  sha256((const uint8_t*)frase, n, h);

  // base32 de los primeros bytes; nos quedamos con 5 caracteres.
  nombre[0] = 'F'; nombre[1] = 'T'; nombre[2] = '-';
  uint32_t acc = 0;
  int bits = 0, escritos = 0;
  for (int i = 0; i < 8 && escritos < 5; i++) {
    acc = (acc << 8) | h[i];
    bits += 8;
    while (bits >= 5 && escritos < 5) {
      bits -= 5;
      nombre[3 + escritos++] = BASE32[(acc >> bits) & 0x1F];
    }
  }
  nombre[8] = '\0';
  return true;
}

}  // namespace proto
