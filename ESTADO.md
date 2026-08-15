# Estado del proyecto y plan de la primera sesión con hardware

Documento de traspaso. `CLAUDE.md` tiene el contexto permanente; esto es lo que cambia.

---

## 1 de agosto de 2026 — macOS, cobertura vigilada y mensajes largos

**El proyecto ya se compila y se prueba en macOS**, y es la ruta limpia: `brew install
platformio`, `pio run` en 6 min la primera vez (se baja el toolchain) y **73 tests en verde**.
Nada de lo de Windows aplica aquí: ni MAX_PATH, ni la unión, ni PowerShell, ni instalar un
`g++` aparte. Ver §"Compilar" de `CLAUDE.md`. **Sin verificar en macOS**: flashear y el
monitor serie — no ha habido ningún aparato enchufado a este ordenador.

### Vigilancia de cobertura (pensada para ciudad)

**El problema**: en urbano el enlace no se pierde por distancia sino por una esquina, y se
recupera igual de rápido. Hasta ahora lo más frecuente que emitía un nodo era la telemetría
cada 10 min, así que **se podía llevar un cuarto de hora incomunicado sin que nada lo dijera**
— y la barra de estado contaba «activos» con una ventana de 15 minutos, que arrastraba el
mismo retardo.

**El latido**, en `app::loop()`: cada nodo emite un `Telemetry` **cada minuto** (`HEARTBEAT_INTERVAL_MS`)
y **con `hop_limit = 0`** (`HEARTBEAT_HOPS`). Las dos decisiones tienen el mismo motivo, el aire:

- A SF11/BW250 un paquete ocupa el aire ~0,5 s y el límite legal son 360 s/hora. Un latido por
  minuto son ~30 s/h: **0,8 puntos de los 10**, un 8 % del presupuesto.
- **Sin relevo**, porque si lo repitieran los otros tres nodos ese gasto se multiplicaría por
  cuatro. Y de paso mide lo que hace falta: si hay enlace **directo**. Lo que llega por salto ya
  lo cuentan los demás paquetes.
- El periódico de 10 min cuenta como latido, para no soltar dos paquetes iguales seguidos.

**Los cuatro estados** (`mesh::estadoEnlace()`, función libre y sin estado para poder probarla
con relojes inventados):

| Estado | Cuándo | Qué significa en la calle |
|---|---|---|
| `DIRECTO` | oído de primera mano hace < 2,5 min | todo bien |
| `RELEVO` | sigue llegando, pero solo por salto | **el aviso útil**: se perdió la vía directa y se está al borde |
| `DEBIL` | 2,5 min callado | dos latidos perdidos |
| `PERDIDO` | 3 min callado | se da por incomunicado |

`RELEVO` es la razón de ser de todo esto: cuando el tercero que hace de puente se vaya, el
enlace se cae de golpe, y eso se puede ver venir.

**Dónde se ve** (se eligió no añadir pantalla nueva: con niños cada pantalla del TAB cuenta):

- **Barra de estado**: `3/4` con el color del **peor** enlace de la tabla (verde directo,
  amarillo por relevo, naranja flojeando, rojo perdido). Sustituye al contador de activos.
- **Lista de nodos**: la antigüedad y los puntos de señal van en el color del estado, `1sal` se
  pinta en amarillo cuando lo que llega viene por un tercero, y hay un **triángulo verde o rojo**
  con la tendencia de la señal — que en ciudad importa más que el nivel, porque dice si hay que
  pararse a mandar el mensaje ya o se puede seguir andando. El pie dice el estado **en palabras**
  para el nodo seleccionado, que es lo que da leyenda a los colores.
- **Aviso en la banda inferior**: «Sin cobertura: PONS», por delante del GPS y de la tarjeta y
  por detrás de la batería. Sin sonido, por decisión expresa.

**La tendencia** es una media exponencial de un cuarto sobre el SNR, con histéresis de 3 dB
(`LINK_SNR_HIST_DB`): sin ella la flecha cambiaría de sentido en cada latido, porque el SNR de
un enlace quieto ya baila un par de dB solo.

**Nada de esto avisa para siempre.** Pasada una hora (`LINK_FORGET_MS`) un nodo callado deja de
contar en la barra y de generar aviso: un aparato apagado a propósito no puede dejar una alarma
encendida, que es la misma razón por la que se quitó el aviso del conmutador de antena. Se sigue
viendo en la lista, que es donde se mira el histórico.

**16 tests nuevos** (64 → 80), todos con relojes inyectados: los saltos de estado tardan tres
minutos en ocurrir de verdad. Se ha dejado además constancia en `test_sin_saltos_no_se_retransmite`
de que **de ese caso depende el latido**: si alguien "optimizara" el router para descartar las
difusiones con `hop_limit = 0`, la vigilancia dejaría de funcionar sin que nada más se rompiera.

**⚠ Modo de fallo que hay que conocer**: el latido sale como envío automático, así que por encima
de `DUTY_SOFT_PCT` (8 %) se corta igual que los demás automáticos, y los nodos empezarían a verse
«perdidos» estando al lado. Con 0,8 puntos de gasto propio eso pide muchísimo tráfico de chat,
pero si algún día se sube el ritmo del latido, este límite es lo primero que hay que mirar.

**⚠ Sin ver con los ojos, y aquí hace falta salir a la calle.** Lo que hay que comprobar:

1. Dos nodos juntos: la barra tiene que decir `1/1` en verde y la lista `directo`.
2. Apagar uno: a los 3 min el otro debe decir `0/1` en rojo y sacar «Sin cobertura». Al volver a
   encenderlo, verde otra vez en menos de un minuto.
3. **La prueba de ciudad**: alejarse doblando esquinas y mirar si la tendencia baja **antes** de
   perder el enlace. Es lo único que dice si el aviso llega a tiempo de servir para algo.
4. Con tres aparatos, comprobar que el estado pasa a `RELEVO` (amarillo) cuando solo se llega por
   el de en medio. Esta es la que necesita el tercer aparato y la que valida la idea entera.
5. Que el gasto de aire medido cuadre: la barra tiene el porcentaje de ciclo de trabajo al lado.

### Los mensajes largos ya no se pierden

**El fallo**: el chat pintaba **un mensaje = una fila** y truncaba a 22 caracteres, así que de
un mensaje de 180 bytes se leía el primer cuarto y **el resto no era alcanzable desde el
aparato**. Y los 22 caracteres eran una cuenta sobre "unos 8 px por carácter" cuando **Font2 es
proporcional**: `MMMM…` se salía de la pantalla y `iiii…` dejaba media fila vacía.

**El arreglo**, en dos piezas:

- **`util/wrap.{h,cpp}`** — `util::envolver()` trocea un texto en líneas que quepan en un ancho
  **en píxeles**. Corta por espacios y, si una palabra sola no cabe (una URL, un nombre
  pegado), la parte por carácter **sin romper el UTF-8**. La medida entra como función
  (`MedirFn`), y eso no es adorno: `util/` no depende de Arduino, así que el troceador se prueba
  en el ordenador con una regla falsa mientras el aparato le pasa `D.textWidth()`.
  **9 tests nuevos** (64 → 73), incluidos el que comprueba que no se pierde ni un carácter y el
  que exige que los cortes de un texto con eñes sigan siendo UTF-8 válido.
- **`ui.cpp`** — `componerChat()` arma la pantalla por **líneas** y no por mensajes, de abajo
  arriba, y `pantallaChat()` la pinta al revés. El nombre del remitente y la marca de estado
  van **solo en la primera línea**; las continuaciones se alinean con la columna del texto
  (x=58), que es lo que hace que un mensaje largo se lea como un párrafo y no como tres
  mensajes seguidos.

**Dos consecuencias que conviene saber:**

1. **El scroll del chat cuenta ahora líneas, no mensajes**, y su tope sale de la propia
   composición (`chatHayMasArriba()`) en vez de restar `numMensajes() - chatVisibles()`. Con la
   resta, en cuanto un mensaje ocupaba dos filas el principio del historial dejaba de
   alcanzarse — el mismo fallo que ya se arregló una vez al pasar a Font2, que habría vuelto
   por otra puerta.
2. **La línea de entrada muestra la cola de lo escrito, no el principio.** Iba por un
   `snprintf` a 45 bytes, así que al pasar de una pantalla el cursor se quedaba fuera y se
   escribía a ciegas. Reusa `colaQueCabe()`, que ya hacía esto en el editor de ajustes (se ha
   movido de sitio en el archivo para que la vean los dos).

