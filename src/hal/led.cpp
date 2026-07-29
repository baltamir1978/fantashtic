#include "led.h"

#include <Adafruit_NeoPixel.h>
#include <Arduino.h>

#include "config.h"

namespace hal {
namespace {

Adafruit_NeoPixel g_px(NEOPIXEL_COUNT, PIN_NEOPIXEL, NEO_GRB + NEO_KHZ800);
bool     g_begun = false;
uint8_t  g_r = 0, g_g = 0, g_b = 0;
uint16_t g_periodo = 0;
uint32_t g_last = 0;
bool     g_on = false;

void push(uint8_t r, uint8_t g, uint8_t b) {
  if (!g_begun) return;
  g_px.setPixelColor(0, g_px.Color(r, g, b));
  g_px.show();
}

}  // namespace

void ledBegin() {
  if (g_begun) return;
  g_px.begin();
  g_px.setBrightness(60);   // a tope deslumbra y consume de mas
  g_begun = true;
  ledOff();
}

void ledSet(uint8_t r, uint8_t g, uint8_t b) {
  g_periodo = 0;
  g_r = r; g_g = g; g_b = b;
  g_on = true;
  push(r, g, b);
}

void ledOff() {
  g_periodo = 0;
  g_on = false;
  push(0, 0, 0);
}

void ledBlink(uint8_t r, uint8_t g, uint8_t b, uint16_t periodo_ms) {
  g_r = r; g_g = g; g_b = b;
  g_periodo = periodo_ms;
  if (periodo_ms == 0) { ledSet(r, g, b); return; }
  g_last = millis();
  g_on = true;
  push(r, g, b);
}

void ledUpdate() {
  if (g_periodo == 0) return;
  const uint32_t now = millis();
  if (now - g_last < g_periodo) return;
  g_last = now;
  g_on = !g_on;
  if (g_on) push(g_r, g_g, g_b);
  else      push(0, 0, 0);
}

}  // namespace hal
