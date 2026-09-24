#pragma once
//
// Fantashtic - configuracion central del proyecto.
//
// TODOS los pines y constantes viven aqui. Ningun otro archivo debe contener
// un numero de pin literal.
//
// Pines verificados contra:
//   - https://docs.m5stack.com/en/core/Cardputer%20Adv
//   - https://docs.m5stack.com/en/cap/Cap_LoRa-1262
//   - meshtastic/firmware  variants/esp32s3/m5stack_cardputer_adv/variant.h
//
// Los marcados TODO(hw) NO estan confirmados: verificar en la primera sesion
// con hardware antes de fiarse de ellos.
//
#include <stdint.h>

// secrets.h esta en .gitignore y no existe en un clon recien hecho. Lo unico
// que define es la sal de PBKDF2 del despliegue (ver la seccion CRIPTOGRAFIA).
// __has_include evita tener que crear el archivo solo para compilar.
#if defined(__has_include)
#  if __has_include("secrets.h")
#    include "secrets.h"
#  endif
#endif

// Tiene que coincidir con la etiqueta de la release publicada. Si no, el aparato
// imprime por consola y muestra en Informacion una version que no corresponde a
// ningun binario liberado, y deja de haber forma de saber que lleva dentro.
#define FT_VERSION        "0.3.0"
#define FT_NOMBRE         "Fantashtic"

// =============================================================================
//  PANTALLA  -  ST7789V2, 240x135, SPI2_HOST (lo reclama M5GFX en M5.begin())
// =============================================================================
#define PIN_TFT_CS        37
#define PIN_TFT_DC        34
#define PIN_TFT_MOSI      35
#define PIN_TFT_SCK       36
#define PIN_TFT_RST       33
#define PIN_TFT_BL        38    // OJO: tambien alimenta el LED RGB
#define TFT_W             240
#define TFT_H             135

// =============================================================================
//  I2C INTERNO  -  TCA8418 (teclado), BMI270 (IMU), ES8311 (audio),
//                  PI4IOE5V6408 (switch de antena RF del Cap LoRa-1262)
// =============================================================================
#define PIN_I2C_SDA        8
#define PIN_I2C_SCL        9
#define PIN_KB_INT        11

// Direccion del expansor que habilita el switch de antena RF.
//
// 0x43, confirmada en la documentacion de M5Stack: el ejemplo oficial del Cap
// LoRa-1262 hace literalmente
//     m5::PI4IOE5V6408_Class ioe(0x43, 400000, &m5::In_I2C);
// y los registros 0x03 / 0x05 / 0x07 de abajo coinciden con la clase
// PI4IOE5V6408_Class de M5Unified.
//
// OJO: en la sesion del 29-jul-2026 el expansor NO hizo ACK en el aparato, ni
// en 0x43 ni en 0x44, ni a 100 kHz ni a 400 kHz, con el Cap montado y estando
// el bus interno sano (ES8311, TCA8418 y BMI270 si responden). Ver ESTADO.md.
// Poner aqui la direccion NO relaja la seguridad: antennaBegin() exige releer
// los tres registros, asi que mientras el chip no conteste el estado sera
// NO_RESPONDE y radioBegin() seguira negandose a arrancar.
//
// 0xFF es el centinela de "sin configurar". NO usar 0x00: esa es la direccion
// de llamada general de I2C, y escribir ahi difunde a TODOS los dispositivos.
#define I2C_ADDR_ANTSW    0x43
#define I2C_ADDR_ANTSW_SIN_CONFIGURAR 0xFF

// Registros del PI4IOE5V6408. Los tres son necesarios: al arrancar, P0 esta
// como entrada Y en alta impedancia. Escribir solo el de salida NO basta.
#define PI4IOE_REG_DIR    0x03   // direccion   : 1 = salida
#define PI4IOE_REG_OUT    0x05   // valor       : 1 = HIGH
#define PI4IOE_REG_HIZ    0x07   // alta imped. : 0 = activo
#define PI4IOE_ANT_MASK   0x01   // P0 -> SX_ANT_SW

