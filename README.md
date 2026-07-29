# Fantashtic

Mensajería LoRa privada para **M5Stack Cardputer Adv + Cap LoRa-1262**. Sin móvil, sin
internet, sin servidores y sin cuenta de nada: cuatro aparatos que hablan entre ellos
directamente por radio, hasta varios kilómetros en campo abierto.

Está escrito desde cero para un grupo familiar cerrado, y parte de los usuarios son niños.
Eso gobierna casi todas las decisiones de interfaz: sin menús anidados, sin estados de los
que no se pueda salir, el SOS siempre cancelable y la letra grande.

- Chat de texto cifrado en un único canal privado, con clave derivada de una frase
- Retransmisión automática: un mensaje de A llega a C pasando por B
- Botón de SOS con posición GPS y alarma que despierta la pantalla del receptor
- Brújula que apunta a cualquier nodo del grupo
- Historial en microSD, legible en cualquier ordenador

**No es** un fork de `meshtastic/firmware`. No arrastra su código, ni MQTT, ni Bluetooth, ni
la app móvil, ni el API de protobufs por serie. **Tampoco es** un nodo de la red pública
Meshtastic: usa un canal privado con clave propia.

El formato de paquete que va por el aire —cabecera de 16 bytes, AES-CTR, encaminamiento por
inundación— sí es el de Meshtastic, reimplementado con fidelidad de byte. Eso es una decisión
de **método, no un objetivo**: permite contrastar la implementación contra vectores conocidos
y, si algún día hace falta, depurar con un nodo de firmware oficial en la mesa. La
interoperabilidad con Meshtastic **nunca fue un requisito** y no se promete: es un grupo
cerrado con su propia clave.

---

## ⚠ Antes de encender

**Atornilla la antena antes de dar corriente.** Transmitir sin antena destruye el SX1262 de
forma permanente. El firmware no transmite nada hasta que lo confirmas en pantalla, pero más
vale no tentar a la suerte.

**El límite legal de ciclo de trabajo no se puede desactivar.** En la subbanda ETSI g3
(869,4–869,65 MHz) son 500 mW p.r.e. y **10 % de emisión por hora**. El firmware lleva un
contador rodante de 60 minutos que bloquea la transmisión al llegar al límite, y no hay
ninguna opción en la interfaz para saltárselo. El porcentaje se ve siempre en la barra de
estado, en verde, naranja y rojo, para que si un día bloquea quede claro que es la ley y no
una avería.

---

## Hardware

| Pieza | Modelo |
|---|---|
| Aparato | M5Stack **Cardputer Adv** (SKU K132-Adv) — ESP32-S3, 8 MB flash, sin PSRAM |
| Radio | M5Stack **Cap LoRa-1262** (SKU U214) — SX1262, +22 dBm, RP-SMA |
| GPS | ATGM336H-6N, integrado en el Cap |
| Pantalla | ST7789V2, 240 × 135 |
| Teclado | 56 teclas, escáner TCA8418 |
| Batería | Li-ion 1750 mAh |

El pinout completo y verificado está en `CLAUDE.md`. Los valores salen de la documentación de
M5Stack cruzada con el `variant.h` del Cardputer Adv de Meshtastic, y están medidos en el
aparato donde hubo discrepancia.

Se necesitan **al menos dos** aparatos para que esto sirva de algo. Con tres se puede probar
la retransmisión.

---

## Compilar y flashear

```bash
pio run                                   # compilar
pio run -t upload --upload-port COM10     # flashear
pio device monitor -b 115200              # consola
pio test -e native                        # 63 tests de protocolo, en el ordenador
```

El aparato se identifica por USB con VID:PID `303A:1001` (USB-Serial-JTAG). Si no lo detecta:
interruptor a OFF, mantener el botón G0, dar corriente, soltar.

La consola solo imprime el diagnóstico al arrancar, así que para verlo hay que encadenar
`-t upload -t monitor`: abrir el monitor después llega tarde.

