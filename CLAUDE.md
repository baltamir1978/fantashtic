# Fantashtic — contexto del repositorio

Firmware standalone para **M5Stack Cardputer Adv + Cap LoRa-1262**, compatible a nivel de aire
con Meshtastic pero con interfaz propia, pensado para un grupo cerrado de 3-4 nodos familiares.
Parte de los usuarios son niños, y eso gobierna casi todas las decisiones de interfaz.

---

## ⚠ ESTADO ACTUAL — LEER PRIMERO

**El firmware funciona en el aparato.** Dos sesiones con hardware (29 y 30 de julio de 2026) y
**dos nodos hablándose por radio**: `130C92AC` 'COVA' (COM10) y `130C9A3C` 'PONS' (COM11).

Verificado ejecutando código, no leyéndolo:

- **95 tests en el ordenador** (`pio test -e native`): protocolo, router, ciclo de trabajo,
  criptografía, geodesia, UTF-8, troceado de líneas, vigilancia del enlace, tabla de nodos, los
  mensajes `Position` / `User` / `Telemetry` / `Routing`, los reintentos de los mensajes
  privados y el formato del historial en la microSD.
- **En el aparato**: arranque, pantalla, teclado, microSD, NVS, GPS con fix, y el **enlace de
  radio entre dos nodos a −12/−18 dBm** — o sea cripto, cabecera, router y ACK implícito en el
  aire real y en los dos sentidos.

Lo que **sigue sin verificar**, y hay que decirlo sin adornos:

- **El light sleep NO funciona y está desactivado.** Se reescribió el 30-jul con `gpio_wakeup`,
  se encontró y arregló una causa real, y **el nodo desenchufado se sigue quedando tonto a los
  30 s**. Necesario pero no suficiente. Ver `ESTADO.md`: lo descartado y por dónde seguir.
- **Los mA.** No hay ni una medida de consumo. Todo lo de autonomía es de hoja de datos.
- **La geometría de las cinco pantallas.** Calculada sobre 240×135, no vista.
- **Los mensajes privados y la recuperación del historial (24-sep)**: ni compilados para el
  ESP32 ni probados en el aparato. Ver `ESTADO.md`.
- El relevo con tres aparatos, el SOS de punta a punta y las pruebas de exterior.

**El conmutador de antena del Cap NO responde** (medido en dos unidades, en los dos buses, a
las dos velocidades, con dos drivers, con el Cap puesto y sin él). No es un fallo del firmware
ni un Cap defectuoso: sus líneas I²C no llegan al host. `radioBegin()` avisa y continúa, y el
enlace medido a −18 dBm demuestra que el TX/RX lo gobierna DIO2 del SX1262. Ver la restricción
nº 2 y `ESTADO.md`.

Ver `ESTADO.md` para el detalle y el orden de las pruebas que quedan.

---

## Qué es y qué NO es este proyecto

**ES**: un firmware ligero escrito desde cero (PlatformIO + Arduino-ESP32 + RadioLib +
M5Unified) que reimplementa el **protocolo de radio de Meshtastic** (cabecera de 16 bytes,
AES-CTR, encaminamiento por inundación) con fidelidad de byte.

**NO ES** un fork de `meshtastic/firmware`. No se arrastra su código, ni su sistema de
módulos, ni MQTT, ni Bluetooth, ni la app móvil, ni el API serie.

**NO ES** un nodo de la red pública Meshtastic. Se usa un canal privado con clave propia
derivada de una frase.

**La interoperabilidad con Meshtastic NUNCA fue un requisito.** La fidelidad de byte es una
decisión de **método**: permite contrastar la implementación contra vectores conocidos y, si
hiciera falta, depurar con un nodo de firmware oficial en la mesa. No se promete
interoperabilidad y no es un objetivo pendiente. No presentarla como tal en la documentación.

---

## Restricciones duras (no negociables)

1. **Nunca transmitir sin antena conectada.** Daña el SX1262 de forma permanente. El firmware
   exige confirmación en pantalla antes del primer TX (`pantallaAntena()` en `main.cpp`).
