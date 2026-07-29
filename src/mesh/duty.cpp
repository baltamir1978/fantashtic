#include "mesh/duty.h"

#include "config.h"

namespace mesh {
namespace {

constexpr uint32_t MS_POR_MIN  = 60000u;
constexpr uint32_t MS_POR_HORA = 3600000u;

inline uint32_t minutoAbsoluto(uint32_t ahora_ms) { return ahora_ms / MS_POR_MIN; }

}  // namespace

// Descarta los cubos que ya han salido de la ventana de 60 minutos. Cada cubo
// recuerda a que minuto absoluto pertenece, asi que un salto de tiempo grande
// (o el desbordamiento de millis a los 49 dias) se resuelve solo.
void CicloTrabajo::purgar(uint32_t ahora_ms) {
  const uint32_t min_ahora = minutoAbsoluto(ahora_ms);
  if (!inicializado_) {
    for (size_t i = 0; i < DUTY_CUBOS; i++) { cubo_ms_[i] = 0; cubo_min_[i] = 0; }
    inicializado_ = true;
  }
  for (size_t i = 0; i < DUTY_CUBOS; i++) {
    if (cubo_ms_[i] == 0) continue;
    // Fuera de ventana, o de un futuro imposible (millis dio la vuelta).
    if (min_ahora < cubo_min_[i] || (min_ahora - cubo_min_[i]) >= DUTY_CUBOS) {
      cubo_ms_[i]  = 0;
      cubo_min_[i] = 0;
    }
  }
}

void CicloTrabajo::registrar(uint32_t ahora_ms, uint32_t airtime_ms) {
  purgar(ahora_ms);
  const uint32_t min_ahora = minutoAbsoluto(ahora_ms);
  const size_t   idx       = min_ahora % DUTY_CUBOS;

  if (cubo_min_[idx] != min_ahora) {   // cubo reciclado: empieza de cero
    cubo_ms_[idx]  = 0;
    cubo_min_[idx] = min_ahora;
  }
  cubo_ms_[idx] += airtime_ms;
}

uint32_t CicloTrabajo::airtimeVentanaMs(uint32_t ahora_ms) {
  purgar(ahora_ms);
  uint32_t total = 0;
  for (size_t i = 0; i < DUTY_CUBOS; i++) total += cubo_ms_[i];
  return total;
}

float CicloTrabajo::utilizacionPct(uint32_t ahora_ms) {
  return (float)airtimeVentanaMs(ahora_ms) * 100.0f / (float)MS_POR_HORA;
}

bool CicloTrabajo::permitido(uint32_t ahora_ms, bool automatico) {
  const float u = utilizacionPct(ahora_ms);
  if (u >= DUTY_HARD_PCT) return false;                 // limite legal
  if (automatico && u >= DUTY_SOFT_PCT) return false;   // reserva para lo urgente
  return true;
}

uint32_t CicloTrabajo::minutosParaLiberar(uint32_t ahora_ms) {
  purgar(ahora_ms);

  uint32_t total = 0;
  for (size_t i = 0; i < DUTY_CUBOS; i++) total += cubo_ms_[i];

  const uint32_t presupuesto = (uint32_t)(MS_POR_HORA * DUTY_HARD_PCT / 100.0f);
  if (total < presupuesto) return 0;

  // Vamos caducando cubos del mas antiguo al mas nuevo hasta bajar del limite,
  // y devolvemos cuanto falta para que ese cubo salga de la ventana.
  const uint32_t min_ahora = minutoAbsoluto(ahora_ms);
  uint32_t restante = total;

  for (uint32_t antiguedad = DUTY_CUBOS; antiguedad > 0; antiguedad--) {
    const uint32_t min_cubo = (min_ahora >= antiguedad - 1)
                                  ? min_ahora - (antiguedad - 1) : 0;
    const size_t idx = min_cubo % DUTY_CUBOS;
    if (cubo_ms_[idx] == 0 || cubo_min_[idx] != min_cubo) continue;

    restante -= cubo_ms_[idx];
    if (restante < presupuesto) {
      // Ese cubo sale de la ventana cuando cumple 60 minutos.
      const uint32_t edad = min_ahora - min_cubo;
      return (edad >= DUTY_CUBOS) ? 0 : (DUTY_CUBOS - edad);
    }
  }
  return DUTY_CUBOS;
}

void CicloTrabajo::limpiar() {
  for (size_t i = 0; i < DUTY_CUBOS; i++) { cubo_ms_[i] = 0; cubo_min_[i] = 0; }
  inicializado_ = true;
}

}  // namespace mesh