// =============================================================================
//  BUS SPI COMPARTIDO  -  LoRa + microSD, en SPI3_HOST (HSPI)
//
//  NO usar el objeto global SPI: ese es SPI2_HOST, el de la pantalla.
//  Remultiplexarlo romperia el display.
// =============================================================================
#define PIN_SPI_SCK       40
#define PIN_SPI_MISO      39
#define PIN_SPI_MOSI      14
#define SPI_FREQ_LORA     4000000UL
#define SPI_FREQ_SD       20000000UL

// =============================================================================
//  LoRa  -  Cap LoRa-1262, SX1262, +22 dBm, RP-SMA
// =============================================================================
#define PIN_LORA_CS        5
#define PIN_LORA_DIO1      4    // IRQ
#define PIN_LORA_BUSY      6    // variant.h lo llama LORA_DIO2. Es BUSY.
#define PIN_LORA_RST       3

#define LORA_TCXO_V       1.8f  // DIO3 alimenta el TCXO. Fallback a 0.0 si
                                // begin() devuelve RADIOLIB_ERR_SPI_CMD_TIMEOUT
#define LORA_CURRENT_MA    140  // OCP maximo del SX1262

// =============================================================================
//  microSD  -  mismo bus SPI que la LoRa. Opcional: sin tarjeta se sigue
//              funcionando, solo se pierde el historial persistente.
// =============================================================================
#define PIN_SD_CS         12
#define SD_DIR_BASE       "/fantashtic"
#define SD_DIR_CHAT       "/fantashtic/chat"
#define SD_DIR_CAMPO      "/fantashtic/campo"
// One single chat file, in the order things happened. It used to be one file
// per month, named after the GPS date, and that order cannot be rebuilt: without
// a fix everything went to sin-fecha.log, so indoors the most recent messages
// ended up in the "oldest" file. Reading the last lines back at boot needs the
// real order, and a single append-only file is the only place that has it.
#define SD_FILE_CHAT      "/fantashtic/chat/chat.log"
#define SD_FILE_NODES     "/fantashtic/nodes.bin"

// =============================================================================
//  GPS  -  ATGM336H-6N (AT6668) en el Cap LoRa-1262, UART1
//
//  RX del ESP = 15 (recibe del TX del GPS), TX del ESP = 13.
//  CONFIRMADO en el aparato el 29-jul-2026: con estos pines llegan los NMEA y
//  se consigue fix. La tabla del bus EXT de M5 los da al reves y esta mal; la
//  pagina del Cap y variant.h coinciden entre si y tienen razon.
// =============================================================================
#define PIN_GPS_RX        15
#define PIN_GPS_TX        13
#define GPS_BAUD          115200

// Ventana de las tramas NMEA, para no dormir encima de ellas.
//
// POR QUE HACE FALTA: en light sleep el APB esta parado, asi que el UART no
// mete en su FIFO lo que llegue mientras dormimos: esos bytes se PIERDEN. El
// modulo suelta todas sus tramas de golpe (una rafaga) y se calla hasta la
// siguiente fijacion. Si se duerme a ciegas en trozos de TICK_IDLE_MS, casi
// todas las rafagas caen dentro de un sueño, el checksum de cada trama falla y
// el aparato deja de tener posicion justo cuando nadie mira -- que es
// exactamente cuando importa, porque es cuando se emiten los avisos de
// posicion automaticos.
//
// La solucion es predecir: el modulo es un reloj muy estable, asi que basta
// medir el comienzo de la ultima rafaga y despertar un poco antes de la
// siguiente. Despierto no se pierde nada, porque la ISR del UART sigue
// llenando el buffer de anillo aunque el bucle este en un delay().
// Y se predice con el intervalo MEDIDO, no con el pedido en $PCAS02: medido el
// 30-jul-2026, el modulo no hace caso del todo (con 5000 ms pedidos el caudal se
// queda en ~400 B/s en vez de bajar a ~40). Ver g_intervalo_ms en hal/gps.cpp.
#define GPS_TRAMA_QUIETO_MS   40   // silencio que separa una rafaga de la siguiente
#define GPS_TRAMA_MARGEN_MS  100   // se despierta esto antes de la rafaga prevista
                                   // (100 y no 30: durmiendo, millis() va sobre el
                                   //  RC de 150 kHz del RTC y se desvia ~1 %)
