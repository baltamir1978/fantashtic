#include "ui/ui.h"

#include <M5Cardputer.h>
#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "app/app.h"
#include "config.h"
#include "hal/antenna.h"
#include "hal/gps.h"
#include "hal/power.h"
#include "hal/sound.h"
#include "radio/radio.h"
#include "store/store.h"
#include "strings.h"
#include "util/geo.h"
#include "util/utf8.h"
#include "util/wrap.h"

#define D M5Cardputer.Display

namespace ui {
namespace {

// Colores por nodo, elegidos de forma determinista a partir del NodeNum: el
// mismo aparato sale siempre del mismo color en todas las pantallas.
// RGB565: ambar, verde agua, rosa, azul, amarillo, verde.
const uint16_t COLOR_NODO[] = {0xFD20, 0x5FEB, 0xFB56, 0x861F, 0xFFE0, 0x87F0};
constexpr size_t N_COLORES = sizeof(COLOR_NODO) / sizeof(COLOR_NODO[0]);

uint16_t colorDe(uint32_t nodo) {
  if (nodo == 0) return 0x07FF;                 // el nuestro, cian
  return COLOR_NODO[(nodo ^ (nodo >> 16)) % N_COLORES];
}

ft_screen_t g_pant = SCR_CHAT;
bool     g_encendida = true;
uint32_t g_ultima_tecla = 0;
uint32_t g_ultimo_pintado = 0;

// Entrada de texto.
char g_linea[MSG_MAX_BYTES + 1] = {};
int  g_linea_n = 0;

// Desplazamiento del historial: 0 = pegado al final.
int  g_scroll = 0;

// Conversation on screen: 0 = the whole group, otherwise the NodeNum of the
// other side of a direct chat. It is also who ENTER sends to, so what is on
// screen and where the message goes can never disagree.
uint32_t g_conv = 0;

// Seleccion en listas, y primera fila visible de la ventana. Las listas tienen
// mas entradas que filas en pantalla (16 nodos, 7 ajustes), asi que la fila
// resaltada y la primera pintada son dos cosas distintas.
int  g_sel = 0;
int  g_top = 0;

char g_aviso[40] = {};

// Asistente de primer arranque.
int  g_paso_asistente = 0;   // 0 nombre, 1 frase, 2 derivando

// --- edicion de los ajustes -------------------------------------------------
// Campo que se esta editando, o NINGUNO. Mientras haya campo abierto, el editor
// se come todas las teclas y se pinta por encima de la pantalla de ajustes.
enum class Campo : uint8_t { NINGUNO = 0, NOMBRE, CORTO, FRASE };
Campo g_campo       = Campo::NINGUNO;
bool  g_derivando   = false;   // PBKDF2 bloquea ~0,5 s y hay que avisar antes
char  g_edit[65]    = {};      // la frase admite 64 bytes; los nombres, menos
int   g_edit_n      = 0;

// Accion delicada esperando confirmacion. La restriccion nº 8 pide que a ningun
// estado roto se llegue de un solo golpe, y estas cinco filas pueden dejar el
// aparato incomunicado o sin configuracion.
enum class Confirmar : uint8_t {
  NADA = 0, FRASE, PRESET, POTENCIA, SALTOS, FABRICA
};
Confirmar g_confirmar = Confirmar::NADA;

// Valor propuesto, a la espera de que se confirme. Se muestra en la pregunta:
// confirmar a ciegas "¿seguro?" sin decir a que se va a cambiar no es confirmar.
uint8_t g_pendiente = 0;

// Potencias ofrecidas, de mas a menos. 22 dBm es el maximo del SX1262 y el que
// da ~193 mW p.r.e. con la antena de 3 dBi incluida, dentro de los 500 mW de la
// subbanda g3.
const int8_t POTENCIAS[] = {22, 17, 14, 10, 5, 2};
constexpr int N_POTENCIAS = sizeof(POTENCIAS) / sizeof(POTENCIAS[0]);

const char* nombrePreset(uint8_t p) {
  switch (p) {
    case 1:  return S_PRESET_1;
    case 2:  return S_PRESET_2;
    default: return S_PRESET_0;
  }
}

// Pantalla de informacion. NO entra en la rotacion del TAB: se abre desde
// ajustes y se sale con ESC. Anadirla a ft_screen_t la habria metido en el ciclo.
bool g_info = false;

// ---------------------------------------------------------------------------

void nombreCorto(uint32_t nodo, char* out, size_t cap) {
  if (nodo == 0) { snprintf(out, cap, "%s", store::config().nombre_corto); return; }
  const auto* n = app::nodos().buscar(nodo);
  if (n) app::nodos().nombreMostrar(*n, out, cap);
  else   snprintf(out, cap, "%04X", (unsigned)(nodo & 0xFFFF));
}

// Name of a conversation: "TODOS" for the group, the short name otherwise.
void nombreConv(uint32_t conv, char* out, size_t cap) {
  if (conv == 0) snprintf(out, cap, "%s", S_TODOS);
  else           nombreCorto(conv, out, cap);
}

// Next or previous conversation: the group, then every node in the table, in
// table order, wrapping around. A conversation whose node left the table is
// treated as the group, so the arrows always lead somewhere sensible.
uint32_t convVecina(uint32_t actual, int paso) {
  uint32_t lista[MAX_NODES + 1];
  size_t n = 0, pos = 0;
  lista[n++] = 0;
  for (size_t i = 0; i < app::nodos().capacidad(); i++) {
    const auto* nd = app::nodos().porIndice(i);
    if (!nd) continue;
    if (nd->num == actual) pos = n;
    lista[n++] = nd->num;
  }
  return lista[(pos + n + (size_t)(paso > 0 ? 1 : n - 1)) % n];
}

void barraEstado() {
  D.fillRect(0, 0, TFT_W, UI_STATUSBAR_H, 0x2104);
  D.setTextSize(1);
  D.setTextDatum(middle_left);

  // Nombre propio, en su color.
  D.setTextColor(colorDe(0));
  D.drawString(store::config().nombre_corto, 3, UI_STATUSBAR_H / 2);

  int x = 42;
  // Calidad del ultimo enlace, en cuatro puntos.
  const int snr = (int)radio::ultimoSnr();
  const int barras = snr >= 5 ? 4 : snr >= 0 ? 3 : snr >= -8 ? 2 : snr >= -15 ? 1 : 0;
  for (int i = 0; i < 4; i++) {
    D.fillCircle(x + i * 6, UI_STATUSBAR_H / 2, 2,
                 i < barras ? TFT_WHITE : 0x4208);
  }
  x += 28;

  // Cuantos del grupo siguen al alcance. Antes era un solo numero con ventana de
  // 15 minutos, o sea que un nodo podia llevar un cuarto de hora incomunicado y
  // seguir contando: en ciudad eso es enterarse tarde. Ahora es "conectados de
  // total" con el criterio del latido (3 minutos), y el color lo pone el PEOR
  // enlace de la tabla, que es lo que hay que mirar de reojo.
  char b[32];
  const uint32_t ahora = millis();
  const size_t vivos = app::nodos().conectados(ahora);
  // El denominador son los vistos en la ultima hora, no los de la tabla: si no,
  // un aparato apagado a proposito dejaria un "3/4" rojo para siempre, y una
  // alarma que no se apaga nunca acaba enseñando a ignorar las alarmas.
  const size_t total = app::nodos().activos(ahora, LINK_FORGET_MS);
  snprintf(b, sizeof(b), "%u/%u", (unsigned)vivos, (unsigned)total);
  const mesh::Enlace peor = app::nodos().peorEnlace(ahora, LINK_FORGET_MS);
  D.setTextColor(total == 0            ? 0x6B4D
                 : peor == mesh::Enlace::PERDIDO ? TFT_RED
                 : peor == mesh::Enlace::DEBIL   ? TFT_ORANGE
                 : peor == mesh::Enlace::RELEVO  ? TFT_YELLOW
                                                 : TFT_GREEN);
  D.drawString(b, x, UI_STATUSBAR_H / 2); x += 22;

  // Ciclo de trabajo: siempre visible. Si un dia bloquea la transmision, debe
  // quedar claro que es el limite legal y no una averia.
  const float duty = radio::dutyPct();
  snprintf(b, sizeof(b), "%.0f%%", (double)duty);
  D.setTextColor(duty >= DUTY_HARD_PCT ? TFT_RED
                 : duty >= DUTY_SOFT_PCT ? TFT_ORANGE : 0x8410);
  D.drawString(b, x, UI_STATUSBAR_H / 2); x += 26;

  if (store::config().gps_activo) {
    D.setTextColor(hal::gpsValido() ? TFT_GREEN : 0x6B4D);
    D.drawString(hal::gpsValido() ? S_GPS_OK : S_GPS_NO, x, UI_STATUSBAR_H / 2);
  }
  x += 24;
  if (!store::hayTarjeta()) {
    D.setTextColor(TFT_ORANGE);
    D.drawString(S_SIN_SD, x, UI_STATUSBAR_H / 2);
  }

  const uint8_t bat = hal::batteryPct();
  snprintf(b, sizeof(b), "%u%%", (unsigned)bat);
  D.setTextDatum(middle_right);
  D.setTextColor(bat < BAT_LOW_PCT ? TFT_RED : TFT_LIGHTGREY);
  D.drawString(b, TFT_W - 3, UI_STATUSBAR_H / 2);
  D.setTextDatum(top_left);
}

// Alto que ocupa el aviso, para que el resto de la pantalla se aparte en vez
// de quedar pisado por debajo. Sin tarjeta el aviso es permanente, asi que no
// es un caso raro: seria la mitad inferior de cada letra que se teclea.
int altoAviso() { return g_aviso[0] ? UI_FOOTER_H : 0; }

void pieAviso() {
  if (!g_aviso[0]) return;
  // Font0 a la fuerza. El aviso vive en una banda de 11 px y es lo ultimo que
  // pinta cada pantalla, asi que llega con la fuente que dejara la pantalla:
  // con Font2 (16 px) activa se salia por abajo y quedaba recortado.
  D.setFont(&fonts::Font0);
  D.fillRect(0, TFT_H - UI_FOOTER_H, TFT_W, UI_FOOTER_H, 0x5000);
  D.setTextColor(TFT_WHITE);
  D.drawString(g_aviso, 3, TFT_H - UI_FOOTER_H + 1);
}

// Filas de 'alto_fila' px que caben entre la barra de estado y el pie.
int filasQueCaben(int alto_fila) {
  const int alto = TFT_H - (UI_STATUSBAR_H + 2) - UI_FOOTER_H - altoAviso();
  const int n = alto / alto_fila;
  return n < 1 ? 1 : n;
}

// Ventana deslizante de una lista: mueve 'top' lo justo para que 'sel' quede a
// la vista. La comparten nodos y ajustes para que no pueda repetirse el fallo
// de pintar 8 filas dejando seleccionables 16.
void ajustarVentana(int sel, int total, int visibles, int& top) {
  if (total <= visibles)          { top = 0; return; }
  if (sel < top)                    top = sel;
  else if (sel >= top + visibles)   top = sel - visibles + 1;
  if (top > total - visibles)       top = total - visibles;
  if (top < 0)                      top = 0;
}

// Pie de las listas: la ayuda a la izquierda y la posicion a la derecha. Ese
// "4/12" es lo unico que delata que hay filas fuera de la ventana; sin el, una
// lista recortada parece completa.
void pieLista(const char* ayuda, int sel, int total, int visibles) {
  D.setFont(&fonts::Font0);
  const int y = TFT_H - 10 - altoAviso();
  D.setTextDatum(top_left);
  D.setTextColor(0x6B4D);
  D.drawString(ayuda, 4, y);
  if (total > visibles) {
    char b[16];
    snprintf(b, sizeof(b), "%d/%d", sel + 1, total);
    D.setTextDatum(top_right);
    D.setTextColor(TFT_ORANGE);
    D.drawString(b, TFT_W - 4, y);
    D.setTextDatum(top_left);
  }
}

// Trozo FINAL de 's' que cabe en 'ancho' px con la fuente activa. Al escribir
// interesa ver lo ultimo tecleado, no el principio: la frase admite 64
// caracteres y en Font2 eso es mas del doble del ancho de la pantalla.
const char* colaQueCabe(const char* s, int ancho) {
  const char* p = s;
  while (*p && (int)D.textWidth(p) > ancho) {
    const uint8_t l = util::longitudCaracter((uint8_t)*p);
    p += l ? l : 1;            // sin el '? :' un byte invalido cuelga el bucle
  }
  return p;
}

// ------------------------------- chat --------------------------------------

// Columna donde empieza el texto, a la derecha del nombre corto y de la marca
// de estado. Las lineas de continuacion se alinean con ella: es lo que hace que
// un mensaje largo se lea como un parrafo y no como tres mensajes distintos.
constexpr int CHAT_TEXTO_X = 58;
constexpr int CHAT_TEXTO_W = TFT_W - CHAT_TEXTO_X - 4;

// Tope de lineas por mensaje. Con Font2, los caracteres mas anchos dan unos 12
// por linea, asi que 180 bytes no pasan de ~15. 16 deja margen y acota el
// buffer de la pila, que aqui importa: no hay PSRAM.
constexpr size_t CHAT_LINEAS_MSG = 16;

// Tope de lineas pintables a la vez. chatVisibles() da 4 con la pantalla
// entera; 8 sobra y evita que un cambio de fuente o de altura desborde.
constexpr int CHAT_LINEAS_MAX = 8;

// Lineas que caben en el chat. El numero de lineas que se pintan y el tope del
// scroll TIENEN que salir los dos de aqui: al pasar el chat a Font2 se bajo a 4
// lineas pero el tope se quedo en 5, y eso dejaba el mensaje mas viejo del
// historial fuera de alcance (con 6 mensajes el scroll se topaba en 1 cuando
// hacian falta 2). Se descuentan los 22 px de la linea de entrada.
int chatVisibles() {
  const int alto = TFT_H - (UI_STATUSBAR_H + 2) - 22 - altoAviso();
  const int n = alto / UI_ROW_H;
  if (n < 1) return 1;
  return n > CHAT_LINEAS_MAX ? CHAT_LINEAS_MAX : n;
}

// Ancho real del texto. Font2 es PROPORCIONAL, asi que el ancho se mide y no se
// cuenta: los "22 caracteres" de antes eran una cuenta sobre 8 px por caracter,
// y con eso "MMMM..." se salia de la pantalla y "iiii..." dejaba media fila
// vacia. Solo vale con Font2 activa, que es lo que deja puesto pantallaChat().
int medirFont2(const char* s, size_t bytes, void*) {
  char b[MSG_BUF_BYTES + 1];
  if (bytes > sizeof(b) - 1) bytes = sizeof(b) - 1;
  memcpy(b, s, bytes);
  b[bytes] = '\0';
  return (int)D.textWidth(b);
}

// Una linea pintable: de que mensaje sale y que trozo de su texto.
struct LineaChat {
  const store::Mensaje* m;
  uint16_t ini;
  uint16_t len;
  bool     primera;   // lleva el nombre del remitente y la marca de estado
};

// Rellena 'out' con las lineas visibles, de la mas NUEVA a la mas vieja
// (out[0] es la de abajo del todo). 'saltar' son lineas de historial hacia
// atras: desde que un mensaje ocupa varias filas, el scroll cuenta lineas y no
// mensajes, que es lo que hace que un mensaje largo se pueda leer entero.
//
// Devuelve cuantas ha llenado, y en 'hay_mas' si quedaba algo por encima. Ese
// dato es el tope del scroll: calcularlo aparte obligaria a envolver los 60
// mensajes del historial en cada pulsacion de tecla.
int componerChat(int saltar, int visibles, LineaChat* out, bool* hay_mas) {
  int n = 0;
  *hay_mas = false;

  for (int idx = (int)store::numMensajes() - 1; idx >= 0; idx--) {
    const auto* m = store::mensaje((size_t)idx);
    if (!m || m->conv != g_conv) continue;

    util::Trozo tr[CHAT_LINEAS_MSG];
    const int k = (int)util::envolver(m->texto, CHAT_TEXTO_W, medirFont2,
                                      nullptr, tr, CHAT_LINEAS_MSG);
    for (int j = k - 1; j >= 0; j--) {
      if (saltar > 0) { saltar--; continue; }
      if (n >= visibles) { *hay_mas = true; return n; }
      out[n].m       = m;
      out[n].ini     = tr[j].ini;
      out[n].len     = tr[j].len;
      out[n].primera = (j == 0);
      n++;
    }
  }
  return n;
}

// Si queda historial por encima de lo que se ve. Lo usa la tecla ARRIBA para
// no dejar que el scroll se pase del principio.
bool chatHayMasArriba() {
  LineaChat lin[CHAT_LINEAS_MAX];
  bool hay_mas = false;
  D.setFont(&fonts::Font2);   // se mide con la fuente con la que se pinta
  componerChat(g_scroll, chatVisibles(), lin, &hay_mas);
  return hay_mas;
}

void pantallaChat() {
  barraEstado();

  // Font2 (16 px) en el chat: Font0 (6x8) es ilegible para los niños, que son
  // parte del publico objetivo. La barra de estado se queda en Font0, donde la
  // densidad importa mas que el tamaño. Cambiar la fuente cambia el alto de
  // linea, asi que caben 4 lineas en vez de 5.
  D.setFont(&fonts::Font2);

  const int visibles = chatVisibles();
  int y = UI_STATUSBAR_H + 2;
  const int dy = UI_ROW_H;

  // On screen means read: this is what clears the unread marker.
  store::marcarLeidos(g_conv);

  LineaChat lin[CHAT_LINEAS_MAX];
  bool hay_mas = false;
  const int n = componerChat(g_scroll, visibles, lin, &hay_mas);

  if (n == 0) {
    D.setTextColor(0x6B4D);
    D.drawString(S_SIN_MENSAJES, 6, y);
  }

  // Se compone de abajo arriba y se pinta al reves, para que las lineas queden
  // pegadas al final del historial aunque el ultimo mensaje ocupe cuatro filas.
  for (int i = n - 1; i >= 0; i--) {
    const LineaChat& L = lin[i];

    // El nombre y la marca solo en la primera linea del mensaje: repetirlos en
    // cada fila haria que un mensaje largo pareciera varios seguidos.
    if (L.primera) {
      char nom[8];
      nombreCorto(L.m->nodo, nom, sizeof(nom));
      D.setTextColor(L.m->sos ? TFT_RED : colorDe(L.m->nodo));
      D.drawString(nom, 4, y);

      // "." sending, "+" someone relayed it, a tick when the recipient of a
      // direct message confirmed it, "!" failed. Nothing for our messages
      // reloaded from the card: their state then was not saved.
      if (L.m->nodo == 0 && L.m->estado == store::EST_ENTREGADO) {
        // Drawn, not a glyph: Font2 has no check mark.
        for (int k = 0; k < 2; k++) {
          D.drawLine(46, y + 8 + k, 49, y + 11 + k, TFT_GREEN);
          D.drawLine(49, y + 11 + k, 54, y + 4 + k, TFT_GREEN);
        }
      } else if (L.m->nodo == 0 && L.m->estado != store::EST_ARCHIVO) {
        const char* marca = L.m->estado == store::EST_PROPAGADO ? "+"
                            : L.m->estado == store::EST_FALLIDO ? "!" : ".";
        D.setTextColor(L.m->estado == store::EST_PROPAGADO ? TFT_GREEN
                       : L.m->estado == store::EST_FALLIDO ? TFT_RED : 0x6B4D);
        D.drawString(marca, 46, y);
      }
    }

    char txt[MSG_BUF_BYTES];
    size_t len = L.len;
    if (len > sizeof(txt) - 1) len = sizeof(txt) - 1;
    memcpy(txt, L.m->texto + L.ini, len);
    txt[len] = '\0';

    D.setTextColor(L.m->sos ? TFT_RED : TFT_WHITE);
    D.drawString(txt, CHAT_TEXTO_X, y);
    y += dy;
  }

  // Linea de entrada, o indicador de que estamos mirando atras. Se sube si hay
  // aviso, para que no la tape.
  const int base = TFT_H - 22 - altoAviso();
  D.fillRect(0, base, TFT_W, 22, 0x2104);
  if (g_scroll > 0) {
    D.setTextColor(TFT_ORANGE);
    char b[40];
    snprintf(b, sizeof(b), "%s -%d   %s", S_HISTORIAL, g_scroll, S_VOLVER_FIN);
    D.drawString(b, 4, base + 5);
  } else {
    // Who this goes to, in their colour, right where the typing happens: a
    // direct message sent to the whole group by mistake is exactly the kind of
    // slip a child will not notice afterwards.
    char para[8];
    nombreConv(g_conv, para, sizeof(para));
    D.setTextColor(g_conv ? colorDe(g_conv) : TFT_LIGHTGREY);
    D.drawString(para, 3, base + 5);
    const int x_txt = 3 + (int)D.textWidth(para) + 2;

    D.setTextColor(TFT_WHITE);
    // Se muestra la COLA de lo escrito, no el principio: al pasar de una linea,
    // un snprintf a 45 bytes dejaba el cursor fuera de la pantalla y se escribia
    // a ciegas. Se reserva sitio para la cuenta de bytes y el aviso de la derecha.
    char cur[MSG_MAX_BYTES + 3];
    snprintf(cur, sizeof(cur), ">%s_", g_linea);
    D.drawString(colaQueCabe(cur, TFT_W - 40 - x_txt), x_txt, base + 5);

    // With nothing typed, the hint on how to reach the other chats. Only when
    // there is somewhere else to go.
    if (g_linea_n == 0 && app::nodos().total() > 0) {
      D.setTextColor(0x6B4D);
      D.drawString(S_CHAT_CAMBIAR, x_txt + (int)D.textWidth(">_") + 6, base + 5);
    }

    // Unread messages in ANOTHER conversation: an orange dot by the counter.
    if (store::noLeidosTotal() > store::noLeidos(g_conv))
      D.fillCircle(TFT_W - 32, base + 11, 3, TFT_ORANGE);

    D.setTextDatum(top_right);
    D.setTextColor(g_linea_n > MSG_MAX_BYTES - 20 ? TFT_ORANGE : 0x6B4D);
    char c[8];
    snprintf(c, sizeof(c), "%d", g_linea_n);
    D.drawString(c, TFT_W - 3, base + 5);
    D.setTextDatum(top_left);
  }
  pieAviso();
}

// ------------------------------- nodos -------------------------------------

// Nodo que ocupa la fila 'idx' de la lista. La tabla tiene huecos, asi que el
// indice de la pantalla y el de la tabla no son el mismo numero. Lo comparten
// la pantalla de nodos y el salto a la brujula, para que no se puedan
// desincronizar: el bucle estaba escrito dos veces.
const mesh::Nodo* nodoDeFila(int idx) {
  if (idx < 0) return nullptr;
  int fila = 0;
  for (size_t i = 0; i < app::nodos().capacidad(); i++) {
    const auto* n = app::nodos().porIndice(i);
    if (!n) continue;
    if (fila++ == idx) return n;
  }
  return nullptr;
}

void pantallaNodos() {
  barraEstado();

  // Font2 (16 px) para el nombre y la distancia, que es lo que un niño mira, y
  // Font0 para el detalle. Mezclar las dos fuentes en la misma fila no es
  // capricho: a 8 px por caracter no caben seis campos en 240 px de ancho.
  const int dy       = UI_ROW_H;
  const int visibles = filasQueCaben(dy);
  const int total    = (int)app::nodos().total();
  ajustarVentana(g_sel, total, visibles, g_top);

  int y    = UI_STATUSBAR_H + 2;
  int fila = 0;                        // indice entre los nodos presentes

  for (size_t i = 0; i < app::nodos().capacidad(); i++) {
    const auto* n = app::nodos().porIndice(i);
    if (!n) continue;                  // porIndice devuelve nullptr en los huecos
    const int idx = fila++;
    if (idx < g_top) continue;
    if (idx >= g_top + visibles) break;

    const int cy = y + dy / 2;
    if (idx == g_sel) D.fillRect(0, y, TFT_W, dy, 0x1082);

    D.setFont(&fonts::Font2);
    D.setTextDatum(middle_left);
    char nom[8];
    app::nodos().nombreMostrar(*n, nom, sizeof(nom));
    D.setTextColor(colorDe(n->num));
    D.drawString(nom, 4, cy);

    // Distancia a la derecha, si los dos tenemos posicion.
    char b[16];
    if (n->tiene_pos && hal::gpsValido()) {
      const uint32_t d = util::distanciaM(hal::gpsFix().lat_e7, hal::gpsFix().lon_e7,
                                          n->lat_e7, n->lon_e7);
      util::formatearDistancia(d, b, sizeof(b));
      D.setTextDatum(middle_right);
      D.setTextColor(TFT_WHITE);
      D.drawString(b, TFT_W - 4, cy);
    }

    // Detalle secundario. El nombre viene de nombreMostrar(), que devuelve 4
    // caracteres como mucho, pero en Font2 esos 4 ya rondan los 40 px: la
    // columna del detalle se coloca midiendo, no a ojo.
    int x = 4 + (int)D.textWidth(nom) + 6;
    if (x < 46) x = 46;
    D.setFont(&fonts::Font0);
    D.setTextDatum(middle_left);

    // Estado del enlace: manda sobre el color de esta fila entera, porque es lo
    // que hay que ver sin leer.
    const uint32_t ahora = millis();
    const mesh::Enlace enl = mesh::estadoEnlace(*n, ahora);
    const uint16_t col_enl = enl == mesh::Enlace::PERDIDO ? TFT_RED
                             : enl == mesh::Enlace::DEBIL   ? TFT_ORANGE
                             : enl == mesh::Enlace::RELEVO  ? TFT_YELLOW
                                                            : TFT_WHITE;

    // Hace cuanto se oyo.
    const uint32_t hace_s = (ahora - n->visto_ms) / 1000;
    if (hace_s < 60)        snprintf(b, sizeof(b), "%lus", (unsigned long)hace_s);
    else if (hace_s < 3600) snprintf(b, sizeof(b), "%lum", (unsigned long)(hace_s / 60));
    else                    snprintf(b, sizeof(b), "%luh", (unsigned long)(hace_s / 3600));
    D.setTextColor(enl == mesh::Enlace::DIRECTO ? TFT_LIGHTGREY : col_enl);
    D.drawString(b, x, cy);

    // Calidad del ultimo paquete oido, en el color del estado. Perdido el
    // enlace, los puntos se quedan en rojo con el ultimo valor conocido: es un
    // dato util (por donde iba cuando se fue) y no una medida de ahora.
    const int barras = n->snr >= 5 ? 4 : n->snr >= 0 ? 3 : n->snr >= -8 ? 2 : n->snr >= -15 ? 1 : 0;
    for (int k = 0; k < 4; k++)
      D.fillCircle(78 + k * 6, cy, 2, k < barras ? col_enl : 0x4208);

    // Hacia donde va la señal. Es la pregunta de ciudad: no importa tanto el
    // nivel de ahora como si esta subiendo o cayendo, porque es lo que dice si
    // hay que pararse a mandar el mensaje ya o se puede seguir andando.
    if (n->tendencia > 0) {
      D.fillTriangle(104, cy - 3, 100, cy + 2, 108, cy + 2, TFT_GREEN);
    } else if (n->tendencia < 0) {
      D.fillTriangle(104, cy + 3, 100, cy - 2, 108, cy - 2, TFT_RED);
    }

    if (n->saltos) {
      // "sal" y no "s": con "s" se confunde con los segundos de la columna de
      // al lado, que tambien sale como "3s".
      snprintf(b, sizeof(b), "%usal", (unsigned)n->saltos);
      // En amarillo cuando lo que llega viene por un tercero: ahi el "1sal" no
      // es un detalle tecnico, es el aviso de que la via directa se ha caido.
      D.setTextColor(enl == mesh::Enlace::RELEVO ? TFT_YELLOW : 0x8410);
      D.drawString(b, 112, cy);
    }

    if (n->bateria <= 100) {
      snprintf(b, sizeof(b), "%u%%", (unsigned)n->bateria);
      D.setTextColor(n->bateria < BAT_LOW_PCT ? TFT_RED : 0x8410);
      D.drawString(b, 140, cy);
    }

    // This node wrote to us privately and it has not been read yet.
    if (store::noLeidos(n->num)) D.fillCircle(172, cy, 3, TFT_ORANGE);

    y += dy;
  }

  D.setTextDatum(top_left);
  if (total == 0) {
    D.setFont(&fonts::Font2);
    D.setTextColor(0x6B4D);
    D.drawString(S_NADIE, 6, UI_STATUSBAR_H + 4);
  }

  // El estado del nodo seleccionado, en palabras. Los colores de la fila se
  // aprenden solos si en algun sitio se dice lo que significan, y aqui hay
  // hueco: el pie ya existe y solo llevaba la ayuda de la tecla.
  char ayuda[40];
  snprintf(ayuda, sizeof(ayuda), "%s", S_K_NODOS);
  const mesh::Nodo* sel = nodoDeFila(g_sel);
  if (sel) {
    const mesh::Enlace e = mesh::estadoEnlace(*sel, millis());
    snprintf(ayuda, sizeof(ayuda), "%s  %s",
             e == mesh::Enlace::PERDIDO ? S_LINK_PERDIDO
             : e == mesh::Enlace::DEBIL   ? S_LINK_DEBIL
             : e == mesh::Enlace::RELEVO  ? S_LINK_RELEVO
                                          : S_LINK_DIRECTO,
             S_K_NODOS);
  }
  pieLista(ayuda, g_sel, total, visibles);
  pieAviso();
}

// ------------------------------ brujula ------------------------------------

void pantallaBrujula() {
  barraEstado();
  const auto* n = app::nodos().buscar(app::nodoSeleccionado());

  D.setFont(&fonts::Font2);
  if (!n || !n->tiene_pos || !hal::gpsValido()) {
    D.setTextColor(0x6B4D);
    D.setTextDatum(middle_center);
    D.drawString(S_SIN_POSICION, TFT_W / 2, TFT_H / 2);
    D.setTextDatum(top_left);
    pieAviso();
    return;
  }

  const auto& f = hal::gpsFix();
  const uint32_t dist = util::distanciaM(f.lat_e7, f.lon_e7, n->lat_e7, n->lon_e7);
  const uint16_t rumbo = util::rumboGrados(f.lat_e7, f.lon_e7, n->lat_e7, n->lon_e7);

  // El BMI270 es un acelerometro, no una brujula: en parado no hay rumbo real,
  // asi que la flecha se orienta al norte geografico y se avisa.
  const bool en_movimiento = f.velocidad_kmh > 3.0f;
  const float giro = en_movimiento ? (rumbo - f.rumbo) : rumbo;

  // Nombre en Font2: es la cabecera de la pantalla y en Font0 son 24 px de
  // ancho en total, ilegibles a un brazo de distancia.
  char nom[8];
  app::nodos().nombreMostrar(*n, nom, sizeof(nom));
  D.setTextDatum(middle_center);
  D.setTextColor(colorDe(n->num));
  D.drawString(nom, TFT_W / 2, UI_STATUSBAR_H + 12);

  // Flecha.
  const int cx = TFT_W / 2, cy = 70, r = 22;
  const float a = giro * 3.14159265f / 180.0f;
  const int px = cx + (int)(r * sinf(a));
  const int py = cy - (int)(r * cosf(a));
  const int bx = cx - (int)(r * 0.6f * sinf(a));
  const int by = cy + (int)(r * 0.6f * cosf(a));
  D.drawCircle(cx, cy, r + 4, 0x4208);
  D.fillTriangle(px, py,
                 bx + (int)(8 * cosf(a)), by + (int)(8 * sinf(a)),
                 bx - (int)(8 * cosf(a)), by - (int)(8 * sinf(a)),
                 colorDe(n->num));

  // La distancia se queda en Font0 a tamaño 2 (12x16). No es un descuido: a esa
  // escala es MAS gorda que Font2 a tamaño 1, que son 8x16. Y Font2 a tamaño 2
  // serian 32 px de alto y se comeria la flecha.
  char b[24];
  util::formatearDistancia(dist, b, sizeof(b));
  D.setFont(&fonts::Font0);
  D.setTextSize(2);
  D.setTextColor(TFT_WHITE);
  D.drawString(b, TFT_W / 2, 108);
  D.setTextSize(1);

  D.setTextColor(0x6B4D);
  if (en_movimiento) snprintf(b, sizeof(b), "%s %u %s", S_RUMBO,
                              (unsigned)rumbo, util::puntoCardinal(rumbo));
  else               snprintf(b, sizeof(b), "%s", S_NORTE_ARRIBA);
  D.drawString(b, TFT_W / 2, TFT_H - 8 - altoAviso());
  D.setTextDatum(top_left);
  pieAviso();
}

// ----------------------------- mi posicion ---------------------------------

// El HDOP en palabras. El numero solo no le dice nada a nadie: los valores
// medidos en este aparato han ido de 0,6 junto a la ventana a 7,1 dentro de casa,
// y sin la palabra al lado ninguno de los dos significa nada para quien lo lee.
const char* palabraHdop(float h) {
  if (h < 1.0f) return S_HDOP_EXCELENTE;
  if (h < 2.0f) return S_HDOP_BUENO;
  if (h < 5.0f) return S_HDOP_ACEPTABLE;
  return S_HDOP_MALO;
}

uint16_t colorHdop(float h) {
  if (h < 2.0f) return TFT_WHITE;
  if (h < 5.0f) return TFT_YELLOW;
  return TFT_ORANGE;
}

// Pantalla de posicion propia: latitud, longitud, altitud, satelites y
// precision. No es solo diagnostico -- leer las coordenadas en voz alta por radio
// es un uso real en el campo, y para eso los numeros van en Font2 y las
// coordenadas con 5 decimales, que es ~1 m y es el formato que traga Google Maps.
//
// La mitad del valor de esta pantalla esta en el caso SIN fix: tiene que decir
// POR QUE no lo hay. El criterio para distinguir "cableado mal" de "sin cielo" es
// el MISMO que usan informeGps() y el aviso de la barra de estado (cero bytes
// NMEA = UART), a proposito: tres sitios con tres criterios distintos acabarian
// contradiciendose delante del usuario.
void pantallaPosicion() {
  barraEstado();

  const auto& f = hal::gpsFix();
  int y = UI_STATUSBAR_H + 4;
  const int dy = UI_ROW_H;
  char b[32];

  // Etiqueta en Font0 a la izquierda, valor en Font2 a la derecha. El +4 de la
  // etiqueta la centra contra los 16 px del valor.
  auto fila = [&](const char* etiqueta, const char* valor, uint16_t color) {
    D.setFont(&fonts::Font0);
    D.setTextColor(0x8410);
    D.drawString(etiqueta, 6, y + 4);
    D.setFont(&fonts::Font2);
    D.setTextColor(color);
    D.setTextDatum(top_right);
    D.drawString(valor, TFT_W - 6, y);
    D.setTextDatum(top_left);
    y += dy;
  };

  if (!hal::gpsEncendido()) {
    D.setFont(&fonts::Font2);
    D.setTextColor(0x6B4D);
    D.setTextDatum(middle_center);
    D.drawString(S_POS_APAGADO,   TFT_W / 2, TFT_H / 2 - 12);
    D.drawString(S_POS_EN_AJUSTES, TFT_W / 2, TFT_H / 2 + 10);
    D.setTextDatum(top_left);
    pieAviso();
    return;
  }

  if (!f.valido) {
    // Sin fix se muestra lo que SI se sabe: los bytes NMEA (que separan un UART
    // mal cableado de la falta de cobertura), los satelites a la vista y cuanto
    // lleva buscando. Es la informacion con la que se decide si salir al balcon o
    // mirar el cable.
    const uint32_t bytes = hal::gpsBytesRecibidos();
    const bool uart_mudo = (bytes == 0);

    D.setFont(&fonts::Font2);
    D.setTextColor(uart_mudo ? TFT_ORANGE : TFT_WHITE);
    D.drawString(uart_mudo ? S_POS_SIN_UART : S_POS_BUSCANDO, 6, y);
    y += dy;

    if (uart_mudo) {
      D.setFont(&fonts::Font0);
      D.setTextColor(TFT_YELLOW);
      D.drawString(S_POS_REVISA_UART, 6, y + 4);
      y += dy;
    } else if (f.satelites_vista) {
      snprintf(b, sizeof(b), "%u %s", (unsigned)f.satelites_vista, S_POS_AL_AIRE);
      fila(S_POS_SATS, b, TFT_WHITE);
    }

    snprintf(b, sizeof(b), "%lu B", (unsigned long)bytes);
    fila(S_POS_NMEA, b, uart_mudo ? TFT_ORANGE : 0x8410);

    snprintf(b, sizeof(b), "%lu s", (unsigned long)hal::gpsSegundosSinFix());
    fila(S_POS_DESDE, b, 0x8410);

    pieLista(S_K_TAB, 0, 1, 1);
    pieAviso();
    return;
  }

  snprintf(b, sizeof(b), "%.5f", (double)f.lat_e7 * 1e-7);
  fila(S_POS_LAT, b, TFT_WHITE);

  snprintf(b, sizeof(b), "%.5f", (double)f.lon_e7 * 1e-7);
  fila(S_POS_LON, b, TFT_WHITE);

  snprintf(b, sizeof(b), "%ld m", (long)f.altitud_m);
  fila(S_POS_ALT, b, TFT_WHITE);

  // "9 de 14" si el modulo manda GSV; si no, solo los usados. Nunca se etiqueta
  // como a la vista un numero que son los usados. Y se exige vista >= usados: al
  // arrancar llegan descoordinados y "9 de 4" quedaria absurdo.
  if (f.satelites_vista >= f.satelites && f.satelites_vista)
    snprintf(b, sizeof(b), "%u %s %u", (unsigned)f.satelites, S_POS_DE,
             (unsigned)f.satelites_vista);
  else
    snprintf(b, sizeof(b), "%u", (unsigned)f.satelites);
  fila(S_POS_SATS, b, TFT_WHITE);

  // hdop == 0 significa "el modulo no lo ha dado todavia", no "precision
  // perfecta": sin este caso la pantalla presumiria de excelente sin dato alguno.
  if (f.hdop > 0.0f) {
    snprintf(b, sizeof(b), "%.1f %s", (double)f.hdop, palabraHdop(f.hdop));
    fila(S_POS_PRECISION, b, colorHdop(f.hdop));
  } else {
    fila(S_POS_PRECISION, "?", 0x8410);
  }

  pieLista(S_K_TAB, 0, 1, 1);
  pieAviso();
}

// ------------------------------ ajustes ------------------------------------

// Escala de 1 a UI_NIVELES para brillo y volumen. Se guarda el valor real y el
// nivel se deriva, asi que no hay nada que migrar en NVS.
//
// De paso desaparece la trampa del desbordamiento: el valor se RECONSTRUYE desde
// un nivel ya acotado, en vez de sumarle 20 o 60 a lo que hubiera. Sumando,
// 90+20 = 110 -> 110*255/100 = 280, que como uint8_t es 24, y la pantalla se iba
// casi a negro. Asi ya no se puede escribir ese fallo.
int nivelDe(int valor, int minimo, int maximo) {
  const int paso = (maximo - minimo) / (UI_NIVELES - 1);
  int n = (valor - minimo + paso / 2) / paso + 1;
  if (n < 1)          n = 1;
  if (n > UI_NIVELES) n = UI_NIVELES;
  return n;
}

int valorDe(int nivel, int minimo, int maximo) {
  const int paso = (maximo - minimo) / (UI_NIVELES - 1);
  return minimo + (nivel - 1) * paso;
}

// Rueda: del ultimo nivel se vuelve al primero.
int siguienteNivel(int nivel) { return nivel >= UI_NIVELES ? 1 : nivel + 1; }

struct Opcion { const char* etiqueta; };
// El orden importa: los indices se usan en el manejador de ENTER. "Avanzado"
// desaparecio -- era una fila que no hacia nada y no prometia nada concreto.
//
// Las cinco ultimas son las delicadas y van juntas AL FINAL, en naranja y detras
// de confirmacion. Agruparlas es deliberado: hay que bajar a proposito.
const Opcion OPCIONES[] = {
    {S_SET_NOMBRE}, {S_SET_CORTO}, {S_SET_BRILLO}, {S_SET_VOLUMEN},
    {S_SET_GPS}, {S_SET_RECUPERAR}, {S_SET_INFO},
    {S_SET_FRASE}, {S_SET_PRESET}, {S_SET_POTENCIA}, {S_SET_HOPS}, {S_SET_FABRICA},
};
constexpr int N_OPCIONES = sizeof(OPCIONES) / sizeof(OPCIONES[0]);

// Indices, para no repartir numeros magicos por el manejador de teclas.
enum {
  OPT_NOMBRE = 0, OPT_CORTO, OPT_BRILLO, OPT_VOLUMEN, OPT_GPS, OPT_RECUPERAR, OPT_INFO,
  OPT_FRASE, OPT_PRESET, OPT_POTENCIA, OPT_SALTOS, OPT_FABRICA
};
constexpr int OPT_PRIMERA_DELICADA = OPT_FRASE;

void pantallaAjustes() {
  barraEstado();

  // Font2 igual que el chat y los nodos. Con 16 px de alto ya no caben las 7
  // filas de golpe, asi que esta pantalla tambien necesita ventana.
  D.setFont(&fonts::Font2);
  const int dy       = UI_ROW_H;
  const int visibles = filasQueCaben(dy);
  ajustarVentana(g_sel, N_OPCIONES, visibles, g_top);

  const auto& c = store::config();
  char v[24];
  int y = UI_STATUSBAR_H + 2;

  for (int i = g_top; i < N_OPCIONES && i < g_top + visibles; i++) {
    const int cy = y + dy / 2;
    if (i == g_sel) D.fillRect(0, y, TFT_W, dy, 0x1082);

    // Las delicadas en naranja: pueden dejar el aparato incomunicado o sin
    // configuracion, y conviene que se vean distintas antes de pulsarlas.
    const bool delicada = (i >= OPT_PRIMERA_DELICADA);
    D.setTextDatum(middle_left);
    D.setTextColor(delicada ? TFT_ORANGE : TFT_WHITE);
    D.drawString(OPCIONES[i].etiqueta, 6, cy);
    const int x_fin = 6 + (int)D.textWidth(OPCIONES[i].etiqueta);

    v[0] = '\0';
    switch (i) {
      case 0: snprintf(v, sizeof(v), "%s", c.nombre_largo); break;
      case 1: snprintf(v, sizeof(v), "%s", c.nombre_corto); break;
      case 2: snprintf(v, sizeof(v), "%d/%d",
                       nivelDe(c.brillo, BRILLO_MIN_PCT, BRILLO_MAX_PCT), UI_NIVELES); break;
      case 3: snprintf(v, sizeof(v), "%d/%d",
                       nivelDe(c.volumen, VOLUMEN_MIN, VOLUMEN_MAX), UI_NIVELES); break;
      case OPT_GPS: snprintf(v, sizeof(v), "%s",
                             c.gps_activo ? S_ACTIVADO : S_DESACTIVADO); break;
      // Takes effect at the next boot: the messages are already on screen.
      case OPT_RECUPERAR: snprintf(v, sizeof(v), "%s",
                                   c.recuperar_chat ? S_ACTIVADO : S_DESACTIVADO); break;
      case OPT_INFO: snprintf(v, sizeof(v), ">"); break;
      // La frase NO se guarda en ningun sitio, solo la clave derivada, asi que
      // aqui se muestra el nombre de canal: sale de la propia frase, y sirve
      // para comprobar de un vistazo que dos aparatos estan en el mismo grupo
      // sin ensenar la frase a nadie.
      case OPT_FRASE:    snprintf(v, sizeof(v), "%s", c.canal); break;
      case OPT_PRESET:   snprintf(v, sizeof(v), "%s", nombrePreset(c.preset)); break;
      case OPT_POTENCIA: snprintf(v, sizeof(v), "%d dBm", (int)c.tx_dbm); break;
      case OPT_SALTOS:   snprintf(v, sizeof(v), "%u", (unsigned)c.hop_limit); break;
      case OPT_FABRICA:  snprintf(v, sizeof(v), ">"); break;
      default: break;
    }

    // El hueco que deja la etiqueta hay que MEDIRLO. En Font2 "Nombre del nodo"
    // se come la mitad de los 240 px, y un nombre largo (hasta 15 caracteres)
    // se pisaria con ella. Se recorta por caracteres UTF-8 completos, que el
    // nombre lo escribe el usuario y puede llevar acentos.
    const int hueco = TFT_W - 6 - x_fin - 8;
    while (v[0] && (int)D.textWidth(v) > hueco)
      util::truncarSeguro(v, strlen(v) - 1);

    D.setTextDatum(middle_right);
    D.setTextColor(0x8410);
    D.drawString(v, TFT_W - 6, cy);
    D.setTextDatum(top_left);
    y += dy;
  }

  pieLista(S_K_FLECHAS "  " S_K_ENTER, g_sel, N_OPCIONES, visibles);
  pieAviso();
}

// --------------------- edicion de ajustes: pantallas ------------------------

int maxBytesCampo(Campo c) {
  switch (c) {
    case Campo::NOMBRE: return NODE_LONGNAME_LEN - 1;    // 15
    case Campo::CORTO:  return NODE_SHORTNAME_LEN - 1;   // 4
    case Campo::FRASE:  return (int)sizeof(g_edit) - 1;  // 64
    default:            return 0;
  }
}

const char* tituloCampo(Campo c) {
  switch (c) {
    case Campo::NOMBRE: return S_SET_NOMBRE;
    case Campo::CORTO:  return S_SET_CORTO;
    case Campo::FRASE:  return S_SET_FRASE;
    default:            return "";
  }
}

void pantallaEditor() {
  D.fillScreen(TFT_BLACK);
  D.setFont(&fonts::Font0);
  D.setTextSize(1);

  // Cabecera con el nombre del campo.
  D.fillRect(0, 0, TFT_W, UI_STATUSBAR_H, 0x2104);
  D.setTextDatum(middle_left);
  D.setTextColor(g_campo == Campo::FRASE ? TFT_ORANGE : TFT_WHITE);
  D.drawString(tituloCampo(g_campo), 4, UI_STATUSBAR_H / 2);
  D.setTextDatum(top_left);

  if (g_derivando) {
    D.setFont(&fonts::Font2);
    D.setTextDatum(middle_center);
    D.setTextColor(TFT_WHITE);
    D.drawString(S_DERIVANDO, TFT_W / 2, TFT_H / 2);
    D.setTextDatum(top_left);
    return;
  }

  // Ayuda de dos lineas, distinta por campo.
  const char* a1 = "";
  const char* a2 = "";
  switch (g_campo) {
    case Campo::NOMBRE: a1 = S_EDIT_NOMBRE_1; a2 = S_EDIT_NOMBRE_2; break;
    case Campo::CORTO:  a1 = S_EDIT_CORTO_1;  a2 = S_EDIT_CORTO_2;  break;
    case Campo::FRASE:  a1 = S_EDIT_FRASE_1;  a2 = S_EDIT_FRASE_2;  break;
    default: break;
  }
  D.setTextColor(0x6B4D);
  D.drawString(a1, 6, UI_STATUSBAR_H + 6);
  D.drawString(a2, 6, UI_STATUSBAR_H + 18);

  // Caja de entrada, en Font2 para que se lea de verdad.
  const int caja_y = 56, caja_h = 26;
  D.fillRect(4, caja_y, TFT_W - 8, caja_h, 0x2104);
  D.drawRect(4, caja_y, TFT_W - 8, caja_h, 0x4208);
  D.setFont(&fonts::Font2);
  D.setTextColor(TFT_GREEN);
  char cur[sizeof(g_edit) + 2];
  snprintf(cur, sizeof(cur), "%s_", g_edit);
  D.setTextDatum(middle_left);
  D.drawString(colaQueCabe(cur, TFT_W - 24), 10, caja_y + caja_h / 2);
  D.setTextDatum(top_left);

  // Bytes usados de los que caben. Se cuentan BYTES, no caracteres: es el limite
  // real del campo y de lo que va por radio.
  D.setFont(&fonts::Font0);
  char b[16];
  snprintf(b, sizeof(b), "%d/%d", g_edit_n, maxBytesCampo(g_campo));
  D.setTextDatum(top_right);
  D.setTextColor(g_edit_n >= maxBytesCampo(g_campo) ? TFT_ORANGE : 0x6B4D);
  D.drawString(b, TFT_W - 6, caja_y + caja_h + 4);
  D.setTextDatum(top_left);

  D.setTextColor(0x6B4D);
  D.drawString(S_EDIT_GUARDAR, 6, TFT_H - 12);
}

// Confirmacion de las rutas delicadas. La restriccion nº 8 pide que a ningun
// estado roto se llegue de un solo golpe.
//
// Muestra ADEMAS el valor propuesto: preguntar "¿seguro?" sin decir a que se va
// a cambiar no es confirmar nada.
void pantallaConfirmar() {
  D.fillScreen(0x3000);
  D.setFont(&fonts::Font0);
  D.setTextDatum(middle_center);

  const char* l1 = ""; const char* l2 = "";
  const char* l3 = ""; const char* l4 = "";
  char nuevo[24] = {};

  switch (g_confirmar) {
    case Confirmar::FRASE:
      l1 = S_AVISO_INCOMUN_1; l2 = S_AVISO_INCOMUN_2;
      l3 = S_AVISO_INCOMUN_3; l4 = S_AVISO_INCOMUN_4;
      break;
    case Confirmar::PRESET:
      l1 = S_AVISO_PRESET_1; l2 = S_AVISO_PRESET_2;
      l3 = S_AVISO_PRESET_3; l4 = S_AVISO_PRESET_4;
      snprintf(nuevo, sizeof(nuevo), "-> %s", nombrePreset(g_pendiente));
      break;
    case Confirmar::POTENCIA:
      l1 = S_AVISO_POTENCIA_1; l2 = S_AVISO_POTENCIA_2;
      l3 = S_AVISO_POTENCIA_3; l4 = S_AVISO_POTENCIA_4;
      snprintf(nuevo, sizeof(nuevo), "-> %d dBm", (int)POTENCIAS[g_pendiente]);
      break;
    case Confirmar::SALTOS:
      l1 = S_AVISO_SALTOS_1; l2 = S_AVISO_SALTOS_2;
      l3 = S_AVISO_SALTOS_3; l4 = S_AVISO_SALTOS_4;
      snprintf(nuevo, sizeof(nuevo), "-> %u", (unsigned)g_pendiente);
      break;
    case Confirmar::FABRICA:
      l1 = S_AVISO_FABRICA_1; l2 = S_AVISO_FABRICA_2;
      l3 = S_AVISO_FABRICA_3; l4 = S_AVISO_FABRICA_4;
      break;
    default: break;
  }

  D.setTextSize(2);
  D.setTextColor(TFT_WHITE);
  D.drawString(S_SEGURO, TFT_W / 2, 20);
  D.setTextSize(1);

  D.setTextColor(0xFCE0);
  D.drawString(l1, TFT_W / 2, 46);
  D.drawString(l2, TFT_W / 2, 58);
  D.drawString(l3, TFT_W / 2, 70);
  D.drawString(l4, TFT_W / 2, 82);

  if (nuevo[0]) {
    D.setFont(&fonts::Font2);
    D.setTextColor(TFT_WHITE);
    D.drawString(nuevo, TFT_W / 2, 100);
    D.setFont(&fonts::Font0);
  }

  D.setTextColor(TFT_WHITE);
  D.drawString(S_CONFIRMA, TFT_W / 2, 122);
  D.setTextDatum(top_left);
}

// Informacion, de solo lectura. Se queda en Font0 a proposito: son nueve filas
// de diagnostico para un adulto, no texto que tenga que leer un niño de lejos.
void pantallaInfo() {
  barraEstado();
  D.setFont(&fonts::Font0);
  const auto& c = store::config();
  int y = UI_STATUSBAR_H + 4;
  const int dy = 12;
  char b[40];

  auto fila = [&](const char* etiqueta, const char* valor, uint16_t color) {
    D.setTextColor(0x8410);
    D.drawString(etiqueta, 6, y);
    D.setTextDatum(top_right);
    D.setTextColor(color);
    D.drawString(valor, TFT_W - 6, y);
    D.setTextDatum(top_left);
    y += dy;
  };

  fila(FT_NOMBRE, FT_VERSION, TFT_WHITE);

  snprintf(b, sizeof(b), "%08X", (unsigned)app::nodoPropio());
  fila(S_INFO_NODO, b, TFT_WHITE);

  snprintf(b, sizeof(b), "%s  0x%02X", c.canal, (unsigned)app::canalHash());
  fila(S_INFO_CANAL, b, TFT_WHITE);

  snprintf(b, sizeof(b), "%.3f MHz SF%d", (double)RF_FREQ_MHZ, PRESET_LONG_FAST_SF);
  fila(S_INFO_RADIO, b, TFT_WHITE);

  const float duty = radio::dutyPct();
  snprintf(b, sizeof(b), "%.1f%% de %.0f%%", (double)duty, (double)DUTY_HARD_PCT);
  fila(S_INFO_CICLO, b, duty >= DUTY_SOFT_PCT ? TFT_ORANGE : TFT_WHITE);

  snprintf(b, sizeof(b), "%u", (unsigned)app::nodos().total());
  fila(S_INFO_NODOS, b, TFT_WHITE);

  fila(S_INFO_TARJETA, store::hayTarjeta() ? S_SI : S_NO,
       store::hayTarjeta() ? TFT_WHITE : TFT_ORANGE);

  // El conmutador de antena no responde en este Cap y no hace falta: el TX/RX lo
  // gobierna DIO2 del SX1262. Se informa, pero sin pintarlo de rojo, que seria
  // señalar una averia que no existe.
  fila(S_INFO_ANTENA, hal::antennaOk() ? S_SI : S_NO, 0x8410);

  D.setTextColor(0x6B4D);
  D.drawString(S_K_ESC, 4, TFT_H - 10 - altoAviso());
  pieAviso();
}

// -------------------------------- SOS --------------------------------------

void pantallaSosArmando() {
  D.fillScreen(0x4000);
  D.setTextDatum(middle_center);
  D.setTextSize(3);
  D.setTextColor(TFT_WHITE);
  D.drawString(S_SOS, TFT_W / 2, 38);
  D.setTextSize(1);

  char b[32];
  snprintf(b, sizeof(b), "%s  %u", S_SOS_CUENTA, (unsigned)app::sosSegundosRestantes());
  D.setTextColor(0xFCE0);
  D.drawString(b, TFT_W / 2, 74);

  D.setTextColor(TFT_WHITE);
  D.drawString(S_SOS_ENVIAR, TFT_W / 2, 100);
  D.setTextColor(0xD69A);
  D.drawString(S_SOS_CANCELAR, TFT_W / 2, 116);
  D.setTextDatum(top_left);
}

void pantallaSosRecibido() {
  // Rojo intermitente, sin tiempo limite: se queda hasta que alguien pulse.
  const bool on = (millis() / 400) % 2;
  D.fillScreen(on ? 0x8000 : 0x5000);
  D.setTextDatum(middle_center);
  D.setTextColor(TFT_WHITE);

  char nom[8];
  nombreCorto(app::sosNodoOrigen(), nom, sizeof(nom));
  char b[40];
  snprintf(b, sizeof(b), "%s %s", S_SOS_RECIBIDO, nom);
  D.setTextSize(2);
  D.drawString(b, TFT_W / 2, 34);
  D.setTextSize(1);

  const auto* n = app::nodos().buscar(app::sosNodoOrigen());
  if (n && n->tiene_pos && hal::gpsValido()) {
    const uint32_t d = util::distanciaM(hal::gpsFix().lat_e7, hal::gpsFix().lon_e7,
                                        n->lat_e7, n->lon_e7);
    const uint16_t r = util::rumboGrados(hal::gpsFix().lat_e7, hal::gpsFix().lon_e7,
                                         n->lat_e7, n->lon_e7);
    char dd[16];
    util::formatearDistancia(d, dd, sizeof(dd));
    snprintf(b, sizeof(b), "%s %s", dd, util::puntoCardinal(r));
    D.setTextSize(2);
    D.drawString(b, TFT_W / 2, 68);
    D.setTextSize(1);
  } else {
    D.drawString(S_SOS_AYUDA, TFT_W / 2, 68);
  }

  D.setTextColor(TFT_WHITE);
  D.drawString(S_SOS_RESPONDER, TFT_W / 2, 112);
  D.setTextDatum(top_left);
}

// -------------------------- primer arranque ---------------------------------

// --------------------- edicion de ajustes: acciones -------------------------

void abrirEditor(Campo c) {
  g_campo       = c;
  g_confirmar   = Confirmar::NADA;
  g_derivando   = false;

  // Se precarga el valor actual: cambiar una letra del nombre no deberia
  // obligar a reescribirlo entero. La frase NO se precarga porque NO se guarda
  // en ningun sitio: en NVS solo esta la clave que sale de ella.
  const auto& cfg = store::config();
  switch (c) {
    case Campo::NOMBRE: snprintf(g_edit, sizeof(g_edit), "%s", cfg.nombre_largo); break;
    case Campo::CORTO:  snprintf(g_edit, sizeof(g_edit), "%s", cfg.nombre_corto); break;
    default:            g_edit[0] = '\0'; break;
  }
  g_edit_n = (int)strlen(g_edit);
}

void cerrarEditor() {
  g_campo       = Campo::NINGUNO;
  g_confirmar   = Confirmar::NADA;
  g_derivando   = false;
  g_edit[0]     = '\0';
  g_edit_n      = 0;
}

// Aplica la accion delicada que estaba esperando confirmacion.
void aplicarConfirmado() {
  auto& c = store::configMutable();

  switch (g_confirmar) {
    case Confirmar::PRESET:
      c.preset = g_pendiente;
      // Si la radio esta ocupada no se reprograma ahora; queda guardado y entra
      // en el siguiente arranque. Mejor eso que cortar una transmision a medias.
      if (!radio::radioAplicarPreset(c.preset, (int8_t)c.tx_dbm)) hal::beepError();
      else                                                       hal::beep();
      store::guardar();
      break;

    case Confirmar::POTENCIA:
      c.tx_dbm = (uint8_t)POTENCIAS[g_pendiente];
      if (!radio::radioAplicarPreset(c.preset, (int8_t)c.tx_dbm)) hal::beepError();
      else                                                       hal::beep();
      store::guardar();
      break;

    case Confirmar::SALTOS:
      // No pasa por la radio: el limite de saltos va en la cabecera de cada
      // paquete, y lo pone app::transmitir() al construirla.
      c.hop_limit = g_pendiente;
      store::guardar();
      hal::beep();
      break;

    case Confirmar::FABRICA:
      store::restablecer();
      app::recargarCanal();
      // No se reinicia el aparato: 'configurado' vuelve a false y pintar() manda
      // al asistente de primer arranque en el siguiente ciclo.
      g_pant   = SCR_CHAT;
      g_sel    = 0;
      g_top    = 0;
      g_scroll = 0;
      g_info   = false;
      hal::beep();
      break;

    default: break;
  }
  cerrarEditor();
}

void guardarNombre() {
  auto& c = store::configMutable();
  if (g_campo == Campo::NOMBRE) {
    snprintf(c.nombre_largo, sizeof(c.nombre_largo), "%s", g_edit);
    util::truncarSeguro(c.nombre_largo, sizeof(c.nombre_largo) - 1);
  } else {
    snprintf(c.nombre_corto, sizeof(c.nombre_corto), "%s", g_edit);
    util::truncarSeguro(c.nombre_corto, sizeof(c.nombre_corto) - 1);
  }

  if (store::guardar()) {
    // Que los demas vean el nombre nuevo sin esperar los 15 min del ciclo.
    app::anunciarNombre();
    hal::beep();
  } else {
    hal::beepError();
  }
  cerrarEditor();
}

void aplicarFrase() {
  g_derivando = true;
  pintar();                      // avisa ANTES de bloquearse ~0,5 s en PBKDF2

  if (store::ponerFrase(g_edit)) {
    app::recargarCanal();
    hal::beep();
  } else {
    hal::beepError();
  }
  cerrarEditor();
}

void pantallaAsistente() {
  D.fillScreen(TFT_BLACK);
  D.setTextColor(TFT_WHITE);
  D.setTextSize(1);

  D.fillRect(0, 0, TFT_W, UI_STATUSBAR_H, 0x2104);
  char cab[32];
  snprintf(cab, sizeof(cab), "%s  paso %d de 2", FT_NOMBRE, g_paso_asistente + 1);
  D.setTextDatum(middle_left);
  D.drawString(cab, 4, UI_STATUSBAR_H / 2);
  D.setTextDatum(top_left);

  if (g_paso_asistente == 2) {
    D.setTextDatum(middle_center);
    D.drawString(S_DERIVANDO, TFT_W / 2, TFT_H / 2);
    D.setTextDatum(top_left);
    return;
  }

  const bool nombre = (g_paso_asistente == 0);
  D.setTextColor(TFT_WHITE);
  D.drawString(nombre ? S_PASO_NOMBRE : S_PASO_FRASE, 6, UI_STATUSBAR_H + 8);

  D.setTextColor(0x6B4D);
  if (nombre) {
    D.drawString("Ej: Papa. Se usaran las 4", 6, UI_STATUSBAR_H + 24);
    D.drawString("primeras letras en el chat.", 6, UI_STATUSBAR_H + 36);
  } else {
    D.drawString("Escribe la misma frase en", 6, UI_STATUSBAR_H + 24);
    D.drawString("todos. Nadie mas podra leeros.", 6, UI_STATUSBAR_H + 36);
  }

  D.fillRect(4, 82, TFT_W - 8, 18, 0x2104);
  D.setTextColor(TFT_GREEN);
  char cur[46];
  snprintf(cur, sizeof(cur), "%s_", g_linea);
  D.drawString(cur, 8, 87);

  D.setTextColor(0x6B4D);
  D.drawString(nombre ? "ENTER = siguiente" : "ENTER = guardar (tarda ~1 s)",
               6, TFT_H - 12);
}

}  // namespace

// ===========================================================================

void begin() {
  D.setRotation(1);
  D.setFont(&fonts::Font0);
  D.setBrightness((uint8_t)(store::config().brillo * 255 / 100));
  g_ultima_tecla = millis();
  g_encendida = true;
}

void despertar() {
  g_ultima_tecla = millis();
  if (!g_encendida) {
    g_encendida = true;
    // Primero despierta el panel y luego se sube el brillo: al reves se ve el
    // transitorio de salida de reposo con la retroiluminacion ya encendida.
    D.wakeup();
    pintar();                 // el contenido de la memoria de video esta viejo
    D.setBrightness((uint8_t)((uint16_t)store::config().brillo * 255 / 100));
  }
}

bool pantallaEncendida() { return g_encendida; }

void ponerAviso(const char* t) { snprintf(g_aviso, sizeof(g_aviso), "%s", t); }
void quitarAviso()             { g_aviso[0] = '\0'; }

void pintar() {
  if (!g_encendida) return;
  D.startWrite();

  // Cada pantalla empieza con la fuente pequeña, y las que la cambien no tienen
  // que acordarse de devolverla: se restaura aqui al terminar. Antes lo hacia
  // solo el chat, y era una obligacion facil de olvidar al añadir una pantalla.
  D.setFont(&fonts::Font0);
  D.setTextSize(1);

  const app::Sos s = app::sosEstado();
  if (s == app::Sos::RECIBIDO)      pantallaSosRecibido();
  else if (s == app::Sos::ARMANDO)  pantallaSosArmando();
  else if (!store::config().configurado) pantallaAsistente();
  // El editor y las confirmaciones se pintan POR ENCIMA de los ajustes, no
  // dentro: ocupan la pantalla entera y no comparten nada con la lista.
  else if (g_confirmar != Confirmar::NADA) pantallaConfirmar();
  else if (g_campo != Campo::NINGUNO)      pantallaEditor();
  else if (g_info) { D.fillScreen(TFT_BLACK); pantallaInfo(); }
  else {
    D.fillScreen(TFT_BLACK);
    switch (g_pant) {
      case SCR_CHAT:     pantallaChat();     break;
      case SCR_NODES:    pantallaNodos();    break;
      case SCR_COMPASS:  pantallaBrujula();  break;
      case SCR_POSITION: pantallaPosicion(); break;
      case SCR_SETTINGS: pantallaAjustes();  break;
      default: break;
    }
  }

  D.endWrite();
  g_ultimo_pintado = millis();
}

// --- teclado ---------------------------------------------------------------

bool tecla(const hal::Evento& ev) {
  // La tecla que enciende la pantalla NO se procesa ademas como entrada: si
  // no, despertar el aparato escribe una letra o cambia de pantalla.
  if (!g_encendida) { despertar(); return true; }
  despertar();

  // El SOS se come todas las teclas.
  const app::Sos s = app::sosEstado();
  if (s == app::Sos::RECIBIDO) {
    if (ev.tecla == hal::Tecla::ENTER) app::sosResponderVoy();
    else                               app::sosCancelar();
    return true;
  }
  if (s == app::Sos::ARMANDO) {
    if (ev.tecla == hal::Tecla::ENTER)     app::sosConfirmar();
    else if (ev.tecla == hal::Tecla::ESC)  app::sosCancelar();
    return true;
  }

  // Asistente de primer arranque.
  if (!store::config().configurado) {
    switch (ev.tecla) {
      case hal::Tecla::CARACTER:
        if (g_linea_n < MSG_MAX_BYTES) { g_linea[g_linea_n++] = ev.ch; g_linea[g_linea_n] = '\0'; }
        break;
      case hal::Tecla::BORRAR:
        if (g_linea_n) g_linea[--g_linea_n] = '\0';
        break;
      case hal::Tecla::ENTER:
        if (!g_linea_n) { hal::beepError(); break; }
        if (g_paso_asistente == 0) {
          auto& c = store::configMutable();
          snprintf(c.nombre_largo, sizeof(c.nombre_largo), "%s", g_linea);
          util::truncarSeguro(c.nombre_largo, sizeof(c.nombre_largo) - 1);
          // El nombre corto sale de las primeras letras, en mayusculas.
          // Hay que limpiarlo antes: viene relleno con el nombre por defecto
          // (4 digitos hex), y escribir "Ana" encima dejaria "ANAB".
          memset(c.nombre_corto, 0, sizeof(c.nombre_corto));
          for (int i = 0; i < NODE_SHORTNAME_LEN - 1 && g_linea[i]; i++)
            c.nombre_corto[i] = (char)toupper((unsigned char)g_linea[i]);
          g_paso_asistente = 1;
        } else {
          g_paso_asistente = 2;
          pintar();                       // avisa antes de bloquearse ~1 s
          if (store::ponerFrase(g_linea)) {
            app::recargarCanal();
            g_paso_asistente = 0;
            hal::beep();
          } else {
            g_paso_asistente = 1;         // no se pudo guardar: repetir paso
            hal::beepError();
          }
        }
        g_linea[0] = '\0'; g_linea_n = 0;
        break;
      default: break;
    }
    return true;
  }

  // --- confirmacion de una accion delicada: solo ENTER o ESC ---
  if (g_confirmar != Confirmar::NADA) {
    if (g_derivando) return true;          // ocupado derivando: no encolar teclas

    if (ev.tecla == hal::Tecla::ENTER) {
      if (g_confirmar == Confirmar::FRASE) aplicarFrase();
      else                                 aplicarConfirmado();
    } else if (ev.tecla == hal::Tecla::ESC) {
      // En la frase, ESC vuelve al editor SIN perder lo escrito. En las demas no
      // hay nada que editar, asi que se cancela del todo.
      if (g_confirmar == Confirmar::FRASE) g_confirmar = Confirmar::NADA;
      else                                 cerrarEditor();
    }
    return true;
  }

  // --- editor de los ajustes: se come todas las teclas mientras este abierto ---
  if (g_campo != Campo::NINGUNO) {
    if (g_derivando) return true;

    switch (ev.tecla) {
      case hal::Tecla::CARACTER: {
        // El nombre corto va siempre en mayusculas: es lo que se ve en el chat y
        // en la lista de nodos, y mezclar cajas ahi queda desalineado.
        const char ch = (g_campo == Campo::CORTO)
                            ? (char)toupper((unsigned char)ev.ch) : ev.ch;
        if (g_edit_n < maxBytesCampo(g_campo)) {
          g_edit[g_edit_n++] = ch;
          g_edit[g_edit_n]   = '\0';
        } else {
          hal::beepError();
        }
        break;
      }
      case hal::Tecla::BORRAR:
        if (g_edit_n) g_edit[--g_edit_n] = '\0';
        break;
      case hal::Tecla::ENTER:
        // Vacio se rechaza: un nodo sin nombre sale como numero hexadecimal, y
        // una frase vacia dejaria el grupo sin clave.
        if (!g_edit_n) { hal::beepError(); break; }
        if (g_campo == Campo::FRASE) g_confirmar = Confirmar::FRASE;
        else                         guardarNombre();
        break;
      case hal::Tecla::ESC:
        cerrarEditor();
        break;
      default: break;
    }
    return true;
  }

  // --- informacion: solo se sale ---
  if (g_info) {
    if (ev.tecla == hal::Tecla::ESC || ev.tecla == hal::Tecla::TAB) g_info = false;
    return true;
  }

  // --- navegacion normal ---
  switch (ev.tecla) {
    case hal::Tecla::TAB:
      g_pant = (ft_screen_t)((g_pant + 1) % SCR_COUNT);
      g_sel = 0; g_scroll = 0; g_top = 0; g_info = false;
      hal::beep();
      return true;

    case hal::Tecla::ARRIBA:
      if (g_pant == SCR_CHAT) {
        // El tope lo decide la propia composicion de la pantalla, que es la
        // unica que sabe cuantas LINEAS ocupa cada mensaje. Restar mensajes
        // como antes se quedaba corto en cuanto uno ocupaba mas de una fila, y
        // entonces el principio del historial no se alcanzaba.
        if (chatHayMasArriba()) g_scroll++;
      } else if (g_sel > 0) g_sel--;
      return true;

    case hal::Tecla::ABAJO:
      if (g_pant == SCR_CHAT) {
        if (g_scroll > 0) g_scroll--;
      } else {
        // Con tope. Sin el, la seleccion se va mas alla de la ultima fila y
        // el usuario tiene que pulsar ARRIBA tantas veces como se paso.
        const int n = (g_pant == SCR_SETTINGS) ? N_OPCIONES
                      : (g_pant == SCR_NODES)  ? (int)app::nodos().total() : 1;
        if (g_sel < n - 1) g_sel++;
      }
      return true;

    case hal::Tecla::IZQUIERDA:
    case hal::Tecla::DERECHA:
      if (g_pant == SCR_CHAT) {
        // Change conversation. What was typed stays: it goes to whichever chat
        // is on screen when ENTER is pressed, and the name is right beside it.
        g_conv = convVecina(g_conv, ev.tecla == hal::Tecla::DERECHA ? 1 : -1);
        g_scroll = 0;
        hal::beep();
      } else if (g_pant == SCR_NODES && ev.tecla == hal::Tecla::DERECHA) {
        // Straight into a private chat with the highlighted node.
        if (const mesh::Nodo* n = nodoDeFila(g_sel)) {
          g_conv = n->num;
          g_pant = SCR_CHAT;
          g_scroll = 0;
          hal::beep();
        }
      }
      return true;

    case hal::Tecla::ESC:
      if (g_pant == SCR_CHAT) {
        if (g_scroll)       g_scroll = 0;      // volver al final
        else if (g_linea_n) { g_linea[0] = '\0'; g_linea_n = 0; }
        else if (g_conv)    g_conv = 0;        // from a private chat, back to TODOS
      } else {
        g_pant = SCR_CHAT;
      }
      return true;

    case hal::Tecla::ENTER:
      if (g_pant == SCR_CHAT) {
        if (g_linea_n) {
          if (!app::enviarTexto(g_linea, g_conv)) hal::beepError();
          else                            hal::beep();
          g_linea[0] = '\0'; g_linea_n = 0;
          g_scroll = 0;
        }
      } else if (g_pant == SCR_NODES) {
        // Selecciona el nodo resaltado y salta a la brujula.
        if (const mesh::Nodo* n = nodoDeFila(g_sel)) {
          app::seleccionarNodo(n->num);
          g_pant = SCR_COMPASS;
        }
      } else if (g_pant == SCR_SETTINGS) {
        auto& c = store::configMutable();
        switch (g_sel) {
          // Brillo y volumen van por niveles 1..5 y dan la vuelta al llegar al
          // ultimo. El valor guardado se reconstruye desde el nivel, que ya
          // viene acotado: por eso aqui no puede haber desbordamiento.
          case OPT_BRILLO: {
            const int n = siguienteNivel(nivelDe(c.brillo, BRILLO_MIN_PCT, BRILLO_MAX_PCT));
            c.brillo = (uint8_t)valorDe(n, BRILLO_MIN_PCT, BRILLO_MAX_PCT);
            D.setBrightness((uint8_t)((uint16_t)c.brillo * 255 / 100));
            store::guardar();
            break;
          }
          case OPT_VOLUMEN: {
            const int n = siguienteNivel(nivelDe(c.volumen, VOLUMEN_MIN, VOLUMEN_MAX));
            c.volumen = (uint8_t)valorDe(n, VOLUMEN_MIN, VOLUMEN_MAX);
            hal::soundVolumen(c.volumen);
            store::guardar();
            hal::beep();            // para oir el nivel nuevo, no solo verlo
            break;
          }
          case OPT_GPS:
            c.gps_activo = !c.gps_activo;
            hal::gpsEncender(c.gps_activo);
            store::guardar();
            break;
          case OPT_RECUPERAR:
            c.recuperar_chat = !c.recuperar_chat;
            store::guardar();
            break;

          // Campos de texto. Abren el editor; guardan al pulsar ENTER alli.
          case OPT_NOMBRE: abrirEditor(Campo::NOMBRE); hal::beep(); break;
          case OPT_CORTO:  abrirEditor(Campo::CORTO);  hal::beep(); break;
          case OPT_FRASE:  abrirEditor(Campo::FRASE);  hal::beep(); break;

          case OPT_INFO:   g_info = true; hal::beep(); break;

          // Filas delicadas: se PROPONE el valor siguiente y se pregunta. No se
          // toca nada hasta que se confirme.
          case OPT_PRESET:
            g_pendiente = (uint8_t)((c.preset + 1) % PRESET_COUNT);
            g_confirmar = Confirmar::PRESET;
            break;

          case OPT_POTENCIA: {
            // Indice de la potencia actual en la tabla, para ofrecer la siguiente.
            int i = 0;
            for (int k = 0; k < N_POTENCIAS; k++)
              if (POTENCIAS[k] == (int8_t)c.tx_dbm) { i = k; break; }
            g_pendiente = (uint8_t)((i + 1) % N_POTENCIAS);
            g_confirmar = Confirmar::POTENCIA;
            break;
          }

          case OPT_SALTOS:
            // 1..HOP_MAX, dando la vuelta. Cero dejaria el paquete sin salir del
            // nodo, asi que el minimo es 1.
            g_pendiente = (uint8_t)(c.hop_limit >= HOP_MAX ? 1 : c.hop_limit + 1);
            g_confirmar = Confirmar::SALTOS;
            break;

          case OPT_FABRICA:
            g_confirmar = Confirmar::FABRICA;
            break;

          default: hal::beep(); break;
        }
      }
      return true;

    case hal::Tecla::BORRAR:
      if (g_pant == SCR_CHAT && g_linea_n) g_linea[--g_linea_n] = '\0';
      return true;

    case hal::Tecla::CARACTER:
      if (g_pant == SCR_CHAT) {
        // El limite se cuenta en BYTES UTF-8, no en caracteres.
        if (g_linea_n < MSG_MAX_BYTES) {
          g_linea[g_linea_n++] = ev.ch;
          g_linea[g_linea_n]   = '\0';
        } else hal::beepError();
      }
      return true;

    default: return false;
  }
}

void loop() {
  const app::Sos s = app::sosEstado();

  // Un SOS mantiene la pantalla encendida cueste lo que cueste.
  if (s == app::Sos::RECIBIDO || s == app::Sos::ARMANDO) {
    despertar();
    if (millis() - g_ultimo_pintado > 300) pintar();
    return;
  }

  if (g_encendida && millis() - g_ultima_tecla > SCREEN_TIMEOUT_MS) {
    g_encendida = false;
    D.setBrightness(0);       // apagar la luz primero, luego dormir el panel
    D.sleep();
    return;
  }
  if (g_encendida && millis() - g_ultimo_pintado > 2000) pintar();
}

}  // namespace ui
