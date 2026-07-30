# Cómo retomar el proyecto en Claude Code

> **Nota (29-jul-2026):** este documento se escribió *antes* de la primera sesión con
> hardware, así que el primer mensaje sugerido de abajo ya no aplica: el firmware compila,
> está flasheado y los dos nodos se hablan por radio. El estado real está en `ESTADO.md`.
> Lo que sigue vigente son las **reglas para la sesión**.

Abre esta carpeta con Claude Code. `CLAUDE.md` se lee solo; pídele que lea también
`ESTADO.md`, que es donde está el plan de la primera sesión con hardware.

---

## Primer mensaje sugerido

> Lee `CLAUDE.md` y `ESTADO.md`. El firmware está completo y con 64 tests en verde en el
> ordenador, pero nunca se ha ejecutado en el aparato. Voy a flashearlo por primera vez.
>
> Empecemos por compilar: `pio run`. Si hay errores, arréglalos de uno en uno y explícame
> cada cambio; el código está auditado contra las fuentes de las librerías, pero nunca ha
> pasado por el compilador de ESP32.
>
> No toques nada de `src/proto`, `src/mesh` ni `src/util` sin volver a ejecutar
> `pio test -e native`: esas capas están verificadas y no quiero romperlas por accidente.

---

## Reglas para la sesión

1. **Los tests son la red de seguridad.** Antes y después de tocar `proto/`, `mesh/` o
   `util/`: `pio test -e native`. Deben seguir siendo 64 en verde. Si un test falla, mirar
   primero si el equivocado es el test — ya pasó dos veces.

2. **Una cosa cada vez.** El orden de pruebas de `ESTADO.md` está pensado para que cada fallo
   se pueda aislar. No saltárselo.

3. **Nada de transmitir a la ligera.** Antena atornillada antes de dar corriente. El firmware
   pide confirmación en pantalla; no quitar esa comprobación "para ir más rápido".

4. **Si algo del hardware no cuadra con `CLAUDE.md`, gana el hardware** — y hay que
   actualizar `CLAUDE.md` en el mismo commit. Ese archivo es la memoria del proyecto: un dato
   erróneo ahí se propaga a todas las sesiones siguientes.

5. **Apuntar lo aprendido.** Cada trampa nueva va a la tabla "Trampas ya pisadas" de
   `CLAUDE.md`. Esa tabla ya ha evitado repetir ocho errores.

---

## Contexto de las decisiones, por si se cuestionan

- **Por qué no es un fork de Meshtastic**: el riesgo principal era implementar mal el
  protocolo, porque falla en silencio — un paquete mal construido sencillamente no se
  descifra y se descarta. Reimplementarlo con fidelidad de byte permite validarlo contra un
  nodo con firmware oficial, que es un criterio de aceptación binario. Un fork habría dado un
  protocolo correcto pero una interfaz imposible de simplificar.

- **Por qué la criptografía está escrita a mano**: mbedTLS existe en el ESP32 pero no en el
  ordenador. Con mbedTLS, los tests probarían una implementación distinta de la que se
  embarca. Escrita a mano, el mismo código corre en los dos sitios y los vectores lo validan
  contra `pycryptodome` y `hashlib`.

- **Por qué el reloj se pasa por parámetro** en `mesh/duty.cpp` y `mesh/router.cpp`: es lo que
  permite probar la ventana rodante de 60 minutos y las ventanas de contención de forma
  determinista, sin esperar una hora.

- **Por qué no hay mensajes predefinidos**: se descartaron a propósito para mantener el
  firmware y la interfaz mínimos. Si se reconsidera, irían como atajos Fn + número, no como
  pantalla propia: la pantalla solo da para cinco líneas de conversación.

- **Por qué el límite de mensaje es en bytes y no en caracteres**: en castellano cada acento
  ocupa 2 bytes y un emoji 4, así que 180 caracteres podrían ser 720 bytes y no caber.

- **Por qué la altura importa más que la potencia**: subir un nodo 300 m multiplica el alcance
  por diez; subir la potencia al máximo legal lo multiplicaría por 1,3. Está calculado en
  `docs/ALCANCE.md`.
