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

  // La tendencia se calcula ANTES de meter la muestra nueva en la media: lo que
  // interesa es si este paquete llega mejor o peor que como venia llegando. Con
  // histeresis, porque el SNR de un enlace quieto ya baila un par de dB solo y
  // sin ella la flecha cambiaria de sentido en cada latido.
  if (n->hay_media) {
    if      (snr > n->snr_medio + LINK_SNR_HIST_DB) n->tendencia = 1;
    else if (snr < n->snr_medio - LINK_SNR_HIST_DB) n->tendencia = -1;
    else                                            n->tendencia = 0;
    // Media exponencial de un cuarto: tres partes de lo que habia y una de lo
    // que llega. Sin divisiones en coma flotante y sin guardar un historico,
    // que aqui no cabe (16 nodos, sin PSRAM).
    n->snr_medio = (int8_t)(((int)n->snr_medio * 3 + snr) / 4);
  } else {
    n->snr_medio = snr;
    n->hay_media = true;
    n->tendencia = 0;
  }

  n->visto_ms = ahora_ms;
  n->rssi     = rssi;
  n->snr      = snr;
  n->saltos   = saltos;
  // 'directo_ms' solo lo mueve un paquete oido de primera mano. Es lo que
  // permite distinguir "te oigo" de "me llega tu voz por un tercero", que en
  // ciudad es el aviso de que el enlace se esta yendo.
  if (saltos == 0) n->directo_ms = ahora_ms;
  return n;
}

Enlace estadoEnlace(const Nodo& n, uint32_t ahora_ms) {
  // Un nodo recien creado, o un reloj que ha ido hacia atras, no se declara
  // perdido: no hay dato, que no es lo mismo que no hay enlace.
  if (!n.usado || ahora_ms < n.visto_ms) return Enlace::DIRECTO;

  const uint32_t callado = ahora_ms - n.visto_ms;
  if (callado >= LINK_LOST_MS) return Enlace::PERDIDO;
  if (callado >= LINK_WEAK_MS) return Enlace::DEBIL;

  // Sigue llegando. La pregunta es por donde.
  if (n.directo_ms == 0 || ahora_ms < n.directo_ms) return Enlace::RELEVO;
  return (ahora_ms - n.directo_ms) >= LINK_LOST_MS ? Enlace::RELEVO
                                                   : Enlace::DIRECTO;
}

size_t BaseNodos::conectados(uint32_t ahora_ms) const {
  size_t n = 0;
  for (size_t i = 0; i < MAX_NODES; i++)
    if (tabla_[i].usado && estadoEnlace(tabla_[i], ahora_ms) != Enlace::PERDIDO) n++;
  return n;
}

Enlace BaseNodos::peorEnlace(uint32_t ahora_ms, uint32_t olvidar_ms) const {
  Enlace peor = Enlace::DIRECTO;
  for (size_t i = 0; i < MAX_NODES; i++) {
    if (!tabla_[i].usado) continue;
    if (ahora_ms - tabla_[i].visto_ms >= olvidar_ms) continue;
    const Enlace e = estadoEnlace(tabla_[i], ahora_ms);
    if ((uint8_t)e > (uint8_t)peor) peor = e;
  }
  return peor;
}

const Nodo* BaseNodos::perdidoParaAvisar(uint32_t ahora_ms, uint32_t olvidar_ms,
                                         size_t* cuantos) const {
  const Nodo* primero = nullptr;
  size_t n = 0;
  for (size_t i = 0; i < MAX_NODES; i++) {
    const Nodo& x = tabla_[i];
    if (!x.usado) continue;
    if (estadoEnlace(x, ahora_ms) != Enlace::PERDIDO) continue;
    if (ahora_ms - x.visto_ms >= olvidar_ms) continue;   // apagado, no perdido
    if (!primero) primero = &x;
    n++;
  }
  if (cuantos) *cuantos = n;
  return primero;
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