**⚠ Sin ver con los ojos.** Compila (RAM 15,4 %, flash 20,0 %) y los 73 tests pasan, pero
**nadie ha mirado la pantalla**: no había aparato conectado. Qué hay que comprobar:

- Un mensaje de 180 bytes: que se lea entero repartido en filas, que no se pise con el nombre
  y que las continuaciones queden alineadas.
- Que el scroll llegue **hasta el primer mensaje** del historial y no se pase.
- Un mensaje de una sola palabra larguísima: que se parta y no desaparezca.
- Un mensaje con acentos y emoji justo en el corte: que no salga basura (es lo que los tests
  cubren en el ordenador, pero la fuente del aparato es la que manda).
- Escribir 60 caracteres y ver que el cursor sigue a la vista.

**Última actualización**: 30 de julio de 2026, **segunda sesión con hardware**. Lo de hoy:
pantalla de posición propia, SOS por teclado a **ESC+DEL**, y el **light sleep descartado por
medida** (se arregló una causa real y no bastó: sigue quedándose tonto a los 30 s). **64 tests en
verde.**

Nodos: **COM10 = `130C92AC` 'COVA'**, **COM11 = `130C9A3C` 'PONS'**.

---

## ✅ YA EJECUTADO EN EL APARATO (29 de julio)

El firmware **compila, se flashea y arranca**. Se acabó el "escrito pero nunca ejecutado".

- `pio run` → **SUCCESS**, cero avisos en nuestro código. RAM 15,1 % (49 352 B),
  flash 19,0 % (633 621 B). Queda sitio de sobra.
- `pio test -e native` → **63 casos, 63 en verde**, reproducidos de verdad en este
  ordenador (antes era una afirmación, no una comprobación).
- Flasheado en COM10, hash verificado, y **Fase 1 superada**:

| Comprobación | Resultado medido |
|---|---|
| Arranque | ✓ sin panic, `=== Fantashtic v0.1.0 ===` |
| Pantalla | ✓ pinta; se vio el error rojo `RADIO NO DETECTADA` |
| microSD | ✓ montada, 7610 MB totales / 175 MB usados |
| NVS | ✓ abre; `SIN CONFIGURAR` → irá al asistente |
| Teclado TCA8418 | ✓ **0x34**, y sigue vivo DESPUÉS del escaneo I²C |
| Audio ES8311 | ✓ **0x18** |
| IMU BMI270 | ✓ **0x69** (la tabla decía 0x68/0x69; es 0x69) |
| GPS | ✓ UART1 arranca en rx 15 / tx 13 |
| Radio | ✓ **se niega correctamente** con el centinela 0xFF |
| ID de nodo | ✓ `130C92AC`, derivado de la MAC como se esperaba |

Tres correcciones necesarias para llegar hasta aquí, todas en `CLAUDE.md`:

1. **MAX_PATH**: hay que compilar por la unión `C:\Users\baltamirano\ft` y desde
   PowerShell. No era un fallo del código; ver §"Compilar" de `CLAUDE.md`.
2. **`hal/keys.cpp`**: `KeysState` de M5Cardputer 1.1.1 no tiene
   `up/down/left/right/esc/backspace`. Las flechas se sintetizan a mano y Fn **no**
   corta `st.word`. Los comentarios del archivo afirmaban lo contrario.
3. **Tests**: faltaba `#include <stdio.h>` en `test_msgs` y `test_util`.

Y se quitó `nanopb` de `lib_deps`: ningún fuente lo incluye, pero obligaba a instalar
protobuf + grpcio-tools y a correr un paso de generación en cada compilación.

### ✅ La radio arranca (29-jul, tras relajar la restricción nº 2)

```
radio: 869.525 MHz  SF11  BW250  CR4/5  22 dBm  slot 28.1 ms
nodo 130C92AC  antena: el expansor no responde  radio: OK  tarjeta: si
```

- **`SX1262::begin()` funciona a la primera con TCXO a 1,8 V.** NO aparece
  `radio: sin TCXO, reintentando con XTAL`. Queda resuelta la discrepancia: 1,8 V (lo que
  dicen `CLAUDE.md` y Meshtastic) es correcto; el `3.0, true` del ejemplo de M5 no hace
  falta en este Cap. **Deuda cerrada.**
- `slot 28.1 ms` concuerda con el cálculo teórico para SF11/BW250.
- El SX1262 responde por SPI, así que **el Cap está bien asentado**: su bus SPI va fino.
  Lo único que no llega son las líneas I²C del expansor.

**Queda abierto**: que salga potencia de verdad por el RP-SMA no está medido. Lo decide la
prueba nº 4 (dos nodos, RSSI a 1 m). ≈ −40 dBm → el camino de RF está bien y la decisión
de seguir sin expansor queda confirmada. Peor que −70 dBm → el conmutador sí hacía falta.

### ✅ GPS verificado (29-jul)

```
GPS:   430 bytes NMEA, 9 sat, CON FIX  alt 740 m  hdop 7.1
GPS: 16848 bytes NMEA, 9 sat, CON FIX
```

Fix conseguido **en interior**, con 9 satélites. Caudal ~820 B/s, coherente con NMEA a
1 Hz. **El sentido del UART queda confirmado: rx=15, tx=13.** Deuda cerrada; ya no hay que
probar a intercambiarlos. Valida además el parseo de satélites y altitud y la integración
de TinyGPSPlus. HDOP 7,1 es alto, normal bajo techo.

(`informeGps()` en `main.cpp` imprime esto cada 5 s. Ojo: solo corre **después** de
confirmar la antena, porque `gpsLoop()` está detrás de ese seguro.)

### Cambios de interfaz tras probar con los dos nodos (29-jul)

Salieron de usar el aparato de verdad, no de leer el código:

1. **Quitado el aviso rojo permanente del conmutador de antena.** Como el expansor nunca
   responde, salía fijo señalando una avería inexistente, robaba 11 px y desplazaba la
   línea de entrada. Con usuarios niños, una alarma perpetua que no significa nada enseña
   a ignorar los avisos. Queda el mensaje por consola de `radioBegin()`.
2. **La confirmación de antena se pide UNA VEZ**, no en cada arranque (`antena_ok` en
   NVS). El Cap va atornillado fijo; repetirla no añadía seguridad y acababa pulsándose
   sin leer. Sigue apareciendo en el primer arranque y tras restablecer de fábrica.
3. **Chat en `Font2` (16 px)**: `Font0` (6×8) es ilegible para los niños. 4 líneas en vez
   de 5, paso 20 px, texto en x=58, truncado a 22 caracteres.
   **Ya extendido a nodos, brújula y ajustes** (ver abajo).
4. **NODEINFO bajo demanda.** El nombre solo se enviaba al arrancar (+5 s) y cada 15 min,
   así que un nodo que encendiera más tarde mostraba un número hexadecimal durante un
   cuarto de hora. Ahora, al oír a alguien sin nombre conocido, se adelanta el NODEINFO
   propio con retardo aleatorio de 2-6 s.

### ✅✅ ENLACE DE RADIO VERIFICADO ENTRE DOS NODOS (29-jul)

```
nodo 2:  RX de 130C92AC id 88FFDC05  -18 dBm  SNR 6.8  saltos 0/3  -> entregar y retransmitir
nodo 1:  RX de 130C92AC id 88FFDC05  -19 dBm  SNR 6.2  saltos 1/3  -> ack implicito
```

**La prueba nº 4 queda superada y cierra la cuestión del conmutador de antena.**
−18 dBm a corta distancia, muy por encima del criterio fijado de antemano (≈ −40 dBm
bien / peor que −70 dBm mal). **Sale potencia real por el RP-SMA sin el expansor**: DIO2
gobierna el TX/RX correctamente. La relajación de la restricción nº 2 queda validada con
medida, no con analogía.

Verificado de paso, todo en el aire real y entre dos aparatos independientes:

- **Cripto y protocolo**: cabecera de 16 B, AES-CTR con nonce, hash de canal y clave
  derivada por PBKDF2. Antes solo estaba contrastado con vectores en el ordenador.
