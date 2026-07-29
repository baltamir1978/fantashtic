# Fantashtic — alcance esperado y plan de pruebas de campo

> Cardputer Adv + Cap LoRa-1262 (SX1262), 869.525 MHz, antena de 3 dBi.
> Todos los números están calculados, no copiados de un foro. Las fórmulas están abajo
> para que puedas rehacerlos si cambias de antena o de preset.

---

## 1. Presupuesto de enlace

Sensibilidad térmica del receptor:

```
Sens(dBm) = -174 + 10·log10(BW_Hz) + NF + SNR_min
```

con NF = 6 dB (cifra de ruido típica del SX1262) y el SNR mínimo de cada spreading factor.

| Preset | SF / BW | SNR mín | Sensibilidad | Presupuesto* |
|---|---|---|---|---|
| **LONG_FAST** *(defecto)* | 11 / 250 kHz | −17.5 dB | **−131.5 dBm** | **159.5 dB** |
| LONG_MODERATE | 11 / 125 kHz | −17.5 dB | −134.5 dBm | 162.5 dB |
| MEDIUM_FAST | 9 / 250 kHz | −12.5 dB | −126.5 dBm | 154.5 dB |
| SHORT_FAST | 7 / 250 kHz | −7.5 dB | −121.5 dBm | 149.5 dB |

\* Presupuesto bruto = 22 dBm TX + 3 dBi antena TX + 3 dBi antena RX − sensibilidad.

Cambiar de LONG_FAST a LONG_MODERATE gana **3 dB** — un 40 % más de distancia en teoría —
a costa de multiplicar por 3 el tiempo en el aire. Bajar a SHORT_FAST pierde 10 dB, que es
aproximadamente **dividir el alcance entre 3**.

---

## 2. Alcance realista por escenario

El presupuesto bruto de 159.5 dB daría 2596 km en espacio libre. Esa cifra no significa
nada: dos personas de pie no tienen espacio libre entre ellas.

**A partir de 26 metros** (el "breakpoint" de dos rayos con los dispositivos a 1.5 m del
suelo) la señal deja de decaer como 1/d² y pasa a decaer como 1/d⁴, porque el rebote en el
suelo llega en oposición de fase y cancela parte de la señal directa. Ese es el modelo que
usan las cifras de abajo:

```
Pérdida(dB) = 40·log10(d_m) − 20·log10(h1_m) − 20·log10(h2_m)
```

Y hay un segundo techo, independiente de la potencia: el **horizonte radioeléctrico**.
Con ambos dispositivos a 1.5 m de altura sobre terreno llano son **10.1 km**. Ni con 1 kW
pasarías de ahí, porque la Tierra se curva por el medio.

| Escenario | Cuerpo | Desvanec. | Obstáculos | Margen neto | **Alcance esperado** |
|---|---|---|---|---|---|
| Ciudad densa | 6 dB | 8 dB | 25 dB | 120.5 dB | **0.5 – 1.5 km** |
| Bosque denso | 6 dB | 6 dB | 30 dB | 117.5 dB | **0.5 – 1.3 km** |
| Pueblo / suburbio | 6 dB | 6 dB | 15 dB | 132.5 dB | **1.5 – 3 km** |
| Campo abierto llano | 4 dB | 3 dB | 6 dB | 146.5 dB | **4 – 7 km** (techo: horizonte 10 km) |
| Un nodo en un cerro (300 m) | 3 dB | 3 dB | 6 dB | 147.5 dB | **20 – 70 km** (horizonte 76 km) |

**La altura importa muchísimo más que la potencia.** Subir un nodo 300 m multiplica el
alcance por diez; subir la potencia de 22 a 27 dBm lo multiplicaría por 1.3 y además te
saldrías de lo que da el chip. Si alguna vez montas un repetidor fijo, ponlo alto antes
que ponerlo potente.

### Las dos pérdidas que la gente olvida

| Efecto | Coste | Comentario |
|---|---|---|
| **Antena horizontal contra antena vertical** | **20 – 30 dB** | Es el error más caro y el más fácil de cometer. Un Cardputer tumbado en una mesa con la antena horizontal, hablando con otro en vertical, pierde más señal que atravesando un bosque. **Ambas antenas verticales, siempre.** |
| **En el bolsillo, contra en la mano** | 5 – 10 dB | El cuerpo humano absorbe a 868 MHz. Sacarlo del bolsillo casi duplica el alcance. |

