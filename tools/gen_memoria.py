# -*- coding: utf-8 -*-
from docx import Document
from docx.shared import Pt, Cm, RGBColor
from docx.enum.text import WD_ALIGN_PARAGRAPH, WD_BREAK
from docx.enum.section import WD_SECTION
from docx.oxml.ns import qn
from docx.oxml import OxmlElement

AZUL = RGBColor(0x1F, 0x3B, 0x57)
GRIS = RGBColor(0x59, 0x59, 0x59)

doc = Document()

# --- estilos base ---
n = doc.styles['Normal']
n.font.name = 'Calibri'; n.font.size = Pt(10.5)
n.paragraph_format.space_after = Pt(6)
n.paragraph_format.line_spacing = 1.15
n.element.rPr.rFonts.set(qn('w:eastAsia'), 'Calibri')

for lvl, sz in ((1, 16), (2, 13), (3, 11.5)):
    s = doc.styles['Heading %d' % lvl]
    s.font.name = 'Calibri'; s.font.size = Pt(sz); s.font.bold = True
    s.font.color.rgb = AZUL
    s.paragraph_format.space_before = Pt(16 if lvl == 1 else 10)
    s.paragraph_format.space_after = Pt(5)

for sec in doc.sections:
    sec.top_margin = Cm(2.5); sec.bottom_margin = Cm(2.2)
    sec.left_margin = Cm(2.5); sec.right_margin = Cm(2.5)

def field(par, instr):
    r = par.add_run()
    f1 = OxmlElement('w:fldChar'); f1.set(qn('w:fldCharType'), 'begin')
    it = OxmlElement('w:instrText'); it.set(qn('xml:space'), 'preserve'); it.text = instr
    f2 = OxmlElement('w:fldChar'); f2.set(qn('w:fldCharType'), 'separate')
    t  = OxmlElement('w:t'); t.text = "  "
    f3 = OxmlElement('w:fldChar'); f3.set(qn('w:fldCharType'), 'end')
    for e in (f1, it, f2, t, f3): r._r.append(e)

def p(txt='', style=None, bold=False, size=None, align=None, color=None, space_after=None):
    par = doc.add_paragraph(style=style)
    if txt:
        r = par.add_run(txt); r.bold = bold
        if size: r.font.size = Pt(size)
        if color: r.font.color.rgb = color
    if align is not None: par.alignment = align
    if space_after is not None: par.paragraph_format.space_after = Pt(space_after)
    return par

def h(txt, lvl=1): return doc.add_heading(txt, level=lvl)
def bullet(txt): return doc.add_paragraph(txt, style='List Bullet')

def tabla(cabeceras, filas, anchos=None):
    t = doc.add_table(rows=1, cols=len(cabeceras))
    t.style = 'Table Grid'
    hdr = t.rows[0].cells
    for i, c in enumerate(cabeceras):
        hdr[i].text = ''
        run = hdr[i].paragraphs[0].add_run(c); run.bold = True; run.font.size = Pt(9.5)
        sh = OxmlElement('w:shd'); sh.set(qn('w:val'), 'clear'); sh.set(qn('w:fill'), 'DCE3EB')
        hdr[i]._tc.get_or_add_tcPr().append(sh)
    for fila in filas:
        cs = t.add_row().cells
        for i, v in enumerate(fila):
            cs[i].text = ''
            run = cs[i].paragraphs[0].add_run(str(v)); run.font.size = Pt(9.5)
    if anchos:
        for r_ in t.rows:
            for i, w in enumerate(anchos): r_.cells[i].width = Cm(w)
    doc.add_paragraph().paragraph_format.space_after = Pt(4)
    return t

# ============================ PORTADA ============================
for _ in range(5): doc.add_paragraph()
p('FANTASHTIC', bold=True, size=34, align=WD_ALIGN_PARAGRAPH.CENTER, color=AZUL, space_after=2)
p('Memoria técnica del proyecto', size=15, align=WD_ALIGN_PARAGRAPH.CENTER, color=GRIS, space_after=30)
p('Sistema de mensajería LoRa privada para grupos reducidos,',
  size=12, align=WD_ALIGN_PARAGRAPH.CENTER, space_after=0)
p('sin infraestructura de red', size=12, align=WD_ALIGN_PARAGRAPH.CENTER, space_after=60)
for _ in range(6): doc.add_paragraph()
p('Autor:  Bruno Altamirano', align=WD_ALIGN_PARAGRAPH.CENTER, space_after=2)
p('Versión 1.0  ·  29 de julio de 2026', align=WD_ALIGN_PARAGRAPH.CENTER, color=GRIS, space_after=2)
p('Estado: operativo en dos unidades; pendientes las pruebas de campo',
  align=WD_ALIGN_PARAGRAPH.CENTER, color=GRIS)
