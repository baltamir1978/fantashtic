#include "app/app.h"

#include <Arduino.h>
#include <stdio.h>
#include <string.h>

#include "config.h"
#include "hal/gps.h"
#include "hal/led.h"
#include "hal/power.h"
#include "hal/sound.h"
#include "mesh/history.h"
#include "mesh/router.h"
#include "proto/crypto.h"
#include "proto/messages.h"
#include "proto/packet.h"
#include "radio/radio.h"
#include "strings.h"
#include "util/geo.h"
#include "util/utf8.h"

namespace app {
namespace {

uint32_t g_nodo = 0;
uint8_t  g_canal_hash = 0;

mesh::BaseNodos g_nodos;
mesh::Historial g_hist;

uint32_t g_ultimo_msg_ms   = 0;
uint32_t g_seleccionado    = 0;

// Envios periodicos.
uint32_t g_prox_nodeinfo = 0;

// Si el proximo NODEINFO va con want_response. Se pone al arrancar y al oir a un
// nodo cuyo nombre no sabemos; se quita al contestar a otro, para no encadenar
// respuestas indefinidamente.
bool g_nodeinfo_pide = true;
uint32_t g_prox_telemetry = 0;
uint32_t g_ultima_pos_ms = 0;
int32_t  g_ultima_lat = 0, g_ultima_lon = 0;
bool     g_hubo_pos = false;

// --- SOS ---
Sos      g_sos = Sos::INACTIVO;
uint32_t g_sos_t0 = 0;            // inicio de la cuenta atras
uint32_t g_sos_lanzado_ms = 0;    // ultimo envio efectivo
uint8_t  g_sos_enviados = 0;
uint32_t g_sos_prox = 0;
uint32_t g_sos_origen = 0;
uint32_t g_prox_pos_intento = 0;  // reintento tras un envio de posicion fallido

const uint8_t* clave()     { return store::config().psk; }
size_t         claveLen()  { return PSK_LEN; }

// ---------------------------------------------------------------------------

// Devuelve el id del paquete, o 0 si no se pudo enviar. El id hace falta para
// asociar el mensaje con su ACK implicito, y hay que registrarlo DESPUES de
// guardar el mensaje: al reves se le pega al mensaje anterior.
uint32_t transmitir(uint32_t portnum, const uint8_t* carga, size_t n, bool automatico,
                    bool pedir_respuesta = false) {
  if (!store::config().configurado) return 0;

  proto::Data d;
  d.portnum = portnum;
  if (n > sizeof(d.payload)) return 0;
  memcpy(d.payload, carga, n);
  d.payload_len   = n;
  d.want_response = pedir_respuesta;

  proto::Cabecera c;
  c.to        = MESH_BROADCAST_ADDR;
  c.from      = g_nodo;
  c.id        = proto::generarId(esp_random());
  c.hop_limit = store::config().hop_limit;
  c.hop_start = store::config().hop_limit;
  c.canal     = g_canal_hash;

  uint8_t aire[MESH_MAX_LORA_PAYLOAD];
  const size_t len = proto::construirPaquete(c, d, clave(), claveLen(), aire, sizeof(aire));
  if (len == 0) return 0;
  if (!radio::radioEnviar(aire, len, automatico)) return 0;

  // Se registra antes de que salga, para ignorar nuestros ecos y reconocer el
  // ACK implicito cuando alguien lo retransmita.
  g_hist.vistoOAnadir(c.from, c.id);
  return c.id;
}

// 'pedir_respuesta' pone want_response: "aqui estoy, y tu quien eres". Es lo que
// hace que el descubrimiento funcione en un viaje de ida y vuelta en vez de
// depender de que el otro lado tambien nos desconozca.
void enviarNodeInfo(bool pedir_respuesta) {
  proto::User u;
  proto::idDesdeNodeNum(g_nodo, u.id, sizeof(u.id));
  snprintf(u.largo, sizeof(u.largo), "%s", store::config().nombre_largo);
  snprintf(u.corto, sizeof(u.corto), "%s", store::config().nombre_corto);

  uint8_t b[128];
  const size_t n = proto::codificarUser(u, b, sizeof(b));
  if (n) transmitir(PORTNUM_NODEINFO, b, n, true, pedir_respuesta);
}

void enviarTelemetria() {
  proto::Telemetry t;
  t.tiempo      = hal::gpsFix().epoch;
  t.bateria_pct = hal::batteryPct();
  t.voltaje     = hal::batteryMv() / 1000.0f;
  t.uso_tx      = radio::dutyPct();
  t.uptime_s    = millis() / 1000;

  uint8_t b[64];
  const size_t n = proto::codificarTelemetry(t, b, sizeof(b));
  if (n) transmitir(PORTNUM_TELEMETRY, b, n, true);
}

void enviarPosicion() {
  const auto& f = hal::gpsFix();
  if (!f.valido) return;

  proto::Position p;
  p.lat_e7 = f.lat_e7; p.lon_e7 = f.lon_e7;
  p.altitud = f.altitud_m; p.tiempo = f.epoch; p.sats = f.satelites;

  uint8_t b[64];
  const size_t n = proto::codificarPosition(p, b, sizeof(b));
  if (!n) return;
  if (transmitir(PORTNUM_POSITION, b, n, true)) {
    g_ultima_lat = f.lat_e7; g_ultima_lon = f.lon_e7;
    g_ultima_pos_ms = millis();
    g_hubo_pos = true;
  } else {
    // Sin esto, con el umbral blando de ciclo de trabajo alcanzado se
    // reintentaria el cifrado completo en cada tick de 10 ms.
    g_prox_pos_intento = millis() + 30000;
  }
}

// Solo se emite posicion si nos hemos movido o ha pasado mucho tiempo: en un
// grupo parado, mandarla cada minuto seria gastar aire para nada.
bool tocaPosicion() {
  const auto& f = hal::gpsFix();
  if (!f.valido) return false;
  if ((int32_t)(millis() - g_prox_pos_intento) < 0) return false;
  if (!g_hubo_pos) return true;
  if (millis() - g_ultima_pos_ms > POSITION_MAX_AGE_MS) return true;
  return util::distanciaM(g_ultima_lat, g_ultima_lon, f.lat_e7, f.lon_e7) > POSITION_MIN_DIST_M;
}

void componerTextoSos(char* out, size_t cap) {
  const auto& f = hal::gpsFix();
  if (f.valido) {
    snprintf(out, cap, "%s %s @ %.5f,%.5f", S_SOS_TEXTO,
             store::config().nombre_corto,
             (double)f.lat_e7 * 1e-7, (double)f.lon_e7 * 1e-7);
  } else {
    snprintf(out, cap, "%s %s", S_SOS_TEXTO, store::config().nombre_corto);
  }
}

void lanzarSos() {
  char txt[MSG_BUF_BYTES];
  componerTextoSos(txt, sizeof(txt));

  // El SOS ignora el umbral blando del ciclo de trabajo, pero NO el duro:
  // el limite de la subbanda no se salta ni en una emergencia.
  const uint32_t id = transmitir(PORTNUM_TEXT_MESSAGE,
                                 (const uint8_t*)txt, strlen(txt), false);

  store::Mensaje m;
  m.nodo = 0; m.ms = millis(); m.epoch = hal::gpsFix().epoch;
  m.estado = id ? store::EST_ENVIANDO : store::EST_FALLIDO;
  m.sos = true;
  snprintf(m.texto, sizeof(m.texto), "%s", txt);
  store::anadirMensaje(m);
  if (id) store::recordarIdPaquete(id);      // DESPUES de guardar el mensaje

  if (hal::gpsValido()) enviarPosicion();
  // El volcado a la tarjeta lo hace el tick cuando la radio quede libre: el
  // bus SPI esta compartido y acabamos de encolar una transmision.

  g_sos_enviados++;
  g_sos_lanzado_ms = millis();
  g_sos_prox = millis() + (g_sos_enviados < SOS_BURST_COUNT
                               ? SOS_BURST_INTERVAL_MS : SOS_REPEAT_MS);
  g_sos = (g_sos_enviados < SOS_BURST_COUNT) ? Sos::ENVIANDO : Sos::REPITIENDO;
}

void recibirTexto(uint32_t de, const char* txt, int16_t rssi, float snr) {
  store::Mensaje m;
  m.nodo = de; m.ms = millis(); m.epoch = hal::gpsFix().epoch;
  m.rssi = rssi; m.snr = (int8_t)snr; m.estado = store::EST_RECIBIDO;
  snprintf(m.texto, sizeof(m.texto), "%s", txt);
  // Un SOS ajeno toma la pantalla y hace sonar la alarma. Se compara con el
  // PRINCIPIO del texto, no con strstr: si no, cualquiera que escriba "SOS -"
  // en mitad de una frase secuestraria la pantalla de todo el grupo.
  m.sos = strncmp(txt, S_SOS_TEXTO, strlen(S_SOS_TEXTO)) == 0;
  store::anadirMensaje(m);
  g_ultimo_msg_ms = millis();

  if (m.sos) {
    // Un SOS ajeno NO pisa el nuestro: si estamos pidiendo ayuda, seguimos.
    if (g_sos == Sos::INACTIVO || g_sos == Sos::RECIBIDO) {
      g_sos = Sos::RECIBIDO;
      g_sos_origen = de;
      hal::alarmaSos();
      hal::ledBlink(255, 0, 0, 250);
    }
  } else {
    hal::beepMensaje();
    hal::ledBlink(0, 60, 0, 120);
  }
}

// Para la traza de consola: sin esto, "RX de 130C9A3C" no dice si lo que llego
// fue un nombre, una posicion o un mensaje, que es justo lo que hace falta saber
// cuando la lista de nodos no se puebla.
const char* portnumNombre(uint32_t p) {
  switch (p) {
    case PORTNUM_TEXT_MESSAGE: return "TEXTO";
    case PORTNUM_POSITION:     return "POSICION";
    case PORTNUM_NODEINFO:     return "NODEINFO";
    case PORTNUM_ROUTING:      return "ROUTING";
    case PORTNUM_TELEMETRY:    return "TELEMETRIA";
    default:                   return "?";
  }
}

void procesarEntrega(const proto::Cabecera& c, const radio::Paquete& p) {
  proto::Cabecera c2;
  proto::Data d;
  if (!proto::analizarPaquete(p.datos, p.len, clave(), claveLen(), &c2, &d)) {
    // Importa distinguirlo: un paquete que se oye pero no descifra NO registra
    // el nodo, asi que desaparece de la lista sin dejar rastro.
    Serial.printf("   ...de %08X NO DESCIFRA (clave o canal distinto)\n",
                  (unsigned)c.from);
    return;
  }

  // Un paquete malformado o ajeno puede traer hop_start < hop_limit; sin la
  // salvaguarda saldria un numero de saltos absurdo en la lista de nodos.
  const uint8_t saltos = (c.hop_start >= c.hop_limit)
                             ? (uint8_t)(c.hop_start - c.hop_limit) : 0;
  mesh::Nodo* nodo = g_nodos.visto(c.from, millis(), p.rssi, (int8_t)p.snr, saltos);
  Serial.printf("   ...%s de %08X, nodos en tabla: %u\n",
                portnumNombre(d.portnum), (unsigned)c.from,
                (unsigned)g_nodos.total());

  // Si oimos a alguien de quien aun no sabemos el nombre, adelantamos NUESTRO
  // NODEINFO **pidiendo respuesta**.
  //
  // Lo de "pidiendo respuesta" es la parte importante, y antes faltaba: sin ella
  // esto solo anunciaba nuestro nombre, que no es lo que nos hace falta. Si el
  // otro lado ya nos conocia, su propia regla no se disparaba, se quedaba callado
  // y nosotros seguiamos viendo un numero hexadecimal hasta su siguiente envio
  // periodico, 15 minutos despues. Ocurria siempre que un nodo se reiniciaba
  // mientras el otro seguia encendido.
  //
  // El retardo aleatorio evita que varios nodos contesten a la vez.
  if (nodo && !nodo->corto[0] &&
      (int32_t)(g_prox_nodeinfo - millis()) > 8000) {
    g_prox_nodeinfo  = millis() + 2000 + (esp_random() % 4000);
    g_nodeinfo_pide  = true;
  }

  switch (d.portnum) {
    case PORTNUM_TEXT_MESSAGE: {
      char txt[MSG_BUF_BYTES];
      const size_t n = d.payload_len < sizeof(txt) - 1 ? d.payload_len : sizeof(txt) - 1;
      memcpy(txt, d.payload, n);
      txt[n] = '\0';
      util::truncarSeguro(txt, sizeof(txt) - 1);
      recibirTexto(c.from, txt, p.rssi, p.snr);
      break;
    }
    case PORTNUM_NODEINFO: {
      proto::User u;
      if (proto::decodificarUser(d.payload, d.payload_len, &u))
        g_nodos.ponerNombres(c.from, u.largo, u.corto);

      // Nos han preguntado quienes somos: contestamos. Con want_response a
      // FALSE, o los dos nodos se contestarian el uno al otro para siempre.
      if (d.want_response &&
          (int32_t)(g_prox_nodeinfo - millis()) > 8000) {
        g_prox_nodeinfo = millis() + 2000 + (esp_random() % 4000);
        g_nodeinfo_pide = false;
      }
      break;
    }
    case PORTNUM_POSITION: {
      proto::Position pos;
      if (proto::decodificarPosition(d.payload, d.payload_len, &pos) &&
          pos.tiene_lat && pos.tiene_lon)
        g_nodos.ponerPosicion(c.from, pos.lat_e7, pos.lon_e7, pos.altitud, millis());
      break;
    }
    case PORTNUM_TELEMETRY: {
      proto::Telemetry t;
      if (proto::decodificarTelemetry(d.payload, d.payload_len, &t) && t.tiene_metrics)
        g_nodos.ponerBateria(c.from, (uint8_t)t.bateria_pct);
      break;
    }
    default: break;
  }
}

}  // namespace

// ===========================================================================

void begin(uint32_t node_num) {
  g_nodo = node_num;
  recargarCanal();
  // Presentarse pronto tras arrancar, y PIDIENDO RESPUESTA: la tabla de nodos
  // vive en RAM, asi que al reiniciar se pierde y hay que volver a preguntar
  // quien anda por ahi. Sin esto, un nodo recien arrancado se queda sin ver a
  // nadie hasta que a otro le toque su envio periodico, hasta 15 minutos.
  g_prox_nodeinfo  = millis() + 5000;
  g_nodeinfo_pide  = true;
  g_prox_telemetry = millis() + 30000;
}

void recargarCanal() {
  g_canal_hash = proto::hashCanal(store::config().canal, store::config().psk, PSK_LEN);
  Serial.printf("app: canal '%s' hash 0x%02X\n", store::config().canal, g_canal_hash);
}

void anunciarNombre() {
  // Solo adelanta el momento; lo transmite loop() como cualquier otro NODEINFO,
  // pasando por la cola y por el contador de ciclo de trabajo. Transmitir desde
  // la interfaz se saltaria las dos cosas.
  g_prox_nodeinfo = millis() + 1000 + (esp_random() % 2000);
}

bool enviarTexto(const char* txt) {
  if (!txt || !*txt) return false;

  char buf[MSG_BUF_BYTES];
  snprintf(buf, sizeof(buf), "%s", txt);
  util::truncarSeguro(buf, MSG_MAX_BYTES);

  const uint32_t id = transmitir(PORTNUM_TEXT_MESSAGE,
                                 (const uint8_t*)buf, strlen(buf), false);

  store::Mensaje m;
  m.nodo = 0; m.ms = millis(); m.epoch = hal::gpsFix().epoch;
  m.estado = id ? store::EST_ENVIANDO : store::EST_FALLIDO;
  snprintf(m.texto, sizeof(m.texto), "%s", buf);
  store::anadirMensaje(m);
  // El id se asocia DESPUES de guardar: recordarIdPaquete marca el ultimo
  // mensaje del buffer, asi que al reves se le pegaria al anterior.
  if (id) store::recordarIdPaquete(id);
  return id != 0;
}

void loop() {
  radio::Paquete p;
  while (radio::radioRecibir(&p)) {
    if (p.len <= MESH_HEADER_LEN) continue;

    proto::Cabecera c;
    if (!proto::desempaquetarCabecera(p.datos, &c)) continue;

    const mesh::Accion a = mesh::decidir(c, g_nodo, g_hist, true);

    // Traza de recepcion. Es la unica forma de leer el RSSI/SNR numericos: la
    // barra de estado solo pinta barritas. Imprescindible para la prueba de
    // alcance (docs/ALCANCE.md) y para la medida de referencia a 1 m.
    Serial.printf("RX de %08X id %08X  %d dBm  SNR %.1f  saltos %u/%u  -> %s\n",
                  (unsigned)c.from, (unsigned)c.id, (int)p.rssi, (double)p.snr,
                  (unsigned)(c.hop_start >= c.hop_limit ? c.hop_start - c.hop_limit : 0),
                  (unsigned)c.hop_start, mesh::accionNombre(a));

    if (a == mesh::Accion::ACK_IMPLICITO) {
      store::marcarEstado(c.id, store::EST_PROPAGADO);
      continue;
    }
    if (a == mesh::Accion::DESCARTAR) continue;

    if (a == mesh::Accion::RETRANSMITIR || a == mesh::Accion::ENTREGAR_Y_RETRANSMITIR) {
      uint8_t copia[MESH_MAX_LORA_PAYLOAD];
      memcpy(copia, p.datos, p.len);
      proto::Cabecera cr = c;
      mesh::prepararRelevo(&cr, g_nodo);
      proto::reescribirCabecera(copia, cr);
      radio::radioRelevar(copia, p.len, p.snr);
    }

    if (a == mesh::Accion::ENTREGAR || a == mesh::Accion::ENTREGAR_Y_RETRANSMITIR)
      procesarEntrega(c, p);
  }

  const uint32_t ahora = millis();

  // --- SOS ---
  switch (g_sos) {
    case Sos::ARMANDO:
      if (ahora - g_sos_t0 >= SOS_COUNTDOWN_MS) lanzarSos();
      break;
    case Sos::ENVIANDO:
    case Sos::REPITIENDO:
      if ((int32_t)(ahora - g_sos_prox) >= 0) {
        if (g_sos_enviados >= SOS_BURST_COUNT + SOS_REPEAT_MAX) g_sos = Sos::INACTIVO;
        else lanzarSos();
      }
      // Si alguien contesta DESPUES de completar la rafaga inicial, dejamos de
      // repetir. Se exige que la rafaga termine: si no, un mensaje cualquiera
      // llegado durante la cuenta atras cortaria el SOS tras un solo envio,
      // que es justo lo que pasa en un grupo que esta hablando.
      if (g_sos_enviados >= SOS_BURST_COUNT &&
          (int32_t)(g_ultimo_msg_ms - g_sos_lanzado_ms) > 0)
        g_sos = Sos::INACTIVO;
      break;
    default: break;
  }

  // --- envios periodicos, todos marcados como automaticos ---
  if ((int32_t)(ahora - g_prox_nodeinfo) >= 0) {
    enviarNodeInfo(g_nodeinfo_pide);
    // El periodico de cada 15 min no pide respuesta: seria pedirle a los cuatro
    // nodos que contesten cada cuarto de hora sin motivo. Solo piden respuesta
    // el del arranque y el que dispara oir a un desconocido.
    g_nodeinfo_pide = false;
    g_prox_nodeinfo = ahora + NODEINFO_INTERVAL_MS;
  }
  if ((int32_t)(ahora - g_prox_telemetry) >= 0) {
    enviarTelemetria();
    g_prox_telemetry = ahora + TELEMETRY_INTERVAL_MS;
  }
  if (tocaPosicion()) enviarPosicion();

  // El volcado a la tarjeta espera a que la radio no este ocupada: el bus SPI
  // esta compartido y una escritura puede tardar decenas de milisegundos.
  store::tickAlmacenamiento(radio::radioOciosa());
}

// --- SOS --------------------------------------------------------------------

void sosArmar() {
  if (g_sos == Sos::INACTIVO) {
    g_sos = Sos::ARMANDO;
    g_sos_t0 = millis();
    g_sos_enviados = 0;
  }
}

void sosConfirmar() { if (g_sos == Sos::ARMANDO) lanzarSos(); }

void sosCancelar() {
  hal::alarmaParar();
  hal::ledOff();
  g_sos = Sos::INACTIVO;
  g_sos_origen = 0;
}

Sos sosEstado() { return g_sos; }

uint8_t sosSegundosRestantes() {
  if (g_sos != Sos::ARMANDO) return 0;
  const uint32_t t = millis() - g_sos_t0;
  if (t >= SOS_COUNTDOWN_MS) return 0;
  return (uint8_t)((SOS_COUNTDOWN_MS - t + 999) / 1000);
}

uint32_t sosNodoOrigen() { return g_sos_origen; }

bool sosResponderVoy() {
  if (!g_sos_origen) return false;
  hal::alarmaParar();
  const bool ok = enviarTexto(S_SOS_VOY);
  sosCancelar();
  return ok;
}

// --- estado -----------------------------------------------------------------

mesh::BaseNodos& nodos()      { return g_nodos; }
uint32_t nodoPropio()         { return g_nodo; }
uint8_t  canalHash()          { return g_canal_hash; }
uint32_t ultimoMensajeMs()    { return g_ultimo_msg_ms; }
void     seleccionarNodo(uint32_t n) { g_seleccionado = n; }
uint32_t nodoSeleccionado()   { return g_seleccionado; }

}  // namespace app