2. **El conmutador de antena RF es OPCIONAL** (revisado el 29-jul-2026; antes era
   bloqueante). PI4IOE5V6408 en el bus I²C interno, **0x43**, pin **P0 = HIGH**.
   **Medido en el aparato: no responde.** Ni en 0x43 ni en 0x44, ni en el bus interno ni
   en el externo, ni a 100 ni a 400 kHz, con dos drivers distintos (`M5.In_I2C` y el
   `Wire1` de Arduino), y el escaneo es **idéntico con el Cap montado y sin él**.
   `radioBegin()` avisa y continúa, porque Meshtastic y MeshCore hacen lo mismo, porque
   el Cap LoRa868 directamente no lleva expansor, y porque este aparato ya transmitió con
   MeshCore sin que nadie pusiera P0 en alto. El TX/RX real lo hace **DIO2 del SX1262**.
   Lo que **sigue siendo innegociable**: la confirmación de antena en pantalla antes del
   primer TX y el límite de ciclo de trabajo. Ver `ESTADO.md` para la investigación.
   Si algún día responde, el orden de escritura de abajo vuelve a importar.
   **No basta con escribir el registro de salida**: al arrancar, P0 está como entrada Y en
   alta impedancia. Orden obligatorio — y este orden importa:
   ```
   reg 0x03 |= 0x01     // 1. P0 como salida
   reg 0x05 |= 0x01     // 2. VALOR a HIGH
   reg 0x07 &= ~0x01    // 3. liberar la alta impedancia
   ```
   Liberar la alta impedancia antes de escribir el valor deja la línea del conmutador en BAJO
   durante toda una transacción I²C. Releer los tres y verificar (`hal::antennaBegin()`).
3. **Ciclo de trabajo**: subbanda ETSI g3 (869,4–869,65 MHz), 500 mW p.r.e. y **10 % por
   hora**. El contador rodante es obligatorio y **no es desactivable desde la interfaz**.
4. **La clave nunca se commitea.** `include/secrets.h` está en `.gitignore`. La clave real se
   deriva de la frase con PBKDF2 y vive en NVS.
5. **Sin PSRAM.** Buffers estáticos, cero asignaciones dinámicas después de `setup()`.
6. **El bus SPI de la LoRa está compartido con la microSD** (SCK 40 / MOSI 14 / MISO 39;
   CS LoRa 5, CS SD 12) y **NO es el de la pantalla**. El ST7789 usa SPI2_HOST y M5GFX lo
   reclama en `M5.begin()`; llamar a `SPI.begin(40,39,14)` sobre el objeto global lo
   remultiplexaría y rompería el display. LoRa y microSD van en `SPIClass(HSPI)` = SPI3_HOST.
   Con un solo hilo la arbitración es un contador reentrante, **nada de mutex de FreeRTOS**.
   **La ISR de DIO1 solo levanta una bandera; nunca toca el SPI.**
7. **Nunca usar el objeto `Wire` de Arduino.** M5Unified instala el bus interno (SDA 8 /
   SCL 9) en **I2C_NUM_1**; `Wire` es I2C_NUM_0. `Wire.begin(8, 9, …)` remultiplexa la salida
   de esos GPIO hacia I2C0, y un GPIO tiene una sola fuente de salida: a partir de ahí se
   quedan mudos el **teclado (TCA8418)**, el IMU y el códec. Fallo silencioso y
   desconcertante: el teclado deja de responder justo después de escanear el bus.
   Usar `M5.In_I2C` (`scanID`, `readRegister`, `writeRegister8`).
8. **Usuarios menores de edad.** Sin rutas fáciles hacia estados rotos. El SOS debe poder
   cancelarse siempre.

---

## Hardware — valores verificados

Contra `docs.m5stack.com`, `variants/esp32s3/m5stack_cardputer_adv/variant.h` de
`meshtastic/firmware` y las tablas de M5Unified.
**No inventar pines. Si algo no está aquí, preguntar.**

### Cardputer Adv (SKU K132-Adv)

| Componente | Detalle |
|---|---|
| SoC | ESP32-S3FN8 (Stamp-S3A), 240 MHz, **8 MB flash, sin PSRAM** |
| Pantalla | ST7789V2, 240 × 135, **SPI2_HOST** — CS 37, DC 34, MOSI 35, SCK 36, RST 33, luz 38 |
| Teclado | 56 teclas, matriz 4×14, escáner I²C **TCA8418 en 0x34** (medido), INT en GPIO **11** |
| IMU | BMI270, **responde en 0x69** (medido, no 0x68) — acelerómetro, **no** magnetómetro: no hay rumbo en parado |
| Audio | ES8311 **en 0x18** (medido) + NS4150B, 1 W, jack 3.5 mm |
| Batería | Li-ion 1750 mAh, ADC en GPIO **10**, divisor 100k+100k → **multiplicador 2.0** (el `2*1.02` de `variant.h` es un fudge para mostrar "cargando"; lee ~2 % alto) |
| LED RGB | WS2812, GPIO **21** (confirmado en las tablas de M5Unified). **Comparte raíl con la retroiluminación (GPIO 38): con la pantalla apagada NO puede encenderse** |
| I²C interno | SDA **8**, SCL **9** (I2C_NUM_1) |
| Grove | SDA **2**, SCL **1** |
| microSD | CS **12**, MOSI 14, CLK 40, MISO 39 |
| Botón | GPIO **0** |

