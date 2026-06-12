# Comandos de envío — IIAP SachaAcoustic

Guía de los **comandos que se le envían** a la workstation de etiquetado `IIAP_SachaAcoustic.exe`:
cómo **compilarla y ejecutarla**, las **teclas y acciones** que entiende, y sobre todo cómo
**automatizarla enviándole mensajes de Windows** (PostMessage) para manejarla por script sin clics manuales.

> Para qué sirve: probar/automatizar el etiquetador (correr un corte, autoetiquetar, guardar) de forma
> reproducible, y tener a mano la referencia de todas las teclas. El bloque de **automatización** es la
> base de los scripts `_verify_*.ps1` que se usan para verificar cada cambio sin tocar el ratón.

---

## 1. Compilar y ejecutar

PATH del compilador: `X:\programas\w64devkit\bin` (g++ 16.1.0, sin OpenCV).

```powershell
# 1) cerrar la app si está abierta (libera el .exe)
Get-Process IIAP_SachaAcoustic -ErrorAction SilentlyContinue | Stop-Process -Force

# 2) compilar SIEMPRE enlazando resource.o (si no, el .exe sale SIN icono)
$env:PATH = "X:\programas\w64devkit\bin;$env:PATH"
windres resource.rc -O coff -o resource.o        # solo si cambió el icono/recurso
g++ -std=c++17 -O2 -Isrc src\iiap_sachaacoustic.cpp resource.o -o IIAP_SachaAcoustic.exe `
    -lopengl32 -lglu32 -lgdi32 -luser32 -lwinmm -lcomdlg32
