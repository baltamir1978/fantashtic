# Fantashtic

Mensajería LoRa privada para **M5Stack Cardputer Adv + Cap LoRa-1262**. Sin móvil, sin
internet, sin servidores y sin cuenta de nada: cuatro aparatos que hablan entre ellos
directamente por radio, hasta varios kilómetros en campo abierto.

Está escrito desde cero para un grupo familiar cerrado, y parte de los usuarios son niños.
Eso gobierna casi todas las decisiones de interfaz: sin menús anidados, sin estados de los
que no se pueda salir, el SOS siempre cancelable y la letra grande.

- Chat de texto cifrado en un único canal privado, con clave derivada de una frase
- Retransmisión automática: un mensaje de A llega a C pasando por B
- SOS con posición GPS y alarma que despierta la pantalla del receptor
- Brújula que apunta a cualquier nodo del grupo
- Historial en microSD, legible en cualquier ordenador
- Todo configurable desde el aparato: nombres, frase, alcance, potencia y saltos

**No es** un fork de `meshtastic/firmware`. No arrastra su código, ni MQTT, ni Bluetooth, ni
la app móvil, ni el API de protobufs por serie. **Tampoco es** un nodo de la red pública
Meshtastic: usa un canal privado con clave propia.

El formato de paquete que va por el aire —cabecera de 16 bytes, AES-CTR, encaminamiento por
inundación— sí es el de Meshtastic, reimplementado con fidelidad de byte. Eso es una decisión
de **método, no un objetivo**: permite contrastar la implementación contra vectores conocidos
y, si algún día hace falta, depurar con un nodo de firmware oficial. La interoperabilidad
**nunca fue un requisito** y no se promete.

---

## ⚠ Antes de encender

**Atornilla la antena antes de dar corriente.** Transmitir sin antena destruye el SX1262 de
forma permanente. El firmware no transmite hasta que lo confirmas en pantalla, pero más vale
no tentar a la suerte.

**El límite legal de ciclo de trabajo no se puede desactivar.** En la subbanda ETSI g3
(869,4–869,65 MHz) son 500 mW p.r.e. y **10 % de emisión por hora**. Hay un contador rodante
de 60 minutos que bloquea la transmisión al llegar al límite, y **ninguna** opción en la
interfaz para saltárselo. El porcentaje se ve siempre en la barra de estado.

---

## Hardware

| Pieza | Modelo |
|---|---|
| Aparato | M5Stack **Cardputer Adv** (K132-Adv) — ESP32-S3, 8 MB flash, sin PSRAM |
| Radio | M5Stack **Cap LoRa-1262** (U214) — SX1262, +22 dBm, RP-SMA |
| GPS | ATGM336H-6N (AT6668), integrado en el Cap |
| Pantalla | ST7789V2, 240 × 135 |
| Teclado | 56 teclas, escáner TCA8418 |
| Batería | Li-ion 1750 mAh |

El pinout completo y verificado está en `CLAUDE.md`. Se necesitan **al menos dos** aparatos;
con tres se puede probar la retransmisión.

---

## Compilar y flashear

```bash
pio run                                   # compilar
pio run -t upload --upload-port COM10     # flashear
pio device monitor -b 115200              # consola
pio test -e native                        # 64 pruebas de protocolo, en el ordenador
```

El aparato se identifica por USB con VID:PID `303A:1001` (USB-Serial-JTAG). La consola solo
imprime el diagnóstico al arrancar, así que para verlo hay que encadenar `-t upload -t monitor`.

**Si no responde al flashear**, fuerza el modo descarga: interruptor a **OFF**, mantener
pulsado **G0**, dar corriente, soltar. El bootloader del ESP32-S3 está en ROM y no se puede
sobrescribir, así que esto funciona siempre — ningún firmware puede dejar el aparato inservible.

> **En Windows, ojo con la longitud de la ruta.** El toolchain xtensa (GCC 8.4) no es
> long-path aware y Windows corta en 260 caracteres. Si el proyecto está hondo sale un
> `fatal error: <cabecera>: No such file or directory` con la librería perfectamente
> instalada. Se resuelve compilando a través de una unión: `cmd /c mklink /J C:\ft <ruta>`.
> Y usar **PowerShell**: Git Bash resuelve la unión al hacer `cd` y vuelve a la ruta larga.

