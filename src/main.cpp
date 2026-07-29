// Fantashtic - firmware completo.
//
// Mensajeria LoRa privada para un grupo reducido, sin movil ni infraestructura.
// Cuatro pantallas (chat, nodos, brujula, ajustes), relevo automatico entre
// nodos, boton de emergencia e historial en microSD.
//
// SEGURIDAD
//   - La radio no se inicia si el conmutador de antena RF no se verifica por
//     relectura de sus registros.
//   - No se transmite nada hasta que el usuario confirma en pantalla que la
//     antena esta atornillada. Transmitir sin antena destruye el SX1262.
//   - El limite del 10 % de ciclo de trabajo no es desactivable.

#include <M5Cardputer.h>

#include "app/app.h"
#include "config.h"
#include "hal/antenna.h"
#include "hal/gps.h"
#include "hal/i2c_bus.h"
#include "hal/keys.h"
#include "hal/led.h"
#include "hal/power.h"
#include "hal/sound.h"
#include "hal/storage.h"
#include "radio/radio.h"
#include "store/store.h"
#include "strings.h"
#include "ui/ui.h"

#define D M5Cardputer.Display

namespace {

uint32_t g_nodo = 0;
bool     g_armado = false;          // el usuario confirmo que hay antena
bool     g_operativo = false;       // HAL y radio listos
uint32_t g_boton_desde = 0;

hal::AntEstado g_ant  = hal::AntEstado::SIN_CONFIGURAR;
radio::Init    g_init = radio::Init::SIN_SWITCH_ANTENA;

void pantallaAntena() {
  D.startWrite();
  D.fillScreen(0x4000);
  D.setTextDatum(middle_center);
  D.setTextColor(TFT_WHITE);
  D.setTextSize(2);
  D.drawString("ANTENA", TFT_W / 2, 28);
  D.setTextSize(1);
  D.drawString("Comprueba que la antena esta", TFT_W / 2, 58);
  D.drawString("atornillada ANTES de transmitir.", TFT_W / 2, 72);
  D.setTextColor(TFT_YELLOW);
  D.drawString("Sin antena se rompe la radio.", TFT_W / 2, 92);
  D.setTextColor(TFT_WHITE);
  D.drawString("ENTER = esta puesta", TFT_W / 2, 116);
  D.setTextDatum(top_left);
  D.endWrite();
}

void pantallaFallo() {
  D.startWrite();
  D.fillScreen(TFT_BLACK);
  D.setTextColor(TFT_RED);
  D.setTextSize(2);
  D.drawString(S_ERR_RADIO, 6, 16);
  D.setTextSize(1);
  D.setTextColor(TFT_WHITE);
  D.drawString(hal::antennaMensaje(g_ant), 6, 48);
  D.drawString(radio::initMensaje(g_init), 6, 62);
  if (g_ant == hal::AntEstado::SIN_CONFIGURAR) {
    D.setTextColor(TFT_YELLOW);
    D.drawString("Ejecuta la Fase 1, anota la", 6, 86);
    D.drawString("direccion del PI4IOE y ponla en", 6, 98);
    D.drawString("config.h -> I2C_ADDR_ANTSW", 6, 110);
  }
  D.endWrite();
}

// Boton G0: pulsacion larga = SOS, corta = silenciar o despertar.
//
// BtnA es GPIO 0, o sea el boton del propio modulo StampS3A -- el mismo del modo
// descarga. NO hay boton dedicado en el chasis: M5Unified.cpp agrupa
// board_M5CardputerADV con board_M5StampS3 y lee gpio_in(GPIO_NUM_0).
//
// PENDIENTE: es un disparador malo para una emergencia. Mantenerlo pulsado
// MIENTRAS se enciende el aparato mete al ESP32-S3 en modo descarga y la
// pantalla se queda muerta, que es justo la ruta facil a un estado roto que la
// restriccion nº 8 prohibe. Hace falta una alternativa desde el teclado.
void atenderBoton() {
  if (M5Cardputer.BtnA.isPressed()) {
    if (!g_boton_desde) g_boton_desde = millis();
    else if (millis() - g_boton_desde >= SOS_HOLD_MS &&
             app::sosEstado() == app::Sos::INACTIVO) {
      app::sosArmar();
      hal::beep();
      g_boton_desde = 0;
    }
    return;
  }

  if (g_boton_desde) {
    const uint32_t dur = millis() - g_boton_desde;
    g_boton_desde = 0;
    if (dur < SOS_HOLD_MS) {
      const auto s = app::sosEstado();
      // Una pulsacion corta cancela CUALQUIER estado de SOS, incluido uno
      // propio ya en marcha: sin esto, un disparo accidental transmitiria
      // durante una hora sin forma de pararlo salvo reiniciando.
      if (s != app::Sos::INACTIVO) { app::sosCancelar(); hal::beepError(); }
      else                          ui::despertar();
    }
  }
}

// --- autonomia ---------------------------------------------------------------
//
// Dos medidas, las dos sin riesgo funcional. La grande que falta es el LIGHT
// SLEEP, y no cabe aqui: ver el bloque de deuda en CLAUDE.md.
//
// Hace falta reaccionar rapido en dos situaciones y solo en dos: alguien esta
// mirando la pantalla, o hay un SOS en marcha. El resto del tiempo el aparato
// solo escucha, y para eso 80 MHz y un tick de 50 ms sobran.
bool haciendoFalta() {
  return ui::pantallaEncendida() || app::sosEstado() != app::Sos::INACTIVO;
}

void ajustarReloj() {
  // Solo con la radio ociosa: setCpuFrequencyMhz() reprograma el PLL y detiene
  // el CPU un instante, y eso en medio de una transaccion SPI con el SX1262 no
  // es buena idea. Si la radio esta ocupada, se ajusta en el tick siguiente.
  if (!radio::radioOciosa()) return;
  hal::cpuRapido(haciendoFalta());
}

uint32_t tickMs() {
  if (haciendoFalta())        return TICK_MS;
  if (!radio::radioOciosa())  return TICK_MS;   // hay algo en vuelo
  return TICK_IDLE_MS;
}

// Informe periodico de energia. Sin esto, bajar el reloj es un cambio invisible
// que hay que creerse: asi se ve en la consola pasar a 80 MHz y a 50 ms de tick
// cuando la pantalla se duerme, y volver a 240/10 al tocar una tecla.
void informeEnergia() {
  static uint32_t ultimo = 0;
  static uint32_t mhz_anterior = 0;
  if (millis() - ultimo < 5000) return;
  ultimo = millis();

  const uint32_t mhz = getCpuFrequencyMhz();
  Serial.printf("energia: CPU %lu MHz  tick %lu ms  pantalla %s  bateria %u%% (%u mV)%s\n",
                (unsigned long)mhz, (unsigned long)tickMs(),
                ui::pantallaEncendida() ? "on" : "off",
                (unsigned)hal::batteryPct(), (unsigned)hal::batteryMv(),
                mhz != mhz_anterior && mhz_anterior ? "   <-- cambio de reloj" : "");
  mhz_anterior = mhz;
}

// Volcado de la tabla de nodos: es EXACTAMENTE lo que pinta la pantalla 2, asi
// que si ahi sale "no se ve a nadie" o un numero en vez de un nombre, aqui se ve
// si el problema es la tabla o el dibujado.
void informeNodos() {
  static uint32_t ultimo = 0;
  if (millis() - ultimo < 10000) return;
  ultimo = millis();

  const auto& t = app::nodos();
  Serial.printf("nodos: total %u", (unsigned)t.total());
  for (size_t i = 0; i < t.capacidad(); i++) {
    const auto* n = t.porIndice(i);
    if (!n) continue;
    char nom[8];
    t.nombreMostrar(*n, nom, sizeof(nom));
    Serial.printf("  | %08X '%s'%s hace %lus", (unsigned)n->num, nom,
                  n->corto[0] ? "" : " SIN NOMBRE",
                  (unsigned long)((millis() - n->visto_ms) / 1000));
  }
  Serial.println();
}

// Informe periodico del GPS por consola.
//
// En interior NO va a haber fix, pero los bytes NMEA deben llegar igual: es lo
// que distingue "cableado bien, sin cobertura" de "RX/TX del UART al reves".
// La documentacion de M5 se contradice consigo misma en ese punto (la tabla del
// bus EXT dice lo contrario que la pagina del Cap), asi que hay que medirlo.
//   bytes > 0  -> el UART esta bien; si no hay fix, es cuestion de cielo.
//   bytes == 0 -> intercambiar PIN_GPS_RX y PIN_GPS_TX en config.h.
void informeGps() {
  if (!store::config().gps_activo) return;

  static uint32_t ultimo = 0;
  if (millis() - ultimo < 5000) return;
  ultimo = millis();

  const auto& f = hal::gpsFix();
  Serial.printf("GPS: %lu bytes NMEA, %u sat, %s",
                (unsigned long)hal::gpsBytesRecibidos(),
                (unsigned)f.satelites,
                f.valido ? "CON FIX" : "sin fix");
  if (f.valido) {
    Serial.printf(" %.5f,%.5f  alt %ld m  hdop %.1f",
                  (double)f.lat_e7 * 1e-7, (double)f.lon_e7 * 1e-7,
                  (long)f.altitud_m, (double)f.hdop);
  } else if (hal::gpsBytesRecibidos() == 0) {
    Serial.printf("  <-- CERO bytes: revisa el UART (probar PIN_GPS_RX/TX al reves)");
  }
  Serial.println();
}

void avisosDeEstado() {
  static uint32_t ultimo = 0;
  if (millis() - ultimo < 5000) return;
  ultimo = millis();

  const uint32_t esperar = radio::dutyMinutosParaLiberar();
  if (esperar) {
    char b[40];
    snprintf(b, sizeof(b), "%s - %lu %s", S_ERR_SATURADA,
             (unsigned long)esperar, S_ERR_MIN);
    ui::ponerAviso(b);
  } else if (hal::batteryLow()) {
    ui::ponerAviso(S_ERR_BATERIA);
  } else if (store::config().gps_activo &&
             hal::gpsSegundosSinFix() > GPS_NOFIX_WARN_MS / 1000) {
    ui::ponerAviso(hal::gpsBytesRecibidos() == 0 ? "GPS sin datos (UART?)" : S_ERR_GPS);
  } else if (!store::hayTarjeta()) {
    ui::ponerAviso(S_ERR_SD);
  // NO poner aqui un aviso por el conmutador de antena. Se probo y se quito:
  // en este Cap el expansor NUNCA responde (medido en dos unidades), asi que el
  // aviso salia rojo y permanente señalando una averia inexistente. Ademas
  // robaba 11 px fijos y desplazaba la linea de entrada del chat. Con usuarios
  // niños, una alarma perpetua que no significa nada enseña a ignorar los
  // avisos. El enlace esta medido a -18 dBm: el camino de RF va bien por DIO2.
  // Queda el mensaje por consola de radioBegin(), que es donde importa.
  } else {
    ui::quitarAviso();
  }
}

}  // namespace

