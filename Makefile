# Makefile para w64devkit (mingw-w64).
# Uso:  make            -> compila las 3 apps
#       make raven      -> solo la workstation tipo Raven (GUI OpenGL)
#       make clean
CXX      := g++
CXXFLAGS := -std=c++17 -O2 -Isrc
GUILIBS  := -lgdi32 -luser32
GLLIBS   := -lopengl32 -lglu32 -lgdi32 -luser32 -lwinmm -lcomdlg32 -lshell32

all: IIAP_SachaAcoustic.exe etiquetador.exe cli.exe

# IIAP SachaAcoustic: espectrograma 2D + 3D + rio + nube + cascada + quiver + volumen
IIAP_SachaAcoustic.exe: src/iiap_sachaacoustic.cpp src/*.hpp resource.o
	$(CXX) $(CXXFLAGS) src/iiap_sachaacoustic.cpp resource.o -o $@ $(GLLIBS)
resource.o: resource.rc icon.ico
	windres resource.rc -O coff -o resource.o

# Etiquetador simple (GDI)
etiquetador.exe: src/main_win32.cpp src/*.hpp
	$(CXX) $(CXXFLAGS) src/main_win32.cpp -o $@ $(GUILIBS)

# Prueba sin GUI
cli.exe: src/main_cli.cpp src/*.hpp
	$(CXX) $(CXXFLAGS) src/main_cli.cpp -o $@

clean:
	rm -f IIAP_SachaAcoustic.exe etiquetador.exe cli.exe