Modo descarga: interruptor OFF → mantener GPIO 0 → alimentar → soltar.

### Cap LoRa-1262 (SKU U214)

| Señal | GPIO |
|---|---|
| LoRa NSS / CS | **5** |
| LoRa SCK / MOSI / MISO | **40 / 14 / 39** |
| LoRa RESET | **3** |
| LoRa DIO1 (IRQ) | **4** |
| LoRa BUSY | **6** — `variant.h` lo llama `LORA_DIO2`. Es BUSY. No "corregirlo" |
| GPS → ESP (RX del ESP) | **15** — CONFIRMADO en hardware |
| ESP → GPS (TX del ESP) | **13** — CONFIRMADO en hardware |

**El sentido del UART del GPS ya NO es una duda** (medido el 29-jul-2026): con
`rx=15, tx=13` llegan los NMEA y se consigue fix. La página del Cap y `variant.h`
tenían razón; la tabla del bus EXT de M5, que los da al revés, está mal.

- **SX1262**, +22 dBm conducidos, −147 dBm, RP-SMA, antena de 3 dBi incluida.
- `setDio2AsRfSwitch(true)`; `SX1262::begin()` ya lo hace.
- **Dirección del expansor CONFIRMADA en la documentación de M5: `0x43`, en el bus
  interno (`m5::In_I2C`), a 400 kHz.** El ejemplo oficial es literalmente
  `m5::PI4IOE5V6408_Class ioe(0x43, 400000, &m5::In_I2C);` y los registros
  0x03 / 0x05 / 0x07 coinciden con `PI4IOE5V6408_Class` de M5Unified, que además
  valida la presencia leyendo el **registro de ID 0x01** (si vale 0, lo da por
  ausente). Ojo: M5Unified NO instancia el expansor para `board_M5CardputerADV`
  — vive en el Cap, no en la placa base, así que hay que crearlo a mano.
- **DISCREPANCIA PENDIENTE sobre el TCXO.** Aquí se dice 1,8 V (y eso es lo que
  usa `LORA_TCXO_V`), pero el ejemplo oficial de M5 para este Cap llama a
  `radio.begin(..., 3.0, true)`: **3,0 V y `useRegulatorLDO = true`**. Meshtastic,
  en cambio, define `SX126X_DIO3_TCXO_VOLTAGE 1.8` con `TCXO_OPTIONAL`. Si
  `radioBegin()` falla con −706 / −707, probar 3,0 V y el LDO antes de dar por
  buena la caída a XTAL.
- TCXO en DIO3 a 1,8 V. Si falta, `begin()` devuelve **−706 (`SPI_CMD_INVALID`) o −707
  (`SPI_CMD_FAILED`)** y hay que reintentar con `XTAL = true` y `tcxoVoltage = 0`.
  **NO es −705** (`SPI_CMD_TIMEOUT`): eso significa que BUSY no baja, o sea un fallo de
  cableado, y enmascararlo con un reintento sería un error.
- GPS **ATGM336H-6N** (AT6668), UART 115200 8N1, NMEA 0183 4.1, antena cerámica.

---

## Stack de software

```ini
[env:cardputer-adv]
platform   = espressif32@6.12.0        ; línea Arduino core 2.0.x
board      = m5stack-stamps3
board_build.partitions = default_8MB.csv
build_flags = -DARDUINO_USB_CDC_ON_BOOT=1 -DARDUINO_USB_MODE=1
lib_deps =
  jgromes/RadioLib@7.3.0
  m5stack/M5Unified@^0.2.8
  m5stack/M5Cardputer@^1.1.1           ; >= 1.1.0 OBLIGATORIO
  mikalhart/TinyGPSPlus@^1.1.0
  adafruit/Adafruit NeoPixel@^1.12.0
```

Cosas que costaron encontrar:

- **`m5stack/TinyGPSPlus` no existe** en el registro de PlatformIO. Es `mikalhart/`.
- **M5Cardputer ≥ 1.1.0 es obligatorio**: el soporte del Cardputer Adv (`board_M5CardputerADV`
  y el lector TCA8418) no existe antes. Con 1.0.x compila pero el teclado está muerto, porque
  la librería intenta leer una matriz por GPIO usando los pines de la LoRa.
- **`M5Cardputer.config()` no existe** — es `M5.config()`.
- **No usamos nanopb ni mbedTLS.** AES, SHA-256, HMAC y PBKDF2 están escritos a mano en
  `src/proto/`, y los protobufs se serializan a mano. Motivo: mbedTLS existe en el ESP32 pero
  no en el ordenador, así que los tests probarían una implementación distinta de la que se
  embarca. Escritos a mano, el mismo código corre en los dos sitios y los vectores lo validan
  contra `hashlib` y `pycryptodome`.
