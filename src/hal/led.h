#pragma once
// LED RGB (WS2812, GPIO 21).
//
// AVISO DE HARDWARE: el LED comparte el rail de alimentacion con la
// retroiluminacion de la pantalla (GPIO 38). Con la pantalla apagada el LED
// NO puede encenderse. No existe notificacion luminosa de bajo consumo.
//
// GPIO 21 CONFIRMADO: la tabla _pin_table_other0 (RGBLED) de M5Unified asocia
// board_M5CardputerADV a GPIO_NUM_21. No es solo el variant.h de Meshtastic.

#include <stdint.h>

namespace hal {

void ledBegin();

// Color inmediato. Apagar con ledOff().
void ledSet(uint8_t r, uint8_t g, uint8_t b);
void ledOff();

// Parpadeo no bloqueante: llamar a ledUpdate() en cada tick.
// periodo_ms = 0 detiene el parpadeo dejando el LED encendido.
void ledBlink(uint8_t r, uint8_t g, uint8_t b, uint16_t periodo_ms);
void ledUpdate();

}  // namespace hal
