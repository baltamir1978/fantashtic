#include "store/store.h"

#include <Arduino.h>
#include <Preferences.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#include "hal/storage.h"
#include "proto/kdf.h"
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

// Cola de lineas pendientes de escribir en la tarjeta.
constexpr size_t PENDIENTES_MAX = 16;
char   g_pend[PENDIENTES_MAX][200];
size_t g_pend_n = 0;
uint32_t g_ultimo_volcado = 0;

size_t indiceFisico(size_t logico) { return (g_ini + logico) % CHAT_RAM_MESSAGES; }

void rutaDelMes(char* out, size_t cap, uint32_t epoch) {
  // Sin hora del GPS todavia, todo va a un fichero de arranque.
  if (epoch < 1600000000u) { snprintf(out, cap, SD_DIR_CHAT "/sin-fecha.log"); return; }
  const time_t t = (time_t)epoch;
  struct tm tmv;
  gmtime_r(&t, &tmv);
  snprintf(out, cap, SD_DIR_CHAT "/%04d-%02d.log", tmv.tm_year + 1900, tmv.tm_mon + 1);
}

void encolarLinea(const Mensaje& m) {
  if (!hal::sdOk()) return;
  if (g_pend_n >= PENDIENTES_MAX) return;   // se pierde la linea, no el mensaje

  // Formato tabulado: epoch, nodo, direccion, RSSI, SNR y texto. Se abre en
  // cualquier hoja de calculo sin herramientas especiales.
  char* d = g_pend[g_pend_n];
  int n = snprintf(d, sizeof(g_pend[0]), "%lu\t%08X\t%s\t%d\t%d\t",
                   (unsigned long)m.epoch, (unsigned)m.nodo,
                   m.estado == EST_RECIBIDO ? "RX" : "TX", (int)m.rssi, (int)m.snr);
  // Los saltos de linea se escapan para no romper el formato de una linea por
  // mensaje.
  for (const char* p = m.texto; *p && n < (int)sizeof(g_pend[0]) - 3; p++) {
    if (*p == '\n')      { d[n++] = '\\'; d[n++] = 'n'; }
    else if (*p == '\t') { d[n++] = ' '; }
    else                 { d[n++] = *p; }
  }
  d[n] = '\0';
  g_pend_n++;
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
    if (g_ids[f] == paquete_id) { g_msgs[f].estado = estado; return; }
  }
}

size_t numMensajes() { return g_n; }

const Mensaje* mensaje(size_t i) {
  if (i >= g_n) return nullptr;
  return &g_msgs[indiceFisico(i)];
}

void volcarYa() {
  if (!hal::sdOk() || g_pend_n == 0) return;

  char ruta[64];
  rutaDelMes(ruta, sizeof(ruta), g_msgs[indiceFisico(g_n ? g_n - 1 : 0)].epoch);

  for (size_t i = 0; i < g_pend_n; i++) hal::sdAppendLine(ruta, g_pend[i]);
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
