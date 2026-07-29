# Estado del proyecto y plan de la primera sesión con hardware

Documento de traspaso. `CLAUDE.md` tiene el contexto permanente; esto es lo que cambia.

**Última actualización**: 29 de julio de 2026, **después de la primera sesión con hardware**.

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

Flasheado en los dos nodos, compilando limpio y con los 63 tests en verde.
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

**63 tests en verde** en cuatro suites: `test_proto`, `test_mesh`, `test_util`, `test_msgs`.

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
4. **Autonomía.** Hechos ya el reloj adaptativo (80/240 MHz) y el tick adaptativo (10/50 ms).
   **Lo siguiente y lo más importante es el light sleep**, y ya está investigado: ver el
   bloque "⭐ Autonomía" de `CLAUDE.md`. Resumen: `CONFIG_PM_ENABLE` no está activado en el
   Arduino-ESP32 6.12.0, así que el light sleep automático no existe, pero
   `esp_light_sleep_start()` sí, y encaja con el tick explícito de este bucle. Luego el GPS
   (hoy no se apaga de verdad) y el altavoz. Y antes de nada, **medir con un medidor USB**.
5. Actualizar `Memoria_tecnica_Fantashtic.docx` con los resultados de campo. Sigue diciendo
   "fases 0 y 1 completadas", y ya está todo escrito y probado en dos nodos.
6. **Subir las iteraciones de PBKDF2.** 20 000 son pocas para una frase corta. Ojo: cambiarlo
   invalida las claves ya guardadas en NVS, así que hay que repasar la frase en todos los
   nodos a la vez. Mismo problema que cambiar la sal.
7. **El disparador del SOS es el botón G0 y eso está mal.** No hay botón dedicado en el
   chasis: `BtnA` es GPIO 0, el botón del módulo StampS3A, el mismo del modo descarga.
   Dos problemas: es pequeño y pensado para el arranque, no para una emergencia; y
   mantenerlo pulsado **mientras se enciende** el aparato mete al ESP32-S3 en modo descarga
   con la pantalla apagada, o sea que parece averiado. Con niños, eso es la ruta fácil hacia
   un estado roto que prohíbe la restricción nº 8. Hace falta **además** un atajo de teclado
   para el SOS. El botón puede quedarse como secundario.

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
