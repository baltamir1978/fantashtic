#include "power.h"

#include <Arduino.h>
#include <M5Unified.h>

#include "config.h"

namespace hal {
namespace {

// Curva de descarga de una celda Li-ion, en mV -> %. Interpolamos entre
// puntos porque la tension NO es lineal con la carga: entre 3.7 V y 3.9 V esta
// la mitad de la capacidad util, y una recta da lecturas absurdas.
struct Punto { uint16_t mv; uint8_t pct; };
const Punto CURVA[] = {
    {4200, 100}, {4100, 92}, {4000, 83}, {3950, 75}, {3900, 66},
    {3850, 57},  {3800, 48}, {3750, 40}, {3700, 32}, {3650, 24},
    {3600, 16},  {3500,  9}, {3400,  4}, {3300,  1}, {3200,  0},
};

// Cache: la UI llama a batteryPct() y batteryLow() en cada redibujo, y este
// ocurre en cada tecla. Sin cache serian decenas de lecturas de ADC por
// pulsacion, y ademas la cabecera y el cuerpo podrian mostrar valores
// distintos por leer en instantes diferentes.
uint16_t g_mv    = 0;
uint8_t  g_pct   = 0;
uint32_t g_stamp = 0;
constexpr uint32_t CACHE_MS = 1000;

uint8_t mvAPct(uint16_t mv) {
  if (mv >= CURVA[0].mv) return 100;
  const size_t n = sizeof(CURVA) / sizeof(CURVA[0]);
  if (mv <= CURVA[n - 1].mv) return 0;
  for (size_t i = 1; i < n; i++) {
    if (mv > CURVA[i].mv) {
      const Punto& hi = CURVA[i - 1];
      const Punto& lo = CURVA[i];
      const int span = hi.mv - lo.mv;
      const int off  = mv - lo.mv;
      return (uint8_t)(lo.pct + (hi.pct - lo.pct) * off / span);
    }
  }
  return 0;
}

void refrescar(bool forzar) {
  const uint32_t now = millis();
  if (!forzar && g_stamp && (now - g_stamp) < CACHE_MS) return;

  // M5Unified ya configura el ADC1 de GPIO10 para esta placa (_adc_ratio 2.0,
  // pmic_adc) usando la calibracion del IDF. Leer por ahi en vez de tocar el
  // ADC por nuestra cuenta evita pisarle la configuracion.
  int32_t suma = 0;
  for (int i = 0; i < BAT_SAMPLES; i++) suma += M5.Power.getBatteryVoltage();
  g_mv    = (uint16_t)(suma / BAT_SAMPLES);
  g_pct   = mvAPct(g_mv);
  g_stamp = now;
}

}  // namespace

void powerBegin() { refrescar(true); }

uint16_t batteryMv()  { refrescar(false); return g_mv; }
uint8_t  batteryPct() { refrescar(false); return g_pct; }
bool     batteryLow() { return batteryPct() < BAT_LOW_PCT; }

// --- reloj del CPU -----------------------------------------------------------

// static: esto va despues del namespace anonimo de arriba, y sin el seria un
// simbolo global exportado desde una unidad de traduccion cualquiera.
// Se arranca a 240 MHz: es lo que deja el bootloader y lo que quiere setup().
static bool g_rapido = true;

void cpuRapido(bool rapido) {
  if (rapido == g_rapido) return;      // reprogramar el PLL no es gratis
  setCpuFrequencyMhz(rapido ? CPU_MHZ_RAPIDO : CPU_MHZ_LENTO);
  g_rapido = rapido;
}

bool cpuEsRapido() { return g_rapido; }

}  // namespace hal
