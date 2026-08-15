// Tests del router de inundacion y del contador de ciclo de trabajo.
// No requieren hardware:  pio test -e native

#include <unity.h>
#include <string.h>

#include "mesh/duty.h"
#include "mesh/history.h"
#include "mesh/router.h"
#include "proto/crypto.h"

using namespace mesh;
using proto::Cabecera;

constexpr uint32_t YO    = 0x7A3B12C4;
constexpr uint32_t OTRO  = 0x4F91E200;   // acaba en 0x00: ultimoByte da 0xFF
constexpr uint32_t BCAST = 0xFFFFFFFFu;

void setUp() {}
void tearDown() {}

Cabecera hacer(uint32_t to, uint32_t from, uint32_t id, uint8_t hops = 3) {
  Cabecera c;
  c.to = to; c.from = from; c.id = id;
  c.hop_limit = hops; c.hop_start = 3;
  return c;
}

// ============================= historial ====================================

void test_historial_detecta_duplicados() {
  Historial h;
  TEST_ASSERT_FALSE(h.vistoOAnadir(OTRO, 100));   // primera vez
  TEST_ASSERT_TRUE(h.vistoOAnadir(OTRO, 100));    // duplicado
  TEST_ASSERT_FALSE(h.vistoOAnadir(OTRO, 101));   // otro id
  TEST_ASSERT_FALSE(h.vistoOAnadir(YO, 100));     // mismo id, otro emisor
  TEST_ASSERT_EQUAL_size_t(3, h.ocupacion());
}

void test_historial_ignora_id_cero() {
  Historial h;
  TEST_ASSERT_FALSE(h.vistoOAnadir(OTRO, 0));
  TEST_ASSERT_FALSE(h.vistoOAnadir(OTRO, 0));   // sigue sin registrarse
  TEST_ASSERT_EQUAL_size_t(0, h.ocupacion());
}

void test_historial_desaloja_el_mas_antiguo() {
  Historial h;
  for (uint32_t i = 1; i <= HIST_ENTRADAS; i++) h.vistoOAnadir(OTRO, i);
  TEST_ASSERT_TRUE(h.contiene(OTRO, 1));

  h.vistoOAnadir(OTRO, HIST_ENTRADAS + 1);       // desborda
  TEST_ASSERT_FALSE(h.contiene(OTRO, 1));        // cayo el mas antiguo
  TEST_ASSERT_TRUE(h.contiene(OTRO, 2));
  TEST_ASSERT_TRUE(h.contiene(OTRO, HIST_ENTRADAS + 1));
  TEST_ASSERT_EQUAL_size_t(HIST_ENTRADAS, h.ocupacion());
}

void test_historial_anota_relevos() {
  Historial h;
  h.vistoOAnadir(OTRO, 7);
  h.anotarRelayer(OTRO, 7, 0xAB);
  h.anotarRelayer(OTRO, 7, 0xAB);     // repetido: no duplica
  h.anotarRelayer(OTRO, 7, 0xCD);
  TEST_ASSERT_TRUE(h.relayadoPor(OTRO, 7, 0xAB));
  TEST_ASSERT_TRUE(h.relayadoPor(OTRO, 7, 0xCD));
  TEST_ASSERT_FALSE(h.relayadoPor(OTRO, 7, 0xEF));
  h.anotarRelayer(OTRO, 7, 0);        // 0 = sin asignar, se ignora
  TEST_ASSERT_FALSE(h.relayadoPor(OTRO, 7, 0));
}

// =============================== router =====================================

void test_difusion_se_entrega_Y_se_retransmite() {
  Historial h;
  const auto c = hacer(BCAST, OTRO, 10);
  // Es la propiedad que sostiene toda la malla: si se filtrara por "es para
  // mi" (cierto en un broadcast), no se retransmitiria nunca nada.
  TEST_ASSERT_EQUAL_INT((int)Accion::ENTREGAR_Y_RETRANSMITIR,
                        (int)decidir(c, YO, h, true));
}

void test_duplicado_se_descarta() {
  Historial h;
  const auto c = hacer(BCAST, OTRO, 11);
  decidir(c, YO, h, true);
  TEST_ASSERT_EQUAL_INT((int)Accion::DESCARTAR, (int)decidir(c, YO, h, true));
}

void test_unicast_para_mi_solo_se_entrega() {
  Historial h;
  const auto c = hacer(YO, OTRO, 12);
  TEST_ASSERT_EQUAL_INT((int)Accion::ENTREGAR, (int)decidir(c, YO, h, true));
}

void test_unicast_para_otro_solo_se_releva() {
  Historial h;
  const auto c = hacer(0x11223344, OTRO, 13);
  TEST_ASSERT_EQUAL_INT((int)Accion::RETRANSMITIR, (int)decidir(c, YO, h, true));
}

