# IIAP SachaAcoustic — workstation bioacústica

**Software pensado para AYUDAR A GENERAR ETIQUETAS (anotaciones) y así ENTRENAR
modelos de IA en bioacústica / paisaje sonoro.** Realza el espectrograma, filtra
por **dB** y **frecuencia**, reproduce las **bandas**, y etiqueta sonidos
(**cajas, polígonos y anillos**). Visualiza en **2D / 3D** y exporta en formato
**COCO** y **Raven**.

Carga audio WAV, calcula el espectrograma y lo realza con el proceso **fiel
(asinh)** elegido en los experimentos. **Sin dependencias externas**: GUI con
Win32 + OpenGL, audio con winmm; todo el procesamiento es código propio.
✅ Compilado y verificado con **w64devkit** (g++ 16.1.0).

## Aplicaciones
| Exe | Qué es |
|---|---|
| **`IIAP_SachaAcoustic.exe`** | Workstation completa: 7 vistas + audio + etiquetado (la principal) |
| `etiquetador.exe` | Etiquetador simple 2D (GDI), export COCO |
| `cli.exe` | Prueba sin ventana (BMP + COCO) |

## `IIAP_SachaAcoustic.exe` — 7 vistas (teclas 1-7)
1. **Espectrograma 2D** — etiquetado de **cajas, polígonos y anillos** + selección para reproducir; realce, filtros y mapas de color.
2. **Terreno 3D** — el espectrograma como relieve; **ejes configurables** (tecla `r` rota la asignación tiempo/frecuencia/dB a X/Y/Z).
3. **Río espectral 3D** — crestas tonales seguidas como hilos/cintas 3D.
4. **Nube de puntos** — cada celda como punto 3D (tiempo/freq/energía), opacidad por energía.
5. **Cascada espectral (Ondas 3D)** — el sonido troceado en segmentos sucesivos apilados en profundidad.
6. **Quiver3D** — campo de glifos/vectores por hilo espectral.
7. **Volumen** — render volumétrico con filtros **locales** de frecuencia y dB propios de la vista.

En todas las vistas 3D: **valores numéricos en cada eje** (s / Hz / dB) sobre un
marco-cubo con los ejes a la izquierda; **playhead** como plano amarillo; la
**resolución 3D** con `Res+`/`Res-`; y las **cajas etiquetadas** se ven como prismas.

**Filtros (botones o teclas):** banda de **frecuencia** `Flo±`/`Fhi±` y banda de
**dB** `dBlo±`/`dBhi±` (quita desde abajo y desde arriba) — afectan a todas las
vistas y a la tira 2D en vivo. El **eje de tiempo (X)** se alarga según la
duración del archivo (caja rectangular, no cubo) y la vista por defecto deja
**t=0 al frente**.

## Etiquetado y exportación
- **Formas:** cajas (bbox), **polígonos** y **anillos/huecos** (polígonos con
  agujeros) para sonidos de contorno complejo.
- **Auto-etiquetado** y selección de banda para acotar el sonido en tiempo +
  frecuencia antes de etiquetar.
- **Exporta** las anotaciones en **COCO** (`.json`) y **Raven** (`.txt` selection
  table) con un solo guardado. Carga etiquetas previas de COCO (autocarga) o de
  Raven (botón **Cargar**).

## Compilar
```bat
cd etiquetador_cpp
build.bat            REM (o: make)
```
Genera `IIAP_SachaAcoustic.exe`, `etiquetador.exe`, `cli.exe`.

## Uso
```
IIAP_SachaAcoustic.exe  [audio.wav]  [carpeta_salida]
```
Si no pasas WAV, ábrelo con `o` (diálogo de archivo) o **arrastrando** el audio a la ventana.

### Controles
**Globales:** `o` abrir WAV · `1`-`7` vista · `ESPACIO` reproducir selección (o todo) · `p` reproducir todo · `.` detener · `s` guardar (COCO + Raven) · `R` cargar Raven · `Ctrl+Z` deshacer · `h` ayuda (consola) · `q`/`ESC` salir.

**Etiquetado (vista 2D o tira inferior, funciona en 3D):**
- **arrastrar** izq. = seleccionar rango (tiempo + frecuencia)
- **clic simple** = posicionar el playhead (dónde empezará a reproducir)
- **clic derecho** = menú contextual: crear etiqueta **bio/antro**, reproducir
  selección, quitar selección; sobre una caja: cambiar clase o borrar
- herramientas: **Seleccionar**, **Nueva** (caja/polígono), **Editar**
  (mover/insertar vértices, anillos), **Cortar**, **Auto** (auto-etiquetar)
- botón **SoloBanda**: ON = reproduce solo la banda (tiempo+frecuencia) de la
  selección; OFF = toda la frecuencia del tiempo seleccionado
- **velocidad de reproducción** ajustable (lento = grave / rápido = agudo)

**Vistas 3D:** arrastrar (área principal) = rotar · rueda = zoom · `r`/Ejes =
intercambia **frecuencia ↔ dB** entre Y y Z (el eje **X siempre es tiempo**).
El **playhead** es una línea que recorre la frecuencia (espectro del instante).
`Play` reproduce la **caja seleccionada** (su tiempo+frecuencia) si hay una.

**Panel inferior (siempre visible):** tira de espectrograma + **oscilograma**.
Puedes **etiquetar arrastrando en la tira** aunque estés en una vista 3D.
Arrastrar selecciona un rango de **tiempo + frecuencia**; `ESPACIO` reproduce
**solo esa banda** (paso-banda). Clic simple = mover el **playhead** (seek).
`k` o botón **Pausa** pausa/reanuda. La línea amarilla marca la reproducción.

**Botones:** toda la funcionalidad está también en la barra de botones superior.

## Estructura
```
etiquetador_cpp/
├── build.bat / Makefile        compilacion (w64devkit)
├── resource.rc, icon.ico       icono embebido (windres)
├── IIAP_SachaAcoustic.exe      app principal (+ etiquetador.exe, cli.exe)
├── src/                        codigo fuente
│   ├── types wav fft spectrogram        datos + STFT
│   ├── enhance magma_lut                realce asinh + paletas
│   ├── autolabel coco raven bmp         auto-etiquetado, export COCO/Raven/BMP
│   ├── audio_play glfont                audio (winmm) + texto GL
│   ├── iiap_sachaacoustic.cpp           workstation (7 vistas) <- principal
│   ├── main_win32.cpp                   etiquetador 2D simple
│   └── main_cli.cpp                     prueba sin GUI
├── docs/        MANUAL.md, MANUAL.pdf, figuras man_*.png, icon.png
├── scripts/     capture_*.ps1 (capturas GUI), limpiar_anillos.py, md2pdf.py
├── screenshots/ capturas de depuracion (cap_*.png)
└── out/         salidas de ejemplo (COCO .json, Raven .txt, BMP)
```
Manual a PDF: `python scripts/md2pdf.py docs/MANUAL.md docs/MANUAL.pdf`.

> Nota: el realce `asinh` (β en `enhance.hpp`) y el auto-etiquetado corren sobre
> el espectro; el audio sintético de prueba se ve ruidoso, con grabaciones
> reales las vistas de hilos (río / quiver) muestran las crestas tonales nítidas.
