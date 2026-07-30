#include "power.h"

#include <Arduino.h>
#include <M5Unified.h>
#include <driver/gpio.h>
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

static bool       g_sleep_ok = false;
static SleepStats g_stats;

bool usbConectado() {
#if defined(ARDUINO_USB_CDC_ON_BOOT) && ARDUINO_USB_CDC_ON_BOOT && \
    defined(ARDUINO_USB_MODE) && ARDUINO_USB_MODE
  // isPlugged() lo mantiene un hook del tick de FreeRTOS que vigila los SOF del
  // USB: un host los manda cada milisegundo, un cargador tonto no manda ninguno.
  // Arranca en true, asi que en el primer arranque no se duerme hasta que el hook
  // confirme que no hay nadie.
  //
  // CON HISTERESIS, y esto NO es adorno. Los SOF paran en cuanto el host suspende
  // el puerto, y Windows lo suspende cuando ningun programa lo tiene abierto. Sin
  // histeresis, un hueco de 5 ms basta para que el nodo se duerma con el cable
  // puesto. Se recuerda el ultimo SOF visto y se da por presente el ordenador
  // durante USB_HISTERESIS_MS mas.
  //
  // LO QUE ESTO NO ARREGLA, y hay que saberlo: si el puerto queda suspendido un
  // rato largo, el nodo se duerme igual y a partir de ahi esptool ya no lo alcanza
  // ("No serial data received"). No es un ladrillo: el bootloader del ESP32-S3
  // esta en ROM y se recupera con interruptor OFF -> mantener G0 -> dar corriente
  // -> soltar. Es el precio acordado de dejar el light sleep siempre activo.
  static uint32_t visto = 0;
  if (Serial.isPlugged()) { visto = millis(); return true; }
  return visto && (millis() - visto) < USB_HISTERESIS_MS;
#else
  return false;
#endif
}

void sleepBegin() {
#ifndef FT_LIGHT_SLEEP
  // DESACTIVADO. Medido en el aparato el 30-jul-2026, y esta vez con gpio_wakeup
  // en vez de ext1: el nodo desenchufado SIGUE quedandose tonto a los 30 s, o sea
  // en cuanto se duerme la pantalla y empieza a dormir. Y ademas el altavoz
  // CREPITA al entrar y salir del sueño.
  //
  // O sea que el arreglo del mux del GPIO era necesario pero NO suficiente: queda
  // al menos otra causa sin encontrar. No se embarca en un aparato cuya razon de
  // ser es recibir un SOS. El codigo y la investigacion se quedan detras de
  // -DFT_LIGHT_SLEEP; ver ESTADO.md para lo que ya esta descartado.
  g_sleep_ok = false;
  Serial.println("energia: light sleep DESACTIVADO (ver ESTADO.md)");
  return;
#else
  // El INT del teclado es de colector abierto: sin pull-up flota y una linea que
  // flota baja despierta sin parar, o sea light sleep que vuelve al instante.
  pinMode(PIN_KB_INT, INPUT_PULLUP);

  // Nada mas que armar aqui. A diferencia de ext1, las fuentes de gpio_wakeup se
  // arman en CADA dormida: gpio_wakeup_enable() cambia el tipo de interrupcion
  // del pin a "por nivel", y eso pisa el flanco de subida que RadioLib pidio con
  // setDio1Action(). Dejarlo puesto entre sueños tendria dos consecuencias malas:
  // una interrupcion por nivel se repite mientras la linea siga alta (tormenta de
  // ISR hasta que el bucle limpie la IRQ por SPI), y ademas cambiaria la semantica
  // de la que depende la radio. Se pone justo antes de dormir y se quita justo
  // despues de despertar.
  g_sleep_ok = true;
  Serial.printf("energia: light sleep listo (DIO1 %d alto, teclado %d bajo, boton %d bajo)\n",
                PIN_LORA_DIO1, PIN_KB_INT, PIN_BTN);
#endif
}

bool sleepDisponible() { return g_sleep_ok; }

const SleepStats& sleepStats() { return g_stats; }
void sleepContarNoGps() { g_stats.no_gps++; }

