// Tests de derivacion de clave, geodesia, UTF-8 y base de datos de nodos.
// No requieren hardware:  pio test -e native
//
// Los valores de referencia de kdf salen de hashlib y los de geo de la formula
// del haversine calculada aparte. No son numeros inventados a ojo.

#include <unity.h>
#include <stdio.h>    // snprintf: libstdc++ 16 ya no lo arrastra de forma transitiva
#include <string.h>

#include "mesh/nodedb.h"
#include "proto/kdf.h"
#include "util/geo.h"
#include "util/utf8.h"

void setUp() {}
void tearDown() {}

static void deHex(const char* hex, uint8_t* out, size_t n) {
  for (size_t i = 0; i < n; i++) {
    auto v = [](char c) -> uint8_t {
      if (c >= '0' && c <= '9') return (uint8_t)(c - '0');
      if (c >= 'a' && c <= 'f') return (uint8_t)(c - 'a' + 10);
      return (uint8_t)(c - 'A' + 10);
    };
    out[i] = (uint8_t)((v(hex[i * 2]) << 4) | v(hex[i * 2 + 1]));
  }
}

// ============================ SHA-256 / PBKDF2 ==============================

void test_sha256() {
  uint8_t o[32], esp[32];
  proto::sha256((const uint8_t*)"abc", 3, o);
  deHex("ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad", esp, 32);
  TEST_ASSERT_EQUAL_UINT8_ARRAY(esp, o, 32);

  proto::sha256((const uint8_t*)"", 0, o);
  deHex("e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855", esp, 32);
  TEST_ASSERT_EQUAL_UINT8_ARRAY(esp, o, 32);
}

void test_hmac_sha256() {
  uint8_t o[32], esp[32];
  proto::hmacSha256((const uint8_t*)"clave", 5, (const uint8_t*)"mensaje", 7, o);
  deHex("17e8847a84636888af5a7abe0910e50a0fa28d6f1d3591a175e6e01fcf80b412", esp, 32);
  TEST_ASSERT_EQUAL_UINT8_ARRAY(esp, o, 32);

  // Clave mas larga que el bloque: se resume antes de usarla.
  const char* larga = "una clave mas larga que el tamano de bloque de sha256, "
                      "es decir mas de sesenta y cuatro bytes de longitud";
  proto::hmacSha256((const uint8_t*)larga, strlen(larga), (const uint8_t*)"x", 1, o);
  deHex("f86df71d72ad02fe1da60a5e050cdc5e361a3c8dd6d5c8c0766adf1eb90b2131", esp, 32);
  TEST_ASSERT_EQUAL_UINT8_ARRAY(esp, o, 32);
}

void test_pbkdf2() {
  uint8_t o[64], esp[64];
  proto::pbkdf2Sha256((const uint8_t*)"password", 8, (const uint8_t*)"salt", 4, 1, o, 32);
  deHex("120fb6cffcf8b32c43e7225256c4f837a86548c92ccc35480805987cb70be17b", esp, 32);
  TEST_ASSERT_EQUAL_UINT8_ARRAY(esp, o, 32);

  proto::pbkdf2Sha256((const uint8_t*)"password", 8, (const uint8_t*)"salt", 4, 4096, o, 32);
  deHex("c5e478d59288c841aa530db6845c4c8d962893a001ce4e11a4963873aa98134a", esp, 32);
  TEST_ASSERT_EQUAL_UINT8_ARRAY(esp, o, 32);

  // Salida de mas de 32 bytes: obliga a encadenar bloques.
  proto::pbkdf2Sha256((const uint8_t*)"passwordPASSWORD", 16,
                      (const uint8_t*)"saltSALTsalt", 12, 100, o, 64);
  deHex("22aeb8d8128eea04dfde5e2f0b36ec94b5c7d54d686d4cdc2e8ce915e7c9fc2f"
        "5df9b2c1c308001c67741925600536aa4ce8c4e96a8efb279e6a2d9a1178dce2", esp, 64);
  TEST_ASSERT_EQUAL_UINT8_ARRAY(esp, o, 64);
}