doc.add_paragraph().add_run().add_break(WD_BREAK.PAGE)

# ============================ INDICE ============================
h('Índice', 1)
par = doc.add_paragraph()
field(par, r'TOC \o "1-2" \h \z \u')
p('(Para rellenar el índice en Word: pulsa sobre él y elige «Actualizar campo».)',
  size=9, color=GRIS)
doc.add_paragraph().add_run().add_break(WD_BREAK.PAGE)

# ============================ 1 RESUMEN ============================
h('1. Resumen ejecutivo', 1)
p('Fantashtic es un firmware propio para el conjunto M5Stack Cardputer Adv y Cap LoRa-1262 '
  'que permite a un grupo reducido de tres o cuatro personas comunicarse por mensajes de texto '
  'mediante radio LoRa en la banda de 868 MHz, sin teléfono móvil, sin cobertura celular y sin '
  'ninguna infraestructura previa. Los dispositivos se retransmiten los mensajes entre sí, de '
  'modo que dos extremos sin enlace directo pueden comunicarse a través de un tercero situado '
  'en medio.')
p('El sistema está pensado para uso familiar en entornos sin cobertura —excursiones, montaña, '
  'estaciones de esquí, acampadas— y una parte de sus usuarios son menores. Esa circunstancia '
  'gobierna la mayoría de las decisiones de diseño: la interfaz es deliberadamente mínima, existe '
  'una función de emergencia accesible desde cualquier pantalla, y no hay forma sencilla de dejar '
  'un aparato incomunicado por error.')
p('Tras evaluar cuatro alternativas de implementación, se ha adoptado una solución híbrida: '
  'firmware escrito desde cero, pero reimplementando fielmente la capa de aire del proyecto '
  'Meshtastic. Esta decisión combina un código pequeño y comprensible con la posibilidad de '
  'validar el protocolo contra un dispositivo comercial que ejecute el firmware original, lo que '
  'reduce de forma sustancial el riesgo técnico de la fase más crítica.')
p('La inversión estimada para completar la red de cuatro nodos es de aproximadamente 96 USD '
  'adicionales sobre el equipamiento ya disponible.')

# ============================ 2 OBJETO ============================
h('2. Objeto y alcance', 1)
h('2.1. Objeto', 2)
p('El objeto de este proyecto es el diseño y desarrollo de un firmware que convierta un conjunto '
  'de dispositivos M5Stack Cardputer Adv equipados con módulo de radio LoRa en una red de '
  'mensajería privada, autónoma y cifrada, operable íntegramente desde el propio aparato.')
h('2.2. Alcance', 2)
p('Quedan incluidos en el alcance:', bold=True)
for t in ('Mensajería de texto entre 3 y 4 nodos, en un único canal privado cifrado.',
          'Retransmisión automática de mensajes entre nodos (encaminamiento por inundación).',
          'Función de emergencia con transmisión de posición geográfica.',
          'Localización relativa entre nodos mediante GPS.',
          'Registro persistente del historial de conversación en tarjeta microSD.',
          'Configuración completa desde el propio dispositivo, sin herramientas externas.'):
    bullet(t)
p('Quedan expresamente excluidos:', bold=True)
for t in ('Conectividad Bluetooth y aplicación para teléfono móvil.',
          'Conexión a Internet, pasarelas MQTT o integración con redes públicas.',
          'Transmisión de voz, imagen o ficheros.',
          'Canales múltiples y mensajería directa con criptografía de clave pública.',
          'Homologación o certificación del conjunto para su comercialización.'):
    bullet(t)

# ============================ 3 ANTECEDENTES ============================
h('3. Antecedentes y necesidad', 1)
p('La necesidad surge de una situación cotidiana: un grupo familiar que se desplaza a entornos '
  'sin cobertura de telefonía móvil y que necesita mantener contacto entre sus miembros, en '
  'particular con los menores del grupo, que pueden separarse momentáneamente.')
p('Las soluciones habituales presentan limitaciones conocidas. El teléfono móvil no funciona sin '
  'cobertura. Los walkie-talkies de uso libre exigen escuchar activamente y tienen un alcance '
  'reducido en terreno accidentado. Los comunicadores por satélite resuelven el problema, pero '
  'con un coste de adquisición y una cuota de suscripción difíciles de justificar para un uso '
  'ocasional.')
