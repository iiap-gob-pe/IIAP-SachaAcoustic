---
title: "IIAP SachaAcoustic — Manual de usuario"
subtitle: "Visualización, análisis y etiquetado de paisajes sonoros"
author: "Instituto de Investigaciones de la Amazonía Peruana (IIAP)"
date: "2026"
---

# IIAP SachaAcoustic

**Software de análisis de audio para bioacústica**, pensado para **apoyar la generación
de datos de entrenamiento para algoritmos de inteligencia artificial**. Permite abordar
**distintos tipos de análisis** de una grabación combinando: **filtros a nivel de
decibeles y de frecuencia**, **etiquetado** de los eventos de interés, y **visualización
2D y 3D** para **entender mejor la distribución de la señal** que se va a analizar.

El flujo es: cargar audio WAV → calcular el espectrograma → realzarlo con un método
**fiel** (no destructivo) → explorar la señal con **7 vistas** (2D + visualizaciones 3D
originales), filtros por barras y navegación temporal → **etiquetar** (por **polígono**
o **caja**, manual o automático) → **exportar** un conjunto de datos en formato **COCO**
(detección + segmentación) y **Raven** (selection table), listo para entrenar modelos.

Las etiquetas quedan **asociadas al audio**: al volver a abrir un archivo, si en la
carpeta de salida existe su conjunto de etiquetas, se **cargan automáticamente**.

Aplicación nativa de Windows escrita en C++ con OpenGL, **sin dependencias externas**.

![Icono](icon.png){ width=96px }

---

## Índice

1. Instalación y compilación
2. El proceso de realce (cómo se construye la imagen)
3. Disposición de la interfaz
4. Las 7 vistas (2D + 3D)
5. Filtros (barras de frecuencia / dB) y volumen
6. Navegación temporal y **zoom 2D**
7. Reproducción
8. Etiquetado (clases, herramientas, editar/cortar, auto, autosave, carga)
8 bis. Protocolo de etiquetado (con señal a 14 dB)
8 ter. Fundamento científico (matemático, físico, ecológico)
9. Referencia de cursores
10. Resumen de teclas
11. Galería de funciones (cómo se usa cada una)
12. Formatos y estructura
13. Referencias
14. Acerca de / Contacto

---

## 1. Instalación y compilación

### Requisitos
- Windows 10/11.
- Para compilar: **w64devkit** (g++ ≥ 13, `windres`) — toolchain portátil, sin instalación.

### Compilar
```bat
cd etiquetador_cpp
build.bat
```
Genera `IIAP_SachaAcoustic.exe` (workstation), `etiquetador.exe` (etiquetador 2D
simple) y `cli.exe` (prueba sin ventana). También con `make`. Cierra la aplicación
antes de recompilar para no bloquear el `.exe`.

### Ejecutar
```bat
IIAP_SachaAcoustic.exe  [audio.wav]  [carpeta_salida]
```
Si no se indica WAV, ábrelo con el botón **Abrir** (icono de carpeta) o la tecla `o`.
Formatos: WAV mono/estéreo, PCM 16/24/32 bits o float32 (se mezcla a mono).
**Al cargar no se autoetiqueta**; pero si en la **carpeta de salida** ya existe
`<nombre_audio>.json` (un guardado COCO previo), esas etiquetas **se cargan
automáticamente** (con sus polígonos/cajas y clases).

![Recién cargado el audio: barra de estado y de botones (arriba), espectrograma 2D con ejes (tiempo abajo, frecuencia a la izquierda), histograma de dB/frecuencia, barras de filtro a la derecha y navegador + oscilograma (abajo). Sin etiquetas.](man_step1_load.png)

---

## 2. El proceso de realce (cómo se construye la imagen)

1. **STFT**: ventana Hann (`nfft=1024`, `hop=256`), FFT propia → magnitud en dB
   `[1,2]`. **Rendimiento con audios pesados/ultrasónicos**: para archivos largos o de
   alta frecuencia de muestreo (p. ej. 96–384 kHz), el espectrograma tendría decenas de
   miles de columnas y todo se volvería lento; por eso el **salto (hop) se aumenta de
   forma automática** para que el ancho quede acotado (~4000 columnas), conservando la
   resolución de frecuencia. Así abrir un audio pesado es rápido y fluido. Después puedes
   ajustar la resolución temporal manualmente con **Res− / Res+** (recalcula el espectro y
   afecta a 2D y 3D — ver §4).
2. **Normalización** con rango dinámico fijo (**80 dB** bajo el máximo) → energía en `[0,1]`;
   por eso el **piso de la escala de dB es −80 dB** (se conserva la señal débil; poco recorte).
   Es el rango que usan la lectura del cursor, las barras de filtro y el eje dB 3D.
3. **Realce FIEL (asinh)**: transformación **monótona** (seno hiperbólico inverso) que
   levanta los sonidos débiles y comprime los fuertes **sin eliminar información**
   (a diferencia de PCEN, que normaliza por banda y distorsiona). Es la misma idea del
   *asinh stretch* usado para imágenes astronómicas con rango dinámico enorme `[5]`. Se
   eligió tras comparar 16 estrategias por entropía, contraste local, saturación y
   fidelidad.
4. **Mapa de color** para la visualización (Magma por defecto; seleccionable entre 10
   paletas con el botón **Mapa** — ver §3).

El **autoetiquetado** y los análisis de crestas se calculan sobre el espectro
**crudo** (donde la relación señal/ruido por banda es real), no sobre el realzado.

---

## 3. Disposición de la interfaz

```
┌───────────────────────────────────────────────────────────┐
│ Barra de estado: app | archivo | vista | clase | herram.   │
├───────────────────────────────────────────────────────────┤
│ Barra de botones por secciones (Archivo · Vistas ·          │
│   Reproducción · Navegación · Display · Etiquetado · 3D ·    │
│   Info[Acerca]) + PALETA de clases clicable                 │
├───────────────────────────────────────────────────────────┤
│   eje Hz                                            barras  │
│ │ │            VISTA PRINCIPAL (2D o 3D)            │ freq │ │
│ │ │                                                 │ dB   │ │
│   eje s                                             │ Vol  │ │
├───────────────────────────────────────────────────────────┤  ← manija de altura
│ NAVEGADOR: tira de espectrograma + ventana con manijas      │
│ Oscilograma (forma de onda, todo el audio)                  │
└───────────────────────────────────────────────────────────┘
```

La **barra de estado** muestra el archivo, la vista, la **clase activa**, la
**herramienta** activa y los rangos de los filtros. El espectro 2D se dibuja con
**márgenes** (eje de frecuencia a la izquierda, eje de tiempo abajo, barras a la
derecha) para poder etiquetar también en los bordes.

**Lectura del cursor (arriba a la derecha):** al pasar el ratón sobre el espectrograma
—el plot 2D principal o la **tira inferior**— aparece, en la parte superior derecha, una
etiqueta verde con el **tiempo (s)**, la **frecuencia (Hz/kHz)** y el **nivel (dB)** del
punto bajo el cursor. El dB usa el mismo criterio que las barras de filtro (0 dB = máximo).

**Barra de scroll horizontal:** sobre la base del **oscilograma** hay una franja delgada
**semitransparente** (cian) que representa la **ventana de tiempo** visible dentro de todo
el audio. Arrástrala para **desplazar la ventana**; además, **la rueda del ratón sobre toda
la zona inferior** (tira + oscilograma) **mueve la ventana** en el tiempo (ver §6).