void test_sin_saltos_no_se_retransmite() {
  Historial h;
  auto c = hacer(BCAST, OTRO, 14, 0);
  // Una difusion sin saltos SE ENTREGA aunque no se releve, y de eso depende el
  // latido de presencia: sale con HEARTBEAT_HOPS = 0 justo para que nadie lo
  // repita y mida el enlace directo. Si esto se "optimizara" a DESCARTAR, la
  // vigilancia de cobertura dejaria de funcionar sin que nada mas se rompiera.
  TEST_ASSERT_EQUAL_INT((int)Accion::ENTREGAR, (int)decidir(c, YO, h, true));

  Historial h2;
  auto c2 = hacer(0x11223344, OTRO, 15, 0);
  TEST_ASSERT_EQUAL_INT((int)Accion::DESCARTAR, (int)decidir(c2, YO, h2, true));
}

void test_nodo_mudo_no_retransmite() {
  Historial h;
  const auto c = hacer(BCAST, OTRO, 16);
  TEST_ASSERT_EQUAL_INT((int)Accion::ENTREGAR, (int)decidir(c, YO, h, false));
}

void test_ack_implicito() {
  Historial h;
  auto propio = hacer(BCAST, YO, 20);
  h.vistoOAnadir(YO, 20);              // lo registramos al enviarlo

  propio.relay_node = 0xFF;            // vuelve relevado por otro
  TEST_ASSERT_EQUAL_INT((int)Accion::ACK_IMPLICITO, (int)decidir(propio, YO, h, true));
  TEST_ASSERT_TRUE(h.relayadoPor(YO, 20, 0xFF));
}

void test_eco_propio_desconocido_se_descarta() {
  Historial h;
  const auto c = hacer(BCAST, YO, 21);   // nuestro, pero no lo hemos enviado
  TEST_ASSERT_EQUAL_INT((int)Accion::DESCARTAR, (int)decidir(c, YO, h, true));
}

void test_id_cero_se_descarta() {
  Historial h;
  const auto c = hacer(BCAST, OTRO, 0);
  TEST_ASSERT_EQUAL_INT((int)Accion::DESCARTAR, (int)decidir(c, YO, h, true));
}

void test_next_hop_ajeno_no_se_releva() {
  Historial h;
  auto c = hacer(0x11223344, OTRO, 22);
  c.next_hop = 0x55;                   // dirigido a otro relevo
  TEST_ASSERT_EQUAL_INT((int)Accion::DESCARTAR, (int)decidir(c, YO, h, true));

  Historial h2;
  auto c2 = hacer(0x11223344, OTRO, 23);
  c2.next_hop = proto::ultimoByte(YO); // nos toca a nosotros
  TEST_ASSERT_EQUAL_INT((int)Accion::RETRANSMITIR, (int)decidir(c2, YO, h2, true));
}

void test_preparar_relevo() {
  auto c = hacer(BCAST, OTRO, 24, 3);
  prepararRelevo(&c, OTRO);            // relevado por el nodo acabado en 0x00
  TEST_ASSERT_EQUAL_UINT8(2, c.hop_limit);
  TEST_ASSERT_EQUAL_UINT8(3, c.hop_start);   // hop_start no cambia nunca
  TEST_ASSERT_EQUAL_UINT8(0xFF, c.relay_node);
}

// ============================ temporizacion =================================

void test_slot_time() {
  // SF11 / BW250: 2,5 * 8,192 + 7,6 = 28,08 ms
  TEST_ASSERT_FLOAT_WITHIN(0.1f, 28.08f, slotTimeMs(11, 250.0f));
}

void test_ventana_de_contencion() {
  TEST_ASSERT_EQUAL_UINT8(3, cwPorSnr(-20.0f));   // CW_MIN
  TEST_ASSERT_EQUAL_UINT8(8, cwPorSnr(10.0f));    // CW_MAX
  TEST_ASSERT_EQUAL_UINT8(3, cwPorSnr(-40.0f));   // se satura por abajo
  TEST_ASSERT_EQUAL_UINT8(8, cwPorSnr(30.0f));    // y por arriba

  // Peor senal -> ventana mas pequena -> retransmite antes. Es intencional.
  TEST_ASSERT_TRUE(cwPorSnr(-18.0f) < cwPorSnr(8.0f));
}