---

## Puesta en marcha

1. Enciende el aparato. La primera vez sale el asistente, de dos pasos.
2. **Nombre.** Uno largo (p. ej. `Papa`). Las 4 primeras letras en mayúsculas son el nombre
   corto, que es el que se ve en el chat y en las listas.
3. **Frase del grupo.** Tiene que ser **exactamente la misma en todos los aparatos**: es la
   clave. Tarda ~0,5 s en derivarla.
4. Repite en los demás. Ya está.

La confirmación de antena se pide **una sola vez**, no en cada arranque: el Cap va atornillado
fijo y repetirla acaba pulsándose sin leer. Vuelve a salir tras un restablecimiento de fábrica.

---

## Teclas

El Cardputer no tiene flechas físicas: se hacen con **Fn**. Y `` ` `` hace de ESC por
comodidad, sin necesidad de Fn.

| Tecla | Qué es |
|---|---|
| `Fn` + `;` | ↑ arriba |
| `Fn` + `.` | ↓ abajo |
| `Fn` + `,` / `Fn` + `/` | ← → cambiar de conversación en el chat; → en Nodos abre un chat privado |
| `Fn` + `` ` `` o `` ` `` sola | ESC |
| `Tab` | cambiar de pantalla |
| `Enter` | enviar / elegir |
| `Backspace` | borrar |
| **`ESC` + `Backspace`, 2 segundos** | **SOS** |
| Botón **G0**, 2 segundos | SOS (secundario, ver abajo) |

El SOS eran `Enter` + `ESC` y se cambió: dos teclas no se levantan nunca en el mismo
milisegundo, así que soltando `ESC` primero quedaba `Enter` pulsado y **se enviaba la línea de
chat** en plena emergencia. Con `ESC` + `Backspace` lo peor que puede pasar al soltar es borrar
un carácter, y siguen estando en esquinas opuestas del teclado.

Los caracteres `;` `.` `,` `/` sin Fn se escriben con normalidad.

---

## Las cinco pantallas

`Tab` va rotando entre ellas. `ESC` vuelve al chat desde cualquiera.

### Chat

Escribes y `Enter` envía. `↑` `↓` recorren el historial; `ESC` vuelve al final, y si no
estabas mirando atrás, borra lo escrito. Marca de estado a la izquierda de cada mensaje propio:

| Marca | Significado |
|---|---|
| `.` | enviado, sin confirmar |
| `+` | **propagado**: alguien lo retransmitió y lo hemos oído volver (ACK implícito) |
| ✓ | **entregado**: el destinatario de un mensaje privado confirmó que le llegó |
| `!` | fallido (en un privado: sin confirmación tras 2 reintentos) |

**Conversaciones.** Hay un chat del grupo, `TODOS`, y uno **privado** con cada nodo. `←` `→`
(`Fn`+`,` / `Fn`+`/`) pasan de uno a otro, y en Nodos `→` abre directamente el privado con el
nodo resaltado. El destinatario sale **en la línea de escritura, en su color**, delante de lo
que escribes: es donde se mira al enviar. `ESC` con la línea vacía vuelve a `TODOS`. Un punto
naranja junto al contador avisa de mensajes sin leer **en otra conversación**, y en la lista de
nodos marca a quien te ha escrito.

Un privado **pide confirmación** y se reintenta (mismo paquete, 2 veces, cada ~12-15 s) hasta que
llega, igual que en Meshtastic. **Privado no es secreto**: va cifrado con la clave del grupo,
así que un nodo que lo releva podría leerlo; simplemente no lo enseña.

**Historial en la microSD.** Todo se guarda en `/fantashtic/chat/chat.log` (texto separado por
tabuladores, se abre en una hoja de cálculo) y, **si hay tarjeta**, al arrancar se recuperan los
últimos 60 mensajes con su conversación. Se desactiva en Ajustes → *Recuperar chat*. Los
ficheros mensuales de versiones anteriores (`AAAA-MM.log`, `sin-fecha.log`) se quedan donde
estaban y no se recuperan.

El límite son **180 bytes UTF-8**, no 180 caracteres: cada acento ocupa 2 y cada emoji 4.

### Nodos

Quién se ha oído, hace cuánto, con qué señal, a cuántos saltos, con cuánta batería y a qué
distancia. `↑` `↓` para moverse, `Enter` para saltar a la brújula de ese nodo, `→` para
escribirle en privado. Caben 5 filas y
la tabla admite 16, así que la lista arrastra: el contador del pie (`4/12`) avisa de que hay
filas fuera de la ventana.

### Brújula

Distancia y flecha hacia el nodo elegido.

**En parado no hay rumbo real.** El Cardputer Adv lleva un BMI270, que es un acelerómetro, no
un magnetómetro. Andando, el GPS da el rumbo y la flecha apunta de verdad; parado, apunta al
**norte geográfico** y lo dice en pantalla. La distancia siempre es correcta.

### Mi posición

La hermana de la brújula: ésa dice dónde está *el otro*, ésta dónde estás *tú*. Cinco filas —
latitud, longitud, altitud, satélites y precisión — con los números en grande, porque el uso
real es **leer las coordenadas en voz alta por radio**.

Las coordenadas van con 5 decimales, que es ~1 metro y es el formato que se pega tal cual en
Google Maps. La precisión sale con número **y** palabra (`1.6 bueno`), porque el HDOP a secas no
le dice nada a nadie: `<1` excelente, `1-2` bueno, `2-5` aceptable, `>5` malo.

Los satélites son **dos** números cuando el módulo los da: `9 de 14` son los *usados en la
solución* de un total *a la vista*. No es lo mismo y no se mezcla: se ven muchos y se usan los
que dan buena geometría.

Y a veces los usados salen **más** que los que se ven, lo que parece imposible y no lo es: los
usados vienen de la trama GGA y cuentan todas las constelaciones, mientras que los «a la vista»
vienen de `$GPGSV`, que solo habla de GPS. O sea que ese segundo número es un suelo, no un total.
Cuando pasa (medido: 8 usados, 6 a la vista) la pantalla muestra solo el primero, en vez de un
`8 de 6` que no significaría nada.

Sin fix la pantalla dice **por qué** no lo hay, que es la mitad de para lo que sirve: si no ha
llegado ni un byte por el UART es el cable, y si llegan bytes pero no hay solución es el cielo.

### Ajustes

`↑` `↓` para moverse, `Enter` para actuar. Doce filas, todas funcionales:

| Fila | Qué hace `Enter` |
|---|---|
| **Nombre del nodo** | editor de texto, hasta 15 letras |
| **Nombre corto** | editor, 4 letras, siempre en mayúsculas |
| **Brillo** | sube un nivel de **1 a 5** y da la vuelta |
| **Volumen** | igual, con un pitido para oírlo |
| **GPS** | enciende y apaga |
| **Recuperar chat** | si al arrancar se cargan los últimos mensajes de la microSD (vale para el siguiente arranque) |
| **Información** | pantalla de solo lectura; `ESC` para salir |
| **Frase del grupo** | editor **+ confirmación** |
| **Alcance / velocidad** | Normal · Máximo alcance · Rápido, **+ confirmación** |
| **Potencia TX** | 22 · 17 · 14 · 10 · 5 · 2 dBm, **+ confirmación** |
| **Saltos máximos** | 1 a 7, **+ confirmación** |
| **Restablecer de fábrica** | **+ confirmación** |

Brillo y volumen van en escala **1 a 5**, no en unidades crudas: «volumen 180» sobre 255 no le
dice nada a nadie. Por dentro se guarda el valor real, así que no hay nada que migrar.

**El editor de texto**: escribes, `Backspace` borra, `Enter` guarda, `ESC` cancela. Precarga el
valor actual para que cambiar una letra no obligue a reescribirlo todo, y el contador de la
derecha cuenta **bytes**, que es el límite real del campo. Al cambiar un nombre se adelanta el
`NODEINFO`, así que los demás lo ven en segundos y no a los 15 minutos.

> **Las cinco últimas filas van juntas al final, en naranja y detrás de confirmación.** Pueden
> dejar el aparato incomunicado o sin configuración, y agruparlas es deliberado: hay que bajar
> a propósito. La confirmación dice **a qué se va a cambiar**, porque un «¿seguro?» a secas no
> es confirmar nada.
>
> **Dos nodos con presets distintos no se oyen en absoluto.** El alcance se cambia en todos o
> en ninguno.
>
> La frase **no se precarga** en el editor porque no se guarda en ninguna parte: en NVS solo
> está la clave derivada. La fila muestra el nombre del canal (p. ej. `FT-6BJHH`), que sale de
> la propia frase, y sirve para comprobar que dos aparatos están en el mismo grupo sin
> enseñársela a nadie.

### Barra de estado

Nombre corto propio en su color, calidad del último enlace en cuatro puntos, nodos activos en
los últimos 15 minutos, **ciclo de trabajo en %**, estado del GPS, `SD?` si no hay tarjeta, y
batería.

---

## SOS

**`ESC` + `Backspace` mantenidos 2 segundos**, desde cualquier pantalla. Sale una cuenta atrás de
3 segundos: `Enter` lo envía ya, `ESC` lo cancela.

Una vez lanzado se manda una ráfaga de 3 envíos separados 30 segundos, y después se repite cada
5 minutos hasta un máximo de una hora. Una pulsación corta cancela un SOS propio en marcha, en
cualquier momento.

En el receptor la pantalla se pone roja e intermitente, suena la alarma y **se enciende aunque
estuviera dormida**. Se queda así, sin límite, hasta que alguien pulse: `Enter` responde «Voy
hacia ti» de un toque. Si el que pide ayuda tiene posición, se ve la distancia y el punto
cardinal en grande.

> **El botón G0 también sirve, pero es el secundario y está pendiente de revisar.** No hay
> botón dedicado en el chasis: `BtnA` es GPIO 0, el botón del propio módulo StampS3A, el mismo
> que se usa para entrar en modo descarga. Es pequeño, está pensado para el arranque, y si se
> mantiene pulsado **mientras se enciende** el aparato entra en modo descarga con la pantalla
> apagada, o sea que parece averiado. Por eso el atajo de teclado es la vía principal.

**No es un sustituto de las normas.** Es una ayuda para mantenerse en contacto, no una garantía
de cobertura: en bosque cerrado un SOS puede no llegar.

---

## Radio y cifrado

| Parámetro | Valor |
|---|---|
| Frecuencia | 869,525 MHz (EU_868, subbanda ETSI g3) |
| Preset por defecto | LONG_FAST — SF11, BW 250 kHz, CR 4/5 |
| Potencia | +22 dBm conducidos (~193 mW p.r.e. con la antena de 3 dBi incluida) |
| Ciclo de trabajo | 10 % por hora, ventana rodante de 60 min, **no desactivable** |
| Saltos por defecto | 3 |
| Cifrado | AES-256-CTR |

La clave se deriva de la frase con **PBKDF2-HMAC-SHA256**, 20 000 iteraciones, 32 bytes. El
nombre del canal sale también de la frase (`"FT-" + base32(SHA256(frase))`), para que dos
grupos con frases distintas no colisionen en el byte de canal. La clave derivada vive en NVS;
**la frase nunca se guarda**.

AES, SHA-256, HMAC y PBKDF2 están escritos a mano en `src/proto/`, no se usa mbedTLS. El motivo
es poder probarlos: mbedTLS existe en el ESP32 pero no en el ordenador, así que los tests
validarían una implementación distinta de la que se embarca. Escritos a mano, el mismo código
corre en los dos sitios y los vectores lo contrastan contra `hashlib` y `pycryptodome`.

### El salt es por despliegue

`include/config.h` trae `"fantashtic-v1"` por defecto —el que reproduce los vectores de
`docs/VECTORES.md`— y cada grupo lo sustituye por uno propio en `include/secrets.h`, que está
en `.gitignore` (ver `include/secrets.example.h`). El archivo es **opcional**: un clon recién
hecho compila y pasa las 64 pruebas sin crearlo.

El salt nunca fue el secreto —la frase sí—, pero uno por grupo impide que una sola tabla
precalculada contra Fantashtic sirva contra todos los despliegues. Es un *pepper*.

> **El punto débil real no es el salt: son las 20 000 iteraciones**, que son pocas para una
> frase corta. Contra un atacante con GPU la defensa es la **longitud de la frase**. Usad una
> frase larga.

Cambiar el salt **no** rompe los nodos ya configurados, porque la clave está en NVS y nadie
vuelve a derivarla. Pero un nodo nuevo derivará otra clave de la misma frase: al poner o
cambiar el salt, hay que repasar la frase en **todos** los nodos.

---

## Autonomía

Con la batería de 1750 mAh, y **sin ninguna medida real todavía** — los números de abajo son de
hoja de datos:

| Hecho | Efecto |
|---|---|
| **Reloj adaptativo** 240/80 MHz | el CPU era el mayor consumidor y estaba siempre a 240 |
| **Tick adaptativo** 10/50 ms | menos tiempo despierto cuando solo se escucha |
| **Ritmo del GPS** 1 Hz / 0,2 Hz | **mucho menos de lo que se creía, ver abajo** |

Los dos primeros están verificados en el aparato. **80 MHz es el suelo a propósito**: con el CPU
a 80/160/240 el bus APB se queda en 80 MHz, así que los divisores de SPI y los baudios del UART
del GPS no cambian; por debajo de 80 habría que recalcularlos.

> **Corrección medida (30-jul-2026): el ritmo del GPS ahorra mucho menos de lo que decía este
> README.** Aquí se prometía bajar el caudal NMEA de ~820 a ~40 B/s. Medido en el aparato, con
> fix y sin él, pidiendo 5000 ms entre fijaciones: el caudal baja de ~630 a ~400 B/s y **las
> tramas siguen llegando cada ~1000 ms**. El AT6668 no hace caso del `$PCAS02` más que a medias.
> El firmware ahora **mide** ese intervalo y lo saca por consola (`rafaga cada 999 ms`) en vez de
> darlo por supuesto.

Lo que falta, por orden de impacto:

1. **Medir el consumo real** con un medidor USB. Todo lo demás depende de esto, y sigue sin
   haber ni una sola medida: los números de arriba son de hoja de datos.
2. **Light sleep — probado y DESCARTADO, no simplemente pendiente.** Es la ganancia grande y no
   se ha conseguido. Se encontró y arregló una causa real —`esp_sleep_enable_ext1_wakeup()` saca
   el GPIO de DIO1 de la matriz digital y lo pasa al dominio RTC sin devolverlo, lo que mata a la
   vez la interrupción de RadioLib y el rearme por nivel que hacía de red de seguridad— y con eso
   arreglado **el nodo desenchufado se sigue quedando sordo a los 30 segundos**, además de hacer
   crepitar el altavoz. Necesario pero no suficiente: queda otra causa sin encontrar. Está detrás
   de `-DFT_LIGHT_SLEEP` y **no se embarca**: en un aparato cuya razón de ser es recibir un SOS,
   dormir no puede costar la radio. Lo descartado y las pistas nuevas, en `ESTADO.md`.
3. **Apagar el GPS de verdad.** Hoy solo se baja su ritmo, y ahora se sabe que eso ahorra poco:
   `gpsEncender(false)` cierra el UART pero el AT6668 sigue siguiendo satélites. No hay comando de
   standby en las fuentes públicas del chip, ni pin de alimentación en el Cap.
4. **Parar el altavoz cuando está ocioso.** `M5.begin()` lo arranca y nadie lo para.

---

## Estado

**Funciona en el aparato**, con dos nodos midiendo de verdad:

| Qué | Resultado medido |
|---|---|
| Arranque, pantalla, teclado, microSD, NVS | ✓ |
| Radio SX1262 | ✓ 869,525 MHz, SF11/BW250, TCXO a 1,8 V a la primera |
| Enlace entre dos nodos | ✓ −12 a −29 dBm, SNR 6-7 a corta distancia |
| Cifrado y protocolo en el aire | ✓ entre dos aparatos independientes |
| Retransmisión y ACK implícito | ✓ el contador de saltos sube 0 → 1 |
| Los dos sentidos del enlace | ✓ |
| Descubrimiento de nodos | ✓ nombre aprendido en 7 s desde el arranque |
| GPS | ✓ fix en interior; hasta 24-25 satélites y HDOP 0,6 junto a la ventana |
| Reloj y tick adaptativos | ✓ 80 MHz sin perder radio ni GPS |
| Light sleep | ✗ **descartado por medida**: deja el nodo sordo a los 30 s |

**95 pruebas en verde en el ordenador** (`pio test -e native`): protocolo, router, ciclo de
trabajo, criptografía, geodesia, UTF-8, tabla de nodos, los mensajes `Position` / `User` /
`Telemetry` / `Routing`, los reintentos de los privados y el formato del historial. El paquete que sale por antena se contrasta contra vectores generados con
`pycryptodome`.

Lo que **no** está verificado:

- **Mensajes privados y recuperación del historial**: escritos y probados en el ordenador,
  sin compilar todavía para el ESP32 ni probar en el aparato.
- **Retransmisión con tres aparatos**, la única que no se puede probar con dos. Es la que falta
  de verdad: es una función del producto.
- **La geometría de las pantallas.** Las cinco están calculadas sobre 240 × 135, no vistas. El
  ancho de lo que puede pisarse se mide en tiempo de ejecución con `textWidth()`, pero las
  posiciones verticales son aritmética. Afecta sobre todo a las más nuevas: ajustes (editor,
  confirmaciones, información) y **Mi posición**.
- **SOS de punta a punta**, incluido el atajo de teclado, y cambio de frase del grupo en caliente.
- **GPS y brújula en exterior**, contrastando distancias contra un mapa.
- **Consumo real**, y por tanto la autonomía.

Detalle y orden de pruebas en `ESTADO.md`.

---

## Consejos de alcance

Salen del presupuesto de enlace de `docs/ALCANCE.md`, y son los que más se notan:

**La antena va vertical.** Una antena tumbada hablando con una de pie pierde más señal que
atravesar un bosque. Es el error más caro y el más fácil de evitar.

**Sácalo del bolsillo si no te llega.** El cuerpo absorbe la señal: con el brazo en alto se
llega casi al doble.

**La altura importa más que la potencia.** Un nodo en alto multiplica el alcance del grupo
entero. En campo abierto, varios kilómetros; en bosque denso, menos de uno.

---

## Arquitectura

```
src/
  main.cpp     arranque, armado de antena, bucle, SOS, informes de consola
  hal/         antenna i2c_bus keys led power sound storage gps
  radio/       SX1262 con RadioLib, cola TX, CAD, ciclo de trabajo, presets
  proto/       aes crypto kdf packet messages   ← sin Arduino, se prueba en el PC
  mesh/        history router duty nodedb        ← sin Arduino, se prueba en el PC
  util/        geo utf8                          ← sin Arduino, se prueba en el PC
  app/         orquestación, SOS, envíos periódicos, descubrimiento
  ui/          cinco pantallas, editor, confirmaciones, SOS, asistente
  store/       NVS (config y clave) + microSD (historial)