**Botón “Acerca” (Info):** abre un panel con la información del software (creador, contacto,
institución) y **para qué sirve**. Se cierra con un **clic** o con **Esc** (ver §14).

**Mapa de color (botón “Mapa”):** elige la **paleta** del espectrograma entre **10**
opciones pensadas para resaltar las señales en distintos casos. Al pulsar **Mapa** se
despliega la lista (con una mini-muestra de cada paleta); haz **clic** en una, o usa
**↑/↓** para previsualizar en vivo y **Enter** para fijarla (**Esc** o un clic fuera
cierra). La tecla **M** abre/cierra la lista. Las paletas:

| # | Paleta | Cuándo usarla |
|---|---|---|
| 1 | **Magma** | General; perceptualmente uniforme (por defecto). |
| 2 | **Turbo** | Máximo detalle; resalta señales **débiles** y estructura fina. |
| 3 | **Jet** | Alto contraste cromático (bandas tonales muy marcadas). |
| 4 | **Caliente** | Enfatiza las señales **fuertes** (intensidad). |
| 5 | **Grises** | Neutro, sin sesgo de color. |
| 6 | **Grises invertido** | Fondo claro para **impresión** / ver señales tenues. |
| 7 | **Verde fósforo** | Clásico; señales tenues sobre negro, cómodo con poca luz. |
| 8 | **Hielo** | Paleta fría; contraste para **tonos puros**. |
| 9 | **Realce débil** | Magma con realce gamma: **levanta llamadas muy débiles**. |
| 10 | **Bandas** | Niveles posterizados: **contornos de igual energía** (umbral señal-ruido). |

El mapa elegido se aplica tanto al **espectrograma 2D** como a las **vistas 3D** y a la
barra de color.

---

## 4. Las 7 vistas

| Tecla | Vista | Descripción |
|---|---|---|
| `1` | **Espectrograma 2D** | Imagen tiempo–frecuencia; superficie de etiquetado, ejes e histograma. |
| `2` | **Terreno 3D** | El espectrograma como relieve; altura = energía. |
| `3` | **Río espectral** | Crestas tonales seguidas en el tiempo como hilos 3D. |
| `4` | **Nube de puntos** | Cada celda como punto 3D; opacidad por energía. |
| `5` | **Cascada espectral** | Espectro por rebanada de tiempo (X=frecuencia, Y=energía, Z=tiempo); marca los picos y **etiqueta las frecuencias dominantes en Hz**. |
| `6` | **Quiver3D** | Por cada hilo, palos verticales entre la envolvente de valles y la curva de crestas. |
| `7` | **Volumen** | Nube de puntos **densa** por etiqueta: en cada (tiempo, frecuencia) **apila** puntos desde el piso de dB hasta el dB real → un **cuerpo sólido** con relieve (no una superficie pobre). |

**Ejes 3D (ortoedro)**: la escena 3D es un **ortoedro** con rejilla en el piso y las
paredes del fondo (estilo gráfico científico), con **títulos** y **valores reales** por
eje, repartidos en aristas concretas:

- **Tiempo (s)** → **arista inferior del frente** (0 a la izquierda).
- **Frecuencia (Hz)** → **arista inferior derecha** (hacia el fondo) **y** también en la
  **arista superior derecha**; su **0 está en el vértice común con Tiempo** (frente-derecha).
- **Nivel (dB)** → **arista vertical frontal izquierda** (la altura = energía), con su
  **barra de color "Nivel (dB)"** a la izquierda.

Las vistas 3D **encuadran exactamente lo que dejan pasar las barras de filtro**, en
**ambos ejes**: el **eje de Nivel (dB)** abarca la **banda de dB del filtro** más un
**margen de 5 dB** (p. ej. si el piso está en −63 dB, el eje empieza en −68 dB), y el
**eje de Frecuencia** abarca la **banda de frecuencia del filtro** más un **margen de
1000 Hz**. Solo se dibuja el relieve dentro de esa banda de dB y de frecuencia, así que
mover las barras **acerca/encuadra** la zona de interés en 3D. (Es **solo
visualización**; **no** modifica el sonido. La barra de color "Nivel (dB)" mantiene el
rango absoluto porque el **color** codifica el dB real.)

La **vista inicial** es una perspectiva de ortoedro (frontal y algo elevada). La tecla
`r` intercambia tiempo ↔ frecuencia en el piso (dB siempre vertical). El
**playhead** es una línea sobre la frecuencia. Las etiquetas se dibujan en 3D **tal
cual**: las **cajas** como prismas y los **polígonos** con su contorno real extruido
(distinguibles entre sí); los **anillos/huecos** de un polígono se **excluyen** del volumen
y de la reproducción. El cubo tiene **manijas** (puntos de color en los extremos de cada
eje) para **agrandar/achicar** cada dimensión — funcionan en **todas las vistas 3D, incluida
la vista Cascada espectral**: arrastra el punto **rojo** (eje X), **verde** (eje Y) o **azul** (eje Z).

**Resolución (Res− / Res+)**: los botones **Res−** y **Res+** (teclas `[` y `]`) ajustan la
resolución y **afectan a TODAS las vistas, 2D y 3D**. Al pulsarlos se **recalcula el
espectrograma** con un salto (hop) menor (Res+ = más fino, más columnas en el tiempo) o
mayor (Res− = más grueso). Como todas las vistas derivan del mismo espectrograma, la vista
2D (textura) y las vistas 3D cambian a la vez; además se ajusta la densidad de las nubes 3D.
**No reinicia nada:** se **conservan la vista actual, los filtros y la ventana de zoom**
(la ventana de tiempo se reescala para que sigas viendo **la misma región**, no se vuelve a
“ver todo”). Las etiquetas existentes se **reescalan en el eje de tiempo** para que no se
desplacen (la resolución de frecuencia no cambia). El cambio **no** se deshace con `Ctrl+Z`.

**Cómo se usa:** pulsa las teclas `1`–`7` (o los botones de la sección *Vistas*) para cambiar de vista. En las vistas 3D (2–7) se **rota** arrastrando con el botón izquierdo, se hace **zoom** con la rueda y se **redimensiona el cubo** arrastrando las manijas de color (rojo=X, verde=Y, azul=Z).

![Vista 1 — Espectrograma 2D (tecla 1): plano tiempo (X, segundos) × frecuencia (Y, Hz), color = energía. Es la vista principal para etiquetar; arriba la barra de botones, a la derecha las barras de filtro, y abajo el navegador + oscilograma.](man_v1_espectro2d.png)

![Vista 2 — Terreno 3D (tecla 2): el espectrograma como relieve continuo; X = tiempo, Y (altura) = energía, Z = frecuencia. Los picos altos son los sonidos fuertes. Rota con el ratón, zoom con la rueda y redimensiona con las manijas.](man_v2_terreno3d.png)

![Vista 3 — Río espectral (tecla 3): líneas 3D que siguen las **crestas** (frecuencias dominantes) a lo largo del tiempo; ideal para seguir llamadas o tonos que cambian de frecuencia (glissandos).](man_v3_rio_espectral.png)