> **En Windows, ojo con la longitud de la ruta.** El toolchain xtensa (GCC 8.4) no es
> long-path aware y Windows corta en 260 caracteres. Si el proyecto está hondo, sale un
> `fatal error: <cabecera>: No such file or directory` con la librería perfectamente
> instalada. Se resuelve compilando a través de una unión, que no necesita permisos de
> administrador: `cmd /c mklink /J C:\ft <ruta-real>`. Y usar **PowerShell**: Git Bash
> resuelve la unión al hacer `cd` y vuelve a la ruta larga.

---

## Puesta en marcha

1. Enciende el aparato. La primera vez sale el asistente, de dos pasos.
2. **Nombre.** Uno largo (p. ej. `Papa`). Las 4 primeras letras en mayúsculas son el nombre
   corto, que es el que se ve en el chat y en las listas.
3. **Frase del grupo.** Tiene que ser **exactamente la misma en todos los aparatos**: es la
   clave. Tarda ~0,5 s en derivarla.
4. Repite en los demás. Ya está.

La primera vez pide confirmar que la antena está puesta. Se pregunta **una sola vez**, no en
cada arranque: el Cap va atornillado fijo y una confirmación repetida acaba pulsándose sin
leer. Vuelve a salir tras un restablecimiento de fábrica.

Si cambias la frase en un aparato, deja de ver a los demás hasta que la cambies en todos.

---

## Teclas

