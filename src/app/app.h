#pragma once
// Logica de la aplicacion: qué se envía, cuándo, y qué se hace con lo que
// llega. La interfaz no habla nunca con la radio directamente; pasa por aquí.

#include <stdint.h>

#include "mesh/nodedb.h"
#include "store/store.h"

namespace app {

enum class Sos : uint8_t {
  INACTIVO = 0,
  ARMANDO,      // boton mantenido, cuenta atras en marcha
  ENVIANDO,     // rafaga inicial
  REPITIENDO,   // cada 5 minutos
  RECIBIDO,     // nos ha llegado un SOS ajeno
};

void begin(uint32_t node_num);
void loop();

// --- mensajeria --------------------------------------------------------------
bool enviarTexto(const char* txt);

// --- SOS ---------------------------------------------------------------------
void      sosArmar();          // arranca la cuenta atras
void      sosConfirmar();      // lanza ya, sin esperar
void      sosCancelar();       // aborta, o silencia uno recibido
Sos       sosEstado();
uint8_t   sosSegundosRestantes();
uint32_t  sosNodoOrigen();     // quien lo envio, si es uno recibido
bool      sosResponderVoy();   // envia "Voy hacia ti" al origen

// --- estado ------------------------------------------------------------------
mesh::BaseNodos& nodos();
uint32_t  nodoPropio();
uint8_t   canalHash();
uint32_t  ultimoMensajeMs();

// Nodo seleccionado en la pantalla de brujula.
void      seleccionarNodo(uint32_t num);
uint32_t  nodoSeleccionado();

// Recalcula clave y hash de canal tras cambiar la configuracion.
void recargarCanal();

}  // namespace app