- **Router**: decide `entregar y retransmitir` en difusión (la trampa nº 3 no ocurre).
- **ACK implícito**: el emisor se oye retransmitido y el contador de saltos sube 0 → 1.
- **SNR 6,2-7,0**, coherente.

**El sentido contrario también queda verificado** (misma noche, tras el flasheo final): en la
consola del nodo 1 aparece `RX de 130C9A3C ... saltos 0/3 -> entregar y retransmitir`, o sea
un mensaje **originado** en el nodo 2 y recibido de primera mano. Deuda cerrada.

### ✅ Segunda unidad: el expansor tampoco está (29-jul)

Flasheado un **segundo Cardputer Adv con su propio Cap** (nodo `130C9A3C`, COM11). Su
diagnóstico es **idéntico** al del nodo 1:

```
interno: 0x18 / 0x34 / 0x69      externo: nada
0x43 y 0x44: sin ACK en los dos buses, las dos velocidades, los dos drivers
radio: 869.525 MHz  SF11  BW250  CR4/5  22 dBm  slot 28.1 ms
```

**Dos unidades independientes con el mismo comportamiento.** Queda descartado que sea un
Cap defectuoso: el expansor no está poblado (o no está cableado al host) en este modelo,
por diseño. La decisión de avisar y continuar (restricción nº 2) queda respaldada por una
segunda muestra, no solo por analogía con Meshtastic/MeshCore.

Su SX1262 también arranca **a la primera con TCXO a 1,8 V**. Segunda confirmación.

Nodos para las pruebas: **`130C92AC`** (COM10) y **`130C9A3C`** (COM11).

### Historial: por qué el expansor del Cap no responde

Con el **Cap LoRa-1262 montado y la antena atornillada**, el escaneo del bus interno
devuelve **solo 3 dispositivos**, los tres del propio Cardputer:

```
0x18  ES8311     0x34  TCA8418     0x69  BMI270
```

**No aparece 0x43 ni ninguna dirección desconocida.** El PI4IOE5V6408 no hace ACK, así
que `I2C_ADDR_ANTSW` sigue sin poder rellenarse y la radio sigue bloqueada — que es el
comportamiento correcto y deliberado, no una avería del firmware.

#### Lo que ya se ha descartado (no repetirlo)

Se añadió `hal::i2cDiagnostico()`, que escanea los dos buses y además sondea el expansor
de forma dirigida. Resultado en el aparato:

```
--- bus interno (SDA 8 / SCL 9): puerto 1, SDA 8, SCL 9, activo ---
  0x18 ES8311    0x34 TCA8418    0x69 BMI270
--- bus externo (Grove/EXT, 2 / 1): puerto 0, SDA 2, SCL 1, activo ---
  (nada responde en este bus)
--- sondeo dirigido del PI4IOE5V6408 (bus interno) ---
  0x43 a 100 kHz: sin ACK     0x43 a 400 kHz: sin ACK
  0x44 a 100 kHz: sin ACK     0x44 a 400 kHz: sin ACK
```

Descartado por tanto:

1. **No está en el bus externo.** Los dos buses se inicializan (puerto 1 y puerto 0,
   como manda la restricción nº 7) y el externo está completamente vacío.
2. **No es la dirección.** Se probaron 0x43 y 0x44.
3. **No es la velocidad.** Se probaron 100 kHz y 400 kHz.
4. **No es el bus.** Tres dispositivos responden en el interno en el mismo arranque.
5. **No es falta de alimentación por software.** `Power_Class::setExtOutput()` no tiene
   caso para `board_M5CardputerADV`: no hay riel de 5 V conmutable desde el firmware.
6. **No es que el chip no exista.** La documentación de M5 confirma address 0x43 en
   `m5::In_I2C` a 400 kHz, y los registros coinciden con `PI4IOE5V6408_Class`.

`I2C_ADDR_ANTSW` ya está puesto a **0x43**, la dirección real. Eso **no** relaja la
seguridad: `antennaBegin()` exige releer los tres registros, así que mientras el chip no
conteste el estado será `NO_RESPONDE` y la radio seguirá sin arrancar. Lo único que
cambia es que el error en pantalla pasa de "dirección sin configurar" a "el expansor no
responde", que es más honesto.

#### Contraste con MeshCore: el driver NO es la causa

Se replicó **literalmente** el método de MeshCore (`Wire1` de Arduino, I2C_NUM_1, GPIO 8/9,
100 kHz, 0x43 — ver `CardputerAdvBoard.h` y `globals.h` de
`MultiMote/meshcore-cardputer-adv`):

```
--- contraste con Wire1 de Arduino (como MeshCore) ---
  Wire1 0x43: sin ACK (endTransmission=2)
  Wire1 0x44: sin ACK (endTransmission=2)
  Wire1 ve: 0x18 0x34 0x69
  despues, M5.In_I2C sigue viendo el TCA8418 (0x34): SI
```

Mismo resultado que con `M5.In_I2C`. Dos drivers independientes, el mismo puerto y los
mismos pines: **el expansor no está en el bus**. (La prueba es inofensiva: el teclado
sobrevive. `Wire1` es I2C_NUM_1, el mismo puerto que usa M5Unified, así que no
remultiplexa pines — el peligro de la restricción 7 es `Wire`, que es I2C_NUM_0.)

#### Dato clave: "funcionaba en MeshCore" no prueba que el expansor respondiese

`radio_init()` de MeshCore hace `if (err != 0) MESH_DEBUG_PRINTLN("Cap LoRa-1262 not
found");` y **sigue adelante** hasta `radio.std_init(&SPI)`. Meshtastic hace lo mismo en
`variant.cpp`: si `findLoraCapBus()` devuelve `nullptr`, registra `LOG_ERROR` y arranca la
radio igual. Los dos firmwares tratan la ausencia del expansor como no fatal y transmiten.

Además, M5 documenta que el **Cap LoRa868 no lleva expansor** y que en ese caso no
responder en 0x43 es normal. O sea: existe una configuración conocida y válida en la que
la radio funciona sin expansor, con el conmutador TX/RX gobernado por DIO2 del SX1262
(`setDio2AsRfSwitch(true)`, que nuestro `radioBegin()` ya hace).

Nuestro firmware es, de los tres, el único que se niega a arrancar. Esa fue una decisión
deliberada de la restricción nº 2, pero conviene saber que es más estricta que la de
Meshtastic y MeshCore, no el consenso.

#### Escaneo SIN el Cap: idéntico

Arrancando con el Cap desmontado, el diagnóstico da **exactamente el mismo resultado** que
con el Cap puesto: 0x18 / 0x34 / 0x69 en el interno, nada en el externo, sin ACK en
0x43 ni 0x44. El Cap no carga el bus ni añade nada: **sus líneas I²C no llegan al host**.

Combinado con que el usuario **transmitía con MeshCore en este mismo aparato**, y que
MeshCore nunca configuró el expansor (habría registrado "Cap LoRa-1262 not found" y
seguido), la conclusión es que en este Cap el expansor **no interviene en el camino de RF**:
el conmutador TX/RX lo hace DIO2 del SX1262. El caso de "transmitir sin P0 en alto" ya ha
ocurrido muchas veces en este hardware sin daño.

#### Lo que queda por comprobar: es físico

Agotado el software, la sospecha es el contacto o la alimentación del Cap:

- Reasentar el Cap (quitarlo y volverlo a poner haciendo tope).
- Ver si el Cap necesita los **5 V** del conector: su pinout tiene `5V_IN` (pin 7) y
  `5V_OUT` (pin 5), y con solo batería ese riel podría estar caído. Probar alimentado
  por USB, que es como se hizo esta sesión, y también con la batería conectada.
- Comprobar que es el Cap **LoRa-1262 (U214)** y no el LoRa868, que no lleva expansor.
- Escanear **sin** el Cap: debe seguir saliendo exactamente 0x18 / 0x34 / 0x69. Si sale
  algo distinto, el Cap sí está tocando el bus y el problema es otro.

> **Resuelto por medida, no por analogía.** Este párrafo decía que sin el expansor no había
> que transmitir de ninguna manera, porque la potencia podía ir contra un circuito abierto y
> llevarse el amplificador. El razonamiento era correcto como precaución, pero la prueba nº 4
> lo zanjó: **−18 dBm a corta distancia entre dos aparatos**, muy por encima del criterio de
> −40 dBm fijado de antemano. Sale potencia real por el RP-SMA sin el expansor, así que
> DIO2 del SX1262 gobierna el TX/RX él solo y el conmutador no está en el camino de RF.
>
> Lo que queda por comprobar es físico y de curiosidad, no de seguridad: reasentar el Cap y
> mirar si necesita los 5 V del conector (`5V_IN` pin 7, `5V_OUT` pin 5).

