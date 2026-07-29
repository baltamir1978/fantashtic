#pragma once
// Altavoz: ES8311 + NS4150B, 1 W. El amplificador se desactiva solo al
// enchufar el jack de 3.5 mm.
//
// Todos los tonos son no bloqueantes: M5Unified los reproduce en segundo
// plano. No meter delay() aqui.

#include <stdint.h>

namespace hal {

void soundBegin();
void soundVolumen(uint8_t v);   // 0..255

void beep();          // confirmacion corta
void beepError();     // tono grave de rechazo
void beepMensaje();   // mensaje entrante

// Alarma de SOS: se repite hasta llamar a alarmaParar().
void alarmaSos();
void alarmaParar();
void soundUpdate();   // llamar en cada tick para mantener la alarma

}  // namespace hal