![Vista 4 — Nube de puntos (tecla 4): cada celda con energía significativa es un punto 3D (X = tiempo, Y = energía, Z = frecuencia). Muestra los eventos discretos sin saturar la imagen.](man_v4_nube_puntos.png)

![Vista 5 — Cascada espectral (tecla 5): cada rebanada de tiempo (profundidad Z) es el **espectro** de ese instante (X = frecuencia en Hz, Y = energía). Los **puntos blancos** marcan los picos (señales) y las **etiquetas cian** muestran las **frecuencias dominantes en Hz** → se ve QUÉ señales hay y A QUÉ frecuencia. Respeta los filtros (banda de frecuencia + dB) y el cubo es redimensionable.](man_v5_cascada_espectral.png)

![Vista 6 — Quiver3D (tecla 6): por cada hilo, palitos verticales entre la envolvente de valles y la cresta; rellenan las "jorobas" mostrando el ancho de banda y la energía de cada llamada.](man_v6_quiver3d.png)

![Vista 7 — Volumen (tecla 7): nube **densa** que dibuja **solo lo que está dentro de las etiquetas** (X = tiempo, Y = energía/dB, Z = frecuencia). En cada (tiempo, frecuencia) **apila** puntos desde el piso de dB hasta el dB real, formando un **cuerpo sólido con relieve** donde se ven los picos tonales y el cuerpo del ruido. Tiene barras de filtro **locales** propias que no afectan a las demás vistas; ideal para inspeccionar una etiqueta aislada del ruido. Requiere haber etiquetado antes (aquí tras pulsar Auto).](man_v7_volumen.png)

---

## 5. Filtros (barras) y volumen

A la **derecha** del espectro hay tres **barras verticales** con manijas arrastrables
que actúan **en vivo** sobre **todas** las vistas, la tira 2D **y la reproducción**:

| Barra | Manijas | Acción |
|---|---|---|
| **Frecuencia** (extremo derecho) | superior + inferior | Banda de frecuencia `[fLo, fHi]` |
| **dB** | superior + inferior | Banda de energía `[piso, techo]` |
| **Vol** (ganancia) | una manija | Volumen de reproducción (0–4×; >1 sube por encima del nivel normalizado) |

En el espectrograma, los píxeles fuera de banda se oscurecen (la imagen refleja el
filtro). Además, al **bajar el filtro de frecuencia** (estrechar la banda `[fLo, fHi]`)
el **plot 2D se ajusta a esa banda**: el espectrograma se **encuadra** a las frecuencias
filtradas y el **eje de frecuencia de la izquierda se reescala** para mostrar los Hz de la
banda visible (se combina con el zoom vertical de la rueda). En la reproducción se aplica un enmascarado STFT + reconstrucción
(overlap-add): **suena solo lo que se ve** —noción de **máscara binaria
tiempo–frecuencia** `[6,7]`. El botón **Hist** superpone el histograma de frecuencia
(lateral) y de dB (inferior), con las bandas del filtro resaltadas. La vista **Volumen**
tiene su **propio juego de barras** (Frecuencia, dB, Vol) que **solo afecta a esa vista**
(filtros locales); en las demás vistas las barras son globales. En la vista Volumen, la
**reproducción del sonido** usa esos filtros locales **y se limita a lo que está DENTRO de
las etiquetas** (suena solo la señal contenida en las etiquetas, en la banda de
dB/frecuencia que ves en 3D).

**Filtrado en tiempo real:** si mueves las barras de **dB** o **frecuencia** (o usas sus
teclas) **mientras se está reproduciendo**, el cambio se **aplica al sonido en vivo** —se
re-genera el audio desde la posición actual del *playhead* con el nuevo filtro, sin
detener la reproducción—.

**Al cargar un audio nuevo** (botón Abrir): **todos los filtros vuelven a su valor por
defecto** (frecuencia completa, dB completo, ganancia normal) y la **vista regresa al
Espectrograma 2D**, para empezar siempre con una imagen limpia.

---

## 6. Navegación temporal (ventana)

El **navegador inferior** muestra todo el audio. El **cuadro de ventana** (recuadro
amarillo con manijas) define qué porción se ve.

- **Manijas de los bordes** (cursor 🖐): arrastrar para **agrandar/achicar** la ventana.
- **Mover la ventana**: **botón central** del ratón o **Ctrl + clic izquierdo** dentro del cuadro.
- **Barra de scroll horizontal** (franja cian semitransparente sobre el oscilograma): arrástrala
  para **desplazar la ventana** por el audio.
- **Rueda del ratón sobre la zona inferior** (tira + oscilograma + scroll): **desplaza la
  ventana** en el tiempo (rueda arriba = izquierda, abajo = derecha). *(Sobre el espectrograma
  2D la rueda hace zoom, ver abajo.)*
- Botones **Ver todo** (ajustar), **Zoom +**, **Zoom −**.
- **Manija de altura**: el divisor superior del panel inferior sube/baja la tira + oscilograma.

### Zoom de la vista 2D (tiempo + frecuencia)
En la vista **Espectrograma 2D** puedes **acercar/alejar en ambos ejes** (tiempo *y*
frecuencia) para colocar o mover con precisión los puntos de los polígonos:

- **Rueda del ratón** sobre el espectro: zoom **hacia el cursor**.
- Botones **Zoom +** / **Zoom −** (lupa) y **Ver todo** (restablece tiempo y frecuencia).

Los **ejes** (s y Hz) y todas las etiquetas se reescalan al área visible.

---

## 7. Reproducción


| Botón / Tecla | Acción |
|---|---|
| ▶ / `Espacio` | Reproducir **la ventana visible** |
| ⏸ / `k` | Pausar / reanudar |
| ⏹ / `.` | Detener |
| **Banda** | Reproduce **solo la banda** (tiempo + frecuencia) de la selección |
| **Vel−** / `v` | Reproducir **más lento** (−0.1×) |
| **Vel+** / `V` | Reproducir **más rápido** (+0.1×) |

**Velocidad de reproducción (Vel− / Vel+):** ajusta la velocidad en pasos de **0.1×**
(de 0.1× a 4.0×); el valor actual se muestra en la barra de estado como `vel=1.0x`. La
velocidad funciona como un **tocadiscos**: a **menos de 1×** el sonido va más **lento y más
grave**, y a **más de 1×** más rápido y agudo (cambia también el tono). Reproducir **lento es
una EXPANSIÓN TEMPORAL**: baja las **altas frecuencias / el ultrasonido** hasta la banda
audible, así puedes **oír** llamadas de murciélagos o insectos (p. ej. un audio de 192 kHz a
0.25× lleva 0–96 kHz a 0–24 kHz). El *playhead* sigue marcando el tiempo real del audio. La
velocidad se puede cambiar **en vivo** durante la reproducción.

La **línea de reproducción (playhead)** amarilla avanza durante la reproducción y se
queda donde termina. Un **clic simple** sobre el navegador o el espectro reposiciona el
playhead (seek). El volumen se ajusta con la barra **Vol**. Cada fragmento se reproduce
con una **rampa de entrada/salida** corta, de modo que **no se oye el "golpe"/click** al
empezar ni al terminar. Los filtros de dB/frecuencia se pueden **mover en vivo** y se
aplican al instante sobre lo que está sonando (ver §5).