#define GPS_TRAMA_MIN_INTERVALO_MS 200  // por debajo de esto es una rafaga partida,
                                        // no una fijacion nueva

// =============================================================================
//  BOTON  -  el del modulo StampS3A, no hay ninguno en el chasis
//
//  Es el MISMO del modo descarga, asi que mantenerlo mientras se enciende el
//  aparato arranca el bootloader con la pantalla muerta. Por eso el SOS tiene
//  ademas un atajo de teclado (ESC+DEL), que es la via principal.
//  M5Unified lo lee como BtnA; aqui se necesita el numero para armarlo como
//  fuente de despertar del light sleep.
// =============================================================================
#define PIN_BTN            0

// =============================================================================
//  VARIOS
// =============================================================================
// Boton G0 del modulo StampS3A. NO hay boton dedicado en el chasis, y es el
// mismo que se usa para entrar en modo descarga.
//
// Esta constante NO se lee en ningun sitio: el codigo va por M5Cardputer.BtnA,
// que M5Unified asocia a este mismo GPIO (M5Unified.cpp agrupa
// board_M5CardputerADV con board_M5StampS3). Se deja documentado porque la regla
// del proyecto es que todos los pines vivan aqui.
#define PIN_BUTTON         0    // pulsacion larga = SOS, corta = cancelar
#define PIN_BATTERY       10
#define BAT_MULTIPLIER    2.0f  // divisor 100k+100k. variant.h usa 2*1.02, pero
                                // ese 1.02 es un fudge para mostrar "cargando"
#define BAT_SAMPLES       16
#define PIN_NEOPIXEL      21    // CONFIRMADO: _pin_table_other0 de M5Unified
                                // asocia board_M5CardputerADV a GPIO 21
#define NEOPIXEL_COUNT     1
// El LED comparte rail con la retroiluminacion (GPIO 38): con la pantalla
// apagada no se enciende. No hay aviso luminoso de bajo consumo.

// =============================================================================
//  RADIO  -  parametros PHY. Deben coincidir con Meshtastic byte a byte.
// =============================================================================
#define RF_FREQ_MHZ       869.525f   // EU_868 + LONG_FAST -> slot unico
#define RF_SYNC_WORD      0x2B
#define RF_PREAMBLE       16
#define RF_TX_POWER_DBM   22         // conducidos. Con antena 3 dBi = ~193 mW ERP
#define RF_TX_POWER_MIN    2
#define RF_TX_POWER_MAX   22

// Presets (SF, BW kHz, CR). LONG_FAST es el defecto y el unico probado en el
// aire. Los valores de SF/BW salen de RadioInterface.cpp de meshtastic/firmware.
//
// CAMBIAR EL PRESET HAY QUE HACERLO EN TODOS LOS NODOS: dos nodos con presets
// distintos no se oyen en absoluto. Por eso va detras de confirmacion.
#define PRESET_LONG_FAST_SF       11
#define PRESET_LONG_FAST_BW       250.0f
#define PRESET_LONG_FAST_CR        5

#define PRESET_COUNT               3
#define PRESET_LONG_SLOW_SF       12      // maximo alcance, la mitad de velocidad
#define PRESET_LONG_SLOW_BW       125.0f
#define PRESET_LONG_SLOW_CR         8
#define PRESET_MEDIUM_FAST_SF      9      // mas rapido, menos alcance
#define PRESET_MEDIUM_FAST_BW     250.0f
#define PRESET_MEDIUM_FAST_CR       5

// Region EU_868 - RadioInterface.cpp: RDEF(EU_868, 869.4, 869.65, 10, 0, 27, ...)
#define REGION_FREQ_START 869.4f
#define REGION_FREQ_END   869.65f
#define REGION_DUTY_PCT   10         // limite duro, ETSI subbanda g3
#define REGION_PWR_LIMIT  27         // conducidos, no ERP

