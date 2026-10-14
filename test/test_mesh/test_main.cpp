// Tests del router de inundacion y del contador de ciclo de trabajo.
// No requieren hardware:  pio test -e native

#include <unity.h>
#include <string.h>

#include "mesh/duty.h"
#include "mesh/history.h"
#include "mesh/pending.h"
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

// ========================= mensajes directos ================================

// A retry of a direct message we already delivered is NOT delivered again, but
// it IS acknowledged again: the first ACK got lost on the way back.
void test_reintento_de_directo_se_reacusa() {
  Historial h;
  auto c = hacer(YO, OTRO, 40);
  c.want_ack = true;
  TEST_ASSERT_EQUAL_INT((int)Accion::ENTREGAR, (int)decidir(c, YO, h, true));
  TEST_ASSERT_EQUAL_INT((int)Accion::REACUSAR, (int)decidir(c, YO, h, true));
}

// Only for a direct message to us that asked for an ACK. A duplicated broadcast,
// or a direct message for someone else, is still just dropped.
void test_reacusar_solo_si_es_para_mi_y_lo_pide() {
  Historial h;
  auto sin_ack = hacer(YO, OTRO, 41);
  decidir(sin_ack, YO, h, true);
  TEST_ASSERT_EQUAL_INT((int)Accion::DESCARTAR, (int)decidir(sin_ack, YO, h, true));

  auto difusion = hacer(BCAST, OTRO, 42);
  difusion.want_ack = true;
  decidir(difusion, YO, h, true);
  TEST_ASSERT_EQUAL_INT((int)Accion::DESCARTAR, (int)decidir(difusion, YO, h, true));

  // For someone else, and already relayed once: dropped. (A copy straight
  // from its sender is relayed again: see the next test.)
  auto ajeno = hacer(0x11223344, OTRO, 43, 2);
  ajeno.want_ack = true;
  decidir(ajeno, YO, h, true);
  TEST_ASSERT_EQUAL_INT((int)Accion::DESCARTAR, (int)decidir(ajeno, YO, h, true));
}

// A -> relay -> B. When A retries (same id), the relay has already seen it and
// would drop it, so no retry could ever cross a relay. A copy straight from A
// (hop_limit == hop_start) is relayed again; a copy that already went through a
// relay (lower hop_limit) is not, so this cannot loop.
void test_reintento_directo_para_otro_se_vuelve_a_relevar() {
  Historial h;
  const uint32_t DESTINO = 0x11223344;
  auto c = hacer(DESTINO, OTRO, 50, 3);        // hop_start is 3 in hacer()
  c.want_ack = true;
  TEST_ASSERT_EQUAL_INT((int)Accion::RETRANSMITIR, (int)decidir(c, YO, h, true));
  TEST_ASSERT_EQUAL_INT((int)Accion::RETRANSMITIR, (int)decidir(c, YO, h, true));

  auto relevado = hacer(DESTINO, OTRO, 50, 2);  // already went through a relay
  relevado.want_ack = true;
  TEST_ASSERT_EQUAL_INT((int)Accion::DESCARTAR, (int)decidir(relevado, YO, h, true));

  // Without want_ack nobody retries, so a duplicate is just a duplicate.
  auto sin_ack = hacer(DESTINO, OTRO, 51, 3);
  decidir(sin_ack, YO, h, true);
  TEST_ASSERT_EQUAL_INT((int)Accion::DESCARTAR, (int)decidir(sin_ack, YO, h, true));

  // A mute node does not relay retries either.
  Historial h2;
  decidir(c, YO, h2, false);
  TEST_ASSERT_EQUAL_INT((int)Accion::DESCARTAR, (int)decidir(c, YO, h2, false));
}

// A relay's copy of the same try arrives seconds after the original: no second
// ACK. A retry arrives after the ACK timeout: acknowledged again.
void test_acuses_copia_no_reintento_si() {
  Acuses a;
  a.anotar(OTRO, 60, 1000);
  TEST_ASSERT_FALSE(a.reacusar(OTRO, 60, 2500, DM_REACK_MIN_MS));     // relay copy
  TEST_ASSERT_TRUE(a.reacusar(OTRO, 60, 1000 + DM_ACK_TIMEOUT_MS, DM_REACK_MIN_MS));
  // ...and that retry's own relay copy is not acknowledged twice either.
  TEST_ASSERT_FALSE(a.reacusar(OTRO, 60, 2000 + DM_ACK_TIMEOUT_MS, DM_REACK_MIN_MS));
  // Never acknowledged: the first try did not decrypt (another key). Its retry
  // must not be confirmed either, or "delivered" would lie.
  TEST_ASSERT_FALSE(a.reacusar(YO, 61, 50000, DM_REACK_MIN_MS));
}