**Audios de alta frecuencia de muestreo (96/192/384 kHz, ultrasónicos):** muchos
dispositivos de audio **no reproducen** tasas tan altas y, además, el contenido
**ultrasónico** (>24 kHz) se "plegaría" sobre la banda audible (**aliasing**) sonando mal.
Por eso la reproducción **remuestrea automáticamente a 48 kHz con un filtro paso‑bajo
anti‑aliasing** (*sinc* enventanado, corte ≈21,6 kHz): el ultrasonido se **elimina** (no se
pliega) y la banda **audible** (≤24 kHz) se oye **limpia**, al **tono y velocidad reales**. El
espectrograma y las etiquetas siguen usando la **frecuencia completa** del archivo (p. ej.
hasta 96 kHz en un audio de 192 kHz); el remuestreo afecta **solo al sonido**, no al análisis.
El playhead avanza a la velocidad correcta. (Para *oír* el ultrasonido como tal harían falta
técnicas de expansión temporal o heterodino, que no se aplican aquí.)

**Reproducción sin filtros (rápida y sin cortes):** cuando **no** hay ningún filtro/selección
activo, la reproducción **copia el audio directamente** (sin reconstrucción STFT). Esto evita
el *lag* al pulsar play y los **cortes/“huecos”** que aparecían en audios **largos** (de
\~1 minuto o más): en ellos el espectrograma usa un salto (hop) grande para acotar las
columnas, y reconstruir el sonido con ese salto dejaba silencios entre tramas. Cuando **sí**
hay filtro activo, la reconstrucción usa un solapamiento fino propio, **independiente** del
salto del espectrograma, de modo que el sonido filtrado tampoco tiene cortes.

---

## 8. Etiquetado

### Clases dinámicas
Las clases se crean a voluntad. Por defecto existen **bio** (verde) y **antro** (cian).
La **paleta** de la barra (con muestra de color y nombre) selecciona la **clase activa**
con un clic. Para **crear una etiqueta nueva** pulsa **+Etiq** (`N`) y escribe el nombre
(Enter = aceptar, Esc = cancelar); también desde el menú contextual *Nueva etiqueta…*.

![Crear una etiqueta nueva (`N`): se abre un campo de texto in-app; escribe el nombre y pulsa **Enter** para aceptarlo (`Esc` cancela). La clase nueva queda como activa y se añade a la paleta.](man_tool_nueva.png)

### Herramientas (formas de etiquetar)
La forma de etiquetar es **polígono** o **bounding box**. Se elige en la barra:

| Tecla | Herramienta | Uso |
|---|---|---|
| `S` | **Selec** | Seleccionar/escuchar; clic = mueve playhead y selecciona etiqueta |
| `Y` | **BBox** | Arrastra un rectángulo → crea la caja de la clase activa |
| `P` | **Poly** | Etiqueta polígono a mano (clic = vértice; clic-der/Enter cierra) |
| `E` | **Editar** | Mueve vértices del polígono / manijas de la caja |
| `X` | **Cortar** | **Corte libre** (ver abajo) |

![Herramienta Selec (`S`): seleccionar y escuchar. Un clic sobre una etiqueta la selecciona (se resalta); un clic sobre un vértice lo marca (luego `Supr` lo borra). Es también la herramienta para arrastrar una **selección** de área (tiempo + frecuencia).](man_tool_selec.png)

![Herramienta BBox (`Y`): arrastra un rectángulo con el botón izquierdo y se crea una caja con la clase activa. Es la forma de etiquetar más rápida.](man_tool_bbox.png)

![Herramienta Poly (`P`): el polígono se traza a mano, un clic por vértice; **clic-derecho** o **Enter** cierra la forma (`Esc` cancela). En la imagen, un polígono en construcción (línea amarilla).](man_tool_poly.png)

- **Polígono a mano**: cada clic izquierdo añade un vértice; **clic derecho** o **Enter**
  cierra el polígono; **Esc** cancela. (Para segmentar una región automáticamente usa **Auto**
  o **Autoetiquetar seleccion** del menú contextual — ver abajo.)

![Herramienta Editar (`E`): primero clic sobre una etiqueta para seleccionarla; aparecen **manijas** en sus vértices (o en las esquinas/lados de una caja) que se arrastran para reformarla. Un clic sobre la línea **inserta** un vértice; con la etiqueta seleccionada, **Supr** borra la etiqueta entera.](man_tool_edit.png)

![Herramienta Cortar (`X`) — corte libre: con el botón izquierdo se **pinta un trazo** sobre la etiqueta (en rojo) y el **clic derecho** ejecuta el corte, separándola en piezas a lo largo del trazo.](man_tool_cut.png)

![Auto-etiquetar (`a`): el detector analiza TODO el espectro filtrado y genera las etiquetas automáticamente (aquí, polígonos sobre cada chirrido). Respeta los filtros de frecuencia y dB activos.](man_tool_auto.png)

![Autoetiquetar la selección: con la herramienta **Selec**, arrastra un rectángulo y haz **clic derecho DENTRO de la selección** → elige *“Autoetiquetar seleccion”*. Detecta **solo dentro del área** y **añade** las etiquetas (no reemplaza), con la clase activa. Importante: si el clic derecho cae dentro de la selección, aparece este menú **aunque haya una etiqueta debajo** (así puedes autoetiquetar la selección sin tocar la etiqueta).](man_tool_autosel.png)

![Mejorar etiqueta: con **clic derecho SOBRE una etiqueta** (polígono), fuera de cualquier selección, aparece el menú de la etiqueta con *“Mejorar etiqueta”* (re-segmenta solo esa etiqueta según el filtro), *“Buffer…”*, cambiar de clase, *Borrar*, etc.](man_tool_mejorar.png)

### Relleno y anillos (huecos)
Cada etiqueta-polígono se dibuja con un **relleno translúcido ligero** del color de su clase
(la **seleccionada**, en amarillo). Los **anillos/huecos** (zonas marcadas como *NO* la clase,
dentro de una etiqueta) quedan **sin rellenar**, de modo que **se ven** claramente como
recortes dentro del relleno. Así distingues de un vistazo dónde está cada anillo. El relleno se
muestra en el plot 2D principal.

