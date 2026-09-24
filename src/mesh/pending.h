#pragma once
// Direct messages waiting for their ACK.
//
// A broadcast is confirmed by the implicit ACK: someone relays it and we hear
// it. A direct message to a neighbour is NOT relayed by anyone -- the router
// delivers a unicast for us without propagating it -- so that trick never
// fires. The recipient answers with a ROUTING_APP ACK instead, and if it does
// not arrive in time the SAME packet (same id) is sent again. Same scheme as
// Meshtastic's ReliableRouter.
//
// The table keeps a copy of the encrypted packet, so a retry does not re-encrypt
// and keeps the id: that is what lets the recipient spot the duplicate and just
// acknowledge it again (mesh::Accion::REACUSAR).
//
// No Arduino, no clock of its own: time comes in as a parameter, so it is tested
// on the host with made-up clocks.

#include <stddef.h>
#include <stdint.h>

#include "config.h"

namespace mesh {

class Pendientes {
 public:
  enum class Tipo : uint8_t {
    NADA = 0,
    REINTENTAR,   // no ACK yet and retries left: send 'datos' again
    AGOTADO,      // no ACK after the last retry: the message did not arrive
  };

  struct Tarea {
    Tipo           tipo  = Tipo::NADA;
    uint32_t       id    = 0;
    const uint8_t* datos = nullptr;   // valid until the next call; null if AGOTADO
    size_t         len   = 0;
  };

  // Registers a packet that was just sent. First check after 'espera_ms'.
  // Returns false if the table is full or the packet does not fit: the message
  // still went out, it is just not retried.
  bool anadir(uint32_t id, const uint8_t* datos, size_t len,
              uint32_t ahora_ms, uint32_t espera_ms);

  // The ACK arrived. Returns true if the id was pending.
  bool confirmar(uint32_t id);

  // Next thing to do, at most one per call. A retry is rescheduled
  // 'espera_ms' later; an exhausted entry is removed.
  Tarea revisar(uint32_t ahora_ms, uint32_t espera_ms);

  size_t ocupacion() const;
  void   limpiar();

 private:
  struct Entrada {
    bool     usado    = false;
    uint32_t id       = 0;
    uint32_t prox_ms  = 0;
    uint8_t  intentos = 0;       // retries already made, not counting the first send
    size_t   len      = 0;
    uint8_t  datos[MESH_MAX_LORA_PAYLOAD] = {};
  };
  Entrada tabla_[DM_PENDING_MAX];
};

// The ACKs we sent lately, to tell a sender's RETRY (acknowledge again) from a
// relay's COPY of the same try (already acknowledged). See DM_REACK_MIN_MS.
class Acuses {
 public:
  // Call when an ACK goes out for (from, id).
  void anotar(uint32_t from, uint32_t id, uint32_t ahora_ms);

  // For a duplicate: true if it deserves a new ACK -- it WAS acknowledged, and
  // that last ACK is at least 'min_ms' old. Never acknowledged means it did not
  // decrypt, and that must not be confirmed. When true, it is noted as sent now.
  bool reacusar(uint32_t from, uint32_t id, uint32_t ahora_ms, uint32_t min_ms);

 private:
  struct Entrada { uint32_t from = 0, id = 0, ms = 0; };
  Entrada tabla_[DM_ACKS_RECIENTES];
  size_t  sig_ = 0;     // next slot to overwrite: oldest first
};

}  // namespace mesh