// Ventana de contencion  -  RadioInterface.h
#define CW_MIN             3
#define CW_MAX             8
#define NUM_SYM_CAD        2
#define PROCESSING_TIME_MS 4500

// Duty cycle
#define DUTY_WINDOW_MIN   60         // ventana rodante, en cubos de 1 minuto
#define DUTY_HARD_PCT     10.0f      // bloquea todo TX
#define DUTY_SOFT_PCT      8.0f      // corta envios automaticos (pos, nodeinfo)

// =============================================================================
//  PROTOCOLO  -  constantes de Meshtastic. Citar la fuente al tocarlas.
// =============================================================================
#define MESH_HEADER_LEN        16    // RadioInterface.h
#define MESH_MAX_LORA_PAYLOAD 255
#define MESH_MAX_ENCRYPTED    239    // 255 - 16
#define MESH_MAX_DATA_PAYLOAD 233    // mesh.options: Data.payload max_size:233
#define MESH_BROADCAST_ADDR   0xFFFFFFFFu

#define FLAG_HOP_LIMIT_MASK   0x07   // bits 0-2
#define FLAG_WANT_ACK_MASK    0x08   // bit 3
#define FLAG_VIA_MQTT_MASK    0x10   // bit 4
#define FLAG_HOP_START_MASK   0xE0   // bits 5-7
#define FLAG_HOP_START_SHIFT     5

#define HOP_MAX                7
#define HOP_DEFAULT            3     // con 4 nodos sobra y limita el trafico

#define PORTNUM_TEXT_MESSAGE   1     // portnums.proto
#define PORTNUM_POSITION       3
#define PORTNUM_NODEINFO       4
#define PORTNUM_ROUTING        5
#define PORTNUM_TELEMETRY     67

// =============================================================================
//  CRIPTOGRAFIA
// =============================================================================
#define PSK_LEN               32     // AES-256-CTR
#define CTR_COUNTER_SIZE       4     // CryptoEngine.cpp: setCounterSize(4)

// Sal de PBKDF2. La de verdad se define en include/secrets.h, que NO se
// commitea; este valor es solo el de por defecto, y es el que reproduce los
// vectores publicados en docs/VECTORES.md y los tests de test_util.
//
// Que la sal sea publica no rompe PBKDF2 -- la sal nunca fue el secreto, la
// frase si. Lo que evita sacarla del repositorio es que UNA sola tabla
// precalculada sirva contra todos los despliegues: con una sal por grupo, hay
// que rehacer el trabajo para cada uno. Es lo que en la practica se llama
// pepper. La frase sigue siendo lo unico que hay que proteger de verdad.
//
// OJO al cambiarla: la clave derivada vive en NVS, asi que los nodos ya
// configurados siguen funcionando. Pero un nodo NUEVO derivara otra clave de la
// MISMA frase y no se entendera con ellos. Al cambiar la sal hay que repasar la
// frase en TODOS los nodos.
#ifndef PBKDF2_SALT
#define PBKDF2_SALT           "fantashtic-v1"
#endif
#define PBKDF2_ITERS          20000
#define CHANNEL_NAME_LEN      12     // "FT-" + 5 chars + NUL, con margen