---

## Interfaz al completo en Font2 (29-jul, segunda tanda)

Flasheado en los dos nodos, compilando limpio y con los 64 tests en verde.
**La geometría está calculada, no vista: falta mirar las cuatro pantallas.**

- **Nodos, brújula y ajustes pasan a Font2**, cerrando lo que quedaba del punto 3 de
  arriba. En nodos y ajustes las filas pasan de 14 a 20 px, así que caben 5 en vez de 7.
- **Nodos y ajustes tienen ventana deslizante.** Cierra el fallo de dibujar 8 filas
  dejando seleccionables 16: `ajustarVentana()` mueve la ventana lo justo para que la fila
  resaltada quede a la vista, y el pie muestra `4/12` cuando hay filas fuera. La comparten
  las dos pantallas a propósito, para que el fallo no pueda reaparecer en una sola.
- **Fila de nodos con dos fuentes**: nombre y distancia en Font2 (lo que un niño mira), el
  resto en Font0. No es un descuido: a 8 px por carácter no caben seis campos en 240 px.
  La columna de detalle se sitúa midiendo el nombre con `D.textWidth()`, no a ojo.
- **El valor de cada fila de ajustes se recorta midiendo.** En Font2 "Nombre del nodo" se
  come media pantalla y un nombre largo se pisaba con la etiqueta.

Tres fallos encontrados leyendo el código, los tres consecuencia del cambio a Font2:

1. **El mensaje más viejo del chat era inalcanzable.** `pantallaChat()` bajó a 4 líneas
   pero el tope del scroll se quedó en `numMensajes() - 5`. Con 6 mensajes el scroll se
   topaba en 1 cuando hacían falta 2. Ahora los dos números salen de `chatVisibles()`.
2. **El aviso del pie se salía por abajo.** `pieAviso()` pinta una banda de 11 px y lo
   llamaba el chat con Font2 (16 px) activa, así que el texto quedaba recortado. Ahora
   fuerza Font0 él mismo.
3. **Restaurar la fuente era una obligación fácil de olvidar** al añadir una pantalla.
   Ahora `pintar()` deja Font0 y tamaño 1 antes de cada pantalla.

### Qué mirar en el aparato

Solo se puede comprobar con los ojos:

- **Nodos**: que nombre, antigüedad, barras de señal, saltos, batería y distancia no se
  pisen. El caso apretado es un nodo con nombre de 4 caracteres anchos, `999h`, `3sal`,
  `100%` y `99,9 km` a la vez.
- **Ajustes**: bajar hasta "Información" y comprobar que la ventana arrastra y que el
  contador del pie cuadra. Y que "Nombre del nodo" con un nombre largo no se solapa.
- **Chat**: subir hasta arriba del historial y comprobar que **se llega al primer
  mensaje** (era el fallo 1). Y quitar la microSD para forzar el aviso del pie y ver que
  ya no está recortado.
- **Brújula**: que el nombre de la cabecera no toque el círculo de la flecha.

Y de la segunda tanda (preferencias editables, sin probar en el aparato):

- **Nombre y nombre corto**: cambiarlos y comprobar que el nuevo aparece en el chat del OTRO
  aparato en segundos, no a los 15 minutos (es lo que hace `app::anunciarNombre()`).
- **Nombre corto**: que salga en mayúsculas aunque se teclee en minúsculas, y que el tope de
  4 caracteres pite en vez de dejar seguir.
- **Editor con texto largo**: escribir una frase de 60 caracteres y comprobar que la caja
  muestra **el final** y no el principio (`colaQueCabe()`), y que no se sale del recuadro.
- **Frase del grupo**: entrar, ver que el editor sale **vacío** (no se precarga: no se
  guarda), que ENTER lleva a la confirmación y que ESC en la confirmación **vuelve al editor**
  sin perder lo escrito. Y que al confirmar sale "Generando la clave..." antes de bloquearse.
- **La prueba de verdad de la frase**: cambiarla en un solo aparato y comprobar que dejan de
  verse; cambiarla en el otro con la misma frase y que vuelven a hablarse. Es la prueba 9.
- **Información**: nueve filas en Font0, que quepan sin pisar el pie, y que ESC salga a
  ajustes en vez de al chat.
- **Brillo y volumen**: que la escala vaya 1→5 y dé la vuelta a 1, y que el nivel 1 de brillo
  siga siendo legible a la luz del día.

---

## ✅ Reloj y tick adaptativos, verificados en el aparato (29-jul)

Primeras dos medidas de autonomía, y **comprobadas en COM10 con la consola delante**, no
solo compiladas:

```
21:46:46  energia: CPU 240 MHz  tick 10 ms  pantalla on   bateria 100% (4203 mV)
21:46:51  energia: CPU 80 MHz   tick 50 ms  pantalla off  ...  <-- cambio de reloj
21:48:09  RX de 130C9A3C id 53158001  -28 dBm  SNR 6.5  saltos 0/3 -> entregar y retransmitir
21:48:17  energia: CPU 80 MHz   tick 10 ms  pantalla off
21:48:21  RX de 130C92AC id 177B9405  -29 dBm  SNR 7.2  saltos 1/3 -> ack implicito
```

Lo que eso demuestra, en orden:

1. **El reloj baja a los 30 s exactos** de la última tecla, junto con el tick.
2. **El GPS sigue con fix a 80 MHz** (NMEA fluyendo, HDOP 1,6). Era el riesgo principal:
   `setCpuFrequencyMhz()` tiene que recalcular los divisores del UART, y lo hace.
3. **La radio recibe a 80 MHz** (`RX de 130C9A3C`, de primera mano).
4. **Y transmite a 80 MHz**: el `ack implicito` es su propio relevo oído de vuelta.
5. **El tick adaptativo hace lo que dice**: a las 21:48:17 está a 10 ms con la pantalla
   apagada, porque la radio no estaba ociosa. Pillado en el acto.

Detalle sin explicar, para vigilarlo: el RSSI de esta tanda es **−28/−29 dBm**, y en las
anteriores era −18/−21. Lo más probable es que los aparatos se hayan movido entre reflasheos
(han estado con los cables por medio). Bajar el reloj no puede afectar a la potencia de RF,
así que no parece una regresión — pero conviene volver a mirarlo con los dos nodos quietos.

**No hay ninguna medida de consumo todavía.** Lo verificado es que el mecanismo funciona y no
rompe nada; cuánto ahorra en mA sigue sin medirse.

---

## ✅ Descubrimiento de nodos arreglado (29-jul), salido de usar el aparato

**Síntoma**: la pantalla de nodos decía «No se ve a nadie» hasta que el otro nodo mandaba un
mensaje de texto, y entonces aparecía con el **número hexadecimal** en vez del nombre corto.

**Causa**: la lógica de «NODEINFO bajo demanda» estaba **al revés**. Al oír a alguien cuyo
nombre no se sabía, se anunciaba el nombre **propio** — pero lo que hace falta es que el otro
anuncie el suyo. Solo funcionaba por casualidad, cuando los dos lados se desconocían a la vez.
En cuanto uno ya conocía al otro, su regla no se disparaba, se quedaba callado, y el que
acababa de arrancar seguía ciego hasta el siguiente envío periódico: **hasta 15 minutos**.

Y pasaba **siempre** que un nodo se reiniciaba con el otro encendido, porque la tabla de nodos
vive en RAM y se pierde al arrancar. Es decir, en cada sesión de flasheo.

**Arreglo**: `Data.want_response`, campo 3 de `mesh.proto`. No estaba implementado —
`proto::Data` solo tenía portnum y payload.

- El NODEINFO del arranque y el que dispara oír a un desconocido van **pidiendo respuesta**.
- Quien recibe un NODEINFO con `want_response` contesta con el suyo, y **sin** pedir respuesta:
  si no, dos nodos se contestarían el uno al otro indefinidamente.
- El periódico de cada 15 min **no** la pide: sería hacer contestar a los cuatro nodos cada
  cuarto de hora sin motivo.
