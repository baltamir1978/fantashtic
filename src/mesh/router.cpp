#include "mesh/router.h"

#include "config.h"
#include "proto/crypto.h"

namespace mesh {
namespace {

int32_t mapear(int32_t x, int32_t x0, int32_t x1, int32_t y0, int32_t y1) {
  if (x <= x0) return y0;
  if (x >= x1) return y1;
  return y0 + (x - x0) * (y1 - y0) / (x1 - x0);
}

}  // namespace

const char* accionNombre(Accion a) {
  switch (a) {
    case Accion::ENTREGAR:                return "entregar";
    case Accion::RETRANSMITIR:            return "retransmitir";
    case Accion::ENTREGAR_Y_RETRANSMITIR: return "entregar y retransmitir";
    case Accion::ACK_IMPLICITO:           return "ack implicito";
    case Accion::REACUSAR:                return "reacusar";
    default:                              return "descartar";
  }
}

Accion decidir(const proto::Cabecera& c, uint32_t mi_nodo,
               Historial& hist, bool soy_retransmisor) {
  // Paquetes sin identificador ni emisor: ni se registran ni se propagan.
  if (c.id == 0 || c.from == 0) return Accion::DESCARTAR;

  // Es nuestro y vuelve: alguien lo ha retransmitido. Eso es la confirmacion
  // implicita de entrega, y no requiere que quien releva tenga nuestra clave.
  if (c.from == mi_nodo) {
    if (hist.contiene(c.from, c.id)) {
      if (c.relay_node) hist.anotarRelayer(c.from, c.id, c.relay_node);
      return Accion::ACK_IMPLICITO;
    }
    return Accion::DESCARTAR;
  }

  // Duplicado: anotamos quien lo relevo, por si sirve, y lo tiramos.
  if (hist.vistoOAnadir(c.from, c.id)) {
    if (c.relay_node) hist.anotarRelayer(c.from, c.id, c.relay_node);
    // A retry of a direct message we already delivered: our ACK got lost on the
    // way back. It is NOT shown again, but it has to be acknowledged again, or
    // the sender runs out of retries and marks as failed a message that arrived.
    // Same as Meshtastic's ReliableRouter on a duplicate with want_ack.
    if (c.to == mi_nodo && c.want_ack) return Accion::REACUSAR;

    // A retry of a direct message for someone else, heard straight from its
    // sender: hop_limit is still hop_start, so no relay has touched this copy.
    // It is relayed again. Without this a retry could never cross a relay --
    // the relay already saw that (from, id) and would drop it -- and when the
    // first try is lost past the relay, or the ACK is lost on the way back,
    // every retry dies at the relay too. Copies that already went through a
    // relay have a lower hop_limit and are still dropped, so this cannot loop:
    // it happens at most once per retry, and there are DM_RETRIES of those.
    const uint8_t mi_ultimo = proto::ultimoByte(mi_nodo);
    if (c.to != MESH_BROADCAST_ADDR && c.want_ack && soy_retransmisor &&
        c.hop_limit > 0 && c.hop_limit == c.hop_start &&
        (c.next_hop == 0 || c.next_hop == mi_ultimo))
      return Accion::RETRANSMITIR;
    return Accion::DESCARTAR;
  }
  if (c.relay_node) hist.anotarRelayer(c.from, c.id, c.relay_node);

  const bool difusion      = (c.to == MESH_BROADCAST_ADDR);
  const bool unicast_parami = (c.to == mi_nodo);

  // Unicast dirigido a nosotros: se entrega y no se propaga.
  if (unicast_parami) return Accion::ENTREGAR;

  const uint8_t mi_ultimo = proto::ultimoByte(mi_nodo);
  const bool relevable = soy_retransmisor && c.hop_limit > 0 &&
                         (c.next_hop == 0 || c.next_hop == mi_ultimo);

  if (difusion)
    return relevable ? Accion::ENTREGAR_Y_RETRANSMITIR : Accion::ENTREGAR;

  // Unicast para otro: solo relevo, nunca se muestra.
  return relevable ? Accion::RETRANSMITIR : Accion::DESCARTAR;
}

void prepararRelevo(proto::Cabecera* c, uint32_t mi_nodo) {
  if (c->hop_limit > 0) c->hop_limit--;
  c->relay_node = proto::ultimoByte(mi_nodo);
}

// --- temporizacion -----------------------------------------------------------

float slotTimeMs(uint8_t sf, float bw_khz) {
  const float sym = (float)(1UL << sf) / bw_khz;
  const float k   = (NUM_SYM_CAD + 0.5f) > 2.25f ? (NUM_SYM_CAD + 0.5f) : 2.25f;
  return k * sym + 7.6f;
}

uint8_t cwPorSnr(float snr) {
  return (uint8_t)mapear((int32_t)snr, -20, 10, CW_MIN, CW_MAX);
}

uint8_t cwPorOcupacion(uint8_t ocupacion_pct) {
  return (uint8_t)mapear(ocupacion_pct, 0, 100, CW_MIN, CW_MAX);
}

uint32_t retardoRelevoMs(float snr, uint32_t aleatorio, float slot_ms) {
  const uint8_t cw = cwPorSnr(snr);
  const uint32_t ranuras = aleatorio % (1UL << cw);
  // El desplazamiento de 2*CW_MAX ranuras (a SF11/BW250, unos 449 ms) deja
  // pasar antes a los nodos con rol de repetidor.
  return (uint32_t)((2u * CW_MAX + ranuras) * slot_ms);
}

uint32_t retardoPropioMs(uint8_t ocupacion_pct, uint32_t aleatorio, float slot_ms) {
  const uint8_t cw = cwPorOcupacion(ocupacion_pct);
  return (uint32_t)((aleatorio % (1UL << cw)) * slot_ms);
}

}  // namespace mesh
