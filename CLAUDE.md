# Fantashtic — contexto del repositorio

Firmware standalone para **M5Stack Cardputer Adv + Cap LoRa-1262**, compatible a nivel de aire
con Meshtastic pero con interfaz propia, pensado para un grupo cerrado de 3-4 nodos familiares.
Parte de los usuarios son niños, y eso gobierna casi todas las decisiones de interfaz.

---

## ⚠ ESTADO ACTUAL — LEER PRIMERO

**El firmware está completo y auditado, pero NUNCA se ha ejecutado en el aparato.**
Todas las fases (0 a 10) están escritas. Faltaba solo la sesión con hardware.

- **63 tests pasan en el ordenador** (`pio test -e native`): protocolo, router, ciclo de
  trabajo, criptografía, geodesia, UTF-8, tabla de nodos y los mensajes `Position` / `User`
  / `Telemetry`. Todo eso está verificado de verdad, no "revisado".
- **Sin verificar**: pantallas, teclado, GPS, microSD, radio y toda la integración. Escrito
  y auditado dos veces, pero no ejecutado.

**Bloqueante inmediato**: `I2C_ADDR_ANTSW` en `include/config.h` vale `0xFF` (centinela de
"sin configurar"). Mientras siga así, `antennaBegin()` devuelve `SIN_CONFIGURAR`,
`radioBegin()` se niega a arrancar y el firmware se queda en la pantalla de error. **Es una
protección deliberada, no un fallo.** La dirección real sale del escáner I²C que el firmware
imprime por consola al encender: es la que aparece con el Cap montado y no aparece sin él.
Candidata más probable: **0x43**.

Ver `ESTADO.md` para el detalle de qué falta y en qué orden probarlo.

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

Tests en el ordenador (**63 casos, 63 en verde**). Necesitan un `g++` de
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
  ui/          cuatro pantallas, SOS, asistente
  store/       NVS (config y clave) + microSD (historial)
```

Reglas de dependencia:

- `ui/` **nunca** toca `radio/` directamente; pasa por `app/`.
- `proto/`, `mesh/` y `util/` **no dependen de Arduino** y compilan en el ordenador. Es lo que
  permite que existan los 63 tests. Al añadir código a esas capas, mantener la propiedad y
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
| El estado de entrega va al mensaje anterior | `recordarIdPaquete()` marca el **último** del buffer: hay que llamarlo DESPUÉS de `anadirMensaje()` |
| La pantalla de nodos dice "no se ve a nadie" hasta que alguien manda un mensaje, y entonces sale el número y no el nombre | El descubrimiento estaba **al revés**: al oír a un desconocido se anunciaba el nombre PROPIO, cuando lo que hace falta es pedir el del otro. Si el otro lado ya te conocía, su regla no se disparaba y se quedaba callado. Pasaba siempre que un nodo se reiniciaba con el otro encendido (la tabla de nodos vive en RAM). Arreglado con `Data.want_response` (campo 3 de `mesh.proto`): el NODEINFO del arranque y el que dispara oír a un desconocido lo piden, y quien lo recibe contesta con el suyo **sin** pedirla, o serían dos nodos contestándose para siempre |
| El SOS se corta tras un envío | Cualquier mensaje entrante lo cancelaba; ahora exige completar la ráfaga |
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
- **Falta una pantalla de posición propia** (lat, lon, altitud, satélites, precisión). Hoy esos
  datos solo salen por consola en `informeGps()`, o sea que hacen falta un ordenador y un cable.
  Especificada en `ESTADO.md`. Ojo con un detalle: `Fix::satelites` son los satélites **usados
  en la solución** (trama GGA), no los visibles; los visibles están en las tramas GSV, que
  TinyGPSPlus no parsea sin un `TinyGPSCustom`. No etiquetar como "visibles" lo que son "usados".
- **La geometría de las cuatro pantallas en Font2 está calculada, no vista.** El ancho se
  mide en tiempo de ejecución con `D.textWidth()` donde puede pisarse algo (el valor de cada
  fila de ajustes, la columna de detalle de nodos), pero las posiciones verticales son
  aritmética sobre 240×135. Confirmar en el aparato antes de darlas por buenas.
### ⭐ Autonomía: el light sleep es lo siguiente y lo más importante

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

**Lo que falta, y es la ganancia grande: light sleep.** Investigado ya, para no repetirlo:

- **`CONFIG_PM_ENABLE` NO está activado** en el `sdkconfig` de Arduino-ESP32 6.12.0 para el S3
  (comprobado en `packages/framework-arduinoespressif32/tools/sdk/esp32s3/sdkconfig`). Por
  tanto `esp_pm_configure()` no sirve y **el light sleep automático no es una opción** sin
  recompilar el framework. No perder tiempo por ahí.
- **Pero `esp_light_sleep_start()` sí está disponible**: vive en `esp_sleep`, no en `esp_pm`.
  Y encaja con esta arquitectura, que ya es un planificador cooperativo con tick explícito:
  se sustituye el `delay(tickMs())` por armar despertadores y dormir.
- Las tres fuentes de despertar necesarias son **GPIO de dominio RTC en el S3** (0-21), así
  que valen como `ext1`: DIO1 de la radio (**4**), INT del teclado (**11**) y el botón (**0**).
- `millis()` va sobre `esp_timer`, que sigue contando durante el light sleep, así que **el
  contador de ciclo de trabajo no se descuadra**. Verificarlo de todos modos.
- **Trampa segura**: el light sleep tumba el USB-Serial-JTAG y se pierde la consola. Hay que
  activarlo solo con la pantalla dormida, o detrás de una bandera de compilación, o no habrá
  forma de depurar nada.
- **IMPLEMENTADO Y DESACTIVADO el 29-jul**: con él activo, el nodo se queda **sordo y mudo** en
  cuanto la pantalla se duerme. Está detrás de `-DFT_LIGHT_SLEEP`. Ver `ESTADO.md` para la
  medida y, sobre todo, para el **siguiente paso**: mirar cómo lo hacen Meshtastic y MeshCore,
  que corren sobre el mismo SX1262 con RadioLib. La hipótesis a comprobar primero es que usen
  `esp_sleep_enable_gpio_wakeup()` en vez de `esp_sleep_enable_ext1_wakeup()`: `ext1` mueve el
  pin al dominio RTC y eso es lo que se sospecha que mata la ISR de `setDio1Action()`, mientras
  que `gpio_wakeup` despierta desde light sleep sin sacarlo del dominio digital.
- `PIN_KB_INT` (GPIO 11) **no se usa hoy**: el teclado se sondea. Para el light sleep hay que
  cablearlo como fuente de despertar.

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
