#include "util/chatlog.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "util/utf8.h"

namespace util {
namespace {

// Up to 7 fields. The last one takes the rest of the line, tabs and all, but a
// well-formed line has none there.
size_t partir(char* s, char* campos[], size_t max) {
  size_t n = 0;
  campos[n++] = s;
  for (char* p = s; *p && n < max; p++) {
    if (*p == '\t') { *p = '\0'; campos[n++] = p + 1; }
  }
  return n;
}

bool leerHex(const char* s, uint32_t* out) {
  if (!*s) return false;
  char* fin = nullptr;
  const unsigned long v = strtoul(s, &fin, 16);
  if (*fin) return false;
  *out = (uint32_t)v;
  return true;
}

bool leerDec(const char* s, long* out) {
  if (!*s) return false;
  char* fin = nullptr;
  const long v = strtol(s, &fin, 10);
  if (*fin) return false;
  *out = v;
  return true;
}

}  // namespace

size_t formatearLineaChat(const EntradaChat& e, char* out, size_t cap) {
  if (!out || cap == 0) return 0;
  char conv[12];
  if (e.conv) snprintf(conv, sizeof(conv), "%08X", (unsigned)e.conv);
  else        snprintf(conv, sizeof(conv), "*");

  int n = snprintf(out, cap, "%lu\t%08X\t%s\t%d\t%d\t%s\t",
                   (unsigned long)e.epoch, (unsigned)e.nodo, e.tx ? "TX" : "RX",
                   (int)e.rssi, (int)e.snr, conv);
  if (n < 0 || (size_t)n >= cap) { out[0] = '\0'; return 0; }

  // Character by character, never splitting an escape or a UTF-8 sequence:
  // a half-written 'ñ' would show up as garbage when read back.
  const char* p = e.texto;
  while (*p) {
    char esc[2];
    size_t k = 1;
    const char* src = p;
    if (*p == '\n')      { esc[0] = '\\'; esc[1] = 'n';  k = 2; src = esc; }
    else if (*p == '\\') { esc[0] = '\\'; esc[1] = '\\'; k = 2; src = esc; }
    else if (*p == '\t') { esc[0] = ' ';                 k = 1; src = esc; }
    else if (*p == '\r') { p++; continue; }
    else {
      k = longitudCaracter((uint8_t)*p);
      if (k == 0) k = 1;
      // A sequence cut short by the end of the string is not copied.
      if (strnlen(p, k) < k) break;
    }
    if ((size_t)n + k >= cap) break;
    memcpy(out + n, src, k);
    n += (int)k;
    p += (src == esc) ? 1 : k;
  }
  out[n] = '\0';
  return (size_t)n;
}

bool analizarLineaChat(const char* linea, EntradaChat* e) {
  if (!linea || !e) return false;
  *e = EntradaChat{};

  char buf[MSG_BUF_BYTES * 2 + 64];
  snprintf(buf, sizeof(buf), "%s", linea);
  size_t len = strlen(buf);
  while (len && (buf[len - 1] == '\r' || buf[len - 1] == '\n')) buf[--len] = '\0';
  if (len == 0) return false;

  char* f[7];
  const size_t nf = partir(buf, f, 7);
  if (nf != 6 && nf != 7) return false;

  long v = 0;
  if (!leerDec(f[0], &v) || v < 0) return false;
  e->epoch = (uint32_t)v;
  if (!leerHex(f[1], &e->nodo)) return false;
  if (strcmp(f[2], "TX") == 0)      e->tx = true;
  else if (strcmp(f[2], "RX") != 0) return false;
  if (!leerDec(f[3], &v)) return false;
  e->rssi = (int16_t)v;
  if (!leerDec(f[4], &v)) return false;
  e->snr = (int8_t)v;

  const char* texto = f[nf - 1];
  if (nf == 7) {
    if (strcmp(f[5], "*") == 0) e->conv = 0;
    else if (!leerHex(f[5], &e->conv)) return false;
  }

  // Undo the escapes. Anything else after a backslash is kept as is: old lines
  // did not escape '\' itself.
  size_t o = 0;
  for (const char* p = texto; *p && o < sizeof(e->texto) - 1; p++) {
    if (p[0] == '\\' && p[1] == 'n')       { e->texto[o++] = '\n'; p++; }
    else if (p[0] == '\\' && p[1] == '\\') { e->texto[o++] = '\\'; p++; }
    else                                   { e->texto[o++] = *p; }
  }
  e->texto[o] = '\0';
  truncarSeguro(e->texto, sizeof(e->texto) - 1);
  return true;
}

}  // namespace util
