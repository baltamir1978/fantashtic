// Tests del protocolo, en el ordenador. No requieren hardware.
//
//   pio test -e native
//
// Los vectores de test_vectors.h se generan con tools/gen_test_vectors.py y
// fijan byte a byte lo que debe salir por antena. Si estos tests pasan, la
// Fase 3 es correcta; si falla alguno, el primero que falla senala la etapa.

#include <unity.h>
#include <string.h>

#include "proto/aes.h"
#include "proto/crypto.h"
#include "proto/packet.h"
#include "test_vectors.h"

using namespace proto;

void setUp() {}
void tearDown() {}

// --- calibracion: constantes publicadas de Meshtastic ------------------------
// No dependen de nuestras claves. Si fallan, el error esta en la primitiva y
// no en la configuracion.

void test_xorhash_longfast_por_defecto() {
  TEST_ASSERT_EQUAL_UINT8(0x0A, xorHash((const uint8_t*)"LongFast", 8));
  TEST_ASSERT_EQUAL_UINT8(0x02, xorHash(PSK_POR_DEFECTO, 16));
  TEST_ASSERT_EQUAL_UINT8(0x08, hashCanal("LongFast", PSK_POR_DEFECTO, 16));
}

void test_djb2() {
  TEST_ASSERT_EQUAL_UINT32(130429955u, djb2("LongFast"));
}

void test_frecuencias() {
  int n = 0;
  // EU_868 con LONG_FAST: un unico hueco.
  TEST_ASSERT_FLOAT_WITHIN(0.0005f, 869.525f,
      frecuenciaCanal(869.4f, 869.65f, 250.0f, "LongFast", &n));
  TEST_ASSERT_EQUAL_INT(1, n);

  // EU_868 con LONG_MODERATE: dos huecos, y la eleccion depende del nombre.
  frecuenciaCanal(869.4f, 869.65f, 125.0f, "LongFast", &n);
  TEST_ASSERT_EQUAL_INT(2, n);

  TEST_ASSERT_FLOAT_WITHIN(0.0005f, 906.875f,
      frecuenciaCanal(902.0f, 928.0f, 250.0f, "LongFast", &n));
  TEST_ASSERT_EQUAL_INT(104, n);

  TEST_ASSERT_FLOAT_WITHIN(0.0005f, 433.875f,
      frecuenciaCanal(433.0f, 434.0f, 250.0f, "LongFast", &n));
  TEST_ASSERT_EQUAL_INT(4, n);
}

void test_ultimo_byte() {
  TEST_ASSERT_EQUAL_UINT8(0xC4, ultimoByte(0x7A3B12C4));
  // El 0x00 se sustituye por 0xFF: el cero significa "sin asignar".
  TEST_ASSERT_EQUAL_UINT8(0xFF, ultimoByte(0x4F91E200));
}

// --- primitivas --------------------------------------------------------------

void test_ctr_es_involutivo() {
  const uint8_t clave[32] = {1, 2, 3, 4, 5, 6, 7, 8};
  uint8_t nonce[16];
  construirNonce(nonce, 0x11223344, 0x55667788);

  const char* txt = "una cadena de prueba lo bastante larga como para ocupar "
                    "varios bloques de dieciseis bytes";
  const size_t n = strlen(txt);
  uint8_t cif[128], desc[128];

  TEST_ASSERT_TRUE(ctrXcrypt(clave, 32, nonce, (const uint8_t*)txt, cif, n));
  TEST_ASSERT_TRUE(ctrXcrypt(clave, 32, nonce, cif, desc, n));
  TEST_ASSERT_EQUAL_UINT8_ARRAY((const uint8_t*)txt, desc, n);
}

void test_aes_rechaza_claves_invalidas() {
  Aes a;
  const uint8_t k[32] = {};
  TEST_ASSERT_FALSE(a.init(k, 24));   // AES-192 no se usa en Meshtastic
  TEST_ASSERT_FALSE(a.init(k, 0));
  TEST_ASSERT_TRUE(a.init(k, 16));
  TEST_ASSERT_TRUE(a.init(k, 32));
}

void test_cabecera_ida_y_vuelta() {
  Cabecera c;
  c.to = 0xFFFFFFFF; c.from = 0x7A3B12C4; c.id = 0x2E4F1A07;
  c.hop_limit = 3; c.hop_start = 3; c.canal = 0x83;

  uint8_t b[16];
  empaquetarCabecera(c, b);
  TEST_ASSERT_EQUAL_UINT8(0x63, b[12]);   // hop_limit 3 | (hop_start 3 << 5)

  Cabecera r;
  TEST_ASSERT_TRUE(desempaquetarCabecera(b, &r));
  TEST_ASSERT_EQUAL_UINT32(c.from, r.from);
  TEST_ASSERT_EQUAL_UINT32(c.id, r.id);
  TEST_ASSERT_EQUAL_UINT8(3, r.hop_limit);
  TEST_ASSERT_EQUAL_UINT8(3, r.hop_start);
  TEST_ASSERT_EQUAL_UINT8(0x83, r.canal);
}

