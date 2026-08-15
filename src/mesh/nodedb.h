#pragma once
// Base de datos de nodos vistos.
//
// Dimensionada para 16, que sobra para los 4 del grupo y deja hueco a vecinos
// ajenos que aparezcan por la zona.
//
// AVISO: los nombres que llegan de fuera hay que TRUNCARLOS. El campo
// long_name del protocolo admite hasta 40 bytes (el firmware oficial recorta a
// 24 al guardar) y aqui solo caben 15 mas el terminador. En la Fase 3 se pone
// a proposito un nodo con firmware oficial en el mismo canal, asi que esto no
// es hipotetico: un strcpy aqui es un desbordamiento en el primer NODEINFO
// ajeno que llegue.

#include <stddef.h>
#include <stdint.h>

#include "config.h"

namespace mesh {

struct Nodo {
  uint32_t num          = 0;      // NodeNum
  char     corto[NODE_SHORTNAME_LEN] = {};   // 4 caracteres + terminador
  char     largo[NODE_LONGNAME_LEN]  = {};   // 15 + terminador
  uint32_t visto_ms     = 0;      // millis del ultimo paquete suyo, de donde sea
  uint32_t directo_ms   = 0;      // ultimo oido a 0 saltos; 0 = nunca
  int16_t  rssi         = 0;
  int8_t   snr          = 0;
  int8_t   snr_medio    = 0;      // media suavizada, para comparar contra ella
  int8_t   tendencia    = 0;      // -1 empeora, 0 estable, +1 mejora
  bool     hay_media    = false;  // la media ya tiene al menos una muestra
  uint8_t  saltos       = 0;
  uint8_t  bateria      = 0;      // 0-100, 255 = desconocida
  bool     tiene_pos    = false;
  int32_t  lat_e7       = 0;
  int32_t  lon_e7       = 0;
  int32_t  altitud_m    = 0;
  uint32_t pos_ms       = 0;
  bool     usado        = false;
};

// Estado del enlace con un nodo. El orden importa: va de mejor a peor y el
// resumen de la barra de estado se queda con el peor de todos.
//
// RELEVO no es un error, es el aviso util en ciudad: significa que al otro ya
// no se le oye de primera mano y lo que llega pasa por un tercero, o sea que se
// esta al borde. Cuando ese tercero se vaya, se pierde el enlace de golpe.
enum class Enlace : uint8_t {
  DIRECTO = 0,   // oido de primera mano hace poco
  RELEVO,        // sigue llegando, pero por salto: se perdio la via directa
  DEBIL,         // lleva un par de latidos callado
  PERDIDO,       // tres latidos: se da por incomunicado
};

// Estado del enlace a partir de los silencios. Funcion libre y sin estado a
// proposito: asi se prueba en el ordenador con relojes inventados, que es la
// unica forma de cubrir los saltos de estado sin esperar tres minutos.
Enlace estadoEnlace(const Nodo& n, uint32_t ahora_ms);

class BaseNodos {
 public:
  // Registra actividad de un nodo, creandolo si no existia. Devuelve el nodo,
  // o nullptr si la tabla esta llena y no habia ninguno que desalojar.
  Nodo* visto(uint32_t num, uint32_t ahora_ms, int16_t rssi, int8_t snr, uint8_t saltos);

  // Actualiza nombres truncando con seguridad. Acepta punteros nulos.
  void ponerNombres(uint32_t num, const char* largo, const char* corto);
  void ponerPosicion(uint32_t num, int32_t lat_e7, int32_t lon_e7,
                     int32_t alt_m, uint32_t ahora_ms);
  void ponerBateria(uint32_t num, uint8_t pct);

  Nodo*       buscar(uint32_t num);
  const Nodo* buscar(uint32_t num) const;

  // Recorrido: indice 0..capacidad-1, devuelve nullptr en los huecos.
  const Nodo* porIndice(size_t i) const;
  static constexpr size_t capacidad() { return MAX_NODES; }

  size_t total() const { return usados_; }
  // Nodos oidos en los ultimos 'ventana_ms' milisegundos.
  size_t activos(uint32_t ahora_ms, uint32_t ventana_ms) const;

  // Nodos con enlace, o sea todos menos los PERDIDOS. Es el numerador del
  // "3/4" de la barra de estado.
  size_t conectados(uint32_t ahora_ms) const;

  // El peor enlace de la tabla, que es el que decide el color de la barra. Con
  // la tabla vacia devuelve DIRECTO: no hay nada que vaya mal, simplemente no
  // hay nadie, y eso ya se dice con el "0/0".
  //
  // Se ignora a quien lleve mas de 'olvidar_ms' callado, por el mismo motivo que
  // en perdidoParaAvisar(): un aparato apagado a proposito no puede dejar la
  // barra en rojo para siempre. Sigue viendose en la lista de nodos, que es
  // donde se mira el historico.
  Enlace peorEnlace(uint32_t ahora_ms, uint32_t olvidar_ms) const;

  // Primer nodo perdido que aun merece un aviso, y cuantos hay. Se ignoran los
  // que llevan mas de 'olvidar_ms' callados: un aparato apagado a proposito no
  // puede dejar una alarma encendida para siempre.
  const Nodo* perdidoParaAvisar(uint32_t ahora_ms, uint32_t olvidar_ms,
                                size_t* cuantos) const;

  // Nombre para mostrar: el corto si lo hay, si no los dos ultimos bytes del
  // NodeNum en hexadecimal. Nunca devuelve una cadena vacia.
  void nombreMostrar(const Nodo& n, char* out, size_t cap) const;

  void limpiar();

 private:
  Nodo tabla_[MAX_NODES];
  size_t usados_ = 0;
};

}  // namespace mesh