---

## 3. Qué significa esto para vuestro uso

- **Excursión o esquí, en grupo, a la vista o casi**: sobra con muchísimo margen. El
  problema no será el alcance.
- **Grupo repartido por un valle**: 1–3 km es lo normal, con caídas si alguien baja detrás
  de una loma. Aquí es donde el relay de 3 nodos gana su sueldo: un nodo a media ladera
  reconecta a los dos extremos.
- **Ciudad**: 0.5–1.5 km. Suficiente para "estoy en la otra punta del parque", no para
  "estoy en el otro barrio".
- **Bosque denso**: el peor caso, y además es donde más falta hace. La vegetación húmeda
  atenúa muchísimo a 868 MHz. Contad con 1 km escaso.
- **Niño que se separa del grupo**: el escenario que motiva el proyecto. En terreno abierto
  el SOS llegará; en bosque cerrado y a más de 1 km, puede que no. Conviene que esto se
  entienda en casa: **es una ayuda, no una garantía**, y no sustituye a las normas de no
  alejarse.

---

## 4. Plan de pruebas de campo

Ejecutar en la **Fase 2** (radio en crudo, ping-pong) y repetir en la **Fase 11**. No hace
falta el protocolo completo: basta con un contador que se envíe al pulsar una tecla y que
el receptor muestre RSSI, SNR y número de secuencia.

**Registro automático en la microSD.** Como el firmware ya escribe en la tarjeta (ver
`SPEC.md` §7.1), el modo de pruebas debe volcar cada ráfaga a `/fantashtic/campo/AAAA-MM-DD.csv`
con distancia, RSSI, SNR, enviados, recibidos y una nota tecleada. Así no hay que apuntar
nada a mano en el monte y los datos se abren luego en el ordenador.

**Instrumentación mínima que debe dar el firmware de pruebas:**

- Contador de secuencia (para calcular paquetes perdidos)
- RSSI y SNR de cada paquete recibido
- Contador acumulado de enviados / recibidos, con el porcentaje de pérdida
- Un modo "ráfaga": 20 paquetes seguidos con un toque, para medir tasa de pérdida sin
  quedarse quieto media hora

**Criterio de "enlace válido"**: **≥ 80 % de paquetes recibidos** en una ráfaga de 20.
Por debajo de eso el enlace es inutilizable aunque llegue algún paquete suelto.

### Prueba 0 — Referencia en mesa

Los dos nodos a 5 m, antenas verticales. Anota RSSI y SNR. Este es tu valor de referencia:
si un día el enlace de mesa empeora respecto a este número, tienes un problema de hardware
(antena floja, switch RF, conector), no de propagación.

> Valor esperado: RSSI en torno a −40 dBm, SNR entre +8 y +10 dB.
> Si a 5 m ves RSSI peor que −70 dBm, para y revisa el switch de antena (P0 del expansor).

### Prueba 1 — Polarización

A 200 m, en abierto. Ráfaga de 20 con ambas antenas verticales, y otra con una horizontal.
Anota la diferencia de RSSI.

> Sirve para dos cosas: confirmar que la diferencia es real (deberían salir 15–25 dB) y
> convencer a todo el mundo en casa de que la antena va **hacia arriba**.

### Prueba 2 — Alejamiento en campo abierto

Un nodo fijo, el otro se aleja en línea recta. Ráfaga de 20 cada 250 m. Anota distancia
(del GPS del móvil o del propio nodo), RSSI, SNR y % recibido. Sigue hasta bajar del 80 %,
y luego un punto más para ver dónde se cae del todo.

> Referencia: deberías pasar de 3 km sin apuros. Si el enlace se rompe antes de 1.5 km en
> abierto, hay algo mal — sospecha de la antena o de la altura a la que lo llevas.

### Prueba 3 — Bolsillo contra mano

En el punto donde la prueba 2 dio entre 85 % y 95 % (el borde del enlace), repite la ráfaga
con el nodo en el bolsillo del pantalón y luego en la mano en alto.

> Es la prueba más útil de todas para el día a día: te dice cuánto alcance regalas por
> comodidad, y justifica la instrucción "sácalo del bolsillo si no te llega".

### Prueba 4 — Ciudad y bosque

