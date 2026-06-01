# IIAP SachaAcoustic — workstation bioacústica (C++ / OpenGL, sin dependencias)

Aplicación para **analizar y etiquetar** paisaje sonoro (biofonía / antropofonía).
Carga audio WAV, calcula el espectrograma y lo realza con el proceso **fiel
(asinh)** elegido en los experimentos. **Sin dependencias externas**: GUI con
Win32 + OpenGL, audio con winmm; todo el procesamiento es código propio.
✅ Compilado y verificado con **w64devkit** (g++ 16.1.0).

## Aplicaciones
| Exe | Qué es |
|---|---|
| **`raven.exe`** | Workstation completa: 5 vistas + audio + etiquetado (la principal) |
| `etiquetador.exe` | Etiquetador simple 2D (GDI), export COCO |
| `cli.exe` | Prueba sin ventana (BMP + COCO) |

## `raven.exe` — 6 vistas (teclas 1-6)
1. **Espectrograma 2D** — etiquetado de cajas + **"hilos" de frecuencia** (seguimiento de crestas) + selección para reproducir.
2. **Terreno 3D** — el espectrograma como relieve; **ejes configurables** (tecla `r` rota la asignación tiempo/frecuencia/dB a X/Y/Z).
3. **Mandala radial** *(viz nueva)* — tiempo=ángulo, frecuencia=radio, energía=color; los ritmos se vuelven simetrías radiales.
4. **Río espectral 3D** *(viz nueva)* — crestas tonales seguidas como hilos/cintas 3D.
5. **Constelación armónica** *(viz nueva)* — picos como puntos 3D + líneas que unen familias armónicas.
6. **Nube de puntos** — cada celda como punto 3D (tiempo/freq/energía), opacidad por energía.
7. **Ondas 3D** — cascada de formas de onda: el sonido troceado en segmentos sucesivos apilados en profundidad.

En todas las vistas 3D: **valores numéricos en cada eje** (s / Hz / dB) sobre un
marco-cubo con los ejes a la izquierda; **playhead** como plano amarillo; la
**resolución 3D** con `Res+`/`Res-`; y las **cajas etiquetadas** se ven como prismas.

**Filtros (botones o teclas):** banda de **frecuencia** `Flo±`/`Fhi±` y banda de
**dB** `dBlo±`/`dBhi±` (quita desde abajo y desde arriba) — afectan a todas las
vistas y a la tira 2D en vivo. El **eje de tiempo (X)** se alarga según la
duración del archivo (caja rectangular, no cubo) y la vista por defecto deja
**t=0 al frente**.

## Compilar
```bat
cd etiquetador_cpp
build.bat            REM (o: make)
```
Genera `raven.exe`, `etiquetador.exe`, `cli.exe`.

## Uso
```
raven.exe  [audio.wav]  [carpeta_salida]
```
Si no pasas WAV, ábrelo con `o` (diálogo de archivo).

### Controles
**Globales:** `o` abrir WAV · `1`-`5` vista · `ESPACIO` reproducir selección (o todo) · `p` reproducir todo · `.` detener · `s` guardar COCO · `h` ayuda (consola) · `q`/`ESC` salir.

**Etiquetado (vista 2D o tira inferior, funciona en 3D):**
- **arrastrar** izq. = seleccionar rango (tiempo + frecuencia)
- **clic simple** = posicionar el playhead (dónde empezará a reproducir)
- **clic derecho** = menú contextual: crear etiqueta **bio/antro**, reproducir
  selección, quitar selección; sobre una caja: cambiar clase o borrar
- botón **Seleccionar** = modo selección · botón **Auto** = auto-etiquetar
- botón **SoloBanda**: ON = reproduce solo la banda (tiempo+frecuencia) de la
  selección; OFF = toda la frecuencia del tiempo seleccionado

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
├── IIAP_SachaAcoustic.exe       app principal (+ etiquetador.exe, cli.exe)
├── src/                        codigo fuente
│   ├── types wav fft spectrogram        datos + STFT
│   ├── enhance magma_lut                realce asinh + paleta
│   ├── autolabel coco bmp               auto-etiquetado, export COCO/BMP
│   ├── audio_play glfont                audio (winmm) + texto GL
│   ├── iiap_sachaacoustic.cpp           workstation (8 vistas) <- principal
│   ├── main_win32.cpp                   etiquetador 2D simple
│   └── main_cli.cpp                     prueba sin GUI
├── docs/        MANUAL.md, MANUAL.pdf, figuras man_v*.png, icon.png
├── scripts/     capture.ps1 (capturas GUI), md2pdf.py (manual -> PDF)
├── screenshots/ capturas de depuracion (cap_*.png)
└── out/         salidas de ejemplo (COCO .json, BMP)
```
Manual a PDF: `python scripts/md2pdf.py docs/MANUAL.md docs/MANUAL.pdf`.

> Nota: el realce `asinh` (β en `enhance.hpp`) y el auto-etiquetado corren sobre
> el espectro; el audio sintético de prueba se ve ruidoso, con grabaciones
> reales las vistas 4/5 muestran hilos y constelación nítidos.
