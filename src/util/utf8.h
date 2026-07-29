#pragma once
// Manejo seguro de UTF-8.
//
// El limite de mensaje son 180 BYTES, no 180 caracteres. En castellano cada
// acento ocupa 2 bytes y un emoji 4, asi que 180 caracteres podrian ser 720
// bytes. Cortar por el byte 180 a lo bruto parte un caracter por la mitad y
// produce basura en pantalla y en el fichero de historial.

#include <stddef.h>
#include <stdint.h>

namespace util {

// Numero de bytes que caben en 'max_bytes' sin partir ningun caracter.
size_t longitudSegura(const char* s, size_t max_bytes);

// Trunca la cadena en el sitio, respetando los limites de caracter.
void truncarSeguro(char* s, size_t max_bytes);

// Numero de caracteres (puntos de codigo), no de bytes.
size_t contarCaracteres(const char* s);

// Longitud en bytes del caracter que empieza en ese byte. 0 si no es un
// comienzo valido.
uint8_t longitudCaracter(uint8_t primero);

// Valida que toda la cadena sea UTF-8 correcto.
bool esUtf8Valido(const char* s);

}  // namespace util
