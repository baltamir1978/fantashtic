#pragma once
// Persistencia en dos niveles:
//
//   NVS (flash interna)  configuracion y clave del canal. Poco y pequeno, pero
//                        imprescindible para arrancar y comunicarse.
//   microSD              historial de conversacion y registros de campo. Crece
//                        con el tiempo, y es OPCIONAL: sin tarjeta el aparato
//                        funciona igual, solo pierde el historial al reiniciar.

#include <stddef.h>
#include <stdint.h>

#include "config.h"

namespace store {

// --- configuracion -----------------------------------------------------------
struct Config {
  uint8_t psk[PSK_LEN]                 = {};
  char    canal[CHANNEL_NAME_LEN]      = {};
  char    nombre_largo[NODE_LONGNAME_LEN] = {};
  char    nombre_corto[NODE_SHORTNAME_LEN] = {};
  uint8_t preset      = 0;      // 0 = LONG_FAST
  uint8_t tx_dbm      = RF_TX_POWER_DBM;
  uint8_t hop_limit   = HOP_DEFAULT;
  // Se guarda el valor real, no el nivel de la interfaz: el nivel 1..5 se
  // deriva al vuelo (ver UI_NIVELES en config.h) y asi no hay que migrar la
  // configuracion que ya haya en NVS. Los dos por defecto son el nivel 3.
  uint8_t brillo      = 60;     // 0-100
  uint8_t volumen     = 150;    // 0-255
  bool    gps_activo  = true;
  bool    configurado = false;  // false -> asistente de primer arranque
  // El usuario confirmo una vez que la antena esta atornillada. Se recuerda
  // porque el Cap va fijo: preguntarlo en cada arranque no protege de nada y
  // acaba pulsandose sin leer, que es peor que no preguntar.
  bool    antena_ok   = false;
  // Reload the last messages from the microSD at boot. On by default; without a
  // card it does nothing either way. Off, the file is still written -- this only
  // decides whether the screen starts empty.
  bool    recuperar_chat = true;
};

void begin();
const Config& config();
Config& configMutable();

// Guarda todo lo que hay en memoria. Devuelve false si NVS falla.
bool guardar();

// Deriva clave y nombre de canal desde la frase y los guarda. Tarda cerca de
// un segundo (20 000 iteraciones de PBKDF2): conviene avisar en pantalla.
bool ponerFrase(const char* frase);

// Deja la configuracion de fabrica y borra la clave.
void restablecer();

// --- historial de conversacion ----------------------------------------------
struct Mensaje {
  uint32_t nodo    = 0;      // 0 = nuestro
  // Conversation: 0 = the group, otherwise the other node of a direct chat
  // (who we wrote to, or who wrote to us).
  uint32_t conv    = 0;
  uint32_t epoch   = 0;      // 0 si aun no hay hora del GPS
  uint32_t ms      = 0;
  char     texto[MSG_BUF_BYTES] = {};
  int16_t  rssi    = 0;
  int8_t   snr     = 0;
  uint8_t  estado  = 0;      // EST_*
  bool     sos     = false;
  bool     leido   = true;   // false for an incoming message not yet on screen
};

// EST_PROPAGADO: someone relayed it (implicit ACK). For a group message that is
//                all there is to know.
// EST_ENTREGADO: the recipient of a direct message sent its ACK. Final.
// EST_FALLIDO:   could not be sent, or a direct message ran out of retries.
// EST_ARCHIVO:   our own message reloaded from the card: its state then was
//                not saved, so none is shown rather than a made-up one.
enum : uint8_t {
  EST_RECIBIDO = 0, EST_ENVIANDO, EST_PROPAGADO, EST_FALLIDO, EST_ENTREGADO, EST_ARCHIVO
};

// Anade al buffer circular y, si hay tarjeta, encola la linea para el fichero.
void anadirMensaje(const Mensaje& m);

// Marca por id de paquete el paso de "enviando" a "propagado", "entregado" o
// "fallido". Never goes backwards: a late relay heard after the ACK must not
// turn "delivered" back into "relayed".
void marcarEstado(uint32_t paquete_id, uint8_t estado);
void recordarIdPaquete(uint32_t paquete_id);   // asocia el ultimo mensaje propio

size_t         numMensajes();
const Mensaje* mensaje(size_t i);        // 0 = el mas antiguo en memoria

// Unread incoming messages in one conversation, and marking them read (done by
// the chat screen when it shows that conversation).
size_t noLeidos(uint32_t conv);
size_t noLeidosTotal();
void   marcarLeidos(uint32_t conv);

// Reloads the tail of the chat file into the RAM buffer. Call once at boot,
// after sdBegin() and before the radio is receiving: it reads from the SPI bus
// the radio shares. Does nothing without a card or with recuperar_chat off.
// Returns how many messages came back.
size_t restaurarHistorial();

// Vuelca a la microSD lo que haya pendiente. Llamar solo cuando la radio este
// ociosa: una escritura puede tardar decenas de milisegundos y el bus SPI esta
// compartido con el transceptor.
void tickAlmacenamiento(bool radio_ociosa);
void volcarYa();

bool hayTarjeta();

}  // namespace store