// =============================================================================
//  BUFFERS  -  todos estaticos. Cero asignaciones dinamicas tras setup().
// =============================================================================
#define MAX_TX_QUEUE          16     // igual que upstream
#define DEDUP_TABLE_SIZE     128
#define DEDUP_RELAYERS         3     // NUM_RELAYERS en PacketHistory
#define MAX_NODES             16
#define CHAT_RAM_MESSAGES     60
#define MSG_MAX_BYTES        180     // BYTES UTF-8, no caracteres
#define MSG_BUF_BYTES        200
// Direct messages waiting for an ACK (mesh/pending.h). Each slot keeps a copy
// of the encrypted packet, ~270 bytes: 6 of them is under 2 KB of RAM. A slot
// lives at most DM_ACK_TIMEOUT_MS * (DM_RETRIES + 1), so six fill up only if
// someone sends six direct messages in under 45 s.
#define DM_PENDING_MAX         6
// Retries after the first send. Meshtastic's ReliableRouter does 3; with 3-4
// nodes and a 10 % duty budget, 2 is enough and costs one packet less.
#define DM_RETRIES             2
// Wait for the ACK before retrying. At SF11/BW250 a packet is ~0.5 s of air;
// the ACK has to cross back through up to one relay, and each relay waits
// ~0.45 s plus its contention window. 12 s leaves room for a busy channel. The
// random extra keeps two nodes that lost the same ACK from retrying in lockstep.
#define DM_ACK_TIMEOUT_MS      12000
#define DM_ACK_JITTER_MS        3000
// A duplicate of a direct message to us is acknowledged again only if our last
// ACK for it is older than this. Copies of the SAME try (the direct one plus a
// relay's) arrive within a couple of seconds of each other; a retry cannot come
// before DM_ACK_TIMEOUT_MS. Without it, with three nodes in range, every direct
// message cost two ACKs, each relayed again.
#define DM_REACK_MIN_MS         6000
#define DM_ACKS_RECIENTES          8
#define NODE_LONGNAME_LEN     16     // truncar: upstream permite hasta 40
#define NODE_SHORTNAME_LEN     5

// =============================================================================
//  TIEMPOS (ms)
// =============================================================================
#define TICK_MS               10

// Tick con la pantalla dormida y la radio ociosa. Lo urgente es un paquete
// entrante, y a SF11/BW250 el paquete mas corto ocupa mas de 100 ms de aire, asi
// que sondear cada 50 ms no pierde ninguno.
//
// Contrapartida conocida: con la pantalla apagada, una pulsacion mas corta que
// esto puede no verse, porque el teclado se sondea (isChange() compara contra la
// ultima lectura). Una pulsacion humana pasa de 100 ms, asi que en la practica no
// deberia notarse; si se nota, este es el numero que hay que bajar.
#define TICK_IDLE_MS          50

// Sueño minimo que merece la pena. Entrar y salir del light sleep cuesta un par
// de milisegundos a pleno reloj (reprogramar relojes, restaurar perifericos), asi
// que por debajo de esto se gasta mas de lo que se ahorra y se usa delay().
#define SLEEP_MIN_MS          10

// Cuanto se sigue dando por presente un ordenador despues del ultimo SOF del USB.
// Los SOF paran en cuanto el host suspende el puerto, asi que sin este colchon un
// hueco de milisegundos dormiria el nodo con el cable puesto. Ver hal::usbConectado().
#define USB_HISTERESIS_MS     15000

// Frecuencias del CPU. 240 MHz solo cuando hay que reaccionar rapido.
//
// 80 es el SUELO A PROPOSITO: en el ESP32-S3, con el CPU a 80, 160 o 240 MHz el
// bus APB se queda en 80 MHz, asi que los divisores de SPI (4 MHz de la LoRa,
// 20 MHz de la microSD) y los baudios del UART del GPS NO cambian. Por debajo de
// 80 el APB pasa a seguir al CPU y habria que recalcularlos todos.
#define CPU_MHZ_RAPIDO       240
#define CPU_MHZ_LENTO         80

#define SCREEN_TIMEOUT_MS     30000
#define SOS_HOLD_MS           2000   // mantener el boton G0, o ESC+DEL a la vez
#define SOS_COUNTDOWN_MS      3000
#define SOS_BURST_INTERVAL_MS 30000  // 3 envios separados 30 s
#define SOS_BURST_COUNT        3
#define SOS_REPEAT_MS         300000 // luego cada 5 min
#define SOS_REPEAT_MAX        12     // tope: una hora
#define NODEINFO_INTERVAL_MS  900000 // 15 min
#define TELEMETRY_INTERVAL_MS 600000 // 10 min
#define POSITION_MIN_DIST_M   100
#define POSITION_MAX_AGE_MS   900000 // 15 min
#define SD_FLUSH_INTERVAL_MS  1000   // como mucho un volcado por segundo
#define NODEDB_DEBOUNCE_MS    60000
#define GPS_NOFIX_WARN_MS     300000 // 5 min

