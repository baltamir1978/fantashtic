# Fantashtic — especificación técnica

> Firmware standalone para M5Stack Cardputer Adv + Cap LoRa-1262.
> Red privada cerrada de 3-4 nodos, compatible en el aire con Meshtastic.

---

## 0. Objetivo y contexto de uso

Un grupo familiar de 3-4 dispositivos que se comunican por LoRa 868 MHz **sin móvil, sin
infraestructura y sin internet**. Escenario típico: excursión, camping, esquí, festival,
un pueblo sin cobertura. Parte de los usuarios son **niños**, lo que gobierna casi todas
las decisiones de UI.

Criterios de éxito, en orden:

1. Un niño de 8 años consigue escribir un mensaje corto, enviarlo y ver la respuesta sin
   ayuda. Todo mensaje se teclea: **no hay mensajes predefinidos** (decisión de diseño,
   se descartaron a propósito para mantener el firmware y la UI mínimos).
2. El SOS se lanza desde cualquier pantalla con **≤ 3 segundos de interacción del usuario**
   (mantener el botón 2 s y pulsar Enter). El tiempo hasta que el paquete sale por antena
   es mayor —cuenta atrás, CAD y ventana de contención— y eso es correcto y deseado.
3. Un mensaje llega de A a C aunque solo haya enlace A↔B y B↔C.
4. La batería aguanta un día entero de uso intermitente.
5. Todo el tráfico va cifrado con una clave que solo tiene el grupo.

Lo que explícitamente **no** hacemos: Bluetooth, MQTT, app móvil, canales múltiples,
mensajes directos con PKC/Curve25519, telemetría ambiental, store & forward, traceroute,
audio/voz por LoRa.

---

## 1. Arquitectura

Firmware propio, no un fork. Reimplementamos la capa de aire de Meshtastic con la fidelidad
suficiente para que:

- Nuestros nodos hablen entre sí de forma fiable.
- Un nodo Meshtastic stock (uno de los dos Cardputer con el firmware de fábrica) pueda
  usarse como **sniffer de depuración**: configurándole el mismo nombre de canal y la misma
  PSK, debe ver nuestros mensajes de texto.
- Un nodo Meshtastic ajeno pueda relayar nuestro tráfico si estuviera en medio (no lo
  buscamos, pero no lo rompemos).

```
                 ┌──────────────────────────────────────────┐
                 │  ui/   pantallas, navegación, render     │
                 └───────────────┬──────────────────────────┘
                                 │
                 ┌───────────────▼──────────────────────────┐
                 │  app/  chat, quick msgs, SOS, posición   │
                 └───────────────┬──────────────────────────┘
                                 │
        ┌────────────────────────▼─────────────────────────┐
        │  mesh/  flooding router, dedup, node DB          │
        └────────────────────────┬─────────────────────────┘
                                 │
        ┌────────────────────────▼─────────────────────────┐
        │  proto/ header 16B, AES-CTR, Data protobuf       │
        └────────────────────────┬─────────────────────────┘
                                 │
        ┌────────────────────────▼─────────────────────────┐
        │  radio/ SX1262 + RadioLib, TX queue, CAD, duty   │
        └──────────────────────────────────────────────────┘
```

Planificador cooperativo de tareas en un único hilo (`loop()`), más una ISR para DIO1 que
solo levanta una bandera. Sin FreeRTOS tasks salvo que se demuestre necesario.

---

## 2. Capa de radio

### 2.1 Parámetros PHY

Preset por defecto **LONG_FAST**, idéntico a Meshtastic:

| Parámetro | Valor |
|---|---|
| Frecuencia | **869.525 MHz** |
| Spreading factor | **11** |
| Ancho de banda | **250 kHz** |
| Coding rate | **4/5** |
| Sync word | **0x2B** |
| Preámbulo | **16 símbolos** |
| Cabecera | explícita |
| CRC | activado |
| LDRO | activado sólo si `(1<<SF)/BW_kHz >= 16` (con SF11/250 → desactivado) |
| Potencia TX | **22 dBm conducidos** (máximo del SX1262) |

Sobre la potencia: la constante `powerLimit = 27` de la región `EU_868` de Meshtastic se
aplica al valor **conducido** de `setOutputPower()`, no es una cifra de ERP. El límite legal
de la banda son 500 mW **ERP**. Con 22 dBm conducidos y la antena de 3 dBi que trae el Cap:
22 + 3 = 25 dBm EIRP = 22.85 dBm ERP ≈ **193 mW ERP**, holgadamente dentro del límite. Si
alguna vez se añade un amplificador o una antena de más ganancia, hay que rehacer esta cuenta.

Derivación de la frecuencia (algoritmo de Meshtastic, para que quede reproducible):
la región `EU_868` va de 869.4 a 869.65 MHz.

```c
numChannels = floor((freqEnd - freqStart) / (spacing + bw/1000));   // spacing = 0 en EU_868
channel_num = hash(channelName) % numChannels;                      // djb2
freq        = freqStart + (bw/2000) + channel_num * (bw/1000);
// hash: h = 5381; para cada char: h = h*33 + c;
```

Con LONG_FAST (BW 250 kHz): `numChannels = floor(0.25/0.25) = 1` → siempre canal 0 →
`freq = 869.4 + 0.125 = 869.525 MHz`. Slot único.

**Implementar el hash djb2 desde el principio**, aunque con LONG_FAST no se use: el preset
LONG_MODERATE usa BW 125 kHz → `numChannels = 2` → la frecuencia (869.4625 o 869.5875 MHz)
sí depende del hash del nombre de canal. Sin djb2, seleccionar LONG_MODERATE aterrizaría en
una frecuencia distinta a la de un nodo stock y el fallo sería silencioso.

Presets seleccionables desde ajustes (misma tabla que Meshtastic):