- **`timegm()` no existe** en el newlib de xtensa. Solo hay `mktime`, que aplica la zona
  horaria local, y el GPS da UTC. Ver `epochUtc()` en `hal/gps.cpp`.
- `M5Cardputer` arrastra `IRremote` como dependencia dura. Es esperado.

## Compilar

### En macOS (verificado el 1-ago-2026)

Es la ruta limpia: **nada de lo de Windows aplica** —ni MAX_PATH, ni la unión, ni
PowerShell, ni instalar un `g++` aparte, porque el de Xcode vale para el entorno
`native`—. PlatformIO entra con `brew install platformio` y queda en el PATH:

```bash
pio run                                  # compilar  (SUCCESS, 17 s en caliente)
pio test -e native                       # 95 casos, 95 en verde
pio run -t upload --upload-port /dev/cu.usbmodem*
pio device monitor -b 115200
pio device list                          # el Cardputer sale como /dev/cu.usbmodem*
```

La primera compilación tarda ~6 min porque se baja el toolchain xtensa.
**Sin verificar en macOS**: flashear y el monitor serie, porque no ha habido
ningún aparato enchufado a este ordenador todavía.

### En Windows

**Siempre desde PowerShell y desde la unión, nunca desde la ruta real.** La ruta
del proyecto mide 205 caracteres y el toolchain xtensa se rompe al pasar de 260
(ver la tabla de trampas). La unión se crea una vez, sin permisos de
administrador:

```powershell
cmd /c mklink /J C:\Users\baltamirano\ft <ruta-real-del-proyecto>
```

`pio` tampoco está en el PATH de esta máquina:

```powershell
Set-Location C:\Users\baltamirano\ft
$pio = "$env:USERPROFILE\.platformio\penv\Scripts\pio.exe"

& $pio run                                    # compilar
& $pio run -t upload --upload-port COM10      # flashear
& $pio run -t upload -t monitor --upload-port COM10 --monitor-port COM10
& $pio device monitor -b 115200 --port COM10  # consola
```

El Cardputer Adv aparece como **COM10**, VID:PID `303A:1001` (USB-Serial-JTAG).
La consola solo imprime el diagnóstico al arrancar, así que para verlo hay que
encadenar `-t upload -t monitor`: abrir el monitor a posteriori llega tarde.

Tests en el ordenador (**95 casos, 95 en verde**). Necesitan un `g++` de
verdad; en esta máquina solo había MSVC, que rechaza los `build_flags` estilo
GCC del entorno `native`:

```powershell
winget install BrechtSanders.WinLibs.POSIX.UCRT --scope user   # una vez
$env:PATH = "$env:LOCALAPPDATA\Microsoft\WinGet\Packages\BrechtSanders.WinLibs.POSIX.UCRT_Microsoft.Winget.Source_8wekyb3d8bbwe\mingw64\bin;$env:PATH"
& $pio test -e native
```

---

## Arquitectura

```
src/
  main.cpp     arranque, armado de antena, bucle, botón de SOS
  hal/         antenna i2c_bus keys led power sound storage gps
  radio/       SX1262 con RadioLib, cola TX, CAD, ciclo de trabajo
  proto/       aes crypto kdf packet messages   ← sin Arduino, se prueba en host
  mesh/        history router duty nodedb        ← sin Arduino, se prueba en host
  util/        geo utf8                          ← sin Arduino, se prueba en host
  app/         orquestación, SOS, envíos periódicos
  ui/          cinco pantallas, SOS, asistente
  store/       NVS (config y clave) + microSD (historial)
```

Reglas de dependencia:

- `ui/` **nunca** toca `radio/` directamente; pasa por `app/`.
- `proto/`, `mesh/` y `util/` **no dependen de Arduino** y compilan en el ordenador. Es lo que
  permite que existan los 95 tests. Al añadir código a esas capas, mantener la propiedad y
  actualizar `build_src_filter` del entorno `native`.
- Todo el estado temporal se pasa por parámetro (reloj, aleatoriedad), no se lee de `millis()`
  dentro de la lógica: por eso el router y el ciclo de trabajo se pueden probar de forma
  determinista.

## Estilo

- C++17. Identificadores y comentarios **en inglés** en el código nuevo; los que ya existen
  están en español y no hace falta migrarlos. Textos de UI en `include/strings.h`.
- Sin `String` de Arduino fuera de la UI. `char[]` fijo y buffers dimensionados en
  `include/config.h`.
