#include "radio.h"

#include <Arduino.h>
#include <RadioLib.h>
#include <string.h>

#include "config.h"
#include "hal/antenna.h"
#include "hal/storage.h"
#include "mesh/duty.h"
#include "mesh/router.h"

namespace radio {
namespace {

// --- objetos estaticos: nada de new despues de setup() -----------------------
Module  g_mod(PIN_LORA_CS, PIN_LORA_DIO1, PIN_LORA_RST, PIN_LORA_BUSY,
              hal::spiBus(), SPISettings(SPI_FREQ_LORA, MSBFIRST, SPI_MODE0));
SX1262  g_radio(&g_mod);

volatile bool g_irq = false;   // la pone la ISR, la consume radioLoop()

Estado   g_estado      = Estado::SIN_INICIAR;
bool     g_tx_ok       = false;   // seguro de transmision
uint32_t g_deadline    = 0;       // vence el retardo de contencion, o el watchdog de TX
uint8_t  g_cad_intentos = 0;

// Cola de transmision, estatica. Cada entrada lleva su propio retardo porque
// no es lo mismo un paquete propio que un relevo: el relevo espera mas y su
// ventana depende de la calidad con que se oyo el original.
struct Salida { uint8_t datos[255]; size_t len; uint32_t retardo_ms; };
Salida g_cola[MAX_TX_QUEUE];
size_t g_cola_n = 0;

mesh::CicloTrabajo g_duty;

// Ultimo paquete recibido sin consumir.
Paquete g_rx;
bool    g_rx_listo = false;

int16_t  g_rssi = 0;
float    g_snr  = 0.0f;
uint32_t g_n_tx = 0, g_n_rx = 0, g_n_crc = 0;
uint32_t g_airtime_ms = 0;

// La ISR debe estar en IRAM: si vive en flash, peta cuando la cache esta
// deshabilitada. Y solo puede levantar una bandera, nunca tocar el SPI.
void IRAM_ATTR onDio1() { g_irq = true; }

float slotTimeMs() { return mesh::slotTimeMs(PRESET_LONG_FAST_SF, PRESET_LONG_FAST_BW); }

// Ocupacion del canal, en tanto por ciento del presupuesto de ciclo de
// trabajo. Es la senal que ensancha la ventana de contencion cuando el aire
// esta cargado.
uint8_t ocupacionPct() {
  const float u = g_duty.utilizacionPct(millis());
  const float p = u * 100.0f / DUTY_HARD_PCT;
  return (uint8_t)(p > 100.0f ? 100.0f : p);
}

void aRecepcion() {
  g_radio.startReceive();
  g_estado = Estado::RX;
  g_cad_intentos = 0;
}

void programarTx(uint32_t extra_ms = 0) {
  const uint32_t base = g_cola_n ? g_cola[0].retardo_ms
                                 : mesh::retardoPropioMs(ocupacionPct(), (uint32_t)random(0, 0x7FFFFFFF), slotTimeMs());
  g_deadline = millis() + base + extra_ms;
  g_estado   = Estado::ESPERA_TX;
}

bool encolar(const uint8_t* datos, size_t len, uint32_t retardo_ms) {
  if (g_cola_n >= MAX_TX_QUEUE) return false;
  memcpy(g_cola[g_cola_n].datos, datos, len);
  g_cola[g_cola_n].len        = len;
  g_cola[g_cola_n].retardo_ms = retardo_ms;
  g_cola_n++;
  if (g_estado == Estado::RX) programarTx();
  return true;
}

void arrancarCad() {
  // startChannelScan() reconfigura por su cuenta la mascara de DIO1 para CAD,
  // y startReceive()/stageMode() la devuelven a RX_DONE/TX_DONE. Con un solo
  // setDio1Action en el arranque basta.
  if (g_radio.startChannelScan() != RADIOLIB_ERR_NONE) { aRecepcion(); return; }
  g_estado = Estado::CAD;
}

void transmitirPrimero() {
  const Salida& s = g_cola[0];

  // Calcular el tiempo en aire ANTES de arrancar: getTimeOnAir habla por SPI y
  // no conviene hacerlo mientras el amplificador esta subiendo.
  const uint32_t aire = tiempoEnAireMs(s.len);

  if (g_radio.startTransmit(s.datos, s.len) != RADIOLIB_ERR_NONE) {
    Serial.println("radio: startTransmit fallo");
    aRecepcion();
    return;
  }
  g_airtime_ms += aire;
  g_duty.registrar(millis(), aire);   // cuenta para el limite legal del 10 %
  // Watchdog: si se pierde el flanco de TX_DONE, sin esto el nodo se queda
  // mudo y sordo hasta el siguiente reinicio.
  g_deadline = millis() + aire + 500;
  g_estado   = Estado::TX;
}

void sacarDeCola() {
  if (g_cola_n == 0) return;
  for (size_t i = 1; i < g_cola_n; i++) g_cola[i - 1] = g_cola[i];
  g_cola_n--;
}

void leerRecibido() {
  uint8_t buf[255];
  const size_t len = g_radio.getPacketLength();
  const int16_t st = g_radio.readData(buf, len);

  if (st == RADIOLIB_ERR_CRC_MISMATCH) {
    g_n_crc++;
  } else if (st == RADIOLIB_ERR_NONE && len > 0) {
    g_rssi = (int16_t)g_radio.getRSSI();
    g_snr  = g_radio.getSNR();
    if (!g_rx_listo) {            // si ya hay uno sin consumir, se descarta
      memcpy(g_rx.datos, buf, len);
      g_rx.len       = len;
      g_rx.rssi      = g_rssi;
      g_rx.snr       = g_snr;
      g_rx.millis_rx = millis();
      g_rx_listo     = true;
    }
    g_n_rx++;
  }
}

void manejarIrq() {
  switch (g_estado) {
    case Estado::CAD: {
      const int16_t r = g_radio.getChannelScanResult();
      if (r == RADIOLIB_CHANNEL_FREE) {
        transmitirPrimero();
      } else {
        // Canal ocupado (RADIOLIB_LORA_DETECTED) o resultado indeterminado.
        // getChannelScanResult() NO limpia el estado de interrupcion y el CAD
        // deja el chip en reposo, asi que hay que volver a recepcion de forma
        // explicita: startReceive() limpia la IRQ y rearma DIO1.
        g_radio.startReceive();
        if (++g_cad_intentos >= 8) {
          g_cad_intentos = 0;
          programarTx(1000);        // canal muy cargado: esperar mas
        } else {
          programarTx();
        }
      }
      break;
    }

    case Estado::TX:
      g_radio.finishTransmit();
      g_n_tx++;
      sacarDeCola();
      g_radio.startReceive();       // hay que oir entre paquete y paquete
      if (g_cola_n) programarTx();
      else          aRecepcion();
      break;

    case Estado::RX:
      leerRecibido();
      aRecepcion();                 // readData deja la radio fuera de RX
      break;

    case Estado::ESPERA_TX:
      // Seguimos escuchando mientras vence el retardo.
      leerRecibido();
      g_radio.startReceive();
      break;

    default:
      break;
  }
}

}  // namespace

// =============================================================================

Init radioBegin(bool* tcxo_ok) {
  if (tcxo_ok) *tcxo_ok = true;

  // El conmutador de antena externo es OPCIONAL, no un requisito.
  //
  // Medido el 29-jul-2026 en el aparato: el PI4IOE5V6408 no responde en 0x43 ni
  // en 0x44, en ninguno de los dos buses I2C, ni a 100 ni a 400 kHz, y el
  // escaneo es IDENTICO con el Cap montado y sin el. O sea que sus lineas I2C
  // no llegan al host. Ver ESTADO.md para el detalle de la investigacion.
  //
  // No se bloquea la radio por eso, por tres razones:
  //   1. Meshtastic (variant.cpp, findLoraCapBus) y MeshCore (target.cpp,
  //      radio_init) registran "Cap not found" y CONTINUAN.
  //   2. M5 documenta que el Cap LoRa868 no lleva expansor y que ahi es normal.
  //   3. Este aparato ya transmitio durante meses con MeshCore sin que nadie
  //      pusiera P0 en alto, y sigue vivo.
  // El conmutador TX/RX real es DIO2 del SX1262 (setDio2AsRfSwitch, mas abajo).
  //
  // Lo que NO se relaja: sigue haciendo falta que el usuario confirme en
  // pantalla que la antena esta atornillada (radioPermitirTx lo gobierna desde
  // main.cpp) y sigue vigente el limite duro de ciclo de trabajo.
  if (!hal::antennaOk()) {
    Serial.printf("radio: conmutador de antena no verificado (%s).\n"
                  "  Se continua: el TX/RX lo hace DIO2 del SX1262. Si no sale\n"
                  "  potencia por la antena, sospechar de esto lo primero.\n",
                  hal::antennaMensaje(hal::antennaEstado()));
  }

  hal::spiBegin();   // obligatorio: con este constructor RadioLib no llama a SPI.begin()

  int16_t st = g_radio.begin(RF_FREQ_MHZ, PRESET_LONG_FAST_BW, PRESET_LONG_FAST_SF,
                             PRESET_LONG_FAST_CR, RF_SYNC_WORD, RF_TX_POWER_DBM,
                             RF_PREAMBLE, LORA_TCXO_V);

  // Placa con oscilador de cristal en vez de TCXO. Los codigos son -706 y -707;
  // -705 (SPI_CMD_TIMEOUT) significa que BUSY no baja, que es un fallo de
  // cableado y NO se debe enmascarar reintentando.
  if (st == RADIOLIB_ERR_SPI_CMD_INVALID || st == RADIOLIB_ERR_SPI_CMD_FAILED) {
    Serial.println("radio: sin TCXO, reintentando con XTAL");
    if (tcxo_ok) *tcxo_ok = false;
    g_radio.XTAL = true;
    st = g_radio.begin(RF_FREQ_MHZ, PRESET_LONG_FAST_BW, PRESET_LONG_FAST_SF,
                       PRESET_LONG_FAST_CR, RF_SYNC_WORD, RF_TX_POWER_DBM,
                       RF_PREAMBLE, 0.0f);
  }
  if (st != RADIOLIB_ERR_NONE) {
    Serial.printf("radio: begin fallo (%d)\n", st);
    g_estado = Estado::ERROR;
    return Init::FALLO_SPI;
  }

  if (g_radio.setCRC(1) != RADIOLIB_ERR_NONE ||          // LORA_CRC_ON
      g_radio.setCurrentLimit(LORA_CURRENT_MA) != RADIOLIB_ERR_NONE) {
    Serial.println("radio: fallo de configuracion");
    g_estado = Estado::ERROR;
    return Init::FALLO_CONFIG;
  }
  // SX1262::begin() ya lo hace; se deja explicito por claridad.
  g_radio.setDio2AsRfSwitch(true);

  g_radio.setDio1Action(onDio1);
  aRecepcion();

  Serial.printf("radio: %.3f MHz  SF%d  BW%.0f  CR4/%d  %d dBm  slot %.1f ms\n",
                RF_FREQ_MHZ, PRESET_LONG_FAST_SF, PRESET_LONG_FAST_BW,
                PRESET_LONG_FAST_CR, RF_TX_POWER_DBM, slotTimeMs());
  return Init::OK;
}

Estado estado() { return g_estado; }

const char* estadoNombre(Estado e) {
  switch (e) {
    case Estado::SIN_INICIAR: return "sin iniciar";
    case Estado::RX:          return "escuchando";
    case Estado::ESPERA_TX:   return "esperando turno";
    case Estado::CAD:         return "sondeando canal";
    case Estado::TX:          return "transmitiendo";
    default:                  return "ERROR";
  }
}

const char* initMensaje(Init i) {
  switch (i) {
    case Init::OK:                 return "OK";
    case Init::SIN_SWITCH_ANTENA:  return "conmutador de antena sin verificar";
    case Init::FALLO_SPI:          return "el SX1262 no responde";
    default:                       return "fallo de configuracion";
  }
}

void radioPermitirTx(bool permitir) {
  g_tx_ok = permitir;
  Serial.printf("radio: transmision %s\n", permitir ? "PERMITIDA" : "bloqueada");
}
bool radioTxPermitido() { return g_tx_ok; }

bool radioEnviar(const uint8_t* datos, size_t len, bool automatico) {
  if (!g_tx_ok || g_estado == Estado::SIN_INICIAR || g_estado == Estado::ERROR) return false;
  if (len == 0 || len > MESH_MAX_LORA_PAYLOAD) return false;

  if (!g_duty.permitido(millis(), automatico)) {
    Serial.printf("radio: ciclo de trabajo al %.1f%%, envio %s rechazado\n",
                  g_duty.utilizacionPct(millis()), automatico ? "automatico" : "de usuario");
    return false;
  }

  return encolar(datos, len,
                 mesh::retardoPropioMs(ocupacionPct(),
                                       (uint32_t)random(0, 0x7FFFFFFF), slotTimeMs()));
}

bool radioRelevar(const uint8_t* datos, size_t len, float snr) {
  if (!g_tx_ok || g_estado == Estado::SIN_INICIAR || g_estado == Estado::ERROR) return false;
  if (len == 0 || len > MESH_MAX_LORA_PAYLOAD) return false;

  // Un relevo cuenta como trafico automatico: si el aire esta cargado, antes
  // se deja de repetir lo ajeno que de mandar lo propio.
  if (!g_duty.permitido(millis(), true)) return false;

  return encolar(datos, len,
                 mesh::retardoRelevoMs(snr, (uint32_t)random(0, 0x7FFFFFFF), slotTimeMs()));
}

size_t radioPendientes() { return g_cola_n; }

float    dutyPct()                { return g_duty.utilizacionPct(millis()); }
uint32_t dutyMinutosParaLiberar() { return g_duty.minutosParaLiberar(millis()); }
bool     dutyBloqueado()          { return !g_duty.permitido(millis(), false); }

void radioLoop() {
  if (g_estado == Estado::SIN_INICIAR || g_estado == Estado::ERROR) return;

  // 1. Los eventos de la radio van PRIMERO. Si se atendieran los temporizadores
  //    antes, un paquete recibido justo al vencer el retardo se perderia y su
  //    interrupcion se interpretaria luego como resultado del CAD.
  if (g_irq) {
    g_irq = false;
    manejarIrq();
  } else {
    // 2. Temporizadores.
    if (g_estado == Estado::ESPERA_TX && (int32_t)(millis() - g_deadline) >= 0) {
      arrancarCad();
    } else if (g_estado == Estado::TX && (int32_t)(millis() - g_deadline) >= 0) {
      // Se perdio el flanco de TX_DONE. Recuperamos en vez de quedarnos mudos.
      Serial.println("radio: watchdog de TX");
      g_radio.finishTransmit();
      g_n_tx++;
      sacarDeCola();
      if (g_cola_n) { g_radio.startReceive(); programarTx(); }
      else          aRecepcion();
    } else if (g_estado == Estado::RX && g_cola_n) {
      // Red de seguridad: nunca dejar la cola varada.
      programarTx();
    }
  }

  // 3. Rearme por nivel. Un solo bool puede perder un flanco, y como DIO1 se
  //    engancha por flanco de subida y el registro de IRQ del SX126x solo se
  //    limpia desde RadioLib, un flanco perdido dejaria el nodo sordo para
  //    siempre. Si la linea sigue alta, queda trabajo pendiente.
  if (digitalRead(PIN_LORA_DIO1)) g_irq = true;
}

bool radioRecibir(Paquete* p) {
  if (!g_rx_listo) return false;
  *p = g_rx;
  g_rx_listo = false;
  return true;
}

int16_t  ultimoRssi()     { return g_rssi; }
float    ultimoSnr()      { return g_snr; }
uint32_t enviados()       { return g_n_tx; }
uint32_t recibidos()      { return g_n_rx; }
uint32_t errCrc()         { return g_n_crc; }
uint32_t airtimeMsTotal() { return g_airtime_ms; }

uint32_t tiempoEnAireMs(size_t len) {
  return (uint32_t)(g_radio.getTimeOnAir(len) / 1000UL);   // RadioLib da microsegundos
}

void reiniciarContadores() { g_n_tx = g_n_rx = g_n_crc = 0; g_airtime_ms = 0; }

float   freqMhz()      { return RF_FREQ_MHZ; }
uint8_t sf()           { return PRESET_LONG_FAST_SF; }
float   bwKhz()        { return PRESET_LONG_FAST_BW; }
int8_t  potenciaDbm()  { return RF_TX_POWER_DBM; }

}  // namespace radio
