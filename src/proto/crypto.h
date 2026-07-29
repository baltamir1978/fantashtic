#pragma once
// Criptografia y funciones resumen del protocolo Meshtastic.
//
// Fuentes (citar al tocar cualquier constante):
//   src/mesh/CryptoEngine.cpp   nonce y AES-CTR
//   src/mesh/Channels.cpp       xorHash y clave por defecto
//   src/mesh/RadioInterface.cpp djb2 y derivacion de frecuencia
//
// Sin dependencias de Arduino: compila tambien en el ordenador.

#include <stddef.h>
#include <stdint.h>

namespace proto {

// Clave por defecto de Meshtastic, expandida (Channels.h). 16 bytes -> AES-128.
extern const uint8_t PSK_POR_DEFECTO[16];

// --- nonce -------------------------------------------------------------------
// 16 bytes:  id (u64 LE, 8) | from (u32 LE, 4) | contador CTR (4 ceros)
// Ambos campos salen de la cabecera SIN CIFRAR, asi que cualquier receptor con
// la clave puede construirlo sin negociar nada.
void construirNonce(uint8_t nonce[16], uint32_t packet_id, uint32_t from);

// --- AES-CTR -----------------------------------------------------------------
// Cifrar y descifrar son la misma operacion. El contador ocupa los ULTIMOS 4
// bytes del nonce y se incrementa en big-endian, un paso por bloque de 16.
// key_len debe ser 16 o 32. Devuelve false si la clave no es valida.
bool ctrXcrypt(const uint8_t* key, size_t key_len, const uint8_t nonce[16],
               const uint8_t* entrada, uint8_t* salida, size_t len);

// --- funciones resumen -------------------------------------------------------

// XOR de todos los bytes. Channels.cpp::xorHash
uint8_t xorHash(const uint8_t* p, size_t n);

// Byte de canal de la cabecera: xorHash(nombre) ^ xorHash(clave).
// OJO: sobre la clave EXPANDIDA y con su longitud real (16 o 32). Pasar
// siempre 32 rompe la interoperabilidad con canales AES-128.
uint8_t hashCanal(const char* nombre, const uint8_t* key, size_t key_len);

// djb2 sobre el nombre del canal. RadioInterface.cpp::hash
uint32_t djb2(const char* s);

// Frecuencia central segun region y preset, tal y como la calcula
// RadioInterface::applyModemConfig. num_canales sale por referencia.
// Con LONG_FAST en EU_868 hay un unico hueco y siempre da 869,525 MHz, pero
// con LONG_MODERATE hay dos y la eleccion SI depende del nombre del canal.
float frecuenciaCanal(float freq_inicio, float freq_fin, float bw_khz,
                      const char* nombre_canal, int* num_canales);

// Ultimo byte de un NodeNum, con 0x00 sustituido por 0xFF porque el cero esta
// reservado para "sin asignar". NodeDB.h::getLastByteOfNodeNum
uint8_t ultimoByte(uint32_t node_num);

}  // namespace proto