void test_retardo_de_relevo_lleva_desplazamiento() {
  const float slot = slotTimeMs(11, 250.0f);
  // Aunque el aleatorio sea 0, hay 2*CW_MAX ranuras de desplazamiento fijo.
  const uint32_t minimo = retardoRelevoMs(-20.0f, 0, slot);
  TEST_ASSERT_TRUE(minimo >= (uint32_t)(2 * 8 * slot) - 1);

  // El propio no lo lleva: con aleatorio 0, sale 0.
  TEST_ASSERT_EQUAL_UINT32(0, retardoPropioMs(0, 0, slot));
}

// ========================== ciclo de trabajo ================================

void test_duty_vacio_permite_todo() {
  CicloTrabajo d;
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 0.0f, d.utilizacionPct(0));
  TEST_ASSERT_TRUE(d.permitido(0, true));
  TEST_ASSERT_TRUE(d.permitido(0, false));
}

void test_duty_acumula_y_bloquea() {
  CicloTrabajo d;
  // 10 % de una hora son 360 s. Metemos 300 s -> 8,33 %: pasa del umbral
  // blando pero no del duro.
  d.registrar(0, 300000);
  TEST_ASSERT_FLOAT_WITHIN(0.05f, 8.33f, d.utilizacionPct(1000));
  TEST_ASSERT_FALSE(d.permitido(1000, true));    // automatico: bloqueado
  TEST_ASSERT_TRUE(d.permitido(1000, false));    // del usuario: pasa

  // Otros 100 s -> 11,1 %: por encima del limite legal, no pasa nadie.
  d.registrar(60000, 100000);
  TEST_ASSERT_TRUE(d.utilizacionPct(61000) > 10.0f);
  TEST_ASSERT_FALSE(d.permitido(61000, false));
  TEST_ASSERT_FALSE(d.permitido(61000, true));
}

void test_duty_ventana_rodante() {
  CicloTrabajo d;
  d.registrar(0, 400000);                        // 400 s: por encima del 10 %
  TEST_ASSERT_FALSE(d.permitido(1000, false));

  // A los 60 minutos ese cubo sale de la ventana y vuelve a permitirse.
  const uint32_t una_hora = 60u * 60u * 1000u;
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 0.0f, d.utilizacionPct(una_hora + 1000));
  TEST_ASSERT_TRUE(d.permitido(una_hora + 1000, false));
}

void test_duty_reparte_en_cubos() {
  CicloTrabajo d;
  // Un segundo de aire por minuto durante 30 minutos = 30 s en la ventana.
  for (uint32_t m = 0; m < 30; m++) d.registrar(m * 60000, 1000);
  TEST_ASSERT_EQUAL_UINT32(30000, d.airtimeVentanaMs(30 * 60000));
  TEST_ASSERT_FLOAT_WITHIN(0.05f, 0.83f, d.utilizacionPct(30 * 60000));
}

void test_duty_minutos_para_liberar() {
  CicloTrabajo d;
  TEST_ASSERT_EQUAL_UINT32(0, d.minutosParaLiberar(0));

  d.registrar(0, 400000);                        // pasado de vueltas
  const uint32_t m = d.minutosParaLiberar(1000);
  TEST_ASSERT_TRUE(m > 0 && m <= 60);

  // Justo despues de que caduque, ya no hay espera.
  TEST_ASSERT_EQUAL_UINT32(0, d.minutosParaLiberar(61u * 60u * 1000u));
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_historial_detecta_duplicados);
  RUN_TEST(test_historial_ignora_id_cero);
  RUN_TEST(test_historial_desaloja_el_mas_antiguo);
  RUN_TEST(test_historial_anota_relevos);
  RUN_TEST(test_difusion_se_entrega_Y_se_retransmite);
  RUN_TEST(test_duplicado_se_descarta);
  RUN_TEST(test_unicast_para_mi_solo_se_entrega);
  RUN_TEST(test_unicast_para_otro_solo_se_releva);
  RUN_TEST(test_sin_saltos_no_se_retransmite);
  RUN_TEST(test_nodo_mudo_no_retransmite);
  RUN_TEST(test_ack_implicito);
  RUN_TEST(test_eco_propio_desconocido_se_descarta);
  RUN_TEST(test_id_cero_se_descarta);
  RUN_TEST(test_next_hop_ajeno_no_se_releva);
  RUN_TEST(test_preparar_relevo);
  RUN_TEST(test_slot_time);
  RUN_TEST(test_ventana_de_contencion);
  RUN_TEST(test_retardo_de_relevo_lleva_desplazamiento);
  RUN_TEST(test_duty_vacio_permite_todo);
  RUN_TEST(test_duty_acumula_y_bloquea);
  RUN_TEST(test_duty_ventana_rodante);
  RUN_TEST(test_duty_reparte_en_cubos);
  RUN_TEST(test_duty_minutos_para_liberar);
  return UNITY_END();
}
