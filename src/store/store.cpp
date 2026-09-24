#include "store/store.h"

#include <Arduino.h>
#include <Preferences.h>
#include <stdio.h>
#include <string.h>

#include "hal/storage.h"
#include "proto/kdf.h"
#include "strings.h"
#include "util/chatlog.h"
#include "util/utf8.h"

namespace store {
namespace {

Preferences g_nvs;
Config      g_cfg;

// Buffer circular en RAM. Con tarjeta es una cache del fichero; sin ella, es
// todo el historial que hay.
Mensaje g_msgs[CHAT_RAM_MESSAGES];
size_t  g_n = 0;        // cuantos hay
size_t  g_ini = 0;      // indice del mas antiguo

// Asociacion entre el ultimo mensaje propio y su id de paquete, para poder
// marcarlo como propagado cuando llegue el ACK implicito.
uint32_t g_ids[CHAT_RAM_MESSAGES] = {};

// Cola de lineas pendientes de escribir en la tarjeta. Room for the escaped
// text (a newline becomes two bytes) plus the fixed columns.
constexpr size_t PENDIENTES_MAX = 16;
constexpr size_t LINEA_MAX = MSG_BUF_BYTES + 80;
char   g_pend[PENDIENTES_MAX][LINEA_MAX];
size_t g_pend_n = 0;
uint32_t g_ultimo_volcado = 0;

size_t indiceFisico(size_t logico) { return (g_ini + logico) % CHAT_RAM_MESSAGES; }

// Into the RAM ring only. anadirMensaje() is this plus the line for the card.
void meterEnBuffer(const Mensaje& m) {
  size_t idx;
  if (g_n < CHAT_RAM_MESSAGES) {
    idx = indiceFisico(g_n);
    g_n++;
  } else {
    idx = g_ini;                        // desaloja el mas antiguo
    g_ini = (g_ini + 1) % CHAT_RAM_MESSAGES;
  }
  g_msgs[idx] = m;
  g_ids[idx]  = 0;
}

void encolarLinea(const Mensaje& m) {
  if (!hal::sdOk()) return;
  if (g_pend_n >= PENDIENTES_MAX) return;   // se pierde la linea, no el mensaje

  // Formato tabulado: see util/chatlog.h. Se abre en cualquier hoja de calculo
  // sin herramientas especiales.
  util::EntradaChat e;
  e.epoch = m.epoch;
  e.nodo  = m.nodo;
  e.conv  = m.conv;
  e.tx    = (m.nodo == 0);
  e.rssi  = m.rssi;
  e.snr   = m.snr;
  snprintf(e.texto, sizeof(e.texto), "%s", m.texto);
  if (util::formatearLineaChat(e, g_pend[g_pend_n], LINEA_MAX)) g_pend_n++;
}

void restaurarLinea(const char* linea, void*) {
  util::EntradaChat e;
  if (!util::analizarLineaChat(linea, &e)) return;
  Mensaje m;
  m.nodo   = e.tx ? 0 : e.nodo;
  m.conv   = e.conv;
  m.epoch  = e.epoch;
  m.ms     = 0;
  m.rssi   = e.rssi;
  m.snr    = e.snr;
  m.estado = e.tx ? EST_ARCHIVO : EST_RECIBIDO;
  m.leido  = true;          // old news: it must not light the unread marker
  snprintf(m.texto, sizeof(m.texto), "%s", e.texto);
  // Red again, but WITHOUT the alarm: that only fires from app::recibirTexto(),
  // and an SOS from last week must not take over the screen at boot.
  m.sos = strncmp(m.texto, S_SOS_TEXTO, strlen(S_SOS_TEXTO)) == 0;
  meterEnBuffer(m);
}

}  // namespace

// ============================== configuracion ===============================

void begin() {
  if (!g_nvs.begin("fantashtic", false)) {
    Serial.println("store: NVS no abre; la configuracion no se guardara");
  }

  g_cfg.configurado = g_nvs.getBool("cfgok", false);
  if (g_cfg.configurado) {
    // Si la clave no esta o no mide lo que debe, mejor volver al asistente que
    // arrancar en silencio con una clave de ceros y no hablar con nadie.
    if (g_nvs.getBytes("psk", g_cfg.psk, PSK_LEN) != PSK_LEN) {
      Serial.println("store: la clave guardada no es valida, se pedira la frase");
      g_cfg.configurado = false;
    } else {
      g_nvs.getString("chname", g_cfg.canal, sizeof(g_cfg.canal));
    }
  }
  g_nvs.getString("lname", g_cfg.nombre_largo, sizeof(g_cfg.nombre_largo));
  g_nvs.getString("sname", g_cfg.nombre_corto, sizeof(g_cfg.nombre_corto));
  g_cfg.preset     = g_nvs.getUChar("preset", 0);
  g_cfg.tx_dbm     = g_nvs.getUChar("txpwr", RF_TX_POWER_DBM);
  g_cfg.hop_limit  = g_nvs.getUChar("hoplim", HOP_DEFAULT);
  g_cfg.brillo     = g_nvs.getUChar("bright", 60);   // nivel 3 de 5
  g_cfg.volumen    = g_nvs.getUChar("vol", 150);     // nivel 3 de 5
  g_cfg.gps_activo = g_nvs.getBool("gpson", true);
  g_cfg.antena_ok  = g_nvs.getBool("antok", false);
  g_cfg.recuperar_chat = g_nvs.getBool("chatrec", true);

  Serial.printf("store: %s, canal '%s', nodo '%s'\n",
                g_cfg.configurado ? "configurado" : "SIN CONFIGURAR",
                g_cfg.canal, g_cfg.nombre_corto);
}

const Config& config()      { return g_cfg; }
Config&       configMutable() { return g_cfg; }

bool guardar() {
  // Ojo: 'bool &= size_t' seria un Y bit a bit. Hay que comparar antes.
  bool ok = g_nvs.putBool("cfgok", g_cfg.configurado) > 0;
  if (g_cfg.configurado) {
    ok = ok && (g_nvs.putBytes("psk", g_cfg.psk, PSK_LEN) == PSK_LEN);
    ok = ok && (g_nvs.putString("chname", g_cfg.canal) > 0);
  }
  g_nvs.putString("lname", g_cfg.nombre_largo);
  g_nvs.putString("sname", g_cfg.nombre_corto);
  g_nvs.putUChar("preset", g_cfg.preset);
  g_nvs.putUChar("txpwr", g_cfg.tx_dbm);
  g_nvs.putUChar("hoplim", g_cfg.hop_limit);
  g_nvs.putUChar("bright", g_cfg.brillo);
  g_nvs.putUChar("vol", g_cfg.volumen);
  g_nvs.putBool("gpson", g_cfg.gps_activo);
  g_nvs.putBool("antok", g_cfg.antena_ok);
  g_nvs.putBool("chatrec", g_cfg.recuperar_chat);
  return ok;
}

bool ponerFrase(const char* frase) {
  char nombre[CHANNEL_NAME_LEN];
  if (!proto::derivarCanal(frase, g_cfg.psk, nombre, sizeof(nombre))) return false;
  snprintf(g_cfg.canal, sizeof(g_cfg.canal), "%s", nombre);
  g_cfg.configurado = true;
  return guardar();
}

void restablecer() {
  g_nvs.clear();
  g_cfg = Config{};
  guardar();
}

// ================================ historial =================================

void anadirMensaje(const Mensaje& m) {
  meterEnBuffer(m);
  encolarLinea(m);
}

void recordarIdPaquete(uint32_t paquete_id) {
  if (g_n == 0) return;
  g_ids[indiceFisico(g_n - 1)] = paquete_id;
}

void marcarEstado(uint32_t paquete_id, uint8_t estado) {
  if (paquete_id == 0) return;
  for (size_t i = 0; i < g_n; i++) {
    const size_t f = indiceFisico(i);
    if (g_ids[f] != paquete_id) continue;
    uint8_t& e = g_msgs[f].estado;
    if (e == EST_ENTREGADO) return;                              // final
    if (estado == EST_PROPAGADO && e != EST_ENVIANDO) return;    // no going back
    e = estado;
    return;
  }
}

size_t numMensajes() { return g_n; }

const Mensaje* mensaje(size_t i) {
  if (i >= g_n) return nullptr;
  return &g_msgs[indiceFisico(i)];
}

size_t noLeidos(uint32_t conv) {
  size_t n = 0;
  for (size_t i = 0; i < g_n; i++) {
    const Mensaje& m = g_msgs[indiceFisico(i)];
    if (m.conv == conv && !m.leido) n++;
  }
  return n;
}

size_t noLeidosTotal() {
  size_t n = 0;
  for (size_t i = 0; i < g_n; i++) n += g_msgs[indiceFisico(i)].leido ? 0 : 1;
  return n;
}

void marcarLeidos(uint32_t conv) {
  for (size_t i = 0; i < g_n; i++) {
    Mensaje& m = g_msgs[indiceFisico(i)];
    if (m.conv == conv) m.leido = true;
  }
}

size_t restaurarHistorial() {
  if (!g_cfg.recuperar_chat || !hal::sdOk()) return 0;
  const size_t antes = g_n;
  hal::sdReadTail(SD_FILE_CHAT, CHAT_RAM_MESSAGES, restaurarLinea, nullptr);
  const size_t n = g_n - antes;
  Serial.printf("store: %u mensajes recuperados de la microSD\n", (unsigned)n);
  return n;
}

void volcarYa() {
  if (!hal::sdOk() || g_pend_n == 0) return;

  for (size_t i = 0; i < g_pend_n; i++) hal::sdAppendLine(SD_FILE_CHAT, g_pend[i]);
  g_pend_n = 0;
  g_ultimo_volcado = millis();
}

void tickAlmacenamiento(bool radio_ociosa) {
  if (g_pend_n == 0) return;
  if (!radio_ociosa) return;      // el bus SPI esta compartido con la radio
  if (millis() - g_ultimo_volcado < SD_FLUSH_INTERVAL_MS) return;
  volcarYa();
}

bool hayTarjeta() { return hal::sdOk(); }

}  // namespace store