// --- vigilancia del enlace ---------------------------------------------------
// En ciudad el enlace no se pierde por distancia sino por una esquina, y se
// recupera igual de rapido. Con los periodicos de arriba (10 y 15 min) un nodo
// podia llevar un cuarto de hora incomunicado sin que se notara, asi que hace
// falta un latido propio y mas corto.
//
// COSTE DE AIRE, que es lo que fija el intervalo: a SF11/BW250 un paquete de
// telemetria ocupa el aire ~0,5 s, y el limite legal son 360 s por hora (10 %).
// Un latido por minuto son ~30 s/h: **0,8 puntos de los 10**, o sea un 8 % del
// presupuesto. Por eso el latido NO se retransmite (HEARTBEAT_HOPS = 0): si lo
// relevaran los otros tres nodos, ese gasto se multiplicaria por cuatro y se
// comeria un tercio del presupuesto. Y de paso mide justo lo que interesa, que
// es si hay enlace DIRECTO; lo que llega por relevo ya lo cuentan los demas
// paquetes, que si van con saltos.
//
// OJO al modo de fallo: el latido sale como envio automatico, asi que por
// encima de DUTY_SOFT_PCT (8 %) se corta como los demas automaticos y los nodos
// empezarian a verse "perdidos" estando al lado. Con 0,8 puntos de gasto propio
// eso solo puede pasar con muchisimo trafico de chat, pero si algun dia se sube
// el ritmo del latido hay que mirar este limite antes que ningun otro.
#define HEARTBEAT_INTERVAL_MS  60000   // "sigo aqui", una vez por minuto
#define HEARTBEAT_HOPS         0       // sin relevo: mide el enlace directo
#define LINK_WEAK_MS          150000   // dos latidos y medio sin oir: flojeando
#define LINK_LOST_MS          180000   // tres latidos: se da por perdido
#define LINK_SNR_HIST_DB       3       // histeresis de la tendencia, en dB
// Un nodo apagado a proposito no puede dejar un aviso rojo para siempre: con
// usuarios niños, una alarma perpetua enseña a ignorar los avisos (es la misma
// razon por la que se quito el del conmutador de antena). Pasada esta hora deja
// de avisarse en el pie, aunque se sigue viendo en la lista de nodos.
#define LINK_FORGET_MS        3600000  // 1 hora

// =============================================================================
//  INTERFAZ
// =============================================================================
#define UI_STATUSBAR_H        12
#define UI_FOOTER_H           11   // banda del pie (ayuda o aviso), en Font0
#define UI_ROW_H              20   // fila de chat o de lista con Font2 (16 px)
#define BAT_LOW_PCT           15

// Brillo y volumen se ajustan en una escala de 1 a 5, no en unidades crudas.
// "Volumen 180" sobre 255 no le dice nada a nadie, y menos a un nino; y el
// brillo en porcentaje solo tenia 5 valores utiles, asi que era una escala de 5
// disfrazada. En NVS se sigue guardando el valor REAL (porcentaje y 0-255) y el
// nivel se deriva al vuelo, para no tener que migrar la config que ya existe.
#define UI_NIVELES             5
#define BRILLO_MIN_PCT        20   // el nivel 1 tiene que seguir siendo legible
#define BRILLO_MAX_PCT       100
#define VOLUMEN_MIN           60   // sin silencio: la alarma del SOS debe oirse
#define VOLUMEN_MAX          240

// Pantallas de la rotacion del TAB, en el orden en que se recorren.
//
// POSITION va justo detras de COMPASS porque son la misma pregunta al reves: la
// brujula dice donde esta EL OTRO, esta dice donde estoy YO. Y va en la rotacion
// y no escondida detras de un ENTER porque leer las coordenadas en voz alta por
// radio es un uso real en el campo, con prisa, y ahi hace falta llegar de un
// toque. El coste es una pantalla mas de ciclo para todo el mundo.
enum ft_screen_t {
  SCR_CHAT = 0,
  SCR_NODES,
  SCR_COMPASS,
  SCR_POSITION,
  SCR_SETTINGS,
  SCR_COUNT
};
