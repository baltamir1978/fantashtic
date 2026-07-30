#pragma once
// Interfaz: cinco pantallas, una superposicion de SOS y el asistente de
// primer arranque.
//
// La pantalla mide 240 x 135 pixeles, lo que deja sitio para cinco lineas de
// conversacion. Esa limitacion manda: los nombres se abrevian a cuatro
// caracteres y cada nodo lleva un color propio, de modo que un nino identifica
// al remitente por el color sin tener que leer.

#include <stdint.h>

#include "hal/keys.h"

namespace ui {

void begin();

// Repinta la pantalla actual.
void pintar();

// Procesa una tecla. Devuelve true si hay que repintar.
bool tecla(const hal::Evento& ev);

// Llamar en cada tick: apagado por inactividad, parpadeos, refrescos.
void loop();

// Enciende la pantalla y reinicia el contador de inactividad.
void despertar();
bool pantallaEncendida();

// Avisos permanentes de la barra inferior.
void ponerAviso(const char* texto);
void quitarAviso();

}  // namespace ui
