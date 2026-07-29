#include "storage.h"

#include <Arduino.h>
#include <SD.h>
#include <SPI.h>
#include <stdio.h>
#include <string.h>

#include "config.h"

namespace hal {
namespace {

SPIClass g_spi(HSPI);   // HSPI == SPI3_HOST en el S3. El global SPI es SPI2,
                        // que usa M5GFX para la pantalla: no tocarlo.
bool     g_spi_begun = false;
int      g_spi_depth = 0;   // contador, no booleano: las funciones publicas se
                            // llaman entre si y un lock simple se autobloquea
bool     g_sd_ok     = false;

// RAII con lock reentrante. Con un solo hilo el contador basta; su unico
// trabajo es documentar y hacer cumplir "nunca dos CS bajos a la vez".
struct SpiGuard {
  SpiGuard()  { spiTake(); }
  ~SpiGuard() { spiGive(); }
};

// Variantes internas SIN guard, para poder llamarlas desde funciones que ya
// lo tienen tomado.
uint32_t totalMB_() { return g_sd_ok ? (uint32_t)(SD.totalBytes() / (1024 * 1024)) : 0; }
uint32_t usedMB_()  { return g_sd_ok ? (uint32_t)(SD.usedBytes()  / (1024 * 1024)) : 0; }

}  // namespace

SPIClass& spiBus() { return g_spi; }

void spiBegin() {
  if (g_spi_begun) return;
  g_spi.begin(PIN_SPI_SCK, PIN_SPI_MISO, PIN_SPI_MOSI);
  // Los dos CS en alto antes de nada.
  pinMode(PIN_SD_CS, OUTPUT);   digitalWrite(PIN_SD_CS, HIGH);
  pinMode(PIN_LORA_CS, OUTPUT); digitalWrite(PIN_LORA_CS, HIGH);
  g_spi_begun = true;
}

bool spiTake() { g_spi_depth++; return true; }
void spiGive() { if (g_spi_depth > 0) g_spi_depth--; }
bool spiBusy() { return g_spi_depth > 0; }

bool sdOk() { return g_sd_ok; }

bool sdBegin() {
  spiBegin();
  SpiGuard g;

  g_sd_ok = SD.begin(PIN_SD_CS, g_spi, SPI_FREQ_SD);
  if (!g_sd_ok) {
    Serial.println("microSD: no montada (se sigue, solo sin historial persistente)");
    return false;
  }
  Serial.printf("microSD: montada, %u MB totales, %u MB usados\n",
                (unsigned)totalMB_(), (unsigned)usedMB_());
  return true;
}

uint32_t sdTotalMB() { SpiGuard g; return totalMB_(); }
uint32_t sdUsedMB()  { SpiGuard g; return usedMB_(); }

bool sdEnsureDirs() {
  if (!g_sd_ok) return false;
  SpiGuard g;
  const char* dirs[] = {SD_DIR_BASE, SD_DIR_CHAT, SD_DIR_CAMPO};
  for (const char* d : dirs)
    if (!SD.exists(d) && !SD.mkdir(d)) {
      Serial.printf("microSD: no se pudo crear %s\n", d);
      return false;
    }
  return true;
}

bool sdAppendLine(const char* path, const char* line) {
  if (!g_sd_ok) return false;
  SpiGuard g;

  File f = SD.open(path, FILE_APPEND);
  if (!f) {
    // Lo mas probable es que hayan sacado la tarjeta en marcha.
    g_sd_ok = false;
    Serial.printf("microSD: fallo al abrir %s, se marca como ausente\n", path);
    return false;
  }
  const bool ok = f.println(line) > 0;
  f.close();
  if (!ok) g_sd_ok = false;
  return ok;
}

bool sdReadLastLine(const char* path, char* out, size_t out_len) {
  if (!g_sd_ok || out_len == 0) return false;
  SpiGuard g;

  File f = SD.open(path, FILE_READ);
  if (!f) return false;

  out[0] = '\0';
  char buf[192];
  while (f.available()) {
    const size_t n = f.readBytesUntil('\n', buf, sizeof(buf) - 1);
    if (n == 0) continue;
    buf[n] = '\0';
    // Quitar un \r final (ficheros tocados en Windows).
    size_t len = n;
    if (len && buf[len - 1] == '\r') buf[--len] = '\0';
    if (len == 0) continue;
    snprintf(out, out_len, "%s", buf);
  }
  f.close();
  return out[0] != '\0';
}

bool sdSelfTest(char* msg, size_t msg_len) {
  if (!g_sd_ok) {
    snprintf(msg, msg_len, "sin tarjeta (correcto: se sigue)");
    return false;
  }
  if (!sdEnsureDirs()) {
    snprintf(msg, msg_len, "no se crearon los directorios");
    return false;
  }

  // Fichero nuevo en cada arranque: si se acumulara, sdReadLastLine tendria
  // que recorrer un fichero cada vez mas largo.
  const char* path = SD_DIR_BASE "/autotest.txt";
  { SpiGuard g; if (SD.exists(path)) SD.remove(path); }

  char escrito[64];
  snprintf(escrito, sizeof(escrito), "fase1 autotest %lu", (unsigned long)millis());

  if (!sdAppendLine(path, escrito)) { snprintf(msg, msg_len, "fallo al escribir"); return false; }

  char leido[96] = {0};
  if (!sdReadLastLine(path, leido, sizeof(leido))) {
    snprintf(msg, msg_len, "fallo al releer");
    return false;
  }
  if (strcmp(escrito, leido) != 0) {
    snprintf(msg, msg_len, "lo releido no coincide");
    return false;
  }

  snprintf(msg, msg_len, "OK  %u/%u MB", (unsigned)sdUsedMB(), (unsigned)sdTotalMB());
  return true;
}

}  // namespace hal
