#pragma once
//
// Todos los textos de la interfaz, en un solo sitio.
// Identificadores y comentarios en ingles; los textos visibles, en espanol.
// Centralizarlos aqui permite traducir despues sin tocar la logica.
//
// Sin acentos ni caracteres no-ASCII en las cadenas cortas de UI: la fuente
// por defecto de M5GFX no los pinta bien a tamano pequeno. Los mensajes de
// chat SI son UTF-8 completo (los escribe el usuario).
//

// --- barra de estado ---------------------------------------------------------
#define S_NODOS           "nodos"
#define S_SIN_SD          "SD?"
#define S_GPS_OK          "GPS"
#define S_GPS_NO          "gps"

// --- pantallas ---------------------------------------------------------------
#define S_SCR_CHAT        "CHAT"
#define S_SCR_NODOS       "NODOS"
#define S_SCR_BRUJULA     "BRUJULA"
#define S_SCR_AJUSTES     "AJUSTES"

// --- chat --------------------------------------------------------------------
#define S_ENVIANDO        "enviando"
#define S_PROPAGADO       "ok"
#define S_FALLIDO         "fallo"
#define S_HISTORIAL       "historial"
#define S_VOLVER_FIN      "ESC = volver al final"
#define S_NUEVO_ABAJO     "hay mensajes nuevos"
#define S_SIN_MENSAJES    "Todavia no hay mensajes"

// --- nodos -------------------------------------------------------------------
#define S_HACE            "hace"
#define S_SALTOS          "saltos"
#define S_SIN_SENAL       "sin senal"
#define S_NADIE           "No se ve a nadie todavia"

// --- brujula -----------------------------------------------------------------
#define S_RUMBO           "rumbo"
#define S_SIN_POSICION    "Sin posicion de ese nodo"
#define S_NORTE_ARRIBA    "N arriba (parado)"

// --- SOS ---------------------------------------------------------------------
#define S_SOS             "SOS"
#define S_SOS_CUENTA      "Enviando en"
#define S_SOS_ENVIAR      "ENTER = enviar ya"
#define S_SOS_CANCELAR    "ESC = cancelar"
#define S_SOS_RECIBIDO    "SOS DE"
#define S_SOS_AYUDA       "necesita ayuda"
#define S_SOS_RESPONDER   "ENTER = Voy hacia ti"
#define S_SOS_TEXTO       "SOS -"            // se concatena el nombre corto
#define S_SOS_VOY         "Voy hacia ti"
#define S_SOS_BLOQUEADO   "Radio saturada: el SOS espera"

// --- ajustes -----------------------------------------------------------------
#define S_SET_NOMBRE      "Nombre del nodo"
#define S_SET_CORTO       "Nombre corto"
#define S_SET_BRILLO      "Brillo"
#define S_SET_VOLUMEN     "Volumen"
#define S_SET_GPS         "GPS"
#define S_SET_AVANZADO    "Avanzado"
#define S_SET_FRASE       "Frase del grupo"
#define S_SET_PSK_HEX     "Clave en hexadecimal"
#define S_SET_PRESET      "Alcance / velocidad"
#define S_SET_POTENCIA    "Potencia TX"
#define S_SET_HOPS        "Saltos maximos"
#define S_SET_FABRICA     "Restablecer de fabrica"
#define S_SET_INFO        "Informacion"
#define S_ACTIVADO        "activado"
#define S_DESACTIVADO     "apagado"

// --- confirmaciones ----------------------------------------------------------
#define S_SEGURO          "Seguro?"
#define S_AVISO_INCOMUN   "Esto puede dejar este aparato\nincomunicado del resto."
#define S_CONFIRMA        "ENTER = si     ESC = no"
#define S_GUARDADO        "Guardado"

// --- primer arranque ---------------------------------------------------------
#define S_BIENVENIDA      "Bienvenido a Fantashtic"
#define S_PASO_NOMBRE     "Como se llama este aparato?"
#define S_PASO_FRASE      "Frase del grupo"
#define S_FRASE_AYUDA     "Escribe la misma frase en todos\nlos aparatos. Nadie mas podra leeros."
#define S_DERIVANDO       "Generando la clave..."

// --- errores -----------------------------------------------------------------
#define S_ERR_RADIO       "RADIO NO DETECTADA"
#define S_ERR_ANTENA      "ANTENA O SWITCH RF?"
#define S_ERR_SATURADA    "RADIO SATURADA"
#define S_ERR_MIN         "min"
#define S_ERR_GPS         "SIN GPS"
#define S_ERR_BATERIA     "BATERIA BAJA"
#define S_ERR_SD          "Sin tarjeta: no se guarda el historial"

// --- teclas ------------------------------------------------------------------
#define S_K_TAB           "TAB = siguiente"
#define S_K_FLECHAS       "flechas = mover"
#define S_K_ENTER         "ENTER = elegir"
#define S_K_ESC           "ESC = atras"
