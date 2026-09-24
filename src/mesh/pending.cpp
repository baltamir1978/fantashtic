#include "mesh/pending.h"

#include <string.h>

namespace mesh {

bool Pendientes::anadir(uint32_t id, const uint8_t* datos, size_t len,
                        uint32_t ahora_ms, uint32_t espera_ms) {
  if (id == 0 || !datos || len == 0 || len > MESH_MAX_LORA_PAYLOAD) return false;
  for (auto& e : tabla_) {
    if (e.usado) continue;
    e.usado    = true;
    e.id       = id;
    e.prox_ms  = ahora_ms + espera_ms;
    e.intentos = 0;
    e.len      = len;
    memcpy(e.datos, datos, len);
    return true;
  }
  return false;
}

bool Pendientes::confirmar(uint32_t id) {
  if (id == 0) return false;
  for (auto& e : tabla_) {
    if (e.usado && e.id == id) { e.usado = false; return true; }
  }
  return false;
}

Pendientes::Tarea Pendientes::revisar(uint32_t ahora_ms, uint32_t espera_ms) {
  Tarea t;
  for (auto& e : tabla_) {
    if (!e.usado) continue;
    // Signed difference: survives the millis() wrap at 49 days.
    if ((int32_t)(ahora_ms - e.prox_ms) < 0) continue;

    t.id = e.id;
    if (e.intentos >= DM_RETRIES) {
      e.usado = false;
      t.tipo  = Tipo::AGOTADO;
      return t;
    }
    e.intentos++;
    e.prox_ms = ahora_ms + espera_ms;
    t.tipo  = Tipo::REINTENTAR;
    t.datos = e.datos;
    t.len   = e.len;
    return t;
  }
  return t;
}

size_t Pendientes::ocupacion() const {
  size_t n = 0;
  for (const auto& e : tabla_) n += e.usado ? 1 : 0;
  return n;
}

void Pendientes::limpiar() {
  for (auto& e : tabla_) e.usado = false;
}

// --- Acuses -------------------------------------------------------------------

void Acuses::anotar(uint32_t from, uint32_t id, uint32_t ahora_ms) {
  for (auto& e : tabla_) {
    if (e.id == id && e.from == from) { e.ms = ahora_ms; return; }
  }
  tabla_[sig_] = Entrada{from, id, ahora_ms};
  sig_ = (sig_ + 1) % DM_ACKS_RECIENTES;
}

bool Acuses::reacusar(uint32_t from, uint32_t id, uint32_t ahora_ms, uint32_t min_ms) {
  for (auto& e : tabla_) {
    if (e.id != id || e.from != from || id == 0) continue;
    if (ahora_ms - e.ms < min_ms) return false;
    e.ms = ahora_ms;
    return true;
  }
  // Never acknowledged: most likely it did not decrypt (another group key), and
  // acknowledging it now would report as delivered something nobody could read.
  // An entry evicted by DM_ACKS_RECIENTES newer ACKs within one retry interval
  // would land here too; with 3-4 nodes that does not happen.
  return false;
}

}  // namespace mesh