| Preset | SF | BW | CR | Uso |
|---|---|---|---|---|
| LONG_FAST *(defecto)* | 11 | 250 kHz | 4/5 | equilibrio, único slot en EU |
| MEDIUM_FAST | 9 | 250 kHz | 4/5 | más rápido, menos alcance |
| SHORT_FAST | 7 | 250 kHz | 4/5 | cerca, muy rápido |
| LONG_MODERATE | 11 | 125 kHz | 4/8 | máximo alcance, 2 slots en EU |

Airtime aproximado de un mensaje corto en LONG_FAST: **0.4–0.6 s**. Cabecera sola 354 ms,
paquete máximo 2.1 s. Cada salto de relay repite ese coste.

### 2.2 Secuencia de inicialización (orden estricto)

```cpp
// 1. Base: pantalla, power, I2C interno. M5GFX reclama SPI2_HOST para el ST7789.
M5.begin();

// 2-3. Switch de antena RF del Cap LoRa-1262. SIN ESTO NO SALE POTENCIA.
//      No basta con escribir el registro de salida: al arrancar, P0 está como entrada
//      Y en alta impedancia. Los tres registros, en este orden:
antSwitch.writeReg(0x03, antSwitch.readReg(0x03) |  0x01);  // Direction: P0 = salida
antSwitch.writeReg(0x07, antSwitch.readReg(0x07) & ~0x01);  // High-Z:    P0 activo
antSwitch.writeReg(0x05, antSwitch.readReg(0x05) |  0x01);  // Output:    P0 = HIGH
assertAntennaSwitchOk();   // relee los tres y bloquea el TX si no cuadra

// 4. Bus SPI DEDICADO en SPI3_HOST. NO usar el objeto global SPI (es SPI2_HOST,
//    el de la pantalla) — remuxearlo rompería el display.
static SPIClass loraSpi(HSPI);
loraSpi.begin(/*sck=*/40, /*miso=*/39, /*mosi=*/14);

// 5. Objetos estáticos: sin new (CLAUDE.md prohíbe asignación dinámica tras setup()).
static Module  mod(/*cs=*/5, /*irq=DIO1*/4, /*rst=*/3, /*gpio=BUSY*/6,
                   loraSpi, SPISettings(4000000, MSBFIRST, SPI_MODE0));
static SX1262  radio(&mod);

// 6. begin(freq, bw, sf, cr, syncWord, power, preambleLength, tcxoVoltage)
int st = radio.begin(869.525, 250.0, 11, 5, 0x2B, 22, 16, 1.8);
if (st == RADIOLIB_ERR_SPI_CMD_TIMEOUT)          // placa sin TCXO
    st = radio.begin(869.525, 250.0, 11, 5, 0x2B, 22, 16, 0.0);

// 7-11.
radio.setDio2AsRfSwitch(true);
radio.setCRC(1);              // RADIOLIB_SX126X_LORA_CRC_ON — SX126xInterface.cpp.
                              // En LoRa el argumento es un booleano de activación; el CRC
                              // siempre es de 16 bits. RadioLib ya lo activa en begin(),
                              // se deja explícito por claridad.
radio.setCurrentLimit(140);   // OCP máximo del SX1262
radio.setDio1Action(onDio1);  // onDio1 DEBE ser IRAM_ATTR y solo levantar una bandera
radio.startReceive();
```

Dos cosas a resolver en la primera sesión con hardware:

- **Dirección I²C del PI4IOE5V6408**: escanear el bus (`Wire.beginTransmission` en bucle
  0x08–0x77) y anotarla en `CLAUDE.md`. No asumirla.
- **La ISR y todo lo que toque** deben ir marcados `IRAM_ATTR`: en Arduino-ESP32, un
  handler en flash provoca un crash cuando la caché está deshabilitada.

### 2.3 Cola de TX, CAD y colisiones

Cola de TX: array estático de **16 paquetes** (`MAX_TX_QUEUE`, igual que upstream),
dimensionado en `config.h`.

Antes de cada transmisión:

1. **CAD**, como estado de la máquina, no como llamada bloqueante.
   **No usar `radio.scanChannel()`**: esa API hace busy-wait (~21 ms a SF11/BW250 más las
   transiciones de modo) y saca la radio de RX en silencio, lo que viola la regla de
   `CLAUDE.md` de no bloquear y se come el tick de 10 ms. Usar en su lugar
   `startChannelScan()` → esperar DIO1 → `getChannelScanResult()`.
   - Canal ocupado → `startReceive()` y reintentar tras un nuevo retardo aleatorio.
   - Canal libre → transmitir. **En ambas ramas hay que volver explícitamente a
     `startReceive()`**; el CAD deja la radio fuera de recepción.
2. Retardo previo al TX, con ventana de contención (mismos valores que Meshtastic):
   `CWmin = 3`, `CWmax = 8`, `NUM_SYM_CAD = 2`.

```
slotTime_ms = max(2.25, NUM_SYM_CAD + 0.5) * (2^SF / BW_kHz) + 7.6
            = 28.1 ms  para SF11/BW250
```

- **Paquete propio** (originado aquí): `CWsize = map(utilizacionCanal%, 0..100, 3..8)`;
  retardo `= random(0, 2^CWsize) * slotTime`.
- **Relay de paquete ajeno**: `CWsize = map(snr, -20..+10, 3..8)`; retardo
  `= 2*CWmax*slotTime + random(0, 2^CWsize) * slotTime` (≈ 449 ms de offset base).
  Nótese que **peor SNR → ventana más pequeña → relaya antes**: es intencional, así los
  nodos lejanos propagan primero y los cercanos se callan al oír el duplicado.

### 2.4 Contador de duty cycle

Obligatorio. Ventana rodante de **60 minutos** dividida en 60 cubos de 1 minuto con el
airtime de TX acumulado en cada uno.

```
utilizacionTX% = suma(cubos) / 3.600.000 ms * 100
```

- Límite duro: **10 %** (banda g3: 869.4–869.65 MHz, 500 mW ERP, 10 % DC).
- Al superarlo, `radio.enqueue()` devuelve error, la UI muestra "Radio saturada — espera
  N min" y el mensaje queda en la cola de salida hasta que haya presupuesto.