```

Tres reglas que sostienen el resto:

- `ui/` **nunca** toca `radio/` directamente; pasa por `app/`.
- `radio/` no depende de `store/`: el preset y la potencia entran por parámetro.
- `proto/`, `mesh/` y `util/` **no dependen de Arduino** y compilan en el ordenador. Es lo que
  permite que existan las 64 pruebas. El estado temporal (reloj, aleatoriedad) se pasa por
  parámetro en lugar de leer `millis()` dentro de la lógica, y por eso el router y el ciclo de
  trabajo se pueden probar de forma determinista.

Sin PSRAM: buffers estáticos dimensionados en `include/config.h` y cero asignaciones dinámicas
después de `setup()`. El código de radio no bloquea; nada de `delay()` fuera de `setup()`.

---

## Documentación

| Archivo | Contenido |
|---|---|
| `CLAUDE.md` | contexto, pinout verificado, restricciones duras, trampas ya pisadas |
| `ESTADO.md` | qué está probado en el aparato y qué falta, con el orden de pruebas |
| `SPEC.md` | especificación técnica completa |
| `docs/ALCANCE.md` | presupuesto de enlace y plan de pruebas de campo |
| `docs/VECTORES.md` | vectores del protocolo y orden de depuración |
| `docs/Memoria_tecnica_Fantashtic.docx` | memoria técnica, generada con `tools/gen_memoria.py` |

---

## Licencia

MIT. Ver `LICENSE`.
