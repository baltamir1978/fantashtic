#pragma once
// Bateria: Li-ion 1750 mAh, medida por ADC en GPIO 10.
//
// No hay PMIC I2C en este aparato: solo un cargador lineal tonto y un divisor
// resistivo. No se puede saber por software si esta cargando, solo la tension.
//
// El divisor es 100k+100k, asi que el multiplicador es 2.0. El variant.h de
// Meshtastic usa 2*1.02, pero ese 1.02 es un fudge deliberado para que la
// tension supere el maximo y la UI muestre "cargando"; para un porcentaje
// real lee un 2 % alto.

#include <stdint.h>

namespace hal {

void powerBegin();

// Tension de bateria en mV, promediada sobre BAT_SAMPLES muestras.
uint16_t batteryMv();

// Porcentaje 0..100, con curva de descarga de Li-ion (no lineal en tension).
uint8_t batteryPct();

// true por debajo de BAT_LOW_PCT.
bool batteryLow();

}  // namespace hal
