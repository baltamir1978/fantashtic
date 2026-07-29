#include "mesh/nodedb.h"

#include <stdio.h>
#include <string.h>

#include "util/utf8.h"

namespace mesh {
namespace {

// Copia acotada que ademas respeta los limites de caracter UTF-8: cortar un
// nombre con acentos por la mitad de un caracter deja basura en pantalla.
void copiarTruncado(char* destino, size_t cap, const char* origen) {
  if (!origen || cap == 0) return;
  const size_t n = util::longitudSegura(origen, cap - 1);
  memcpy(destino, origen, n);
  destino[n] = '\0';
}

}  // namespace

Nodo* BaseNodos::buscar(uint32_t num) {
  for (size_t i = 0; i < MAX_NODES; i++)
    if (tabla_[i].usado && tabla_[i].num == num) return &tabla_[i];
  return nullptr;
}

const Nodo* BaseNodos::buscar(uint32_t num) const {
  return const_cast<BaseNodos*>(this)->buscar(num);
}

const Nodo* BaseNodos::porIndice(size_t i) const {
  if (i >= MAX_NODES || !tabla_[i].usado) return nullptr;
  return &tabla_[i];
}

Nodo* BaseNodos::visto(uint32_t num, uint32_t ahora_ms, int16_t rssi,
                       int8_t snr, uint8_t saltos) {
  if (num == 0) return nullptr;

  Nodo* n = buscar(num);
  if (!n) {
    // Hueco libre, si lo hay.
    for (size_t i = 0; i < MAX_NODES && !n; i++)
      if (!tabla_[i].usado) { n = &tabla_[i]; *n = Nodo{}; usados_++; }

    // Si no, desalojamos al que lleve mas tiempo sin dar senales.
    if (!n) {
      size_t peor = 0;
      for (size_t i = 1; i < MAX_NODES; i++)
        if (tabla_[i].visto_ms < tabla_[peor].visto_ms) peor = i;
      n = &tabla_[peor];
      *n = Nodo{};
    }
    n->num     = num;
    n->usado   = true;
    n->bateria = 255;            // desconocida hasta que llegue telemetria
  }

  n->visto_ms = ahora_ms;
  n->rssi     = rssi;
  n->snr      = snr;
  n->saltos   = saltos;
  return n;
}

void BaseNodos::ponerNombres(uint32_t num, const char* largo, const char* corto) {
  Nodo* n = buscar(num);
  if (!n) return;
  copiarTruncado(n->largo, sizeof(n->largo), largo);
  copiarTruncado(n->corto, sizeof(n->corto), corto);
}

void BaseNodos::ponerPosicion(uint32_t num, int32_t lat_e7, int32_t lon_e7,
                              int32_t alt_m, uint32_t ahora_ms) {
  Nodo* n = buscar(num);
  if (!n) return;
  // Una posicion en el (0,0) es casi siempre un fix invalido, no la isla nula.
  if (lat_e7 == 0 && lon_e7 == 0) return;
  n->lat_e7    = lat_e7;
  n->lon_e7    = lon_e7;
  n->altitud_m = alt_m;
  n->pos_ms    = ahora_ms;
  n->tiene_pos = true;
}

void BaseNodos::ponerBateria(uint32_t num, uint8_t pct) {
  Nodo* n = buscar(num);
  if (n) n->bateria = pct > 100 ? 100 : pct;
}

size_t BaseNodos::activos(uint32_t ahora_ms, uint32_t ventana_ms) const {
  size_t n = 0;
  for (size_t i = 0; i < MAX_NODES; i++) {
    if (!tabla_[i].usado) continue;
    if (ahora_ms >= tabla_[i].visto_ms &&
        (ahora_ms - tabla_[i].visto_ms) <= ventana_ms) n++;
  }
  return n;
}

void BaseNodos::nombreMostrar(const Nodo& n, char* out, size_t cap) const {
  if (cap == 0) return;
  if (n.corto[0]) { snprintf(out, cap, "%s", n.corto); return; }
  snprintf(out, cap, "%04X", (unsigned)(n.num & 0xFFFF));
}

void BaseNodos::limpiar() {
  for (size_t i = 0; i < MAX_NODES; i++) tabla_[i] = Nodo{};
  usados_ = 0;
}

}  // namespace mesh
