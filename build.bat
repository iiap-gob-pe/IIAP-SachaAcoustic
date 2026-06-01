@echo off
REM Compila las apps con w64devkit. Ajusta W64 si esta en otra ruta.
set W64=X:\programas\w64devkit\bin
set PATH=%W64%;%PATH%

echo Compilando icono...
windres resource.rc -O coff -o resource.o
echo Compilando IIAP_SachaAcoustic.exe (2D/3D/mandala/rio/constelacion/ondas/quiver)...
g++ -std=c++17 -O2 -Isrc src\iiap_sachaacoustic.cpp resource.o -o IIAP_SachaAcoustic.exe -lopengl32 -lglu32 -lgdi32 -luser32 -lwinmm -lcomdlg32
if errorlevel 1 goto :err

echo Compilando etiquetador.exe (etiquetador simple GDI)...
g++ -std=c++17 -O2 -Isrc src\main_win32.cpp -o etiquetador.exe -lgdi32 -luser32
if errorlevel 1 goto :err

echo Compilando cli.exe (prueba sin GUI)...
g++ -std=c++17 -O2 -Isrc src\main_cli.cpp -o cli.exe
if errorlevel 1 goto :err

echo.
echo OK. Ejecuta:  IIAP_SachaAcoustic.exe  tu_audio.wav
goto :eof
:err
echo ERROR de compilacion.
