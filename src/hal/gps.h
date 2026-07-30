#pragma once
// GPS ATGM336H-6N (AT6668) del Cap LoRa-1262, por UART1 a 115200 8N1.
//
// Es el mayor consumidor de energia del aparato, asi que se puede apagar. La
// antena es ceramica e integrada: en interiores tarda o no engancha.

#include <stdint.h>

namespace hal {

struct Fix {
  bool     valido    = false;
  int32_t  lat_e7    = 0;
  int32_t  lon_e7    = 0;
  int32_t  altitud_m = 0;
  uint32_t epoch     = 0;
  // OJO A LA DIFERENCIA, que no es cosmetica:
  //   satelites        los que entran en la SOLUCION, campo 7 de la trama GGA.
  //   satelites_vista  los que se VEN, campo 3 de las tramas GSV. Puede ser
  //                    bastante mayor: se ven muchos y se usan los que dan
  //                    buena geometria. 0 = el modulo no manda GSV.
  // Llamar "visibles" a los primeros seria dar un numero equivocado con una
  // etiqueta correcta, que es peor que no darlo.
  uint8_t  satelites = 0;
  uint8_t  satelites_vista = 0;
  float    hdop      = 0.0f;
  float    rumbo     = 0.0f;   // solo fiable en movimiento
  float    velocidad_kmh = 0.0f;
  uint32_t edad_ms   = 0;      // desde la ultima trama valida
};

void gpsBegin();
void gpsEncender(bool on);
bool gpsEncendido();

// Llamar en cada tick: consume los caracteres que haya llegado por el UART.
void gpsLoop();

const Fix& gpsFix();

// true si hay fix valido y reciente (menos de 10 s).
bool gpsValido();

// Segundos desde el arranque sin conseguir fix, para el aviso de la barra.
uint32_t gpsSegundosSinFix();

// Caracteres NMEA recibidos: si es 0, el UART esta mal conectado, que es
// distinto de "hay UART pero no engancha satelites".
uint32_t gpsBytesRecibidos();

// Ritmo de salida de tramas, en milisegundos entre fijaciones (comando CASIC
// $PCAS02 del AT6668). 1000 = 1 Hz, el defecto del modulo; 5000 = 0,2 Hz.
//
// LO QUE AHORRA Y LO QUE NO: baja el caudal NMEA de ~820 B/s a ~40 B/s, o sea
// que ahorra UART y CPU del ESP32. NO apaga el receptor: el AT6668 sigue
// siguiendo satelites y consumiendo. Para apagarlo de verdad haria falta un
// comando de standby que NO esta documentado en las fuentes publicas del chip
// (ver la deuda de autonomia en CLAUDE.md); el Cap tampoco expone un pin de
// alimentacion del GPS.
//
// La trama se construye con el checksum calculado, no escrito a mano: una
// constante mal copiada haria que el modulo ignore el comando en silencio.
void gpsRitmo(uint16_t ms_entre_fijaciones);

// Milisegundos que se puede dormir sin comerse la siguiente rafaga NMEA.
//   UINT32_MAX  no hay nada que respetar (GPS apagado, o callado del todo)
//   0           esta hablando ahora, o esta a punto: NO dormir
//   n           se puede dormir hasta n ms
//
// En light sleep el APB esta parado y el UART no recoge nada, asi que lo que
// llegue durmiendo se pierde y la trama sale con el checksum roto. El modulo es
// un reloj estable, asi que se mide donde empezo la ultima rafaga y se despierta
// GPS_TRAMA_MARGEN_MS antes de la siguiente. Ver GPS_TRAMA_* en config.h.
uint32_t gpsMsHastaProximaTrama();

// Intervalo MEDIDO entre el comienzo de dos rafagas. No es lo mismo que lo que
// se pidio con gpsRitmo(): el modulo no hace caso del todo. Se saca por consola
// en informeGps() porque es el numero que dice si el ahorro del ritmo es real.
uint32_t gpsIntervaloTramaMs();

}  // namespace hal
