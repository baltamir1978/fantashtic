#include "history.h"

namespace mesh {

const Historial::Entrada* Historial::buscar(uint32_t from, uint32_t id) const {
  for (size_t i = 0; i < HIST_ENTRADAS; i++)
    if (tabla_[i].usada && tabla_[i].from == from && tabla_[i].id == id)
      return &tabla_[i];
  return nullptr;
}

Historial::Entrada* Historial::buscar(uint32_t from, uint32_t id) {
  return const_cast<Entrada*>(
      static_cast<const Historial*>(this)->buscar(from, id));
}

bool Historial::vistoOAnadir(uint32_t from, uint32_t id) {
  if (id == 0) return false;          // los paquetes sin id no se registran
  if (buscar(from, id)) return true;

  Entrada& e = tabla_[siguiente_];
  if (!e.usada) usadas_++;
  e = Entrada{};
  e.from  = from;
  e.id    = id;
  e.usada = true;

  siguiente_ = (siguiente_ + 1) % HIST_ENTRADAS;
  return false;
}

bool Historial::contiene(uint32_t from, uint32_t id) const {
  return id != 0 && buscar(from, id) != nullptr;
}

void Historial::anotarRelayer(uint32_t from, uint32_t id, uint8_t relayer) {
  if (relayer == 0) return;
  Entrada* e = buscar(from, id);
  if (!e) return;
  for (uint8_t i = 0; i < e->n_relayers; i++)
    if (e->relayers[i] == relayer) return;         // ya anotado
  if (e->n_relayers < HIST_RELAYERS)
    e->relayers[e->n_relayers++] = relayer;
}

bool Historial::relayadoPor(uint32_t from, uint32_t id, uint8_t relayer) const {
  const Entrada* e = buscar(from, id);
  if (!e) return false;
  for (uint8_t i = 0; i < e->n_relayers; i++)
    if (e->relayers[i] == relayer) return true;
  return false;
}

void Historial::limpiar() {
  for (size_t i = 0; i < HIST_ENTRADAS; i++) tabla_[i] = Entrada{};
  siguiente_ = 0;
  usadas_    = 0;
}

}  // namespace mesh
