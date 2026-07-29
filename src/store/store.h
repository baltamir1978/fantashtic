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
  uint32_t epoch   = 0;      // 0 si aun no hay hora del GPS
  uint32_t ms      = 0;
  char     texto[MSG_BUF_BYTES] = {};
  int16_t  rssi    = 0;
  int8_t   snr     = 0;
  uint8_t  estado  = 0;      // 0 recibido, 1 enviando, 2 propagado, 3 fallido
  bool     sos     = false;
};

enum : uint8_t { EST_RECIBIDO = 0, EST_ENVIANDO, EST_PROPAGADO, EST_FALLIDO };

// Anade al buffer circular y, si hay tarjeta, encola la linea para el fichero.
void anadirMensaje(const Mensaje& m);

// Marca por id de paquete el paso de "enviando" a "propagado" o "fallido".
void marcarEstado(uint32_t paquete_id, uint8_t estado);
void recordarIdPaquete(uint32_t paquete_id);   // asocia el ultimo mensaje propio

size_t         numMensajes();
const Mensaje* mensaje(size_t i);        // 0 = el mas antiguo en memoria

// Vuelca a la microSD lo que haya pendiente. Llamar solo cuando la radio este
// ociosa: una escritura puede tardar decenas de milisegundos y el bus SPI esta
// compartido con el transceptor.
void tickAlmacenamiento(bool radio_ociosa);
void volcarYa();

bool hayTarjeta();

}  // namespace store
