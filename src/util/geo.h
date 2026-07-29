#pragma once
// Distancia y rumbo entre dos posiciones.
//
// Las coordenadas van en grados * 1e7 y como enteros de 32 bits, que es como
// las transporta el mensaje Position de Meshtastic. Trabajar con enteros evita
// perder precision al serializar: en punto flotante de 32 bits, 1e-7 grados ya
// esta en el limite de la resolucion.

#include <stddef.h>
#include <stdint.h>

namespace util {

// Distancia sobre la superficie terrestre, en metros (formula del haversine,
// radio medio 6 371 000 m). Precision de sobra para el uso previsto.
uint32_t distanciaM(int32_t lat1_e7, int32_t lon1_e7,
                    int32_t lat2_e7, int32_t lon2_e7);

// Rumbo inicial del punto 1 al 2, en grados desde el norte (0-359).
uint16_t rumboGrados(int32_t lat1_e7, int32_t lon1_e7,
                     int32_t lat2_e7, int32_t lon2_e7);

// Texto compacto para una pantalla de 240 pixeles: metros hasta 999, luego
// kilometros con un decimal y coma como separador.
void formatearDistancia(uint32_t metros, char* out, size_t cap);

// Punto cardinal abreviado (N, NE, E, SE, S, SO, O, NO).
const char* puntoCardinal(uint16_t rumbo);

}  // namespace util