void test_data_salta_campos_desconocidos() {
  // portnum=1, payload="ab", want_response y un request_id fixed32.
  const uint8_t buf[] = {0x08, 0x01,                    // portnum = 1
                         0x12, 0x02, 'a', 'b',          // payload = "ab"
                         0x18, 0x01,                    // want_response = true
                         0x35, 0xDE, 0xAD, 0xBE, 0xEF}; // request_id, fixed32
  Data d;
  TEST_ASSERT_TRUE(decodificarData(buf, sizeof(buf), &d));
  TEST_ASSERT_EQUAL_UINT32(1, d.portnum);
  TEST_ASSERT_EQUAL_size_t(2, d.payload_len);
  TEST_ASSERT_EQUAL_UINT8_ARRAY((const uint8_t*)"ab", d.payload, 2);
}

// --- vectores: la prueba de fuego -------------------------------------------

void test_vectores() {
  for (size_t i = 0; i < FT_NUM_VECTORES; i++) {
    const ft_vector_t* v = &FT_VECTORES[i];

    // 1. hash de canal
    TEST_ASSERT_EQUAL_UINT8(v->ch_hash, hashCanal(v->ch_name, v->key, v->key_len));

    // 2. nonce
    uint8_t nonce[16];
    construirNonce(nonce, v->id, v->from);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(v->nonce, nonce, 16);

    // 3. Data en claro. El vector trae 08 01 12 LL seguido del texto.
    Data d;
    d.portnum = 1;                                   // TEXT_MESSAGE_APP
    d.payload_len = v->plain_len - 4;
    memcpy(d.payload, v->plain + 4, d.payload_len);
    uint8_t claro[256];
    const size_t nc = codificarData(d, claro, sizeof(claro));
    TEST_ASSERT_EQUAL_size_t(v->plain_len, nc);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(v->plain, claro, nc);

    // 4. paquete completo, byte a byte contra lo que sale por antena
    Cabecera c;
    c.to = v->to; c.from = v->from; c.id = v->id;
    c.hop_limit  = (uint8_t)(v->flags & 0x07);
    c.hop_start  = (uint8_t)((v->flags & 0xE0) >> 5);
    c.want_ack   = (v->flags & 0x08) != 0;
    c.canal      = v->ch_hash;
    c.next_hop   = v->air[14];
    c.relay_node = v->air[15];

    uint8_t aire[256];
    const size_t na = construirPaquete(c, d, v->key, v->key_len, aire, sizeof(aire));
    TEST_ASSERT_EQUAL_size_t(v->air_len, na);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(v->air, aire, na);

    // 5. vuelta
    Cabecera c2; Data d2;
    TEST_ASSERT_TRUE(analizarPaquete(v->air, v->air_len, v->key, v->key_len, &c2, &d2));
    TEST_ASSERT_EQUAL_UINT32(v->from, c2.from);
    TEST_ASSERT_EQUAL_UINT32(v->id, c2.id);
    TEST_ASSERT_EQUAL_UINT32(1, d2.portnum);
    TEST_ASSERT_EQUAL_size_t(d.payload_len, d2.payload_len);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(d.payload, d2.payload, d.payload_len);
  }
}

void test_clave_incorrecta_se_rechaza() {
  const ft_vector_t* v = &FT_VECTORES[0];
  uint8_t mala[32];
  memcpy(mala, v->key, v->key_len);
  mala[0] ^= 0xFF;

  Cabecera c; Data d;
  // Con la clave equivocada el descifrado da basura y el protobuf no cuadra.
  TEST_ASSERT_FALSE(analizarPaquete(v->air, v->air_len, mala, v->key_len, &c, &d));
}

void test_relay_no_toca_la_carga() {
  const ft_vector_t* v = &FT_VECTORES[0];
  uint8_t copia[256];
  memcpy(copia, v->air, v->air_len);

  Cabecera c;
  desempaquetarCabecera(copia, &c);
  c.hop_limit--;
  c.relay_node = ultimoByte(0x4F91E200);
  reescribirCabecera(copia, c);

  // La carga cifrada debe quedar intacta: retransmitir no exige descifrar.
  TEST_ASSERT_EQUAL_UINT8_ARRAY(v->air + 16, copia + 16, v->air_len - 16);
  TEST_ASSERT_EQUAL_UINT8(0x62, copia[12]);   // hop_limit 3 -> 2
  TEST_ASSERT_EQUAL_UINT8(0xFF, copia[15]);   // relay_node
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_xorhash_longfast_por_defecto);
  RUN_TEST(test_djb2);
  RUN_TEST(test_frecuencias);
  RUN_TEST(test_ultimo_byte);
  RUN_TEST(test_ctr_es_involutivo);
  RUN_TEST(test_aes_rechaza_claves_invalidas);
  RUN_TEST(test_cabecera_ida_y_vuelta);
  RUN_TEST(test_data_salta_campos_desconocidos);
  RUN_TEST(test_vectores);
  RUN_TEST(test_clave_incorrecta_se_rechaza);
  RUN_TEST(test_relay_no_toca_la_carga);
  return UNITY_END();
}