El Cardputer no tiene flechas físicas: se hacen con **Fn**. Y la tecla `` ` `` hace de ESC
por comodidad, sin necesidad de Fn.

| Tecla | Qué es |
|---|---|
| `Fn` + `;` | ↑ arriba |
| `Fn` + `.` | ↓ abajo |
| `Fn` + `,` | ← izquierda *(sin uso hoy)* |
| `Fn` + `/` | → derecha *(sin uso hoy)* |
| `Fn` + `` ` `` o `` ` `` sola | ESC |
| `Tab` | cambiar de pantalla |
| `Enter` | enviar / elegir |
| `Backspace` | borrar |
| Botón **G0**, 2 segundos | **SOS** (ver el aviso de abajo) |

Los caracteres `;` `.` `,` `/` sin Fn se escriben con normalidad.

---

## Las cuatro pantallas

`Tab` va rotando entre ellas. `ESC` vuelve al chat desde cualquiera.

### Chat

Escribes y `Enter` envía. `↑` `↓` recorren el historial hacia atrás; `ESC` vuelve al final, y
si no estabas mirando atrás, borra lo que llevabas escrito.

Cada mensaje propio lleva una marca de estado a la izquierda del texto:

| Marca | Significado |
|---|---|
| `.` | enviado, todavía sin confirmar |
| `+` | **propagado**: alguien lo ha retransmitido y lo hemos oído volver (ACK implícito) |
| `!` | fallido |

El límite de mensaje son **180 bytes UTF-8**, no 180 caracteres: cada acento ocupa 2 y cada
emoji 4. El contador de la derecha cuenta bytes.

### Nodos

Quién se ha oído, hace cuánto, con qué calidad de señal, a cuántos saltos, con cuánta batería
y a qué distancia. `↑` `↓` para moverse, `Enter` para saltar a la brújula de ese nodo.

Caben 5 filas en pantalla y la tabla admite 16 nodos, así que la lista arrastra: el contador
del pie (`4/12`) indica que hay filas fuera de la ventana.

### Brújula

Distancia y flecha hacia el nodo elegido.

**En parado no hay rumbo real.** El Cardputer Adv lleva un BMI270, que es un acelerómetro, no
un magnetómetro: no hay brújula magnética. Andando, el GPS da el rumbo y la flecha apunta de
verdad al nodo. Parado, la flecha apunta al **norte geográfico** y lo dice en pantalla
(`N arriba (parado)`). La distancia siempre es correcta.

### Ajustes

`↑` `↓` para moverse, `Enter` para actuar. Siete filas, todas funcionales:

| Fila | Qué hace `Enter` |
|---|---|
| **Nombre del nodo** | abre el editor de texto, hasta 15 letras |
| **Nombre corto** | abre el editor, 4 letras, siempre en mayúsculas |
| **Brillo** | sube un nivel de **1 a 5** y da la vuelta |
| **Volumen** | sube un nivel de **1 a 5** y da la vuelta, con un pitido para oírlo |
| **GPS** | enciende y apaga |
| **Frase del grupo** | abre el editor, **y pide confirmación** (en naranja) |
| **Información** | pantalla de solo lectura; `ESC` para salir |

Brillo y volumen van en una escala de **1 a 5**, no en unidades crudas: «volumen 180» sobre
255 no le dice nada a nadie, y menos a un niño. Por dentro se sigue guardando el valor real,
así que no hace falta migrar nada de lo ya configurado.

**El editor de texto**: escribes, `Backspace` borra, `Enter` guarda y `ESC` cancela sin
guardar. Precarga el valor actual, para que cambiar una letra no obligue a reescribirlo todo.
El contador de la derecha cuenta **bytes**, que es el límite real del campo.

Al cambiar cualquiera de los dos nombres se adelanta el envío del `NODEINFO`, para que los
demás vean el nombre nuevo en segundos en vez de esperar hasta 15 minutos.

> **La frase del grupo es la única ruta destructiva de los ajustes**, y por eso va en naranja
> y detrás de una pantalla de confirmación: cambiarla en un solo aparato lo deja incomunicado
> del resto hasta que la cambies en todos. La frase **no se precarga en el editor** porque no
> se guarda en ninguna parte — en NVS solo está la clave derivada. La fila muestra el nombre
> del canal (p. ej. `FT-6BJHH`), que sale de la propia frase: sirve para comprobar de un
> vistazo que dos aparatos están en el mismo grupo sin enseñar la frase a nadie.

### Barra de estado

De izquierda a derecha: nombre corto propio en su color, calidad del último enlace en cuatro
puntos, número de nodos activos en los últimos 15 minutos, **ciclo de trabajo en %**, estado
del GPS, `SD?` si no hay tarjeta, y batería.

---

## SOS

Mantener el botón **G0 2 segundos** desde cualquier pantalla. Sale una cuenta atrás de
3 segundos: `Enter` lo envía ya, `ESC` lo cancela.

> **G0 es el botón del propio módulo StampS3A, el mismo que se usa para entrar en modo
> descarga.** No hay ningún botón dedicado en el chasis. En el código es
> `M5Cardputer.BtnA`, que M5Unified asocia a GPIO 0 en esta placa
> (`M5Unified.cpp`, `board_M5CardputerADV` va junto a `board_M5StampS3`).
>
> **Esto es una mala elección para un disparador de emergencia y está pendiente de
> cambiar**, por dos razones: es un botón pequeño y pensado para el arranque, no para
> pulsarlo con prisa o con guantes; y si se mantiene pulsado **mientras se enciende** el
> aparato, el ESP32-S3 entra en modo descarga y la pantalla se queda apagada, o sea que
> parece averiado. Para un aparato que van a usar niños eso es exactamente la clase de ruta
> fácil hacia un estado roto que el proyecto se prohíbe.

Una vez lanzado se manda una ráfaga de 3 envíos separados 30 segundos, y después se repite
cada 5 minutos hasta un máximo de una hora. Una pulsación corta del botón cancela un SOS
propio ya en marcha, en cualquier momento.

En el receptor la pantalla se pone roja e intermitente, suena la alarma y **se enciende la
pantalla aunque estuviera dormida**. Se queda ahí, sin límite de tiempo, hasta que alguien
pulse. `Enter` responde «Voy hacia ti» de un toque; cualquier otra tecla cierra el aviso.
Si el nodo que pide ayuda tiene posición, se ve la distancia y el punto cardinal en grande.

**No es un sustituto de las normas.** Es una ayuda para mantenerse en contacto, no una
garantía de cobertura: en bosque cerrado un SOS puede no llegar.

---

## Radio y cifrado

| Parámetro | Valor |
|---|---|
| Frecuencia | 869,525 MHz (EU_868, subbanda ETSI g3) |
| Preset | LONG_FAST — SF11, BW 250 kHz, CR 4/5 |
| Potencia | +22 dBm conducidos (~193 mW p.r.e. con la antena de 3 dBi incluida) |
| Ciclo de trabajo | 10 % por hora, ventana rodante de 60 minutos, **no desactivable** |
| Saltos máximos | 3 |
| Cifrado | AES-256-CTR, igual que Meshtastic |

La clave se deriva de la frase del grupo con **PBKDF2-HMAC-SHA256**, 20 000 iteraciones,
32 bytes. El nombre del canal sale también de la frase (`"FT-" + base32(SHA256(frase))`), para
que dos grupos con frases distintas no colisionen en el byte de canal de la cabecera. La clave
derivada vive en NVS; **la frase nunca se guarda**.

AES, SHA-256, HMAC y PBKDF2 están escritos a mano en `src/proto/`, no se usa mbedTLS. El
motivo es poder probarlos: mbedTLS existe en el ESP32 pero no en el ordenador, así que los
tests validarían una implementación distinta de la que se embarca. Escritos a mano, el mismo
código corre en los dos sitios y los vectores lo contrastan contra `hashlib` y `pycryptodome`.

### El salt es por despliegue

`include/config.h` trae `"fantashtic-v1"` por defecto, que es el que reproduce los vectores de
`docs/VECTORES.md`. Cada grupo lo sustituye por uno propio en `include/secrets.h`, que está en
`.gitignore` (ver `include/secrets.example.h`). El archivo es opcional: un clon recién hecho
compila y pasa los 63 tests sin crearlo.

El salt nunca fue el secreto —la frase sí—, pero uno por grupo impide que una sola tabla
precalculada contra Fantashtic sirva contra todos los despliegues. Es un *pepper*.

> **El punto débil real no es el salt: son las 20 000 iteraciones**, que son pocas para una
> frase corta y memorizable. Contra un atacante con GPU la defensa es la **longitud de la
> frase**, no el número de iteraciones. Usad una frase larga.

Cambiar el salt **no** rompe los nodos ya configurados, porque la clave derivada está en NVS y
nadie vuelve a derivarla. Pero un nodo nuevo derivará otra clave de la misma frase: al poner o
cambiar el salt, hay que repasar la frase en **todos** los nodos.

---

## Estado

**Funciona en el aparato**, con dos nodos midiendo de verdad. Verificado:

| Qué | Resultado medido |
|---|---|
| Arranque, pantalla, teclado, microSD, NVS | ✓ |
| Radio SX1262 | ✓ 869,525 MHz, SF11/BW250, TCXO a 1,8 V a la primera |
| Enlace entre dos nodos | ✓ −18 a −21 dBm, SNR 6–7 a corta distancia |
| Cifrado y protocolo en el aire | ✓ dos aparatos independientes se entienden |
| Retransmisión y ACK implícito | ✓ el contador de saltos sube 0 → 1 |
| Los dos sentidos del enlace | ✓ |
| GPS | ✓ fix con 8-9 satélites, HDOP 2,3 |

**63 tests en verde en el ordenador** (`pio test -e native`), en cuatro suites: protocolo,
router, ciclo de trabajo, criptografía, geodesia, UTF-8, tabla de nodos y los mensajes
`Position` / `User` / `Telemetry`. El paquete que sale por antena es byte a byte idéntico al
de Meshtastic, contrastado contra vectores generados con `pycryptodome`.

Lo que **no** está verificado todavía:

- **Retransmisión con tres aparatos**, la única que no se puede probar con dos. Es la que
  falta de verdad: es una función del producto, no una comprobación de laboratorio.
- **GPS y brújula en exterior**, contrastando distancias contra un mapa.
- **SOS de punta a punta** y cambio de frase del grupo en caliente.
- **Autonomía.** No hay light sleep y el SX1262 está en recepción continua.
- La geometría de las pantallas está calculada, no vista en todos los casos límite.

*(Contrastar contra un nodo Meshtastic oficial sigue estando disponible como herramienta de
depuración si algo del protocolo da problemas, pero no es un objetivo pendiente: el protocolo
ya está validado con vectores en el ordenador y con dos aparatos hablándose en el aire.)*

Detalle de qué falta y en qué orden probarlo, en `ESTADO.md`.

### Fases

- [x] 0 — andamiaje · [x] 1 — HAL · [x] 2 — radio en crudo
- [x] 3 — protocolo Meshtastic · [x] 4 — router de inundación · [x] 5 — ciclo de trabajo
- [x] 6 — chat e interfaz · [x] 7 — nodos, `NODEINFO` y telemetría
- [x] 8 — GPS, posición y brújula · [x] 9 — emergencia
- [x] 10 — ajustes y asistente de primer arranque
- [ ] 11 — acabado, tras las pruebas de campo con tres aparatos

---

## Consejos de alcance

Salen del presupuesto de enlace de `docs/ALCANCE.md`, y son los que más se notan:

**La antena va vertical.** Una antena tumbada hablando con una de pie pierde más señal que
atravesar un bosque. Es el error más caro y el más fácil de evitar.

**Sácalo del bolsillo si no te llega.** El cuerpo absorbe la señal: con el brazo en alto se
llega casi al doble de distancia.

**La altura importa más que la potencia.** Un nodo en alto multiplica el alcance del grupo
entero. En campo abierto, varios kilómetros; en bosque denso, menos de uno.

---

## Arquitectura

```
src/
  main.cpp     arranque, armado de antena, bucle, botón de SOS
  hal/         antenna i2c_bus keys led power sound storage gps
  radio/       SX1262 con RadioLib, cola TX, CAD, ciclo de trabajo
  proto/       aes crypto kdf packet messages   ← sin Arduino, se prueba en el PC
  mesh/        history router duty nodedb        ← sin Arduino, se prueba en el PC
  util/        geo utf8                          ← sin Arduino, se prueba en el PC
  app/         orquestación, SOS, envíos periódicos
  ui/          cuatro pantallas, SOS, asistente
  store/       NVS (config y clave) + microSD (historial)
