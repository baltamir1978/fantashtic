#include "keys.h"

#include <M5Cardputer.h>

// Notas sobre la libreria M5Cardputer 1.1.1, verificadas leyendo
// utility/Keyboard/Keyboard.{h,cpp} de la version que se compila:
//
//  - KeysState NO tiene up/down/left/right/esc. Solo trae tab, fn, shift,
//    ctrl, opt, alt, del, enter, space, modifiers, word, hid_keys y
//    modifier_keys. Las flechas hay que sintetizarlas AQUI.
//
//  - La tecla de borrar es KEY_BACKSPACE y updateKeysState() la refleja en
//    st.del. NO existe st.backspace: mirarlo no compila.
//
//  - Fn NO corta el relleno de st.word. La pasada 2 de updateKeysState() mete
//    el caracter base en word aunque Fn este pulsado, asi que la capa Fn hay
//    que resolverla ANTES de emitir CARACTER; si no, Fn+';' moveria el cursor
//    Y ademas escribiria un ';'.
//
//  - Shift y el bloqueo de mayusculas ya los aplica la libreria: en ese caso
//    word trae value_second. Aqui no hay que volver a traducir nada.
//
//  - M5Cardputer.update() llama a updateKeyList() y updateKeysState(), de modo
//    que esta funcion solo lee un estado ya refrescado.
//
//  - keysState() devuelve una REFERENCIA. Copiarla con 'auto' clona tres
//    std::vector y reserva memoria en cada pulsacion.

namespace hal {
namespace {

// Capa Fn: las flechas del Cardputer son Fn + ; . , / y ESC es Fn + `.
// Los caracteres ';' '.' ',' '/' sin Fn se escriben con normalidad.
Tecla teclaFn(char c) {
  switch (c) {
    case ';': return Tecla::ARRIBA;
    case '.': return Tecla::ABAJO;
    case ',': return Tecla::IZQUIERDA;
    case '/': return Tecla::DERECHA;
    case '`': return Tecla::ESC;
    default:  return Tecla::NINGUNA;
  }
}

}  // namespace

void keysBegin() {
  // M5Cardputer.begin(cfg, true) ya arranca el teclado.
}

bool keysPoll(Evento* ev) {
  if (!M5Cardputer.Keyboard.isChange())  return false;
  if (!M5Cardputer.Keyboard.isPressed()) return false;   // ignoramos las sueltas

  const auto& st = M5Cardputer.Keyboard.keysState();

  *ev = Evento{};
  ev->fn    = st.fn;
  ev->shift = st.shift;
  ev->ctrl  = st.ctrl;

  // --- teclas de control ---
  if (st.enter) { ev->tecla = Tecla::ENTER;  return true; }
  if (st.del)   { ev->tecla = Tecla::BORRAR; return true; }
  if (st.tab)   { ev->tecla = Tecla::TAB;    return true; }

  if (st.word.empty()) return false;

  const char c = st.word.front();

  // --- capa Fn: flechas y ESC, ANTES de tratar el caracter como texto ---
  if (st.fn) {
    const Tecla t = teclaFn(c);
    if (t != Tecla::NINGUNA) { ev->tecla = t; return true; }
    return false;                // Fn + cualquier otra cosa no significa nada
  }

  // La tecla ` tambien hace de ESC: es mas comoda que Fn+`.
  if (c == '`') { ev->tecla = Tecla::ESC; return true; }

  ev->tecla = Tecla::CARACTER;
  ev->ch    = c;
  return true;
}

bool keysSosCombo() {
  if (!M5Cardputer.Keyboard.isPressed()) return false;
  const auto& st = M5Cardputer.Keyboard.keysState();
  if (!st.enter) return false;

  // ESC se sintetiza de '`'. Hay que recorrer TODA la lista de teclas pulsadas:
  // keysPoll() devuelve un solo evento y se queda con ENTER, asi que por esa via
  // el combo es invisible.
  for (const char c : st.word)
    if (c == '`') return true;
  return false;
}

const char* keysNombre(Tecla t) {
  switch (t) {
    case Tecla::CARACTER:   return "caracter";
    case Tecla::ENTER:      return "ENTER";
    case Tecla::BORRAR:     return "BORRAR";
    case Tecla::TAB:        return "TAB";
    case Tecla::ESC:        return "ESC";
    case Tecla::ARRIBA:     return "ARRIBA";
    case Tecla::ABAJO:      return "ABAJO";
    case Tecla::IZQUIERDA:  return "IZQUIERDA";
    case Tecla::DERECHA:    return "DERECHA";
    default:                return "-";
  }
}

}  // namespace hal