void test_pendiente_confirmado_no_se_reintenta() {
  Pendientes p;
  const uint8_t pkt[] = {1, 2, 3};
  TEST_ASSERT_TRUE(p.anadir(77, pkt, sizeof(pkt), 1000, 5000));
  TEST_ASSERT_TRUE(p.confirmar(77));
  TEST_ASSERT_FALSE(p.confirmar(77));                 // ya no estaba
  TEST_ASSERT_EQUAL_INT((int)Pendientes::Tipo::NADA,
                        (int)p.revisar(100000, 5000).tipo);
}

void test_pendiente_reintenta_y_se_agota() {
  Pendientes p;
  const uint8_t pkt[] = {9, 8, 7, 6};
  p.anadir(55, pkt, sizeof(pkt), 1000, 5000);

  // Before the timeout: nothing.
  TEST_ASSERT_EQUAL_INT((int)Pendientes::Tipo::NADA, (int)p.revisar(5999, 5000).tipo);

  // Each timeout without ACK: the SAME bytes again, rescheduled.
  uint32_t t = 6000;
  for (int i = 0; i < DM_RETRIES; i++) {
    const auto r = p.revisar(t, 5000);
    TEST_ASSERT_EQUAL_INT((int)Pendientes::Tipo::REINTENTAR, (int)r.tipo);
    TEST_ASSERT_EQUAL_UINT32(55, r.id);
    TEST_ASSERT_EQUAL_size_t(sizeof(pkt), r.len);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(pkt, r.datos, sizeof(pkt));
    TEST_ASSERT_EQUAL_INT((int)Pendientes::Tipo::NADA, (int)p.revisar(t + 1, 5000).tipo);
    t += 5000;
  }

  // Out of retries: reported once and removed.
  const auto fin = p.revisar(t, 5000);
  TEST_ASSERT_EQUAL_INT((int)Pendientes::Tipo::AGOTADO, (int)fin.tipo);
  TEST_ASSERT_EQUAL_UINT32(55, fin.id);
  TEST_ASSERT_EQUAL_size_t(0, p.ocupacion());
  TEST_ASSERT_EQUAL_INT((int)Pendientes::Tipo::NADA, (int)p.revisar(t + 99999, 5000).tipo);
}

// millis() wraps after 49 days. A timeout straddling the wrap must still fire
// on time, not 49 days later.
void test_pendiente_sobrevive_al_desbordamiento_de_millis() {
  Pendientes p;
  const uint8_t pkt[] = {1};
  p.anadir(3, pkt, 1, 0xFFFFF000u, 0x2000);       // due at 0x00001000
  TEST_ASSERT_EQUAL_INT((int)Pendientes::Tipo::NADA, (int)p.revisar(0xFFFFFF00u, 0x2000).tipo);
  TEST_ASSERT_EQUAL_INT((int)Pendientes::Tipo::REINTENTAR, (int)p.revisar(0x1000, 0x2000).tipo);
}

void test_pendientes_llenos_no_desalojan() {
  Pendientes p;
  const uint8_t pkt[] = {1};
  for (uint32_t i = 1; i <= DM_PENDING_MAX; i++) TEST_ASSERT_TRUE(p.anadir(i, pkt, 1, 0, 100));
  TEST_ASSERT_FALSE(p.anadir(999, pkt, 1, 0, 100));
  TEST_ASSERT_FALSE(p.anadir(0, pkt, 1, 0, 100));   // id 0 never
  TEST_ASSERT_EQUAL_size_t(DM_PENDING_MAX, p.ocupacion());
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
  RUN_TEST(test_reintento_de_directo_se_reacusa);
  RUN_TEST(test_reacusar_solo_si_es_para_mi_y_lo_pide);
  RUN_TEST(test_reintento_directo_para_otro_se_vuelve_a_relevar);
  RUN_TEST(test_acuses_copia_no_reintento_si);
  RUN_TEST(test_pendiente_confirmado_no_se_reintenta);
  RUN_TEST(test_pendiente_reintenta_y_se_agota);
  RUN_TEST(test_pendiente_sobrevive_al_desbordamiento_de_millis);
  RUN_TEST(test_pendientes_llenos_no_desalojan);
  return UNITY_END();
}
