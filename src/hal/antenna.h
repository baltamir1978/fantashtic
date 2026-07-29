#pragma once
// Conmutador de antena RF del Cap LoRa-1262 (PI4IOE5V6408, bus I2C interno).
//
// SIN ESTO NO SALE POTENCIA. El SX1262 transmite contra una carga inexistente,
// que es la situacion que destruye el amplificador de forma permanente.
//
// El expansor arranca con P0 como ENTRADA y en ALTA IMPEDANCIA. Escribir solo
// el registro de salida no basta: hay que tocar tres registros en orden y
// releerlos para confirmar. Mientras no se confirme, el firmware NO transmite.

#include <stdint.h>

namespace hal {

enum class AntEstado : uint8_t {
  OK = 0,             // P0 verificado en alto: se puede transmitir
  SIN_CONFIGURAR,     // I2C_ADDR_ANTSW sigue en 0xFF: falta la Fase 1
  NO_RESPONDE,        // no hay nada en esa direccion I2C
  VERIFICACION_FALLIDA // escribimos, pero la relectura no cuadra
};

// Ejecuta la secuencia completa y verifica. Idempotente.
AntEstado antennaBegin();

// true solo si antennaBegin() devolvio OK. La radio consulta esto antes de
// cada transmision.
bool antennaOk();

AntEstado antennaEstado();
const char* antennaMensaje(AntEstado e);

// Volcado de los tres registros, para depurar en la consola.
void antennaDump();

}  // namespace hal
