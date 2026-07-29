// Tests de Position, User y Telemetry.
//
// Las secuencias esperadas se contrastaron ademas con un analizador de formato
// de cable independiente, para confirmar que los numeros de campo y los tipos
// coinciden con meshtastic/protobufs y no solo consigo mismos.

#include <unity.h>
#include <stdio.h>    // snprintf: libstdc++ 16 ya no lo arrastra de forma transitiva
#include <string.h>

#include "proto/messages.h"

using namespace proto;

void setUp() {}
void tearDown() {}

// ================================ Position ==================================

void test_position_bytes_exactos() {
  Position p;
  p.lat_e7 = 404168000; p.lon_e7 = -37038000;
  p.altitud = 650; p.tiempo = 1753790400; p.sats = 9;

  uint8_t b[64];
  const size_t n = codificarPosition(p, b, sizeof(b));

  // campo 1 sfixed32 -> etiqueta 0x0D, y el valor en little-endian
  TEST_ASSERT_EQUAL_UINT8(0x0D, b[0]);
  TEST_ASSERT_EQUAL_UINT8(0x40, b[1]);
  TEST_ASSERT_EQUAL_UINT8(0x1D, b[2]);
  TEST_ASSERT_EQUAL_UINT8(0x17, b[3]);
  TEST_ASSERT_EQUAL_UINT8(0x18, b[4]);
  // campo 2 sfixed32 -> etiqueta 0x15
  TEST_ASSERT_EQUAL_UINT8(0x15, b[5]);
  // campo 3 varint -> etiqueta 0x18
  TEST_ASSERT_EQUAL_UINT8(0x18, b[10]);
  // 5 (lat) + 5 (lon) + 3 (altitud 650 son 2 bytes de varint) + 5 (tiempo)
  // + 2 (sats) = 20
  TEST_ASSERT_EQUAL_size_t(20, n);
}

void test_position_ida_y_vuelta() {
  Position p;
  p.lat_e7 = 404168000; p.lon_e7 = -37038000;
  p.altitud = 650; p.tiempo = 1753790400; p.sats = 9;

  uint8_t b[64];
  const size_t n = codificarPosition(p, b, sizeof(b));

  Position r;
  const bool ok = decodificarPosition(b, n, &r);
  TEST_ASSERT_TRUE(ok);
  TEST_ASSERT_EQUAL_INT32(p.lat_e7, r.lat_e7);
  TEST_ASSERT_EQUAL_INT32(p.lon_e7, r.lon_e7);
  TEST_ASSERT_EQUAL_INT32(p.altitud, r.altitud);
  TEST_ASSERT_EQUAL_UINT32(p.tiempo, r.tiempo);
  TEST_ASSERT_EQUAL_UINT32(9, r.sats);
  TEST_ASSERT_TRUE(r.tiene_lat && r.tiene_lon);
}

void test_position_coordenadas_negativas() {
  // Hemisferio sur y altitud bajo el nivel del mar: el caso que rompe una
  // implementacion que trate sfixed32 como varint sin signo.
  Position p;
  p.lat_e7 = -334000000;   // Buenos Aires
  p.lon_e7 = -584000000;
  p.altitud = -30;

  uint8_t b[64];
  const size_t n = codificarPosition(p, b, sizeof(b));

  Position r;
  const bool ok = decodificarPosition(b, n, &r);
  TEST_ASSERT_TRUE(ok);
  TEST_ASSERT_EQUAL_INT32(-334000000, r.lat_e7);
  TEST_ASSERT_EQUAL_INT32(-584000000, r.lon_e7);
  TEST_ASSERT_EQUAL_INT32(-30, r.altitud);
}

void test_position_salta_campos_desconocidos() {
  // Un nodo con firmware oficial manda muchos mas campos que nosotros.
  const uint8_t b[] = {
      0x0D, 0x40, 0x1D, 0x17, 0x18,          // campo 1 sfixed32
      0x28, 0x01,                            // campo 5 varint (location_source)
      0x15, 0x50, 0xD8, 0xCA, 0xFD,          // campo 2 sfixed32
      0x48, 0x7B,                            // campo 9 varint (PDOP)
      0x52, 0x03, 'a', 'b', 'c'};            // campo 10 bytes, desconocido
  Position r;
  const bool ok = decodificarPosition(b, sizeof(b), &r);
  TEST_ASSERT_TRUE(ok);
  TEST_ASSERT_EQUAL_INT32(404168000, r.lat_e7);
  TEST_ASSERT_EQUAL_INT32(-37038000, r.lon_e7);
}

// ================================== User ====================================