- Se codifica **solo si es true**, porque proto3 omite los valores por defecto. De eso depende
  que los paquetes existentes sigan saliendo byte a byte iguales, y `test_vectores` lo
  comprueba.

**Verificado en el aparato**, en el escenario exacto que fallaba (COM11 encendido, COM10
reiniciado):

```
22:07:28  radio: transmision PERMITIDA        <- COM10 arranca, tabla vacia
22:07:34  RX de 130C9A3C ... -> entregar y retransmitir
22:07:34     ...NODEINFO de 130C9A3C, nodos en tabla: 1
22:07:36  nodos: total 1  | 130C9A3C 'PONS' hace 2s
```

**Nombre aprendido en 7 s** desde el arranque, sin que nadie escriba nada. **64 tests** (uno
nuevo, `test_data_want_response`).

De paso se añadieron dos diagnósticos de consola que hicieron falta para esto y se quedan:

- La traza de RX ahora dice **qué portnum** llegó (`...NODEINFO de 130C9A3C, nodos en tabla: 1`)
  y avisa explícitamente si un paquete **no descifra**, que antes desaparecía sin rastro.
- `informeNodos()` vuelca cada 10 s la tabla tal cual, con un `SIN NOMBRE` visible. Es
  exactamente lo que pinta la pantalla 2, así que permite distinguir un fallo de la tabla de
  un fallo del dibujado.

---

## ⛔ El light sleep NO VALE: desactivado otra vez (30-jul, medido)

**Resultado, y manda sobre todo lo que sigue en esta sección.** Con `gpio_wakeup` en vez de
`ext1`, el nodo desenchufado **sigue quedándose tonto a los 30 s** — o sea en cuanto se duerme
la pantalla y empieza a dormir de verdad. Deja de responder a todo. Y aparece un síntoma nuevo
que no se había visto: **el altavoz CREPITA** al entrar y salir del sueño.

Conclusión honesta: **el arreglo del mux del GPIO era necesario pero NO suficiente.** La causa
que se encontró era real (está documentada abajo y en la tabla de trampas de `CLAUDE.md`, y
explica por qué la red de seguridad no saltaba), pero **queda al menos otra causa sin encontrar**.

Desactivado, detrás de `-DFT_LIGHT_SLEEP`. En un aparato cuya razón de ser es recibir un SOS no
se embarca algo que se queda sordo medio minuto después de quedarse solo. **Y el objetivo del
proyecto es el funcionamiento standalone**, así que aquí no hay término medio: si dormir cuesta
la radio, no se duerme.

### Si algún día se retoma, esto es lo que YA está descartado

1. **No es `ext1` moviendo el pin al dominio RTC.** Eso era real y está arreglado
   (`gpio_wakeup_enable` + `esp_sleep_enable_gpio_wakeup`, un nivel por pin, con la ISR de DIO1
   callada durante el sueño y el flanco de subida restaurado al despertar). Sigue fallando.
2. **No es el UART del GPS comiéndose las tramas.** También arreglado (previsión de la ráfaga
   con el intervalo medido). Sigue fallando.
3. **No es la consola.** El guardián del USB impide dormir con un ordenador enchufado, así que
   el fallo ocurre precisamente cuando NO hay cable.

**Pistas nuevas que no se han seguido**, y por dónde empezaría la próxima vez:

- **El crepitar del altavoz** es un dato, no una molestia: apunta a que el ES8311 / NS4150B
  pierde reloj o alimentación en el sueño y vuelve mal. Si eso le pasa al códec, conviene
  sospechar que le pasa a más periféricos — y el SX1262 va por SPI3, que también se
  reprograma. **Probar `M5.Speaker.end()` antes de dormir**, y mirar si el SPI necesita
  reinicializarse al despertar.
- No se ha comprobado si la **microSD** sobrevive al sueño (`store::` escribe historial).
- No se ha probado a **reinicializar la radio** al despertar (`startReceive()` incondicional),
  que sería la prueba de si lo que se pierde es el estado del SX1262 y no la interrupción.

### Lo que SÍ se queda de esta sesión, y vale por sí solo

- La causa del mux del GPIO, documentada: explica por qué el rearme por nivel de `radioLoop()`
  no salvaba el nodo, que era lo que de verdad desconcertaba.
- **`$PCAS02` no baja el ritmo del módulo.** Medido con fix y sin él: pedido 5000 ms, la ráfaga
  sigue llegando **cada ~1000 ms** (`rafaga cada 999 ms` en consola) y el caudal solo baja de
  ~630 a ~400 B/s. Esto **corrige la documentación**, que prometía ~40 B/s, y significa que el
  ajuste de ritmo del GPS ahorra mucho menos de lo que se creía.
- **Los satélites usados pueden ser MÁS que los "a la vista"**: medido `8/6`. GGA cuenta los
  usados de todas las constelaciones y `$GPGSV` solo los de GPS, así que el número de GSV es un
  suelo. La pantalla de posición detecta ese caso y no imprime un "8 de 6" absurdo.
- **Abrir el puerto serie resetea el nodo** y le roba el teclado a quien lo esté usando. Para
  observar un nodo, no abrir su puerto.

---

## ~~✅ El light sleep, reescrito y activo (30-jul)~~ — NO VALIÓ, ver arriba

**La causa del nodo sordo está localizada, y era más profunda que la hipótesis.** La hipótesis
decía que `ext1` mataba la ISR de `setDio1Action()`. Es verdad, pero no explicaba lo que de
verdad desconcertaba: **por qué no saltó la red de seguridad.** `radioLoop()` termina con

```cpp
if (digitalRead(PIN_LORA_DIO1)) g_irq = true;   // rearme por nivel
```

que está puesto exactamente para el caso de perder un flanco. Debería haber recuperado el nodo
en el tick siguiente, y no lo hizo.

**Por qué**: `esp_light_sleep_start()` llama a `ext1_wakeup_prepare()`, que hace
**`rtc_gpio_init()`** sobre cada pin de la máscara. Eso saca el pin de la matriz GPIO digital y
lo pasa al mux del dominio RTC — y al despertar **nadie lo devuelve**. Con el GPIO 4 en el
dominio RTC, `digitalRead(4)` deja de leer el pin igual que la ISR deja de dispararse. **Las dos
vías las mata la misma causa**, así que la red de seguridad estaba tan muerta como lo que
vigilaba. Con eso, la máquina de estados de la radio se queda parada para siempre: eso es el
"sordo". Y el "mudo" era consecuencia, no causa: no contestaba al NODEINFO porque nunca lo oyó.

**El arreglo, confirmado contra Meshtastic** (`src/sleep.cpp`, `enableLoraInterrupt()`, que corre
sobre el mismo SX1262 con RadioLib): `gpio_wakeup_enable(LORA_DIO1, GPIO_INTR_HIGH_LEVEL)` +
`esp_sleep_enable_gpio_wakeup()`, que despiertan **sin tocar el mux**. La hipótesis apuntada
ayer era la correcta y se resolvió en una consulta. De regalo, `gpio_wakeup` admite un nivel
**por pin**, así que ahora también despierta el **botón** (activo bajo), que con `ext1` no cabía.

Detalles del código que no son adorno, cada uno con su motivo, en `hal::dormirHasta()`:

1. **Solo se arman las fuentes que estén en reposo.** Una línea ya activa despertaría en el acto
   y el light sleep sería un no-op que gasta más que el `delay()`. El caso real es el INT del
   teclado, que se queda bajo hasta que alguien vacíe la cola del TCA8418.
2. **La ISR de DIO1 se calla al dormir** (`gpio_intr_disable`) y se le devuelve el flanco de
   subida al despertar. Motivo: `gpio_wakeup_enable()` cambia el tipo de interrupción a "por
   nivel", y una interrupción por nivel se repite mientras la línea siga alta — o sea una
   tormenta de ISR hasta que el bucle limpie la IRQ por SPI. El despertar no necesita la ISR
   (lo dispara el bit de wakeup del pin), y el aviso no se pierde porque el rearme por nivel de
   `radioLoop()` vuelve a funcionar, ahora que el pin sigue siendo digital.
3. **No se duerme con un ordenador en el USB.** Ver la advertencia de abajo sobre su alcance.
4. **No se duerme encima de una ráfaga NMEA.** Ver la sección siguiente: es un efecto colateral
   que no estaba previsto y que habría roto los avisos de posición en silencio.

