#pragma once
// Transceptor SX1262 (Cap LoRa-1262) mediante RadioLib.
//
// Maquina de estados NO BLOQUEANTE. La ISR de DIO1 solo levanta una bandera;
// todo el trabajo ocurre en radioLoop(), que hay que llamar en cada tick.
//
// Secuencia de transmision, igual que Meshtastic:
//   encolar -> retardo aleatorio (ventana de contencion) -> CAD -> TX -> RX
//
// El CAD se hace con startChannelScan()/getChannelScanResult(), NO con
// scanChannel(): esa version bloquea ~21 ms a SF11 y saca la radio de
// recepcion en silencio.
//
// SEGURIDAD: radioBegin() NO habilita la transmision. Hay que llamar
// explicitamente a radioPermitirTx(true) despues de que el usuario confirme
// que la antena esta atornillada. Transmitir sin antena destruye el chip.

#include <stdint.h>
#include <stddef.h>

namespace radio {

enum class Estado : uint8_t {
  SIN_INICIAR = 0,
  RX,            // escuchando
  ESPERA_TX,     // esperando a que venza el retardo de contencion
  CAD,           // sondeando el canal
  TX,            // transmitiendo
  ERROR,
};

enum class Init : uint8_t {
  OK = 0,
  SIN_SWITCH_ANTENA,   // el PI4IOE no esta verificado: nos negamos a seguir
  FALLO_SPI,           // el SX1262 no responde
  FALLO_CONFIG,
};

struct Paquete {
  uint8_t  datos[255];
  size_t   len;
  int16_t  rssi;
  float    snr;
  uint32_t millis_rx;
};

// Arranca la radio. Requiere que hal::antennaBegin() haya devuelto OK.
// tcxo_ok queda a false si hubo que reintentar sin TCXO.
Init radioBegin(bool* tcxo_ok = nullptr);

Estado estado();
const char* estadoNombre(Estado e);
const char* initMensaje(Init i);

// --- seguro de transmision ---------------------------------------------------
// Mientras sea false, radioEnviar() rechaza todo.
void radioPermitirTx(bool permitir);
bool radioTxPermitido();

// --- envio y recepcion -------------------------------------------------------

// Encola un paquete propio. false si la cola esta llena, la radio no esta
// lista, la transmision no esta permitida o el ciclo de trabajo lo impide.
//
// 'automatico' distingue lo que genera el firmware por su cuenta (posicion,
// telemetria) de lo que pide una persona: lo automatico se corta ya en el
// umbral blando del 8 %, reservando el aire restante para lo que importa.
bool radioEnviar(const uint8_t* datos, size_t len, bool automatico = false);

// Encola una RETRANSMISION de un paquete ajeno. Usa la ventana de contencion
// ponderada por senal: peor SNR, retransmite antes.
bool radioRelevar(const uint8_t* datos, size_t len, float snr);

size_t radioPendientes();

// Llamar en cada tick.
void radioLoop();

// Devuelve true y rellena 'p' si hay un paquete recibido sin consumir.
bool radioRecibir(Paquete* p);

// --- metricas ----------------------------------------------------------------
int16_t  ultimoRssi();
float    ultimoSnr();
uint32_t enviados();
uint32_t recibidos();
uint32_t errCrc();
uint32_t airtimeMsTotal();          // acumulado desde el arranque
uint32_t tiempoEnAireMs(size_t len); // estimacion para un paquete de 'len'
void     reiniciarContadores();

// --- ciclo de trabajo --------------------------------------------------------
// Ventana rodante de 60 minutos. Limite duro del 10 % (subbanda ETSI g3),
// no desactivable desde la interfaz.
float    dutyPct();
uint32_t dutyMinutosParaLiberar();
bool     dutyBloqueado();          // por encima del umbral duro

// Parametros efectivos, para mostrarlos en pantalla.
float freqMhz();
uint8_t sf();
float bwKhz();
int8_t potenciaDbm();

}  // namespace radio
