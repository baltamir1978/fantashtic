#include "sound.h"

#include <Arduino.h>
#include <M5Cardputer.h>

namespace hal {
namespace {

bool     g_alarma = false;
uint32_t g_last   = 0;
bool     g_alto   = false;

// Tono y silencio alternados: sin el hueco, tone() con
// stop_current_sound=true encadena los tonos y suena un zumbido continuo en
// vez de una alarma.
constexpr uint16_t SOS_HZ_ALTO = 1100;
constexpr uint16_t SOS_HZ_BAJO = 760;
constexpr uint16_t SOS_TONO_MS = 220;
constexpr uint16_t SOS_PASO_MS = 380;

}  // namespace

void soundBegin() {
  // M5.begin() ya arranca el altavoz y registra el callback del ES8311.
  M5Cardputer.Speaker.setVolume(160);
}

void soundVolumen(uint8_t v) { M5Cardputer.Speaker.setVolume(v); }

void beep()        { M5Cardputer.Speaker.tone(2000, 40); }
void beepError()   { M5Cardputer.Speaker.tone(220, 180); }
void beepMensaje() { M5Cardputer.Speaker.tone(1500, 60); }

void alarmaSos() {
  g_alarma = true;
  g_last   = millis() - SOS_PASO_MS;   // dispara en el proximo update
}

void alarmaParar() {
  g_alarma = false;
  M5Cardputer.Speaker.stop();
}

void soundUpdate() {
  if (!g_alarma) return;
  const uint32_t now = millis();
  if (now - g_last < SOS_PASO_MS) return;
  g_last = now;
  g_alto = !g_alto;
  M5Cardputer.Speaker.tone(g_alto ? SOS_HZ_ALTO : SOS_HZ_BAJO, SOS_TONO_MS);
}

}  // namespace hal