### ⚠ Lo que el guardián del USB NO arregla

`hal::usbConectado()` mira los SOF del USB (`HWCDC::isPlugged()`), así que distingue un
ordenador de un cargador tonto. Sirve para **no perder una consola ya abierta**, y eso resuelve
la vieja trampa de "el light sleep tumba el USB-Serial-JTAG".

**No sirve como red de seguridad para flashear**, y conviene saberlo antes de pelearse con ello:
Windows **suspende** el puerto cuando ningún programa lo tiene abierto, los SOF paran,
`isPlugged()` pasa a false, el nodo se duerme, y dormido ya no hay USB al que resetear. Pasó en
esta sesión con COM10. **No es un ladrillo**: el bootloader del ESP32-S3 está en ROM y se
recupera con interruptor **OFF** → mantener **G0** → dar corriente → soltar.

Se decidió a propósito dejar el light sleep **siempre activo** en vez de esconderlo detrás de un
ajuste: el objetivo del aparato es funcionar solo con su batería, no ser cómodo de flashear.

### Y de paso, dos medidas que corrigen la documentación

- **`$PCAS02` no hace lo que se creía.** Con el ritmo pedido a 5000 ms, el caudal NMEA baja de
  ~630 a ~400 B/s, no a los ~40 B/s que decía `hal/gps.h`, y **la ráfaga sigue llegando cada
  ~1000 ms** (`rafaga cada 1000 ms` en la consola). El AT6668 no hace caso del todo. Esto tumbó
  la primera versión de mi propio arreglo, que predecía la ráfaga con el periodo *pedido*: habría
  dormido encima de cuatro ráfagas de cada cinco. Ahora se usa el **menor** del pedido y el
  **medido**: equivocarse despertando antes no cuesta nada, equivocarse despertando tarde pierde
  la trama.
- **Abrir el puerto serie resetea el nodo** (`rst:0x15 USB_UART_CHIP_RESET`), porque en el
  USB-Serial-JTAG DTR/RTS gobiernan reset y modo de arranque. Para observar un nodo sin
  perturbarlo hay que mirarlo **desde la consola del otro**, no abriendo el suyo.

### 📌 LA PRUEBA QUE FALTA, y es la que decide

No está medido que el nodo siga oyendo la radio dormido. El procedimiento, que además esquiva
las dos trampas de arriba:

1. Nodo A **enchufado**, con la consola abierta. No dormirá (guardián del USB), y es el testigo.
2. Nodo B **desenchufado**, solo con batería, interruptor en ON. A los 30 s de la última tecla
   se apaga la pantalla y empieza a dormir.
3. En la consola de A, mirar durante un par de minutos: ¿sigue apareciendo B en `nodos: total`?
   ¿Llegan sus NODEINFO y su telemetría? Mandarle un mensaje desde A y ver si contesta.
   **Si B sigue vivo en la tabla de A, el light sleep no lo deja sordo.**
4. Luego enchufar B y abrir su consola: el reset la abre en limpio, pero `informeEnergia()`
   seguirá contando lo acumulado si NO se resetea — y como abrir el puerto lo resetea, los
   contadores del rato con batería se pierden. Para leerlos hay que tener el monitor **ya
   abierto** cuando se desenchufa... que es imposible. **Conclusión práctica**: la residencia
   del sueño se mide con un **medidor USB en línea**, no por consola. La consola sirve para lo
   otro: comprobar que sigue oyendo.
5. Y la prueba de verdad del ahorro son los mA con el medidor en línea, en cuatro escenarios
   (pantalla apagada con GPS y sin él, pantalla encendida, transmitiendo).

---

## ~~⚠ El light sleep deja el nodo SORDO: desactivado (29-jul)~~ — RESUELTO, ver arriba

Se implementó (`hal::dormirHasta()`, sustituyendo el `delay()` del tick) y **se desactivó al
medirlo**. Con él activo, el nodo deja de recibir y de transmitir en cuanto la pantalla se
duerme. Dos evidencias independientes:

- **COM11, sin consola conectada, se quedó mudo** a los 30 s de arrancar y no contestó al
  NODEINFO de COM10. `nodos: total 0` durante todo el minuto siguiente.
- **COM10 dejó de imprimir** en el instante exacto de su primer sueño (22:45:13.174).

Lo segundo por sí solo no prueba nada —el light sleep tumba el USB-Serial-JTAG y perder la
consola es lo esperado y estaba anticipado—. Lo que no se explica con eso es **el silencio de
radio del otro nodo**, que no tenía monitor.

**Hipótesis principal, sin confirmar**: `esp_sleep_enable_ext1_wakeup()` pasa el GPIO 4 (DIO1)
al dominio RTC y eso rompe la interrupción GPIO normal que RadioLib registró con
`setDio1Action()`. Sin esa ISR, `g_irq` nunca se levanta y la máquina de estados de la radio se
queda parada para siempre. Hay que resolver eso **antes** de volver a encenderlo.

En un aparato cuya razón de ser es recibir un SOS, esto no se embarca encendido. Se compila con
`-DFT_LIGHT_SLEEP` para retomarlo; el código y la investigación se quedan.

### ~~📌 PARA MAÑANA: cómo lo hacen Meshtastic y MeshCore~~ — HECHO, era `gpio_wakeup`

> Resuelto el 30-jul en una consulta a `meshtastic/firmware`, `src/sleep.cpp`. La hipótesis
> apuntada aquí era la correcta. Se deja el resto de la sección porque las dos preguntas
> secundarias (RX cíclico del SX1262 y standby del GPS) **siguen abiertas**.

Los dos corren sobre el **mismo SX1262 con RadioLib y el mismo ESP32**, así que se han topado
con esto sí o sí. Antes de seguir peleándose con `esp_sleep_*` a ciegas, mirar cómo lo
resuelven ellos. Es la vía más rápida y no cuesta nada.

**La pregunta concreta a responder**: cuando duermen el CPU, ¿cómo consiguen que la
interrupción de DIO1 siga despertándolos y siga llegando a RadioLib?

**Hipótesis a comprobar primero, y es muy probable que sea la respuesta:** que usen
`esp_sleep_enable_gpio_wakeup()` + `gpio_wakeup_enable()` en lugar de
`esp_sleep_enable_ext1_wakeup()`. La diferencia importa:

- **`ext1`** (lo que usé) mueve el pin al **dominio RTC**, y eso es lo que se sospecha que
  desconecta el pin de la matriz GPIO normal y mata la ISR que registró `setDio1Action()`.
- **`gpio_wakeup`** despierta desde light sleep **sin sacar el pin del dominio digital**, así
  que la interrupción normal sigue viva. Es exactamente la herramienta para este caso, y solo
  funciona en light sleep (no en deep sleep), que es justo lo que queremos.

Si es eso, el arreglo son tres líneas en `hal::sleepBegin()`.

Dónde mirar:

| Proyecto | Archivos |
|---|---|
| Meshtastic | `src/sleep.cpp` (`doLightSleep`), `src/PowerFSM.cpp`, `src/mesh/RadioLibInterface.cpp` |
| MeshCore | `MultiMote/meshcore-cardputer-adv` → `CardputerAdvBoard.h`, y el `MainBoard`/`onIdle` del núcleo |

Dos cosas más que conviene mirar de paso, porque son las que quedan en la lista de autonomía:

1. **Si duty-ciclan el RX del SX1262** (`startReceiveDutyCycleAuto`) y con qué parámetros. Es
   el punto que aquí se dejó para el final por riesgo de perder paquetes: ver si ellos lo dan
   por seguro y con qué preámbulo.
2. **Qué hacen con el GPS.** Si alguno consigue dormir un AT6668/ATGM336H, ahí estará el
   comando de standby que no aparece en las fuentes públicas del chip.

---

## ✅ Pantalla de posición propia: HECHA (30-jul)

Está en `ui::pantallaPosicion()`, como **`SCR_POSITION`, quinta pantalla de la rotación del
TAB**, justo detrás de la brújula. Se eligió la opción 1 de las dos que se especificaban abajo,
por el motivo que se daba: leer las coordenadas en voz alta por radio es un uso con prisa y hay
que llegar de un toque.

