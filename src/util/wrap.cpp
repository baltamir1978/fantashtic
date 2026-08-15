#include "util/wrap.h"

#include "util/utf8.h"

namespace util {
namespace {

// Buscar el espacio byte a byte es seguro en UTF-8: los bytes de continuacion
// son siempre >= 0x80 y nunca pueden valer 0x20.
size_t finDePalabra(const char* s, size_t i) {
  while (s[i] && s[i] != ' ') i++;
  return i;
}

size_t saltarEspacios(const char* s, size_t i) {
  while (s[i] == ' ') i++;
  return i;
}

}  // namespace

size_t envolver(const char* s, int ancho_px, MedirFn medir, void* ctx,
                Trozo* out, size_t max_trozos) {
  if (!s || !medir || !out || max_trozos == 0) return 0;

  if (s[0] == '\0') {
    out[0].ini = 0;
    out[0].len = 0;
    return 1;
  }

  size_t n = 0;
  size_t i = 0;

  while (s[i] && n < max_trozos) {
    i = saltarEspacios(s, i);
    if (!s[i]) break;

    // Ultimo final de palabra que cabe en la linea, medido desde 'i'. Se mide
    // el trozo entero en cada paso, no palabra a palabra sumando: asi la
    // medida es exactamente la que hara el que pinte.
    size_t corte = 0;
    size_t j = i;
    while (s[j]) {
      const size_t fin = finDePalabra(s, j);
      if (medir(s + i, fin - i, ctx) > ancho_px) break;
      corte = fin;
      j = saltarEspacios(s, fin);
      if (j == fin) break;              // no habia espacio: era la ultima
    }

    if (corte == 0) {
      // Ni la primera palabra cabe. Se parte por caracter, y se emite al menos
      // uno aunque se salga: si no, con un ancho ridiculo el bucle no
      // avanzaria nunca.
      size_t k = i;
      while (s[k]) {
        const uint8_t p = longitudCaracter((uint8_t)s[k]);
        const size_t sig = k + (p ? p : 1);   // byte invalido: avanzar uno
        if (k > i && medir(s + i, sig - i, ctx) > ancho_px) break;
        k = sig;
      }
      corte = k;
    }

    out[n].ini = (uint16_t)i;
    out[n].len = (uint16_t)(corte - i);
    n++;
    i = corte;
  }

  return n;
}

}  // namespace util
