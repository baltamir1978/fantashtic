#!/usr/bin/env python3
"""Fantashtic - generador de vectores de test del protocolo Meshtastic.

Reproduce byte a byte lo que hace el firmware de Meshtastic (master) para que
los tests de la Fase 3 tengan verdad de referencia. Fuentes citadas por funcion.

  pip install pycryptodome
  python3 gen_test_vectors.py            -> imprime el informe
  python3 gen_test_vectors.py --header   -> emite test_vectors.h
"""
import hashlib, hmac, base64, struct, sys
from Crypto.Cipher import AES
from Crypto.Util import Counter

# --- constantes del protocolo -------------------------------------------------
BROADCAST      = 0xFFFFFFFF
DEFAULT_PSK16  = bytes.fromhex("d4f1bb3a20290759f0bcffabcf4e6901")  # Channels.h
PORTNUM_TEXT   = 1                                                  # portnums.proto
SALT           = b"fantashtic-v1"
ITERS          = 20000

# --- primitivas ---------------------------------------------------------------
def xor_hash(b):                      # Channels.cpp::xorHash
    c = 0
    for x in b: c ^= x
    return c

def channel_hash(name, key):          # Channels.cpp::generateHash
    return xor_hash(name.encode()) ^ xor_hash(key)

def djb2(s):                          # RadioInterface.cpp::hash
    h = 5381
    for c in s.encode(): h = (h * 33 + c) & 0xFFFFFFFF
    return h

def freq(start, end, bw_khz, name, ch_num=0):   # RadioInterface.cpp::applyModemConfig
    n = int((end - start) / (bw_khz / 1000.0))
    idx = (ch_num - 1) if ch_num else (djb2(name) % n)
    return n, idx, start + (bw_khz / 2000.0) + idx * (bw_khz / 1000.0)

def make_nonce(pkt_id, from_node):    # CryptoEngine.cpp::initNonce
    return struct.pack("<Q", pkt_id) + struct.pack("<I", from_node) + b"\x00" * 4

def aes_ctr(key, nonce, data):        # CryptoEngine.cpp - setCounterSize(4)
    ctr = Counter.new(32, prefix=nonce[:12], initial_value=int.from_bytes(nonce[12:], "big"))
    return AES.new(key, AES.MODE_CTR, counter=ctr).encrypt(data)

def flags(hop_limit, want_ack=False, via_mqtt=False, hop_start=None):
    hs = hop_limit if hop_start is None else hop_start
    return (hop_limit & 0x07) | (0x08 if want_ack else 0) | (0x10 if via_mqtt else 0) | ((hs & 0x07) << 5)

def header(to, frm, pid, fl, chan, next_hop=0, relay=0):
    return struct.pack("<III", to, frm, pid) + bytes([fl, chan, next_hop, relay])

def last_byte(num):                   # NodeDB.h::getLastByteOfNodeNum
    b = num & 0xFF
    return b if b else 0xFF

def data_text(msg):                   # Data protobuf: portnum=1 (varint), payload=2 (bytes)
    p = msg.encode("utf-8")
    return bytes([0x08, PORTNUM_TEXT, 0x12, len(p)]) + p

def derive(passphrase):
    psk = hashlib.pbkdf2_hmac("sha256", passphrase.encode(), SALT, ITERS, 32)
    name = "FT-" + base64.b32encode(hashlib.sha256(passphrase.encode()).digest()).decode()[:5]
    return psk, name

def hx(b, sep=" "): return sep.join(f"{x:02X}" for x in b)
def carr(b): return ", ".join(f"0x{x:02X}" for x in b)

# --- casos --------------------------------------------------------------------
PASS   = "montana-verde-2026"
PSK, CHNAME = derive(PASS)
PAPA, LUCI  = 0x7A3B12C4, 0x4F91E200          # nota: LUCI acaba en 0x00 -> lastByte 0xFF

CASES = []
def case(n, **kw): CASES.append((n, kw)); return kw

C1 = case("1 - texto broadcast de PAPA (canal Fantashtic)",
    key=PSK, name=CHNAME, to=BROADCAST, frm=PAPA, pid=0x2E4F1A07, hop=3, msg="Bajamos en 20 min")
C2 = case("2 - interoperabilidad: canal LongFast con la PSK por defecto de Meshtastic",
    key=DEFAULT_PSK16, name="LongFast", to=BROADCAST, frm=PAPA, pid=0x00000001, hop=3, msg="test")
C3 = case("3 - acentos y emoji (UTF-8 multibyte)",
    key=PSK, name=CHNAME, to=BROADCAST, frm=LUCI, pid=0x0000ABCD, hop=3, msg="Estoy aqui arriba ñáéí \U0001F600")
C4 = case("4 - mismo paquete que el 1, ya retransmitido por LUCI",
    key=PSK, name=CHNAME, to=BROADCAST, frm=PAPA, pid=0x2E4F1A07, hop=2, hop_start=3,
    relay=last_byte(LUCI), msg="Bajamos en 20 min")

def build(c):
    ch  = channel_hash(c["name"], c["key"])
    fl  = flags(c["hop"], hop_start=c.get("hop_start", c["hop"]))
    hdr = header(c["to"], c["frm"], c["pid"], fl, ch, 0, c.get("relay", 0))
    pt  = data_text(c["msg"])
    nc  = make_nonce(c["pid"], c["frm"])
    ct  = aes_ctr(c["key"], nc, pt)
    assert aes_ctr(c["key"], nc, ct) == pt, "CTR no es involutivo"
    return dict(ch=ch, fl=fl, hdr=hdr, pt=pt, nonce=nc, ct=ct, air=hdr + ct)

