#include "power.h"

#include <Arduino.h>
#include <M5Unified.h>
#include <esp_sleep.h>

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

// --- light sleep -------------------------------------------------------------

static bool g_sleep_ok = false;

void sleepBegin() {
#ifndef FT_LIGHT_SLEEP
  // DESACTIVADO A PROPOSITO. Ver el bloque de autonomia en CLAUDE.md.
  //
  // Medido el 29-jul-2026: con el light sleep activo, el nodo se queda SORDO Y
  // MUDO en cuanto la pantalla se duerme. Dos evidencias independientes:
  //   - COM11, sin consola conectada, dejo de transmitir a los 30 s de arrancar
  //     y no contesto al NODEINFO de COM10.
  //   - COM10 dejo de imprimir en el instante exacto del primer sueño.
  // (Lo segundo por si solo no prueba nada: el light sleep tumba el
  //  USB-Serial-JTAG y perder la consola es lo esperado. Lo que no se explica
  //  con eso es el silencio de radio del otro nodo.)
  //
  // Hipotesis principal, sin confirmar: esp_sleep_enable_ext1_wakeup() pasa el
  // GPIO 4 (DIO1) al dominio RTC y eso rompe la interrupcion GPIO normal que
  // RadioLib registro con setDio1Action(). Sin esa ISR, g_irq nunca se levanta y
  // la maquina de estados de la radio se queda parada para siempre.
  //
  // En un aparato cuya razon de ser es recibir un SOS, esto no se embarca
  // encendido. Se compila con -DFT_LIGHT_SLEEP para retomarlo, y hay que
  // resolver la ISR de DIO1 antes de darlo por bueno.
  g_sleep_ok = false;
  return;
#else
  // Las tres lineas que tienen que poder despertarnos. Se arman una sola vez:
  // ext1 es una mascara de pines, no hay que rearmarla en cada dormida.
  //
  // DIO1 y el INT del teclado despiertan en ALTO; el boton G0 lleva pull-up y se
  // va a BAJO al pulsarlo, asi que ese NO puede ir en la misma mascara ext1 (un
  // solo nivel para todos los pines). El boton se detecta en el tick siguiente,
  // que con 50 ms de tope y 2 s de pulsacion larga llega de sobra.
  const uint64_t mascara = (1ULL << PIN_LORA_DIO1) | (1ULL << PIN_KB_INT);
  const esp_err_t e = esp_sleep_enable_ext1_wakeup(mascara, ESP_EXT1_WAKEUP_ANY_HIGH);
  if (e != ESP_OK) {
    Serial.printf("energia: ext1 fallo (%d), sin light sleep\n", (int)e);
    g_sleep_ok = false;
    return;
  }
  g_sleep_ok = true;
  Serial.printf("energia: light sleep listo (despiertan DIO1 %d, teclado %d)\n",
                PIN_LORA_DIO1, PIN_KB_INT);
#endif
}

bool sleepDisponible() { return g_sleep_ok; }

void dormirHasta(uint32_t ms) {
  if (!g_sleep_ok || ms == 0) { delay(ms); return; }

  // Vaciar la consola ANTES de dormir: el USB-Serial-JTAG se apaga con el CPU y
  // lo que quede en el buffer se perderia a medias.
  Serial.flush();

  esp_sleep_enable_timer_wakeup((uint64_t)ms * 1000ULL);
  esp_light_sleep_start();
}

}  // namespace hal
