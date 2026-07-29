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

// --- reloj del CPU -----------------------------------------------------------
// Alterna entre CPU_MHZ_RAPIDO y CPU_MHZ_LENTO. Es la medida de autonomia mas
// barata que hay: el CPU a 240 MHz es el mayor consumidor del aparato y con la
// pantalla dormida no hace nada que necesite esa velocidad.
//
// Idempotente: si ya esta en la frecuencia pedida no toca nada, porque
// setCpuFrequencyMhz() reprograma el PLL y detiene el CPU un instante. Por eso
// mismo, llamarla solo con la radio ociosa (ver radio::radioOciosa()): ese
// paron en mitad de una transaccion SPI con el SX1262 no es buena idea.
void cpuRapido(bool rapido);
bool cpuEsRapido();

// --- light sleep -------------------------------------------------------------
// Duerme el CPU hasta 'ms' o hasta que algo lo despierte. Es la medida de
// autonomia mas grande que queda: sustituye al delay() del tick, que solo cede a
// la tarea idle y deja el reloj corriendo.
//
// Fuentes de despertar, las tres GPIO de dominio RTC en el S3 (0-21):
//   DIO1 de la radio (4)   paquete recibido o transmision terminada
//   INT del teclado  (11)  el TCA8418 avisa de una pulsacion
//   boton G0         (0)   SOS
// Mas el temporizador, que es el que marca el tick normal.
//
// millis() va sobre esp_timer, que sigue contando durmiendo: el contador de
// ciclo de trabajo NO se descuadra.
//
// ⚠ DESACTIVADO POR DEFECTO. Se compila con -DFT_LIGHT_SLEEP para retomarlo.
//
// Medido el 29-jul-2026: con el light sleep activo el nodo se queda SORDO Y MUDO
// en cuanto la pantalla se duerme. La hipotesis principal es que
// esp_sleep_enable_ext1_wakeup() pasa el GPIO 4 (DIO1) al dominio RTC y rompe la
// interrupcion que RadioLib registro con setDio1Action(), dejando la maquina de
// estados de la radio parada. Hay que resolver eso antes de encenderlo.
//
// Ademas, el light sleep tumba el USB-Serial-JTAG y se pierde la consola; por eso
// el bucle solo lo usa con la pantalla ya dormida.
void sleepBegin();
bool sleepDisponible();
void dormirHasta(uint32_t ms);

}  // namespace hal
