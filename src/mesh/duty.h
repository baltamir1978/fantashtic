#pragma once
// Contador de ciclo de trabajo. Obligatorio: la subbanda ETSI g3
// (869,4-869,65 MHz) permite 500 mW p.r.e. pero solo un 10 % de ocupacion por
// hora, y ese limite no se puede desactivar desde la interfaz de usuario.
//
// Ventana rodante de 60 minutos en cubos de un minuto. El reloj se pasa como
// parametro en vez de leer millis(): asi el comportamiento es determinista y
// se puede probar en el ordenador inyectando tiempo sintetico.
//
// Dos umbrales:
//   duro  (10 %)  bloquea CUALQUIER transmision
//   blando (8 %)  bloquea solo los envios automaticos (posicion, telemetria,
//                 nodeinfo). Los mensajes escritos por una persona y el SOS
//                 pasan igualmente.

#include <stddef.h>
#include <stdint.h>

namespace mesh {

constexpr size_t DUTY_CUBOS = 60;

class CicloTrabajo {
 public:
  // Anota el tiempo en aire de una transmision.
  void registrar(uint32_t ahora_ms, uint32_t airtime_ms);

  // Porcentaje de ocupacion en los ultimos 60 minutos.
  float utilizacionPct(uint32_t ahora_ms);

  // ¿Se permite transmitir? 'automatico' distingue los envios que genera el
  // firmware por su cuenta de los que pide una persona.
  bool permitido(uint32_t ahora_ms, bool automatico);

  // Minutos que faltan para volver a estar por debajo del umbral duro.
  // 0 si ya se puede transmitir.
  uint32_t minutosParaLiberar(uint32_t ahora_ms);

  // Milisegundos de aire acumulados en la ventana.
  uint32_t airtimeVentanaMs(uint32_t ahora_ms);

  void limpiar();

 private:
  void purgar(uint32_t ahora_ms);

  uint32_t cubo_ms_[DUTY_CUBOS] = {};   // airtime acumulado en cada cubo
  uint32_t cubo_min_[DUTY_CUBOS] = {};  // a que minuto absoluto pertenece
  bool     inicializado_ = false;
};

}  // namespace mesh