### Editar y cortar
- **Entrar a Editar**: pulsa **Editar** (`E`) o, más rápido, **clic derecho sobre una etiqueta → “Editar”**.
- **Editar**: primero **selecciona** una etiqueta (clic en su interior). Si hay una etiqueta
  **pequeña dentro de una más grande**, el clic selecciona **la más pequeña** (la de menor área),
  para que siempre puedas editar la interior. La etiqueta seleccionada muestra **manijas amarillas**
  en TODOS sus vértices —los del **contorno exterior y los de los anillos/huecos**— para verlos y agarrarlos. Luego:
  - **Mover un vértice**: arrastra un **vértice** (del contorno **o de un anillo**), o las **manijas**
    de una caja (4 esquinas + 4 lados).
  - **Mover el polígono entero**: arrastra **dentro** del polígono (conserva la forma, mueve también
    sus anillos; no se convierte en caja). El desplazamiento se **limita al área del espectro por eje**:
    si no hay espacio horizontal no se mueve en el tiempo, pero si lo hay hacia arriba/abajo sí se mueve
    en frecuencia (y viceversa). Funciona aunque el polígono tenga **muchos puntos**.
  - **Agregar un punto**: **clic izquierdo SIMPLE SOBRE LA LÍNEA (arista)** del contorno **o de un anillo**;
    se inserta ahí. Solo al **soltar un clic sin movimiento** (si arrastras, no crea nada; un clic dentro
    del cuerpo tampoco).
  - **Marcar / borrar un vértice**: un **clic izquierdo SOBRE un vértice** (exterior o de anillo) lo
    **marca** (punto **rojo**) y **Supr** lo borra; un **clic derecho sobre un vértice** lo **borra**
    directamente. *Un vértice solo se selecciona si haces clic **sobre él**; un clic en zona vacía o en el
    cuerpo del polígono **no** marca ningún vértice.* Si un anillo quedara con menos de 3 puntos se elimina
    ese anillo; si el contorno quedara con menos de 3, se borra la etiqueta.
  - **Salir de Editar**: un **clic fuera** del polígono que editas (o **sobre otro** polígono) **desactiva
    Editar** y pasa a **Seleccionar** (deja seleccionado el de abajo, si lo hay). `Esc` también sale de Editar.
  - **Borrar la etiqueta entera**: con una etiqueta **seleccionada** (sin vértice marcado), pulsa **Supr**.
  - **Deshacer** (`Ctrl+Z`): revierte el último cambio en las etiquetas (crear, editar, mover, agregar/quitar
    punto, cortar, auto, borrar…). Hasta 50 pasos.
  - Combínalo con el **zoom 2D** (rueda) para mayor precisión.
- **Cortar** (`X`) — **corte libre**: con el botón izquierdo **pintas el trazo** y con **clic derecho** se
  ejecuta el corte. Corta **todas las etiquetas que cruza** el trazo (no hace falta seleccionarlas) y
  **conserva el tipo** de cada una (un polígono se parte en polígonos; una caja, en cajas). Cada pieza puede
  recibir otra clase. `Esc` limpia el trazo.

### Menú contextual (clic derecho)
El menú **distingue** si actúas sobre una **etiqueta** o sobre una **selección**:
- **Sobre una etiqueta** (clic derecho encima, **fuera** de cualquier selección): *Reproducir
  etiqueta*, **Editar** (activa la herramienta Editar sobre esa etiqueta), y para polígonos
  **Mejorar etiqueta** (re-segmenta **solo esa** etiqueta según el filtro) y **Buffer…**, cambiar
  de **clase**, *Nueva etiqueta…*, *Borrar*.
- **Sobre una selección** (clic derecho **dentro del rectángulo** seleccionado): *Reproducir
  selección*, **Autoetiquetar seleccion (poligonos)** y **Autoetiquetar seleccion (cajas)**,
  *Crear: <clase>*, **Borrar etiquetas en la selección (Supr)**, *Quitar selección*.
  - **Autoetiquetar seleccion** ejecuta el autoetiquetado **sólo dentro del rectángulo**
    (tiempo + frecuencia), sobre el espectro **filtrado**, en la forma que elijas (**polígonos** o
    **cajas**). Las etiquetas nuevas se **añaden** (no reemplazan) y reciben la **clase activa**.
  - **Prioridad:** si el clic derecho cae **dentro de la selección**, sale el menú de
    *selección* **aunque haya una etiqueta debajo** — así puedes autoetiquetar el área sin
    afectar la etiqueta. Para el menú de la etiqueta, haz clic derecho sobre ella **fuera** de
    la selección (o quita la selección con *Quitar selección*).
- En zona vacía (sin etiqueta ni selección): *Auto-etiquetar* (todo), *Nueva etiqueta…*.

### Borrar con la herramienta Selec (`S`)
- **Borrar un vértice**: haz **clic SOBRE un vértice** de cualquier polígono —del contorno **o de
  un anillo**— (se marca en **rojo**) y pulsa **Suprimir (Supr)** para eliminar **solo ese vértice**.
  También con **clic derecho** sobre el vértice. Si el contorno quedaría con menos de 3 vértices se
  borra la etiqueta; si es un anillo, se elimina ese anillo.
- **Borrar etiquetas por área**: arrastra una **caja de selección** (tiempo + frecuencia)
  sobre el espectro y pulsa **Supr**: se borran **todas las etiquetas cuyo centro queda
  dentro** de la caja. También por **clic derecho → "Borrar etiquetas en la selección"**.
- **Supr** prioriza el **vértice marcado**; si no hay ninguno, actúa sobre la selección o,
  en su defecto, sobre la etiqueta seleccionada.

### Mejorar y Buffer (solo polígonos)
- **Mejorar etiqueta**: re-segmenta **solo la región** de esa etiqueta sobre el
  espectro **filtrado** y reemplaza su contorno (conserva la clase).
- **Buffer…**: abre un modal con una **barra** para ajustar el *buffer* (suavizado/margen)
  y re-procesa el polígono en vivo (menos detalle y menos sub-etiquetas).

### Autoetiquetado
**Auto** (`a`) detecta sobre el espectro **filtrado** (respeta los filtros) y produce la
**forma activa** (polígono o caja). En modo polígono aplica un *buffer* de suavizado. El
detector combina un **umbral por banda** (`mediana + K·MAD`) `[3,4]` con un **umbral
global** (que capta las **bandas graves estacionarias**, que el umbral por fila no ve),
seguido de morfología y **componentes conexos** + trazado de contorno `[7]`. **No hay
límite de tamaño ni de ancho:** las detecciones grandes —incluida una **banda que cruza
toda la grabación**— se etiquetan como un solo evento (no se descartan). Para evitar
detecciones no deseadas, ajusta los filtros de dB/frecuencia o la estrictez del detector
antes de pulsar **Auto**, o usa **Autoetiquetar seleccion** para acotar el área.

En modo **polígono**, el autoetiquetado también detecta **anillos/huecos**: si dentro de una
detección hay una zona que **no** supera el umbral (un “agujero” que no es la clase), se traza
como un **anillo** y se excluye de la etiqueta (y de su volumen/reproducción). Los anillos se
guardan en el COCO como polígonos adicionales de la misma anotación (1.º exterior, luego huecos)
y se pueden **editar** como cualquier vértice (ver *Editar*).

### Otras
- **Ocultar** (`O`): muestra/oculta las etiquetas (2D y 3D).
- **Limpiar** (`c`): borra **todas** las etiquetas.
- **Guardar** (`s`): exporta `<audio>.json` (**COCO**: categorías dinámicas, bbox +
  segmentación, `kind`) y `<audio>.txt` (**Raven** selection table). En cada anotación
  COCO se guarda además el **umbral de dB de señal** (`signal_db_threshold`), el rango de
  frecuencia/dB y la frecuencia de muestreo.
- **Autoguardado (autosave)**: cualquier cambio en las etiquetas (crear, editar, mover,
  cortar, borrar, auto…) se **guarda solo** a los pocos segundos en `<audio>.json` +
  `<audio>.txt`, sin pulsar nada, y también **al cerrar** la aplicación. El botón
  **Guardar** sigue disponible (muestra el aviso); el autosave es silencioso.
- **Carga automática**: al abrir un audio, si existe `<audio>.json` en la carpeta de
  salida, sus etiquetas (polígonos/cajas y clases) **se cargan solas** —se retoma el
  trabajo donde quedó—. **Cargar** (`R`) importa además una selection table de **Raven**
  (`.txt`) y recrea las cajas (creando clases nuevas si hace falta).