void setup() {
  auto cfg = M5.config();
  M5Cardputer.begin(cfg, true);

  D.setRotation(1);
  D.setFont(&fonts::Font0);
  D.fillScreen(TFT_BLACK);
  D.setTextDatum(middle_center);
  D.setTextColor(TFT_WHITE);
  D.setTextSize(2);
  D.drawString(FT_NOMBRE, TFT_W / 2, TFT_H / 2 - 8);
  D.setTextSize(1);
  D.setTextColor(TFT_DARKGREY);
  D.drawString("v" FT_VERSION, TFT_W / 2, TFT_H / 2 + 16);
  D.setTextDatum(top_left);

  Serial.begin(115200);
  delay(200);
  Serial.printf("\n=== %s v%s ===\n", FT_NOMBRE, FT_VERSION);

  // Identificador de nodo: ultimos 4 bytes de la MAC, igual que Meshtastic.
  uint8_t mac[6] = {0};
  esp_read_mac(mac, ESP_MAC_WIFI_STA);
  g_nodo = ((uint32_t)mac[2] << 24) | ((uint32_t)mac[3] << 16) |
           ((uint32_t)mac[4] << 8)  |  (uint32_t)mac[5];
  randomSeed(esp_random());

  hal::powerBegin();
  hal::soundBegin();
  hal::ledBegin();
  hal::keysBegin();
  hal::sdBegin();
  hal::sdEnsureDirs();

  store::begin();
  // Nombre por defecto en el primer arranque, antes del asistente.
  if (!store::config().nombre_corto[0]) {
    auto& c = store::configMutable();
    snprintf(c.nombre_largo, sizeof(c.nombre_largo), "Nodo %04X",
             (unsigned)(g_nodo & 0xFFFF));
    snprintf(c.nombre_corto, sizeof(c.nombre_corto), "%04X",
             (unsigned)(g_nodo & 0xFFFF));
  }
  hal::soundVolumen(store::config().volumen);

  const auto scan = hal::i2cScan();
  hal::i2cScanReport(scan);
  hal::i2cDiagnostico();   // los dos buses: el Cap podria no estar en el interno

  g_ant  = hal::antennaBegin();
  g_init = radio::radioBegin();
  g_operativo = (g_init == radio::Init::OK);

  if (store::config().gps_activo) hal::gpsBegin();

  app::begin(g_nodo);
  ui::begin();

  Serial.printf("nodo %08X  antena: %s  radio: %s  tarjeta: %s\n",
                (unsigned)g_nodo, hal::antennaMensaje(g_ant),
                radio::initMensaje(g_init),
                store::hayTarjeta() ? "si" : "no");

  hal::ledSet(0, 0, 40);
  delay(600);

  // La confirmacion de antena se pide UNA VEZ y se recuerda en NVS. El Cap va
  // atornillado de forma permanente: repetir la pregunta en cada arranque no
  // añade seguridad y acaba pulsandose sin leer. Sigue apareciendo en el primer
  // arranque y despues de restablecer de fabrica, que es cuando importa.
  if (g_operativo && store::config().antena_ok) {
    g_armado = true;
    radio::radioPermitirTx(true);
  }

  if (!g_operativo)     { hal::beepError(); pantallaFallo(); }
  else if (!g_armado)   { pantallaAntena(); }
}

void loop() {
  M5Cardputer.update();
  hal::ledUpdate();
  hal::soundUpdate();

  if (!g_operativo) { delay(TICK_MS); return; }

  // Pantalla de armado: sin confirmar la antena no se transmite nada.
  if (!g_armado) {
    hal::Evento ev;
    if (hal::keysPoll(&ev) && ev.tecla == hal::Tecla::ENTER) {
      g_armado = true;
      radio::radioPermitirTx(true);
      store::configMutable().antena_ok = true;   // no volver a preguntarlo
      store::guardar();
      hal::beep();
      ui::despertar();
      ui::pintar();
    }
    delay(TICK_MS);
    return;
  }

  radio::radioLoop();
  hal::gpsLoop();
  app::loop();

  atenderBoton();
  avisosDeEstado();
  informeGps();
  informeEnergia();
  informeNodos();

  hal::Evento ev;
  if (hal::keysPoll(&ev)) {
    if (ui::tecla(ev)) ui::pintar();
  }

  ui::loop();

  // Al final, cuando ui::loop() ya ha podido dormir la pantalla: asi el reloj y
  // el tick bajan en el mismo ciclo en que se apaga, no uno despues.
  ajustarReloj();
  delay(tickMs());
}