- Umbral blando al **8 %**: a partir de ahí se suprimen los envíos automáticos (posición,
  nodeinfo, telemetría) y solo pasan los mensajes escritos por el usuario y los SOS.
- **El SOS ignora el umbral blando pero NO el duro.** Se documenta en la UI.

En la práctica 10 % de una hora son 360 s de aire; a ~0.5 s por mensaje eso son cientos de
mensajes. Solo se toca el límite si algo entra en bucle de relay, que es justo lo que el
contador está para atrapar.

---

## 3. Protocolo — compatibilidad Meshtastic

Todas las constantes provienen de `meshtastic/firmware` y `meshtastic/protobufs` (master).
Citar el archivo fuente en el comentario de cada constante.

### 3.1 Cabecera en el aire — 16 bytes, sin cifrar

| Offset | Bytes | Campo | Notas |
|---|---|---|---|
| 0x00 | 4 | `to` | u32 LE. `0xFFFFFFFF` = broadcast |
| 0x04 | 4 | `from` | u32 LE. Si es 0 al recibir → descartar |
| 0x08 | 4 | `id` | u32 LE |
| 0x0C | 1 | `flags` | ver abajo |
| 0x0D | 1 | `channel` | hash de canal (§3.3) |
| 0x0E | 1 | `next_hop` | último byte del NodeNum del siguiente salto; 0 = sin preferencia |
| 0x0F | 1 | `relay_node` | último byte del NodeNum del que relaya; 0 = sin asignar |
| 0x10 | ≤239 | payload | `Data` protobuf serializado y cifrado |

```
PACKET_FLAGS_HOP_LIMIT_MASK  0x07   bits 0-2
PACKET_FLAGS_WANT_ACK_MASK   0x08   bit  3
PACKET_FLAGS_VIA_MQTT_MASK   0x10   bit  4
PACKET_FLAGS_HOP_START_MASK  0xE0   bits 5-7 (shift 5)
```

`HOP_MAX = 7`. `hops_away = hop_start - hop_limit`.
**Nosotros usamos `hop_limit = 3` por defecto** — con 4 nodos sobra y limita el tráfico.

Regla de compatibilidad hacia atrás: si al recibir `hop_start == 0`, forzar `next_hop = 0` y
`relay_node = 0` ignorando lo que venga en el aire. (`hop_start` se añadió en Meshtastic 2.3;
`next_hop` y `relay_node` en 2.5 junto al `NextHopRouter`. Un `hop_start` a cero significa
firmware antiguo, y por tanto que esos dos bytes no son de fiar.)

Codificación del "último byte" de un NodeNum:
```c
uint8_t lastByte(uint32_t num) { uint8_t b = num & 0xFF; return b ? b : 0xFF; }
```
(el 0 está reservado para "sin asignar", por eso 0x00 se sustituye por 0xFF).

**Tamaños máximos — son dos límites distintos, no dos versiones del mismo:**

| Límite | Valor | Origen |
|---|---|---|
| Payload **cifrado** en el aire | **239 B** | `MAX_LORA_PAYLOAD_LEN (255) − 16`, `RadioInterface.h` |
| Campo `Data.payload` dentro del protobuf | **233 B** | `max_size:233` en `mesh.options` |

Dimensionar el campo `payload` de nanopb a **233**, no a 239: ponerlo a 239 rompe la
compatibilidad byte a byte con un nodo stock. La cifra de 237 que circula en la
documentación pública de Meshtastic está desactualizada, y el 251 que aparece en
`mesh.options` es un comentario sobre `MeshPacket.encrypted`, no sobre `Data.payload`.

Generación del `id`: contador rodante de 10 bits (`id & 0x3FF`) OR'd con 22 bits altos
aleatorios. Nunca emitir `id == 0`.

### 3.2 Cifrado — AES-CTR

```cpp
// clave de 16 bytes → AES-128-CTR ; clave de 32 bytes → AES-256-CTR
// Fantashtic usa SIEMPRE 32 bytes → AES-256-CTR
ctr.setKey(psk, 32);
ctr.setIV(nonce, 16);
ctr.setCounterSize(4);
ctr.encrypt(out, in, n);     // descifrar == cifrar
```

Construcción del nonce de 16 bytes:

| bytes 0-3 | bytes 4-7 | bytes 8-11 | bytes 12-15 |
|---|---|---|---|
| `packet_id` (u32 LE) | 0x00 ×4 | `from` NodeNum (u32 LE) | 0x00 ×4 (contador CTR) |

Ambos campos salen de la cabecera **sin cifrar**, así que cualquier receptor puede
construir el nonce. `from` es siempre el emisor original, nunca el que relaya.

CTR no aporta integridad. Como el `Data` va en protobuf, un descifrado con clave incorrecta
produce basura que casi nunca parsea: **la validación es "el protobuf parsea Y
`portnum != 0`"**. Rechazar si no.

> Nota de seguridad honesta: esto es exactamente lo que hace Meshtastic. Protege contra un
> curioso con un SDR, no contra un atacante decidido — no hay MAC, así que un tercero podría
> alterar bits del texto cifrado sin que lo detectemos. Para uso familiar es más que
> suficiente; conviene que Bruno lo sepa.

### 3.3 Hash de canal (byte 0x0D)

```c
uint8_t xorHash(const uint8_t* p, size_t n) { uint8_t c = 0; while (n--) c ^= *p++; return c; }
uint8_t channelHash = xorHash(name, strlen(name)) ^ xorHash(psk, psk_len);
```

Se calcula sobre la **clave expandida, con la longitud que tenga** — 16 bytes si el canal
es AES-128, 32 si es AES-256 — nunca sobre la forma abreviada de 1 byte. Fantashtic usa
siempre 32, pero la función debe recibir la longitud como parámetro: el test de regresión
de §10 (`"LongFast"` + PSK por defecto → 0x08) usa la clave de **16** bytes, y sin eso ni
el test pasa ni hay interoperabilidad con un canal AES-128.