- El código de radio no bloquea. Nada de `delay()` fuera de `setup()`.
- **No usar `radio.scanChannel()`**: bloquea ~21 ms a SF11 y saca la radio de recepción en
  silencio. Usar `startChannelScan()` + DIO1 + `getChannelScanResult()`.
- Toda constante del protocolo lleva un comentario citando el archivo de
  `meshtastic/firmware` del que sale.

---

## Trampas ya pisadas (no volver a caer)

| Síntoma | Causa |
|---|---|
| `fatal error: <cabecera>: No such file or directory` con la librería instalada y el `-I` correcto | **MAX_PATH**. La ruta del proyecto mide 205 caracteres y Windows corta en 260; el toolchain xtensa (GCC 8.4) no es long-path aware. La pista: `M5Unified.h` (258) se encuentra y `M5Unified.hpp` (260) no, **estando en la misma carpeta**. Compilar por la unión `C:\Users\baltamirano\ft` (ver §"Compilar") |
| El mismo error MAX_PATH aunque se compile por la unión | Git Bash resuelve la unión al hacer `cd` y vuelve a la ruta larga. Usar **PowerShell** (`Set-Location`) para todo lo que sea `pio` |
| `KeysState has no member named 'up'` / `'backspace'` | En M5Cardputer 1.1.1 `KeysState` **no** trae `up/down/left/right/esc` ni `backspace`. Las flechas se sintetizan a mano y la tecla de borrar es `st.del`. Y **Fn NO corta el relleno de `st.word`**: hay que resolver la capa Fn antes de emitir CARACTER, o Fn+`;` mueve el cursor Y escribe `;` |
| `'snprintf' was not declared` en los tests | libstdc++ 16 ya no arrastra `<stdio.h>` de forma transitiva. Es fallo del test, no del firmware |
| El teclado deja de responder tras escanear el I²C | `Wire.begin(8,9)` roba el bus a M5UnifIed |
| Cuelgue en el primer dibujo | `auto& D = M5Cardputer.Display` a nivel de fichero: orden de inicialización entre unidades de traducción |
| No se retransmite nada | Filtrar por "¿es para mí?" en el router: en un broadcast es cierto |
| Los nodos parecen conectados cuando ya no lo están | Los periódicos más rápidos eran de 10-15 min, así que la barra contaba «activos» con una ventana de 15 min y en ciudad se llevaba un cuarto de hora incomunicado sin enterarse. Resuelto el 1-ago-2026 con un **latido cada minuto** (`HEARTBEAT_INTERVAL_MS`) y cuatro estados en `mesh::estadoEnlace()`. El latido va con **`hop_limit = 0` a propósito**: mide el enlace DIRECTO y evita que los otros tres nodos multipliquen por cuatro el gasto de aire. **De esto depende que el router entregue las difusiones con `hop_limit = 0`** — está fijado en `test_sin_saltos_no_se_retransmite`; "optimizarlo" a DESCARTAR rompería la vigilancia sin romper nada más |
| El estado de entrega va al mensaje anterior | `recordarIdPaquete()` marca el **último** del buffer: hay que llamarlo DESPUÉS de `anadirMensaje()` |
| Un mensaje largo se ve a cuartos: ocupa una sola fila y el resto desaparece | `pantallaChat()` pintaba **un mensaje = una fila** y hacía `truncarSeguro(txt, 22)`. Los 22 eran además una cuenta sobre "unos 8 px por carácter", y **Font2 es proporcional**: con mayúsculas se salía de la pantalla y con `iiii` dejaba media fila vacía. Arreglado el 1-ago-2026 con `util::envolver()` (mide con `D.textWidth()`, corta por palabras, parte por carácter sin romper UTF-8) y una composición por LÍNEAS en `componerChat()`. **El scroll del chat cuenta ahora líneas, no mensajes**, y su tope sale de la propia composición (`chatHayMasArriba()`): restar mensajes se quedaba corto en cuanto uno ocupaba dos filas |
| La pantalla de nodos dice "no se ve a nadie" hasta que alguien manda un mensaje, y entonces sale el número y no el nombre | El descubrimiento estaba **al revés**: al oír a un desconocido se anunciaba el nombre PROPIO, cuando lo que hace falta es pedir el del otro. Si el otro lado ya te conocía, su regla no se disparaba y se quedaba callado. Pasaba siempre que un nodo se reiniciaba con el otro encendido (la tabla de nodos vive en RAM). Arreglado con `Data.want_response` (campo 3 de `mesh.proto`): el NODEINFO del arranque y el que dispara oír a un desconocido lo piden, y quien lo recibe contesta con el suyo **sin** pedirla, o serían dos nodos contestándose para siempre |
| El nodo se queda **sordo** desde el primer light sleep, y la red de seguridad no salta | `esp_light_sleep_start()` llama a `ext1_wakeup_prepare()`, que hace **`rtc_gpio_init()`** sobre cada pin de la máscara de `esp_sleep_enable_ext1_wakeup()`. Eso saca el pin de la matriz GPIO digital y lo pasa al mux del dominio RTC, **y nadie lo devuelve al despertar**. En el GPIO 4 mueren por tanto LAS DOS vías de aviso: la ISR de `setDio1Action()` y el rearme por nivel `if (digitalRead(PIN_LORA_DIO1))` de `radioLoop()`, que estaba puesto justo para el caso de perder un flanco. Por eso no se recuperaba nunca y por eso la red no saltó: las mata la misma causa. Se arregla con `gpio_wakeup_enable()` + `esp_sleep_enable_gpio_wakeup()`, que no tocan el mux — es lo que hace Meshtastic en `src/sleep.cpp` (`enableLoraInterrupt()`). Y admiten un nivel **por pin**, así que ahí sí cabe el botón, que es activo bajo |
| Las tramas NMEA salen con el checksum roto en cuanto se activa el light sleep | Durmiendo, el APB está parado y **el UART no recoge nada**: lo que llegue se pierde. El módulo suelta sus tramas en ráfagas, así que dormir a ciegas se come casi todas y el aparato deja de tener posición justo cuando nadie mira — que es cuando se emiten los avisos de posición automáticos. Se predice la ráfaga (`hal::gpsMsHastaProximaTrama()`). Despierto no se pierde nada: la ISR del UART sigue llenando el buffer de anillo aunque el bucle esté en un `delay()` |
| `$PCAS02,5000` no baja el caudal NMEA a ~40 B/s | Medido el 30-jul-2026: baja de ~630 a ~400 B/s y **la ráfaga sigue llegando cada ~1000 ms**. El AT6668 no hace caso del todo (sigue soltando GSV mientras busca, por lo menos). Consecuencia: la previsión de la ráfaga hay que hacerla con el intervalo **medido**, no con el pedido; fiarse del pedido dormiría encima de cuatro ráfagas de cada cinco |
| Abrir el puerto serie **resetea el nodo** | En el USB-Serial-JTAG, DTR/RTS gobiernan el reset y el modo de arranque, así que un `SerialPort.Open()` corriente provoca `rst:0x15 (USB_UART_CHIP_RESET)`. Para observar un nodo sin perturbarlo, **no abrir su puerto**: se mira desde la consola del otro |
| Con el light sleep activo, esptool deja de alcanzar el nodo (`No serial data received`) | Windows **suspende** el puerto cuando ningún programa lo tiene abierto → paran los SOF del USB → `HWCDC::isPlugged()` pasa a false → el nodo se duerme → y dormido ya no hay USB al que resetear. El guardián del USB sirve para no perder una consola **ya abierta**, no como red de seguridad para flashear. No es un ladrillo: el bootloader está en ROM, se recupera con interruptor **OFF** → mantener **G0** → dar corriente → soltar |
| Un mensaje privado a un vecino se queda en `.` para siempre | El ACK implícito solo existe si alguien RELEVA el paquete, y un unicast para nosotros se entrega **sin propagarse**. Los privados llevan `want_ack` y se confirman con un `ROUTING_APP` ACK (`Data.request_id`, campo 6, **fixed32**). Y tres detalles que no se ven con dos nodos: el relevo tiene que **volver a relevar el reintento** que llega directo del emisor (`hop_limit == hop_start`) o ningún reintento cruza un relevo; la copia de un relevo **no se vuelve a acusar** (`DM_REACK_MIN_MS`); y un reintento **que no se acusó la primera vez** (no descifró) no se acusa nunca. Todo en `mesh/pending.h` y `router.cpp`, con test |
| El historial recuperado sale desordenado | Era un fichero por mes con la fecha del GPS, y sin fix todo iba a `sin-fecha.log`. Ahora es **un solo `chat.log`** y se lee su cola desde el final |
| El SOS se corta tras un envío | Cualquier mensaje entrante lo cancelaba; ahora exige completar la ráfaga |
| Al soltar el combo del SOS se enviaba la línea de chat | El combo era ENTER+ESC, y dos teclas no se levantan nunca en el mismo milisegundo: soltando ESC primero quedaba ENTER pulsado y **se enviaba lo que hubiera escrito**, en plena emergencia. Cambiado a **ESC+DEL** el 30-jul-2026: lo peor que puede pasar al soltar es borrar un carácter. Siguen en esquinas opuestas del teclado, que es lo que hace el gesto difícil por accidente |
| Brillo o volumen se desploman al segundo toque | Desbordamiento de `uint8_t`: 90+20 = 110 → `110*255/100` = 280 → 24 |
| Coordenadas absurdas | `latitude_i` es **sfixed32**, no varint |
| Paquete que no descifra sin motivo | `dest` / `request_id` / `reply_id` / `emoji` son **fixed32**, no varint |