p('El punto de partida es favorable: se dispone ya de dos dispositivos Cardputer Adv con sus '
  'respectivos módulos LoRa de 868 MHz, verificados previamente con los firmwares Meshtastic y '
  'MeshCore. Ambos funcionan correctamente, lo que confirma que el hardware es apto y que la '
  'cuestión pendiente es exclusivamente de software.')

# ============================ 4 ALTERNATIVAS ============================
h('4. Análisis de alternativas', 1)
h('4.1. Alternativas a nivel de sistema', 2)
p('Antes de decidir la implementación se evaluaron las tecnologías de comunicación disponibles '
  'para el escenario descrito.')
tabla(['Tecnología', 'Ventajas', 'Inconvenientes', 'Valoración'],
      [['Telefonía móvil', 'Ya disponible; sin coste adicional',
        'Inservible sin cobertura, que es precisamente el caso de uso', 'Descartada'],
       ['PMR446 (walkie-talkie)', 'Uso libre; inmediatez de la voz',
        'Requiere escucha activa; sin cifrado; alcance corto en terreno accidentado', 'Descartada'],
       ['Comunicador por satélite', 'Cobertura global; fiabilidad alta',
        'Coste elevado por unidad más cuota mensual; desproporcionado para uso ocasional', 'Descartada'],
       ['LoRa 868 MHz', 'Alcance kilométrico; consumo bajo; sin cuotas; cifrado propio; hardware ya disponible',
        'Solo texto; latencia de segundos; tasa de datos muy baja', 'ADOPTADA']],
      [3.2, 4.2, 5.6, 2.4])
p('La tecnología LoRa es la única que satisface simultáneamente las tres condiciones del problema: '
  'funcionar sin infraestructura, alcanzar distancias kilométricas y no incurrir en costes '
  'recurrentes.')

h('4.2. Alternativas de implementación del firmware', 2)
p('Confirmada la tecnología, se evaluaron cuatro vías de implementación.')
tabla(['Alternativa', 'Descripción', 'Ventajas', 'Inconvenientes'],
      [['A. Firmware Meshtastic sin modificar',
        'Utilizar el firmware oficial con un canal privado configurado',
        'Coste de desarrollo nulo; red probada',
        'Interfaz compleja y en inglés; pensada para uso con móvil; sin función de emergencia adaptada; imposible de simplificar'],
       ['B. Bifurcación de Meshtastic',
        'Partir del repositorio oficial y modificarlo',
        'Se heredan pila de radio, encaminamiento y cifrado ya probados',
        'Base de código muy extensa; hay que desactivar numerosos subsistemas; el mantenimiento de la bifurcación es costoso'],
       ['C. Desarrollo íntegro desde cero',
        'Protocolo propio sobre la biblioteca RadioLib',
        'Control total; código mínimo; protocolo no identificable',
        'Toda la lógica de encaminamiento, cifrado y ciclo de trabajo hay que escribirla y depurarla sin referencia externa'],
       ['D. Híbrido: protocolo Meshtastic, firmware propio',
        'Reimplementar solo la capa de aire; interfaz y lógica propias',
        'Código pequeño; interfaz a medida; y sobre todo: se puede validar contra un nodo comercial',
        'Exige reproducir el protocolo con exactitud de byte; hay que documentarlo leyendo el código fuente ajeno']],
      [3.4, 3.8, 4.0, 4.2])

h('4.3. Solución adoptada', 2)
p('Se adopta la alternativa D.', bold=True)
p('El argumento decisivo no es el ahorro de código, sino la verificabilidad. El principal riesgo '
  'técnico del proyecto reside en la implementación del protocolo: si el cálculo del vector de '
  'inicialización criptográfico, la función resumen del canal o la codificación de los mensajes '
  'contienen el más mínimo error, el sistema no funciona y el fallo es opaco, porque un paquete '
  'mal construido sencillamente no se descifra y se descarta en silencio.')
p('Al mantener compatibilidad con el protocolo de Meshtastic, uno de los dispositivos puede '
  'conservar el firmware original y actuar como testigo independiente: si ese dispositivo muestra '
  'en pantalla los mensajes generados por el firmware propio, la implementación es correcta. Se '
  'sustituye así una depuración a ciegas por un criterio de aceptación binario e inequívoco.')
p('Como beneficio secundario, cualquier nodo Meshtastic ajeno que se encuentre en las '
  'proximidades puede retransmitir el tráfico del grupo, ampliando el alcance sin coste ni '
  'configuración.')
doc.add_paragraph().add_run().add_break(WD_BREAK.PAGE)

