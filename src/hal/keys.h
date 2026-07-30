#pragma once
// Teclado: 56 teclas en matriz 4x14, leidas por un TCA8418 en el bus I2C
// interno, con INT en GPIO 11.
//
// La libreria M5Cardputer abstrae el escaner, asi que el codigo de arriba no
// deberia saber que hay un TCA8418 debajo. Esta capa traduce el estado crudo
// a eventos de alto nivel.
//
// Las flechas son Fn + ; . , / y la libreria ya las resuelve. Los caracteres
// ';' '.' ',' '/' sin Fn se escriben normalmente.
//
// REQUIERE M5Cardputer >= 1.1.0: el soporte del Cardputer Adv (board
// board_M5CardputerADV y el lector TCA8418) no existe antes. Con 1.0.x
// compila pero el teclado no responde.

#include <stdint.h>

namespace hal {

enum class Tecla : uint8_t {
  NINGUNA = 0,
  CARACTER,     // hay texto en Evento::ch
  ENTER,
  BORRAR,
  TAB,
  ESC,          // Fn+` o la tecla ` a secas
  ARRIBA,
  ABAJO,
  IZQUIERDA,
  DERECHA,
};

struct Evento {
  Tecla tecla = Tecla::NINGUNA;
  char  ch    = 0;      // valido si tecla == CARACTER
  bool  fn    = false;
  bool  shift = false;
  bool  ctrl  = false;
};

void keysBegin();

// Llamar una vez por tick. Devuelve true si hay evento nuevo.
// Solo reporta pulsaciones, no repeticiones ni sueltas.
bool keysPoll(Evento* ev);

// true mientras ESC y DEL esten pulsados A LA VEZ: es el gesto del SOS por
// teclado. NO pasa por keysPoll(), que devuelve un solo evento y se queda con el
// primero, asi que por esa via el combo seria invisible.
//
// ERA ENTER+ESC y se cambio a ESC+DEL el 30-jul-2026, por el riesgo de la SUELTA.
// Las dos teclas no se levantan nunca en el mismo milisegundo, asi que la que
// queda pulsada emite su evento:
//   con ENTER+ESC, soltar ESC primero dejaba ENTER pulsado -> se ENVIABA la linea
//   de chat que hubiera escrita, en medio de armar una emergencia.
//   con ESC+DEL, lo peor que puede pasar es borrar un caracter.
// Siguen estando lejos la una de la otra (esquina inferior izquierda y esquina
// superior derecha), que es lo que hace el gesto dificil por accidente y facil a
// proposito y con prisa.
//
// Mientras esten pulsadas, el bucle principal NO procesa teclas: si no, el DEL
// iria comiendose la linea de chat durante los 2 s que hay que mantener.
bool keysSosCombo();

// Nombre legible de una tecla, para depurar.
const char* keysNombre(Tecla t);

}  // namespace hal