```

Dos reglas que sostienen el resto:

- `ui/` **nunca** toca `radio/` directamente; pasa por `app/`.
- `proto/`, `mesh/` y `util/` **no dependen de Arduino** y compilan en el ordenador. Es lo que
  permite que existan los 63 tests. El estado temporal (reloj, aleatoriedad) se pasa por
  parámetro en lugar de leer `millis()` dentro de la lógica, y por eso el router y el ciclo de
  trabajo se pueden probar de forma determinista.

Sin PSRAM: buffers estáticos dimensionados en `include/config.h` y cero asignaciones
dinámicas después de `setup()`. El código de radio no bloquea; nada de `delay()` fuera de
`setup()`.

---

## Documentación

| Archivo | Contenido |
|---|---|
| `CLAUDE.md` | contexto del repo, pinout verificado, restricciones duras, trampas ya pisadas |
| `ESTADO.md` | qué está probado en el aparato y qué falta, con el orden de pruebas |
| `SPEC.md` | especificación técnica completa |
| `docs/ALCANCE.md` | presupuesto de enlace y plan de pruebas de campo |
| `docs/VECTORES.md` | vectores del protocolo y orden de depuración |
| `tools/gen_test_vectors.py` | generador de esos vectores |

---

## Licencia

MIT. Ver `LICENSE`.