Decidido también lo de los satélites, y por la vía buena: se añadió un `TinyGPSCustom` sobre el
campo 3 de `$GPGSV`, así que hay **dos** números en `hal::Fix` — `satelites` (los usados en la
solución, trama GGA) y `satelites_vista` (los que se ven, tramas GSV). Se muestra "9 de 14"
cuando llegan los dos y solo el primero cuando el módulo no manda GSV; **nunca** se etiqueta como
"a la vista" un número que son los usados. Verificado que llegan: la consola saca ahora `0/1 sat`
en interior, o sea que el GSV se parsea de verdad.

**Sin verificar con los ojos**: la geometría (5 filas de 20 px entre la barra y el pie) y que la
pantalla sin fix diga lo que tiene que decir. Falta mirarla en el aparato.

Lo que sigue vale como especificación de lo que hay:

Una pantalla nueva con los datos del GPS: **latitud, longitud, altitud, satélites y precisión.**

No es solo diagnóstico: **leer tus coordenadas en voz alta por radio** es un uso real en el
campo, y hoy no hay forma de verlas en el aparato — solo salen por consola en `informeGps()`,
que requiere un ordenador enchufado. Eso también decide dónde va (ver abajo).

### Qué mostrar

Todo menos una cosa está ya en `hal::Fix`, así que no hay que tocar el parser:

| Fila | De dónde sale | Formato propuesto |
|---|---|---|
| Latitud | `f.lat_e7 * 1e-7` | `40.50996` — decimal, 5 decimales (~1 m). Es lo que se pega en Google Maps |
| Longitud | `f.lon_e7 * 1e-7` | `-3.69238` |
| Altitud | `f.altitud_m` | `713 m` |
| Satélites | `f.satelites` | `9` — **ojo, ver abajo** |
| Precisión | `f.hdop` | `1.6 bueno` — el número **y** la palabra |

**El HDOP en palabras**, porque el número solo no le dice nada a nadie:
`< 1` excelente · `1-2` bueno · `2-5` aceptable · `> 5` malo. Los valores medidos en este
aparato han ido de 0,6 (junto a la ventana) a 7,1 (dentro de casa).

**⚠ «Satélites visibles» no es lo que hay hoy.** `f.satelites` sale de la trama GGA vía
TinyGPSPlus, y son los satélites **usados en la solución**, no los a la vista. Los visibles
están en las tramas **GSV**, que TinyGPSPlus no parsea por defecto: hace falta un
`TinyGPSCustom` sobre el campo 3 de `$GPGSV`. Decidir una de dos:

- **Mostrar solo los usados** y etiquetarlo bien (`Sats usados`). Cero código nuevo.
- **Mostrar los dos** (`9 de 14`), que es más informativo y es lo que enseñan los GPS de
  verdad. Cuesta un `TinyGPSCustom` y hay que probar que el AT6668 emite GSV (debería, es
  NMEA 0183 4.1 estándar).

La segunda es mejor y no es cara. Pero **no etiquetar como «visibles» lo que son «usados»**:
eso es dar un número equivocado con una etiqueta correcta, que es peor que no darlo.

### Dónde va

Dos opciones, y la primera es la recomendada:

1. **Quinta pantalla en la rotación del TAB** (`SCR_COUNT` pasa a 5). A favor: se llega en un
   toque, que es lo que hace falta si estás leyendo coordenadas por radio con prisa. En contra:
   alarga el ciclo del TAB para todo el mundo, y con niños cada pantalla de más cuenta.
2. **Con ENTER desde la brújula.** `ENTER` en `SCR_COMPASS` **está libre hoy** (el manejador no
   tiene caso para esa pantalla), y es su hermana natural: la brújula dice dónde está *el otro*,
   ésta dónde estás *tú*. A favor: no alarga el TAB. En contra: hay que saber que está ahí.

### Detalles de dibujado que ya están resueltos y conviene reusar

- Caben **5 filas de `UI_ROW_H` (20 px)** entre la barra de estado y el pie: `filasQueCaben()`
  lo calcula. Justo las cinco filas de la tabla, sin scroll.
- **Font2** para los valores: son números que hay que leer en voz alta. Etiqueta a la izquierda
  en Font0 y valor a la derecha en Font2, como hace `pantallaInfo()`.
- Terminar con `pieAviso()`, que ya fuerza Font0 él mismo.
- Y `pintar()` deja Font0 antes de cada pantalla, así que no hay que acordarse de restaurarla.

### Lo que tiene que funcionar SIN fix

Es la mitad del valor de la pantalla: cuando no hay fix, tiene que decir **por qué**. Con
`hal::gpsBytesRecibidos() == 0` el UART está mal; con bytes pero sin fix, es cobertura. Esa
distinción ya existe en `informeGps()` y en el aviso de la barra de estado — reusar el criterio,
no inventar otro. Mostrar los bytes NMEA recibidos y los satélites, que sí llegan sin fix.

**Efecto colateral que conviene saber**: con ese firmware dentro, los dos aparatos dejaron de
responder al flasheo automático (`No serial data received`), porque el USB está apagado la mayor
parte del tiempo. **No es un ladrillo**: el bootloader del ESP32-S3 está en ROM. Se recupera con
interruptor **OFF** → mantener **G0** → dar corriente → soltar. Verificado: los dos volvieron.

---

## Qué está hecho

| Fase | Contenido | Estado |
|---|---|---|
| 0 | Estructura, `platformio.ini`, `config.h` | escrita |
| 1 | HAL: pantalla, teclado, batería, LED, altavoz, I²C, microSD | escrita |
| 2 | Radio SX1262, conmutador de antena, CAD, cola TX | escrita |
| 3 | Protocolo: cabecera 16 B, AES-CTR, `Data` protobuf | **11 tests ✓** |
| 4 | Router de inundación, tabla de duplicados, contención | **23 tests ✓** (con la 5) |
| 5 | Ciclo de trabajo, ventana rodante de 60 min | **incluido arriba** |
| 6 | Chat, historial navegable, barra de estado | escrita |
| 7 | Nodos, `NODEINFO`, telemetría | **9 tests ✓** (mensajes) |
| 8 | GPS, posición, brújula | escrita · **20 tests ✓** (geodesia) |
| 9 | Emergencia | escrita |
| 10 | Ajustes, asistente de primer arranque, PBKDF2 | escrita · tests incluidos arriba |
| 11 | Acabado | pendiente, después de verificar |

**80 tests en verde** en cuatro suites: `test_proto`, `test_mesh`, `test_util`, `test_msgs`.
Los 16 últimos son del 1-ago-2026: troceado de líneas del chat y vigilancia del enlace.

### Qué significa "verificado" aquí

Verificado de verdad, ejecutando código:

- El paquete que sale por antena es **byte a byte idéntico** al de Meshtastic, contrastado
  contra vectores generados con `pycryptodome` (`tools/gen_test_vectors.py`).
- El AES propio (128 y 256) produce el mismo texto cifrado que `pycryptodome`.
- SHA-256, HMAC y PBKDF2 propios coinciden con `hashlib` en diez casos.
- La lógica del router: difusión se entrega **y** se retransmite, duplicados se descartan,
  unicast ajeno solo se releva, el relevo no toca la carga cifrada.
- El ciclo de trabajo bloquea al 10 % y libera al salir de la ventana, con reloj inyectado.
- Haversine contra distancias reales (Madrid–Barcelona con < 600 m de error).
- Truncado de UTF-8 sin partir caracteres, con acentos y emoji.

Ejecutado en el aparato el 29-jul: pantalla, teclado, microSD, NVS, GPS (fix con 24-25
satélites y HDOP 0,6), radio, cifrado en el aire, router y ACK implícito en los dos sentidos.

Sigue sin ejecutar: la retransmisión con tres aparatos, el SOS de punta a punta y las pruebas
de exterior. (Contrastar contra un nodo Meshtastic oficial **no** está en esa lista: nunca fue
un requisito, solo una herramienta de depuración por si el protocolo diera problemas.)

---

## ~~Lo primero: desbloquear la radio~~ — RESUELTO

> **Histórico.** Esta sección describía el bloqueo por `I2C_ADDR_ANTSW = 0xFF` y el
> procedimiento para averiguar la dirección del expansor. **Ya no aplica**: la dirección es
> `0x43` (confirmada en la documentación de M5), el expansor **no responde** en ninguna
> configuración, y la restricción nº 2 se relajó a "avisar y continuar". La radio arranca y
> transmite, medido a −18 dBm. Se deja la constancia arriba, en la sección del expansor.
>
> Lo que **sigue vigente**: atornillar la antena antes de dar corriente, y la confirmación
> en pantalla antes del primer TX.