Es solo una pista para acelerar el descarte; la validación real es el parseo del protobuf.

### 3.4 Derivación de la PSK

Fantashtic **no** usa la PSK por defecto de Meshtastic. Dos modos:

**Modo frase (por defecto, el que usará Bruno).** El usuario teclea la misma frase en los
4 nodos. Derivación:

```
PSK[32] = PBKDF2-HMAC-SHA256(passphrase, salt=PBKDF2_SALT, iters=20000, dkLen=32)
```

20 000 iteraciones en el S3 tardan ~0.5 s: aceptable, se hace una sola vez al guardar los
ajustes, no en cada arranque (la PSK derivada se guarda en NVS).

**El salt es por despliegue, no del proyecto.** `config.h` trae `"fantashtic-v1"` por
defecto —es el que reproduce los vectores de `docs/VECTORES.md` y los tests— y cada grupo lo
sustituye por uno propio en `include/secrets.h`, que está en `.gitignore`. El salt nunca fue
el secreto (la frase sí), pero uno por grupo impide que una sola tabla precalculada contra
Fantashtic sirva contra todos los despliegues: es un *pepper*.

Consecuencia operativa: la PSK derivada vive en NVS, así que cambiar el salt **no** rompe los
nodos ya configurados. Pero un nodo nuevo derivará otra clave de la misma frase. Al poner o
cambiar el salt hay que repasar la frase en **todos** los nodos.

> Punto débil real, y no es el salt: **20 000 iteraciones son pocas** para una frase corta y
> memorizable. Contra un atacante con GPU, la defensa es la longitud de la frase, no el
> número de iteraciones. Subirlas invalidaría las claves de los nodos ya configurados, así
> que se deja anotado en lugar de cambiarlo a la ligera.

**Modo hex (avanzado).** Introducir 64 caracteres hexadecimales a mano, o generarla en un
nodo (`esp_random()`) y mostrarla en pantalla en 4 líneas de 16 chars para copiarla.

El nombre del canal se deriva también de la frase para que dos grupos con frases distintas
no colisionen en el hash de canal: `channelName = "FT-" + primeros 5 chars de
base32(SHA256(passphrase))`. Guardarlo en NVS junto a la PSK.

### 3.5 El mensaje `Data` (lo que se cifra)

```proto
message Data {
  PortNum portnum          = 1;   // varint
  bytes   payload          = 2;   // max 233
  bool    want_response    = 3;
  fixed32 dest             = 4;
  fixed32 source           = 5;
  fixed32 request_id       = 6;
  fixed32 reply_id         = 7;
  fixed32 emoji            = 8;
  optional uint32 bitfield = 9;
}
```

Atención: `dest`, `source`, `request_id`, `reply_id` y `emoji` son **`fixed32`, no varint**
(4 bytes cada uno cuando no son cero).

Portnums que implementamos:

| Portnum | # | Contenido | Uso en Fantashtic |
|---|---|---|---|
| `TEXT_MESSAGE_APP` | 1 | UTF-8 en crudo | chat y SOS |
| `POSITION_APP` | 3 | `Position` | coordenadas GPS |
| `NODEINFO_APP` | 4 | `User` | nombre corto/largo, id |
| `ROUTING_APP` | 5 | `Routing` | ACK / NAK |
| `TELEMETRY_APP` | 67 | `Telemetry` | batería (`DeviceMetrics`) |

Todo lo demás se ignora silenciosamente al recibir (pero se relaya igual, ver §4).

Usar **nanopb** con los `.proto` oficiales de `meshtastic/protobufs` para `Position`,
`User`, `Routing` y `Telemetry`. El `Data` y la cabecera se pueden serializar a mano si
resulta más claro, pero es preferible nanopb también para `Data`.

### 3.6 ACKs

**Broadcast (nuestro caso normal):** `want_ack` se elimina antes de transmitir un broadcast.
La confirmación es **implícita**: si oímos a alguien retransmitir un paquete cuyo `from`
somos nosotros, damos el mensaje por entregado y paramos los reintentos. En la UI eso es
el paso de "enviando" a "✓ propagado".

**Unicast (solo lo usamos para el ACK del SOS):** el destinatario responde con
`ROUTING_APP`, `Routing.error_reason = NONE` y `Data.request_id = id del original`.
Reintentos del originador: 3.

---

## 4. Router de flooding

Con 3-4 nodos no hace falta `NextHopRouter`. Implementar **solo flooding**, que es lo que
Meshtastic usa para broadcasts de todas formas. Dejar el campo `next_hop` siempre a 0.

### 4.1 Tabla de duplicados

- Clave: **(`from`, `id`)**.
- Array fijo de **128 entradas**, desalojo del más antiguo (no hay expiración por tiempo,
  igual que Meshtastic).
- Cada entrada guarda además hasta 3 `relay_node` que hemos oído retransmitirla, para poder
  detectar el ACK implícito.
- Los paquetes propios se insertan **antes** de transmitir, para ignorar nuestros ecos.
- Paquetes con `id == 0` no se registran ni se relayan nunca.

### 4.2 Decisión de retransmisión

Hacen falta **dos predicados distintos**, y confundirlos rompe toda la malla:

```c
bool esParaMostrar(p) { return p.to == miNodeNum || p.to == BROADCAST; }  // capa app
bool esUnicastParaMi(p) { return p.to == miNodeNum; }                     // capa router
```

Retransmitir si y solo si:

```
!esUnicastParaMi(p) && !esNuestro(p) && p.hop_limit > 0 && p.id != 0 && !esDuplicado(p)
```

Es decir: **un broadcast se entrega localmente Y se retransmite**. Si se usara
`esParaMostrar()` en el predicado del router, ningún broadcast se relayaría nunca — y los
broadcasts son el 100 % de nuestro tráfico normal.

Al retransmitir: `hop_limit--`, `relay_node = lastByte(nuestroNodeNum)`. El resto de la
cabecera y el payload **se reenvían intactos, sin descifrar**. Relayar no requiere tener la
clave — y es bueno que sea así.

