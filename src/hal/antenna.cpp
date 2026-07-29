#include "antenna.h"

#include <Arduino.h>

#include "config.h"
#include "i2c_bus.h"

namespace hal {
namespace {

AntEstado g_estado = AntEstado::SIN_CONFIGURAR;

bool setBits(uint8_t reg, uint8_t mask, bool poner) {
  uint8_t v = 0;
  if (!i2cReadReg(I2C_ADDR_ANTSW, reg, &v)) return false;
  const uint8_t nuevo = poner ? (uint8_t)(v | mask) : (uint8_t)(v & ~mask);
  if (nuevo == v) return true;
  return i2cWriteReg(I2C_ADDR_ANTSW, reg, nuevo);
}

}  // namespace

AntEstado antennaBegin() {
  if (g_estado == AntEstado::OK) return g_estado;

  if (I2C_ADDR_ANTSW == I2C_ADDR_ANTSW_SIN_CONFIGURAR) {
    Serial.println(
        "SWITCH RF: I2C_ADDR_ANTSW sigue sin configurar.\n"
        "  Ejecuta la Fase 1, anota la direccion del PI4IOE5V6408 y ponla en\n"
        "  config.h. Hasta entonces NO se permite transmitir.");
    g_estado = AntEstado::SIN_CONFIGURAR;
    return g_estado;
  }

  uint8_t sonda = 0;
  if (!i2cReadReg(I2C_ADDR_ANTSW, PI4IOE_REG_DIR, &sonda)) {
    Serial.printf("SWITCH RF: nada responde en 0x%02X\n", I2C_ADDR_ANTSW);
    g_estado = AntEstado::NO_RESPONDE;
    return g_estado;
  }

  // ORDEN CRITICO: direccion -> VALOR -> liberar alta impedancia.
  //
  // El registro de salida vale 0 al arrancar. Si se liberase la alta
  // impedancia antes de escribir el valor, la linea de control del conmutador
  // quedaria forzada a nivel BAJO durante toda una transaccion I2C (~100 us a
  // 100 kHz) y despues saltaria a alto: justo el pulso que hay que evitar.
  const bool esc = setBits(PI4IOE_REG_DIR, PI4IOE_ANT_MASK, true) &&
                   setBits(PI4IOE_REG_OUT, PI4IOE_ANT_MASK, true) &&
                   setBits(PI4IOE_REG_HIZ, PI4IOE_ANT_MASK, false);
  if (!esc) {
    Serial.println("SWITCH RF: fallo al escribir los registros");
    g_estado = AntEstado::VERIFICACION_FALLIDA;
    return g_estado;
  }

  delay(2);

  // Relectura: es la unica prueba de que el switch esta realmente activo.
  uint8_t dir = 0, hiz = 0, out = 0;
  if (!i2cReadReg(I2C_ADDR_ANTSW, PI4IOE_REG_DIR, &dir) ||
      !i2cReadReg(I2C_ADDR_ANTSW, PI4IOE_REG_HIZ, &hiz) ||
      !i2cReadReg(I2C_ADDR_ANTSW, PI4IOE_REG_OUT, &out)) {
    g_estado = AntEstado::VERIFICACION_FALLIDA;
    return g_estado;
  }

  const bool ok = (dir & PI4IOE_ANT_MASK) &&
                  !(hiz & PI4IOE_ANT_MASK) &&
                  (out & PI4IOE_ANT_MASK);

  Serial.printf("SWITCH RF @0x%02X: dir=0x%02X hiz=0x%02X out=0x%02X -> %s\n",
                I2C_ADDR_ANTSW, dir, hiz, out, ok ? "ACTIVO" : "NO ACTIVO");

  g_estado = ok ? AntEstado::OK : AntEstado::VERIFICACION_FALLIDA;
  return g_estado;
}

bool        antennaOk()     { return g_estado == AntEstado::OK; }
AntEstado   antennaEstado() { return g_estado; }

const char* antennaMensaje(AntEstado e) {
  switch (e) {
    case AntEstado::OK:                   return "switch RF activo";
    case AntEstado::SIN_CONFIGURAR:       return "direccion I2C sin configurar";
    case AntEstado::NO_RESPONDE:          return "el expansor no responde";
    case AntEstado::VERIFICACION_FALLIDA: return "la relectura no cuadra";
    default:                              return "?";
  }
}

void antennaDump() {
  if (I2C_ADDR_ANTSW == I2C_ADDR_ANTSW_SIN_CONFIGURAR) return;
  uint8_t v = 0;
  const uint8_t regs[] = {0x00, 0x01, PI4IOE_REG_DIR, PI4IOE_REG_OUT, PI4IOE_REG_HIZ};
  Serial.printf("SWITCH RF @0x%02X registros:", I2C_ADDR_ANTSW);
  for (uint8_t r : regs)
    Serial.printf("  0x%02X=%s", r, i2cReadReg(I2C_ADDR_ANTSW, r, &v)
                      ? String(v, HEX).c_str() : "??");
  Serial.println();
}

}  // namespace hal