---

## Deuda conocida

- El cargador de batería no está documentado por M5Stack. No hay PMIC I²C: solo lectura de
  tensión por ADC.
- No hay ruta de interfaz hacia `store::restablecer()`: el restablecimiento de fábrica existe
  en la API pero no se puede lanzar desde el aparato. Con niños, añadirlo hay que pensarlo.
- Ajustes: "Avanzado" se quitó (no hacía nada) y en su sitio está "Frase del grupo". Sigue sin
  interfaz lo de preset, potencia y saltos máximos, que en `strings.h` tienen texto pero no
  pantalla. Cambiarlos afecta al enlace, así que van detrás de confirmación cuando se hagan.
- ~~Falta una pantalla de posición propia.~~ **Hecha el 30-jul-2026**: `SCR_POSITION`, quinta
  pantalla de la rotación del TAB, justo detrás de la brújula porque son la misma pregunta al
  revés (dónde está el otro / dónde estoy yo). Cinco filas: latitud y longitud con 5 decimales
  (~1 m, el formato que traga Google Maps), altitud, satélites y precisión. Sin fix dice **por
  qué** no lo hay, con el mismo criterio que `informeGps()` y la barra de estado: cero bytes
  NMEA = UART; con bytes y sin fix = cobertura. Detalle que había que respetar y se respetó:
  `Fix::satelites` son los **usados en la solución** (trama GGA) y `Fix::satelites_vista` los
  que se ven (tramas GSV, que TinyGPSPlus no parsea sin un `TinyGPSCustom`); se muestra
  "9 de 14" cuando llegan los dos y solo el primero cuando no, pero **nunca** se etiqueta como
  "a la vista" un número que son los usados.