Al recibir un duplicado de algo que aún tenemos en la cola de TX: **cancelar nuestro envío**
(alguien ya lo propagó). Si el duplicado trae un `hop_limit` mayor que nuestra copia
encolada, sustituir la copia por la nueva.

### 4.3 Base de datos de nodos

Array estático de **16 nodos** (sobra para 4, deja margen para vecinos ajenos):

```c
struct NodeEntry {
  uint32_t num;            // NodeNum
  char     shortName[5];   // upstream User.short_name = max_size:5 (NUL incluido): encaja
  char     longName[16];   // upstream User.long_name = max_size:40. ¡TRUNCAR!
  uint32_t lastHeard;      // millis del último paquete
  int8_t   snr;            // último SNR
  int16_t  rssi;
  uint8_t  hopsAway;
  uint8_t  battery;        // %
  bool     hasPosition;
  int32_t  latI, lonI;     // grados * 1e7
  int32_t  altitude;
  uint32_t posTime;
};
```

**Truncado obligatorio de nombres entrantes.** `User.long_name` admite hasta 40 bytes en el
protobuf oficial (el firmware stock recorta a 24 al guardar). Nuestro campo son 16 bytes:
todo `NODEINFO_APP` de un nodo ajeno —y en la Fase 3 vamos a poner uno stock en el mismo
canal a propósito— debe copiarse con una copia acotada (`strlcpy` o equivalente), nunca con
`strcpy`. Un `strcpy` aquí es un desbordamiento en el primer NODEINFO externo que llegue.

`NodeNum` propio = últimos 4 bytes de la MAC de la eFuse (igual que Meshtastic).

Se persiste al cambiar (con antirrebote de 60 s) y al apagar, para que tras un reinicio la
lista de nodos no aparezca vacía: a `/fantashtic/nodes.bin` si hay tarjeta, y siempre
también a NVS como respaldo. Nunca en un temporizador periódico.

---

## 5. Aplicación

### 5.1 Chat

- Buffer circular de **60 mensajes** en RAM. Todo mensaje enviado o recibido se añade
  además al log de la microSD (§7.1); al recorrer el historial más allá de los 60 en RAM,
  se leen del fichero. Sin tarjeta, el historial son esos 60 y se pierden al reiniciar.
- Cada entrada: `from`, texto (buffer de **200 bytes**), timestamp, estado
  (`enviando` / `propagado` / `fallido` / `recibido`), SNR si es entrante.
- Longitud máxima de mensaje: **180 bytes UTF-8**, no 180 caracteres. La distinción importa:
  los acentos ocupan 2 bytes y un emoji 4, así que 180 caracteres podrían ser 720 bytes.
  El contador de la UI cuenta **bytes** y el truncado nunca parte un code point por la mitad.
- **Navegación del historial con los cursores**: en la pantalla de chat, ↑/↓ recorren los
  mensajes antiguos y `ESC` vuelve al final. Mientras se está desplazando, la línea de
  entrada se atenúa y un indicador discreto muestra la posición; un mensaje entrante no
  arrastra la vista, solo marca que hay algo nuevo abajo.
- Cada nodo tiene un **color** asignado determinísticamente desde su `NodeNum`, y su
  `shortName` de 4 caracteres. Un niño identifica quién habla por color, no por leer.

### 5.2 SOS

Requisitos:

- Accesible desde **cualquier** pantalla.
- Difícil de disparar por accidente, fácil de disparar a propósito.
- Debe salir aunque el resto del firmware esté en mal estado.

Activación: **mantener el botón físico (GPIO 0) 2 segundos**. Aparece pantalla roja a
pantalla completa con una cuenta atrás de **3 segundos**:

```
        ⚠  SOS  ⚠

   Enviando en  3 …

  ENTER = enviar ya
   ESC  = cancelar
```

Al confirmarse (o al agotarse la cuenta):

- Se envía `TEXT_MESSAGE_APP` con el texto `"🆘 SOS — <shortName>"` más, si hay fix GPS,
  `" @ <lat>,<lon>"`, seguido de un `POSITION_APP` separado.

**Política de repetición** (única, sustituye a los "3 reintentos" genéricos de §3.6, que
aplican solo al ACK de unicast normal):

1. Ráfaga inicial: **3 envíos separados 30 s**.
2. Después: **1 envío cada 5 minutos**, hasta un máximo de **12** (una hora).
3. La repetición se detiene si el usuario cancela **o** si llega cualquier mensaje de otro
   nodo posterior al SOS.
4. Las repeticiones van como broadcast. Solo si hay un nodo concreto que respondió se envía
   además un unicast con `want_ack = true` a ese nodo.
5. El SOS ignora el umbral blando de duty cycle pero **no el duro**. Si el duro bloquea un
   envío, la UI lo dice de forma explícita en la propia pantalla de SOS.

En **recepción** de un SOS:

- Pantalla roja intermitente que se impone sobre cualquier cosa, sin timeout.
- Tono de alarma repetido por el altavoz (ES8311) hasta que se pulse una tecla.
- LED RGB en rojo parpadeante. **Nota de hardware**: el LED comparte raíl con la
  retroiluminación (GPIO 38), así que encender el LED implica encender la pantalla. No
  existe una notificación luminosa de bajo consumo con la pantalla apagada.
- El mensaje queda fijado arriba en el chat.
- Un solo toque de tecla ofrece **responder "Voy hacia ti"** al que lo envió.

### 5.3 Posición y distancia

- GPS ATGM336H por `Serial1` a 115200, parseado con TinyGPSPlus.
- Se emite `POSITION_APP` cuando: hay fix nuevo **y** (nos hemos movido > 100 m **o** han
  pasado > 15 min). Con umbral blando de duty cycle superado, se suprime.
- Pantalla de **brújula**: elige un nodo y muestra una flecha grande hacia él más la
  distancia. La flecha se orienta con el rumbo del GPS si hay movimiento; en parado se usa
  el norte geográfico y se avisa ("N ↑ arriba"). El BMI270 **no** es magnetómetro, así que
  no hay rumbo real en parado — no prometer lo que no hay.
