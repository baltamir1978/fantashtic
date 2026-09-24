#pragma once
// microSD, sobre el bus SPI3 compartido con la LoRa.
//
// La tarjeta es OPCIONAL: sin ella el firmware funciona igual, solo se pierde
// el historial persistente. Nunca se bloquea el arranque por no haber tarjeta.
//
// El bus SPI se crea aqui y se comparte con la radio (Fase 2). El guard
// spiTake()/spiGive() serializa los accesos: con un solo hilo basta un bool,
// pero hay que respetarlo o se pisaran los dos CS.

#include <stdint.h>
#include <stddef.h>

class SPIClass;

namespace hal {

// Instancia SPI compartida (SPI3_HOST). Creada por spiBegin().
SPIClass& spiBus();

// Arranca el bus SPI3 en los pines de config.h. Idempotente.
// NO usar el objeto global SPI: ese es SPI2_HOST, el de la pantalla.
void spiBegin();

// Guard cooperativo y REENTRANTE del bus (contador, no booleano: las
// funciones publicas de aqui se llaman entre si). Con un solo hilo no puede
// fallar, asi que spiTake() siempre devuelve true; existe para documentar y
// para que spiBusy() diga si hay una transaccion en vuelo -- que es lo que
// la Fase 2 consultara antes de volcar a la tarjeta.
bool spiTake();
void spiGive();
bool spiBusy();

// --- microSD -----------------------------------------------------------------

// Monta la tarjeta. Devuelve false si no hay o si falla; en ese caso el resto
// de funciones son no-ops seguras.
bool sdBegin();
bool sdOk();

// Crea SD_DIR_BASE y subdirectorios si no existen.
bool sdEnsureDirs();

// Anade una linea a un fichero, creandolo si hace falta. Abre, escribe, cierra.
// Devuelve false si no hay tarjeta o si la escritura falla (y marca la tarjeta
// como perdida, encendiendo el indicador de la barra de estado).
bool sdAppendLine(const char* path, const char* line);

// Lee la ultima linea de un fichero a un buffer. Para el autotest.
bool sdReadLastLine(const char* path, char* out, size_t out_len);

// Espacio total y libre en MB. 0 si no hay tarjeta.
// Calls 'fn' with each of the last 'max_lineas' lines of the file, oldest first,
// without the line ending. Seeks back from the end instead of reading the whole
// file, so boot time does not grow with the history. Lines longer than the read
// buffer (~480 bytes) come out split; the caller must reject what it does not
// recognise. false if there is no card or no file.
bool sdReadTail(const char* path, size_t max_lineas,
                void (*fn)(const char* linea, void* ctx), void* ctx);

uint32_t sdTotalMB();
uint32_t sdUsedMB();

// Autotest de la Fase 1: crea directorios, escribe una marca de tiempo y la
// relee. Deja el resultado en 'msg'.
bool sdSelfTest(char* msg, size_t msg_len);

}  // namespace hal