# ============================ 5 SOLUCION ============================
h('5. Descripción de la solución', 1)
h('5.1. Arquitectura general', 2)
p('El firmware se organiza en siete capas con responsabilidades separadas, de la más próxima al '
  'hardware a la más próxima al usuario: abstracción de hardware, radio, protocolo, malla, '
  'aplicación, interfaz y almacenamiento. La regla de dependencia es estricta: la capa de interfaz '
  'nunca accede directamente a la radio, y la capa de protocolo no depende de ninguna biblioteca '
  'de Arduino, lo que permite compilarla y someterla a pruebas unitarias en un ordenador '
  'convencional sin necesidad de hardware.')

h('5.2. Hardware', 2)
tabla(['Elemento', 'Componente', 'Características'],
      [['Microcontrolador', 'ESP32-S3FN8 (módulo Stamp-S3A)', '240 MHz, 8 MB de memoria flash, sin PSRAM'],
       ['Pantalla', 'ST7789V2', '1,14 pulgadas, 240 × 135 píxeles'],
       ['Teclado', 'Matriz 4 × 14 con explorador TCA8418', '56 teclas, interfaz I²C'],
       ['Radio', 'Semtech SX1262 (Cap LoRa-1262)', '868–923 MHz, +22 dBm, sensibilidad −147 dBm, antena RP-SMA de 3 dBi'],
       ['Posicionamiento', 'ATGM336H-6N (AT6668)', 'GPS, GLONASS, Galileo y BeiDou; precisión < 1,5 m'],
       ['Almacenamiento', 'Ranura microSD', 'Historial de conversación y registros'],
       ['Alimentación', 'Batería de ion-litio', '1750 mAh, interruptor físico']],
      [3.0, 5.0, 8.0])
p('El conjunto presenta dos particularidades relevantes para el diseño del software. La primera es '
  'que el módulo de radio incorpora un conmutador de antena gobernado por un expansor de '
  'entrada/salida sobre el bus I²C, que debe habilitarse explícitamente antes de cualquier '
  'transmisión; de no hacerlo, el equipo transmite contra una carga inexistente. La segunda es que '
  'la radio y la tarjeta microSD comparten el mismo bus SPI, lo que obliga a serializar los '
  'accesos.')

h('5.3. Arquitectura de software', 2)
p('El firmware se desarrolla en C++17 sobre PlatformIO con el marco Arduino-ESP32. Se emplea la '
  'biblioteca RadioLib para el control del transceptor, M5Unified para los periféricos, TinyGPSPlus '
  'para el análisis de tramas NMEA y nanopb para la serialización de los mensajes del protocolo.')
p('El diseño se rige por tres restricciones autoimpuestas: no se realizan asignaciones dinámicas de '
  'memoria una vez completada la inicialización, todos los búferes tienen tamaño fijo declarado en '
  'un único fichero de configuración, y el código de radio no bloquea la ejecución, sino que se '
  'organiza como una máquina de estados gobernada por interrupción.')

h('5.4. Protocolo de radio', 2)
tabla(['Parámetro', 'Valor'],
      [['Frecuencia central', '869,525 MHz'],
       ['Factor de ensanchamiento', 'SF11'],
       ['Ancho de banda', '250 kHz'],
       ['Tasa de codificación', '4/5'],
       ['Potencia de transmisión', '22 dBm conducidos (≈ 193 mW p.r.e. con antena de 3 dBi)'],
       ['Cabecera', '16 bytes sin cifrar'],
       ['Carga útil máxima', '239 bytes cifrados'],
       ['Saltos máximos', '3'],
       ['Tiempo en el aire típico', '0,4 – 0,6 s por mensaje corto']],
      [6.0, 10.0])
p('El encaminamiento es por inundación controlada. Cada nodo retransmite los paquetes que no van '
  'dirigidos a él, decrementando un contador de saltos y registrando el paquete en una tabla de '
  'duplicados de 128 entradas para no reenviarlo dos veces. El retardo previo a la retransmisión se '
  'calcula en función de la relación señal-ruido recibida, de forma que los nodos más lejanos '
  'retransmiten antes y los cercanos, al oír el duplicado, cancelan su propio envío. Es un mecanismo '
  'sencillo y probado que evita las tormentas de retransmisiones.')
p('La confirmación de entrega es implícita: si un nodo oye a otro retransmitir un paquete del que él '
  'era el origen, da el mensaje por propagado.')

h('5.5. Seguridad', 2)
p('El tráfico se cifra con AES-256 en modo contador. La clave no se introduce en hexadecimal, lo que '
  'sería impracticable en un teclado de estas dimensiones, sino que se deriva de una frase que el '
  'usuario teclea igual en todos los aparatos, mediante la función PBKDF2-HMAC-SHA256 con 20 000 '
  'iteraciones. El nombre del canal se deriva también de esa misma frase, de modo que dos grupos con '
  'frases distintas quedan aislados entre sí.')