- Distancia: fórmula de haversine. Mostrar en metros hasta 999, luego en km con un decimal.
- El GPS puede apagarse desde ajustes (ahorro de batería); estado visible en la barra.

### 5.4 Telemetría

`TELEMETRY_APP` / `DeviceMetrics` con nivel de batería y tensión, cada 10 minutos, sujeto
al umbral blando de duty cycle. Batería leída del ADC en GPIO 10 con multiplicador ≈ 2.04,
promediada sobre 16 muestras.

---

## 6. Interfaz de usuario

Pantalla de **240 × 135**. Es diminuta: fuentes grandes, poco texto, mucho color.

### 6.1 Barra de estado (12 px, siempre visible)

```
┌────────────────────────────────────────────────────────┐
│ PAPA   ●●●○  3 nodos   📡 4%   🛰 ok   🔋 82%          │
└────────────────────────────────────────────────────────┘
```

De izquierda a derecha: nuestro `shortName`, calidad del último enlace (4 puntos según
SNR), nodos vistos en los últimos 15 min, uso de duty cycle (rojo por encima del 8 %),
estado del GPS, batería.

### 6.2 Pantallas

| # | Pantalla | Contenido |
|---|---|---|
| 1 | **Chat** | historial navegable con los cursores + línea de entrada abajo. Pantalla de inicio |
| 2 | **Nodos** | nombre, hace cuánto se oyó, barras SNR, batería, distancia |
| 3 | **Brújula** | flecha grande hacia el nodo seleccionado + distancia |
| 4 | **Ajustes** | ver §6.4 |
| — | **SOS** | superpuesta, se impone sobre todo |

Cuatro pantallas en la rotación. La de mensajes rápidos se eliminó junto con la función.

### 6.3 Teclado

El Cardputer Adv tiene 56 teclas leídas por el TCA8418 vía `M5Cardputer.Keyboard`.

| Tecla | Acción |
|---|---|
| Escribir + `Enter` | enviar mensaje (en la pantalla de Chat) |
| `↑` / `↓` (`;` / `.`) | **recorrer el historial de chat**; en listas, mover la selección |
| `` ` `` / `Esc` | volver al final del historial / atrás / cancelar |
| `Tab` | siguiente pantalla |
| `Shift`+`Tab` | pantalla anterior |
| `Fn` + `↑` / `↓` | brillo de pantalla |
| Botón G0 (2 s) | **SOS** |
| Botón G0 (pulsación corta) | silenciar alarma / despertar pantalla |

Confirmar el mapeo real de las flechas en la primera sesión con hardware; en el Cardputer
original son `;`(↑) `.`(↓) `,`(←) `/`(→) y el Adv debería mantenerlo.

### 6.4 Ajustes en el dispositivo

Menú de lista, navegación con flechas, `Enter` para editar.

**Básicos** (accesibles siempre):

- Nombre del nodo (largo, 15 chars) y nombre corto (4 chars)
- Brillo de pantalla
- Volumen / silencio
- GPS encendido/apagado

**Avanzados** (tras confirmación de dos pasos con aviso de que puede dejar el nodo
incomunicado):

- Frase del canal → deriva PSK + nombre de canal
- Introducir/mostrar PSK en hexadecimal
- Preset de modem
- Potencia TX (2…22 dBm)
- Hop limit (1…7, por defecto 3)
- Restablecer valores de fábrica
- Info: NodeNum, versión, hash de canal, frecuencia, uso de duty cycle

Sin bloqueo por PIN — es innecesario y añade una forma más de quedarse fuera. La
confirmación de dos pasos ya evita accidentes.

### 6.5 Estados de error visibles

Nada de fallos silenciosos. Banners persistentes para:

- `RADIO NO DETECTADA` — el SX1262 no responde al `begin()`
- `SIN ANTENA?` — heurística: TX correcto pero cero paquetes recibidos en 10 min con otros
  nodos activos
- `RADIO SATURADA — Nmin` — límite duro de duty cycle alcanzado
- `SIN GPS` — GPS activado pero sin fix en 5 min
- `¿ANTENA O SWITCH RF?` — heurística: hemos transmitido con éxito pero no hemos recibido
  ni un paquete en 10 min habiendo otros nodos activos.
  **Aviso honesto**: esto solo detecta el problema *después* de haber transmitido, que es
  justo la acción que destruye el SX1262 si falta la antena. El SX1262 no mide potencia
  reflejada ni ROE, así que **no existe detección previa al TX**. La protección real son
  las otras dos: la confirmación explícita del primer TX y la relectura de los registros
  del PI4IOE5V6408 (§2.2)
- `BATERÍA BAJA` — por debajo del 15 %

---

## 7. Almacenamiento

Dos niveles, con responsabilidades separadas: la **flash interna (NVS)** guarda lo poco que
el dispositivo necesita para arrancar y comunicarse; la **microSD** guarda todo lo que crece
con el tiempo.

### 7.1 microSD — historial y registros

El Cardputer Adv trae ranura microSD (CS **12**, sobre el mismo bus SPI3 que la LoRa).
Formato **FAT32**, librería `SD` de Arduino-ESP32 montada sobre la misma instancia
`loraSpi`.

```
/fantashtic/
  chat/2026-07.log      historial mensual, append-only
  nodes.bin             copia de la base de datos de nodos
  campo/2026-07-29.csv  registros de pruebas de campo (ver ALCANCE.md)
