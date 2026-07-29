#pragma once
// Bus I2C interno (SDA 8 / SCL 9) y escaner de dispositivos.
//
// En este bus viven: TCA8418 (teclado), BMI270 (IMU), ES8311 (audio) y el
// PI4IOE5V6408 del Cap LoRa-1262, cuya direccion NO esta documentada por
// M5Stack. Encontrarla es el objetivo principal de la Fase 1.

#include <stdint.h>
#include <stddef.h>

namespace hal {

// Direcciones conocidas, para poder etiquetar el resultado del escaneo.
struct I2cKnown {
  uint8_t     addr;
  const char* nombre;
};

constexpr size_t I2C_MAX_FOUND = 16;

struct I2cScan {
  uint8_t addrs[I2C_MAX_FOUND];
  size_t  count;
  bool    overflow;      // habia mas dispositivos que huecos
};

// Inicializa Wire en los pines internos. Idempotente.
void i2cBegin();

// Recorre 0x08..0x77. Devuelve las direcciones que respondieron.
I2cScan i2cScan();

// Nombre conocido de una direccion, o nullptr si no la reconocemos.
const char* i2cNombre(uint8_t addr);

// Imprime el resultado por Serial en formato legible, marcando los candidatos
// a expansor de I/O (los que no reconocemos).
void i2cScanReport(const I2cScan& s);

// Escanea LOS DOS buses (interno SDA 8/SCL 9 y externo Grove SDA 2/SCL 1) e
// imprime puerto y pines reales de cada uno.
//
// Existe porque con el Cap LoRa-1262 montado el expansor no aparecio en el bus
// interno, y hay que distinguir "el Cap esta en el otro bus" de "el Cap no
// responde". Toca solo GPIO 2/1 a traves de M5.Ex_I2C, asi que no remultiplexa
// el bus interno: ese es el fallo que dejaria mudo el teclado.
void i2cDiagnostico();

// Lectura y escritura de un registro de 8 bits. Devuelven false si el
// dispositivo no responde. Se usaran en la Fase 2 con el PI4IOE5V6408.
bool i2cReadReg(uint8_t addr, uint8_t reg, uint8_t* out);
bool i2cWriteReg(uint8_t addr, uint8_t reg, uint8_t val);

}  // namespace hal