- **El SOS por teclado es ESC+DEL** (antes ENTER+ESC, ver la tabla de trampas). Pero
  `S_SOS_TECLAS` **no se pinta en ninguna pantalla**: el atajo no se puede descubrir desde el
  aparato, y en un cacharro sin manual eso importa. `pantallaInfo()` ya va con 9 filas de 12 px
  y no cabe una décima, así que hace falta decidir dónde ponerlo.
- **La geometría de las CINCO pantallas en Font2 está calculada, no vista.** El ancho se
  mide en tiempo de ejecución con `D.textWidth()` donde puede pisarse algo (el valor de cada
  fila de ajustes, la columna de detalle de nodos), pero las posiciones verticales son
  aritmética sobre 240×135. Confirmar en el aparato antes de darlas por buenas.
### ⭐ Autonomía: el light sleep ya está escrito; falta MEDIRLO

Hecho ya (sin riesgo, no depende de ninguna medida):

- **Reloj adaptativo**: 240 MHz solo con la pantalla encendida o con un SOS en marcha, 80 MHz
  el resto del tiempo (`hal::cpuRapido()`). **80 es el suelo a propósito**: con el CPU a
  80/160/240 el bus APB se queda en 80 MHz, así que los divisores de SPI (4 MHz LoRa, 20 MHz
  SD) y los baudios del UART del GPS no cambian. Por debajo de 80, el APB sigue al CPU y hay
  que recalcularlos todos.
- **Tick adaptativo**: 10 ms cuando hace falta reaccionar, `TICK_IDLE_MS` (50) cuando solo se
  escucha. El cambio de reloj se hace **solo con `radio::radioOciosa()`**: `setCpuFrequencyMhz()`
  reprograma el PLL y detiene el CPU un instante, y eso en medio de una transacción SPI con el
  SX1262 no es buena idea.
- `informeEnergia()` lo saca por consola cada 5 s, porque si no es un cambio invisible.

**⛔ Light sleep: DESACTIVADO. Medido el 30-jul-2026 y NO vale.** Con `gpio_wakeup` en vez de
`ext1`, el nodo desenchufado **sigue quedándose tonto a los 30 s** y además **el altavoz
crepita** al entrar y salir del sueño. El arreglo de abajo era **necesario pero no suficiente**:
queda al menos otra causa sin encontrar. Detrás de `-DFT_LIGHT_SLEEP`; en un aparato cuya razón
de ser es recibir un SOS no se embarca algo que se queda sordo al quedarse solo. Lo ya descartado
y las pistas nuevas (empezar por el códec de audio) están en `ESTADO.md`.