void test_derivar_canal() {
  uint8_t psk[32], esp[32];
  char nombre[16];

  TEST_ASSERT_TRUE(proto::derivarCanal("montana-verde-2026", psk, nombre, sizeof(nombre)));

  // El nombre del canal sale de SHA256(frase) y no pasa por PBKDF2, asi que no
  // depende de la sal: se comprueba siempre.
  TEST_ASSERT_EQUAL_STRING("FT-OS6TN", nombre);

  // La PSK si depende de la sal, y la sal es por despliegue. Este vector es el
  // de la sal por defecto; con una propia en include/secrets.h la clave es otra
  // A PROPOSITO, y comparar aqui seria un fallo falso. PBKDF2 en si no se queda
  // sin cubrir: test_pbkdf2() lo valida contra los vectores de la RFC 6070.
  if (strcmp(PBKDF2_SALT, "fantashtic-v1") == 0) {
    deHex("5187da08a44dff357c3b06e3084edc4b978ddd85cd1275e54fac8532bc894afe", esp, 32);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(esp, psk, 32);
  } else {
    printf("  (sal propia en secrets.h: vector de PSK omitido a proposito)\n");
  }

  // Frase distinta: clave y nombre de canal distintos, para que dos grupos
  // no colisionen en el byte de canal de la cabecera.
  uint8_t psk2[32];
  char nombre2[16];
  TEST_ASSERT_TRUE(proto::derivarCanal("otra frase distinta", psk2, nombre2, sizeof(nombre2)));
  TEST_ASSERT_EQUAL_STRING("FT-56G54", nombre2);
  TEST_ASSERT_TRUE(memcmp(psk, psk2, 32) != 0);

  // Frase vacia: se rechaza.
  TEST_ASSERT_FALSE(proto::derivarCanal("", psk, nombre, sizeof(nombre)));
}

void test_derivacion_reproducible() {
  uint8_t a[32], b[32];
  char na[16], nb[16];
  proto::derivarCanal("la misma frase", a, na, sizeof(na));
  proto::derivarCanal("la misma frase", b, nb, sizeof(nb));
  TEST_ASSERT_EQUAL_UINT8_ARRAY(a, b, 32);
  TEST_ASSERT_EQUAL_STRING(na, nb);
}

// ================================ geodesia ==================================

void test_distancias() {
  // Madrid -> Barcelona: 505 095,7 m
  TEST_ASSERT_UINT32_WITHIN(600, 505096,
      util::distanciaM(404168000, -37038000, 413874000, 21686000));
  // Madrid -> Burgos: 214 284,5 m
  TEST_ASSERT_UINT32_WITHIN(300, 214285,
      util::distanciaM(404168000, -37038000, 423439000, -36969000));
  // 500 m al norte
  TEST_ASSERT_UINT32_WITHIN(3, 499,
      util::distanciaM(404168000, -37038000, 404212900, -37038000));
  // 1 km al este
  TEST_ASSERT_UINT32_WITHIN(3, 1000,
      util::distanciaM(404168000, -37038000, 404168000, -36919900));
  // El mismo punto
  TEST_ASSERT_EQUAL_UINT32(0,
      util::distanciaM(404168000, -37038000, 404168000, -37038000));
}

void test_rumbos() {
  TEST_ASSERT_UINT32_WITHIN(2, 76,
      util::rumboGrados(404168000, -37038000, 413874000, 21686000));
  TEST_ASSERT_UINT32_WITHIN(2, 0,
      util::rumboGrados(404168000, -37038000, 404212900, -37038000));
  TEST_ASSERT_UINT32_WITHIN(2, 90,
      util::rumboGrados(404168000, -37038000, 404168000, -36919900));
}

void test_puntos_cardinales() {
  TEST_ASSERT_EQUAL_STRING("N",  util::puntoCardinal(0));
  TEST_ASSERT_EQUAL_STRING("NE", util::puntoCardinal(45));
  TEST_ASSERT_EQUAL_STRING("E",  util::puntoCardinal(90));
  TEST_ASSERT_EQUAL_STRING("S",  util::puntoCardinal(180));
  TEST_ASSERT_EQUAL_STRING("O",  util::puntoCardinal(270));
  TEST_ASSERT_EQUAL_STRING("N",  util::puntoCardinal(359));   // vuelve al norte
}

void test_formato_distancia() {
  char b[16];
  util::formatearDistancia(420, b, sizeof(b));    TEST_ASSERT_EQUAL_STRING("420 m", b);
  util::formatearDistancia(999, b, sizeof(b));    TEST_ASSERT_EQUAL_STRING("999 m", b);
  util::formatearDistancia(1800, b, sizeof(b));   TEST_ASSERT_EQUAL_STRING("1,8 km", b);
  util::formatearDistancia(12340, b, sizeof(b));  TEST_ASSERT_EQUAL_STRING("12,3 km", b);
  util::formatearDistancia(505096, b, sizeof(b)); TEST_ASSERT_EQUAL_STRING("505 km", b);
}

// ================================== UTF-8 ===================================