uint32_t dormirHasta(uint32_t ms) {
  if (!g_sleep_ok || ms == 0) { delay(ms); return 0; }

  // 1. Con un ordenador en el USB no se duerme: ver usbConectado().
  if (usbConectado()) { g_stats.no_usb++; delay(ms); return 0; }

  // 2. Si DIO1 ya esta alto, la radio tiene un aviso sin atender. Dormir aqui
  //    seria dormir con trabajo pendiente; y ademas el despertador por nivel
  //    saltaria de inmediato, asi que no se ahorraria nada.
  if (digitalRead(PIN_LORA_DIO1)) { g_stats.no_dio1++; delay(ms); return 0; }

  // 3. Armar las fuentes. Un nivel POR PIN, que es lo que ext1 no permitia.
  //
  //    Se arma solo lo que este en reposo: una linea ya activa despertaria en el
  //    acto y el light sleep se volveria un no-op silencioso que gasta mas que el
  //    delay(). El caso real es el INT del teclado, que se queda bajo hasta que
  //    alguien vacie la cola de eventos del TCA8418.
  gpio_wakeup_enable((gpio_num_t)PIN_LORA_DIO1, GPIO_INTR_HIGH_LEVEL);
  const bool kb  = digitalRead(PIN_KB_INT);   // en reposo esta ALTO (open drain)
  const bool btn = digitalRead(PIN_BTN);      // en reposo esta ALTO (pull-up)
  if (kb)  gpio_wakeup_enable((gpio_num_t)PIN_KB_INT, GPIO_INTR_LOW_LEVEL);
  if (btn) gpio_wakeup_enable((gpio_num_t)PIN_BTN,    GPIO_INTR_LOW_LEVEL);

  // 4. Callar la ISR de DIO1 mientras dormimos. El despertar NO la necesita (lo
  //    dispara el bit de wakeup del pin, no que la interrupcion llegue al CPU),
  //    y asi se evita de raiz la tormenta de ISR por nivel al despertar con la
  //    linea alta. No se pierde el aviso: radioLoop() rearma por nivel leyendo
  //    DIO1 en cada vuelta, que es exactamente para lo que estaba puesto.
  gpio_intr_disable((gpio_num_t)PIN_LORA_DIO1);

  esp_sleep_enable_gpio_wakeup();
  esp_sleep_enable_timer_wakeup((uint64_t)ms * 1000ULL);

  // Vaciar la consola ANTES de dormir: el USB-Serial-JTAG se apaga con el CPU y
  // lo que quede en el buffer se perderia a medias. (Con el cable puesto no se
  // llega hasta aqui, pero un cargador tonto tambien deja el USB alimentado.)
  Serial.flush();

  const uint32_t t0 = millis();
  esp_light_sleep_start();
  const uint32_t dormido = millis() - t0;

  // 5. Deshacerlo TODO, y lo primero devolver DIO1 al flanco de subida: hasta que
  //    eso ocurra, el pin esta configurado por nivel y la semantica de RadioLib
  //    no se cumple. gpio_wakeup_disable() deja el tipo en DISABLE, asi que hay
  //    que volver a ponerlo a mano; POSEDGE es lo que significa el RISING que usa
  //    Module::setDio1Action().
  gpio_wakeup_disable((gpio_num_t)PIN_LORA_DIO1);
  gpio_set_intr_type((gpio_num_t)PIN_LORA_DIO1, GPIO_INTR_POSEDGE);
  gpio_intr_enable((gpio_num_t)PIN_LORA_DIO1);
  if (kb)  gpio_wakeup_disable((gpio_num_t)PIN_KB_INT);
  if (btn) gpio_wakeup_disable((gpio_num_t)PIN_BTN);
  esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_GPIO);

  g_stats.veces++;
  g_stats.ms_dormido += dormido;
  if (esp_sleep_get_wakeup_cause() == ESP_SLEEP_WAKEUP_GPIO) g_stats.desp_gpio++;
  else                                                       g_stats.desp_timer++;
  return dormido;
}

}  // namespace hal