---

## Orden de pruebas

Cada punto tiene un criterio claro. No pasar al siguiente sin cumplirlo.

### 1. Arranque y HAL
Se ve "Fantashtic", el escaneo lista TCA8418 / BMI270 / ES8311 / PI4IOE, la batería marca un
porcentaje creíble y la microSD monta.
**Probar también sin tarjeta**: debe seguir funcionando y mostrar `SD?` en la barra.

### 2. Teclado
Escribir en el chat y ver las letras. Comprobar las flechas (son Fn + `;` `.` `,` `/`), el
borrado y que `` ` `` hace de ESC. Si el teclado no responde, revisar la versión de
M5Cardputer: por debajo de 1.1.0 no soporta esta placa.

### 3. Radio, un solo nodo
El conmutador de antena verifica por relectura y la radio informa de su frecuencia y preset.
Anotar si arranca con TCXO o cae a XTAL.

### 4. Radio, dos nodos
Mensajes de un aparato a otro. Anotar RSSI y SNR a 1 m: la referencia esperada es unos
−40 dBm y SNR entre +8 y +10. Si a corta distancia sale peor que −70 dBm, sospechar del
conmutador de antena.

### 5. Interoperabilidad con Meshtastic — OPCIONAL, solo para depurar
**No es un requisito del proyecto y nunca lo fue.** Este apartado decía "la prueba que
decide", y eso era un error de encuadre: Fantashtic es un grupo cerrado con su propia clave,
no un nodo de la red pública. La fidelidad de byte del formato de paquete es una decisión de
método —permite contrastar con vectores conocidos—, no un objetivo.

Queda aquí como **herramienta**, por si algún día el protocolo da un problema que no se pueda
aislar de otra forma: dejar un segundo Cardputer con firmware Meshtastic oficial (EU_868,
LongFast, canal por defecto) y pasar el nodo Fantashtic al modo interoperable. Si el nodo
oficial ve los mensajes, el protocolo está bien; si no, el orden de depuración está en
`docs/VECTORES.md` §"Orden de depuración recomendado".

A día de hoy el protocolo ya está validado por dos caminos independientes: vectores contra
`pycryptodome` en el ordenador, y dos aparatos entendiéndose en el aire.

### 6. Relevo (necesita el tercer aparato)
La única prueba que **no se puede hacer con dos**. Separar A y C hasta que no se oigan, poner
B en medio. Los mensajes de A deben llegar a C. En la consola de B verificar que retransmite,
que el contador de saltos baja de 3 a 2, y que **no hay bucles**.

### 7. GPS y brújula
En exterior. Contrastar la distancia mostrada contra Google Maps en dos puntos separados
≥ 500 m. Recordar que en parado no hay rumbo real: el BMI270 no es magnetómetro.

### 8. Emergencia
Mantener el botón **G0** (el del módulo StampS3A, el del modo descarga: **no hay botón en el
chasis**) 2 s desde cada pantalla. Comprobar que despierta al receptor con la
pantalla apagada, que suena la alarma, que "Voy hacia ti" funciona a un toque, y **que una
pulsación corta cancela un SOS propio ya lanzado**.

### 9. Frase del grupo
Cambiar la frase en los cuatro aparatos y comprobar que siguen hablando. Uno con la frase
antigua debe dejar de verlos.

### 10. Campo y autonomía
`docs/ALCANCE.md` tiene el protocolo de siete ensayos. El firmware de la Fase 2 registraba las
ráfagas en `/fantashtic/campo/`; si hace falta repetirlo, recuperar ese modo.

---

## Cosas que probablemente fallen primero

Predicciones, para no perder tiempo:

- **Compilación**: algún include o firma de M5GFX. El código está auditado contra las fuentes
  reales, pero nunca se ha compilado para ESP32.
- **Mapeo de teclas**: las flechas del Cardputer Adv podrían no ser Fn + `;.,/`. Se resuelve
  en `hal/keys.cpp`.
- **Posiciones en pantalla**: las coordenadas de la interfaz están calculadas, no vistas. Es
  de esperar algún solape en 240 × 135.
- **Dirección del PI4IOE**: si no es 0x43, sale del escaneo.
- **Sentido del UART del GPS**: la documentación de M5 se contradice consigo misma. Si no
  llegan datos NMEA, intercambiar `PIN_GPS_RX` y `PIN_GPS_TX`. El firmware distingue "GPS sin
  datos (UART?)" de "sin fix" justo para esto.

---

## Deuda pendiente (después de que funcione)

1. Quitar la clave provisional: el asistente ya deriva la clave real de la frase, pero
   conviene comprobar que ninguna ruta usa todavía una constante fija.
2. ~~Ajustes: las filas Nombre, Nombre corto, Avanzado e Información no hacen nada.~~
   **Hecho**: editor de texto para Nombre, Nombre corto y Frase del grupo (esta última con
   confirmación), pantalla de Información de solo lectura, y brillo y volumen en escala 1-5
   en vez de porcentaje y unidades crudas. "Avanzado" se quitó: no prometía nada concreto.
   Queda sin interfaz el restablecimiento de fábrica, y preset/potencia/saltos.
3. ~~`pantallaNodos()` dibuja 8 filas pero permite seleccionar 16. Falta scroll.~~
   **Hecho**: ventana deslizante compartida con ajustes, con contador en el pie.
4. **Autonomía.** Hechos el reloj adaptativo (80/240 MHz), el tick adaptativo (10/50 ms) y
   **el light sleep, que ya está escrito, activo y con la causa del nodo sordo resuelta**
   (ver la sección de arriba). Lo que falta es **medirlo**: que siga oyendo con dos nodos, y
   los mA con un medidor USB en línea. Después, por orden: el GPS (hoy `gpsEncender(false)`
   cierra el UART pero deja el ATGM336H alimentado y siguiendo satélites, así que el ajuste
   "GPS: apagado" no ahorra batería) y el altavoz (el ES8311 y el NS4150B están alimentados
   siempre porque nadie llama a `end()`; medir antes, si son 0,3 mA no merece la pena).
5. Actualizar `Memoria_tecnica_Fantashtic.docx` con los resultados de campo. Sigue diciendo
   "fases 0 y 1 completadas", y ya está todo escrito y probado en dos nodos.
6. **Subir las iteraciones de PBKDF2.** 20 000 son pocas para una frase corta. Ojo: cambiarlo
   invalida las claves ya guardadas en NVS, así que hay que repasar la frase en todos los
   nodos a la vez. Mismo problema que cambiar la sal.
7. ~~El disparador del SOS es el botón G0 y eso está mal.~~ **Resuelto**: la vía principal es
   un atajo de teclado y G0 se queda como secundario. El atajo es **ESC+DEL mantenido 2 s**
   (`hal::keysSosCombo()`), cambiado el 30-jul desde ENTER+ESC por el riesgo de la **suelta**:
   dos teclas no se levantan en el mismo milisegundo, y soltando ESC primero quedaba ENTER
   pulsado, o sea que **se enviaba la línea de chat** en plena emergencia. Con ESC+DEL lo peor
   que puede pasar es borrar un carácter. Siguen en esquinas opuestas del teclado.
   **Queda una punta**: `S_SOS_TECLAS` no se pinta en ninguna pantalla, así que el atajo no se
   puede descubrir desde el aparato. `pantallaInfo()` ya va con 9 filas de 12 px y no cabe una
   décima; hay que decidir dónde ponerlo.
   Sigue vigente el aviso sobre G0: mantenerlo **mientras se enciende** el aparato mete al
   ESP32-S3 en modo descarga con la pantalla muerta, y parece averiado.

---

## Mapa de la documentación

| Archivo | Contenido |
|---|---|
| `CLAUDE.md` | contexto permanente, pinout verificado, restricciones, trampas ya pisadas |
| `ESTADO.md` | este documento |
| `SPEC.md` | especificación técnica completa |
| `docs/ALCANCE.md` | presupuesto de enlace y plan de pruebas de campo |
| `docs/VECTORES.md` | vectores del protocolo y orden de depuración |
| `tools/gen_test_vectors.py` | generador de los vectores |
| `tools/gen_memoria.py` | generador de la memoria técnica |