void test_user_ida_y_vuelta() {
  User u;
  idDesdeNodeNum(0x7A3B12C4, u.id, sizeof(u.id));
  TEST_ASSERT_EQUAL_STRING("!7a3b12c4", u.id);
  snprintf(u.largo, sizeof(u.largo), "Papa");
  snprintf(u.corto, sizeof(u.corto), "PAPA");
  u.hw_model = 9;

  uint8_t b[128];
  const size_t n = codificarUser(u, b, sizeof(b));
  TEST_ASSERT_EQUAL_UINT8(0x0A, b[0]);       // campo 1, delimitado por longitud

  User r;
  const bool ok = decodificarUser(b, n, &r);
  TEST_ASSERT_TRUE(ok);
  TEST_ASSERT_EQUAL_STRING("!7a3b12c4", r.id);
  TEST_ASSERT_EQUAL_STRING("Papa", r.largo);
  TEST_ASSERT_EQUAL_STRING("PAPA", r.corto);
  TEST_ASSERT_EQUAL_UINT32(9, r.hw_model);
}

void test_user_trunca_nombre_largo() {
  // El protocolo admite 40 bytes; un nodo ajeno puede mandarlos todos.
  uint8_t b[128];
  size_t n = 0;
  b[n++] = 0x12;                              // campo 2, delimitado
  b[n++] = 60;                                // 60 bytes: mas de lo que cabe
  for (int i = 0; i < 60; i++) b[n++] = (uint8_t)('A' + (i % 26));

  User r;
  const bool ok = decodificarUser(b, n, &r);
  TEST_ASSERT_TRUE(ok);
  TEST_ASSERT_TRUE(strlen(r.largo) <= sizeof(r.largo) - 1);
}

// ================================ Telemetry =================================

void test_telemetry_ida_y_vuelta() {
  Telemetry t;
  t.tiempo = 1753790400; t.bateria_pct = 82;
  t.voltaje = 3.95f; t.uso_tx = 4.2f; t.uptime_s = 3600;

  uint8_t b[64];
  const size_t n = codificarTelemetry(t, b, sizeof(b));

  Telemetry r;
  const bool ok = decodificarTelemetry(b, n, &r);
  TEST_ASSERT_TRUE(ok);
  TEST_ASSERT_TRUE(r.tiene_metrics);
  TEST_ASSERT_EQUAL_UINT32(82, r.bateria_pct);
  TEST_ASSERT_EQUAL_UINT32(3600, r.uptime_s);
  TEST_ASSERT_FLOAT_WITHIN(0.001f, 3.95f, r.voltaje);
  TEST_ASSERT_FLOAT_WITHIN(0.001f, 4.2f, r.uso_tx);
  TEST_ASSERT_EQUAL_UINT32(1753790400, r.tiempo);
}

void test_telemetry_submensaje_anidado() {
  Telemetry t;
  t.bateria_pct = 50;
  uint8_t b[64];
  const size_t n = codificarTelemetry(t, b, sizeof(b));
  // Campo 2, delimitado por longitud: la etiqueta debe ser 0x12.
  TEST_ASSERT_EQUAL_UINT8(0x12, b[0]);
  TEST_ASSERT_EQUAL_UINT8(0x02, b[1]);   // el submensaje ocupa 2 bytes
  TEST_ASSERT_EQUAL_UINT8(0x08, b[2]);   // campo 1 varint dentro
  TEST_ASSERT_EQUAL_UINT8(50,   b[3]);
  TEST_ASSERT_EQUAL_size_t(4, n);
}

void test_mensajes_truncados_se_rechazan() {
  const uint8_t b[] = {0x0D, 0x40, 0x1D};    // fixed32 a medias
  Position p;
  TEST_ASSERT_FALSE(decodificarPosition(b, sizeof(b), &p));

  const uint8_t c[] = {0x12, 0x10, 'a'};     // longitud mayor que el buffer
  User u;
  TEST_ASSERT_FALSE(decodificarUser(c, sizeof(c), &u));
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_position_bytes_exactos);
  RUN_TEST(test_position_ida_y_vuelta);
  RUN_TEST(test_position_coordenadas_negativas);
  RUN_TEST(test_position_salta_campos_desconocidos);
  RUN_TEST(test_user_ida_y_vuelta);
  RUN_TEST(test_user_trunca_nombre_largo);
  RUN_TEST(test_telemetry_ida_y_vuelta);
  RUN_TEST(test_telemetry_submensaje_anidado);
  RUN_TEST(test_mensajes_truncados_se_rechazan);
  return UNITY_END();
}
