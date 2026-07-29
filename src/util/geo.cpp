#include "util/geo.h"

#include <math.h>
#include <stdio.h>

namespace util {
namespace {

constexpr double RADIO_TIERRA_M = 6371000.0;
constexpr double GRADOS_A_RAD   = 3.14159265358979323846 / 180.0;

inline double aRad(int32_t e7) { return (double)e7 * 1e-7 * GRADOS_A_RAD; }

}  // namespace

uint32_t distanciaM(int32_t lat1_e7, int32_t lon1_e7,
                    int32_t lat2_e7, int32_t lon2_e7) {
  const double f1 = aRad(lat1_e7), f2 = aRad(lat2_e7);
  const double df = f2 - f1;
  const double dl = aRad(lon2_e7) - aRad(lon1_e7);

  const double a = sin(df / 2) * sin(df / 2) +
                   cos(f1) * cos(f2) * sin(dl / 2) * sin(dl / 2);
  const double c = 2 * atan2(sqrt(a), sqrt(1 - a));
  const double d = RADIO_TIERRA_M * c;
  return (uint32_t)(d < 0 ? 0 : d + 0.5);
}

uint16_t rumboGrados(int32_t lat1_e7, int32_t lon1_e7,
                     int32_t lat2_e7, int32_t lon2_e7) {
  const double f1 = aRad(lat1_e7), f2 = aRad(lat2_e7);
  const double dl = aRad(lon2_e7) - aRad(lon1_e7);

  const double y = sin(dl) * cos(f2);
  const double x = cos(f1) * sin(f2) - sin(f1) * cos(f2) * cos(dl);
  double g = atan2(y, x) / GRADOS_A_RAD;
  if (g < 0) g += 360.0;
  if (g >= 360.0) g -= 360.0;
  return (uint16_t)(g + 0.5) % 360;
}

void formatearDistancia(uint32_t metros, char* out, size_t cap) {
  if (cap == 0) return;
  if (metros < 1000) {
    snprintf(out, cap, "%u m", (unsigned)metros);
  } else {
    const unsigned km  = metros / 1000;
    const unsigned dec = (metros % 1000) / 100;
    if (km >= 100) snprintf(out, cap, "%u km", km);          // sin decimal
    else           snprintf(out, cap, "%u,%u km", km, dec);
  }
}

const char* puntoCardinal(uint16_t rumbo) {
  static const char* P[8] = {"N", "NE", "E", "SE", "S", "SO", "O", "NO"};
  return P[(uint8_t)(((rumbo % 360) + 22) / 45) % 8];
}

}  // namespace util
