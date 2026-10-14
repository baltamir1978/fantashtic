#pragma once
// Router de inundacion controlada.
//
// Con 3-4 nodos no hace falta encaminamiento por siguiente salto: los
// broadcasts se inundan de todas formas, tambien en Meshtastic. Aqui solo se
// implementa la inundacion, dejando next_hop siempre a 0.
//
// Todo son funciones puras o dependientes de un estado explicito, sin reloj ni
// aleatoriedad internos: el valor aleatorio y el instante se pasan como
// parametro para poder probarlo en el ordenador de forma determinista.

#include <stdint.h>

#include "mesh/history.h"
#include "proto/packet.h"

namespace mesh {

enum class Accion : uint8_t {
  DESCARTAR = 0,             // invalido, duplicado, o no nos incumbe
  ENTREGAR,                  // unicast dirigido a nosotros
  RETRANSMITIR,              // unicast para otro: solo relevo
  ENTREGAR_Y_RETRANSMITIR,   // difusion: se muestra Y se propaga
  ACK_IMPLICITO,             // alguien retransmitio un paquete nuestro
  REACUSAR,                  // duplicate of a direct message to us that asked
                             // for an ACK: the sender did not hear the first
                             // one and retried, so the ACK goes out again
};

const char* accionNombre(Accion a);

// Decide que hacer con un paquete recien recibido y actualiza el historial.
//
//   mi_nodo          NodeNum propio
//   soy_retransmisor false para un nodo configurado como mudo
//
// El paquete de difusion se ENTREGA Y SE RETRANSMITE: son dos cosas distintas
// y confundirlas rompe la malla entera (si se filtrara por "es para mi", que
// para un broadcast es cierto, no se retransmitiria nunca nada).
Accion decidir(const proto::Cabecera& c, uint32_t mi_nodo,
               Historial& hist, bool soy_retransmisor);

// Prepara la cabecera para retransmitir: decrementa el contador de saltos y
// firma el relevo. La carga util NO se toca: relevar no exige descifrar.
void prepararRelevo(proto::Cabecera* c, uint32_t mi_nodo);

// --- temporizacion -----------------------------------------------------------

// slotTime = max(2.25, NUM_SYM_CAD + 0.5) * (2^SF / BW_kHz) + 7.6
// RadioInterface::computeSlotTimeMsec. A SF11/BW250 salen 28,1 ms.
float slotTimeMs(uint8_t sf, float bw_khz);

// Ventana de contencion en funcion de la relacion senal-ruido, entre CW_MIN y
// CW_MAX para SNR de -20 a +10 dB.
//
// OJO al sentido: PEOR senal -> ventana MAS PEQUENA -> retransmite ANTES.
// Es intencional. Asi los nodos lejanos propagan primero y los cercanos, al
// oir el duplicado, se callan.
uint8_t cwPorSnr(float snr);

// Ventana en funcion de la ocupacion del canal, para paquetes propios.
uint8_t cwPorOcupacion(uint8_t ocupacion_pct);

// Retardo antes de retransmitir algo ajeno. Lleva un desplazamiento fijo de
// 2*CW_MAX ranuras para no pisar a los nodos que actuan de repetidor.
uint32_t retardoRelevoMs(float snr, uint32_t aleatorio, float slot_ms);

// Retardo antes de transmitir algo propio.
uint32_t retardoPropioMs(uint8_t ocupacion_pct, uint32_t aleatorio, float slot_ms);

}  // namespace mesh