p('Debe hacerse constar una limitación deliberada: el modo contador proporciona confidencialidad '
  'pero no integridad. No existe código de autenticación de mensaje, por lo que un tercero podría '
  'alterar bits del texto cifrado sin que el sistema lo detecte. Esta es exactamente la misma '
  'garantía que ofrece Meshtastic, y se considera suficiente para el uso previsto —protege frente a '
  'un observador casual con un receptor de radio definida por software— pero no debe atribuírsele '
  'una robustez que no posee.')

h('5.6. Interfaz de usuario', 2)
p('La pantalla mide 240 × 135 píxeles, lo que permite mostrar cinco líneas de conversación. Esta '
  'limitación física determina el diseño: los nombres se abrevian a cuatro caracteres y cada nodo '
  'recibe un color propio, de manera que un niño identifica al remitente por el color sin necesidad '
  'de leer.')
p('Se organiza en cuatro pantallas —conversación, nodos, brújula y ajustes— que rotan con la tecla '
  'de tabulación, más una pantalla de emergencia que se superpone a todas las demás. El historial se '
  'recorre con las teclas de cursor. Una barra de estado permanente muestra la calidad del último '
  'enlace, los nodos visibles, el uso del ciclo de trabajo, el estado del GPS y la batería.')
p('La función de emergencia se activa manteniendo pulsado el botón lateral durante dos segundos, '
  'seguido de una confirmación con cuenta atrás de tres segundos. En recepción, ocupa la pantalla '
  'completa, emite una alarma acústica y ofrece responder con un solo toque. Se repite '
  'automáticamente hasta que alguien contesta.')

h('5.7. Almacenamiento', 2)
p('El historial de conversación se registra en la tarjeta microSD como un fichero de texto por mes, '
  'en formato tabulado y con adición al final. Este formato tiene dos ventajas prácticas: escribir un '
  'mensaje no obliga a reescribir el fichero, y la tarjeta puede extraerse y leerse en cualquier '
  'ordenador sin herramientas especiales.')
p('La tarjeta es opcional. Si no está presente, el sistema funciona igualmente con los sesenta '
  'mensajes que mantiene en memoria y lo indica discretamente en la barra de estado. La '
  'configuración y las claves residen siempre en la memoria no volátil interna, de modo que la '
  'ausencia de tarjeta nunca impide la comunicación.')

# ============================ 6 NORMATIVA ============================
h('6. Marco normativo', 1)
p('El equipo opera en la banda de dispositivos de corto alcance regulada por la norma '
  'ETSI EN 300 220-2. Dentro de dicha banda se ha seleccionado deliberadamente la subbanda '
  'denominada g3.')
tabla(['Subbanda', 'Margen de frecuencias', 'Potencia máxima', 'Ciclo de trabajo'],
      [['g1', '868,0 – 868,6 MHz', '25 mW p.r.e.', '1 %'],
       ['g2', '868,7 – 869,2 MHz', '25 mW p.r.e.', '0,1 %'],
       ['g3 (seleccionada)', '869,4 – 869,65 MHz', '500 mW p.r.e.', '10 %']],
      [4.0, 4.6, 3.4, 3.0])
p('La elección de g3 frente a la coloquialmente conocida como «banda de 868» (g1) responde a que '
  'aquélla admite veinte veces más potencia y diez veces más ciclo de trabajo. La potencia efectiva '
  'del equipo, 22 dBm conducidos con antena de 3 dBi, equivale a unos 193 mW de potencia radiada '
  'equivalente, holgadamente por debajo del límite de 500 mW.')
p('El cumplimiento del ciclo de trabajo se garantiza por software mediante un contador de tiempo en '
  'el aire sobre ventana móvil de sesenta minutos, que bloquea la transmisión al alcanzar el 10 % y '
  'suspende los envíos automáticos a partir del 8 %. Esta limitación no es desactivable desde la '
  'interfaz de usuario.')
p('Se hace constar que el conjunto no ha sido sometido a ensayo ni certificación. Los módulos '
  'empleados son productos comerciales con marcado propio del fabricante, pero la responsabilidad '
  'del cumplimiento del equipo final, tal y como se configura y utiliza, corresponde al usuario.')

