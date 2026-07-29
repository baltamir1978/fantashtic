#include "i2c_bus.h"

#include <Arduino.h>
#include <M5Unified.h>
#include <Wire.h>     // solo para el contraste de i2cDiagnostico(); ver alli

#include "config.h"

// NO USAR Wire AQUI.
//
// En el Cardputer Adv, M5Unified instala el bus interno (SDA 8 / SCL 9) en
// I2C_NUM_1, mientras que el objeto Wire de Arduino es I2C_NUM_0. Llamar a
// Wire.begin(8, 9, ...) ejecuta i2c_param_config, que remultiplexa la SALIDA
// de esos GPIO hacia I2C0 -- y un GPIO solo tiene una fuente de salida. A
// partir de ese momento I2C1 ya no puede excitar SDA/SCL y se quedan mudos el
// TCA8418 (teclado), el BMI270 y el ES8311.
//
// Es decir: escanear el bus con Wire mataria el teclado. Usamos el bus que
// M5Unified ya gestiona.

namespace hal {
namespace {

constexpr uint32_t I2C_FREQ = 100000;

// Direcciones documentadas. Verificadas contra las tablas de M5Unified:
// es8311_i2c_addr0 = 0x18, addr1 = 0x19; el BMI270 va en 0x68/0x69.
const I2cKnown KNOWN[] = {
    {0x18, "ES8311 (audio)"},
    {0x19, "ES8311 (audio, alt)"},
    {0x34, "TCA8418 (teclado)"},
    {0x43, "PI4IO (expansor) - candidato al switch RF"},
    {0x68, "BMI270 (IMU)"},
    {0x69, "BMI270 (IMU, alt)"},
};

}  // namespace

void i2cBegin() {
  // M5.begin() ya deja el bus interno listo. Esta funcion existe para que la
  // capa de arriba no dependa de ese detalle.
}

const char* i2cNombre(uint8_t addr) {
  for (const auto& k : KNOWN)
    if (k.addr == addr) return k.nombre;
  return nullptr;
}

I2cScan i2cScan() {
  I2cScan s{};
  bool encontrado[120] = {};
  M5.In_I2C.scanID(encontrado);

  for (uint8_t a = 0x08; a <= 0x77; a++) {
    if (!encontrado[a]) continue;
    if (s.count < I2C_MAX_FOUND) s.addrs[s.count++] = a;
    else                         s.overflow = true;
  }
  return s;
}

void i2cScanReport(const I2cScan& s) {
  Serial.printf("\n--- escaneo I2C interno (SDA %d / SCL %d): %u dispositivos ---\n",
                PIN_I2C_SDA, PIN_I2C_SCL, (unsigned)s.count);
  if (s.count == 0) {
    Serial.println("  NADA. Sin teclado el aparato no sirve: revisa el bus.");
    return;
  }
  for (size_t i = 0; i < s.count; i++) {
    const char* n = i2cNombre(s.addrs[i]);
    Serial.printf("  0x%02X  %s\n", s.addrs[i],
                  n ? n : "DESCONOCIDO  <-- candidato a PI4IOE5V6408 (switch antena RF)");
  }
  if (s.overflow) Serial.println("  (habia mas: amplia I2C_MAX_FOUND)");
  Serial.println(
      "\n  Anota la direccion del PI4IOE5V6408 en CLAUDE.md y ponla en\n"
      "  config.h -> I2C_ADDR_ANTSW antes de empezar la Fase 2.\n"
      "  Metodo fiable: escanear CON el Cap montado y SIN el; la direccion\n"
      "  que solo aparece con el Cap es la buena.");
}

void i2cDiagnostico() {
  // M5Unified solo llama a Ex_I2C.begin() si se ha pedido RTC o IMU externos,
  // asi que aqui hay que arrancarlo a mano antes de escanearlo.
  M5.Ex_I2C.begin();

  struct { const char* nombre; m5::I2C_Class* bus; } buses[] = {
      {"interno (SDA 8 / SCL 9)",   &M5.In_I2C},
      {"externo (Grove/EXT, 2 / 1)", &M5.Ex_I2C},
  };

  for (const auto& b : buses) {
    Serial.printf("\n--- bus %s: puerto %d, SDA %d, SCL %d, %s ---\n",
                  b.nombre, (int)b.bus->getPort(), (int)b.bus->getSDA(),
                  (int)b.bus->getSCL(), b.bus->isEnabled() ? "activo" : "DESACTIVADO");
    if (!b.bus->isEnabled()) continue;

    // scanID rellena los indices 8..119: 120 bytes, como pide la cabecera.
    bool encontrado[120] = {};
    b.bus->scanID(encontrado, I2C_FREQ);

    size_t n = 0;
    for (uint8_t a = 0x08; a <= 0x77; a++) {
      if (!encontrado[a]) continue;
      n++;
      const char* k = i2cNombre(a);
      Serial.printf("  0x%02X  %s\n", a,
                    k ? k : "DESCONOCIDO  <-- candidato a PI4IOE5V6408");
    }
    if (n == 0) Serial.println("  (nada responde en este bus)");
  }

  // Sondeo dirigido del expansor del Cap, EN LOS DOS BUSES.
  //
  // El escaneo general va a 100 kHz y solo comprueba el ACK de la direccion; el
  // ejemplo oficial de M5 usa 400 kHz y valida leyendo el registro de
  // identificacion 0x01, que es lo que hace PI4IOE5V6408_Class::begin() (si
  // devuelve 0, lo da por ausente).
  //
  // Hay que sondear tambien el externo: en el pinout del Cap, SCL es el pin 3 y
  // SDA el pin 4 (consecutivos), y variant.h de Meshtastic declara
  // I2C_SCL1 = 1 / I2C_SDA1 = 2, tambien consecutivos. O sea que el expansor
  // podria estar en GPIO 2/1 y no en el bus interno, al reves de lo que da por
  // supuesto CLAUDE.md.
  Serial.println("--- sondeo dirigido del PI4IOE5V6408 ---");
  for (const auto& b : buses) {
    if (!b.bus->isEnabled()) continue;
    for (uint8_t addr : {(uint8_t)0x43, (uint8_t)0x44}) {
      for (uint32_t freq : {100000u, 400000u}) {
        uint8_t id = 0;
        const bool ok = b.bus->readRegister(addr, 0x01, &id, 1, freq);
        Serial.printf("  %-28s 0x%02X a %3lu kHz: %s", b.nombre, addr,
                      (unsigned long)(freq / 1000), ok ? "RESPONDE" : "sin ACK");
        if (ok) Serial.printf(", reg 0x01 (ID) = 0x%02X%s", id,
                              id == 0 ? "  <-- ID 0: begin() lo daria por ausente" : "");
        Serial.println();
      }
    }
  }
  // ---------------------------------------------------------------------
  // Contraste con el driver de Arduino, replicando lo que hace MeshCore.
  //
  // MeshCore habla con el expansor asi: Wire1.begin(8, 9, 100000) y luego
  // beginTransmission(0x43) (CardputerAdvBoard.h + globals.h de
  // MultiMote/meshcore-cardputer-adv). Son EXACTAMENTE el mismo puerto, los
  // mismos pines y la misma velocidad que usa M5.In_I2C, y a el si le contesta.
  // La unica variable que queda es el driver: M5Unified va por
  // m5gfx::i2c::init(), no por el Wire de Arduino.
  //
  // Wire1 es I2C_NUM_1, el MISMO puerto que M5Unified: no se remultiplexan
  // pines hacia otro periferico, que es el fallo que avisa la restriccion 7
  // (ese lo provoca Wire, que es I2C_NUM_0). Aun asi es intrusivo, asi que
  // despues se vuelve a escanear con M5.In_I2C: si el TCA8418 desapareciera,
  // se veria aqui mismo y habria que quitar esta prueba.
  Serial.println("--- contraste con Wire1 de Arduino (como MeshCore) ---");
  Wire1.begin(PIN_I2C_SDA, PIN_I2C_SCL, 100000);
  for (uint8_t addr : {(uint8_t)0x43, (uint8_t)0x44}) {
    Wire1.beginTransmission(addr);
    const uint8_t err = Wire1.endTransmission();
    Serial.printf("  Wire1 0x%02X: %s (endTransmission=%u)\n", addr,
                  err == 0 ? "RESPONDE" : "sin ACK", (unsigned)err);
  }
  // Y de paso, quien contesta a Wire1 en todo el bus.
  Serial.print("  Wire1 ve:");
  for (uint8_t a = 0x08; a <= 0x77; a++) {
    Wire1.beginTransmission(a);
    if (Wire1.endTransmission() == 0) Serial.printf(" 0x%02X", a);
  }
  Serial.println();

  // Control de danos: el teclado tiene que seguir estando.
  bool tras[120] = {};
  M5.In_I2C.scanID(tras, I2C_FREQ);
  Serial.printf("  despues, M5.In_I2C sigue viendo el TCA8418 (0x34): %s\n",
                tras[0x34] ? "SI" : "NO  <-- quitar esta prueba");
  Serial.println();
}

bool i2cReadReg(uint8_t addr, uint8_t reg, uint8_t* out) {
  return M5.In_I2C.readRegister(addr, reg, out, 1, I2C_FREQ);
}

bool i2cWriteReg(uint8_t addr, uint8_t reg, uint8_t val) {
  return M5.In_I2C.writeRegister8(addr, reg, val, I2C_FREQ);
}

}  // namespace hal