```

**Formato del historial: log de texto, append-only, una línea por mensaje.**

```
1753790400\t7A3B12C4\tPAPA\tRX\t-92\t6.5\tBajamos en 20 min
```

epoch · NodeNum · nombre corto · RX/TX · RSSI · SNR · texto (con `\n` escapado).

Append-only es la elección correcta aquí: escribir un mensaje cuesta abrir, añadir una línea
y cerrar, sin reescribir nada. Rotación mensual, sin límite práctico de historial. Y como es
texto plano y tabulado, la tarjeta se puede sacar y abrir el historial en cualquier
ordenador o importarlo a una hoja de cálculo sin herramientas especiales.

### 7.2 Sin tarjeta, el firmware sigue funcionando

La microSD es **opcional**. Si no monta al arrancar:

- El chat funciona igual, pero solo con los **60 mensajes** del buffer en RAM, que se
  pierden al reiniciar.
- La base de datos de nodos cae al respaldo en NVS.
- La barra de estado muestra un indicador `SD?` discreto y permanente.
- **Nunca se bloquea el arranque ni se muestra un error modal por no haber tarjeta.**

Se monta una sola vez en `setup()`. Nada de detección de extracción en caliente: si alguien
saca la tarjeta en marcha, las escrituras fallan en silencio y se enciende el indicador.

### 7.3 Arbitración SPI — el punto delicado

microSD y LoRa comparten SCK/MOSI/MISO. Una escritura en SD puede tardar **decenas de
milisegundos** (y en una tarjeta mala, cientos) si le toca un borrado de bloque. Durante ese
tiempo no se puede tocar el SX1262.

No es fatal —el SX1262 mantiene el paquete en su FIFO hasta que lo leamos, y un paquete en
LONG_FAST tarda ~500 ms en el aire— pero hay que ser disciplinado:

1. **Escrituras diferidas.** Los mensajes se encolan en RAM y se vuelcan cuando la radio
   está ociosa: ni transmitiendo, ni con un paquete pendiente de leer del FIFO, ni con un
   CAD en curso.
2. **Como mucho un volcado por segundo**, y se vacía la cola entera en ese volcado
   (abrir, escribir N líneas, cerrar) en vez de abrir el fichero una vez por mensaje.
3. **Volcado forzado** al enviar o recibir un SOS, al entrar en ajustes y al apagar.
4. La bandera `spiBusy` cubre ambos periféricos. Nunca dos CS bajos a la vez.
5. La ISR de DIO1 no toca el SPI: solo levanta la bandera, y la lectura del FIFO ocurre en
   el bucle principal cuando el SPI está libre.

### 7.4 Flash interna (NVS) — namespace `fantashtic`

Con el historial en la tarjeta, en NVS solo quedan claves pequeñas más la copia de
respaldo de nodos (~1 KB). Cabe de sobra en la partición `nvs` estándar, así que
**`default_8MB.csv` vale** y no hace falta tabla de particiones propia.

| Clave | Tipo | Contenido |
|---|---|---|
| `psk` | blob 32 B | clave del canal derivada |
| `chname` | str | nombre del canal |
| `lname` / `sname` | str | nombres del nodo |
| `preset` | u8 | preset de modem |
| `txpwr` | u8 | potencia TX en dBm |
| `hoplim` | u8 | hop limit |
| `bright` | u8 | brillo |
| `vol` | u8 | volumen |
| `gpson` | bool | GPS activado |
| `nodes` | blob | copia de respaldo de la base de datos de nodos (~1 KB) |

El historial de chat **no** se guarda en NVS. Vive en la microSD (§7.1) o, si no hay
tarjeta, solo en RAM.

Primer arranque sin `psk`: asistente de configuración → pedir nombre y frase del canal.

---

## 8. Consumo de energía

Batería de 1750 mAh. Objetivo: un día de uso intermitente.

- Pantalla apagada tras **30 s** sin teclas (se despierta con cualquier tecla o al recibir
  un mensaje). Recordar que apagar la retroiluminación apaga también el LED RGB: comparten
  raíl (GPIO 38).
- El SX1262 en RX continuo es lo que domina el consumo en reposo. **Es una decisión de
  diseño para la v1, no una limitación del hardware**: el SX1262 soporta
  `startReceiveDutyCycleAuto(preambleLength, 8)` y upstream lo usa en placas de esta misma
  clase — de hecho el preámbulo de 16 símbolos se eligió precisamente para eso. Queda como
  la primera optimización a probar si la autonomía no llega.
- El GPS es el mayor consumidor: apagable, y con opción "solo bajo demanda" que lo enciende
  únicamente al pedir posición o al disparar un SOS.
- Sin light sleep del ESP32 en la v1 (rompe la recepción). Anotado como mejora futura si
  hace falta.
- Medir consumo real en las fases de verificación y documentarlo en el README.

---

## 9. Plan por fases

Cada fase debe compilar, flashear y ser verificable en hardware antes de pasar a la
siguiente. **No avanzar sin verificar.**

### Fase 0 — Andamiaje
`platformio.ini`, `partitions_fantashtic.csv`, estructura de carpetas, `config.h`,
`secrets.example.h`, `.gitignore`, README. Compila y arranca mostrando "Fantashtic".
**Verificación:** flashea y se ve el texto.

### Fase 1 — HAL
Pantalla (M5GFX), teclado (eco de teclas), batería (%), LED RGB, altavoz (un pitido),
escáner I²C que imprime todas las direcciones por consola, y **montaje de la microSD** con
lectura/escritura de un fichero de prueba.
**Verificación:** escribir en el teclado se ve en pantalla; el escáner I²C lista TCA8418,
BMI270, ES8311 y **el PI4IOE5V6408 — anotar su dirección en `CLAUDE.md`**; la SD monta,
escribe y relee. Probar también **arrancando sin tarjeta**: debe seguir funcionando.

### Fase 2 — Radio en crudo
Inicialización del SX1262 con la secuencia de §2.2, incluida la secuencia completa de tres
registros del expansor y su relectura. Ping-pong: **el TX solo se dispara al pulsar una
tecla**, nunca automáticamente ni en el arranque (restricción 1 de `CLAUDE.md`). El receptor
muestra el contador con RSSI/SNR.
**Verificación:** los dos Cardputer se ven. Anotar RSSI/SNR a 1 m y a 100 m.
**⚠️ Antena atornillada antes de encender. Confirmar en pantalla antes del primer TX.**

### Fase 3 — Protocolo Meshtastic
Cabecera de 16 bytes, AES-CTR con nonce, hash de canal, `Data` protobuf con
`TEXT_MESSAGE_APP`. Tests unitarios en host (`env:native`) con vectores conocidos.
**Verificación crítica:** configurar el **tercer** dispositivo (o uno de los dos) con
firmware Meshtastic stock, región EU_868, preset LongFast, nombre de canal y PSK idénticos
a los nuestros → **debe ver nuestros mensajes de texto en su pantalla**. Si esto funciona,
el protocolo está bien. Si no, no seguir.

### Fase 4 — Router de flooding
Tabla de duplicados, decremento de hop limit, retardo con ventana de contención, CAD,
cancelación por duplicado, ACK implícito.
**Verificación:** prueba de 3 nodos. Alejar A y C hasta que no se oigan directamente,
dejar B en medio; los mensajes de A deben llegar a C. Comprobar en la consola de B que
relaya y que no hay bucles.

### Fase 5 — Contador de duty cycle
Ventana rodante, umbrales blando y duro, indicador en la barra de estado.
**Verificación:** test unitario que inyecta airtime sintético; en hardware, enviar en bucle
hasta ver el bloqueo y confirmar que el minutero de recuperación es correcto.

### Fase 6 — Chat y UI
Barra de estado, pantalla de chat, entrada de texto, **navegación del historial con los
cursores**, colores por nodo, persistencia.
**Verificación:** conversación de ida y vuelta entre dos nodos; recorrer el historial hacia
atrás y volver con `ESC`; reiniciar y comprobar que el historial sobrevive; sacar la
microSD, meterla en un ordenador y abrir `/fantashtic/chat/AAAA-MM.log` — debe leerse.
Repetir toda la fase **sin tarjeta** para confirmar que degrada bien.

### Fase 7 — Nodos
`NODEINFO_APP`, `TELEMETRY_APP`, base de datos de nodos, pantalla de nodos.
**Verificación:** los 3-4 nodos aparecen con nombre, batería y SNR correctos.

### Fase 8 — GPS y brújula
`POSITION_APP`, TinyGPSPlus, haversine, pantalla de brújula.
**Verificación:** prueba en exterior; contrastar la distancia mostrada contra Google Maps
en dos puntos separados ≥ 500 m.

### Fase 9 — SOS
Detección de pulsación larga, pantalla de confirmación, repetición, recepción con alarma,
respuesta de un toque.
**Verificación:** disparar SOS desde cada nodo; confirmar que despierta a los demás desde
pantalla apagada y que la alarma suena.

### Fase 10 — Ajustes en el dispositivo
Menú, PBKDF2 sobre la frase, entrada hexadecimal, confirmación de dos pasos, asistente de
primer arranque.
**Verificación:** cambiar la frase en los 4 nodos y comprobar que siguen comunicándose y
que un nodo con la frase antigua deja de leerlos.

### Fase 11 — Pulido
Estados de error, gestión de brillo/apagado de pantalla, medición de consumo, README con
instrucciones de puesta en marcha en español para el resto de la familia.

---

## 10. Pruebas

**En host (`env:native`, sin hardware)** — todo lo que hay en `proto/` y `mesh/`:

- Ida y vuelta de serialización de la cabecera de 16 bytes
- Construcción del nonce contra vectores conocidos
- Ida y vuelta de AES-CTR
- `xorHash` del canal: comprobar el caso documentado — nombre `"LongFast"` con la PSK por
  defecto de Meshtastic (los **16** bytes `d4 f1 bb 3a 20 29 07 59 f0 bc ff ab cf 4e 69 01`)
  da hash **0x08** (`0x0A ^ 0x02`). Es un buen test de regresión aunque no usemos esa clave,
  y obliga a que la función acepte longitudes de clave de 16 y 32.
- djb2 del nombre de canal: `hash("LongFast") = 130429955`. Casos de referencia:
  US → 104 slots, slot 20 → 906.875 MHz; EU_433 → 4 slots, slot 4 → 433.875 MHz.
- `lastByte()` incluyendo el caso 0x00 → 0xFF
- Tabla de duplicados: inserción, desalojo, detección
- Lógica de decisión de relay: tabla de casos
- Ventana de duty cycle con reloj inyectado
- Haversine contra distancias conocidas

**En hardware** — la checklist de verificación de cada fase, arriba.

---

## 11. Referencias

Documentación M5Stack:
- https://docs.m5stack.com/en/core/Cardputer%20Adv
- https://docs.m5stack.com/en/core/Stamp-S3A
- https://docs.m5stack.com/en/cap/Cap_LoRa-1262
- https://shop.m5stack.com/products/cap-lora-1262-for-cardputer-adv-sx1262-atgm336h

Protocolo Meshtastic (leer el código, no solo los docs):
- `src/mesh/RadioInterface.h` / `.cpp` — cabecera, presets, tabla de regiones, retardos
- `src/mesh/CryptoEngine.cpp` — AES-CTR y nonce
- `src/mesh/Channels.cpp` / `.h` — hash de canal, PSK por defecto
- `src/mesh/FloodingRouter.cpp` — lógica de relay
- `src/mesh/PacketHistory.cpp` — tabla de duplicados
- `src/mesh/Router.cpp` — duty cycle, encode/decode
- `meshtastic/protobufs`: `mesh.proto`, `portnums.proto`, `channel.proto`, `config.proto`
- `variants/esp32s3/m5stack_cardputer_adv/variant.h` — pinout de referencia
- https://meshtastic.org/docs/overview/mesh-algo/
- https://meshtastic.org/docs/overview/encryption/

Normativa:
- ETSI EN 300 220-2. Subbanda **g3**: 869.4–869.65 MHz, **500 mW ERP, 10 % duty cycle**.
  Es donde Meshtastic sitúa EU_868 y donde operamos nosotros.
- Como referencia, la subbanda g1 (868.0–868.6 MHz) solo permite 25 mW y 1 % — por eso
  **no** usamos 868.0 pese al nombre coloquial "LoRa 868".
