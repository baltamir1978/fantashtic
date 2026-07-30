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
//
// ⚠ DESACTIVADO. Se compila con -DFT_LIGHT_SLEEP para retomarlo.
//
// MEDIDO EL 30-JUL-2026, ya con gpio_wakeup en vez de ext1: el nodo desenchufado
// se queda tonto a los 30 s igual, y de paso el altavoz CREPITA al entrar y salir
// del sueño. El arreglo del mux del GPIO (todo lo que se explica mas abajo) era
// NECESARIO pero NO SUFICIENTE: queda al menos otra causa sin encontrar.
//
// En un aparato cuya razon de ser es recibir un SOS, no se embarca. Lo que sigue
// se queda escrito porque la investigacion vale, no porque este resuelto.
//
// Duerme el CPU hasta 'ms' o hasta que algo lo despierte. Sustituye al delay()
// del tick, que solo cede a la tarea idle y deja el reloj corriendo.
//
// Fuentes de despertar, con su nivel PROPIO cada una:
//   DIO1 de la radio (4)   ALTO  paquete recibido o transmision terminada
//   INT del teclado  (11)  BAJO  el TCA8418 avisa de una pulsacion (open drain)
//   boton G0         (0)   BAJO  SOS
// Mas el temporizador, que es el que marca el tick normal.
//
// POR QUE gpio_wakeup Y NO ext1 (esto es el arreglo del 30-jul-2026):
//
// La primera version usaba esp_sleep_enable_ext1_wakeup() y dejaba el nodo SORDO
// desde el primer sueño. La causa, ya localizada: esp_light_sleep_start() llama a
// ext1_wakeup_prepare(), que hace rtc_gpio_init() sobre cada pin de la mascara, y
// eso saca el pin de la matriz GPIO digital y lo pasa al mux del dominio RTC --
// para siempre, porque al despertar nadie lo devuelve. A partir de ahi, en el
// GPIO 4 se pierden LAS DOS vias de aviso de la radio:
//   1. la ISR que RadioLib registro con setDio1Action(), y
//   2. el rearme por nivel de radioLoop() (`if (digitalRead(PIN_LORA_DIO1))`),
//      que era justo la red de seguridad puesta para el caso de perder un flanco.
// Por eso el nodo no se recuperaba nunca y por eso la red de seguridad no salto:
// las mata la misma causa.
//
// gpio_wakeup_enable() + esp_sleep_enable_gpio_wakeup() despiertan sin tocar el
// mux: el pin sigue siendo digital y la ISR sigue viva. Es lo que hace Meshtastic
// en src/sleep.cpp (enableLoraInterrupt(), GPIO_INTR_HIGH_LEVEL sobre LORA_DIO1),
// que corre sobre el mismo SX1262 con RadioLib. Y de paso admite un nivel por
// pin, asi que ahora el boton tambien despierta: con ext1 no cabia, porque su
// mascara impone un unico nivel para todos los pines y el boton es activo bajo.
//
// millis() va sobre esp_timer, que sigue contando durmiendo: el contador de
// ciclo de trabajo NO se descuadra (se desvia ~1 %, ver GPS_TRAMA_MARGEN_MS).
//
// Se desactiva en compilacion con -DFT_NO_LIGHT_SLEEP.
void sleepBegin();
bool sleepDisponible();

// Devuelve los ms realmente dormidos; 0 si no se durmio (y entonces ya ha hecho
// el delay() equivalente, asi que el bucle no tiene que hacer nada distinto).
uint32_t dormirHasta(uint32_t ms);

// Estadistica del light sleep. Sin esto es un cambio invisible: no habria forma
// de distinguir "duerme y ahorra" de "cree que duerme y vuelve al instante",
// que es el modo de fallo silencioso mas probable.
struct SleepStats {
  uint32_t veces      = 0;   // sueños completados
  uint32_t ms_dormido = 0;   // total dormido, en ms
  uint32_t desp_gpio  = 0;   // despertados por DIO1, teclado o boton
  uint32_t desp_timer = 0;   // despertados por el temporizador del tick
  uint32_t no_usb     = 0;   // no se durmio: hay un ordenador en el USB
  uint32_t no_gps     = 0;   // no se durmio: el GPS estaba soltando una rafaga
  uint32_t no_dio1    = 0;   // no se durmio: DIO1 alto, la radio tiene trabajo
};
const SleepStats& sleepStats();
void sleepContarNoGps();     // la decide el bucle, que es quien conoce el GPS

// true si hay un ordenador escuchando por el USB-Serial-JTAG. Es la condicion
// que evita dormir cuando hay consola: dormido, el USB se cae, se pierde el
// monitor Y el flasheo automatico deja de funcionar (`No serial data received`,
// que obliga al baile de G0 + interruptor). Asi el light sleep solo actua con el
// cable fuera, que es cuando la bateria importa y cuando no hay nada que mirar.
bool usbConectado();

}  // namespace hal