void test_longitud_caracter() {
  TEST_ASSERT_EQUAL_UINT8(1, util::longitudCaracter('a'));
  TEST_ASSERT_EQUAL_UINT8(2, util::longitudCaracter(0xC3));   // n con virgulilla
  TEST_ASSERT_EQUAL_UINT8(3, util::longitudCaracter(0xE2));
  TEST_ASSERT_EQUAL_UINT8(4, util::longitudCaracter(0xF0));   // emoji
  TEST_ASSERT_EQUAL_UINT8(0, util::longitudCaracter(0x80));   // continuacion
}

void test_truncado_no_parte_caracteres() {
  // "año" son 4 bytes: 'a', 0xC3 0xB1, 'o'
  const char* s = "a\xC3\xB1o";
  TEST_ASSERT_EQUAL_size_t(4, strlen(s));
  TEST_ASSERT_EQUAL_size_t(3, util::contarCaracteres(s));

  // Cortando por el byte 2 quedaria medio caracter: debe devolver 1.
  TEST_ASSERT_EQUAL_size_t(1, util::longitudSegura(s, 2));
  TEST_ASSERT_EQUAL_size_t(3, util::longitudSegura(s, 3));
  TEST_ASSERT_EQUAL_size_t(4, util::longitudSegura(s, 10));

  char buf[16];
  snprintf(buf, sizeof(buf), "%s", s);
  util::truncarSeguro(buf, 2);
  TEST_ASSERT_EQUAL_STRING("a", buf);
  TEST_ASSERT_TRUE(util::esUtf8Valido(buf));
}

void test_emoji_ocupa_cuatro_bytes() {
  const char* s = "ok \xF0\x9F\x91\x8D";     // ok + pulgar arriba
  TEST_ASSERT_EQUAL_size_t(7, strlen(s));
  TEST_ASSERT_EQUAL_size_t(4, util::contarCaracteres(s));
  // Con 5 o 6 bytes de hueco el emoji no cabe entero: no se parte.
  TEST_ASSERT_EQUAL_size_t(3, util::longitudSegura(s, 5));
  TEST_ASSERT_EQUAL_size_t(7, util::longitudSegura(s, 7));
}

void test_utf8_invalido_se_detecta() {
  TEST_ASSERT_TRUE(util::esUtf8Valido("normal"));
  TEST_ASSERT_TRUE(util::esUtf8Valido("acci\xC3\xB3n"));
  TEST_ASSERT_FALSE(util::esUtf8Valido("\xC3"));        // truncado
  TEST_ASSERT_FALSE(util::esUtf8Valido("\xC3\x28"));    // continuacion mala
  TEST_ASSERT_FALSE(util::esUtf8Valido("\x80""abc"));   // empieza por continuacion
}

// ============================ base de datos =================================

void test_nodedb_alta_y_actualizacion() {
  mesh::BaseNodos db;
  TEST_ASSERT_EQUAL_size_t(0, db.total());

  auto* n = db.visto(0x1111, 1000, -80, 7, 0);
  TEST_ASSERT_NOT_NULL(n);
  TEST_ASSERT_EQUAL_size_t(1, db.total());
  TEST_ASSERT_EQUAL_UINT8(255, n->bateria);   // desconocida al principio

  db.visto(0x1111, 2000, -70, 9, 1);          // el mismo, no crea otro
  TEST_ASSERT_EQUAL_size_t(1, db.total());
  TEST_ASSERT_EQUAL_INT(-70, db.buscar(0x1111)->rssi);
  TEST_ASSERT_EQUAL_UINT32(2000, db.buscar(0x1111)->visto_ms);

  TEST_ASSERT_NULL(db.visto(0, 1000, 0, 0, 0));   // NodeNum 0 no es valido
}

void test_nodedb_trunca_nombres_largos() {
  mesh::BaseNodos db;
  db.visto(0x2222, 1000, -80, 7, 0);
  // El protocolo admite hasta 40 bytes; aqui caben 15. Un strcpy seria un
  // desbordamiento en el primer NODEINFO de un nodo con firmware oficial.
  db.ponerNombres(0x2222,
                  "Un nombre larguisimo que no cabe ni de lejos en el campo",
                  "ABCDEFGHIJ");
  const auto* n = db.buscar(0x2222);
  TEST_ASSERT_TRUE(strlen(n->largo) <= NODE_LONGNAME_LEN - 1);
  TEST_ASSERT_TRUE(strlen(n->corto) <= NODE_SHORTNAME_LEN - 1);
  TEST_ASSERT_EQUAL_STRING("ABCD", n->corto);
}

