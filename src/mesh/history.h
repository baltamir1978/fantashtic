#pragma once
// Tabla de duplicados. Es lo que impide que un paquete rebote indefinidamente
// entre nodos: sin ella, tres aparatos en alcance mutuo saturan el canal en
// segundos.
//
// Clave: (from, id). Array fijo de 128 entradas con desalojo del mas antiguo.
// NO hay expiracion por tiempo, igual que en Meshtastic (PacketHistory.cpp):
// con 128 huecos y cuatro nodos, una entrada sobrevive de sobra el tiempo que
// tarda un paquete en propagarse.
//
// Sin dependencias de Arduino: compila y se prueba en el ordenador.

#include <stddef.h>
#include <stdint.h>

namespace mesh {

constexpr size_t HIST_ENTRADAS = 128;
constexpr size_t HIST_RELAYERS = 3;    // NUM_RELAYERS en Meshtastic

class Historial {
 public:
  // Registra el paquete. Devuelve true si YA estaba (es decir, es duplicado).
  // Los paquetes con id 0 no se registran nunca y siempre devuelven false.
  bool vistoOAnadir(uint32_t from, uint32_t id);

  // Consulta sin registrar.
  bool contiene(uint32_t from, uint32_t id) const;

  // Anota que 'relayer' (ultimo byte de su NodeNum) retransmitio el paquete.
  // Sirve para detectar el ACK implicito y para aprender rutas.
  void anotarRelayer(uint32_t from, uint32_t id, uint8_t relayer);
  bool relayadoPor(uint32_t from, uint32_t id, uint8_t relayer) const;

  size_t ocupacion() const { return usadas_; }
  void limpiar();

 private:
  struct Entrada {
    uint32_t from = 0;
    uint32_t id   = 0;
    uint8_t  relayers[HIST_RELAYERS] = {};
    uint8_t  n_relayers = 0;
    bool     usada = false;
  };

  const Entrada* buscar(uint32_t from, uint32_t id) const;
  Entrada*       buscar(uint32_t from, uint32_t id);

  Entrada tabla_[HIST_ENTRADAS];
  size_t  siguiente_ = 0;   // indice circular: desaloja el mas antiguo
  size_t  usadas_    = 0;
};

}  // namespace mesh