# ============================ 7 COBERTURA ============================
h('7. Estudio de cobertura', 1)
h('7.1. Presupuesto de enlace', 2)
p('La sensibilidad térmica del receptor se obtiene de la expresión '
  'S = −174 + 10·log₁₀(B) + F + SNRmín, donde B es el ancho de banda en hercios, F la cifra de ruido '
  'del receptor (6 dB para el SX1262) y SNRmín la relación señal-ruido mínima del factor de '
  'ensanchamiento empleado (−17,5 dB para SF11).')
tabla(['Concepto', 'Valor'],
      [['Sensibilidad del receptor', '−131,5 dBm'],
       ['Potencia transmitida', '22 dBm'],
       ['Ganancia de antenas (transmisión y recepción)', '3 + 3 dBi'],
       ['Presupuesto de enlace bruto', '159,5 dB']],
      [10.0, 6.0])

h('7.2. Alcance estimado por escenario', 2)
p('El presupuesto bruto arrojaría distancias de miles de kilómetros en espacio libre, cifra carente '
  'de sentido práctico. A partir de unos 26 metros de separación, con los aparatos a 1,5 m del suelo, '
  'la propagación se rige por el modelo de dos rayos, en el que la reflexión en el terreno cancela '
  'parcialmente la señal directa. Existe además un techo absoluto independiente de la potencia: el '
  'horizonte radioeléctrico, que para dos aparatos a 1,5 m de altura se sitúa en 10,1 km.')
tabla(['Escenario', 'Márgenes considerados', 'Alcance estimado'],
      [['Ciudad densa', 'cuerpo 6 dB, desvanecimiento 8 dB, obstáculos 25 dB', '0,5 – 1,5 km'],
       ['Bosque denso', 'cuerpo 6 dB, desvanecimiento 6 dB, obstáculos 30 dB', '0,5 – 1,3 km'],
       ['Pueblo o suburbio', 'cuerpo 6 dB, desvanecimiento 6 dB, obstáculos 15 dB', '1,5 – 3 km'],
       ['Campo abierto llano', 'cuerpo 4 dB, desvanecimiento 3 dB, obstáculos 6 dB', '4 – 7 km'],
       ['Un nodo elevado 300 m', 'cuerpo 3 dB, desvanecimiento 3 dB, obstáculos 6 dB', '20 – 70 km']],
      [4.2, 7.0, 4.0])

h('7.3. Factores críticos', 2)
p('Dos efectos, ajenos al diseño del equipo, dominan el rendimiento real y conviene destacarlos:')
bullet('Polarización cruzada: una antena en posición horizontal comunicándose con otra en vertical '
       'introduce una pérdida de 20 a 30 dB, superior a la de atravesar un bosque denso. Es el error '
       'más costoso y el más sencillo de evitar.')
bullet('Absorción corporal: llevar el equipo en el bolsillo frente a sostenerlo en alto supone entre '
       '5 y 10 dB, lo que aproximadamente duplica o reduce a la mitad el alcance útil.')
p('Se concluye que la altura del emplazamiento influye mucho más que la potencia: elevar un nodo '
  '300 metros multiplica el alcance por diez, mientras que aumentar la potencia hasta el máximo '
  'regulatorio apenas lo multiplicaría por 1,3.')

# ============================ 8 PRESUPUESTO ============================
h('8. Presupuesto', 1)
p('Precios unitarios consultados en la tienda oficial del fabricante el 29 de julio de 2026, '
  'expresados en dólares estadounidenses, sin impuestos ni gastos de envío.')
tabla(['Concepto', 'Unidades', 'Precio unitario', 'Importe'],
      [['Cardputer Mesh Kit (Cardputer Adv + Cap LoRa-1262)', '2', '48,00 USD', '96,00 USD'],
       ['Tarjeta microSD (estimación)', '4', '≈ 5,00 USD', '≈ 20,00 USD'],
       ['Equipamiento ya disponible (2 nodos completos)', '2', '—', '0,00 USD'],
       ['TOTAL ESTIMADO PARA COMPLETAR LA RED DE 4 NODOS', '', '', '≈ 116,00 USD']],
      [8.0, 2.0, 3.2, 3.2])
p('El desarrollo del firmware no comporta coste de licencias: la totalidad de las bibliotecas '
  'empleadas son de código abierto.')
p('Procede señalar que la ampliación a tres nodos no es opcional desde el punto de vista técnico. '
  'La función de retransmisión —que constituye una de las razones de ser del proyecto— no puede '
  'verificarse con dos dispositivos, puesto que se requiere un tercero situado entre dos extremos '
  'sin enlace directo.')