Repite la prueba 2 en los dos entornos difíciles, con incrementos de 100 m en lugar de 250.
En ciudad, prueba también con y sin línea de vista en la misma distancia (una esquina de
edificio en medio) para ver el coste de un obstáculo concreto.

### Prueba 5 — Relay a 3 nodos *(necesita el tercer Cardputer)*

Es la única prueba que **no se puede hacer con dos dispositivos**. Coloca A y C a una
distancia a la que la prueba 2 dio 0 % — sin enlace directo, confirmado. Pon B a mitad de
camino.

> Éxito: A y C se comunican a través de B, con más latencia. En la consola de B debe verse
> que retransmite, que `hop_limit` baja de 3 a 2, y **que no hay bucles** (cada paquete se
> retransmite una sola vez).

### Prueba 6 — Altura

Sube un nodo a lo más alto que tengas a mano (un cerro, una azotea, un mirador) y deja el
otro abajo. Mide hasta dónde llega.

> Es la prueba que más impresiona y la que mejor enseña el principio: la altura vale más
> que la potencia. Con 300 m de desnivel y línea de vista, decenas de kilómetros son
> perfectamente normales.

### Prueba 7 — Autonomía

Un nodo encendido, en RX continuo, pantalla apagándose sola, GPS activado, un mensaje cada
15 minutos. Anota el porcentaje de batería cada hora hasta agotarla.

> Objetivo de la spec: un día entero de uso intermitente. Si no llega, la primera
> optimización es apagar el GPS, y la segunda `startReceiveDutyCycleAuto()` en el SX1262
> (ver `SPEC.md` §8).

---

## 5. Plantilla de registro

Una tabla por prueba. Rellenar sobre el terreno, no de memoria.

| Prueba | Fecha | Dist. | Entorno | Antenas | Enviados | Recibidos | % | RSSI | SNR | Notas |
|---|---|---|---|---|---|---|---|---|---|---|
| 0 | | 5 m | mesa | V-V | 20 | | | | | referencia |
| 1a | | 200 m | abierto | V-V | 20 | | | | | |
| 1b | | 200 m | abierto | V-H | 20 | | | | | polarización cruzada |
| 2 | | 250 m | abierto | V-V | 20 | | | | | |
| 2 | | 500 m | abierto | V-V | 20 | | | | | |
| … | | | | | | | | | | |

Guardar los resultados en el repositorio (`docs/campo/`). Sirven de línea base: cuando
dentro de un año algo vaya mal, tendrás con qué comparar.

---

## 6. Fórmulas, para rehacer los cálculos

```python
# Sensibilidad del receptor
sens = -174 + 10*log10(BW_Hz) + NF + SNR_min          # NF = 6 dB para el SX1262

# Presupuesto de enlace
presupuesto = P_tx + G_tx + G_rx - sens                # 22 + 3 + 3 - sens

# Espacio libre (solo válido con línea de vista despejada y zona de Fresnel libre)
FSPL = 32.45 + 20*log10(f_MHz) + 20*log10(d_km)        # 91.24 + 20log10(d_km) a 869.5 MHz

# Distancia de breakpoint: donde empieza el régimen de dos rayos
d_bp = 4*h1*h2/lambda                                  # lambda = 0.345 m a 869.525 MHz

# Dos rayos con reflexión en el suelo (el modelo realista más allá del breakpoint)
PL = 40*log10(d_m) - 20*log10(h1_m) - 20*log10(h2_m)

# Horizonte radioeléctrico (techo absoluto, independiente de la potencia)
d_km = 4.12 * (sqrt(h1_m) + sqrt(h2_m))
```

Valores de SNR mínimo por spreading factor (hoja de datos del SX1262):
SF7 −7.5 · SF8 −10 · SF9 −12.5 · SF10 −15 · SF11 −17.5 · SF12 −20 dB.

---

## 7. Aviso

Estas cifras son estimaciones de ingeniería, no promesas. La propagación real depende de
la humedad, la vegetación, el terreno, los edificios y de dónde lleves el aparato. Sirven
para saber **qué esperar** y para detectar cuándo algo va mal, no para garantizar cobertura.

Para el uso que le vais a dar —y muy en particular para la función de SOS con niños— lo
importante es interiorizar el caso peor, no el mejor: **en bosque denso, cuenta con menos
de un kilómetro.**