Lo que sigue se queda escrito porque la investigación vale, no porque esté resuelto. La causa del
nodo sordo está localizada y era más profunda que la hipótesis; el detalle completo está en la
tabla de trampas, en la fila "el nodo se queda sordo desde el primer light sleep". Resumen:
`ext1` hace `rtc_gpio_init()` sobre el GPIO 4 y lo saca de la matriz digital **para siempre**,
así que mata a la vez la ISR de `setDio1Action()` **y** el rearme por nivel de `radioLoop()`
que estaba puesto de red de seguridad. Por eso no se recuperaba.

Lo que hay hoy en `hal::dormirHasta()`:

- **`gpio_wakeup_enable()` + `esp_sleep_enable_gpio_wakeup()`**, con un nivel **por pin**:
  DIO1 (**4**) en ALTO, INT del teclado (**11**) en BAJO — es de colector abierto — y el botón
  (**0**) en BAJO. Con `ext1` el botón no cabía: su máscara impone un nivel único.
- **Solo se arma lo que esté en reposo.** Una línea ya activa despertaría en el acto y el
  light sleep se volvería un no-op silencioso que gasta más que el `delay()`.
- **La ISR de DIO1 se calla mientras se duerme** (`gpio_intr_disable`) y se le devuelve el
  flanco de subida al despertar (`gpio_set_intr_type(POSEDGE)`, que es lo que significa el
  RISING de `Module::setDio1Action()`). El despertar no necesita la ISR, lo dispara el bit de
  wakeup del pin; y así se evita de raíz la tormenta de interrupciones por nivel. El aviso no
  se pierde porque `radioLoop()` rearma leyendo el nivel de DIO1 en cada vuelta.
- **No se duerme si hay un ordenador en el USB** (`hal::usbConectado()`, con histéresis).
  Sirve para no perder una consola **ya abierta**; NO es una red de seguridad para flashear
  — ver la fila de esptool en la tabla de trampas.
- **No se duerme encima de una ráfaga NMEA**, que es un efecto colateral que no estaba
  previsto y que habría matado en silencio los avisos de posición automáticos. Dos filas de
  la tabla de trampas.
- `esp_pm_configure()` **sigue sin servir**: `CONFIG_PM_ENABLE` no está activado en el
  `sdkconfig` de Arduino-ESP32 6.12.0 para el S3, así que el light sleep automático no es una
  opción sin recompilar el framework. No perder tiempo por ahí. `esp_light_sleep_start()` vive
  en `esp_sleep`, no en `esp_pm`, y sí está.
- `millis()` va sobre `esp_timer`, que sigue contando durmiendo, así que **el contador de
  ciclo de trabajo no se descuadra**. Se desvía ~1 % porque durmiendo el reloj es el RC de
  150 kHz del RTC: irrelevante para un presupuesto del 10 % con bloqueo duro, pero es la razón
  de que `GPS_TRAMA_MARGEN_MS` sean 100 ms y no 30.
- `informeEnergia()` saca residencia, número de sueños, causa del despertar y por qué NO se
  durmió. **Sin eso el modo de fallo probable es invisible**: no se distingue "duerme y
  ahorra" de "cree que duerme y vuelve al instante".
- Se desactiva en compilación con `-DFT_NO_LIGHT_SLEEP`.

**Pendiente de medir**: que el nodo siga oyendo la radio dormido, con dos aparatos, y cuántos
mA ahorra de verdad. Ver `ESTADO.md`.

Otras dos, por orden, cuando haya una medida real delante:

- **El GPS no se apaga nunca.** `hal::gpsEncender(false)` hace `Serial1.end()`, que cierra el
  UART pero **deja el ATGM336H alimentado y siguiendo satélites** (~20-25 mA estimados). El
  ajuste "GPS: apagado" no ahorra batería hoy. No hay pin de alimentación documentado en el
  Cap, así que la vía sería un comando de standby del AT6668: **hay que confirmar la cadena
  contra la hoja de comandos del chip, no inventarla.**
- **El altavoz no se para nunca**: `M5.begin()` lo arranca y nadie llama a `end()`, así que el
  ES8311 y el NS4150B están alimentados siempre. Medir antes: si son 0,3 mA no merece la pena.

Lo último de la lista, y solo si hace falta, es `startReceiveDutyCycleAuto(preambleLength, 8)`
para el RX cíclico del SX1262: ahorra unos 3-4 mA y es **el único cambio de la lista que puede
hacer que se pierda un paquete**. En un aparato donde el SOS tiene que llegar, es el que menos
gana y el que más arriesga.

**Nada de esto está medido.** Los mA son de hoja de datos y los reguladores de la placa tienen
su propio consumo en vacío. Con un medidor USB en línea y cuatro escenarios (pantalla apagada
con GPS y sin él, pantalla encendida, transmitiendo) se sabe en diez minutos cuál manda.