# ============================ 9 PLANIFICACION ============================
h('9. Planificación y estado', 1)
tabla(['Fase', 'Contenido', 'Estado'],
      [['0', 'Estructura del proyecto y configuración de compilación', 'Completada'],
       ['1', 'Capa de abstracción de hardware', 'Verificada en hardware'],
       ['2', 'Puesta en marcha de la radio', 'Verificada en hardware'],
       ['3', 'Implementación del protocolo', 'Verificada: vectores y enlace real'],
       ['4', 'Encaminamiento por inundación', 'Verificado entre dos nodos'],
       ['5', 'Control del ciclo de trabajo', 'Completada, 64 pruebas automáticas'],
       ['6', 'Conversación e interfaz', 'Completada'],
       ['7', 'Gestión de nodos', 'Verificada en hardware'],
       ['8', 'Posicionamiento y brújula', 'GPS verificado; brújula sin verificar'],
       ['9', 'Función de emergencia', 'Completada, sin verificar de extremo a extremo'],
       ['10', 'Configuración en el dispositivo', 'Completada'],
       ['11', 'Acabado y documentación', 'Pendiente de las pruebas de campo']],
      [1.8, 9.2, 5.0])
p('Se dispone de dos unidades operativas que intercambian mensajes cifrados por radio, con una '
  'potencia recibida de −18 a −29 dBm a corta distancia y una relación señal a ruido de 6 a 7 dB. '
  'El protocolo se ha validado por dos vías independientes: contraste con vectores de prueba '
  'generados con una implementación de referencia, y funcionamiento real entre dos equipos.')
p('Queda pendiente la verificación de la retransmisión, que requiere un tercer equipo y constituye '
  'la única función del sistema todavía sin comprobar, así como las pruebas de exterior de '
  'posicionamiento y de la función de emergencia.')

# ============================ 10 VERIFICACION ============================
h('10. Verificación y validación', 1)
h('10.1. Pruebas en ordenador', 2)
p('Las capas de protocolo y malla compilan de forma independiente en un ordenador convencional y '
  'se someten a pruebas unitarias sin necesidad de hardware. Se han generado previamente vectores '
  'de prueba que fijan, byte a byte, la cabecera, el vector de inicialización, el resultado del '
  'cifrado y la función resumen del canal para cuatro casos representativos, incluyendo texto con '
  'caracteres multibyte y un paquete ya retransmitido.')
p('Se han validado asimismo cuatro constantes publicadas del protocolo original, que actúan como '
  'calibración: si alguna de ellas no se reproduce, el error está en la primitiva y no en la '
  'configuración.')
h('10.2. Pruebas en hardware', 2)
p('Cada fase incorpora un criterio de aceptación explícito. El de la fase 3 es determinante: un '
  'dispositivo con el firmware comercial original, configurado con la misma región, preajuste, '
  'nombre de canal y clave, debe mostrar en su pantalla los mensajes generados por el firmware '
  'propio.')
h('10.3. Pruebas de campo', 2)
p('Se ha definido un protocolo de siete ensayos —referencia en mesa, polarización, alejamiento en '
  'campo abierto, absorción corporal, entornos urbano y forestal, retransmisión con tres nodos, '
  'influencia de la altura y autonomía— con un criterio de enlace válido fijado en el 80 % de '
  'paquetes recibidos sobre ráfagas de veinte. Los resultados se registran automáticamente en la '
  'tarjeta microSD.')

# ============================ 11 RIESGOS ============================
h('11. Limitaciones y riesgos', 1)
tabla(['Riesgo o limitación', 'Naturaleza', 'Mitigación'],
      [['Error en la implementación del protocolo',
        'Fallo silencioso, difícil de localizar',
        'Vectores de prueba precalculados y validación contra un nodo comercial'],
       ['Daño irreversible del transceptor por transmitir sin antena',
        'Destrucción física del componente',
        'El firmware no transmite sin confirmación explícita; verificación del conmutador de antena antes de habilitar la transmisión'],
       ['Colisión de accesos al bus SPI compartido',
        'Pérdida de paquetes o bloqueo',
        'Escrituras diferidas a los periodos de inactividad de la radio y guarda de exclusión'],
       ['Alcance insuficiente en bosque denso',
        'Limitación física, no subsanable por software',
        'Documentada explícitamente; el sistema se presenta como ayuda, no como garantía'],
       ['Autonomía inferior a la prevista',
        'Limitación de diseño',
        'Desconexión del GPS y, en su caso, recepción cíclica del transceptor'],
       ['Pérdida de la frase de canal',
        'Incomunicación del grupo',
        'La frase es memorizable; puede reintroducirse en todos los aparatos']],
      [4.6, 4.4, 6.2])
