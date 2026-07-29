# Fantashtic — vectores de test del protocolo

> Verdad de referencia para la Fase 3. Si tu implementación reproduce estos bytes, el
> protocolo es correcto. Si no, sabes exactamente en qué paso se rompe.
>
> Generados por `tools/gen_test_vectors.py`, listos para C en `test_vectors.h`.

---

## Por qué esto importa

La Fase 3 es la que decide si el proyecto funciona, y su modo de fallo es horrible: si el
nonce, el hash de canal o la codificación del protobuf tienen un solo bit mal, el nodo
Meshtastic de referencia **no muestra nada**. Ni un error, ni basura en pantalla. Nada. Y
"nada" no te dice cuál de las cinco capas falló.

Con estos vectores el fallo se localiza en segundos: comparas tu salida contra el byte
esperado en cada etapa (Data → nonce → cifrado → cabecera → aire) y la primera que difiere
es la culpable.

---

## Comprobaciones contra valores publicados de Meshtastic

Estos cuatro son de calibración: no dependen de nuestras claves, así que si fallan es que
la primitiva está mal implementada, no que hayamos elegido mal un parámetro.

| Comprobación | Esperado | Estado |
|---|---|---|
| `xorHash("LongFast")` | `0x0A` | ✅ |
| `xorHash(PSK por defecto)` | `0x02` | ✅ |
| Hash de canal de `LongFast` | **`0x08`** | ✅ |
| `djb2("LongFast")` | `130429955` | ✅ |
| `lastByte(0x4F91E200)` | `0xFF` | ✅ el `0x00` se sustituye |

Y la derivación de frecuencia, reproducida en cuatro regiones:

| Región / preset | Slots | Índice | Frecuencia |
|---|---|---|---|
| **EU_868 / LONG_FAST** | 1 | 0 | **869.5250 MHz** |
| EU_868 / LONG_MODERATE | 2 | 1 | 869.5875 MHz |
| US / LONG_FAST | 104 | 19 | 906.8750 MHz |
| EU_433 / LONG_FAST | 4 | 3 | 433.8750 MHz |

Las tres marcadas coinciden con los valores que publica Meshtastic. La de LONG_MODERATE es
la que justifica implementar djb2 desde el principio: con 2 slots, la frecuencia **sí**
depende del nombre del canal.

---

## Canal de ejemplo

```
Frase          montana-verde-2026
PSK            51 87 DA 08 A4 4D FF 35 7C 3B 06 E3 08 4E DC 4B
               97 8D DD 85 CD 12 75 E5 4F AC 85 32 BC 89 4A FE
Canal          FT-OS6TN
Hash de canal  0x83
```

PBKDF2-HMAC-SHA256, salt `fantashtic-v1`, 20 000 iteraciones, 32 bytes.

> Esta frase es de ejemplo y está publicada aquí. **No la uséis como clave real.**

> La sal `fantashtic-v1` es la de **por defecto**, la que reproduce este vector y
> los tests de `test_util`. Un despliegue real la sustituye por una propia en
> `include/secrets.h`, que no se commitea (ver `include/secrets.example.h`). Si
> has puesto sal propia, la PSK de arriba **no** te va a salir: el vector sigue
> siendo válido, pero para la sal por defecto.

---

## Vector 1 — texto broadcast

Mensaje `"Bajamos en 20 min"` de PAPA (`0x7A3B12C4`), id `0x2E4F1A07`, hop limit 3.

```
Data (claro)   08 01 12 11 42 61 6A 61 6D 6F 73 20 65 6E 20 32 30 20 6D 69 6E
nonce          07 1A 4F 2E 00 00 00 00 C4 12 3B 7A 00 00 00 00
cabecera 16B   FF FF FF FF C4 12 3B 7A 07 1A 4F 2E 63 83 00 00
cifrado        0F B3 EF 84 6E F5 39 0C BB DF B2 AB 74 37 81 67 34 3D DC CD D0
EN EL AIRE     FF FF FF FF C4 12 3B 7A 07 1A 4F 2E 63 83 00 00
               0F B3 EF 84 6E F5 39 0C BB DF B2 AB 74 37 81 67 34 3D DC CD D0
```

Desglose de la cabecera, byte a byte:

| Bytes | Valor | Significado |
|---|---|---|
| `FF FF FF FF` | to | broadcast |
| `C4 12 3B 7A` | from | `0x7A3B12C4` en little-endian |
| `07 1A 4F 2E` | id | `0x2E4F1A07` en little-endian |
| `63` | flags | hop_limit 3 (`0x03`) + hop_start 3 (`3<<5 = 0x60`) |
| `83` | canal | hash de `FT-OS6TN` con nuestra PSK |
| `00 00` | next_hop, relay_node | sin asignar |

Y del `Data` protobuf: `08 01` es portnum = 1 (`TEXT_MESSAGE_APP`), `12 11` es el campo 2
con 17 bytes de longitud, y el resto es el texto en UTF-8 crudo.