---

## 8 bis. Protocolo de etiquetado (cómo etiquetar)

> **Regla general — extensión de la etiqueta:** *Etiquetar desde la frecuencia mínima
> visible hasta la frecuencia máxima donde la energía del sonido todavía sea claramente
> distinguible del ruido de fondo.*

Antes de marcar un sonido conviene saber **a qué frecuencias y a qué nivel (dB) es
entendible** —dónde deja de ser señal y pasa a ser ruido—. Esto es, operativamente,
buscar dónde la **relación señal/ruido** cae por debajo del umbral de detección `[8]`.
Ejemplo con un **valor de señal de 14 dB**:

**Paso 1 — Localiza el sonido.** Acércate con la ventana del navegador (zoom) para
aislarlo en el tiempo. El espectrograma muestra señal + ruido (figura del §1).

**Paso 2 — Baja el techo de dB y escucha.** Arrastra la manija superior de la barra de
**dB** (o `dBhi-`) y **reproduce** (`Espacio`) repetidamente. Al bajar el techo se quitan
los componentes más fuertes; sigue bajando hasta que el sonido **deje de entenderse** y
se oiga sólo como **estática/ruido**. En el ejemplo, 14 dB por debajo del techo el sonido
ya se vuelve indistinguible del fondo.

![Paso 2 — Se ha bajado el techo de dB 14 dB: la señal fuerte queda removida y sólo queda el ruido de fondo (el punto donde el sonido "se vuelve estática").](man_step2_techo.png)

**Paso 3 — Fija el valor de señal (botón "Señal", `f`).** Ese techo (el nivel donde el
sonido se volvió ruido) es el **umbral de señal**. Pulsa **Señal**: el programa lo
convierte en el **piso de dB** y abre el techo al máximo, dejando el rango
**[14 dB, 0]**. Ahora el espectrograma muestra **sólo lo entendible** (lo que está por
encima del ruido).

![Paso 3 — Tras pulsar "Señal" con el umbral a 14 dB: el filtro muestra el rango [14 dB, 0] y queda únicamente la señal por encima del ruido, lista para etiquetar.](man_step3_senal.png)