def report():
    print("=" * 78); print("FANTASHTIC - VECTORES DE TEST DEL PROTOCOLO"); print("=" * 78)
    print(f"\nFrase          : {PASS}")
    print(f"PSK (PBKDF2)   : {hx(PSK)}")
    print(f"Canal          : {CHNAME}")
    print(f"Hash de canal  : 0x{channel_hash(CHNAME, PSK):02X}")
    print("\n--- comprobaciones contra valores publicados de Meshtastic ---")
    a, b = xor_hash(b"LongFast"), xor_hash(DEFAULT_PSK16)
    print(f"xorHash('LongFast')      = 0x{a:02X}")
    print(f"xorHash(PSK por defecto) = 0x{b:02X}")
    print(f"hash de canal LongFast   = 0x{a ^ b:02X}   (esperado 0x08)  {'OK' if a^b==8 else 'FALLO'}")
    print(f"djb2('LongFast')         = {djb2('LongFast')}   (esperado 130429955)  {'OK' if djb2('LongFast')==130429955 else 'FALLO'}")
    for reg,(s,e,bw,exp) in {"EU_868 / LONG_FAST":(869.4,869.65,250,869.525),
                             "EU_868 / LONG_MODERATE":(869.4,869.65,125,None),
                             "US / LONG_FAST":(902.0,928.0,250,906.875),
                             "EU_433 / LONG_FAST":(433.0,434.0,250,433.875)}.items():
        n,i,f = freq(s,e,bw,"LongFast")
        ok = "" if exp is None else ("  OK" if abs(f-exp)<1e-6 else "  FALLO")
        print(f"{reg:24s} {n:3d} slots, idx {i:3d} -> {f:.4f} MHz{ok}")
    print(f"lastByte(0x{LUCI:08X})      = 0x{last_byte(LUCI):02X}   (el 0x00 se sustituye por 0xFF)")
    for name, c in CASES:
        v = build(c)
        print("\n" + "-" * 78); print(name); print("-" * 78)
        print(f"  mensaje      : {c['msg']!r}  ({len(c['msg'].encode())} bytes UTF-8)")
        print(f"  clave        : {hx(c['key'])}")
        print(f"  canal        : {c['name']}  ->  hash 0x{v['ch']:02X}")
        print(f"  flags        : 0x{v['fl']:02X}  (hop_limit {c['hop']}, hop_start {c.get('hop_start',c['hop'])})")
        print(f"  Data (claro) : {hx(v['pt'])}")
        print(f"  nonce        : {hx(v['nonce'])}")
        print(f"  cabecera 16B : {hx(v['hdr'])}")
        print(f"  cifrado      : {hx(v['ct'])}")
        print(f"  EN EL AIRE   : {hx(v['air'])}")
        print(f"  total        : {len(v['air'])} bytes  (limite 255)")

def emit_header():
    L = ["#pragma once", "// Generado por tools/gen_test_vectors.py - NO EDITAR A MANO",
         "#include <stdint.h>", "#include <stddef.h>", "",
         "typedef struct {", "  const char*    nombre;", "  const uint8_t* key;   size_t key_len;",
         "  const char*    ch_name;  uint8_t ch_hash;",
         "  uint32_t to, from, id;   uint8_t flags;",
         "  const uint8_t* plain; size_t plain_len;",
         "  const uint8_t* nonce;", "  const uint8_t* air;   size_t air_len;", "} ft_vector_t;", ""]
    L.append(f"static const uint8_t FT_PSK[32]      = {{{carr(PSK)}}};")
    L.append(f"static const uint8_t FT_PSK_DEF[16]  = {{{carr(DEFAULT_PSK16)}}};")
    L.append(f'#define FT_CANAL "{CHNAME}"')
    L.append(f'#define FT_FRASE "{PASS}"'); L.append("")
    for i,(name,c) in enumerate(CASES,1):
        v = build(c)
        L += [f"static const uint8_t v{i}_plain[] = {{{carr(v['pt'])}}};",
              f"static const uint8_t v{i}_nonce[] = {{{carr(v['nonce'])}}};",
              f"static const uint8_t v{i}_air[]   = {{{carr(v['air'])}}};", ""]
    L.append("static const ft_vector_t FT_VECTORES[] = {")
    for i,(name,c) in enumerate(CASES,1):
        v = build(c); dflt = c["key"] == DEFAULT_PSK16
        k = "FT_PSK_DEF, 16" if dflt else "FT_PSK, 32"
        L.append(f'  {{"{name}", {k}, "{c["name"]}", 0x{v["ch"]:02X}, '
                 f'0x{c["to"]:08X}u, 0x{c["frm"]:08X}u, 0x{c["pid"]:08X}u, 0x{v["fl"]:02X}, '
                 f'v{i}_plain, sizeof(v{i}_plain), v{i}_nonce, v{i}_air, sizeof(v{i}_air)}},')
    L += ["};", f"#define FT_NUM_VECTORES {len(CASES)}", ""]
    return "\n".join(L)

if __name__ == "__main__":
    if "--header" in sys.argv: print(emit_header())
    else: report()