**Fíjate en el nonce**: `07 1A 4F 2E` son los primeros 4 bytes del id, seguidos de 4 ceros
(el id se escribe como `uint64` little-endian), luego el `from`, luego 4 ceros que son el
contador CTR. Todo sale de la cabecera sin cifrar — por eso cualquier receptor con la clave
puede descifrar sin negociar nada.

---

## Vector 2 — interoperabilidad con un nodo stock

Este es **el vector que valida la Fase 3**. Canal `LongFast` con la PSK por defecto de
Meshtastic, mensaje `"test"`, id `0x00000001`.

```
Data (claro)   08 01 12 04 74 65 73 74
nonce          01 00 00 00 00 00 00 00 C4 12 3B 7A 00 00 00 00
cabecera 16B   FF FF FF FF C4 12 3B 7A 01 00 00 00 63 08 00 00
cifrado        15 79 60 8F 50 91 86 D5
EN EL AIRE     FF FF FF FF C4 12 3B 7A 01 00 00 00 63 08 00 00 15 79 60 8F 50 91 86 D5
```

24 bytes en total. Si tu firmware genera exactamente esta cadena, **cualquier** nodo
Meshtastic con la configuración por defecto en EU_868 mostrará "test" en su pantalla.

Nótese que aquí la clave es de **16 bytes** → AES-128-CTR, no 256. Es lo que obliga a que
la función de cifrado y la de hash acepten ambas longitudes.

---

## Vector 3 — UTF-8 multibyte

`"Estoy aqui arriba ñáéí 😀"` — 24 caracteres, **31 bytes**. Ahí está la trampa que motiva
que el límite de mensaje se cuente en bytes y no en caracteres.

```
Data (claro)   08 01 12 1F 45 73 74 6F 79 20 61 71 75 69 20 61 72 72 69 62 61 20
               C3 B1 C3 A1 C3 A9 C3 AD 20 F0 9F 98 80
cabecera 16B   FF FF FF FF 00 E2 91 4F CD AB 00 00 63 83 00 00
EN EL AIRE     51 bytes
```

El `1F` de longitud es 31, no 24. Y el emisor es LUCI (`0x4F91E200`), cuyo NodeNum **acaba
en `0x00`** — el caso límite de `lastByte()`, que debe devolver `0xFF`.

---

## Vector 4 — el mismo paquete, ya retransmitido

Idéntico al vector 1 después de pasar por LUCI:

```
cabecera 16B   FF FF FF FF C4 12 3B 7A 07 1A 4F 2E 62 83 00 FF
                                                    ▲        ▲
                                          flags 0x62│        │relay_node = lastByte(LUCI)
```

Solo cambian **dos bytes**: `flags` de `0x63` a `0x62` (hop_limit 3 → 2, hop_start intacto)
y `relay_node` de `0x00` a `0xFF`.

El payload cifrado es **byte a byte idéntico**. Eso no es casualidad: relayar no requiere
descifrar, y de hecho el nodo que retransmite puede no tener la clave. Si tu implementación
del router modifica el payload al relayar, hay un error de diseño.

---

## Cómo usarlos

`test_vectors.h` trae los cuatro casos como arrays de C, listos para el entorno `native`:

```c
#include "test_vectors.h"

for (size_t i = 0; i < FT_NUM_VECTORES; i++) {
    const ft_vector_t* v = &FT_VECTORES[i];

    TEST_ASSERT_EQUAL_UINT8(v->ch_hash, channel_hash(v->ch_name, v->key, v->key_len));

    uint8_t nonce[16];
    make_nonce(nonce, v->id, v->from);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(v->nonce, nonce, 16);

    uint8_t out[256];
    size_t n = build_packet(out, v);
    TEST_ASSERT_EQUAL_size_t(v->air_len, n);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(v->air, out, n);      // ida

    uint8_t back[256];
    parse_packet(back, v->air, v->air_len, v->key, v->key_len);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(v->plain, back, v->plain_len);   // vuelta
}
```

Para regenerarlos —si cambias la frase de ejemplo o añades casos:

```bash
pip install pycryptodome
python3 tools/gen_test_vectors.py            # informe legible
python3 tools/gen_test_vectors.py --header > test/test_vectors.h
```

El script se comprueba a sí mismo: verifica que AES-CTR es involutivo y contrasta las
cuatro constantes públicas de Meshtastic. Si alguna falla, aborta.

---

## Orden de depuración recomendado

Cuando la Fase 3 no funcione —y es probable que no funcione a la primera— compara en este
orden. La primera etapa que difiera es la culpable:

1. **`Data` en claro.** Si falla aquí, el problema es la codificación protobuf. Sospecha de
   los campos `fixed32` codificados como varint.
2. **Nonce.** Si falla, revisa el orden de bytes: el id va como `uint64` little-endian, no
   como `uint32`.
3. **Hash de canal.** Si falla, casi seguro estás pasando la longitud de clave equivocada.
4. **Cabecera.** Si falla, mira los flags: `hop_start` va desplazado 5 bits.
5. **Cifrado.** Si todo lo anterior cuadra y esto no, el problema es el tamaño del contador
   CTR: deben ser **4 bytes**, no 16.