**Paso 4 — Etiqueta (o autoetiqueta).** Con la herramienta **Poly**/**BBox** marca el
sonido —**desde la frecuencia mínima visible hasta la máxima aún distinguible**— o pulsa
**Auto** para que el detector proponga las etiquetas **sobre la señal filtrada**. Cada
sonido puede tener su propio umbral; repite por sonido o por banda de frecuencia.

![Paso 4 — Autoetiquetado sobre la señal filtrada a 14 dB: polígonos (verde = bio, cian = antro) ajustados a cada sonido por encima del ruido.](man_step4_auto.png)

Para revisar sin el estorbo de las cajas, usa **Ocultar** (`O`):

![Ocultar / mostrar etiquetas (`O`): alterna la visibilidad de todos los contornos para inspeccionar el espectro limpio (toggle).](man_tool_ocultar.png)

![Limpiar (`c`): borra **todas** las etiquetas de golpe (deja el espectro sin anotaciones). Se puede revertir con `Ctrl+Z` si se hace de inmediato.](man_tool_limpiar.png)

El **botón Señal** ejecuta los pasos 2→3 con un clic: la inteligibilidad la juzga **un
humano escuchando** (no se automatiza), y el programa sólo traduce ese juicio a un filtro.

---

## 8 ter. Fundamento científico de la metodología

El protocolo —bajar el techo de dB escuchando hasta que el sonido se vuelve ruido, fijar
ese nivel como umbral de señal y etiquetar lo entendible, desde la frecuencia mínima
visible hasta la máxima distinguible— tiene sustento **matemático**, **físico** y
**ecológico**.

### Justificación matemática
El espectrograma expresa la energía en **decibeles**, una escala logarítmica obtenida por
una **transformada de Fourier de tiempo corto (STFT)** `[1,2]`:

```
L(t,f) = 10 · log10( |X(t,f)|² / P_ref )
```

En cada banda coexisten una **señal** y un **piso de ruido** N(f). Un evento es
detectable cuando supera al ruido por un margen (relación señal/ruido):
`SNR(t,f) = L(t,f) − N(f) ≥ θ`. Elegir θ por escucha es fijar el **punto de operación**
de un detector `[8]`. Filtrar equivale a aplicar una **máscara binaria** sobre el plano
tiempo–frecuencia `[6,7]`:

```
M(t,f) = 1  si  L(t,f) ≥ θ ;   0  en caso contrario
```

Al fijar el piso en θ y abrir el techo se conserva exactamente `{(t,f) : L ≥ θ}`, el
soporte de la señal por encima del ruido. El realce asinh es una función **monótona** g,
así que `L₁ ≥ L₂ ⇔ g(L₁) ≥ g(L₂)`: **no altera qué bins superan el umbral**, sólo su
visibilidad `[5]`. El autoetiquetado por banda usa un estimador **robusto** del piso de
ruido, `mediana + K·MAD` (la MAD tiene punto de ruptura del 50 % y factor de
consistencia ≈ 1.4826 para ruido gaussiano) `[3,4]`, y agrupa el soporte con
**componentes conexos** `[7]`.

### Justificación física
El nivel de presión sonora es **logarítmico** (decibel re 20 µPa) `[13]` y la percepción
auditiva sigue aproximadamente la **ley de Weber–Fechner** (respuesta proporcional al
logaritmo del estímulo) `[9]`, de ahí el uso natural de los decibeles y de un realce
cuasi-logarítmico. La inteligibilidad está gobernada por el **enmascaramiento auditivo**:
dentro de una **banda crítica**, un sonido sólo se percibe si excede el **umbral de
enmascaramiento** impuesto por el ruido `[10,11,12]`. Cuando el oyente baja el techo y el
sonido "se vuelve estática", localiza operativamente ese **nivel de enmascaramiento /
piso de ruido**. Definir θ **por banda** reconoce que N(f) **depende de la frecuencia**
`[10,11]`.

### Justificación ecológica
En el marco de la **ecología del paisaje sonoro**, una grabación se descompone en
**biofonía**, **geofonía** y **antropofonía** `[14]`. Una señal animal es funcional sólo
si supera el ruido en el receptor: esto define su **espacio activo** (*active space*)
`[16,17,18]`, y la **hipótesis de adaptación acústica** liga la estructura de frecuencia
de la señal a la transmisión del hábitat `[19]`. Los sonidos bióticos tienden a
**repartirse en frecuencia y tiempo** (**hipótesis del nicho acústico**) `[15]`, lo que
justifica delimitar la etiqueta por su **banda de frecuencia**. La **antropofonía** (ruido
grave y de banda ancha) **enmascara** la biofonía y reduce el espacio activo `[20,21]`.
Por eso anotar, por cada sonido, la banda y el nivel a los que es entendible es coherente
con el criterio biológico de **detectabilidad** `[26]`: lo que está por encima del piso de
ruido es lo que un receptor podría percibir. Además, los **índices ecoacústicos** —ACI
`[22]`, NDSI `[23]` (que separa biofonía/antropofonía por banda), Índice Bioacústico
`[24]` y la entropía acústica `[25]`— son sensibles al ruido; etiquetar sólo lo que supera
el umbral produce datos **ecológicamente significativos**, mejores para entrenar
clasificadores y comparar sitios.

En síntesis, el procedimiento traduce un criterio perceptual (¿se entiende el sonido?) en
un umbral de SNR sobre el espectrograma, coherente con la física del enmascaramiento y con
la ecología de la detectabilidad de señales.

---

## 9. Referencia de cursores

| Zona | Cursor |
|---|---|
| Sobre manijas (bordes de ventana, divisor de panel) | 🖐 mano |
| Sobre el espectrograma (principal o tira) | ✛ cruz |
| Resto | flecha |

---

## 10. Resumen de teclas

```
o abrir   R cargar Raven   s guardar (COCO + Raven)   1-7 vistas
ESPACIO play ventana   k pausa   . detener   B banda   v/V velocidad -/+ 0.1x
T ver todo   Z zoom+   U zoom-   r ejes(3D)   [ ] Res-/Res+ resolucion (2D y 3D)
I histograma   f Senal (fija umbral)   M mapa de color (combo)   N nueva etiqueta
S selec   Y bbox   P poligono   E editar   X cortar
a auto   O ocultar etiquetas   c limpiar   d/Supr borra sel/etiqueta(Editar)   z asigna clase
Ctrl+Z deshacer   ? Acerca de / Contacto   b bio   n antro   q salir   ESC cancela (no cierra)
```
Filtros y volumen: arrastra las **barras** de la derecha (frecuencia, dB, Vol).

---

## 11. Galería de funciones (cómo se usa cada una)

Esta sección muestra, con una captura, **cada herramienta y función** y los pasos para usarla.
(Las 7 vistas están en §4 y las herramientas de etiquetado en §8.)

### Visualización

![Lectura del cursor (arriba a la derecha): al **mover el ratón** sobre el espectrograma (o la tira inferior) aparece un panel verde con el **tiempo (s)**, la **frecuencia (Hz/kHz)** y el **nivel (dB)** del punto bajo el cursor, en vivo. Sirve para inspeccionar un punto antes de etiquetar.](man_feat_cursor.png)

![Mapa de color (`M` o botón **Mapa**): abre un combo con **10 paletas** (Magma, Turbo, Jet, Caliente, Grises, Grises invertido, Verde fósforo, Hielo, Realce débil, Bandas). Haz **clic** en una, o usa **↑/↓** para previsualizar y **Enter** para fijarla. Cada paleta resalta mejor distintos casos (señales débiles, contraste, impresión…).](man_feat_mapa_combo.png)

![Ejemplo de mapa de color: paleta **Grises** seleccionada. El mismo espectro en escala de grises, útil para una lectura neutra o para impresión.](man_feat_mapa_grises.png)

![Histograma (`I`): superpone la distribución de energía por **frecuencia** (panel derecho) y por **dB** (abajo), con las bandas del filtro activo resaltadas. Ayuda a ver dónde se concentra la energía.](man_feat_histograma.png)

![Fijar señal (`f`): el oyente baja el **techo de dB** hasta que el sonido se vuelve ruido; al pulsar `f`, ese nivel pasa a ser el **piso** (umbral de señal) y el techo se abre. Separa señal de ruido para etiquetar/auto-etiquetar solo lo audible.](man_feat_senal.png)

![Filtro de **frecuencia**: arrastra las manijas de la barra de Frecuencia (o teclas `u/j` arriba, `i/m` abajo). Al **estrechar la banda**, el espectrograma 2D y su **eje de frecuencia se reencuadran** a esa banda — aquí el plot pasó a mostrar solo la parte baja del espectro.](man_feat_filtro_freq.png)

![Filtro de **dB**: arrastra las manijas de la barra de dB (o teclas `y/g` piso, `w/e` techo). Al **subir el piso** se ocultan los píxeles débiles y resaltan las señales fuertes; lo que queda fuera de la banda se ve negro.](man_feat_filtro_db.png)

![Control de **Volumen** (Vol): barra con una manija a la derecha; ajusta la **ganancia de reproducción** (0.1× a 4×). Afecta solo al sonido, no a la imagen.](man_feat_filtro_vol.png)

### Reproducción y navegación

![Reproducir (**Espacio**): suena la ventana visible y el **playhead** (línea vertical amarilla) avanza marcando el tiempo. Un clic reposiciona el playhead; Espacio de nuevo pausa.](man_feat_play.png)

![Velocidad de reproducción (`v` más lento / `V` más rápido, pasos de 0.1×): el HUD muestra `vel=…x`. A **menos de 1×** baja el tono (expansión temporal) y permite **oír el ultrasonido** llevado a la banda audible; a más de 1× acelera.](man_feat_velocidad.png)

![Banda (`B`): con una **selección** activa (arrastre en el espectro) reproduce **solo** esa banda de tiempo + frecuencia. El HUD indica `play=banda(t+f)`.](man_feat_banda.png)

![Zoom 2D (`Z` acercar / `U` alejar; también la **rueda** sobre el espectro): amplía tiempo y frecuencia para colocar puntos con precisión. `T` = ver todo.](man_feat_zoom2d.png)

![Resolución (`[` Res− / `]` Res+): recalcula el espectrograma con un salto temporal mayor/menor; afecta a **2D y a todas las 3D**. Más fino = más columnas (más detalle); las etiquetas se reescalan solas.](man_feat_resolucion.png)

![Navegador inferior: la **tira** muestra todo el audio con un recuadro de la ventana visible (arrastrable para desplazarse) y el **oscilograma** la forma de onda. A la derecha, las tres barras de filtro (Frecuencia, dB, Vol).](man_feat_navegador.png)

### Guardar

![Guardar (`s`): exporta en `out/` el `<audio>.json` (**COCO**, con clases, cajas y polígonos) y el `<audio>.txt` (**Raven** selection table). El autoguardado lo hace solo tras cada cambio. Al reabrir el audio, las etiquetas se **recargan** automáticamente desde el `.json`. (Cargar Raven: `R`; abrir audio: `o` — abren un diálogo del sistema.)](man_flujo_guardar.png)

---

## 12. Formatos y estructura

- **Entrada**: WAV (PCM16/24/32, float32).
- **Salida**: COCO `<audio>.json` (categorías dinámicas, bbox + segmentación, `kind`,
  `signal_db_threshold`) y Raven `<audio>.txt` (selection table: Begin/End Time, Low/High
  Freq, Annotation). **Carga** de selection tables de Raven.
- **Código** (`src/`): `wav, fft, spectrogram, enhance(+magma_lut), autolabel, coco,
  raven, bmp, audio_play, glfont, types`, y `iiap_sachaacoustic.cpp` (aplicación).

---

## 13. Referencias

*Todas las referencias fueron verificadas (autores, año, publicación y DOI).*

### Matemática y procesamiento de señales
- **[1]** Gabor, D. (1946). *Theory of communication. Part 1: The analysis of information.* Journal of the IEE — Part III, 93(26), 429–441. https://doi.org/10.1049/ji-3-2.1946.0074
- **[2]** Allen, J. B.; Rabiner, L. R. (1977). *A unified approach to short-time Fourier analysis and synthesis.* Proceedings of the IEEE, 65(11), 1558–1564. https://doi.org/10.1109/PROC.1977.10770
- **[3]** Hampel, F. R. (1974). *The influence curve and its role in robust estimation.* Journal of the American Statistical Association, 69(346), 383–393. https://doi.org/10.1080/01621459.1974.10482962
- **[4]** Rousseeuw, P. J.; Croux, C. (1993). *Alternatives to the median absolute deviation.* Journal of the American Statistical Association, 88(424), 1273–1283. https://doi.org/10.1080/01621459.1993.10476408
- **[5]** Lupton, R.; Blanton, M. R.; Fekete, G.; Hogg, D. W.; O'Mullane, W.; Szalay, A.; Wherry, N. (2004). *Preparing red-green-blue images from CCD data.* Publications of the Astronomical Society of the Pacific, 116(816), 133–137. https://doi.org/10.1086/382245
- **[6]** Wang, D. L. (2005). *On ideal binary mask as the computational goal of auditory scene analysis.* En Speech Separation by Humans and Machines, pp. 181–197. Springer. https://doi.org/10.1007/0-387-22794-6_12
- **[7]** Wang, D. L.; Brown, G. J. (Eds.) (2006). *Computational Auditory Scene Analysis: Principles, Algorithms, and Applications.* Wiley-IEEE Press. https://doi.org/10.1109/9780470043387
- **[8]** Kay, S. M. (1998). *Fundamentals of Statistical Signal Processing, Vol. II: Detection Theory.* Prentice Hall. ISBN 0-13-504135-X.

### Física y psicoacústica
- **[9]** Fechner, G. T. (1860). *Elemente der Psychophysik.* Breitkopf und Härtel. https://doi.org/10.3931/e-rara-10879
- **[10]** Fletcher, H. (1940). *Auditory patterns.* Reviews of Modern Physics, 12(1), 47–65. https://doi.org/10.1103/RevModPhys.12.47
- **[11]** Zwicker, E.; Fastl, H. (2007). *Psychoacoustics: Facts and Models* (3.ª ed.). Springer. https://doi.org/10.1007/978-3-540-68888-4
- **[12]** Moore, B. C. J. (2013). *An Introduction to the Psychology of Hearing* (6.ª ed.). Brill. ISBN 978-90-04-25242-4.
- **[13]** Kinsler, L. E.; Frey, A. R.; Coppens, A. B.; Sanders, J. V. (2000). *Fundamentals of Acoustics* (4.ª ed.). Wiley. ISBN 978-0-471-84789-2.

### Ecología y bioacústica
- **[14]** Pijanowski, B. C.; Villanueva-Rivera, L. J.; Dumyahn, S. L.; Farina, A.; Krause, B. L.; Napoletano, B. M.; Gage, S. H.; Pieretti, N. (2011). *Soundscape ecology: the science of sound in the landscape.* BioScience, 61(3), 203–216. https://doi.org/10.1525/bio.2011.61.3.6
- **[15]** Krause, B. L. (1993). *The niche hypothesis: a virtual symphony of animal sounds, the origins of musical expression and the health of habitats.* The Soundscape Newsletter, 6, 6–10.
- **[16]** Marten, K.; Marler, P. (1977). *Sound transmission and its significance for animal vocalization. I. Temperate habitats.* Behavioral Ecology and Sociobiology, 2(3), 271–290. https://doi.org/10.1007/BF00299740
- **[17]** Brenowitz, E. A. (1982). *The active space of red-winged blackbird song.* Journal of Comparative Physiology A, 147(4), 511–522. https://doi.org/10.1007/BF00612017
- **[18]** Lohr, B.; Wright, T. F.; Dooling, R. J. (2003). *Detection and discrimination of natural calls in masking noise by birds: estimating the active space of a signal.* Animal Behaviour, 65(4), 763–777. https://doi.org/10.1006/anbe.2003.2093
- **[19]** Morton, E. S. (1975). *Ecological sources of selection on avian sounds.* The American Naturalist, 109(965), 17–34. https://doi.org/10.1086/282971
- **[20]** Brumm, H.; Slabbekoorn, H. (2005). *Acoustic communication in noise.* Advances in the Study of Behavior, 35, 151–209. https://doi.org/10.1016/S0065-3454(05)35004-2
- **[21]** Barber, J. R.; Crooks, K. R.; Fristrup, K. M. (2010). *The costs of chronic noise exposure for terrestrial organisms.* Trends in Ecology & Evolution, 25(3), 180–189. https://doi.org/10.1016/j.tree.2009.08.002
- **[22]** Pieretti, N.; Farina, A.; Morri, D. (2011). *A new methodology to infer the singing activity of an avian community: the Acoustic Complexity Index (ACI).* Ecological Indicators, 11(3), 868–873. https://doi.org/10.1016/j.ecolind.2010.11.005
- **[23]** Kasten, E. P.; Gage, S. H.; Fox, J.; Joo, W. (2012). *The Remote Environmental Assessment Laboratory's acoustic library: an archive for studying soundscape ecology.* Ecological Informatics, 12, 50–67 (índice NDSI). https://doi.org/10.1016/j.ecoinf.2012.08.001
- **[24]** Boelman, N. T.; Asner, G. P.; Hart, P. J.; Martin, R. E. (2007). *Multi-trophic invasion resistance in Hawaii: bioacoustics, field surveys, and airborne remote sensing.* Ecological Applications, 17(8), 2137–2144 (Índice Bioacústico). https://doi.org/10.1890/07-0004.1
- **[25]** Sueur, J.; Pavoine, S.; Hamerlynck, O.; Duvail, S. (2008). *Rapid acoustic survey for biodiversity appraisal.* PLoS ONE, 3(12), e4065 (entropía acústica). https://doi.org/10.1371/journal.pone.0004065
- **[26]** Marques, T. A.; Thomas, L.; Martin, S. W.; Mellinger, D. K.; Ward, J. A.; Moretti, D. J.; Harris, D.; Tyack, P. L. (2013). *Estimating animal population density using passive acoustics.* Biological Reviews, 88(2), 287–309. https://doi.org/10.1111/brv.12001

---

## 14. Acerca de / Contacto

El botón **Acerca** (sección *Info* de la barra, o tecla `?`) abre un panel con esta
información; se cierra con un **clic** o con **Esc**.

- **Creador:** Rodolfo Cárdenas
- **Correo:** rcardenasv@iiap.gob.pe
- **Institución:** Instituto de Investigaciones de la Amazonía Peruana (**IIAP**)
- **Programa Bosques — Laboratorio de Inteligencia Artificial**

**Para qué sirve:** software pensado para **ayudar a generar etiquetas (anotaciones)** y así
**entrenar modelos de IA** en bioacústica / paisaje sonoro. Realza el espectrograma, filtra por
dB y frecuencia, reproduce las bandas seleccionadas, etiqueta sonidos (cajas, polígonos y
anillos), los visualiza en 2D y 3D y los exporta en formato **COCO** y **Raven**.

![Panel “Acerca de / Contacto” (botón **Acerca** o tecla `?`): muestra el creador, el correo de contacto, la institución (IIAP — Programa Bosques, Laboratorio de Inteligencia Artificial) y para qué sirve el software. Se cierra con un clic o con Esc.](man_acerca.png)

---

*IIAP — Instituto de Investigaciones de la Amazonía Peruana.*