void test_nodedb_trunca_respetando_utf8() {
  mesh::BaseNodos db;
  db.visto(0x3333, 1000, -80, 7, 0);
  // Nombre lleno de caracteres de 2 bytes: el corte no debe partir ninguno.
  db.ponerNombres(0x3333, "\xC3\xB1\xC3\xB1\xC3\xB1\xC3\xB1\xC3\xB1\xC3\xB1"
                          "\xC3\xB1\xC3\xB1\xC3\xB1\xC3\xB1", "\xC3\xB1\xC3\xB1\xC3\xB1");
  const auto* n = db.buscar(0x3333);
  TEST_ASSERT_TRUE(util::esUtf8Valido(n->largo));
  TEST_ASSERT_TRUE(util::esUtf8Valido(n->corto));
}

void test_nodedb_desaloja_al_llenarse() {
  mesh::BaseNodos db;
  for (uint32_t i = 1; i <= mesh::BaseNodos::capacidad(); i++)
    db.visto(0x1000 + i, i * 1000, -80, 7, 0);
  TEST_ASSERT_EQUAL_size_t(mesh::BaseNodos::capacidad(), db.total());
  TEST_ASSERT_NOT_NULL(db.buscar(0x1001));

  // Uno mas: cae el que llevaba mas tiempo sin dar senales.
  db.visto(0x9999, 999000, -80, 7, 0);
  TEST_ASSERT_NULL(db.buscar(0x1001));
  TEST_ASSERT_NOT_NULL(db.buscar(0x9999));
  TEST_ASSERT_EQUAL_size_t(mesh::BaseNodos::capacidad(), db.total());
}

void test_nodedb_activos() {
  mesh::BaseNodos db;
  constexpr uint32_t VENTANA = 15u * 60u * 1000u;   // 900 000 ms
  constexpr uint32_t AHORA   = 1000000u;

  db.visto(0x1, 1000,   -80, 7, 0);   // hace 999 s: fuera de la ventana
  db.visto(0x2, 500000, -80, 7, 0);   // hace 500 s: dentro
  db.visto(0x3, 900000, -80, 7, 0);   // hace 100 s: dentro
  TEST_ASSERT_EQUAL_size_t(2, db.activos(AHORA, VENTANA));

  // El limite es inclusivo: justo en el borde sigue contando.
  TEST_ASSERT_EQUAL_size_t(3, db.activos(901000, VENTANA));
}

void test_nodedb_ignora_posicion_nula() {
  mesh::BaseNodos db;
  db.visto(0x4444, 1000, -80, 7, 0);
  db.ponerPosicion(0x4444, 0, 0, 0, 2000);      // fix invalido tipico
  TEST_ASSERT_FALSE(db.buscar(0x4444)->tiene_pos);

  db.ponerPosicion(0x4444, 404168000, -37038000, 650, 3000);
  TEST_ASSERT_TRUE(db.buscar(0x4444)->tiene_pos);
  TEST_ASSERT_EQUAL_INT32(404168000, db.buscar(0x4444)->lat_e7);
}

void test_nodedb_nombre_para_mostrar() {
  mesh::BaseNodos db;
  db.visto(0xABCD1234, 1000, -80, 7, 0);
  char b[16];
  // Sin nombre corto todavia: se muestran los dos ultimos bytes en hexadecimal.
  db.nombreMostrar(*db.buscar(0xABCD1234), b, sizeof(b));
  TEST_ASSERT_EQUAL_STRING("1234", b);

  db.ponerNombres(0xABCD1234, "Papa", "PAPA");
  db.nombreMostrar(*db.buscar(0xABCD1234), b, sizeof(b));
  TEST_ASSERT_EQUAL_STRING("PAPA", b);
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_sha256);
  RUN_TEST(test_hmac_sha256);
  RUN_TEST(test_pbkdf2);
  RUN_TEST(test_derivar_canal);
  RUN_TEST(test_derivacion_reproducible);
  RUN_TEST(test_distancias);
  RUN_TEST(test_rumbos);
  RUN_TEST(test_puntos_cardinales);
  RUN_TEST(test_formato_distancia);
  RUN_TEST(test_longitud_caracter);
  RUN_TEST(test_truncado_no_parte_caracteres);
  RUN_TEST(test_emoji_ocupa_cuatro_bytes);
  RUN_TEST(test_utf8_invalido_se_detecta);
  RUN_TEST(test_nodedb_alta_y_actualizacion);
  RUN_TEST(test_nodedb_trunca_nombres_largos);
  RUN_TEST(test_nodedb_trunca_respetando_utf8);
  RUN_TEST(test_nodedb_desaloja_al_llenarse);
  RUN_TEST(test_nodedb_activos);
  RUN_TEST(test_nodedb_ignora_posicion_nula);
  RUN_TEST(test_nodedb_nombre_para_mostrar);
  return UNITY_END();
}