```

(o `build.bat`, que ya hace todo lo anterior).

**Ejecutar** — `argv[1]` = WAV de entrada, `argv[2]` = carpeta de salida (COCO/Raven):

```powershell
.\IIAP_SachaAcoustic.exe ruta\audio.wav carpeta_salida
```

Al abrir, si en `carpeta_salida` existe `<nombre_audio>.json` (COCO), **carga esas etiquetas**.
Al guardar (tecla `s`) escribe `carpeta_salida\<nombre_audio>.json` (COCO) + `.txt` (Raven).

---

## 2. Comandos que entiende la app (teclas)

Las teclas son **sensibles a mayúsculas** (p. ej. `v` ≠ `V`, `i` ≠ `I`).

### Archivo / sesión
| Tecla | Acción | Para qué |
|---|---|---|
| `o` | Abrir audio (diálogo) | cargar un WAV |
| `R` | Cargar tabla Raven | importar selecciones |
| `s` | **Guardar** (COCO + Raven) | exportar etiquetas |
| `q` | Salir | cerrar la app |
| `?` | Acerca de / Contacto | créditos y para qué sirve |

### Vistas (1–7)
`1` Espectro 2D · `2` Terreno 3D · `3` Río espectral · `4` Nube de puntos · `5` Cascada · `6` Quiver 3D · `7` Volumen.

### Herramientas de etiquetado
| Tecla | Herramienta |
|---|---|
| `S` | Seleccionar |
| `Y` | Caja (bbox) — arrastrar para crearla |
| `P` | Polígono |
| `E` | Editar forma (vértices/anillos) |
| `X` | **Cortar** (corte geométrico libre) |
| `N` | Nueva clase (captura texto: escribir + Enter) |
| `a` | **Auto-etiquetar** (sobre lo filtrado) |
| `l` | Crear caja con la selección activa |
| `b` / `n` | Clase activa = bio / antro |
| `z` | Asignar la clase activa a la etiqueta seleccionada |
| `d` / `Supr` | Borrar (etiqueta / vértices marcados) |
| `c` | Limpiar (borrar todas) |
| `O` | Ocultar / mostrar etiquetas |

### Reproducción y navegación
| Tecla | Acción |
|---|---|
| `Espacio` | Play/Pausa de la ventana o la selección |
| `p` | Reproducir todo (con filtros) |
| `k` | Pausa | 
| `.` | Stop |
| `v` / `V` | Velocidad −0.1× / +0.1× |
| `T` | Ver todo (resetea zoom tiempo+frecuencia) |
| `Z` / `U` | Zoom + / Zoom − (vista 2D) |
| `]` `+` `=` / `[` `-` `_` | Más / menos resolución (recalcula 2D y 3D) |

### Filtros (en vivo, afectan visual y sonido)
| Tecla | Filtro |
|---|---|
| `u` / `j` | Frecuencia superior + / − |
| `i` / `m` | Frecuencia inferior + / − |
| `y` / `g` | Piso de dB + / − |
| `w` / `e` | Techo de dB + / − |
| `f` | Fijar "Señal" (el techo escuchado pasa a ser piso) |
| `I` | Histograma sobre el espectro |
| `M` | Combo de mapa de color (flechas + Enter) |

### Otras
`H` hilos completos (río) · `G` glifos completos (quiver) · `B` solo-banda · `t` modo hilo ·
`r` (en 3D) intercambia ejes freq/dB · **Ctrl+Z** deshacer · `Enter` cierra polígono a mano · `Esc` cancela el modo.

### Ratón
- **Clic-izq + arrastre** en el espectro 2D o en la tira inferior = seleccionar tiempo+frecuencia (o crear caja/polígono según la herramienta).
- **Clic-izq simple** = mover el playhead (seek) / seleccionar una etiqueta o vértice.
- **Clic-der** = menú contextual (en Seleccionar/Editar) o ejecutar el corte (en Cortar).
- **Clic-der + arrastre** (en Editar) = rectángulo que marca varios vértices (Supr los borra).
- **Rueda** sobre el espectro 2D = zoom; sobre la tira inferior = mover la ventana de tiempo.
- **Botón central** o **Ctrl+clic-izq** = mover/redimensionar la ventana de tiempo.

---

## 3. Automatización: enviar entrada por mensajes de Windows (PostMessage)

`PostMessage` deja **enviar teclas y ratón a la ventana sin traerla al frente**, ideal para scripts.
Las teclas (`WM_CHAR`) y el ratón (`WM_*BUTTON*`/`WM_MOUSEMOVE`) funcionan en segundo plano.

### 3.1 Preparación (P/Invoke en PowerShell)

```powershell
$sig = @'
using System;
using System.Runtime.InteropServices;
public class W {
  [DllImport("user32.dll")] public static extern int PostMessage(IntPtr h,uint m,IntPtr w,IntPtr l);
  [DllImport("user32.dll")] public static extern bool SetWindowPos(IntPtr h,IntPtr a,int x,int y,int cx,int cy,uint f);
  [StructLayout(LayoutKind.Sequential)] public struct RECT { public int left, top, right, bottom; }
  [DllImport("user32.dll")] public static extern bool GetClientRect(IntPtr h, out RECT r);
}
'@
Add-Type $sig

# lanzar y obtener el HWND
$p = Start-Process .\IIAP_SachaAcoustic.exe -ArgumentList @("audio.wav","salida") -PassThru
Start-Sleep -Seconds 4
$h = $p.MainWindowHandle

# (opcional) forzar un tamaño de ventana CONOCIDO -> coordenadas reproducibles
[W]::SetWindowPos($h,[IntPtr]0,80,40,1320,860,0x14) | Out-Null   # 0x14 = SWP_NOZORDER|SWP_NOACTIVATE
$rc = New-Object W+RECT; [W]::GetClientRect($h,[ref]$rc) | Out-Null
$cW = $rc.right; $cH = $rc.bottom
```

### 3.2 Códigos de mensaje

| Mensaje | Código | wParam | lParam |
|---|---|---|---|
| `WM_CHAR` | `0x0102` | código del **carácter** (`'S'`=0x53, `'s'`=0x73) | 0 |
| `WM_KEYDOWN` | `0x0100` | **virtual key** (Supr=`0x2E`, Enter=`0x0D`, Esc=`0x1B`, flechas `0x25..0x28`) | 0 |
| `WM_LBUTTONDOWN` | `0x0201` | botones (1=MK_LBUTTON) | `(y<<16)\|x` **cliente** |
| `WM_LBUTTONUP` | `0x0202` | 0 | `(y<<16)\|x` |
| `WM_MOUSEMOVE` | `0x0200` | 1 si se arrastra | `(y<<16)\|x` |
| `WM_RBUTTONDOWN` | `0x0204` | 0 | `(y<<16)\|x` |
| `WM_RBUTTONUP` | `0x0205` | 0 | `(y<<16)\|x` |
| `WM_MOUSEWHEEL` | `0x020A` | `(delta<<16)` (delta=±120) | `(y<<16)\|x` **de PANTALLA, con signo** |

Helpers:

```powershell
function Key($h,$ch){ [W]::PostMessage($h,0x0102,[IntPtr]$ch,[IntPtr]0)|Out-Null; Start-Sleep -Milliseconds 400 }
function LP([int]$x,[int]$y){ return [IntPtr](($y -shl 16) -bor ($x -band 0xFFFF)) }   # empaqueta coords cliente

Key $h ([int][char]'a')   # auto-etiquetar
Key $h ([int][char]'s')   # guardar
[W]::PostMessage($h,0x0100,[IntPtr]0x2E,[IntPtr]0)|Out-Null   # Supr (VK_DELETE)
```

### 3.3 Mapa de coordenadas: cliente ↔ espectrograma

El área del espectro 2D va de `x∈[52, cW-88]`, dejando `MARG_L=52` a la izquierda y `BARS_RIGHT=80 (+8)` a la
derecha para las barras de filtro. Con la vista completa (columna 0..W, W=255 por defecto):

```
columna -> x (cliente):   x  = 52 + col/255 * (cW - 140)
x -> columna:             col = (x - 52) / (cW - 140) * 255
```

El eje vertical (filas/frecuencia) va de `plotY0 = HUD(20) + alto_toolbar + 10` hasta `plotY1 = cH - 174 - 22`.
Como `alto_toolbar` varía con el ancho de la ventana, conviene **forzar el tamaño** (3.1) y, para puntos que deben
quedar fuera del polígono, recordar que `main_to_spec` **recorta** a la fila 0..H (enviar `y=8` cae en la fila 0,
`y=cH-8` en la última fila).

### 3.4 Ejemplo completo: cortar un polígono por la mitad

```powershell
$xc = [int](52 + 127.0/255.0*($cW-140))   # columna central
$yMid = [int](0.45*$cH)                    # ~centro vertical (dentro del polígono)

Key $h ([int][char]'X')                                              # modo Cortar
[W]::PostMessage($h,0x0201,[IntPtr]1,(LP $xc $yMid))|Out-Null         # LBUTTONDOWN (inicia el trazo, DENTRO del polígono)
Start-Sleep -Milliseconds 200
[W]::PostMessage($h,0x0200,[IntPtr]1,(LP $xc 8))|Out-Null             # MOUSEMOVE arriba  (recorta a fila 0, sobrepasa el polígono)
Start-Sleep -Milliseconds 160
[W]::PostMessage($h,0x0200,[IntPtr]1,(LP $xc ($cH-8)))|Out-Null       # MOUSEMOVE abajo   (recorta a la última fila)
Start-Sleep -Milliseconds 160
[W]::PostMessage($h,0x0202,[IntPtr]0,(LP $xc ($cH-8)))|Out-Null       # LBUTTONUP (mantiene el trazo dibujado)
[W]::PostMessage($h,0x0204,[IntPtr]0,(LP $xc $yMid))|Out-Null         # RBUTTONDOWN -> EJECUTA el corte
Key $h ([int][char]'s')                                              # guardar
```

Para **abrir un anillo**: dibujar el trazo desde **dentro del hueco** hacia **afuera del borde** (cruza el anillo y
el exterior una vez cada uno) — el corte conecta el hueco con el borde y deja 1 sola pieza sin ese hueco.

---

## 4. Notas y trampas (aprendidas a los golpes)

- **PostMessage de teclas/ratón NO necesita foreground** (sirve en segundo plano). Excepción: el **menú contextual**
  de clic-der (en Seleccionar/Editar) usa `TrackPopupMenu`, que **bloquea** y exige ratón real; no responde a
  PostMessage. En modo **Cortar** el clic-der no abre menú (solo ejecuta), por eso sí se automatiza.
- **No** uses `mouse_event`/`SetForegroundWindow` para esto: roban el foco y fallan en segundo plano.
- **Captura de pantalla**: `CopyFromScreen` devuelve **azul** (`#00487E`) si la ventana GL no está limpiamente al
  frente — por eso la verificación se hace **leyendo el COCO de salida**, no por imagen.
- **stdout** está bufferizado: redirige con `-RedirectStandardOutput archivo.txt` y léelo **después** de cerrar la app
  (al matar el proceso de golpe se puede perder lo no vaciado).
- El **COCO de salida se sobreescribe en cada corrida** (autosave + `s`): **regenera el `.json`** antes de cada prueba
  o el estado se acumula (parece que "duplica" o "ya estaba cortado").
- La ventana **abre con tamaño variable** según el monitor (p. ej. 960×420 o 1304×821): **fuerza** el tamaño con
  `SetWindowPos` y lee el real con `GetClientRect` antes de calcular coordenadas.
- Compila **siempre con `resource.o`** (o `build.bat`) para no perder el icono.
- Coordenadas de `WM_MOUSEWHEEL` son de **pantalla y con signo** (en multimonitor pueden ser negativas): empaquetar
  con extensión de signo, no como unsigned.
