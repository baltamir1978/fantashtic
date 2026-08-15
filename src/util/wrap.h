#pragma once
// Troceado de un texto en lineas que quepan en un ancho dado.
//
// El chat pintaba cada mensaje en UNA fila y lo truncaba a 22 caracteres, asi
// que todo lo que pasara de ahi se perdia sin avisar. Dos problemas en uno:
//
//   1. Un mensaje de 180 bytes se veia a cuartos y no habia forma de leer el
//      resto desde el aparato.
//   2. Los 22 caracteres eran una cuenta sobre "unos 8 px por caracter", y
//      Font2 es PROPORCIONAL: "MMMMMMMM" se salia de la pantalla y "iiiiiiii"
//      dejaba media fila vacia.
//
// Por eso el ancho se mide y no se cuenta, y por eso la medida entra como
// funcion: 'util/' no depende de Arduino —es lo que permite que estos casos se
// prueben en el ordenador—, y quien llama pasa D.textWidth() o, en los tests,
// una regla falsa y predecible.

#include <stddef.h>
#include <stdint.h>

namespace util {

// Ancho en pixeles de los 'bytes' primeros bytes de 's'. No se puede suponer
// que 's' termine en nul dentro de esos bytes.
using MedirFn = int (*)(const char* s, size_t bytes, void* ctx);

// Un trozo del texto original: desplazamiento y longitud en BYTES. No se copia
// nada, para no duplicar los 200 bytes de cada mensaje en la pila.
struct Trozo {
  uint16_t ini;
  uint16_t len;
};

// Trocea 's' en lineas de como mucho 'ancho_px'. Corta por espacios; si una
// palabra sola no cabe entera, la parte por caracter SIN romper el UTF-8. Los
// espacios del corte no empiezan la linea siguiente.
//
// Devuelve cuantos trozos ha escrito en 'out', nunca mas de 'max_trozos'. Una
// cadena vacia da un trozo vacio, no cero: un mensaje sin texto tiene que
// seguir ocupando su fila con el nombre de quien lo mando.
size_t envolver(const char* s, int ancho_px, MedirFn medir, void* ctx,
                Trozo* out, size_t max_trozos);

}  // namespace util