p('Debe subrayarse una limitación de fondo: el sistema es una ayuda a la comunicación, no un '
  'dispositivo de seguridad. En entornos de vegetación densa y a distancias superiores al kilómetro '
  'un mensaje de emergencia puede no llegar. Esta circunstancia debe ser conocida por todos los '
  'usuarios y, en particular, no debe sustituir a las normas de comportamiento del grupo.')

# ============================ 12 CONCLUSIONES ============================
h('12. Conclusiones', 1)
p('El análisis realizado confirma que la tecnología LoRa en la subbanda de 869,4 a 869,65 MHz '
  'satisface los requisitos del problema planteado, y que el hardware ya disponible es apto sin '
  'modificación alguna.')
p('La alternativa de implementación adoptada —firmware propio con protocolo compatible— resuelve '
  'el principal riesgo del proyecto al proporcionar un método de verificación externo e '
  'independiente, sin renunciar a una interfaz diseñada específicamente para los usuarios previstos.')
p('El estudio de cobertura establece expectativas realistas: varios kilómetros en terreno abierto y '
  'menos de uno en vegetación densa, con la altura del emplazamiento como factor determinante muy '
  'por encima de la potencia radiada.')
p('El coste de completar la red asciende a unos 116 dólares, y la ampliación al tercer nodo resulta '
  'imprescindible para poder validar la función de retransmisión.')
p('El firmware se encuentra operativo en dos unidades, que intercambian mensajes cifrados por radio '
  'con encaminamiento y confirmación implícita de entrega. Los siguientes hitos son la verificación '
  'de la retransmisión con un tercer equipo, las pruebas de exterior y la medida del consumo real, '
  'de la que depende la estimación de autonomía.')
p('Sobre la autonomía: se han incorporado el escalado de frecuencia del procesador y el espaciado '
  'adaptativo del ciclo de ejecución, ambos verificados en el equipo. La medida de mayor impacto, la '
  'suspensión ligera del procesador, se implementó y se desactivó al comprobarse que dejaba el nodo '
  'sin recepción de radio; queda documentada y pendiente de resolver.')

# ============================ 13 REFERENCIAS ============================
h('13. Referencias', 1)
refs = [
 'ETSI EN 300 220-2. Short Range Devices (SRD) operating in the frequency range 25 MHz to 1 000 MHz.',
 'Semtech Corporation. SX1261/2 Long Range, Low Power, sub-GHz RF Transceiver. Hoja de características.',
 'M5Stack Technology Co., Ltd. Cardputer Adv (K132-Adv). https://docs.m5stack.com/en/core/Cardputer%20Adv',
 'M5Stack Technology Co., Ltd. Cap LoRa-1262 (U214). https://docs.m5stack.com/en/cap/Cap_LoRa-1262',
 'Proyecto Meshtastic. Código fuente del firmware. https://github.com/meshtastic/firmware',
 'Proyecto Meshtastic. Definiciones de protocolo. https://github.com/meshtastic/protobufs',
 'Proyecto Meshtastic. Algoritmo de malla. https://meshtastic.org/docs/overview/mesh-algo/',
 'Proyecto Meshtastic. Cifrado. https://meshtastic.org/docs/overview/encryption/',
 'Gromes, J. RadioLib: biblioteca universal de comunicaciones inalámbricas. https://github.com/jgromes/RadioLib',
 'M5Stack Technology Co., Ltd. M5Unified y M5Cardputer. https://github.com/m5stack',
]
for i, r in enumerate(refs, 1):
    doc.add_paragraph('[%d]  %s' % (i, r)).paragraph_format.space_after = Pt(3)

p()
p('Documentación técnica complementaria del proyecto: CLAUDE.md (contexto y pinout verificado), '
  'SPEC.md (especificación técnica y plan por fases), ALCANCE.md (presupuesto de enlace y plan de '
  'pruebas de campo) y VECTORES.md (vectores de prueba del protocolo).', size=9.5, color=GRIS)

# --- pie de pagina con numeracion ---
for sec in doc.sections:
    par = sec.footer.paragraphs[0]
    par.alignment = WD_ALIGN_PARAGRAPH.CENTER
    field(par, 'PAGE')
    for r in par.runs: r.font.size = Pt(9); r.font.color.rgb = GRIS

# Junto al resto de la documentacion, y relativo al repositorio: la ruta absoluta
# que habia aqui apuntaba a una sesion de trabajo que ya no existe.
import os
raiz = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
out = os.path.join(raiz, 'docs', 'Memoria_tecnica_Fantashtic.docx')
doc.save(out)
print('OK ->', out)
