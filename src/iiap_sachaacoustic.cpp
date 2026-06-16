// raven.cpp - Workstation bioacustica tipo Raven (Win32 + OpenGL, sin OpenCV).
//
// Vistas (1-7): 1) Espectrograma 2D  2) Terreno 3D (ejes configurables)
//   3) Rio espectral 3D  4) Nube de puntos  5) Ondas 3D  6) Quiver3D  7) Volumen.
// Panel inferior SIEMPRE visible: tira de espectrograma (para etiquetar incluso
// en 3D) + oscilograma. Barra de BOTONES arriba. Playhead con pausa/seek.
// Reproduccion de la seleccion recortada en TIEMPO y FRECUENCIA (paso-banda).
#define NOMINMAX
#include <windows.h>
#include <GL/gl.h>
#include <GL/glu.h>
#include <commdlg.h>
#include <shellapi.h>     // DragAcceptFiles / WM_DROPFILES (arrastrar y soltar audio)
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>
#include <iostream>
using std::min; using std::max;

#include "types.hpp"
#include "wav.hpp"
#include "spectrogram.hpp"
#include "enhance.hpp"
#include "autolabel.hpp"
#include "coco.hpp"
#include "raven.hpp"
#include "bmp.hpp"
#include "audio_play.hpp"
#include "glfont.hpp"

struct Hilo { std::vector<int> col, row; };
struct Boton { int x, y, w, h; std::string label; int key; int icon; std::string tip; };

// Herramientas de etiquetado (A.tool)
// Herramientas: seleccionar, etiquetar bounding-box, etiquetar poligono, editar, cortar
enum { T_SELECT = 0, T_BBOX = 1, T_POLY = 2, T_EDIT = 3, T_CUT = 4, T_MERGE = 5, T_ERASER = 6 };

struct AppS {
    AudioData audio; int sr = 0;
    SpecParams P;
    Img spec, enh;
    GLuint tex = 0;
    std::vector<Det> dets;
    std::vector<Hilo> hilos, rios;
    std::vector<std::array<int,2>> picos;
    std::vector<float> envMin, envMax;   // oscilograma precalculado
    int NB = 2048;

    int view = 1, clase_activa = CLASE_BIO, sel = -1;
    float K = 4.0f; int area_min = 30;

    // --- etiquetado dinamico (poligono / bounding box) ---
    std::vector<LabelClass> classes = default_classes();
    int tool = T_SELECT;             // T_SELECT/BBOX/POLY/EDIT/CUT
    bool shape_poly = true;          // forma activa de etiquetado (true=poligono, false=bbox); la usa Auto
    int autoBuffer = 2;             // buffer (px) de suavizado para auto-poligono
    float gain = 1.0f;               // ganancia de reproduccion (1=normalizado; >1 sube mas)
    float playSpeed = 1.0f;          // velocidad de reproduccion (1.0x); <1 lento+grave (expansion temporal), >1 rapido
    bool hide_labels = false;        // ocultar etiquetas (cajas/poligonos) del dibujo
    float volFLo=0.f, volFHi=1.f, volDbMin=0.f, volDbMax=1.f;  // filtros LOCALES de la vista Volumen
    std::vector<int> polyX, polyY;   // poligono en construccion (modo poligono a mano)
    std::vector<int> cutX, cutY;     // trazo de corte LIBRE (modo Cortar); clic-der ejecuta
    bool naming = false; std::string nameBuf;   // modo "crear etiqueta": captura de texto
    bool dirty = false;              // hay cambios sin guardar -> autosave
    unsigned long long lastSig = 0; bool sigInit = false;   // firma de etiquetas para autosave
    bool buffering = false; int bufSel = -1; Det bufOrig; int bufOrigBuf = 0;  // modal de buffer (con copia para cancelar)
    int editVert = -1;               // poligono: vertice en edicion ; bbox: 100+manija, 108 mover
    int editHole = -1;               // editar: -1 = contorno exterior ; >=0 = indice del HUECO/anillo en edicion
    int lastEditC = 0, lastEditR = 0;// ultima posicion (spec) al mover un poligono (para trasladar)
    int vr0 = 0, vr1 = 0;            // ventana VERTICAL (filas/freq) visible en la vista 2D; vr1=0 -> todo
    bool playMask = false;           // reproduccion limitada a la mascara de etiquetas (vista Volumen)
    int selDet = -1, selVert = -1;   // vertice marcado (selDet,selVert) de un poligono
    int selVertHole = -1;            // contorno del vertice marcado: -1 = exterior ; >=0 = indice del HUECO/anillo
    std::vector<std::pair<int,int>> selVerts;   // vertices marcados con rectangulo de BOTON DERECHO (Editar): {anillo,k}, anillo=-1 exterior / >=0 indice de hueco; todos del A.selDet. Supr los borra
    bool rbDrag=false; int rbx0=0,rby0=0,rbx1=0,rby1=0;   // rectangulo de seleccion de vertices con BOTON DERECHO (modo Editar)
    int pendEdge=-1, pendEdgeC=0, pendEdgeR=0;   // EDITAR: insercion de punto PENDIENTE (solo si el clic NO se arrastra)
    int pickHole=-1, pendEdgeHole=-1;            // EDITAR: -1 = arista del contorno exterior ; >=0 = arista del HUECO/anillo
    std::vector<std::vector<Det>> undo;   // historial para Ctrl+Z (instantaneas de A.dets)
    std::vector<std::vector<Det>> redo;   // historial para Ctrl+Y (rehacer)

    // --- optimizaciones: mascara espacial, cache de hash, cache de lista ---
    Mask labelMask;                      // mascara W×H: 1=pixel dentro de alguna etiqueta
    bool maskDirty = true;               // reconstruir mascara cuando A.dets cambia
    unsigned long long cachedLabelSig = 0;  // hash cacheado de A.dets
    bool labelsSigDirty = true;          // recalcular hash cuando A.dets cambia
    std::vector<int> cachedListOrder;    // orden de display cacheado
    bool listOrderDirty = true;          // reconstruir cuando cambia A.dets o listSortSize
    std::vector<bool> listSelBitmap;     // bitmap: listSelBitmap[i]=true si i esta en listSel
    bool listSelDirty = true;            // reconstruir cuando cambia listSel
    unsigned long long genCounter = 0;   // generation counter para build_rios
    std::vector<unsigned long long> usedGen;  // vector de generaciones para evitar allocs

    // --- borrador temporal (mascara por filtro) ---
    std::vector<uint8_t> eraserMask;     // mascara W*H: 0=normal, 1=borrado
    int eraserRadius = 5;                // radio del pincel (ajustable con rueda)
    bool eraserDragging = false;         // clic izq presionado dibujando
    int eraserPrevC = -1, eraserPrevR = -1;  // ultimo pixel de spec dibujado
    float prevDbMin = 0.f, prevDbMax = 1.f;  // para detectar cambios de filtro
    float prevFLo = 0.f, prevFHi = 1.f;

    // seleccion en coords de espectro (tiempo c, frecuencia r)
    int sc0 = -1, sc1 = -1, sr0 = -1, sr1 = -1;
    int cursor_col = 0;          // posicion para empezar a reproducir / seek

    bool dragging = false; int dx0, dy0, dx1, dy1; int dragRegion = 0;
    bool modo_hilo = false;
    bool solo_banda = true;     // reproducir solo la banda (freq) de la seleccion
    bool rio_completo = false;  // rio: mostrar el hilo COMPLETO si algun punto pasa el filtro
    bool quiver_completo = true;// quiver: glifos completos por hilo (vs solo puntos que pasan)
    int mergeFirst = -1;        // primera etiqueta seleccionada para unir (-1 = ninguna)
    int mx = 0, my = 0;         // posicion del raton (para tooltips)

    float yaw = -40, pitch = 22, dist = 4.0f; int axperm = 0;
    float dbAxisLo = 0.f, dbAxisHi = 1.f;  // eje dB en 3D: rango = banda del FILTRO (barras) + margen
    float fAxisLo = 0.f, fAxisHi = 1.f;    // eje frecuencia en 3D: rango = banda del FILTRO (barras) + margen
    // contexto de la reproduccion actual (para re-aplicar filtros en TIEMPO REAL sin cortar)
    int playC1 = 0; bool playUseSel = false; float playSelLo = 0.f, playSelHi = 1.f; bool playCtx = false;
    bool refilterPending = false;     // un filtro cambio mientras suena -> re-renderizar desde el playhead
    float tlen = 2.5f;          // longitud del eje tiempo (segun duracion)
    float crosslen = 1.7f;      // tamano de los ejes freq/dB (mas grandes)
    float ax[3] = {2.5f,1.7f,1.7f};  // escala del cubo 3D por eje espacial X,Y,Z (manijas)
    int scaleAxis = -1;         // eje que se esta redimensionando con la manija
    double adx = 0, ady = 0;    // direccion en pantalla del eje arrastrado
    int vc0 = 0, vc1 = 0;       // ventana de tiempo visible (columnas); vc1=0 -> todo
    int navMode = 0;            // arrastre del cuadro: 0 nada,1 borde izq,2 borde der,3 mover
    int navStartCol = 0, navVc0 = 0, navVc1 = 0;
    int res3d = 200;            // resolucion de las vistas 3D (mayor = mas fino)
    float fLo = 0.f, fHi = 1.f;  // filtro de frecuencia (fraccion 0..1)
    float dbMin = 0.f, dbMax = 1.f;  // filtro de dB/energia: banda [min,max]
    int cmap = 0;                // mapa de color del espectrograma (indice 0..N-1)
    bool cmapOpen = false;       // combo de mapa de color desplegado
    bool showAbout = false;      // panel "Acerca de / Contacto"
    bool listOpen = false;            // panel LISTA de todas las etiquetas
    std::vector<int> listSel;         // indices de dets seleccionados en la lista (se resaltan en 2D)
    int listCursor = -1, listAnchor = -1, listScroll = 0;   // fila actual / ancla (Shift) / scroll de la lista
    bool listSortSize = false;        // lista ordenada por TAMANO (area) desc; false = por id
    unsigned char cmapLUT[256][3] = {};   // LUT activa (se rellena con build_cmap segun cmap)
    int cW = 1280, cH = 760, toolbar_h = 28;
    int panelH = 174;           // alto del panel inferior (tira espectro + oscilograma)
    std::vector<Boton> botones;
    std::string out_dir = ".";
    std::string fname = "(sin archivo)";   // nombre del WAV cargado
};
static AppS A;
static AudioPlayer PLAYER;
static GLFont FONT;
static double g_mv[16], g_pj[16]; static int g_vp[4];   // matrices 3D (para proyectar manijas)
static HDC g_hdc = 0;                                   // para mostrar el indicador de carga
static HWND g_hwnd = 0;                                 // ventana principal (dueña de los dialogos)
static bool g_dialogOpen = false;                       // hay un dialogo Abrir/Cargar abierto -> no abrir otro
static void show_busy(const std::string& msg);         // indicador de carga (def. mas abajo)
static void layout_botones();                           // (def. mas abajo) recalcula barra/paleta
static unsigned long long labels_sig();                 // (def. mas abajo) firma de etiquetas (autosave)
// Invalida TODOS los flags de cache dependientes de A.dets.
// Llamar despues de cualquier mutacion de A.dets (push_back, erase, asignacion, etc.)
static void invalidate_dets_caches() {
    A.maskDirty = true;
    A.labelsSigDirty = true;
    A.listOrderDirty = true;
    A.listSelDirty = true;
}
static const char* CLS = "RavenLikeGL";

static const int HUD_H = 20;
static const int SCROLL_H = 13;   // alto de la barra de scroll horizontal (sobre la base del oscilograma)
static int STRIP_H_(){ return (int)(A.panelH*0.62f); }   // tira espectro
static int OSC_H_(){ return A.panelH - STRIP_H_(); }      // oscilograma
#define STRIP_H (STRIP_H_())
#define OSC_H (OSC_H_())
static int main_y0() { return HUD_H + A.toolbar_h; }
static int panel_y0() { return A.cH - (OSC_H + STRIP_H); }
static int main_h()   { return max(40, panel_y0() - main_y0()); }

// barras a la derecha: frecuencia (extremo der), dB, y ganancia/volumen
static const int BAR_W = 16, BAR_GAP = 10, BARS_RIGHT = 3*BAR_W+2*BAR_GAP+12;
static const float GAIN_MAX = 4.0f;     // ganancia maxima del control de volumen
static const int BUF_MAX = 12;          // buffer maximo (px) del modal de etiqueta poligono
struct Bar { float x0,x1,ytop,ybot; };
static Bar bar_at(int i){ float x1=A.cW-8-i*(BAR_W+BAR_GAP), x0=x1-BAR_W; return {x0,x1,(float)main_y0()+18,(float)panel_y0()-8}; }
static Bar bar_freq(){ return bar_at(0); }
static Bar bar_db(){ return bar_at(1); }
static Bar bar_gain(){ return bar_at(2); }
static float bar_y(const Bar&b,float v){ return b.ybot-v*(b.ybot-b.ytop); }      // v=0 abajo, v=1 arriba
static float bar_v(const Bar&b,float y){ float v=(b.ybot-y)/(b.ybot-b.ytop); return v<0?0:(v>1?1:v); }
// area de dibujo del espectro 2D CON MARGENES (para etiquetar tambien en los bordes y
// dejar sitio a los ejes y a las barras de filtro)
static const int MARG_L=52, MARG_T=10, MARG_B=22;
static float plotX0(){ return (float)MARG_L; }
static float plotX1(){ return (float)(A.cW-BARS_RIGHT-8); }
static float plotY0(){ return (float)(main_y0()+MARG_T); }
static float plotY1(){ return (float)(panel_y0()-MARG_B); }
// filtros que controlan las barras: GLOBALES, salvo la vista Volumen (V7), que tiene
// sus propios filtros LOCALES (solo afectan a esa vista).
static float& flt_fLo(){ return A.view==7?A.volFLo:A.fLo; }
static float& flt_fHi(){ return A.view==7?A.volFHi:A.fHi; }
static float& flt_dbMin(){ return A.view==7?A.volDbMin:A.dbMin; }
static float& flt_dbMax(){ return A.view==7?A.volDbMax:A.dbMax; }
static void spec_to_main(float c,float r,float&sx,float&sy);   // def. mas abajo
static void sync_bbox_from_poly(Det&d);                        // def. mas abajo
static void push_undo();                                       // (def. mas abajo) snapshot para Ctrl+Z
static bool list_is_sel(int i);                                // (def. mas abajo) ¿el det i esta en la seleccion de la LISTA?
// modal de buffer: rectangulo de la pista del slider {tx0,tx1,ty}
static void bufslider(float& tx0,float& tx1,float& ty){ float w=400,x=A.cW*0.5f-w*0.5f,y=A.cH*0.5f-45;
    tx0=x+24; tx1=x+w-24; ty=y+56; }

// X SIEMPRE = tiempo (dim 0). Rotar solo intercambia freq(1)/dB(2) entre Y y Z.
// dB SIEMPRE vertical (Y). Default: X=Tiempo (comparte la pared DELANTERA con dB -> "L"
// al frente), Z=Frecuencia (profundidad). 'r' intercambia tiempo/freq en el piso.
static const int PERM[2][3] = {{0,2,1},{1,2,0}};  // {X=t,Y=dB,Z=freq} , {X=freq,Y=dB,Z=t}
static const char* DIMN[3] = {"tiempo","freq","dB"};

// ----- MAPAS DE COLOR del espectrograma (10 paletas, enfocadas a resaltar señales) -----
// Cada una sirve mejor para un caso (ver nombres). build_cmap() rellena A.cmapLUT[256] y tanto
// la textura 2D (upload_texture) como las vistas 3D (colorf) leen esa LUT activa.
static const char* CMAP_NAMES[] = {
    "Magma (general)", "Turbo (detalle/debiles)", "Jet (contraste cromatico)",
    "Caliente (senales fuertes)", "Grises (neutro)", "Grises invert. (impresion)",
    "Verde fosforo (clasico)", "Hielo (tonos puros)", "Realce debil (gamma)", "Bandas (umbral)" };
static const int CMAP_N = 10;
static inline float cmcl(float x){ return x<0.f?0.f:(x>1.f?1.f:x); }
static void cmap_eval(int idx, float v, unsigned char& R, unsigned char& G, unsigned char& B){
    v=cmcl(v); float r=0,g=0,b=0;
    auto magma=[&](float t){ int i=(int)(cmcl(t)*255.f+0.5f); R=MAGMA_LUT[i][0];G=MAGMA_LUT[i][1];B=MAGMA_LUT[i][2]; };
    switch(idx){
      case 0: magma(v); return;                                                    // Magma
      case 1: {                                                                     // Turbo (aprox. polinomica)
          r=0.13572138f+v*(4.61539260f+v*(-42.66032258f+v*(132.13108234f+v*(-152.94239396f+v*59.28637943f))));
          g=0.09140261f+v*(2.19418839f+v*(4.84296658f+v*(-14.18503333f+v*(4.27729857f+v*2.82956604f))));
          b=0.10667330f+v*(12.64194608f+v*(-60.58204836f+v*(110.36276771f+v*(-89.90310912f+v*27.34824973f)))); break; }
      case 2: r=cmcl(1.5f-std::fabs(4*v-3)); g=cmcl(1.5f-std::fabs(4*v-2)); b=cmcl(1.5f-std::fabs(4*v-1)); break;  // Jet
      case 3: r=cmcl(3*v); g=cmcl(3*v-1); b=cmcl(3*v-2); break;                     // Caliente (hot)
      case 4: r=g=b=v; break;                                                       // Grises
      case 5: r=g=b=1-v; break;                                                     // Grises invertido
      case 6: g=std::pow(v,0.7f); r=0.10f*v; b=0.18f*v*v*v; break;                  // Verde fosforo
      case 7: b=cmcl(2*v); g=cmcl(2*v-0.6f); r=cmcl(2*v-1.3f); break;               // Hielo (azul->cian->blanco)
      case 8: magma(std::sqrt(v)); return;                                          // Realce debil (gamma 0.5 sobre magma)
      case 9: magma(std::floor(v*8.f)/8.f+0.06f); return;                           // Bandas (posterizado: contornos de igual energia)
      default: magma(v); return;
    }
    R=(unsigned char)(cmcl(r)*255.f+0.5f); G=(unsigned char)(cmcl(g)*255.f+0.5f); B=(unsigned char)(cmcl(b)*255.f+0.5f);
}
static void build_cmap(){ for(int k=0;k<256;++k){ unsigned char R,G,B; cmap_eval(A.cmap,(float)k/255.f,R,G,B); A.cmapLUT[k][0]=R;A.cmapLUT[k][1]=G;A.cmapLUT[k][2]=B; } }
static void colorf(float v, float& r, float& g, float& b) {
    v = v < 0 ? 0 : (v > 1 ? 1 : v); int i = (int)(v * 255.0f + 0.5f);
    r = A.cmapLUT[i][0]/255.f; g = A.cmapLUT[i][1]/255.f; b = A.cmapLUT[i][2]/255.f;
}
static size_t next_pow2(size_t n){ size_t p=1; while(p<n) p<<=1; return p; }
// filtro: pasa si freq-fraccion en [fLo,fHi] y energia >= dbMin
static bool pass_filt(float f,float e){ return f>=A.fLo && f<=A.fHi && e>=A.dbMin && e<=A.dbMax; }
static void upload_texture();   // forward declaration (def. mas abajo)

// ---- borrador temporal ----
static void eraser_init(){
    int W=A.spec.W, H=A.spec.H;
    A.eraserMask.assign((size_t)W*H, 0);
    A.prevDbMin=A.dbMin; A.prevDbMax=A.dbMax; A.prevFLo=A.fLo; A.prevFHi=A.fHi;
}
static void eraser_paint(int c, int r){
    int W=A.spec.W, H=A.spec.H;
    if(W<1||H<1||A.eraserMask.empty()) return;
    int rad=A.eraserRadius;
    for(int dr=-rad;dr<=rad;++dr){
        for(int dc=-rad;dc<=rad;++dc){
            if(dc*dc+dr*dr>rad*rad) continue;
            int rr=r+dr, cc=c+dc;
            if(rr>=0&&rr<H&&cc>=0&&cc<W) A.eraserMask[(size_t)rr*W+cc]=1;
        }
    }
}
static void eraser_line(int c0,int r0,int c1,int r1){
    int dx=std::abs(c1-c0), sx=c0<c1?1:-1;
    int dy=-std::abs(r1-r0), sy=r0<r1?1:-1;
    int err=dx+dy;
    for(;;){
        eraser_paint(c0,r0);
        if(c0==c1&&r0==r1) break;
        int e2=2*err;
        if(e2>=dy){err+=dy;c0+=sx;}
        if(e2<=dx){err+=dx;r0+=sy;}
    }
}
static void eraser_reset_if_filter_changed(){
    if(A.dbMin!=A.prevDbMin||A.dbMax!=A.prevDbMax||A.fLo!=A.prevFLo||A.fHi!=A.prevFHi){
        std::fill(A.eraserMask.begin(),A.eraserMask.end(),0);
        A.prevDbMin=A.dbMin; A.prevDbMax=A.dbMax; A.prevFLo=A.fLo; A.prevFHi=A.fHi;
        upload_texture();   // re-subir textura SIN la mascara
    }
}

// ---- clases dinamicas ----
static void class_color(int c, float& r, float& g, float& b){
    if(c>=0 && c<(int)A.classes.size()){ const LabelClass& L=A.classes[c];
        r=L.r/255.f; g=L.g/255.f; b=L.b/255.f; }
    else { r=g=b=0.7f; }
}
static const char* class_name(int c){ return (c>=0&&c<(int)A.classes.size())?A.classes[c].name.c_str():"?"; }
static void add_class(const std::string& nm){
    static const unsigned char PAL[][3]={{230,120,60},{200,90,220},{250,210,70},{90,160,250},
        {240,80,120},{120,230,160},{200,200,200},{250,140,180}};
    const unsigned char* p=PAL[A.classes.size()%8];
    A.classes.push_back({nm,p[0],p[1],p[2]}); A.clase_activa=(int)A.classes.size()-1;
}
// ventana de tiempo visible (columnas)
static int vlo(){ return max(0,A.vc0); }
static int vhi(){ int b=(A.vc1>A.vc0)?A.vc1:A.spec.W; return min(A.spec.W,b); }
// barra de SCROLL horizontal (mover la ventana en el tiempo). Banda en la base del oscilograma.
static bool in_scroll(int my){ return my>=A.cH-SCROLL_H && my<=A.cH; }
static void scroll_center(int mx){ int W=A.spec.W; if(W<1)return; int w=vhi()-vlo(); if(w>=W)return;   // ventana completa: nada que desplazar
    int center=(int)((double)mx/max(1,A.cW)*W); int v0=center-w/2; v0=max(0,min(W-w,v0)); A.vc0=v0; A.vc1=v0+w; }
static void scroll_pan(int dir){ int W=A.spec.W; if(W<1)return; int w=vhi()-vlo(); if(w>=W)return;       // dir>0 derecha, <0 izquierda
    int step=max(1,(int)(w*0.15)); int v0=max(0,min(W-w,vlo()+dir*step)); A.vc0=v0; A.vc1=v0+w; }
static float tnorm(int c){ int a=vlo(),b=vhi(); return (float)(c-a)/(float)max(1,b-a-1); }  // tiempo norm en la ventana
static bool cin(int c){ return c>=vlo() && c<vhi(); }
// ventana VERTICAL (filas/frecuencia) visible en la vista 2D
// Filas (frecuencia) visibles en la vista 2D = INTERSECCION de la ventana de zoom vertical
// (vr0/vr1) con la BANDA DEL FILTRO de frecuencia (fLo/fHi). Asi, al bajar el filtro de
// frecuencia, el plot 2D, su eje de frecuencia y los overlays se ajustan a la banda filtrada.
// f=(H-1-r)/(H-1)  =>  fHi (freq alta)=fila superior (rTop) ; fLo (freq baja)=fila inferior.
static int rlo(){ int H=A.spec.H; int z=max(0,A.vr0); if(H<2)return z;
    int rf=max(0,(int)((H-1)*(1.f-A.fHi))); return max(rf,z); }
static int rhi(){ int H=A.spec.H; int b=(A.vr1>A.vr0)?A.vr1:H; b=min(H,b); if(H<2)return b;
    int rf=min(H,(int)((H-1)*(1.f-A.fLo))+1); int r=min(rf,b); int lo=rlo();
    return r>lo?r:min(H,lo+1); }                              // garantiza rango no-degenerado

// ---------------- analisis: crestas, picos, hilos, oscilograma ----------
static void build_rios() {
    A.rios.clear(); const Img& e = A.enh; int W=e.W,H=e.H; float thr=0.35f;
    std::vector<Hilo> act;
    ++A.genCounter;  // generation counter: evita limpiar el vector used en cada columna
    for (int c=0;c<W;++c){
        std::vector<int> pk;
        for(int r=1;r<H-1;++r){ float v=e.at(r,c); if(v>thr&&v>=e.at(r-1,c)&&v>=e.at(r+1,c)) pk.push_back(r);}
        // Generation counter en vez de vector<bool> used: misma logica, cero allocs
        for(int pr:pk){ int best=-1,bd=6; for(size_t k=0;k<act.size();++k){ if(A.usedGen[k]==A.genCounter)continue; int d=std::abs(act[k].row.back()-pr); if(d<bd){bd=d;best=(int)k;}}
            if(best>=0){act[best].col.push_back(c);act[best].row.push_back(pr);A.usedGen[best]=A.genCounter;}
            else{Hilo h;h.col.push_back(c);h.row.push_back(pr);act.push_back(h);A.usedGen.push_back(A.genCounter);}}
        std::vector<Hilo> nxt;
        for(size_t k=0;k<act.size();++k){ if(A.usedGen[k]==A.genCounter&&act[k].col.back()==c) nxt.push_back(act[k]); else if(act[k].col.size()>=8) A.rios.push_back(act[k]); }
        act.swap(nxt);
    }
    for(auto&h:act) if(h.col.size()>=8) A.rios.push_back(h);
}
static void build_picos(){ A.picos.clear(); const Img&e=A.enh; int W=e.W,H=e.H; float thr=0.45f;
    for(int c=0;c<W;c+=2) for(int r=1;r<H-1;++r){ float v=e.at(r,c); if(v>thr&&v>=e.at(r-1,c)&&v>=e.at(r+1,c)) A.picos.push_back({c,r}); } }
static Hilo track_hilo(int c0,int r0){ const Img&e=A.enh; int W=e.W,H=e.H;
    auto bn=[&](int c,int r){int br=r;float bv=-1;for(int d=-3;d<=3;++d){int rr=r+d;if(rr<0||rr>=H)continue;if(e.at(rr,c)>bv){bv=e.at(rr,c);br=rr;}}return br;};
    std::vector<int> lc,lr,pc,pr; int r=bn(c0,r0);
    for(int c=c0;c<W;++c){r=bn(c,r);if(e.at(r,c)<0.2f)break;lc.push_back(c);lr.push_back(r);}
    r=bn(c0,r0); for(int c=c0-1;c>=0;--c){r=bn(c,r);if(e.at(r,c)<0.2f)break;pc.push_back(c);pr.push_back(r);}
    Hilo h; for(int i=(int)pc.size()-1;i>=0;--i){h.col.push_back(pc[i]);h.row.push_back(pr[i]);}
    for(size_t i=0;i<lc.size();++i){h.col.push_back(lc[i]);h.row.push_back(lr[i]);} return h; }
static void build_env(){ A.envMin.assign(A.NB,0); A.envMax.assign(A.NB,0);
    const auto&s=A.audio.samples; if(s.empty())return;
    for(int b=0;b<A.NB;++b){ size_t i0=(size_t)b*s.size()/A.NB, i1=(size_t)(b+1)*s.size()/A.NB; if(i1<=i0)i1=i0+1;
        float mn=1e9f,mx=-1e9f; for(size_t i=i0;i<i1&&i<s.size();++i){mn=min(mn,s[i]);mx=max(mx,s[i]);} A.envMin[b]=mn;A.envMax[b]=mx; } }

// ---------------- carga ----------
static void upload_texture(){ int W=A.enh.W,H=A.enh.H; RGBImg c(W,H);
    bool hasMask=!A.eraserMask.empty();
    for(int r=0;r<H;++r){ float f=(float)(H-1-r)/(H-1);
        for(int x=0;x<W;++x){ float e=A.enh.at(r,x); size_t i=((size_t)r*W+x)*3;
            if(pass_filt(f,e) && (!hasMask||A.eraserMask[r*W+x]==0)){
                int k=(int)(min(1.f,max(0.f,e))*255+0.5f);
                c.d[i]=A.cmapLUT[k][0];c.d[i+1]=A.cmapLUT[k][1];c.d[i+2]=A.cmapLUT[k][2]; }
            else { c.d[i]=c.d[i+1]=c.d[i+2]=0; } } }
    if(!A.tex)glGenTextures(1,&A.tex);
    glBindTexture(GL_TEXTURE_2D,A.tex); glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_LINEAR); glPixelStorei(GL_UNPACK_ALIGNMENT,1);
    glTexImage2D(GL_TEXTURE_2D,0,GL_RGB,c.W,c.H,0,GL_RGB,GL_UNSIGNED_BYTE,c.d.data()); }

// punto-en-poligono (ray casting) en coords de espectro
static bool pt_in_poly(const std::vector<int>& px,const std::vector<int>& py,float x,float y){
    bool in=false; int n=(int)px.size();
    for(int i=0,j=n-1;i<n;j=i++){ if(((py[i]>y)!=(py[j]>y)) &&
        (x < (float)(px[j]-px[i])*(y-py[i])/(float)((py[j]-py[i])+1e-9f)+px[i])) in=!in; }
    return in; }
// ¿(x,y) dentro de la etiqueta CONSIDERANDO los huecos/anillos? (dentro del exterior y fuera de todo hueco)
static bool pt_in_det(const Det& d,float x,float y){
    if(d.kind==KIND_POLY&&d.px.size()>=3){ if(!pt_in_poly(d.px,d.py,x,y))return false;
        for(size_t k=0;k<d.hx.size();++k) if(d.hx[k].size()>=3&&pt_in_poly(d.hx[k],d.hy[k],x,y))return false; return true; }
    return (x>=d.x&&x<d.x+d.w&&y>=d.y&&y<d.y+d.h); }

// cierra el poligono en construccion como un Det de la clase activa
static void finish_poly(){ if(A.polyX.size()<3){A.polyX.clear();A.polyY.clear();return;}
    push_undo();
    Det d; d.kind=KIND_POLY; d.cls=A.clase_activa; d.px=A.polyX; d.py=A.polyY;
    int mnx=d.px[0],mxx=d.px[0],mny=d.py[0],mxy=d.py[0];
    for(size_t k=0;k<d.px.size();++k){mnx=min(mnx,d.px[k]);mxx=max(mxx,d.px[k]);mny=min(mny,d.py[k]);mxy=max(mxy,d.py[k]);}
    d.x=mnx;d.y=mny;d.w=max(1,mxx-mnx);d.h=max(1,mxy-mny);
    A.dets.push_back(std::move(d)); A.sel=(int)A.dets.size()-1; A.polyX.clear();A.polyY.clear(); invalidate_dets_caches(); }
static bool load_audio(const std::string& path){ try{
    { size_t p=path.find_last_of("/\\"); show_busy(std::string("Cargando ")+((p==std::string::npos)?path:path.substr(p+1))+" ..."); }
    A.audio=load_wav(path); A.sr=A.audio.sample_rate;
    // RENDIMIENTO: para audios LARGOS o de alta frecuencia (ultrasonico), el espectrograma
    // tendria decenas de miles de columnas y todo se vuelve lento. Aumentamos el HOP de forma
    // adaptativa para que el ancho quede acotado (~MAXCOLS), conservando la resolucion de
    // frecuencia (nfft fijo). hop multiplo del base (256) para no perder enventanado.
    { const int MAXCOLS=4000; size_t N=A.audio.samples.size(); int base=256;
      int needed=(int)((N>(size_t)A.P.nfft)?((N-A.P.nfft)/(size_t)MAXCOLS):0);
      int hop=base; while(hop<needed)hop+=base;          // sube en pasos de 256
      A.P.hop=max(base,hop); }
    A.spec=compute_spectrogram(A.audio,A.P);
    { size_t p=path.find_last_of("/\\"); if(p!=std::string::npos){ A.out_dir=path.substr(0,p); A.fname=path.substr(p+1); } else A.fname=path; }   // nombre del archivo + CARPETA: carga/guarda las etiquetas JUNTO al audio
    A.enh=enhance_asinh(A.spec,0.07); A.dets.clear(); invalidate_dets_caches();  // NO auto-etiquetar al cargar (lo hace el boton Auto)
    A.hilos.clear(); A.sel=-1; A.sc0=A.sc1=A.sr0=A.sr1=-1; A.cursor_col=0; A.polyX.clear(); A.polyY.clear(); A.cutX.clear(); A.cutY.clear(); A.selDet=A.selVert=-1; A.selVerts.clear(); A.rbDrag=false;
    A.listSel.clear(); A.listCursor=A.listAnchor=-1; A.listScroll=0;   // lista de etiquetas: reset
    A.vr0=0; A.vr1=A.spec.H; A.sigInit=false; A.dirty=false; A.undo.clear();   // ventana freq completa + base de autosave + limpia historial
    A.vc0=0; A.vc1=A.spec.W;                                   // ventana = todo el audio
    // al cargar un audio NUEVO: reinicia TODOS los filtros a su valor por defecto y vuelve a la vista 2D
    A.fLo=0.f; A.fHi=1.f; A.dbMin=0.f; A.dbMax=1.f;            // filtros GLOBALES (freq + dB) a default
    A.volFLo=0.f; A.volFHi=1.f; A.volDbMin=0.f; A.volDbMax=1.f; // filtros LOCALES de la vista Volumen a default
    A.gain=1.0f; A.view=1; layout_botones();                   // ganancia normal + vista Espectro 2D + relayout de botones
    eraser_init();                                              // inicializar mascara del borrador
    double dur=A.sr?(double)A.audio.samples.size()/A.sr:0;     // eje X segun duracion
    A.tlen=(float)max(2.5,min(14.0,dur*0.7)); A.crosslen=1.7f;  // tiempo largo, freq/dB mas grandes
    A.ax[0]=A.tlen; A.ax[1]=A.ax[2]=A.crosslen;                 // escalas iniciales del cubo (editables con manijas)
    A.dist=max(5.0f,A.tlen*1.5f+A.crosslen*1.6f); A.yaw=18; A.pitch=34;  // ortoedro: frente=+Z, derecha=+X; vista frontal-elevada
    build_rios(); build_picos(); build_env(); build_cmap(); upload_texture();
    // auto-cargar etiquetas guardadas: si en out_dir existe <nombre_audio>.json (COCO), cargarlas
    { std::string stem=A.fname; size_t dp=stem.find_last_of('.'); if(dp!=std::string::npos)stem=stem.substr(0,dp);
      std::string jp=A.out_dir+"/"+stem+".json"; std::ifstream tf(jp.c_str());
      if(tf.good()){ tf.close(); std::vector<Det> in=import_coco(jp,A.classes);
        if(!in.empty()){ A.dets=in; if(A.clase_activa>=(int)A.classes.size())A.clase_activa=0; layout_botones(); invalidate_dets_caches();
          std::cout<<"Etiquetas cargadas de "<<jp<<": "<<in.size()<<"\n"; } } }
    // LIMPIEZA DEFENSIVA: descarta anillos mal ubicados (centroide FUERA del exterior) -> artefactos
    // del bug viejo de coords (anillos en la esquina sup-izq, fuera de su etiqueta). Tambien quita huecos degenerados.
    { int removed=0; for(auto&d:A.dets){ if(d.kind!=KIND_POLY||d.px.size()<3){ d.hx.clear(); d.hy.clear(); continue; }
        for(int hh=(int)d.hx.size()-1;hh>=0;--hh){
            if(hh>=(int)d.hy.size()||d.hx[hh].size()<3){ if(hh<(int)d.hx.size())d.hx.erase(d.hx.begin()+hh); if(hh<(int)d.hy.size())d.hy.erase(d.hy.begin()+hh); continue; }
            double cx=0,cy=0; for(size_t v=0;v<d.hx[hh].size();++v){cx+=d.hx[hh][v];cy+=d.hy[hh][v];} cx/=d.hx[hh].size(); cy/=d.hy[hh].size();
            if(!pt_in_poly(d.px,d.py,(float)cx,(float)cy)){ d.hx.erase(d.hx.begin()+hh); d.hy.erase(d.hy.begin()+hh); ++removed; } } }
      if(removed)std::cout<<"Anillos fuera de su etiqueta descartados (artefacto de bug viejo): "<<removed<<"\n"; }
    A.lastSig=labels_sig(); A.sigInit=true;   // base del autosave = estado recien cargado
    std::cout<<"Cargado "<<path<<": "<<A.spec.W<<"x"<<A.spec.H<<", "<<A.dets.size()<<" det, "<<A.rios.size()<<" crestas\n";
    return true;}catch(const std::exception&e){std::cerr<<"Error: "<<e.what()<<"\n";return false;} }
// RESOLUCION GLOBAL (botones/teclas Res- y Res+): recalcula el espectrograma con un HOP
// distinto (resolucion TEMPORAL). dir>0 = MAS fino (hop menor -> mas columnas); dir<0 = MAS
// grueso. Afecta la vista 2D (textura) Y todas las vistas 3D (derivan de A.enh). Reescala las
// etiquetas y la seleccion en el eje TIEMPO para que no se desplacen (nfft fijo: la resolucion
// de FRECUENCIA y las filas no cambian). Tambien ajusta res3d (densidad de las nubes 3D). NO
// usa push_undo: el cambio de resolucion no es deshacible por Ctrl+Z (que solo restaura dets).
static void set_resolution(int dir){
    A.res3d = dir>0 ? min(600,A.res3d+40) : max(48,A.res3d-40);            // densidad de display 3D
    if(A.spec.W<1||A.audio.samples.empty())return;
    int oldHop=A.P.hop, oldW=A.spec.W; size_t N=A.audio.samples.size(), nfft=(size_t)A.P.nfft;
    auto cols=[&](int h){ return (N>nfft)?(int)((N-nfft)/(size_t)max(1,h))+1:1; };
    int newHop = dir>0 ? max(64,(oldHop*3)/4) : (oldHop*4)/3;              // +/-25% por paso
    newHop=(newHop/16)*16; if(newHop<64)newHop=64;                         // multiplo de 16, minimo 64
    while(cols(newHop)>20000 && newHop<oldHop*16) newHop+=64;              // acota el ancho (memoria/velocidad)
    while(cols(newHop)<200 && newHop>64) newHop-=64;
    if(newHop==oldHop)return;                                              // sin cambio efectivo
    show_busy("Recalculando resolucion...");
    A.P.hop=newHop; A.spec=compute_spectrogram(A.audio,A.P); A.enh=enhance_asinh(A.spec,0.07);
    int newW=A.spec.W; double rx=oldW>0?(double)newW/oldW:1.0;
    for(auto&d:A.dets){ d.x=(int)(d.x*rx+0.5); d.w=max(1,(int)(d.w*rx+0.5));  // reescala etiquetas en tiempo
        for(auto&xx:d.px) xx=(int)(xx*rx+0.5); }
    if(A.sc0>=0)A.sc0=(int)(A.sc0*rx+0.5); if(A.sc1>=0)A.sc1=(int)(A.sc1*rx+0.5);
    A.cursor_col=(int)(A.cursor_col*rx+0.5);
    A.vc0=max(0,min(newW-1,(int)(A.vc0*rx+0.5))); A.vc1=max(A.vc0+1,min(newW,(int)(A.vc1*rx+0.5)));   // REESCALA la ventana de tiempo por rx (CONSERVA la region visible; NO resetea)
    A.vr0=max(0,min(A.spec.H-1,A.vr0)); A.vr1=max(A.vr0+1,min(A.spec.H,A.vr1>0?A.vr1:A.spec.H));      // freq (vr): H sin cambio -> se conserva el zoom de frecuencia (clamp defensivo)
    build_rios(); build_picos(); eraser_init(); upload_texture(); A.dirty=true;
    std::cout<<"Resolucion: hop="<<newHop<<" -> "<<newW<<" columnas (res3d="<<A.res3d<<")\n"; }
static bool open_dialog(){
    if(g_dialogOpen){ if(g_hwnd)SetForegroundWindow(g_hwnd); return false; }   // ya hay un dialogo abierto -> traer al frente, NO abrir otro
    g_dialogOpen=true;
    char f[MAX_PATH]=""; OPENFILENAMEA o{}; o.lStructSize=sizeof(o); o.hwndOwner=g_hwnd;   // dueña -> deshabilita la ventana mientras el dialogo esta abierto
    o.lpstrFilter="WAV\0*.wav\0Todos\0*.*\0"; o.lpstrFile=f; o.nMaxFile=MAX_PATH;
    o.Flags=OFN_FILEMUSTEXIST|OFN_PATHMUSTEXIST;
    bool ok=GetOpenFileNameA(&o)!=0; g_dialogOpen=false;
    return ok ? load_audio(f) : false; }

// ---------------- mascara espacial de etiquetas (para play_filtered O(1)) ----------
// Construye una mascara W×H que marca que pixeles estan dentro de alguna etiqueta.
// Se cachea y solo se reconstruye cuando A.maskDirty=true.
static void build_label_mask() {
    int W = A.spec.W, H = A.spec.H;
    A.labelMask.assign((size_t)W * H, 0);
    for (const Det& d : A.dets) {
        if (d.kind == KIND_POLY && d.px.size() >= 3) {
            // Rellenar poligono con scanline (simplificado: marcar bounding box y pt_in_det)
            int x0 = max(0, d.x), x1 = min(W, d.x + d.w);
            int y0 = max(0, d.y), y1 = min(H, d.y + d.h);
            for (int r = y0; r < y1; ++r)
                for (int c = x0; c < x1; ++c)
                    if (pt_in_det(d, c + 0.5f, r + 0.5f))
                        A.labelMask[r * W + c] = 1;
        } else {
            // Bounding box: marcar rectangulo
            int x0 = max(0, d.x), x1 = min(W, d.x + d.w);
            int y0 = max(0, d.y), y1 = min(H, d.y + d.h);
            for (int r = y0; r < y1; ++r)
                for (int c = x0; c < x1; ++c)
                    A.labelMask[r * W + c] = 1;
        }
    }
    A.maskDirty = false;
}
inline bool in_label_mask(int c, int r) {
    int W = A.spec.W, H = A.spec.H;
    if (c < 0 || c >= W || r < 0 || r >= H) return false;
    return A.labelMask[r * W + c] != 0;
}

// ---------------- reproduccion con filtros aplicados al SONIDO ----------
// STFT enmascarada (mismo criterio que el display: pass_filt sobre freq+dB) +
// banda de la seleccion (si solo_banda) -> reconstruccion overlap-add (WOLA).
static void play_filtered(int c0,int c1,bool useSel,float selLo,float selHi){
    const auto& s=A.audio.samples; if(s.empty())return;
    if(A.maskDirty) build_label_mask();  // reconstruir mascara si hubo cambios en A.dets
    int W=A.spec.W,H=A.spec.H, nfft=A.P.nfft, hop=A.P.hop;
    c0=max(0,min(W-1,c0)); c1=max(c0+1,min(W,c1));
    size_t i0=(size_t)c0*hop, i1=min(s.size(),(size_t)c1*hop+nfft); if(i1<=i0)return;
    size_t outN=i1-i0;
    // filtro activo para el SONIDO: global, salvo en la vista Volumen (V7) que usa el suyo local
    float qfLo=(A.view==7)?A.volFLo:A.fLo, qfHi=(A.view==7)?A.volFHi:A.fHi;
    float qdLo=(A.view==7)?A.volDbMin:A.dbMin, qdHi=(A.view==7)?A.volDbMax:A.dbMax;
    bool useMask=A.playMask;         // limitar a la mascara de etiquetas (solo lo que esta dentro de las etiquetas)
    bool anyFilt = (qfLo>0||qfHi<1||qdLo>0||qdHi<1||useSel||useMask);
    std::vector<float> out(outN,0.f); float mx=1e-6f;
    if(!anyFilt){
        // SIN filtro: COPIA DIRECTA del audio (no STFT). En audios largos el hop de display se
        // acota (~4000 columnas) y queda > nfft, con lo que la reconstruccion STFT dejaba HUECOS
        // de silencio entre tramas -> sonaba "feo"; ademas evita el coste de las FFT (sin lag).
        for(size_t i=0;i<outN;++i){ float v=s[i0+i]; out[i]=v; float a=std::fabs(v); if(a>mx)mx=a; }
    } else {
        // CON filtro: STFT enmascarada + overlap-add con HOP FINO (nfft/4), INDEPENDIENTE del hop de
        // display -> solapamiento correcto (WOLA, sin huecos) aunque el espectrograma use un hop grande.
        std::vector<float> wsum(outN,0.f), win(nfft); std::vector<Cf> X(nfft);
        for(int k=0;k<nfft;++k) win[k]=0.5f*(1-std::cos(2.0f*(float)M_PI*k/(nfft-1)));
        int rhop=max(1,nfft/4);
        for(size_t off=i0; off<i1; off+=(size_t)rhop){ int cc=min(W-1,(int)(off/(size_t)hop));   // columna de display p/ el filtro
            for(int k=0;k<nfft;++k){ size_t idx=off+k; float sv=(idx<s.size())?s[idx]:0.f; X[k]=Cf(sv*win[k],0); }
            fft(X);
            for(int k=0;k<nfft;++k){ int b=(k<=nfft/2)?k:(nfft-k); float f=(float)b/(H-1);
                int r=(H-1)-b; if(r<0)r=0; if(r>=H)r=H-1; float e=A.enh.at(r,cc);
                bool keep=(f>=qfLo&&f<=qfHi&&e>=qdLo&&e<=qdHi); if(useSel&&(f<selLo||f>selHi))keep=false;
                if(keep&&useMask){ if(!in_label_mask(cc,r)) keep=false; }
                if(!keep) X[k]=Cf(0,0); }
            ifft(X);
            size_t base=off-i0;
            for(int k=0;k<nfft;++k){ size_t p=base+k; if(p<outN){ out[p]+=X[k].real()*win[k]; wsum[p]+=win[k]*win[k]; } }
        }
        for(size_t i=0;i<outN;++i){ if(wsum[i]>1e-6f)out[i]/=wsum[i]; float a=std::fabs(out[i]); if(a>mx)mx=a; }
    }
    float g=0.9f*A.gain/mx; std::vector<int16_t> pcm(outN);   // ganancia: >1 satura (sube volumen)
    for(size_t i=0;i<outN;++i){ float v=out[i]*g; v=v<-1?-1:(v>1?1:v); pcm[i]=(int16_t)(v*32767); }
    int fade=min((int)(outN/2),max(1,A.sr/150));   // rampa ~7ms: evita el GOLPE/click al iniciar y al terminar
    for(int i=0;i<fade;++i){ float gg=(float)i/fade; pcm[i]=(int16_t)(pcm[i]*gg); pcm[outN-1-i]=(int16_t)(pcm[outN-1-i]*gg); }
    A.playC1=c1; A.playUseSel=useSel; A.playSelLo=selLo; A.playSelHi=selHi; A.playCtx=true;  // contexto p/ re-filtrar en vivo
    PLAYER.play_buffer(std::move(pcm),A.sr,i0,(double)A.playSpeed);   // aplica la velocidad de reproduccion
}
// re-aplica los filtros actuales al sonido en reproduccion: re-renderiza desde el
// PLAYHEAD hasta el final del rango original (fade-in suaviza el empalme).
static void refilter_live(){ if(!A.playCtx||!PLAYER.playing||PLAYER.paused)return;
    long long cs=PLAYER.cur_sample(); if(cs<0)return; int cur=(int)(cs/A.P.hop);
    if(cur>=A.playC1-1)return;
    play_filtered(cur,A.playC1,A.playUseSel,A.playSelLo,A.playSelHi); }
// Reproduce una ETIQUETA concreta (su tiempo + banda de frecuencia)
static void play_det(int i){ if(A.audio.samples.empty()||i<0||i>=(int)A.dets.size())return; const Det&d=A.dets[i]; int H=A.spec.H;
    bool useSel=false; float sLo=0,sHi=1;
    if(A.solo_banda){ useSel=true; sHi=(float)(H-1-d.y)/(H-1); sLo=(float)(H-1-(d.y+d.h))/(H-1); }
    A.playMask=false; play_filtered(d.x,d.x+d.w,useSel,sLo,sHi); }
// Reproduce la SELECCION de arrastre (ignora la etiqueta seleccionada)
static void play_drag_sel(){ if(A.audio.samples.empty()||A.sc0<0||A.sc1<=A.sc0)return; int H=A.spec.H;
    bool useSel=false; float sLo=0,sHi=1;
    if(A.solo_banda&&A.sr0>=0&&A.sr1>=0){ useSel=true; int rmn=min(A.sr0,A.sr1),rmx=max(A.sr0,A.sr1);
        sHi=(float)(H-1-rmn)/(H-1); sLo=(float)(H-1-rmx)/(H-1); }
    A.playMask=false; play_filtered(A.sc0,A.sc1,useSel,sLo,sHi); }
// Play principal: reproduce la ventana visible. En la vista VOLUMEN (V7) reproduce SOLO
// lo que esta DENTRO de las etiquetas (mascara) y con los filtros locales aplicados.
static void play_window(){ if(A.audio.samples.empty())return;
    A.playMask=(A.view==7); play_filtered(vlo(),vhi(),false,0,1); }

// ---------------- layout de botones ----------
static std::vector<std::array<int,2>> g_seps;   // separadores de seccion (x,y)
static std::vector<std::array<int,5>> g_palette; // swatches de clase {x,y,w,h,cls}
static void layout_botones(){
    struct Bdef{ const char* label; int key; int icon; int when; const char* tip; };  // when: 0=siempre, else vista
    static const Bdef defs[]={
        // --- Archivo ---
        {"",'o',0,0,"Abrir archivo WAV"},{"Cargar",'R',-1,0,"Cargar etiquetas Raven (.txt selection table)"},{"",'s',7,0,"Guardar (COCO .json + Raven .txt)"},
        {0,0,0,0,0},
        // --- Vistas ---
        {"2D",'1',-1,0,"Vista espectrograma 2D"},{"3D",'2',-1,0,"Vista terreno 3D"},
        {"Rio",'3',-1,0,"Vista rio espectral (crestas)"},
        {"Nube",'4',-1,0,"Vista nube de puntos"},{"Cascada",'5',-1,0,"Cascada espectral: espectro por tiempo, marca picos y etiqueta frecuencias dominantes"},{"Quiver",'6',-1,0,"Vista Quiver3D (glifos por hilo)"},
        {"Vol",'7',-1,0,"Vista volumen 3D: nube de puntos densa por etiqueta (freq y dB)"},
        {0,0,0,0,0},
        // --- Reproduccion ---
        {"",' ',1,0,"Reproducir la ventana visible"},{"",'k',2,0,"Pausar / reanudar"},{"",'.',3,0,"Detener"},
        {"Banda",'B',-1,0,"Reproducir SOLO la banda (tiempo + frecuencia)"},
        {"Vel-",'v',-1,0,"Reproducir mas LENTO (-0.1x). Lento = mas grave: baja el ultrasonido a lo audible (expansion temporal)"},
        {"Vel+",'V',-1,0,"Reproducir mas RAPIDO (+0.1x)"},
        {0,0,0,0,0},
        // --- Navegacion ---
        {"",'T',6,0,"Ver todo el audio"},{"",'Z',4,0,"Acercar la ventana (zoom +)"},{"",'U',5,0,"Alejar la ventana (zoom -)"},
        {0,0,0,0,0},
        // --- Display / filtro ---
        {"Senal",'f',-1,0,"Fijar valor de senal: pasa el techo de dB actual a piso y abre el techo (muestra de ese valor a 0 dB)"},
        {"Mapa",'M',-1,0,"Mapa de color del espectrograma: elige entre 10 paletas (clic = desplegar)"},
        {0,0,0,0,0},
        // --- Etiquetado ---
        {"Selec",'S',-1,0,"Herramienta: seleccionar / escuchar"},
        {"BBox",'Y',-1,0,"Etiquetar BOUNDING BOX (arrastra un rectangulo)"},
        {"Poly",'P',-1,0,"Etiquetar POLIGONO (clic a clic; clic-der/Enter cierra)"},
        {"Cortar",'X',-1,0,"Corte LIBRE: pinta el trazo y clic-derecho corta TODAS las etiquetas que cruza"},
        {"Unir",'J',-1,0,"Unir dos etiquetas: clic en la primera, luego en la segunda"},
        {"Borrar",'d',-1,0,"Borrar residuos: pinta areas para excluir del autoetiquetado (radio adjustable con rueda)"},
        {"Auto",'a',-1,0,"Auto-etiquetar SOBRE EL FILTRO, con la forma activa (poligono o bbox)"},
        {"+Etiq",'N',-1,0,"Crear etiqueta nueva (escribe el nombre)"},
        {"Ocultar",'O',-1,0,"Ocultar / mostrar las etiquetas"},
        {"Lista",'L',-1,0,"Lista de TODAS las etiquetas: clic=selecciona (resalta en 2D); cabecera ordena por tamano; Supr/clic-der borra; Ctrl/Shift/flechas = multiseleccion"},
        {"Limpiar",'c',-1,0,"Borrar TODAS las etiquetas"},
        {0,0,0,0,0},
        // --- 3D ---
        {"Res-",'[',-1,0,"Menos resolucion (toda vista: recalcula espectro + 3D)"},{"Res+",']',-1,0,"Mas resolucion (toda vista: recalcula espectro + 3D)"},
        {"Crestas",'H',-1,3,"Crestas completas: muestra el hilo entero si algun punto pasa el filtro"},  // solo vista Rio (V3)
        {"Glifos",'G',-1,6,"Glifos completos: dibuja todos los palos del hilo si algun punto pasa el filtro"}, // solo Quiver (V6)
        {0,0,0,0,0},
        // --- Info ---
        {"Acerca",'?',-1,0,"Acerca de / Contacto: creador, IIAP, y para que sirve el software"},
    };
    A.botones.clear(); g_seps.clear(); g_palette.clear(); int x=4,y=HUD_H+3,rows=1;
    for(auto&d:defs){
        if(d.label==nullptr){ g_seps.push_back({x+3,y}); x+=10; continue; }       // separador de seccion
        if(d.when!=0 && d.when!=A.view) continue;                                  // boton dependiente de la vista
        std::string l=d.label; int w=(d.icon>=0)?26:(int)l.size()*8+12;
        if(x+w>A.cW-4){ x=4; y+=24; rows++; }
        A.botones.push_back({x,y,w,20,l,d.key,d.icon,d.tip}); x+=w+4; }
    // paleta de clases (swatch + nombre) al final, clicable
    g_seps.push_back({x+3,y}); x+=10;
    for(int c=0;c<(int)A.classes.size();++c){ int w=(int)A.classes[c].name.size()*8+24;
        if(x+w>A.cW-4){ x=4; y+=24; rows++; }
        g_palette.push_back({x,y,w,20,c}); x+=w+4; }
    A.toolbar_h=rows*24+6;
}
static int hit_palette(int mx,int my){ for(size_t i=0;i<g_palette.size();++i){auto&p=g_palette[i];
    if(mx>=p[0]&&mx<p[0]+p[2]&&my>=p[1]&&my<p[1]+p[3])return p[4];} return -1; }
static int hit_boton(int mx,int my){ for(size_t i=0;i<A.botones.size();++i){auto&b=A.botones[i];
    if(mx>=b.x&&mx<b.x+b.w&&my>=b.y&&my<b.y+b.h)return (int)i;} return -1; }

// ---------------- mapeo de coordenadas ----------
static void main_to_spec(int mx,int my,int&c,int&r){     // dentro de la ventana, respetando los margenes del plot
    double fx=(mx-plotX0())/max(1.f,plotX1()-plotX0()); fx=fx<0?0:(fx>1?1:fx);
    double fy=(my-plotY0())/max(1.f,plotY1()-plotY0()); fy=fy<0?0:(fy>1?1:fy);
    c=vlo()+(int)(fx*(vhi()-vlo())); r=rlo()+(int)(fy*(rhi()-rlo()));
    c=max(0,min(A.spec.W-1,c)); r=max(0,min(A.spec.H-1,r)); }
static void strip_to_spec(int mx,int my,int&c,int&r){ c=(int)((double)mx/A.cW*A.spec.W);
    r=(int)((double)(my-panel_y0())/STRIP_H*A.spec.H); c=max(0,min(A.spec.W-1,c)); r=max(0,min(A.spec.H-1,r)); }

// ---------------- render: helpers 2D ----------
static void ortho2d(){ glViewport(0,0,A.cW,A.cH); glDisable(GL_DEPTH_TEST);
    glMatrixMode(GL_PROJECTION); glLoadIdentity(); gluOrtho2D(0,A.cW,A.cH,0);
    glMatrixMode(GL_MODELVIEW); glLoadIdentity(); }

// indicador de carga/proceso: pinta un cartel centrado y lo presenta de inmediato
// (la operacion pesada que sigue bloquea el hilo, por eso se dibuja ANTES).
static void show_busy(const std::string& msg){ if(!g_hdc)return;
    ortho2d(); glEnable(GL_BLEND);glBlendFunc(GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA);
    glColor4f(0,0,0,0.55f);glBegin(GL_QUADS);glVertex2f(0,0);glVertex2f(A.cW,0);glVertex2f(A.cW,A.cH);glVertex2f(0,A.cH);glEnd();
    float w=380,hh=64,x=A.cW*0.5f-w*0.5f,y=A.cH*0.5f-hh*0.5f;
    glColor4f(0.10f,0.12f,0.18f,0.96f);glBegin(GL_QUADS);glVertex2f(x,y);glVertex2f(x+w,y);glVertex2f(x+w,y+hh);glVertex2f(x,y+hh);glEnd();
    glColor3f(0.4f,0.7f,1.f);glLineWidth(2.f);glBegin(GL_LINE_LOOP);glVertex2f(x,y);glVertex2f(x+w,y);glVertex2f(x+w,y+hh);glVertex2f(x,y+hh);glEnd();
    // barra animacion simple (3 bloques) - estatica pero indica actividad
    glColor3f(0.4f,0.7f,1.f);for(int i=0;i<3;++i){float bx=x+w*0.5f-24+i*18;glBegin(GL_QUADS);glVertex2f(bx,y+hh-16);glVertex2f(bx+12,y+hh-16);glVertex2f(bx+12,y+hh-8);glVertex2f(bx,y+hh-8);glEnd();}
    glDisable(GL_BLEND); glColor3f(1,1,1); FONT.at2d(x+18,y+30,msg);
    SwapBuffers(g_hdc); }
// espectro restringido al filtro actual (freq+dB): para auto-segmentar SOLO lo visible
static Img filtered_spec(){ Img s=A.spec; int W=A.spec.W,H=A.spec.H;
    bool hasMask=!A.eraserMask.empty();
    for(int r=0;r<H;++r){ float f=(float)(H-1-r)/(H-1);
        for(int c=0;c<W;++c){ float e=A.enh.at(r,c);
            if(!pass_filt(f,e)||(hasMask&&A.eraserMask[r*W+c])) s.at(r,c)=0.f; } }
    return s; }
// Auto-etiquetar: detecta sobre el espectro FILTRADO; si el filtro deja todo vacio,
// reintenta sobre el espectro completo para que SIEMPRE proponga algo. Muestra etiquetas.
static void auto_segment(){ if(A.spec.W<1)return;
    show_busy("Auto-etiquetando..."); push_undo();
    std::vector<Det> d=auto_label(filtered_spec(),A.K,A.area_min,A.shape_poly,A.shape_poly?A.autoBuffer:0);   // sin limite de ancho: puede etiquetar areas grandes (bandas que cruzan toda la grabacion)
    if(d.empty()) d=auto_label(A.spec,A.K,A.area_min,A.shape_poly,A.shape_poly?A.autoBuffer:0);  // respaldo: espectro crudo
    A.dets=std::move(d); A.sel=-1; A.hide_labels=false; A.listSel.clear(); invalidate_dets_caches();      // asegurar que se vean
    std::cout<<"Auto-etiquetado: "<<A.dets.size()<<" etiquetas\n"; }
// Auto-etiquetar SOLO dentro del rectangulo de seleccion [sc0,sc1]x[sr0,sr1], sobre el
// espectro FILTRADO (respeta fLo/fHi/dbMin/dbMax). AGREGA las etiquetas (no reemplaza) y
// les pone la clase activa. Coords del recorte vueltas a globales antes de insertar.
static void auto_segment_in_selection(bool make_poly){ if(A.spec.W<1)return;
    int W=A.spec.W,H=A.spec.H; int c0=A.sc0,c1=A.sc1; if(c0<0||c1<=c0)return;
    c0=max(0,min(W,c0)); c1=max(c0+1,min(W,c1));
    int r0,r1; if(A.sr0>=0&&A.sr1>=0){ r0=min(A.sr0,A.sr1); r1=max(A.sr0,A.sr1); } else { r0=0; r1=H; }
    r0=max(0,min(H,r0)); r1=max(r0+1,min(H,r1));
    int cw=c1-c0, ch=r1-r0; if(cw<3||ch<3)return;
    show_busy("Auto-etiquetando seleccion..."); push_undo();
    Img fs=filtered_spec(); Img crop(cw,ch);                      // recorte del espectro filtrado
    for(int r=0;r<ch;++r)for(int c=0;c<cw;++c) crop.at(r,c)=fs.at(r0+r,c0+c);
    std::vector<Det> d=auto_label(crop,A.K,max(8,A.area_min/2),make_poly,make_poly?A.autoBuffer:0);
    if(d.empty()){ A.undo.pop_back(); std::cout<<"Auto-etiquetado seleccion: 0\n"; return; }
    for(auto&det:d){ det.x+=c0; det.y+=r0; det.cls=A.clase_activa;   // offset a coords globales + clase activa
        for(size_t k=0;k<det.px.size();++k){ det.px[k]+=c0; det.py[k]+=r0; }
        for(auto&hxv:det.hx)for(auto&v:hxv)v+=c0; for(auto&hyv:det.hy)for(auto&v:hyv)v+=r0;   // FIX: offsetear TAMBIEN los anillos/huecos (antes quedaban en coords del recorte -> aparecian en la esquina sup-izq, fuera de la etiqueta)
        A.dets.push_back(std::move(det)); }
    A.sel=(int)A.dets.size()-1; A.hide_labels=false; A.dirty=true; invalidate_dets_caches();
    std::cout<<"Auto-etiquetado seleccion: "<<d.size()<<" etiquetas\n"; }
// Auto-mejorar una etiqueta: re-segmenta SOLO su region (con un margen) sobre el
// espectro filtrado y reemplaza el poligono por el contorno hallado (conserva clase).
static void improve_det(int i){ if(i<0||i>=(int)A.dets.size())return; int W=A.spec.W,H=A.spec.H; if(W<2)return;
    Det& d=A.dets[i]; int pad=6;
    int x0=max(0,d.x-pad),y0=max(0,d.y-pad),x1=min(W,d.x+d.w+pad),y1=min(H,d.y+d.h+pad);
    int cw=x1-x0, ch=y1-y0; if(cw<3||ch<3)return;
    Img fs=filtered_spec(); Img crop(cw,ch);                       // recorte filtrado de la region
    for(int r=0;r<ch;++r)for(int c=0;c<cw;++c) crop.at(r,c)=fs.at(y0+r,x0+c);
    std::vector<Det> ds=auto_label(crop,A.K,max(8,A.area_min/2),true,A.shape_poly?A.autoBuffer:0);
    if(ds.empty())return;
    int best=0; long ba=0; for(size_t k=0;k<ds.size();++k){ long a=(long)ds[k].w*ds[k].h; if(a>ba){ba=a;best=(int)k;} }
    Det nd=ds[best]; for(size_t k=0;k<nd.px.size();++k){ nd.px[k]+=x0; nd.py[k]+=y0; }   // offset a coords globales
    for(auto&hxv:nd.hx)for(auto&v:hxv)v+=x0; for(auto&hyv:nd.hy)for(auto&v:hyv)v+=y0;   // FIX: offsetear TAMBIEN los anillos/huecos al global
    nd.x+=x0; nd.y+=y0; nd.cls=d.cls; nd.kind=KIND_POLY; d=nd; invalidate_dets_caches(); }
// modal de buffer: fija autoBuffer desde la posicion del raton y re-mejora la etiqueta
// (siempre desde la copia original, para que el slider no acumule).
static void buffer_apply(int mx){ float tx0,tx1,ty; bufslider(tx0,tx1,ty);
    float v=(mx-tx0)/(tx1-tx0>1?tx1-tx0:1); v=v<0?0:(v>1?1:v);
    A.autoBuffer=(int)(v*BUF_MAX+0.5f);
    if(A.bufSel>=0&&A.bufSel<(int)A.dets.size()){ A.dets[A.bufSel]=A.bufOrig; improve_det(A.bufSel); invalidate_dets_caches(); } }

static void draw_tex_quad(float x0,float y0,float x1,float y1,float u0=0.f,float u1=1.f,float v0=0.f,float v1=1.f){
    glEnable(GL_TEXTURE_2D); glBindTexture(GL_TEXTURE_2D,A.tex); glColor3f(1,1,1);
    glBegin(GL_QUADS); glTexCoord2f(u0,v0);glVertex2f(x0,y0); glTexCoord2f(u1,v0);glVertex2f(x1,y0);
    glTexCoord2f(u1,v1);glVertex2f(x1,y1); glTexCoord2f(u0,v1);glVertex2f(x0,y1); glEnd();
    glDisable(GL_TEXTURE_2D); }

// cajas/seleccion/playhead en [x0,y0]-[x1,y1]; columnas [cLo,cHi]->[x0,x1], filas [rLo,rHi]->[y0,y1]
// scratch reutilizable para el relleno por scanlines (evita churn por frame)
struct FillSeg{ float c0,r0,c1,r1; };           // arista en coords de ESPECTRO (col,fila)
static std::vector<FillSeg> g_fillSeg;
static std::vector<float> g_fillX;
static void draw_overlays(float x0,float y0,float x1,float y1,bool conHilos,int cLo,int cHi,int rLo=0,int rHi=0){
    int H=A.spec.H; if(rHi<=rLo)rHi=H; int span=max(1,cHi-cLo),rspan=max(1,rHi-rLo); float sx=(x1-x0)/span, sy=(y1-y0)/rspan;
    auto xc=[&](float c){ float x=x0+(c-cLo)*sx; return x<x0?x0:(x>x1?x1:x); };
    auto yc=[&](float r){ float y=y0+(r-rLo)*sy; return y<y0?y0:(y>y1?y1:y); };
    // Relleno LIGERO de una etiqueta que EXCLUYE los huecos/anillos (para verlos).
    // Even-odd por scanlines: itera pixeles de pantalla pero MUESTREA e interseca en
    // espacio de espectro (Δfila entera -> sin denominador diminuto ni coincidencia con
    // vertices). Bandas como GL_QUADS (sin rayas ni dependencia del grosor de linea).
    auto fill_label=[&](const Det&d,float rr,float gg,float bb,float al){
        glColor4f(rr,gg,bb,al);
        if(d.kind!=KIND_POLY||d.px.size()<3){ glBegin(GL_QUADS);
            glVertex2f(xc(d.x),yc(d.y));glVertex2f(xc(d.x+d.w),yc(d.y));glVertex2f(xc(d.x+d.w),yc(d.y+d.h));glVertex2f(xc(d.x),yc(d.y+d.h)); glEnd(); return; }
        g_fillSeg.clear();
        auto add=[&](const std::vector<int>&xs,const std::vector<int>&ys){ int n=(int)xs.size(); if(n<3)return;
            for(int k=0;k<n;++k){ int j=(k+1)%n; g_fillSeg.push_back({(float)xs[k],(float)ys[k],(float)xs[j],(float)ys[j]}); } };
        add(d.px,d.py); for(size_t h=0;h<d.hx.size();++h) add(d.hx[h],d.hy[h]);   // exterior + huecos -> paridad par-impar
        int rmn=d.py[0],rmx=d.py[0]; for(int v:d.py){ if(v<rmn)rmn=v; if(v>rmx)rmx=v; }
        float ya=y0+(rmn-rLo)*sy, yb=y0+(rmx-rLo)*sy;
        float ymn=max(min(ya,yb),y0), ymx=min(max(ya,yb),y1); if(ymx<=ymn)return;
        float syy=(sy>1e-6f?sy:1e-6f);
        glBegin(GL_QUADS);
        for(float yy=ymn; yy<ymx; yy+=1.f){ float y2=(yy+1.f<ymx)?yy+1.f:ymx;
            float rv=rLo+(yy+0.5f-y0)/syy; g_fillX.clear();                       // fila de muestreo (centro del pixel)
            for(auto&s:g_fillSeg){ float lo=min(s.r0,s.r1),hi=max(s.r0,s.r1);     // horizontales (r0==r1) se saltan -> sin div/0
                if(rv>=lo&&rv<hi){ float col=s.c0+(rv-s.r0)*(s.c1-s.c0)/(s.r1-s.r0); g_fillX.push_back(x0+(col-cLo)*sx); } }
            if(g_fillX.size()<2)continue; std::sort(g_fillX.begin(),g_fillX.end());
            if(g_fillX.size()&1)continue;                                         // recuento impar (coincidencia float) -> salta la fila
            for(size_t k=0;k+1<g_fillX.size();k+=2){ float xa=g_fillX[k]<x0?x0:g_fillX[k], xb=g_fillX[k+1]>x1?x1:g_fillX[k+1];
                if(xb>xa){ glVertex2f(xa,yy);glVertex2f(xb,yy);glVertex2f(xb,y2);glVertex2f(xa,y2); } } }
        glEnd(); };
    if(A.sc0>=0&&A.sc1>A.sc0){ glEnable(GL_BLEND);glBlendFunc(GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA);
        glColor4f(1,1,1,0.18f); float yy0=(A.sr0>=0)?yc((float)min(A.sr0,A.sr1)):y0, yy1=(A.sr1>=0)?yc((float)max(A.sr0,A.sr1)):y1;
        glBegin(GL_QUADS);glVertex2f(xc(A.sc0),yy0);glVertex2f(xc(A.sc1),yy0);glVertex2f(xc(A.sc1),yy1);glVertex2f(xc(A.sc0),yy1);glEnd(); glDisable(GL_BLEND);}
    // "seleccionados" = A.sel (clic en uno) O todos los poligonos cuyo CENTRO cae en la caja de seleccion (varios) -> se resaltan igual
    bool hasRect=(A.sc0>=0&&A.sc1>A.sc0); int qs0=0,qs1=0,qr0=0,qr1=0;
    if(hasRect){ qs0=min(A.sc0,A.sc1); qs1=max(A.sc0,A.sc1); bool hr=(A.sr0>=0&&A.sr1>=0); qr0=hr?min(A.sr0,A.sr1):0; qr1=hr?max(A.sr0,A.sr1):H; }
    auto inSelRect=[&](const Det&d){ if(!hasRect)return false; int cx=d.x+d.w/2,cy=d.y+d.h/2; return cx>=qs0&&cx<=qs1&&cy>=qr0&&cy<=qr1; };
    if(!A.hide_labels) for(int i=0;i<(int)A.dets.size();++i){const Det&d=A.dets[i];
        bool sel=(i==A.sel)||inSelRect(d)||(A.listOpen&&list_is_sel(i)); float cr,cg,cb; class_color(d.cls,cr,cg,cb);
        auto emit=[&](){ if(d.kind==KIND_POLY&&d.px.size()>=3){ for(size_t k=0;k<d.px.size();++k)glVertex2f(xc((float)d.px[k]),yc((float)d.py[k])); }
            else { glVertex2f(xc(d.x),yc(d.y));glVertex2f(xc(d.x+d.w),yc(d.y));glVertex2f(xc(d.x+d.w),yc(d.y+d.h));glVertex2f(xc(d.x),yc(d.y+d.h)); } };
        if(conHilos){ glEnable(GL_BLEND);glBlendFunc(GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA);   // RELLENO LIGERO (excluye huecos -> se ven los anillos); solo plot principal
            if(sel) fill_label(d,1.f,0.95f,0.25f,0.16f); else fill_label(d,cr,cg,cb,0.13f);
            glDisable(GL_BLEND); }
        if(sel){ glEnable(GL_BLEND);glBlendFunc(GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA);          // SELECCIONADA: halo amarillo grueso (contorno exterior)
            glColor3f(1.f,0.95f,0.25f); glLineWidth(5.f); glBegin(GL_LINE_LOOP); emit(); glEnd();
            glDisable(GL_BLEND); }
        glColor3f(cr,cg,cb); glLineWidth(sel?2.f:1.5f); glBegin(GL_LINE_LOOP); emit(); glEnd();   // borde con color de clase
        for(size_t hk=0;hk<d.hx.size();++hk){ if(d.hx[hk].size()<3)continue;                       // HUECOS/anillos (contorno interno)
            if(sel){ glEnable(GL_BLEND);glBlendFunc(GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA);           // SELECCIONADA: halo amarillo TAMBIEN en las lineas de los anillos
                glColor3f(1.f,0.95f,0.25f); glLineWidth(5.f); glBegin(GL_LINE_LOOP); for(size_t k=0;k<d.hx[hk].size();++k)glVertex2f(xc((float)d.hx[hk][k]),yc((float)d.hy[hk][k])); glEnd(); glDisable(GL_BLEND); }
            glColor3f(cr,cg,cb); glLineWidth(sel?2.f:1.f); glBegin(GL_LINE_LOOP); for(size_t k=0;k<d.hx[hk].size();++k)glVertex2f(xc((float)d.hx[hk][k]),yc((float)d.hy[hk][k])); glEnd(); } }
    if(conHilos){ glColor3f(0.2f,1,1);glLineWidth(2.f);
        for(auto&h:A.hilos){glBegin(GL_LINE_STRIP);for(size_t k=0;k<h.col.size();++k)glVertex2f(xc((float)h.col[k]),yc((float)h.row[k]));glEnd();}}
    long long cs=PLAYER.cur_sample(); if(cs>=0)A.cursor_col=(int)(cs/A.P.hop); int pcol=A.cursor_col;
    if(pcol>=cLo&&pcol<cHi){ glColor3f(1,1,0.2f);glLineWidth(2.f);
        glBegin(GL_LINES);glVertex2f(xc((float)pcol),y0);glVertex2f(xc((float)pcol),y1);glEnd(); }
}

// ---------------- 3D ----------
static void setup_3d(){ int mh=main_h(); glViewport(0,A.cH-panel_y0(),A.cW,mh); glEnable(GL_DEPTH_TEST);
    glMatrixMode(GL_PROJECTION);glLoadIdentity(); gluPerspective(45.0,(double)A.cW/mh,0.05,100.0);
    glMatrixMode(GL_MODELVIEW);glLoadIdentity();
    float ry=A.yaw*3.14159f/180,rp=A.pitch*3.14159f/180;
    gluLookAt(A.dist*std::cos(rp)*std::sin(ry),A.dist*std::sin(rp),A.dist*std::cos(rp)*std::cos(ry),0,0,0,0,1,0);
    glGetDoublev(GL_MODELVIEW_MATRIX,g_mv); glGetDoublev(GL_PROJECTION_MATRIX,g_pj); glGetIntegerv(GL_VIEWPORT,g_vp); }  // para proyectar manijas
static void map_axes(float t,float f,float e,float&X,float&Y,float&Z){
    float er=(e-A.dbAxisLo)/((A.dbAxisHi-A.dbAxisLo)>1e-4f?(A.dbAxisHi-A.dbAxisLo):1e-4f);  // eje dB = banda del filtro
    er=er<0?0:(er>1?1:er);
    float fr=(f-A.fAxisLo)/((A.fAxisHi-A.fAxisLo)>1e-4f?(A.fAxisHi-A.fAxisLo):1e-4f);       // eje freq = banda del filtro
    fr=fr<0?0:(fr>1?1:fr);
    float dim[3]={t*2-1,1.f-2.f*fr,er*2-1};                          // freq INVERTIDA (0 al frente); freq y dB reescalados
    const int*p=PERM[A.axperm]; float o[3];
    for(int a=0;a<3;++a){ float v=dim[p[a]]; v*=A.ax[a]; o[a]=v; }   // escala por eje (manijas del cubo)
    X=o[0];Y=o[1];Z=o[2]; }
// caja con REJILLA en las 3 caras (piso + 2 paredes traseras), estilo grafico 3D
// matplotlib + ejes de color en la esquina inferior-izquierda-frontal.
static void draw_cube_frame(float Lx,float Ly,float Lz){
    const int N=5; glLineWidth(1.f);
    glColor3f(0.16f,0.16f,0.20f); glBegin(GL_LINES);       // rejilla de las 3 caras
    for(int i=0;i<=N;++i){ float a=-1+2.f*i/N;
        glVertex3f(-Lx,-Ly,a*Lz);glVertex3f(Lx,-Ly,a*Lz); glVertex3f(a*Lx,-Ly,-Lz);glVertex3f(a*Lx,-Ly,Lz);   // piso
        glVertex3f(-Lx,a*Ly,-Lz);glVertex3f(Lx,a*Ly,-Lz); glVertex3f(a*Lx,-Ly,-Lz);glVertex3f(a*Lx,Ly,-Lz);   // pared trasera
        glVertex3f(-Lx,a*Ly,-Lz);glVertex3f(-Lx,a*Ly,Lz); glVertex3f(-Lx,-Ly,a*Lz);glVertex3f(-Lx,Ly,a*Lz); } // pared lateral
    glEnd();
    glColor3f(0.38f,0.38f,0.44f);                          // marco exterior del cubo
    float c[8][3]={{-Lx,-Ly,-Lz},{Lx,-Ly,-Lz},{Lx,Ly,-Lz},{-Lx,Ly,-Lz},{-Lx,-Ly,Lz},{Lx,-Ly,Lz},{Lx,Ly,Lz},{-Lx,Ly,Lz}};
    int ed[12][2]={{0,1},{1,2},{2,3},{3,0},{4,5},{5,6},{6,7},{7,4},{0,4},{1,5},{2,6},{3,7}};
    glBegin(GL_LINES); for(auto&e:ed){glVertex3fv(c[e[0]]);glVertex3fv(c[e[1]]);} glEnd();
    glLineWidth(2.f); glBegin(GL_LINES);                                     // ejes en aristas FRONTALES
    glColor3f(.8f,.4f,.4f);glVertex3f(-Lx,-Ly,Lz);glVertex3f(Lx,-Ly,Lz);     // X=Tiempo: arista inferior del frente
    glColor3f(.4f,.8f,.4f);glVertex3f(-Lx,-Ly,Lz);glVertex3f(-Lx,Ly,Lz);     // Y=dB: arista vertical frontal izquierda
    glColor3f(.4f,.4f,.9f);glVertex3f(Lx,-Ly,Lz);glVertex3f(Lx,-Ly,-Lz);     // Z=Frecuencia: arista inferior derecha (al fondo)
    glEnd();
}
// valor real de una dimension (0=tiempo s, 1=freq, 2=dB rel) en fraccion t[0,1]
static std::string axval(int dim, float t){ char b[40];
    if(dim==0){ double dur=A.sr?(double)A.audio.samples.size()/A.sr:0; snprintf(b,40,"%.2f",t*dur); }
    else if(dim==1){ double ny=A.sr*0.5; double fr=A.fAxisLo+t*(A.fAxisHi-A.fAxisLo); double f=fr*ny;  // eje freq = banda del filtro
        if(ny>=1000) snprintf(b,40,"%.1fk",f/1000.0); else snprintf(b,40,"%.0f",f); }
    else { double eat=A.dbAxisLo+t*(A.dbAxisHi-A.dbAxisLo); snprintf(b,40,"%.0f",(eat-1.0)*A.P.dyn_range_db); }  // eje dB dinamico
    return b; }
// titulo del eje con unidad (como en la imagen de referencia)
static const char* axtitle(int dim){ return dim==0?"Tiempo (s)":(dim==1?"Frecuencia (Hz)":"Nivel (dB)"); }
static void draw_axes_labels(){ const int*p=PERM[A.axperm]; float Lx=A.ax[0],Ly=A.ax[1],Lz=A.ax[2]; draw_cube_frame(Lx,Ly,Lz);
    // fraccion de dato segun la posicion geom v[-1,1]; el eje FRECUENCIA (dim 1) va invertido (0 al frente)
    auto frac=[&](int dim,float v){ return dim==1 ? (1.f-v)*0.5f : (v+1.f)*0.5f; };
    glColor3f(0.85f,0.85f,0.9f); float vs[5]={-1.f,-0.5f,0.f,0.5f,1.f};       // VALORES en las aristas frontales
    for(float v:vs){
        FONT.at3d(v*Lx,-Ly-0.07f,Lz, axval(p[0],frac(p[0],v)));      // X = arista inferior del FRENTE
        FONT.at3d(-Lx-0.07f,v*Ly,Lz, axval(p[1],frac(p[1],v)));      // Y = arista vertical frontal IZQUIERDA
        FONT.at3d(Lx+0.05f,-Ly-0.07f,v*Lz, axval(p[2],frac(p[2],v)));    // Z = arista inferior DERECHA
        FONT.at3d(Lx+0.05f,Ly+0.04f,v*Lz, axval(p[2],frac(p[2],v))); }   // Z = arista SUPERIOR DERECHA (tambien)
    // arista superior-derecha resaltada (une la esquina trasera-arriba con la delantera-arriba)
    glColor3f(.4f,.4f,.9f); glLineWidth(2.f); glBegin(GL_LINES); glVertex3f(Lx,Ly,Lz); glVertex3f(Lx,Ly,-Lz); glEnd();
    glColor3f(1,1,1);                                        // TITULOS de cada eje
    FONT.at3d(0.f,-Ly-0.18f,Lz, axtitle(p[0]));              // Tiempo (s)  -> frente-abajo
    FONT.at3d(-Lx-0.12f,Ly+0.06f,Lz, axtitle(p[1]));         // Nivel (dB)  -> frontal-izq vertical
    FONT.at3d(Lx+0.05f,-Ly-0.18f,0.f, axtitle(p[2]));        // Frecuencia (Hz) -> inferior-derecha
    FONT.at3d(Lx+0.05f,Ly+0.16f,0.f, axtitle(p[2]));         // Frecuencia (Hz) -> superior-derecha
    glPointSize(11.f); glBegin(GL_POINTS);                  // manijas (redimensionar) en los extremos de cada eje frontal
    glColor3f(1,.5f,.5f); glVertex3f(Lx,-Ly,Lz);            // X (tiempo)
    glColor3f(.5f,1,.5f); glVertex3f(-Lx,Ly,Lz);            // Y (dB)
    glColor3f(.5f,.6f,1); glVertex3f(Lx,Ly,-Lz);            // Z (frecuencia) -> manija ARRIBA (arista superior derecha)
    glEnd(); glPointSize(1.f);
}
// posicion en pantalla (top-left) de un punto 3D del cubo
static bool project3(double X,double Y,double Z,double&sx,double&sy){ double wx,wy,wz;
    if(gluProject(X,Y,Z,g_mv,g_pj,g_vp,&wx,&wy,&wz)){ sx=wx; sy=A.cH-wy; return true; } return false; }
// detecta si (mx,my) cae sobre una manija de eje; devuelve eje 0/1/2 o -1, y guarda direccion
static int pick_handle(int mx,int my){ float Lx=A.ax[0],Ly=A.ax[1],Lz=A.ax[2];
    double ox,oy; if(!project3(-Lx,-Ly,Lz,ox,oy))return -1;        // origen del cubo (frente-izq-abajo)
    double tip[3][3]={{Lx,-Ly,Lz},{-Lx,Ly,Lz},{Lx,Ly,-Lz}};       // tips: X(tiempo), Y(dB), Z(frecuencia ARRIBA)
    for(int a=0;a<3;++a){ double hx,hy; if(!project3(tip[a][0],tip[a][1],tip[a][2],hx,hy))continue;
        double dx=mx-hx,dy=my-hy; if(dx*dx+dy*dy<=14*14){ A.adx=hx-ox; A.ady=hy-oy;
            double L=std::sqrt(A.adx*A.adx+A.ady*A.ady)+1e-6; A.adx/=L; A.ady/=L; return a; } }
    return -1; }

// dibuja un prisma 3D (rango tiempo x frecuencia x energia) - wireframe
static void draw_box3d(float t0,float t1,float fL,float fH,float e0,float e1){
    float P8[8][3]; int idx=0;
    float ts[2]={t0,t1}, fs[2]={fL,fH}, es[2]={e0,e1};
    for(int a=0;a<2;++a)for(int b=0;b<2;++b)for(int cc=0;cc<2;++cc){
        map_axes(ts[a],fs[b],es[cc],P8[idx][0],P8[idx][1],P8[idx][2]); idx++; }
    int ed[12][2]={{0,1},{2,3},{4,5},{6,7},{0,2},{1,3},{4,6},{5,7},{0,4},{1,5},{2,6},{3,7}};
    glBegin(GL_LINES); for(auto&e:ed){glVertex3fv(P8[e[0]]);glVertex3fv(P8[e[1]]);} glEnd();
}
// dibuja una etiqueta POLIGONO en 3D TAL CUAL: extruye el contorno (col,row) entre
// e0 y e1 -> dos bucles (abajo/arriba) + aristas verticales en cada vertice.
static void draw_poly3d(const Det& d, float e0, float e1){ int H=A.spec.H; int n=(int)d.px.size(); if(n<3)return;
    auto cl=[&](float v){return v<0?0:(v>1?1:v);};
    auto vert=[&](int k,float e,float&X,float&Y,float&Z){ float t=cl(tnorm(d.px[k])), f=(float)(H-1-d.py[k])/(H-1); map_axes(t,f,e,X,Y,Z); };
    for(float e:{e0,e1}){ glBegin(GL_LINE_LOOP); for(int k=0;k<n;++k){ float X,Y,Z; vert(k,e,X,Y,Z); glVertex3f(X,Y,Z);} glEnd(); }
    glBegin(GL_LINES); for(int k=0;k<n;++k){ float X,Y,Z; vert(k,e0,X,Y,Z); glVertex3f(X,Y,Z); vert(k,e1,X,Y,Z); glVertex3f(X,Y,Z);} glEnd();
}
// marca en 3D las etiquetas: BBOX como prisma rectangular, POLIGONO como su forma real.
static void draw_boxes_3d(){ int W=A.spec.W,H=A.spec.H; if(W<2||H<2||A.hide_labels)return; glLineWidth(2.f);
    auto cl=[&](float v){return v<0?0:(v>1?1:v);};
    auto rng=[&](const Det&d,float&t0,float&t1,float&fL,float&fH){
        t0=cl(tnorm(d.x)); t1=cl(tnorm(d.x+d.w));
        fH=(float)(H-1-d.y)/(H-1); fL=(float)(H-1-(d.y+d.h))/(H-1); };
    for(int i=0;i<(int)A.dets.size();++i){ const Det&d=A.dets[i];
        float cr,cg,cb; class_color(d.cls,cr,cg,cb); glColor3f(cr,cg,cb);
        glLineWidth(i==A.sel?3.f:2.f);
        if(d.kind==KIND_POLY&&d.px.size()>=3) draw_poly3d(d,0.f,1.f);       // poligono tal cual
        else { float t0,t1,fL,fH; rng(d,t0,t1,fL,fH); draw_box3d(t0,t1,fL,fH,0.f,1.f); } }  // bounding box
    if(A.sc0>=0&&A.sc1>A.sc0){ float t0=cl(tnorm(A.sc0)),t1=cl(tnorm(A.sc1)),fL=0,fH=1;
        if(A.sr0>=0&&A.sr1>=0){int rmn=min(A.sr0,A.sr1),rmx=max(A.sr0,A.sr1);fH=(float)(H-1-rmn)/(H-1);fL=(float)(H-1-rmx)/(H-1);}
        glColor3f(1,1,1);glLineWidth(2.f); draw_box3d(t0,t1,fL,fH,0.f,1.f); } }
static int playhead_col(){ long long cs=PLAYER.cur_sample(); if(cs>=0)A.cursor_col=(int)(cs/A.P.hop); return A.cursor_col; }  // sigue y se queda donde termina
// playhead en 3D: LINEA que recorre la FRECUENCIA en el instante actual,
// trazando el espectro de esa columna sobre el relieve.
static void draw_playhead_3d(){ int H=A.spec.H; int pc=playhead_col(); if(!cin(pc))return;
    float t=tnorm(pc);
    glColor3f(1,1,0.25f); glLineWidth(3.f); glBegin(GL_LINE_STRIP);
    for(int r=0;r<H;++r){ float f=(float)(H-1-r)/(H-1); float e=A.enh.at(r,pc);
        float X,Y,Z; map_axes(t,f,e,X,Y,Z); glVertex3f(X,Y,Z); }
    glEnd(); }

static void render_terrain3d(){ setup_3d(); int W=A.enh.W,H=A.enh.H; int st=max(1,max(W,H)/A.res3d);
    int rTop=max(0,(int)((H-1)*(1-A.fHi))), rBot=min(H-1,(int)((H-1)*(1-A.fLo)));  // banda de freq
    for(int r=rTop;r+st<=rBot;r+=st){ glBegin(GL_TRIANGLE_STRIP);
        for(int c=vlo();c<vhi();c+=st) for(int rr=r;rr<=r+st;rr+=st){ float t=tnorm(c),f=(float)(H-1-rr)/(H-1);
            float e=A.enh.at(rr,c); if(e<A.dbMin||e>A.dbMax)e=0;            // banda de dB
            float X,Y,Z;map_axes(t,f,e,X,Y,Z);float cr,cg,cb;colorf(e,cr,cg,cb);glColor3f(cr,cg,cb);glVertex3f(X,Y,Z);} glEnd(); }
    draw_boxes_3d(); draw_playhead_3d(); draw_axes_labels(); }
// Vista 6: nube de puntos volumetrica (tiempo,freq,energia); alfa por energia
static void render_pointcloud(){ setup_3d(); int W=A.enh.W,H=A.enh.H; int st=max(1,max(W,H)/A.res3d);
    glEnable(GL_BLEND);glBlendFunc(GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA); glPointSize(2.5f); glBegin(GL_POINTS);
    for(int r=0;r<H;r+=st)for(int c=vlo();c<vhi();c+=st){ float e=A.enh.at(r,c); float f=(float)(H-1-r)/(H-1);
        if(e<0.10f||!pass_filt(f,e))continue; float t=tnorm(c); float X,Y,Z;map_axes(t,f,e,X,Y,Z);
        float cr,cg,cb;colorf(e,cr,cg,cb); glColor4f(cr,cg,cb,min(1.f,0.15f+e)); glVertex3f(X,Y,Z);} glEnd();
    glDisable(GL_BLEND); draw_boxes_3d(); draw_playhead_3d(); draw_axes_labels(); }
static void render_rios3d(){ setup_3d(); int W=A.enh.W,H=A.enh.H; glLineWidth(2.f);
    for(auto&h:A.rios){
        if(A.rio_completo){            // modo HILO COMPLETO: si algun punto pasa, dibuja todo el hilo
            bool any=false;
            for(size_t k=0;k<h.col.size();++k){ float f=(float)(H-1-h.row[k])/(H-1),e=A.enh.at(h.row[k],h.col[k]);
                if(pass_filt(f,e)){any=true;break;} }
            if(!any) continue;
            bool pen=false;
            for(size_t k=0;k<h.col.size();++k){ if(!cin(h.col[k])){ if(pen){glEnd();pen=false;} continue; }
                if(!pen){glBegin(GL_LINE_STRIP);pen=true;}
                float t=tnorm(h.col[k]),f=(float)(H-1-h.row[k])/(H-1),e=A.enh.at(h.row[k],h.col[k]);
                float X,Y,Z;map_axes(t,f,e,X,Y,Z);float cr,cg,cb;colorf(e,cr,cg,cb);glColor3f(cr,cg,cb);glVertex3f(X,Y,Z);}
            if(pen)glEnd();
        } else {                       // modo corte por vertice (oculta los tramos fuera de rango)
            bool pen=false;
            for(size_t k=0;k<h.col.size();++k){ float t=tnorm(h.col[k]),f=(float)(H-1-h.row[k])/(H-1),e=A.enh.at(h.row[k],h.col[k]);
                if(!pass_filt(f,e)||!cin(h.col[k])){ if(pen){glEnd();pen=false;} continue; }
                if(!pen){glBegin(GL_LINE_STRIP);pen=true;}
                float X,Y,Z;map_axes(t,f,e,X,Y,Z);float cr,cg,cb;colorf(e,cr,cg,cb);glColor3f(cr,cg,cb);glVertex3f(X,Y,Z);}
            if(pen)glEnd();
        }
    }
    draw_boxes_3d(); draw_playhead_3d(); draw_axes_labels(); }
// Vista 6: QUIVER3D - por cada hilo, palos verticales (eje dB) entre la curva
// (crestas, borde superior) y la envolvente inferior que CONECTA LOS VALLES.
// Cada joroba del hilo queda como un area rellena de palitos.
static void render_quiver3d(){ setup_3d(); int H=A.enh.H;
    glLineWidth(2.f); glBegin(GL_LINES);
    for(auto&h:A.rios){ int n=(int)h.col.size(); if(n<2)continue;
        bool any=false; for(int k=0;k<n;++k){ float f=(float)(H-1-h.row[k])/(H-1),e=A.enh.at(h.row[k],h.col[k]); if(pass_filt(f,e)){any=true;break;} }
        if(A.quiver_completo && !any)continue;                               // glifos completos: hilo entero si algun punto pasa
        std::vector<float> e(n); for(int k=0;k<n;++k) e[k]=A.enh.at(h.row[k],h.col[k]);
        std::vector<int> val; val.push_back(0);                              // valles (minimos locales) + extremos
        for(int k=1;k<n-1;++k) if(e[k]<=e[k-1]&&e[k]<=e[k+1]) val.push_back(k);
        val.push_back(n-1);
        std::vector<float> env(n,0);                                         // envolvente inferior (lineal entre valles)
        for(int s=0;s+1<(int)val.size();++s){ int a=val[s],b=val[s+1]; float ea=e[a],eb=e[b];
            for(int k=a;k<=b;++k) env[k]=(b>a)?ea+(eb-ea)*(float)(k-a)/(b-a):ea; }
        for(int k=0;k<n;++k){ int c=h.col[k],r=h.row[k]; if(!cin(c))continue;
            if(!A.quiver_completo){ float fp=(float)(H-1-r)/(H-1); if(!pass_filt(fp,e[k]))continue; }  // solo puntos que pasan
            float eb=max(env[k],A.dbMin), et=min(e[k],A.dbMax); if(et<=eb)continue;  // palo de envolvente a cresta
            float t=tnorm(c),f=(float)(H-1-r)/(H-1);
            float Xb,Yb,Zb,Xt,Yt,Zt; map_axes(t,f,eb,Xb,Yb,Zb); map_axes(t,f,et,Xt,Yt,Zt);
            float cr,cg,cb;colorf(e[k],cr,cg,cb);glColor3f(cr,cg,cb); glVertex3f(Xb,Yb,Zb); glVertex3f(Xt,Yt,Zt); } }
    glEnd();
    draw_boxes_3d(); draw_playhead_3d(); draw_axes_labels(); }
// Vista 5: CASCADA ESPECTRAL — por cada rebanada de tiempo (Z=profundidad) dibuja el ESPECTRO de
// esa columna (X = FRECUENCIA, Y = energia). Marca los PICOS (señales) y ETIQUETA en Hz las
// frecuencias DOMINANTES del rango -> se ve QUE señales hay y A QUE frecuencia. Respeta los filtros.
static void render_cascade3d(){ setup_3d();
    int W=A.spec.W,H=A.spec.H; float Lx=A.ax[0],Ly=A.ax[1],Lz=A.ax[2]; double ny=A.sr*0.5;
    int rTop=max(0,(int)((H-1)*(1.f-A.fHi))), rBot=min(H-1,(int)((H-1)*(1.f-A.fLo)));   // banda de freq del filtro
    if(rBot<=rTop+1){rTop=0;rBot=H-1;}
    int c0=max(0,min(W-1,vlo())), c1=max(c0+1,min(W,vhi()));
    int NSEG=max(20,min(200,A.res3d));
    auto rowX=[&](int r){ float ff=(float)(H-1-r)/(H-1); float u=(ff-A.fLo)/max(1e-4f,A.fHi-A.fLo); u=u<0?0:(u>1?1:u); return (u*2-1)*Lx; };  // freq baja = IZQ
    auto eF=[&](int r,int c){ float e=A.enh.at(r,c); return (e<A.dbMin||e>A.dbMax)?0.f:e; };   // respeta el filtro de dB
    std::vector<int> peakHist(H,0);
    if(A.enh.W>=2) for(int sg=0;sg<NSEG;++sg){ float z=((float)sg/max(1,NSEG-1)*2-1)*Lz;
        int c=c0+(int)((float)sg/max(1,NSEG-1)*(c1-1-c0)); if(c<c0)c=c0; if(c>=W)c=W-1;
        float m=0.f; for(int r=rTop;r<=rBot;++r){ float e=eF(r,c); if(e>m)m=e; }              // pico relativo a la rebanada
        float thr=max(0.5f,0.55f*m);
        glLineWidth(1.4f); glBegin(GL_LINE_STRIP);                                            // espectro de la rebanada
        for(int r=rTop;r<=rBot;++r){ float e=eF(r,c); float x=rowX(r),y=(e*2-1)*Ly;
            float cr,cg,cb;colorf(e,cr,cg,cb);glColor3f(cr,cg,cb); glVertex3f(x,y,z);} glEnd();
        glColor3f(1,1,1); glPointSize(4.f); glBegin(GL_POINTS);                               // PICOS = señales
        for(int r=rTop+1;r<rBot;++r){ float e=eF(r,c); if(e<thr)continue;
            if(e>=eF(r-1,c)&&e>=eF(r+1,c)){ peakHist[r]++; glVertex3f(rowX(r),(e*2-1)*Ly,z);} }
        glEnd(); glPointSize(1.f);
    }
    int pc=playhead_col(); if(pc>=c0&&pc<c1){ float zp=((float)(pc-c0)/max(1,c1-c0)*2-1)*Lz;  // plano del playhead
        glEnable(GL_BLEND);glBlendFunc(GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA);glColor4f(1,1,0.2f,0.16f);
        glBegin(GL_QUADS);glVertex3f(-Lx,-Ly,zp);glVertex3f(Lx,-Ly,zp);glVertex3f(Lx,Ly,zp);glVertex3f(-Lx,Ly,zp);glEnd();glDisable(GL_BLEND); }
    draw_cube_frame(Lx,Ly,Lz);                                                                // marco + rejilla (redimensionable)
    glColor3f(1,1,1); FONT.at3d(0.f,-Ly-0.18f,Lz,"Frecuencia (Hz)"); FONT.at3d(-Lx-0.14f,Ly,Lz,"Energia"); FONT.at3d(Lx+0.05f,-Ly,0.f,"Tiempo (s)");
    glColor3f(0.85f,0.85f,0.9f);                                                              // ticks de frecuencia (X)
    for(int i=0;i<=5;++i){ float u=i/5.f; float x=(u*2-1)*Lx; double hz=(A.fLo+u*(A.fHi-A.fLo))*ny;
        char b[24]; if(ny>=1000)snprintf(b,24,"%.1fk",hz/1000.0);else snprintf(b,24,"%.0f",hz); FONT.at3d(x,-Ly-0.07f,Lz,b); }
    double dur=A.sr?(double)(c1-c0)*A.P.hop/A.sr:0, off=A.sr?(double)c0*A.P.hop/A.sr:0;        // ticks de tiempo (Z)
    for(int i=0;i<=4;++i){ float v=(i/4.f)*2-1; char b[24]; snprintf(b,24,"%.2fs",off+(i/4.f)*dur); FONT.at3d(Lx+0.05f,-Ly-0.05f,v*Lz,b); }
    // FRECUENCIAS DOMINANTES: linea guia (cian) + etiqueta en Hz en las mas persistentes (= señales)
    std::vector<std::pair<int,int>> pk; for(int r=rTop;r<=rBot;++r) if(peakHist[r]>0) pk.push_back(std::make_pair(peakHist[r],r));
    std::sort(pk.begin(),pk.end(),[](const std::pair<int,int>&a,const std::pair<int,int>&b){return a.first>b.first;});
    glDisable(GL_DEPTH_TEST); int shown=0; std::vector<float> usedX;
    for(size_t k=0;k<pk.size()&&shown<6;++k){ int r=pk[k].second; float x=rowX(r);
        bool dup=false; for(float ux:usedX) if(std::fabs(x-ux)<Lx*0.16f)dup=true; if(dup)continue;
        usedX.push_back(x); shown++;
        glColor4f(0.3f,1.f,1.f,0.95f); glLineWidth(1.6f); glBegin(GL_LINES); glVertex3f(x,-Ly,Lz); glVertex3f(x,Ly,Lz); glEnd();
        double hz=((double)(H-1-r)/(H-1))*ny; char b[24]; if(ny>=1000)snprintf(b,24,"%.1f kHz",hz/1000.0);else snprintf(b,24,"%.0f Hz",hz);
        glColor3f(0.5f,1.f,1.f); FONT.at3d(x,Ly+0.06f,Lz,b); }
    glPointSize(11.f); glBegin(GL_POINTS);                                                    // MANIJAS de redimension
    glColor3f(1,.5f,.5f); glVertex3f(Lx,-Ly,Lz); glColor3f(.5f,1,.5f); glVertex3f(-Lx,Ly,Lz); glColor3f(.5f,.6f,1); glVertex3f(Lx,Ly,-Lz);
    glEnd(); glPointSize(1.f); glEnable(GL_DEPTH_TEST); }

// Vista 9: VOLUMEN como NUBE DE PUNTOS DENSA por cada poligono etiquetado.
// UN punto por (tiempo, frecuencia) en su valor REAL de dB (no toda la columna), con
// sub-muestreo del eje de FRECUENCIA (SUBF). Respeta los filtros LOCALES de la vista
// (barras volFLo/volFHi y volDbMin/volDbMax), que NO afectan a las demas vistas.
static void render_volume3d(){ setup_3d(); int W=A.enh.W,H=A.enh.H; if(W<2||H<2){draw_axes_labels();return;}
    glEnable(GL_BLEND);glBlendFunc(GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA); glPointSize(3.0f);
    // VOLUMEN DENSO: por cada (tiempo,frecuencia) dentro de la etiqueta, APILA puntos desde el
    // piso de dB (volDbMin) hasta el dB REAL -> rellena el cuerpo bajo la superficie (se ve como
    // un volumen solido, no una superficie pobre). Presupuesto de COLUMNAS (stride en t/f) para
    // que no se ralentice; el apilado vertical usa un paso fino en dB.
    long area=0; for(const Det&d:A.dets){ if(d.kind==KIND_POLY&&d.px.size()>=3) area+=(long)d.w*d.h; }
    const long COLBUDGET=22000; int st=1; if(area>0){ st=(int)std::sqrt((double)area/(double)COLBUDGET); if(st<1)st=1; }
    const float dbStep=0.03f;                                   // densidad VERTICAL del apilado (fraccion de dB)
    float lo=A.volDbMin>0?A.volDbMin:0.f;                       // piso del volumen (borde inferior del cubo)
    glBegin(GL_POINTS);
    for(const Det&d:A.dets){ if(d.kind!=KIND_POLY||d.px.size()<3)continue;
        float cr0,cg0,cb0; class_color(d.cls,cr0,cg0,cb0);
        int x0=max(0,d.x),x1=min(W-1,d.x+d.w),y0=max(0,d.y),y1=min(H-1,d.y+d.h);
        for(int c=x0;c<=x1;c+=st){ if(!cin(c))continue; float t=tnorm(c);
            for(int r=y0;r<=y1;r+=st){ if(!pt_in_det(d,c+0.5f,r+0.5f))continue;   // excluye huecos/anillos
                float e=A.enh.at(r,c); float f=(float)(H-1-r)/(H-1);
                if(f<A.volFLo||f>A.volFHi||e<A.volDbMin||e>A.volDbMax)continue;   // FILTROS LOCALES
                float einv=1.f/max(0.001f,e);
                for(float ev=lo; ev<=e+1e-4f; ev+=dbStep){                        // APILA volDbMin..dB_real -> cuerpo del volumen
                    float X,Y,Z; map_axes(t,f,ev,X,Y,Z);
                    float pr,pg,pb; colorf(ev,pr,pg,pb);                          // color = dB de ESE punto (magma) + tinte de clase
                    float a=0.22f+0.62f*(ev*einv);                               // mas opaco hacia la superficie (relieve)
                    glColor4f(min(1.f,pr*0.78f+cr0*0.28f),min(1.f,pg*0.78f+cg0*0.28f),min(1.f,pb*0.78f+cb0*0.28f), min(1.f,a)); glVertex3f(X,Y,Z); } } } }
    glEnd(); glDisable(GL_BLEND);
    draw_boxes_3d(); draw_playhead_3d(); draw_axes_labels(); }


// manijas de edicion del Det seleccionado (poligono: vertices; bbox: 8 manijas)
static void draw_edit_handles(float X0,float Y0,float X1,float Y1,int cLo,int cHi){
    (void)X0;(void)Y0;(void)X1;(void)Y1;(void)cLo;(void)cHi;
    if(A.sel<0||A.sel>=(int)A.dets.size())return; const Det&d=A.dets[A.sel];
    auto P=[&](float c,float r){ float sx,sy; spec_to_main(c,r,sx,sy); glVertex2f(sx,sy); };  // respeta el zoom 2D
    glColor3f(1,1,0.3f); glPointSize(9.f); glBegin(GL_POINTS);
    if(d.kind==KIND_POLY&&d.px.size()>=3){ for(size_t k=0;k<d.px.size();++k) P((float)d.px[k],(float)d.py[k]);
        for(size_t hh=0;hh<d.hx.size();++hh) for(size_t k=0;k<d.hx[hh].size();++k) P((float)d.hx[hh][k],(float)d.hy[hh][k]); }   // manijas de los ANILLOS/huecos (igual que las del contorno exterior)
    else { P((float)d.x,(float)d.y); P((float)(d.x+d.w),(float)d.y); P((float)(d.x+d.w),(float)(d.y+d.h)); P((float)d.x,(float)(d.y+d.h));
        P(d.x+d.w*0.5f,(float)d.y); P(d.x+d.w*0.5f,(float)(d.y+d.h)); P((float)d.x,d.y+d.h*0.5f); P((float)(d.x+d.w),d.y+d.h*0.5f); }
    glEnd(); glPointSize(1.f); }

// ---------------- panel inferior + toolbar + hud ----------
static void render_panel(){ ortho2d(); float py=panel_y0(); int W=A.spec.W;
    if(W<1)return;
    float wx0=(float)vlo()/W*A.cW, wx1=(float)vhi()/W*A.cW;   // ventana visible
    // tira de espectrograma = NAVEGADOR (todo el audio)
    draw_tex_quad(0,py,A.cW,py+STRIP_H);
    draw_overlays(0,py,A.cW,py+STRIP_H,false,0,W);
    glEnable(GL_BLEND);glBlendFunc(GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA);glColor4f(0,0,0,0.5f);  // oscurece fuera de la ventana
    glBegin(GL_QUADS);glVertex2f(0,py);glVertex2f(wx0,py);glVertex2f(wx0,py+STRIP_H);glVertex2f(0,py+STRIP_H);glEnd();
    glBegin(GL_QUADS);glVertex2f(wx1,py);glVertex2f(A.cW,py);glVertex2f(A.cW,py+STRIP_H);glVertex2f(wx1,py+STRIP_H);glEnd();glDisable(GL_BLEND);
    glColor3f(1,1,0.3f);glLineWidth(2.f);glBegin(GL_LINE_LOOP);glVertex2f(wx0,py+1);glVertex2f(wx1,py+1);glVertex2f(wx1,py+STRIP_H-1);glVertex2f(wx0,py+STRIP_H-1);glEnd();
    float gcy=py+STRIP_H*0.5f; glColor3f(1,1,0.6f);   // manijas para agrandar/achicar
    glBegin(GL_QUADS);glVertex2f(wx0-4,gcy-10);glVertex2f(wx0+4,gcy-10);glVertex2f(wx0+4,gcy+10);glVertex2f(wx0-4,gcy+10);
        glVertex2f(wx1-4,gcy-10);glVertex2f(wx1+4,gcy-10);glVertex2f(wx1+4,gcy+10);glVertex2f(wx1-4,gcy+10);glEnd();
    glColor3f(1,1,1); FONT.at2d(4,py+12,"tira: arrastra=selecciona | ventana: Ctrl+izq o boton central");
    // divisor para subir/bajar el alto del panel (manija)
    glColor3f(0.45f,0.45f,0.5f);glLineWidth(2.f);glBegin(GL_LINES);glVertex2f(0,py);glVertex2f(A.cW,py);glEnd();
    glColor3f(0.8f,0.8f,0.85f);glBegin(GL_QUADS);glVertex2f(A.cW*0.5f-16,py-3);glVertex2f(A.cW*0.5f+16,py-3);glVertex2f(A.cW*0.5f+16,py+3);glVertex2f(A.cW*0.5f-16,py+3);glEnd();
    // oscilograma (todo el audio)
    float oy=py+STRIP_H, oh=OSC_H, mid=oy+oh*0.5f;
    glColor3f(0.08f,0.08f,0.10f); glBegin(GL_QUADS);glVertex2f(0,oy);glVertex2f(A.cW,oy);glVertex2f(A.cW,oy+oh);glVertex2f(0,oy+oh);glEnd();
    glColor3f(0.4f,0.9f,0.5f); glBegin(GL_LINES);
    for(int x=0;x<A.cW;++x){ int b=(int)((double)x/A.cW*A.NB); if(b<0||b>=A.NB)continue;
        glVertex2f(x,mid-A.envMax[b]*oh*0.5f); glVertex2f(x,mid-A.envMin[b]*oh*0.5f);} glEnd();
    glEnable(GL_BLEND);glBlendFunc(GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA);glColor4f(0,0,0,0.5f);  // ventana en oscilograma
    glBegin(GL_QUADS);glVertex2f(0,oy);glVertex2f(wx0,oy);glVertex2f(wx0,oy+oh);glVertex2f(0,oy+oh);glEnd();
    glBegin(GL_QUADS);glVertex2f(wx1,oy);glVertex2f(A.cW,oy);glVertex2f(A.cW,oy+oh);glVertex2f(wx1,oy+oh);glEnd();glDisable(GL_BLEND);
    glColor3f(1,1,0.3f);glLineWidth(1.f);glBegin(GL_LINE_LOOP);glVertex2f(wx0,oy);glVertex2f(wx1,oy);glVertex2f(wx1,oy+oh);glVertex2f(wx0,oy+oh);glEnd();
    long long cs=PLAYER.cur_sample(); if(cs>=0)A.cursor_col=(int)(cs/A.P.hop); int pcol=A.cursor_col;
    if(pcol>=0&&pcol<W){glColor3f(1,1,0.2f);glLineWidth(2.f);float px=(float)pcol/W*A.cW;glBegin(GL_LINES);glVertex2f(px,oy);glVertex2f(px,oy+oh);glEnd();}
    glColor3f(0.7f,0.7f,0.7f); FONT.at2d(4,oy+12,"oscilograma");
    // BARRA DE SCROLL horizontal (semitransparente) en la base del oscilograma: mueve la ventana.
    // Rueda del raton sobre esta banda = desplazar. El pulgar (cian) marca [vlo,vhi].
    float sb0=(float)(A.cH-SCROLL_H), sb1=(float)A.cH;
    glEnable(GL_BLEND);glBlendFunc(GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA);
    glColor4f(0.10f,0.10f,0.14f,0.5f); glBegin(GL_QUADS);glVertex2f(0,sb0);glVertex2f((float)A.cW,sb0);glVertex2f((float)A.cW,sb1);glVertex2f(0,sb1);glEnd();
    glColor4f(0.35f,0.8f,1.f,0.55f); glBegin(GL_QUADS);glVertex2f(wx0,sb0+1);glVertex2f(wx1,sb0+1);glVertex2f(wx1,sb1-1);glVertex2f(wx0,sb1-1);glEnd();   // pulgar
    glColor4f(0.6f,0.95f,1.f,0.9f); glLineWidth(1.f);glBegin(GL_LINE_LOOP);glVertex2f(wx0,sb0+1);glVertex2f(wx1,sb0+1);glVertex2f(wx1,sb1-1);glVertex2f(wx0,sb1-1);glEnd();
    glDisable(GL_BLEND);
}
// iconos dibujados (y hacia abajo). id: 0 abrir,1 play,2 pausa,3 stop,4 zoom+,5 zoom-,6 ajustar,7 guardar
static void draw_icon(int id,float cx,float cy,float s){
    const float TP=6.2831853f;
    switch(id){
    case 0: glBegin(GL_LINE_LOOP);glVertex2f(cx-s,cy-s*0.6f);glVertex2f(cx-s*0.1f,cy-s*0.6f);glVertex2f(cx+s*0.1f,cy-s);glVertex2f(cx+s,cy-s);glVertex2f(cx+s,cy+s);glVertex2f(cx-s,cy+s);glEnd(); break; // carpeta
    case 1: glBegin(GL_TRIANGLES);glVertex2f(cx-s,cy-s);glVertex2f(cx-s,cy+s);glVertex2f(cx+s,cy);glEnd(); break; // play
    case 2: glBegin(GL_QUADS);glVertex2f(cx-s,cy-s);glVertex2f(cx-s*0.3f,cy-s);glVertex2f(cx-s*0.3f,cy+s);glVertex2f(cx-s,cy+s);
            glVertex2f(cx+s*0.3f,cy-s);glVertex2f(cx+s,cy-s);glVertex2f(cx+s,cy+s);glVertex2f(cx+s*0.3f,cy+s);glEnd(); break; // pausa
    case 3: glBegin(GL_QUADS);glVertex2f(cx-s,cy-s);glVertex2f(cx+s,cy-s);glVertex2f(cx+s,cy+s);glVertex2f(cx-s,cy+s);glEnd(); break; // stop
    case 4: case 5:{ float r=s*0.65f,ox=cx-s*0.25f,oy=cy-s*0.25f; glBegin(GL_LINE_LOOP);for(int i=0;i<14;++i){float a=i*TP/14;glVertex2f(ox+r*std::cos(a),oy+r*std::sin(a));}glEnd();
            glBegin(GL_LINES);glVertex2f(ox+r*0.7f,oy+r*0.7f);glVertex2f(cx+s,cy+s); glVertex2f(ox-r*0.5f,oy);glVertex2f(ox+r*0.5f,oy);
            if(id==4){glVertex2f(ox,oy-r*0.5f);glVertex2f(ox,oy+r*0.5f);} glEnd(); } break; // lupa +/-
    case 6: glBegin(GL_LINES);glVertex2f(cx-s,cy-s);glVertex2f(cx-s,cy+s);glVertex2f(cx-s,cy);glVertex2f(cx-s*0.4f,cy);
            glVertex2f(cx+s,cy-s);glVertex2f(cx+s,cy+s);glVertex2f(cx+s,cy);glVertex2f(cx+s*0.4f,cy);glEnd(); break; // ajustar [ ]
    case 7: glBegin(GL_LINE_LOOP);glVertex2f(cx-s,cy-s);glVertex2f(cx+s,cy-s);glVertex2f(cx+s,cy+s);glVertex2f(cx-s,cy+s);glEnd();
            glBegin(GL_QUADS);glVertex2f(cx-s*0.5f,cy-s);glVertex2f(cx+s*0.5f,cy-s);glVertex2f(cx+s*0.5f,cy-s*0.2f);glVertex2f(cx-s*0.5f,cy-s*0.2f);glEnd(); break; // guardar
    }
}
static void render_toolbar(){ ortho2d();
    glColor3f(0.10f,0.10f,0.13f); glBegin(GL_QUADS);glVertex2f(0,HUD_H);glVertex2f(A.cW,HUD_H);glVertex2f(A.cW,HUD_H+A.toolbar_h);glVertex2f(0,HUD_H+A.toolbar_h);glEnd();
    glColor3f(0.4f,0.4f,0.45f);glLineWidth(1.f);glBegin(GL_LINES);                  // separadores de seccion
    for(auto&sp:g_seps){glVertex2f((float)sp[0],(float)sp[1]);glVertex2f((float)sp[0],(float)sp[1]+20);}glEnd();
    for(auto&b:A.botones){ bool on=false;
        if(b.key=='1'+A.view-1)on=true; if(b.key=='B'&&A.solo_banda)on=true;
        if(b.key=='k'&&PLAYER.paused)on=true;
        if(b.key=='H'&&A.rio_completo)on=true;
        if(b.key=='G'&&A.quiver_completo)on=true;
        if(b.key=='S'&&A.tool==T_SELECT)on=true;
        if(b.key=='Y'&&A.tool==T_BBOX)on=true; if(b.key=='P'&&A.tool==T_POLY)on=true;
        if(b.key=='X'&&A.tool==T_CUT)on=true; if(b.key=='J'&&A.tool==T_MERGE)on=true; if(b.key=='d'&&A.tool==T_ERASER)on=true;
        if(b.key=='O'&&A.hide_labels)on=true; if(b.key=='L'&&A.listOpen)on=true;
        if(on)glColor3f(0.25f,0.45f,0.65f);else glColor3f(0.20f,0.20f,0.24f);
        glBegin(GL_QUADS);glVertex2f(b.x,b.y);glVertex2f(b.x+b.w,b.y);glVertex2f(b.x+b.w,b.y+b.h);glVertex2f(b.x,b.y+b.h);glEnd();
        glColor3f(0.5f,0.5f,0.55f);glLineWidth(1.f);glBegin(GL_LINE_LOOP);glVertex2f(b.x,b.y);glVertex2f(b.x+b.w,b.y);glVertex2f(b.x+b.w,b.y+b.h);glVertex2f(b.x,b.y+b.h);glEnd();
        glColor3f(1,1,1);
        if(b.icon>=0) draw_icon(b.icon,b.x+b.w*0.5f,b.y+b.h*0.5f,6.f);
        else FONT.at2d(b.x+5,b.y+14,b.label); }
    // paleta de clases: swatch de color + nombre; resalta la activa
    for(auto&p:g_palette){ int c=p[4]; float cr,cg,cb; class_color(c,cr,cg,cb);
        bool act=(c==A.clase_activa);
        glColor3f(act?0.35f:0.18f,act?0.35f:0.18f,act?0.42f:0.22f);
        glBegin(GL_QUADS);glVertex2f(p[0],p[1]);glVertex2f(p[0]+p[2],p[1]);glVertex2f(p[0]+p[2],p[1]+p[3]);glVertex2f(p[0],p[1]+p[3]);glEnd();
        glColor3f(cr,cg,cb);glBegin(GL_QUADS);glVertex2f(p[0]+3,p[1]+4);glVertex2f(p[0]+15,p[1]+4);glVertex2f(p[0]+15,p[1]+16);glVertex2f(p[0]+3,p[1]+16);glEnd();
        glColor3f(act?1.f:0.7f,act?1.f:0.7f,act?1.f:0.75f);glLineWidth(act?2.f:1.f);
        glBegin(GL_LINE_LOOP);glVertex2f(p[0],p[1]);glVertex2f(p[0]+p[2],p[1]);glVertex2f(p[0]+p[2],p[1]+p[3]);glVertex2f(p[0],p[1]+p[3]);glEnd();
        glColor3f(1,1,1);FONT.at2d(p[0]+18,p[1]+14,A.classes[c].name); }
}
static void render_hud(){ ortho2d(); glEnable(GL_BLEND);glBlendFunc(GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA);
    glColor4f(0,0,0,0.6f);glBegin(GL_QUADS);glVertex2f(0,0);glVertex2f(A.cW,0);glVertex2f(A.cW,HUD_H);glVertex2f(0,HUD_H);glEnd();glDisable(GL_BLEND);
    const char* vn[]={"","Espectro2D","Terreno3D","RioEspectral","NubePuntos","CascadaEspectral","Quiver3D","Volumen"};
    const char* tn[]={"Selec","BBox","Poligono","Editar","Cortar","Unir","Borrador"};
    std::string s=std::string("IIAP SachaAcoustic | ")+A.fname+" | V"+std::to_string(A.view)+":"+vn[A.view]+
        "  etiq="+std::to_string(A.dets.size())+"  clase="+class_name(A.clase_activa)+"  tool="+tn[A.tool]+
        (A.tool==T_POLY?"(a mano)":"")+
        (A.tool==T_ERASER?std::string(" r=")+std::to_string(A.eraserRadius):"")+
        "  play="+(A.solo_banda?"banda(t+f)":"toda-freq(t)")+(PLAYER.paused?"  [PAUSA]":"");
    { char vb[24]; snprintf(vb,24,"  vel=%.1fx",A.playSpeed); s+=vb; }   // velocidad de reproduccion
    if(A.view>=2){const int*p=PERM[A.axperm];s+=std::string("  ejes X=")+DIMN[p[0]]+" Y="+DIMN[p[1]]+" Z="+DIMN[p[2]]+"  res3d="+std::to_string(A.res3d);}
    if(A.spec.W>0){ double ny=A.sr*0.5,dr=A.P.dyn_range_db; char fb[120];
        snprintf(fb,120,"  filtro f=%.0f-%.0fHz dB=%.0f..%.0f",A.fLo*ny,A.fHi*ny,(A.dbMin-1)*dr,(A.dbMax-1)*dr); s+=fb; }
    glColor3f(1,1,1); FONT.at2d(6,14,s); }
// Lectura EN VIVO de lo que apunta el cursor: tiempo (s), frecuencia (Hz/kHz) y nivel (dB).
// Se dibuja arriba a la derecha (sobre el HUD) solo si el cursor esta sobre un espectrograma:
// el plot 2D principal (vista 1) o la tira inferior (cualquier vista). dB = (enh-1)*rango, el
// mismo criterio que las barras de filtro y el eje dB 3D (asi el valor coincide con el filtro).
static void render_cursor_readout(){
    if(A.spec.W<1) return;
    int mx=A.mx,my=A.my,col=-1,row=-1;
    bool inStrip=(mx>=0&&mx<A.cW&&my>=panel_y0()&&my<panel_y0()+STRIP_H);
    bool inMain=(A.view==1&&mx>=(int)plotX0()&&mx<=(int)plotX1()&&my>=(int)plotY0()&&my<=(int)plotY1());
    if(inMain) main_to_spec(mx,my,col,row);
    else if(inStrip) strip_to_spec(mx,my,col,row);
    else return;
    if(col<0||col>=A.spec.W||row<0||row>=A.spec.H) return;
    double t=A.sr?(double)col*A.P.hop/(double)A.sr:0;
    double f=(A.sr*0.5)*(double)(A.spec.H-1-row)/(double)max(1,A.spec.H-1);
    double db=((double)A.enh.at(row,col)-1.0)*A.P.dyn_range_db;
    char s[110];
    if(f>=1000.0) snprintf(s,110,"t=%.3f s   f=%.2f kHz   %.1f dB",t,f/1000.0,db);
    else          snprintf(s,110,"t=%.3f s   f=%.0f Hz   %.1f dB",t,f,db);
    ortho2d();
    float tw=(float)strlen(s)*7.0f+14, x=A.cW-tw-2, y=0;
    glEnable(GL_BLEND);glBlendFunc(GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA);
    glColor4f(0.08f,0.18f,0.16f,0.92f);glBegin(GL_QUADS);glVertex2f(x,y);glVertex2f(x+tw,y);glVertex2f(x+tw,y+HUD_H);glVertex2f(x,y+HUD_H);glEnd();
    glColor4f(0.4f,0.9f,0.6f,1.f);glLineWidth(1.f);glBegin(GL_LINE_LOOP);glVertex2f(x,y+1);glVertex2f(x+tw,y+1);glVertex2f(x+tw,y+HUD_H-1);glVertex2f(x,y+HUD_H-1);glEnd();
    glDisable(GL_BLEND);
    glColor3f(0.7f,1.f,0.8f); FONT.at2d(x+7,14,s); }
// ----- combo desplegable de MAPA DE COLOR -----
static bool cmap_btn_rect(float&x,float&y,float&w,float&h){
    for(auto&b:A.botones){ if(b.key=='M'){ x=(float)b.x;y=(float)b.y;w=(float)b.w;h=(float)b.h; return true; } } return false; }
static void cmap_list_rect(float&x,float&y,float&w,float&h){
    float bx,by,bw,bh; if(!cmap_btn_rect(bx,by,bw,bh)){x=y=w=h=0;return;}
    w=235.f; h=(float)(CMAP_N*18+4); x=bx; y=by+bh+2; if(x+w>A.cW)x=A.cW-w-2.f; if(x<2)x=2.f; }
static int cmap_item_at(int mx,int my){ if(!A.cmapOpen)return -1; float x,y,w,h; cmap_list_rect(x,y,w,h);
    if(w<1||mx<x||mx>x+w||my<y+2||my>=y+2+CMAP_N*18)return -1; int idx=(int)((my-(y+2))/18); return (idx>=0&&idx<CMAP_N)?idx:-1; }
static void render_cmap_dropdown(){ if(!A.cmapOpen)return; float x,y,w,h; cmap_list_rect(x,y,w,h); if(w<1)return;
    ortho2d(); glEnable(GL_BLEND);glBlendFunc(GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA);
    glColor4f(0.10f,0.11f,0.14f,0.97f);glBegin(GL_QUADS);glVertex2f(x,y);glVertex2f(x+w,y);glVertex2f(x+w,y+h);glVertex2f(x,y+h);glEnd();
    glColor4f(0.5f,0.6f,0.7f,1.f);glLineWidth(1.f);glBegin(GL_LINE_LOOP);glVertex2f(x,y);glVertex2f(x+w,y);glVertex2f(x+w,y+h);glVertex2f(x,y+h);glEnd();
    int hov=cmap_item_at(A.mx,A.my);
    for(int i=0;i<CMAP_N;++i){ float iy=y+2+i*18;
        if(i==A.cmap){ glColor4f(0.25f,0.45f,0.65f,1.f);glBegin(GL_QUADS);glVertex2f(x+1,iy);glVertex2f(x+w-1,iy);glVertex2f(x+w-1,iy+18);glVertex2f(x+1,iy+18);glEnd(); }
        else if(i==hov){ glColor4f(0.20f,0.28f,0.36f,1.f);glBegin(GL_QUADS);glVertex2f(x+1,iy);glVertex2f(x+w-1,iy);glVertex2f(x+w-1,iy+18);glVertex2f(x+1,iy+18);glEnd(); }
        for(int s=0;s<10;++s){ unsigned char R,G,B; cmap_eval(i,s/9.f,R,G,B); glColor3f(R/255.f,G/255.f,B/255.f);   // mini-muestra de la paleta
            float sx=x+5+s*2.0f; glBegin(GL_QUADS);glVertex2f(sx,iy+4);glVertex2f(sx+2,iy+4);glVertex2f(sx+2,iy+14);glVertex2f(sx,iy+14);glEnd(); }
        glColor3f(1,1,1); FONT.at2d(x+30,iy+13,CMAP_NAMES[i]); }
    glDisable(GL_BLEND); }
// ----- LISTA de TODAS las etiquetas (seleccionar -> resalta en 2D, ordenar por tamano, borrar) -----
static const int LIST_W = 300, LIST_HH = 22, LIST_RH = 16;   // ancho panel, alto cabecera, alto fila
static void list_rect(float&x,float&y,float&w,float&h){ w=(float)LIST_W; x=(float)(A.cW-LIST_W); y=(float)main_y0();
    h=(float)(panel_y0()-main_y0()); if(h<60)h=60; }
// Cache: solo recalcula cuando listOrderDirty=true.
static std::vector<int> list_order() {
    if (A.listOrderDirty) {
        A.cachedListOrder.resize(A.dets.size());
        for (int i = 0; i < (int)A.dets.size(); ++i) A.cachedListOrder[i] = i;
        if (A.listSortSize) std::stable_sort(A.cachedListOrder.begin(), A.cachedListOrder.end(),
            [](int a, int b) { return (long long)A.dets[a].w * A.dets[a].h > (long long)A.dets[b].w * A.dets[b].h; });
        A.listOrderDirty = false;
    }
    return A.cachedListOrder;
}
// Bitmap: reconstruccion lazy cuando listSelDirty=true. Lookup O(1).
static void rebuild_list_sel_bitmap() {
    A.listSelBitmap.assign(A.dets.size(), false);
    for (int s : A.listSel) if (s >= 0 && s < (int)A.dets.size()) A.listSelBitmap[s] = true;
    A.listSelDirty = false;
}
static bool list_is_sel(int i) {
    if (A.listSelDirty) rebuild_list_sel_bitmap();
    if (i < 0 || i >= (int)A.listSelBitmap.size()) return false;
    return A.listSelBitmap[i];
}
static void list_toggle(int i){ for(size_t k=0;k<A.listSel.size();++k) if(A.listSel[k]==i){ A.listSel.erase(A.listSel.begin()+(long)k); return; } A.listSel.push_back(i); }
static void list_select_range(int a,int b){ if(a>b){int t=a;a=b;b=t;} auto order=list_order(); A.listSel.clear();
    for(int r=a;r<=b&&r<(int)order.size();++r) if(r>=0)A.listSel.push_back(order[r]); }
static int list_visrows(){ float x,y,w,h; list_rect(x,y,w,h); return max(1,(int)((h-LIST_HH)/LIST_RH)); }
static int list_row_at(int mx,int my){ if(!A.listOpen)return -1; float x,y,w,h; list_rect(x,y,w,h);   // fila (en orden de display) bajo el raton, o -1
    if(mx<x||mx>x+w||my<y+LIST_HH||my>=y+h)return -1; int r=(int)((my-(y+LIST_HH))/LIST_RH)+A.listScroll;
    return (r>=0&&r<(int)A.dets.size())?r:-1; }
static void list_ensure_visible(int row){ int vis=list_visrows();   // asegura que la fila este a la vista (scroll)
    if(row<A.listScroll)A.listScroll=row; else if(row>=A.listScroll+vis)A.listScroll=row-vis+1; if(A.listScroll<0)A.listScroll=0; }
static void list_reveal(int di){ if(di<0||di>=(int)A.dets.size()||A.spec.W<1)return; const Det&d=A.dets[di];   // si la etiqueta NO esta en la ventana 2D, centra la vista en ella (para ver el resalte)
    int cx=d.x+d.w/2, wv=max(1,vhi()-vlo()); if(cx<vlo()||cx>vhi()){ int a=max(0,min(A.spec.W-wv,cx-wv/2)); A.vc0=a; A.vc1=a+wv; }
    int cy=d.y+d.h/2, hv=max(1,rhi()-rlo()); if(cy<rlo()||cy>rhi()){ int b=max(0,min(A.spec.H-hv,cy-hv/2)); A.vr0=b; A.vr1=b+hv; } }
static void render_label_list(){ if(!A.listOpen)return; ortho2d();
    float x,y,w,h; list_rect(x,y,w,h); auto order=list_order(); int n=(int)order.size();
    int vis=list_visrows(); if(A.listScroll>n-vis)A.listScroll=n-vis; if(A.listScroll<0)A.listScroll=0;
    glEnable(GL_BLEND);glBlendFunc(GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA);
    glColor4f(0.07f,0.08f,0.11f,0.97f);glBegin(GL_QUADS);glVertex2f(x,y);glVertex2f(x+w,y);glVertex2f(x+w,y+h);glVertex2f(x,y+h);glEnd();
    glColor4f(0.5f,0.6f,0.7f,1.f);glLineWidth(1.f);glBegin(GL_LINE_LOOP);glVertex2f(x,y);glVertex2f(x+w,y);glVertex2f(x+w,y+h);glVertex2f(x,y+h);glEnd();
    glColor4f(0.14f,0.16f,0.2f,1.f);glBegin(GL_QUADS);glVertex2f(x,y);glVertex2f(x+w,y);glVertex2f(x+w,y+LIST_HH);glVertex2f(x,y+LIST_HH);glEnd();   // cabecera
    char hb[96]; snprintf(hb,96,"Etiquetas (%d)  sel:%d",n,(int)A.listSel.size());
    glColor3f(1,1,1); FONT.at2d(x+6,y+15,hb);
    glColor3f(0.7f,0.9f,1.f); FONT.at2d(x+w-120,y+15, A.listSortSize?"orden: TAMANO":"orden: id");   // clic en cabecera = alterna orden
    int yend=(int)(y+h);
    for(int r=0;r<vis;++r){ int row=A.listScroll+r; if(row>=n)break; int di=order[row]; if(di<0||di>=(int)A.dets.size())continue;
        const Det&d=A.dets[di]; float ry=y+LIST_HH+r*LIST_RH; if(ry+LIST_RH>yend)break;
        if(list_is_sel(di)){ glColor4f(0.25f,0.45f,0.65f,1.f);glBegin(GL_QUADS);glVertex2f(x+1,ry);glVertex2f(x+w-1,ry);glVertex2f(x+w-1,ry+LIST_RH);glVertex2f(x+1,ry+LIST_RH);glEnd(); }
        else if(row==A.listCursor){ glColor4f(0.17f,0.21f,0.27f,1.f);glBegin(GL_QUADS);glVertex2f(x+1,ry);glVertex2f(x+w-1,ry);glVertex2f(x+w-1,ry+LIST_RH);glVertex2f(x+1,ry+LIST_RH);glEnd(); }
        float cr,cg,cb; class_color(d.cls,cr,cg,cb); glColor3f(cr,cg,cb);   // swatch de color de clase
        glBegin(GL_QUADS);glVertex2f(x+5,ry+3);glVertex2f(x+15,ry+3);glVertex2f(x+15,ry+LIST_RH-3);glVertex2f(x+5,ry+LIST_RH-3);glEnd();
        int ci=d.cls; if(ci<0||ci>=(int)A.classes.size())ci=0;
        char rb[110]; snprintf(rb,110,"%d  %s  %dx%d=%d",di,A.classes[ci].name.c_str(),d.w,d.h,d.w*d.h);   // id, clase, tamano (area)
        glColor3f(0.9f,0.9f,0.92f); FONT.at2d(x+20,ry+12,rb); }
    if(n>vis){ float th0=y+LIST_HH+(h-LIST_HH)*A.listScroll/(float)n, th1=y+LIST_HH+(h-LIST_HH)*min(n,A.listScroll+vis)/(float)n;   // barra de scroll
        glColor4f(0.5f,0.6f,0.7f,0.8f);glBegin(GL_QUADS);glVertex2f(x+w-4,th0);glVertex2f(x+w-1,th0);glVertex2f(x+w-1,th1);glVertex2f(x+w-4,th1);glEnd(); }
    glDisable(GL_BLEND); }
static int list_delete_selected(){ if(A.listSel.empty())return 0; push_undo();   // borra TODAS las etiquetas seleccionadas en la lista
    std::vector<int> idx=A.listSel; std::sort(idx.begin(),idx.end()); idx.erase(std::unique(idx.begin(),idx.end()),idx.end());
    for(int i=(int)idx.size()-1;i>=0;--i){ if(idx[i]>=0&&idx[i]<(int)A.dets.size())A.dets.erase(A.dets.begin()+idx[i]); }   // descendente: no invalida indices menores
    int k=(int)idx.size(); A.listSel.clear(); A.sel=-1; A.selDet=A.selVert=-1; A.selVerts.clear();
    if(A.listCursor>=(int)A.dets.size())A.listCursor=(int)A.dets.size()-1; A.listAnchor=-1; A.dirty=true; invalidate_dets_caches(); return k; }

// Crestas del rio dibujadas SOBRE el espectrograma 2D, con el mismo filtro
// (hilos completos o corte por vertice). Refleja el filtro de hilos en 2D.
static void draw_rios_2d(float X0,float Y0,float X1,float Y1){
    int W=A.spec.W,H=A.spec.H; if(W<2)return; float sx=(X1-X0)/W, sy=(Y1-Y0)/H;
    glEnable(GL_BLEND);glBlendFunc(GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA); glLineWidth(1.6f); glColor4f(0.2f,1.f,1.f,0.9f);
    for(auto&h:A.rios){
        if(A.rio_completo){ bool any=false;
            for(size_t k=0;k<h.col.size();++k){ float f=(float)(H-1-h.row[k])/(H-1),e=A.enh.at(h.row[k],h.col[k]); if(pass_filt(f,e)){any=true;break;} }
            if(!any)continue; glBegin(GL_LINE_STRIP);
            for(size_t k=0;k<h.col.size();++k) glVertex2f(X0+h.col[k]*sx,Y0+h.row[k]*sy); glEnd();
        } else { bool pen=false;
            for(size_t k=0;k<h.col.size();++k){ float f=(float)(H-1-h.row[k])/(H-1),e=A.enh.at(h.row[k],h.col[k]);
                if(!pass_filt(f,e)){ if(pen){glEnd();pen=false;} continue; }
                if(!pen){glBegin(GL_LINE_STRIP);pen=true;} glVertex2f(X0+h.col[k]*sx,Y0+h.row[k]*sy); }
            if(pen)glEnd();
        }
    }
    glDisable(GL_BLEND);
}
// tooltip: muestra que hace el boton bajo el cursor
static void render_tooltip(){ if(A.dragging)return; int i=hit_boton(A.mx,A.my); if(i<0)return;
    const std::string& tip=A.botones[i].tip; if(tip.empty())return; ortho2d();
    float tw=tip.size()*7.0f+12, th=18, x=A.mx+12, y=A.my+18; if(x+tw>A.cW)x=A.cW-tw-2; if(y+th>A.cH)y=A.my-th-4;
    glEnable(GL_BLEND);glBlendFunc(GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA);glColor4f(0,0,0,0.88f);
    glBegin(GL_QUADS);glVertex2f(x,y);glVertex2f(x+tw,y);glVertex2f(x+tw,y+th);glVertex2f(x,y+th);glEnd();glDisable(GL_BLEND);
    glColor3f(0.55f,0.55f,0.6f);glLineWidth(1.f);glBegin(GL_LINE_LOOP);glVertex2f(x,y);glVertex2f(x+tw,y);glVertex2f(x+tw,y+th);glVertex2f(x,y+th);glEnd();
    glColor3f(1,1,1);FONT.at2d(x+6,y+13,tip); }
// Barras de filtro a la derecha: Frecuencia (extremo der) y dB. Cada una con
// manija superior e inferior arrastrables.
static void render_filter_bars(){ ortho2d(); double ny=A.sr*0.5,dr=A.P.dyn_range_db; char b[32];
    struct BB{ Bar bar; float lo,hi; const char* tag; bool isf; } items[2]={
        {bar_freq(),flt_fLo(),flt_fHi(),"Hz",true},{bar_db(),flt_dbMin(),flt_dbMax(),"dB",false} };
    glEnable(GL_BLEND);glBlendFunc(GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA);
    for(auto&it:items){ const Bar&bar=it.bar;
        glColor4f(0,0,0,0.55f);glBegin(GL_QUADS);glVertex2f(bar.x0-1,bar.ytop-14);glVertex2f(bar.x1+1,bar.ytop-14);glVertex2f(bar.x1+1,bar.ybot+2);glVertex2f(bar.x0-1,bar.ybot+2);glEnd();
        glColor4f(0.18f,0.18f,0.22f,1.f);glBegin(GL_QUADS);glVertex2f(bar.x0,bar.ytop);glVertex2f(bar.x1,bar.ytop);glVertex2f(bar.x1,bar.ybot);glVertex2f(bar.x0,bar.ybot);glEnd();
        float yl=bar_y(bar,it.lo),yh=bar_y(bar,it.hi);    // banda activa
        if(it.isf)glColor4f(0.2f,0.9f,0.95f,0.55f);else glColor4f(1.f,0.6f,0.2f,0.55f);
        glBegin(GL_QUADS);glVertex2f(bar.x0,yh);glVertex2f(bar.x1,yh);glVertex2f(bar.x1,yl);glVertex2f(bar.x0,yl);glEnd();
        glColor3f(1,1,1);                                  // manijas
        glBegin(GL_QUADS);glVertex2f(bar.x0-3,yh-4);glVertex2f(bar.x1+3,yh-4);glVertex2f(bar.x1+3,yh+4);glVertex2f(bar.x0-3,yh+4);
            glVertex2f(bar.x0-3,yl-4);glVertex2f(bar.x1+3,yl-4);glVertex2f(bar.x1+3,yl+4);glVertex2f(bar.x0-3,yl+4);glEnd();
        glColor3f(0.9f,0.9f,0.95f); FONT.at2d(bar.x0-2,bar.ytop-3,it.tag);
        if(it.isf){ snprintf(b,32,"%.0f",it.hi*ny);FONT.at2d(bar.x0-30,yh+4,b); snprintf(b,32,"%.0f",it.lo*ny);FONT.at2d(bar.x0-30,yl+4,b);}
        else { snprintf(b,32,"%.0f",(it.hi-1)*dr);FONT.at2d(bar.x0-30,yh+4,b); snprintf(b,32,"%.0f",(it.lo-1)*dr);FONT.at2d(bar.x0-30,yl+4,b);} }
    // barra de ganancia/volumen (una sola manija; relleno desde abajo)
    { Bar bar=bar_gain(); float v=A.gain/GAIN_MAX, yv=bar_y(bar,v);
        glColor4f(0,0,0,0.55f);glBegin(GL_QUADS);glVertex2f(bar.x0-1,bar.ytop-14);glVertex2f(bar.x1+1,bar.ytop-14);glVertex2f(bar.x1+1,bar.ybot+2);glVertex2f(bar.x0-1,bar.ybot+2);glEnd();
        glColor4f(0.18f,0.18f,0.22f,1.f);glBegin(GL_QUADS);glVertex2f(bar.x0,bar.ytop);glVertex2f(bar.x1,bar.ytop);glVertex2f(bar.x1,bar.ybot);glVertex2f(bar.x0,bar.ybot);glEnd();
        glColor4f(0.5f,0.9f,0.5f,0.6f);glBegin(GL_QUADS);glVertex2f(bar.x0,yv);glVertex2f(bar.x1,yv);glVertex2f(bar.x1,bar.ybot);glVertex2f(bar.x0,bar.ybot);glEnd();
        glColor3f(1,1,1);glBegin(GL_QUADS);glVertex2f(bar.x0-3,yv-4);glVertex2f(bar.x1+3,yv-4);glVertex2f(bar.x1+3,yv+4);glVertex2f(bar.x0-3,yv+4);glEnd();
        glColor3f(0.9f,0.9f,0.95f);FONT.at2d(bar.x0-4,bar.ytop-3,"Vol"); snprintf(b,32,"x%.1f",A.gain);FONT.at2d(bar.x0-30,yv+4,b);
        float y1g=bar_y(bar,1.f/GAIN_MAX); glColor3f(0.7f,0.7f,0.7f);glLineWidth(1.f);glBegin(GL_LINES);glVertex2f(bar.x0,y1g);glVertex2f(bar.x1,y1g);glEnd(); }  // marca x1.0
    glDisable(GL_BLEND); }

// Valores de ejes en la vista 2D: tiempo (abajo, s) y frecuencia (izquierda, Hz/kHz).
static void render_axes2d(){ ortho2d(); double ny=A.sr*0.5;
    float X0=plotX0(),X1=plotX1(),Y0=plotY0(),Y1=plotY1(); int a=vlo(),bb=vhi(); char s[40];
    glEnable(GL_BLEND);glBlendFunc(GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA);
    glColor4f(0,0,0,0.5f);glBegin(GL_QUADS);glVertex2f(X0-MARG_L,Y1);glVertex2f(X1,Y1);glVertex2f(X1,Y1+MARG_B);glVertex2f(X0-MARG_L,Y1+MARG_B);glEnd();   // banda tiempo (abajo)
    glColor4f(0,0,0,0.5f);glBegin(GL_QUADS);glVertex2f(X0-MARG_L,Y0);glVertex2f(X0,Y0);glVertex2f(X0,Y1);glVertex2f(X0-MARG_L,Y1);glEnd();                  // banda freq (izq)
    glColor3f(0.9f,0.9f,0.95f); glLineWidth(1.f);
    for(int i=0;i<=6;++i){ float fx=i/6.f; float x=X0+fx*(X1-X0); double t=A.sr?((a+fx*(bb-a))*A.P.hop/(double)A.sr):0;
        glBegin(GL_LINES);glVertex2f(x,Y1);glVertex2f(x,Y1+5);glEnd(); snprintf(s,40,"%.2fs",t); if(i<6)FONT.at2d(x+2,Y1+13,s); }
    int H=A.spec.H,rL=rlo(),rH=rhi();                                  // freq dentro de la ventana vertical
    for(int i=0;i<=6;++i){ float fy=i/6.f; float y=Y0+fy*(Y1-Y0); int row=rL+(int)(fy*(rH-rL));
        double f=ny*(double)(H-1-row)/max(1,H-1);
        glBegin(GL_LINES);glVertex2f(X0-5,y);glVertex2f(X0,y);glEnd();
        if(ny>=1000)snprintf(s,40,"%.1fk",f/1000.0);else snprintf(s,40,"%.0f",f); FONT.at2d(X0-MARG_L+3,y+4,s); }
    glColor3f(0.7f,0.7f,0.75f); FONT.at2d(X0-MARG_L+3,Y0+12,"Hz"); glDisable(GL_BLEND); }

// barra de color "Nivel (dB)" para las vistas 3D (a la izquierda), como la imagen de
// referencia: gradiente magma con los valores de dB.
static void render_colorbar3d(){ ortho2d(); double dr=A.P.dyn_range_db; char b[24];
    float x0=12,x1=28,y0=(float)main_y0()+30,y1=(float)panel_y0()-16;
    glBegin(GL_QUAD_STRIP);
    for(int i=0;i<=40;++i){ float t=i/40.f; float cr,cg,cb; colorf(t,cr,cg,cb); glColor3f(cr,cg,cb);
        float y=y1-t*(y1-y0); glVertex2f(x0,y); glVertex2f(x1,y); }
    glEnd();
    glColor3f(0.6f,0.6f,0.66f);glLineWidth(1.f);glBegin(GL_LINE_LOOP);glVertex2f(x0,y0);glVertex2f(x1,y0);glVertex2f(x1,y1);glVertex2f(x0,y1);glEnd();
    glColor3f(1,1,1); FONT.at2d(x0-4,y0-8,"Nivel (dB)");
    glColor3f(0.85f,0.85f,0.9f);
    for(int i=0;i<=4;++i){ float t=i/4.f; float y=y1-t*(y1-y0); snprintf(b,24,"%.0f",(t-1.0)*dr); FONT.at2d(x1+3,y+4,b); } }

// overlay del modo "crear etiqueta": entrada de texto in-app
static void render_naming(){ if(!A.naming)return; ortho2d();
    float w=360,hh=58,x=A.cW*0.5f-w*0.5f,y=A.cH*0.5f-hh*0.5f;
    glEnable(GL_BLEND);glBlendFunc(GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA);glColor4f(0,0,0,0.9f);
    glBegin(GL_QUADS);glVertex2f(x,y);glVertex2f(x+w,y);glVertex2f(x+w,y+hh);glVertex2f(x,y+hh);glEnd();glDisable(GL_BLEND);
    glColor3f(0.6f,0.8f,1.f);glLineWidth(1.f);glBegin(GL_LINE_LOOP);glVertex2f(x,y);glVertex2f(x+w,y);glVertex2f(x+w,y+hh);glVertex2f(x,y+hh);glEnd();
    glColor3f(1,1,1);FONT.at2d(x+10,y+18,"Nueva etiqueta (Enter=ok, Esc=cancela):");
    FONT.at2d(x+10,y+42,A.nameBuf+"_"); }

// modal de BUFFER: barra para ajustar el buffer (suavizado) de la etiqueta poligono
static void render_buffer_modal(){ if(!A.buffering)return; ortho2d();
    float w=400,hh=90,x=A.cW*0.5f-w*0.5f,y=A.cH*0.5f-45; char b[48];
    glEnable(GL_BLEND);glBlendFunc(GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA);glColor4f(0,0,0,0.92f);
    glBegin(GL_QUADS);glVertex2f(x,y);glVertex2f(x+w,y);glVertex2f(x+w,y+hh);glVertex2f(x,y+hh);glEnd();
    glColor3f(0.6f,0.8f,1.f);glLineWidth(1.f);glBegin(GL_LINE_LOOP);glVertex2f(x,y);glVertex2f(x+w,y);glVertex2f(x+w,y+hh);glVertex2f(x,y+hh);glEnd();
    glColor3f(1,1,1); snprintf(b,48,"Buffer de la etiqueta: %d px",A.autoBuffer); FONT.at2d(x+14,y+22,b);
    FONT.at2d(x+14,y+hh-10,"arrastra la barra | Enter=ok | Esc=cancela");
    float tx0,tx1,ty; bufslider(tx0,tx1,ty);
    glColor3f(0.3f,0.3f,0.36f);glLineWidth(4.f);glBegin(GL_LINES);glVertex2f(tx0,ty);glVertex2f(tx1,ty);glEnd();  // pista
    float hx=tx0+(float)A.autoBuffer/BUF_MAX*(tx1-tx0);                                                          // relleno + manija
    glColor3f(0.4f,0.7f,1.f);glLineWidth(4.f);glBegin(GL_LINES);glVertex2f(tx0,ty);glVertex2f(hx,ty);glEnd();
    glColor3f(1,1,1);glBegin(GL_QUADS);glVertex2f(hx-5,ty-8);glVertex2f(hx+5,ty-8);glVertex2f(hx+5,ty+8);glVertex2f(hx-5,ty+8);glEnd();
    glDisable(GL_BLEND); }

// panel "Acerca de / Contacto" (clic o Esc para cerrar)
static void render_about(){ if(!A.showAbout)return; ortho2d();
    glEnable(GL_BLEND);glBlendFunc(GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA);
    glColor4f(0,0,0,0.62f);glBegin(GL_QUADS);glVertex2f(0,0);glVertex2f(A.cW,0);glVertex2f(A.cW,A.cH);glVertex2f(0,A.cH);glEnd();   // oscurece el fondo
    float w=690,hh=380,x=A.cW*0.5f-w*0.5f,y=A.cH*0.5f-hh*0.5f; if(x<4)x=4; if(y<4)y=4;
    glColor4f(0.05f,0.07f,0.10f,0.97f);glBegin(GL_QUADS);glVertex2f(x,y);glVertex2f(x+w,y);glVertex2f(x+w,y+hh);glVertex2f(x,y+hh);glEnd();
    glColor3f(0.35f,0.72f,0.95f);glLineWidth(2.f);glBegin(GL_LINE_LOOP);glVertex2f(x,y);glVertex2f(x+w,y);glVertex2f(x+w,y+hh);glVertex2f(x,y+hh);glEnd();
    glColor3f(0.35f,0.72f,0.95f);glLineWidth(1.f);glBegin(GL_LINES);glVertex2f(x+16,y+58);glVertex2f(x+w-16,y+58);glEnd();   // separador bajo el titulo
    glDisable(GL_BLEND);
    float tx=x+24, ty=y+30;
    glColor3f(0.55f,0.85f,1.f); FONT.at2d(tx,ty,"IIAP SachaAcoustic  -  Acerca de / Contacto"); ty+=46;
    glColor3f(1,1,1);
    FONT.at2d(tx,ty,"Creador:  Rodolfo Cardenas"); ty+=22;
    FONT.at2d(tx,ty,"Correo:   rcardenasv@iiap.gob.pe"); ty+=22;
    FONT.at2d(tx,ty,"Instituto de Investigaciones de la Amazonia Peruana (IIAP)"); ty+=22;
    FONT.at2d(tx,ty,"Programa Bosques  -  Laboratorio de Inteligencia Artificial"); ty+=34;
    glColor3f(0.7f,0.9f,1.f); FONT.at2d(tx,ty,"Para que sirve:"); ty+=22; glColor3f(0.88f,0.88f,0.9f);
    FONT.at2d(tx,ty,"Software pensado para AYUDAR A GENERAR ETIQUETAS (anotaciones)"); ty+=20;
    FONT.at2d(tx,ty,"y asi ENTRENAR modelos de IA en bioacustica / paisaje sonoro."); ty+=20;
    FONT.at2d(tx,ty,"Realza el espectrograma, filtra por dB y frecuencia, reproduce"); ty+=20;
    FONT.at2d(tx,ty,"las bandas, y etiqueta sonidos (cajas, poligonos y anillos)."); ty+=20;
    FONT.at2d(tx,ty,"Visualiza en 2D / 3D y exporta en formato COCO y Raven."); ty+=30;
    glColor3f(0.6f,0.6f,0.66f); FONT.at2d(tx,ty,"(clic o Esc para cerrar)"); }

// leyenda/tooltip de la vista Volumen (detalle de lo que se muestra y los ejes)
static void render_volume_legend(){ ortho2d(); const int*p=PERM[A.axperm];
    double ny=A.sr*0.5,dr=A.P.dyn_range_db; double dur=A.sr?(double)A.audio.samples.size()/A.sr:0;
    int npoly=0; for(auto&d:A.dets) if(d.kind==KIND_POLY)++npoly;
    float x=8,y=main_y0()+6,w=380,lh=15,n=8,hh=n*lh+12; char b[110];
    glEnable(GL_BLEND);glBlendFunc(GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA);glColor4f(0,0,0,0.72f);
    glBegin(GL_QUADS);glVertex2f(x,y);glVertex2f(x+w,y);glVertex2f(x+w,y+hh);glVertex2f(x,y+hh);glEnd();
    glColor3f(0.5f,0.7f,1.f);glLineWidth(1.f);glBegin(GL_LINE_LOOP);glVertex2f(x,y);glVertex2f(x+w,y);glVertex2f(x+w,y+hh);glVertex2f(x,y+hh);glEnd();
    glDisable(GL_BLEND); glColor3f(1,1,1); float ty=y+14;
    FONT.at2d(x+8,ty,"VOLUMEN 3D - nube de puntos densa por etiqueta"); ty+=lh;
    FONT.at2d(x+8,ty,"Apila dB desde el piso hasta el dB REAL en cada"); ty+=lh;
    FONT.at2d(x+8,ty,"(tiempo,frecuencia) -> cuerpo solido (filtros locales)."); ty+=lh;
    glColor3f(0.85f,0.85f,0.9f);
    snprintf(b,110,"Ejes: X=%s  Y=%s  Z=%s   color=dB",DIMN[p[0]],DIMN[p[1]],DIMN[p[2]]); FONT.at2d(x+8,ty,b); ty+=lh;
    snprintf(b,110,"Rango: t 0-%.2fs  freq 0-%.0fHz  dB -%.0f..0",dur,ny,dr); FONT.at2d(x+8,ty,b); ty+=lh;
    glColor3f(0.6f,0.9f,1.f); FONT.at2d(x+8,ty,"Barras der. (SOLO esta vista): Frecuencia, dB, Vol"); ty+=lh;
    glColor3f(0.85f,0.85f,0.9f); snprintf(b,110,"Etiquetas poligono mostradas: %d",npoly); FONT.at2d(x+8,ty,b); ty+=lh;
    glColor3f(0.7f,0.8f,1.f); FONT.at2d(x+8,ty,"arrastrar=rotar  rueda=zoom  [ ]=resolucion"); }

// ejes dB Y FRECUENCIA de las vistas 3D = banda del FILTRO (barras) + MARGEN. Solo afecta
// al DIBUJO 3D (map_axes); NO toca el sonido. En la vista Volumen usa el filtro local.
static void update_db_axis(){ if(A.spec.W<1){A.dbAxisLo=0;A.dbAxisHi=1;A.fAxisLo=0;A.fAxisHi=1;return;}
    float dlo=(A.view==7)?A.volDbMin:A.dbMin, dhi=(A.view==7)?A.volDbMax:A.dbMax;
    float dm=5.0f/(float)A.P.dyn_range_db;                        // margen dB = 5 dB
    A.dbAxisLo=max(0.f, dlo-dm); A.dbAxisHi=min(1.f, dhi+dm);
    if(A.dbAxisHi<A.dbAxisLo+0.06f)A.dbAxisHi=min(1.f,A.dbAxisLo+0.06f);
    float flo=(A.view==7)?A.volFLo:A.fLo, fhi=(A.view==7)?A.volFHi:A.fHi;
    float fm=(A.sr>0)?1000.0f/(float)(A.sr*0.5):0.05f;           // margen freq = 1000 Hz
    A.fAxisLo=max(0.f, flo-fm); A.fAxisHi=min(1.f, fhi+fm);
    if(A.fAxisHi<A.fAxisLo+0.06f)A.fAxisHi=min(1.f,A.fAxisLo+0.06f); }
static void render(){ glClearColor(0.04f,0.03f,0.06f,1); glViewport(0,0,A.cW,A.cH); glClear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT);
    if(A.view>=2) update_db_axis();
    if(A.spec.W>0){
        if(A.view==1){ ortho2d(); float u0=(float)vlo()/A.spec.W,u1=(float)vhi()/A.spec.W;
            float v0=(float)rlo()/A.spec.H,v1=(float)rhi()/A.spec.H;        // recorte vertical (zoom de frecuencia)
            float X0=plotX0(),Y0=plotY0(),X1=plotX1(),Y1=plotY1();
            glColor3f(0.06f,0.05f,0.08f);glBegin(GL_QUADS);glVertex2f(X0,Y0);glVertex2f(X1,Y0);glVertex2f(X1,Y1);glVertex2f(X0,Y1);glEnd();
            draw_tex_quad(X0,Y0,X1,Y1,u0,u1,v0,v1);
            draw_overlays(X0,Y0,X1,Y1,true,vlo(),vhi(),rlo(),rhi());
            if(A.tool==T_EDIT) draw_edit_handles(X0,Y0,X1,Y1,vlo(),vhi());
            if(!A.polyX.empty()){ glColor3f(1,1,0.3f);glLineWidth(2.f);glBegin(GL_LINE_STRIP);  // poligono en construccion
                for(size_t k=0;k<A.polyX.size();++k){float sx,sy;spec_to_main((float)A.polyX[k],(float)A.polyY[k],sx,sy);glVertex2f(sx,sy);} glEnd();
                glPointSize(7.f);glBegin(GL_POINTS);for(size_t k=0;k<A.polyX.size();++k){float sx,sy;spec_to_main((float)A.polyX[k],(float)A.polyY[k],sx,sy);glVertex2f(sx,sy);}glEnd();glPointSize(1.f); }
            if(A.tool==T_EDIT&&A.selDet>=0&&A.selDet<(int)A.dets.size()){   // VERTICES marcados (solo en Editar): resaltados en rojo (exterior O anillo)
                const Det&d=A.dets[A.selDet]; if(d.kind==KIND_POLY){ glColor3f(1,0.3f,0.3f); glPointSize(11.f); glBegin(GL_POINTS);
                    if(A.selVert>=0){
                        if(A.selVertHole>=0){ if(A.selVertHole<(int)d.hx.size()&&A.selVert<(int)d.hx[A.selVertHole].size()){ float sx,sy; spec_to_main((float)d.hx[A.selVertHole][A.selVert],(float)d.hy[A.selVertHole][A.selVert],sx,sy); glVertex2f(sx,sy); } }   // vertice de ANILLO
                        else if(A.selVert<(int)d.px.size()){ float sx,sy; spec_to_main((float)d.px[A.selVert],(float)d.py[A.selVert],sx,sy); glVertex2f(sx,sy); } }   // vertice exterior
                    for(const auto&pr:A.selVerts){ int hh=pr.first,k=pr.second; float sx,sy;   // varios vertices marcados (rectangulo clic-der): exterior O anillo
                        if(hh<0){ if(k>=0&&k<(int)d.px.size()){ spec_to_main((float)d.px[k],(float)d.py[k],sx,sy); glVertex2f(sx,sy); } }
                        else if(hh<(int)d.hx.size()&&k>=0&&k<(int)d.hx[hh].size()){ spec_to_main((float)d.hx[hh][k],(float)d.hy[hh][k],sx,sy); glVertex2f(sx,sy); } }
                    glEnd(); glPointSize(1.f); } }
            if(A.rbDrag){ glColor3f(1,0.5f,0.3f); glLineWidth(1.f); glBegin(GL_LINE_LOOP);   // rectangulo de seleccion de vertices (arrastre clic-der, Editar)
                glVertex2f((float)A.rbx0,(float)A.rby0);glVertex2f((float)A.rbx1,(float)A.rby0);glVertex2f((float)A.rbx1,(float)A.rby1);glVertex2f((float)A.rbx0,(float)A.rby1);glEnd(); }
            render_axes2d();                                                // ejes 2D (solo vista 2D)
            if(A.dragging&&A.dragRegion==1){glColor3f(1,1,1);glLineWidth(1.f);glBegin(GL_LINE_LOOP);glVertex2f(A.dx0,A.dy0);glVertex2f(A.dx1,A.dy0);glVertex2f(A.dx1,A.dy1);glVertex2f(A.dx0,A.dy1);glEnd();}
            if(A.tool==T_CUT&&A.cutX.size()>=1){ glColor3f(1,0.35f,0.35f);glLineWidth(2.5f);  // trazo de corte libre
                glBegin(GL_LINE_STRIP);for(size_t k=0;k<A.cutX.size();++k){float sx,sy;spec_to_main((float)A.cutX[k],(float)A.cutY[k],sx,sy);glVertex2f(sx,sy);}glEnd(); }
            if(A.tool==T_ERASER){ int c,r; main_to_spec(A.mx,A.my,c,r); float sx,sy; spec_to_main((float)c,(float)r,sx,sy);   // cursor circular del borrador
                float spanX=max(1.f,(plotX1()-plotX0())/(float)max(1,vhi()-vlo()));
                float spanY=max(1.f,(plotY1()-plotY0())/(float)max(1,rhi()-rlo()));
                float rx=(float)A.eraserRadius*spanX, ry=(float)A.eraserRadius*spanY;
                glLineWidth(2.f); glColor4f(1.f,0.3f,0.3f,0.7f); glEnable(GL_BLEND); glBlendFunc(GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA);
                int segs=32; glBegin(GL_LINE_LOOP); for(int i=0;i<segs;++i){ float a=6.2832f*i/segs; glVertex2f(sx+cosf(a)*rx,sy+sinf(a)*ry); } glEnd();
                glDisable(GL_BLEND); glLineWidth(1.f); } }
        else if(A.view==2) render_terrain3d();
        else if(A.view==3) render_rios3d();
        else if(A.view==4) render_pointcloud();
        else if(A.view==5) render_cascade3d();
        else if(A.view==6) render_quiver3d();
        else if(A.view==7){ render_volume3d(); render_volume_legend(); }
        if(A.view>=2&&A.view<=6) render_colorbar3d();                        // leyenda Nivel(dB) en 3D (no en V7 Volumen: tiene leyenda)
        render_panel();
        render_filter_bars();                                               // barras de filtro en TODAS las vistas (V7 = filtros locales)
        if(A.dragging&&(A.dragRegion==2||A.dragRegion==3)){ortho2d();glColor3f(1,1,1);glLineWidth(1.f);glBegin(GL_LINE_LOOP);glVertex2f(A.dx0,A.dy0);glVertex2f(A.dx1,A.dy0);glVertex2f(A.dx1,A.dy1);glVertex2f(A.dx0,A.dy1);glEnd();}
    } else { ortho2d(); glColor3f(1,1,1); FONT.at2d(20,A.cH/2,"Pulsa 'Abrir' (tecla o) o ARRASTRA un WAV aqui para cargarlo..."); }
    render_toolbar(); render_hud(); render_cursor_readout(); render_label_list(); render_cmap_dropdown(); render_tooltip(); render_naming(); render_buffer_modal(); render_about(); }

// ---------------- acciones ----------
// historial para Ctrl+Z: guarda una instantanea de las etiquetas ANTES de modificarlas.
static void push_undo(){ A.undo.push_back(A.dets); if(A.undo.size()>50)A.undo.erase(A.undo.begin()); A.redo.clear(); }
static void do_undo(){ if(A.undo.empty())return; A.redo.push_back(A.dets); A.dets=A.undo.back(); A.undo.pop_back();
    A.sel=-1; A.selDet=A.selVert=-1; A.selVerts.clear(); A.listSel.clear(); A.dirty=true; invalidate_dets_caches(); std::cout<<"Deshacer ("<<A.undo.size()<<" deshacer, "<<A.redo.size()<<" rehacer)\n"; }
static void do_redo(){ if(A.redo.empty())return; A.undo.push_back(A.dets); A.dets=A.redo.back(); A.redo.pop_back();
    A.sel=-1; A.selDet=A.selVert=-1; A.selVerts.clear(); A.listSel.clear(); A.dirty=true; invalidate_dets_caches(); std::cout<<"Rehacer ("<<A.undo.size()<<" deshacer, "<<A.redo.size()<<" rehacer)\n"; }
// Selecciona la etiqueta bajo (c,r). Si hay varias solapadas (p.ej. una PEQUENA dentro de
// otra GRANDE) elige la de MENOR area, para poder seleccionar/editar la interior. Para
// poligonos exige que el punto caiga dentro de la forma real (pt_in_poly), no solo del bbox.
static int caja_en(int c,int r){ int best=-1; long bestA=0;
    for(int i=(int)A.dets.size()-1;i>=0;--i){ const Det&d=A.dets[i];
        if(c<d.x||c>=d.x+d.w||r<d.y||r>=d.y+d.h)continue;                        // fuera del bbox
        bool in=(d.kind==KIND_POLY&&d.px.size()>=3)?pt_in_poly(d.px,d.py,c+0.5f,r+0.5f):true;
        if(!in)continue;
        long a=(long)d.w*d.h; if(best<0||a<bestA){ best=i; bestA=a; } }                // prefiere la mas pequena
    return best; }
// borra las etiquetas cuyo CENTRO cae dentro de la caja de seleccion (tiempo + frecuencia
// si la hay). Devuelve cuantas borro. Si no hay seleccion, borra la etiqueta seleccionada.
static int delete_in_selection(){
    if(A.sc0>=0&&A.sc1>A.sc0){ int c0=min(A.sc0,A.sc1),c1=max(A.sc0,A.sc1);
        bool hasR=(A.sr0>=0&&A.sr1>=0); int r0=hasR?min(A.sr0,A.sr1):0, r1=hasR?max(A.sr0,A.sr1):A.spec.H;
        int n=0;
        for(int i=(int)A.dets.size()-1;i>=0;--i){ const Det&d=A.dets[i];
            int cx=d.x+d.w/2, cy=d.y+d.h/2;                       // centro de la etiqueta
            if(cx>=c0&&cx<=c1&&cy>=r0&&cy<=r1){ A.dets.erase(A.dets.begin()+i); ++n; } }
        if(n){ A.sel=-1; A.dirty=true; invalidate_dets_caches(); } return n; }
    if(A.sel>=0&&A.sel<(int)A.dets.size()){ A.dets.erase(A.dets.begin()+A.sel); A.sel=-1; A.dirty=true; invalidate_dets_caches(); return 1; }
    return 0; }

// Corta las etiquetas que intersectan con la caja de seleccion.
// Etiqueta dentro de la caja -> borrar. Superposicion -> recortar (mascara + componente conexo).
// Preserva huecos existentes via find_holes sobre la mascara resultante.
static int cut_in_selection(){
    if(A.sc0<0||A.sc1<=A.sc0) return 0;
    int c0=min(A.sc0,A.sc1), c1=max(A.sc0,A.sc1);
    bool hasR=(A.sr0>=0&&A.sr1>=0);
    int r0=hasR?min(A.sr0,A.sr1):0, r1=hasR?max(A.sr0,A.sr1):A.spec.H;
    int W=A.spec.W, H=A.spec.H;
    int n=0;
    for(int i=(int)A.dets.size()-1;i>=0;--i){
        Det& d=A.dets[i];
        if(d.x+d.w<=c0||d.x>=c1||d.y+d.h<=r0||d.y>=r1) continue;
        if(d.x>=c0&&d.x+d.w<=c1&&d.y>=r0&&d.y+d.h<=r1){
            A.dets.erase(A.dets.begin()+i); ++n; continue; }
        Mask detMask((size_t)W*H, 0);
        if(d.kind==KIND_POLY&&d.px.size()>=3){
            for(int y=std::max(0,d.y);y<std::min(H,d.y+d.h);++y){
                std::vector<int> nodes; int np=(int)d.px.size();
                for(int k=0;k<np;++k){ int k2=(k+1)%np;
                    float y1a=(float)d.py[k], y2a=(float)d.py[k2];
                    if((y1a<(float)y&&y2a>=(float)y)||(y2a<(float)y&&y1a>=(float)y)){
                        float xint=(float)d.px[k]+(float)(d.px[k2]-d.px[k])*(y-y1a)/(y2a-y1a);
                        nodes.push_back((int)xint); } }
                std::sort(nodes.begin(),nodes.end());
                for(size_t k=0;k+1<nodes.size();k+=2)
                    for(int x=std::max(0,nodes[k]);x<std::min(W,nodes[k+1]+1);++x)
                        detMask[(size_t)y*W+x]=1; }
            for(size_t hh=0;hh<d.hx.size();++hh){
                if((int)d.hx[hh].size()<3) continue;
                for(int y=std::max(0,d.y);y<std::min(H,d.y+d.h);++y){
                    std::vector<int> nodes; int np=(int)d.hx[hh].size();
                    for(int k=0;k<np;++k){ int k2=(k+1)%np;
                        float y1a=(float)d.hy[hh][k], y2a=(float)d.hy[hh][k2];
                        if((y1a<(float)y&&y2a>=(float)y)||(y2a<(float)y&&y1a>=(float)y)){
                            float xint=(float)d.hx[hh][k]+(float)(d.hx[hh][k2]-d.hx[hh][k])*(y-y1a)/(y2a-y1a);
                            nodes.push_back((int)xint); } }
                    std::sort(nodes.begin(),nodes.end());
                    for(size_t k=0;k+1<nodes.size();k+=2)
                        for(int x=std::max(0,nodes[k]);x<std::min(W,nodes[k+1]+1);++x)
                            detMask[(size_t)y*W+x]=0; } }
        } else {
            for(int y=std::max(0,d.y);y<std::min(H,d.y+d.h);++y)
                for(int x=std::max(0,d.x);x<std::min(W,d.x+d.w);++x)
                    detMask[(size_t)y*W+x]=1; }
        for(int y=r0;y<r1;++y)
            for(int x=c0;x<c1;++x)
                if(x>=0&&x<W&&y>=0&&y<H) detMask[(size_t)y*W+x]=0;
        bool any=false;
        for(size_t k=0;k<detMask.size();++k) if(detMask[k]){ any=true; break; }
        if(!any){ A.dets.erase(A.dets.begin()+i); ++n; continue; }
        std::vector<int> lab((size_t)W*H, 0); int nextLabel=0;
        const int dx8[8]={1,1,0,-1,-1,-1,0,1}; const int dy8[8]={0,1,1,1,0,-1,-1,-1};
        struct Comp{ int label; int area; int minx,maxx,miny,maxy; int startx,starty; };
        std::vector<Comp> comps;
        for(int y=0;y<H;++y) for(int x=0;x<W;++x){
            if(!detMask[y*W+x]||lab[y*W+x]) continue; ++nextLabel;
            int area=0,minx=x,maxx=x,miny=y,maxy=y;
            std::queue<std::pair<int,int>> q; q.push({x,y}); lab[y*W+x]=nextLabel;
            while(!q.empty()){ auto[cx,cy]=q.front(); q.pop(); ++area;
                minx=std::min(minx,cx); maxx=std::max(maxx,cx);
                miny=std::min(miny,cy); maxy=std::max(maxy,cy);
                for(int k=0;k<8;++k){ int nx=cx+dx8[k],ny=cy+dy8[k];
                    if(nx<0||nx>=W||ny<0||ny>=H) continue;
                    if(detMask[ny*W+nx]&&!lab[ny*W+nx]){ lab[ny*W+nx]=nextLabel; q.push({nx,ny}); } } }
            comps.push_back({nextLabel,area,minx,maxx,miny,maxy,x,y}); }
        A.dets.erase(A.dets.begin()+i);
        for(auto& comp: comps){
            if(comp.area<A.area_min) continue;
            Det nd; nd.kind=KIND_POLY; nd.cls=d.cls;
            nd.x=comp.minx; nd.y=comp.miny;
            nd.w=comp.maxx-comp.minx+1; nd.h=comp.maxy-comp.miny+1;
            std::vector<int> bx,by;
            trace_boundary(lab, W, H, comp.label, comp.startx, comp.starty, bx, by);
            for(size_t k=0;k<bx.size();++k){ nd.px.push_back(bx[k]); nd.py.push_back(by[k]); }
            if(nd.px.size()<3){ nd.px={nd.x,nd.x+nd.w,nd.x+nd.w,nd.x}; nd.py={nd.y,nd.y,nd.y+nd.h,nd.y+nd.h}; }
            sync_bbox_from_poly(nd);
            find_holes(detMask, W, H, comp.minx, comp.miny, comp.maxx, comp.maxy, 1, A.area_min, nd.hx, nd.hy);
            A.dets.push_back(std::move(nd)); ++n; } }
    if(n){ A.sel=-1; A.dirty=true; invalidate_dets_caches(); }
    return n;
}
// asigna la clase `cls` a TODAS las etiquetas cuyo CENTRO cae en la caja de seleccion (tiempo +
// frecuencia si la hay). Para "Cambiar etiqueta" en lote (uno o varios poligonos). Devuelve cuantas cambio.
static int set_class_in_selection(int cls){
    if(!(A.sc0>=0&&A.sc1>A.sc0))return 0;
    int c0=min(A.sc0,A.sc1),c1=max(A.sc0,A.sc1);
    bool hasR=(A.sr0>=0&&A.sr1>=0); int r0=hasR?min(A.sr0,A.sr1):0, r1=hasR?max(A.sr0,A.sr1):A.spec.H;
    int n=0;
    for(auto&d:A.dets){ int cx=d.x+d.w/2, cy=d.y+d.h/2;                  // centro de la etiqueta
        if(cx>=c0&&cx<=c1&&cy>=r0&&cy<=r1){ d.cls=cls; ++n; } }
    if(n)A.dirty=true; return n; }
// borra el VERTICE marcado (A.selDet,A.selVert,A.selVertHole) — exterior o de un anillo.
// Si un anillo quedaria con <3 vertices lo elimina; si el exterior quedaria <3, borra la etiqueta.
static bool delete_marked_vertex(){ if(A.selDet<0||A.selDet>=(int)A.dets.size())return false;
    Det&d=A.dets[A.selDet]; if(d.kind!=KIND_POLY)return false;
    if(!A.selVerts.empty()){                                // VARIOS vertices marcados con rectangulo (clic-der) -> borra todos (exterior Y/O anillos)
        int extCount=0; for(const auto&pr:A.selVerts) if(pr.first<0)++extCount;   // cuantos del contorno exterior
        if((int)d.px.size()-extCount<3){ A.dets.erase(A.dets.begin()+A.selDet); A.sel=-1;   // el exterior quedaria <3 -> borra la etiqueta entera
            A.selDet=A.selVert=-1; A.selVertHole=-1; A.selVerts.clear(); A.dirty=true; return true; }
        std::vector<int> ext; for(const auto&pr:A.selVerts) if(pr.first<0)ext.push_back(pr.second);   // exterior: borrar indices descendentes
        std::sort(ext.begin(),ext.end()); ext.erase(std::unique(ext.begin(),ext.end()),ext.end());
        for(int i=(int)ext.size()-1;i>=0;--i){ int k=ext[i]; if(k>=0&&k<(int)d.px.size()){ d.px.erase(d.px.begin()+k); d.py.erase(d.py.begin()+k); } }
        for(int hh=(int)d.hx.size()-1;hh>=0;--hh){                                // anillos: de mayor a menor indice (borrar un anillo entero no invalida los menores)
            std::vector<int> ks; for(const auto&pr:A.selVerts) if(pr.first==hh)ks.push_back(pr.second);
            if(ks.empty())continue; std::sort(ks.begin(),ks.end()); ks.erase(std::unique(ks.begin(),ks.end()),ks.end());
            if((int)d.hx[hh].size()-(int)ks.size()<3){ d.hx.erase(d.hx.begin()+hh); d.hy.erase(d.hy.begin()+hh); }   // anillo quedaria <3 -> quita el anillo entero
            else { for(int i=(int)ks.size()-1;i>=0;--i){ int k=ks[i]; if(k>=0&&k<(int)d.hx[hh].size()){ d.hx[hh].erase(d.hx[hh].begin()+k); d.hy[hh].erase(d.hy[hh].begin()+k); } } } }
        sync_bbox_from_poly(d);
        A.selDet=A.selVert=-1; A.selVertHole=-1; A.selVerts.clear(); A.dirty=true; return true; }
    if(A.selVert<0)return false;
    if(A.selVertHole>=0){                                   // vertice de un ANILLO/hueco
        if(A.selVertHole>=(int)d.hx.size()||A.selVert>=(int)d.hx[A.selVertHole].size())return false;
        if((int)d.hx[A.selVertHole].size()<=3){ d.hx.erase(d.hx.begin()+A.selVertHole); d.hy.erase(d.hy.begin()+A.selVertHole); }   // <3 -> quita el anillo entero
        else { d.hx[A.selVertHole].erase(d.hx[A.selVertHole].begin()+A.selVert); d.hy[A.selVertHole].erase(d.hy[A.selVertHole].begin()+A.selVert); } }
    else {                                                  // vertice del contorno exterior
        if(A.selVert>=(int)d.px.size())return false;
        if((int)d.px.size()<=3){ A.dets.erase(A.dets.begin()+A.selDet); A.sel=-1; }   // <3 -> borra etiqueta
        else { d.px.erase(d.px.begin()+A.selVert); d.py.erase(d.py.begin()+A.selVert); sync_bbox_from_poly(d); } }
    A.selDet=A.selVert=-1; A.selVertHole=-1; A.selVerts.clear(); A.dirty=true; invalidate_dets_caches(); return true; }
static void guardar(bool silent=false){ if(A.spec.W<1)return; std::string stem=A.fname; { size_t p=stem.find_last_of('.'); if(p!=std::string::npos)stem=stem.substr(0,p); }
    if(stem.empty()||stem=="(sin archivo)")stem="etiquetas"; std::string base=A.out_dir+"/"+stem;  // nombrar por el audio
    if(!silent) show_busy("Guardando COCO + Raven...");
    double dyn=A.P.dyn_range_db, ny=A.sr*0.5;
    LabelMeta m; m.audio_file=A.fname; m.sample_rate=A.sr;
    m.signal_db=(A.dbMin-1.0)*dyn;             // dB donde se considera SENAL (piso fijado)
    m.db_hi=(A.dbMax-1.0)*dyn; m.f_lo=A.fLo*ny; m.f_hi=A.fHi*ny;
    export_coco(base+".json",A.fname,A.spec.W,A.spec.H,A.dets,m,A.classes);
    RavenGeom g{A.spec.W,A.spec.H,A.sr,A.P.hop}; export_raven(base+".txt",A.dets,A.classes,g);
    A.dirty=false;
    std::cout<<(silent?"Autosave ":"Guardado ")<<base<<".json + .txt(Raven) ("<<A.dets.size()<<" etiquetas)\n"; }
// firma (hash) del estado de etiquetas: detecta CUALQUIER cambio para el autosave
// Cache: solo recalcula cuando labelsSigDirty=true (marca invalidate_dets_caches).
static unsigned long long compute_labels_sig(){ unsigned long long s=1469598103934665603ULL;
    auto mix=[&](long long v){ s=(s^(unsigned long long)v)*1099511628211ULL; };
    mix((long long)A.dets.size()); mix((long long)A.classes.size());
    for(const Det&d:A.dets){ mix(d.x);mix(d.y);mix(d.w);mix(d.h);mix(d.cls);mix(d.kind);mix((long long)d.px.size());
        for(size_t k=0;k<d.px.size();++k){ mix(d.px[k]); mix(d.py[k]); }
        mix((long long)d.hx.size());
        for(size_t hh=0;hh<d.hx.size();++hh){ mix((long long)d.hx[hh].size());
            for(size_t k=0;k<d.hx[hh].size();++k){ mix(d.hx[hh][k]); mix(d.hy[hh][k]); } } }
    return s; }
static unsigned long long labels_sig() {
    if (A.labelsSigDirty) { A.cachedLabelSig = compute_labels_sig(); A.labelsSigDirty = false; }
    return A.cachedLabelSig;
}
// autosave: si la firma cambio y no se esta arrastrando/escribiendo, guarda en silencio
static void autosave_tick(){ static int t=0; if(++t<100)return; t=0;   // ~cada 1 s
    if(A.spec.W<1||A.fname=="(sin archivo)"||A.dragging||A.naming||A.buffering)return;
    if(!A.sigInit)return;                                              // base se fija al cargar
    unsigned long long sg=labels_sig();
    if(sg!=A.lastSig){ guardar(true); A.lastSig=sg; } }
static void cargar_raven(){
    if(g_dialogOpen){ if(g_hwnd)SetForegroundWindow(g_hwnd); return; }   // ya hay un dialogo abierto -> traer al frente, NO abrir otro
    g_dialogOpen=true;
    char f[MAX_PATH]=""; OPENFILENAMEA o{}; o.lStructSize=sizeof(o); o.hwndOwner=g_hwnd;
    o.lpstrFilter="Raven selection table\0*.txt\0Todos\0*.*\0"; o.lpstrFile=f; o.nMaxFile=MAX_PATH;
    o.Flags=OFN_FILEMUSTEXIST|OFN_PATHMUSTEXIST; bool ok=GetOpenFileNameA(&o)!=0; g_dialogOpen=false;
    if(!ok)return; if(A.spec.W<1)return;
    show_busy("Cargando etiquetas Raven...");
    RavenGeom g{A.spec.W,A.spec.H,A.sr,A.P.hop};
    std::vector<Det> in=import_raven(f,g,A.classes); for(auto&d:in)A.dets.push_back(d);
    A.sel=-1; layout_botones(); invalidate_dets_caches(); std::cout<<"Cargadas "<<in.size()<<" etiquetas Raven de "<<f<<"\n"; }
static void crear_caja(int cls){ if(A.sc0<0||A.sc1<=A.sc0)return; push_undo(); Det d; d.x=A.sc0; d.w=A.sc1-A.sc0;
    if(A.sr0>=0&&A.sr1>=0){d.y=min(A.sr0,A.sr1);d.h=max(1,std::abs(A.sr1-A.sr0));}else{d.y=0;d.h=A.spec.H;}
    d.px={d.x,d.x+d.w,d.x+d.w,d.x}; d.py={d.y,d.y,d.y+d.h,d.y+d.h}; d.cls=cls;
    A.dets.push_back(d); A.sel=(int)A.dets.size()-1; invalidate_dets_caches(); }
// Fijar valor de senal: el humano baja el TECHO de dB escuchando hasta que el
// sonido se vuelve ruido; al pulsar, ese techo (umbral de senal) pasa a ser el
// PISO y el techo se abre al maximo -> el filtro muestra de ese valor a 0 dB.
static void set_signal_value(){ if(A.spec.W<1)return; float thr=A.dbMax;
    A.dbMin=max(0.f,min(0.98f,thr)); A.dbMax=1.0f; upload_texture(); eraser_reset_if_filter_changed();
    A.refilterPending=true;
    std::cout<<"Valor de senal fijado: piso dB="<<(A.dbMin-1)*A.P.dyn_range_db<<" dB\n"; }

// zoom 2D: reescala las ventanas de tiempo (vc) y frecuencia (vr) por 'factor',
// centrado en (cc,cr) en coords de espectro. factor<1 acerca, >1 aleja.
static void zoom2d(float factor,int cc,int cr){ int W=A.spec.W,H=A.spec.H; if(W<2)return;
    int cw=vhi()-vlo(), ch=rhi()-rlo();
    int ncw=max(8,min(W,(int)(cw*factor+0.5f))), nch=max(8,min(H,(int)(ch*factor+0.5f)));
    double fx=cw>0?(cc-vlo())/(double)cw:0.5, fy=ch>0?(cr-rlo())/(double)ch:0.5;
    int nv0=cc-(int)(fx*ncw); nv0=max(0,min(W-ncw,nv0)); A.vc0=nv0; A.vc1=nv0+ncw;
    int nr0=cr-(int)(fy*nch); nr0=max(0,min(H-nch,nr0)); A.vr0=nr0; A.vr1=nr0+nch; }
static void do_action(int c){
    if(c=='q')PostQuitMessage(0);
    else if(c=='f')set_signal_value();   // fija el techo escuchado como piso de senal
    else if(c>='1'&&c<='7'){ A.view=c-'0'; layout_botones(); }   // relayout: botones segun la vista (1-7)
    else if(c=='G')A.quiver_completo=!A.quiver_completo;          // Quiver: glifos completos
    else if(c=='B')A.solo_banda=!A.solo_banda;
    else if(c=='v'){ A.playSpeed=max(0.1f,A.playSpeed-0.1f); A.playSpeed=roundf(A.playSpeed*10.f)/10.f; A.refilterPending=true; }   // mas lento (-0.1x)
    else if(c=='V'){ A.playSpeed=min(4.0f,A.playSpeed+0.1f); A.playSpeed=roundf(A.playSpeed*10.f)/10.f; A.refilterPending=true; }   // mas rapido (+0.1x)
    else if(c=='S'){A.tool=T_SELECT;A.polyX.clear();A.polyY.clear();A.cutX.clear();A.cutY.clear();A.selDet=A.selVert=-1;A.selVerts.clear();}
    else if(c=='Y'){A.tool=T_BBOX;A.shape_poly=false;A.polyX.clear();A.polyY.clear();A.cutX.clear();A.cutY.clear();A.selDet=A.selVert=-1;A.selVerts.clear();}   // etiquetar bounding box
    else if(c=='P'){A.tool=T_POLY;A.shape_poly=true;A.cutX.clear();A.cutY.clear();A.selDet=A.selVert=-1;A.selVerts.clear();}                 // etiquetar poligono
    else if(c=='E'){A.tool=T_EDIT;A.polyX.clear();A.polyY.clear();A.cutX.clear();A.cutY.clear();A.selDet=A.selVert=-1;A.selVerts.clear();}   // editar forma
    else if(c=='X'){A.tool=T_CUT;A.polyX.clear();A.polyY.clear();A.cutX.clear();A.cutY.clear();A.selDet=A.selVert=-1;A.selVerts.clear();}    // corte libre
    else if(c=='J'){A.tool=T_MERGE;A.mergeFirst=-1;A.polyX.clear();A.polyY.clear();A.cutX.clear();A.cutY.clear();A.selDet=A.selVert=-1;A.selVerts.clear();}  // unir etiquetas
    else if(c=='d'){A.tool=T_ERASER;A.polyX.clear();A.polyY.clear();A.cutX.clear();A.cutY.clear();A.selDet=A.selVert=-1;A.selVerts.clear();}   // borrador
    else if(c=='N'){ A.naming=true; A.nameBuf.clear(); }  // crear etiqueta (captura de texto)
    else if(c=='R')cargar_raven();                    // cargar Raven
    else if(c=='H')A.rio_completo=!A.rio_completo;   // rio: hilos completos vs corte por vertice
    else if(c=='M')A.cmapOpen=!A.cmapOpen;            // desplegar/cerrar el combo de MAPA DE COLOR
    else if(c=='?')A.showAbout=!A.showAbout;          // panel Acerca de / Contacto
    else if(c=='O')A.hide_labels=!A.hide_labels;      // ocultar / mostrar etiquetas
    else if(c=='L')A.listOpen=!A.listOpen;            // panel LISTA de etiquetas
    else if(c=='T'){A.vc0=0;A.vc1=A.spec.W;A.vr0=0;A.vr1=A.spec.H;}   // ver todo (tiempo + frecuencia)
    else if(c=='Z')zoom2d(0.7f,(vlo()+vhi())/2,(rlo()+rhi())/2);       // zoom + (centro)
    else if(c=='U')zoom2d(1.4f,(vlo()+vhi())/2,(rlo()+rhi())/2);       // zoom -
    else if(c==']'||c=='+'||c=='=')set_resolution(+1);   // mas resolucion (recalcula espectro 2D + 3D)
    else if(c=='['||c=='-'||c=='_')set_resolution(-1);   // menos resolucion
    else if(c=='u'||c=='j'||c=='i'||c=='m'||c=='y'||c=='g'||c=='w'||c=='e'){
        float sf=(A.sr>0)?1000.0f/(A.sr*0.5f):0.05f;          // paso 1000 Hz (fraccion)
        float sd=(A.P.dyn_range_db>0)?0.5f/(float)A.P.dyn_range_db:0.01f;  // paso 0.5 dB (fraccion)
        if(c=='u')A.fHi=min(1.f,A.fHi+sf);                    // freq sup +
        else if(c=='j')A.fHi=max(A.fLo+sf,A.fHi-sf);          // freq sup -
        else if(c=='i')A.fLo=min(A.fHi-sf,A.fLo+sf);          // freq inf +
        else if(c=='m')A.fLo=max(0.f,A.fLo-sf);               // freq inf -
        else if(c=='y')A.dbMin=min(A.dbMax-sd,A.dbMin+sd);    // piso dB +
        else if(c=='g')A.dbMin=max(0.f,A.dbMin-sd);           // piso dB -
        else if(c=='w')A.dbMax=min(1.f,A.dbMax+sd);           // techo dB +
        else if(c=='e')A.dbMax=max(A.dbMin+sd,A.dbMax-sd);    // techo dB -
        upload_texture(); eraser_reset_if_filter_changed(); A.refilterPending=true; }           // re-filtra el sonido en vivo
    else if(c=='o')open_dialog();
    else if(c==' '){ if(PLAYER.playing&&PLAYER.cur_sample()>=0)PLAYER.pause_toggle(); else play_window(); }  // espacio = play/pausa
    else if(c=='p')play_filtered(0,A.spec.W,false,0,1);   // todo, con filtros aplicados
    else if(c=='k')PLAYER.pause_toggle();
    else if(c=='.')PLAYER.stop();
    else if(c=='r'&&A.view>=2)A.axperm=(A.axperm+1)%2;  // solo intercambia freq/dB (X=tiempo fijo)
    else if(c=='a'&&A.spec.W>0)auto_segment();
    else if(c=='l'&&A.sc0>=0&&A.sc1>A.sc0){ Det d; d.x=A.sc0;d.w=A.sc1-A.sc0;
        if(A.sr0>=0&&A.sr1>=0){d.y=min(A.sr0,A.sr1);d.h=std::abs(A.sr1-A.sr0);}else{d.y=0;d.h=A.spec.H;}
        d.px={d.x,d.x+d.w,d.x+d.w,d.x};d.py={d.y,d.y,d.y+d.h,d.y+d.h};d.cls=A.clase_activa;A.dets.push_back(d);invalidate_dets_caches();}
    else if(c=='t')A.modo_hilo=!A.modo_hilo;
    else if(c=='b')A.clase_activa=0;                              // bio
    else if(c=='n'&&A.classes.size()>1)A.clase_activa=1;          // antro
    else if(c=='z'&&A.sel>=0){push_undo();A.dets[A.sel].cls=A.clase_activa;A.dirty=true;invalidate_dets_caches();}    // asigna la clase activa al seleccionado
    else if(c=='c'){push_undo();A.dets.clear();A.hilos.clear();A.sel=-1;A.polyX.clear();A.polyY.clear();A.cutX.clear();A.cutY.clear();A.selDet=A.selVert=-1;A.selVerts.clear();A.listSel.clear();invalidate_dets_caches();}
    else if(c=='s')guardar();
}
static void ayuda(){ std::cout<<"\n=== raven.exe ===\n Botones arriba o teclas: o abrir, 1-5 vistas, ESPACIO play sel, p todo, k pausa, . stop,\n"
    " a auto, l caja, t hilo, b/n clase, z asig cl, d borrador, Supr borra, c limpia, s guarda, r ejes(3D), q salir.\n"
    " Etiqueta arrastrando en la vista 2D o en la tira inferior (sirve en 3D). Arrastrar selecciona\n"
    " tiempo+frecuencia; ESPACIO reproduce SOLO esa banda. Clic simple = mover playhead (seek).\n"; }

// inicia arrastre del cuadro de ventana (borde izq/der/centro) segun mx
static void start_navdrag(int mx){ int W=A.spec.W; if(W<1)return;
    float wx0=(float)vlo()/W*A.cW,wx1=(float)vhi()/W*A.cW; int col=(int)((double)mx/A.cW*W);
    if(std::fabs((double)mx-wx0)<=8)A.navMode=1; else if(std::fabs((double)mx-wx1)<=8)A.navMode=2; else A.navMode=3;
    A.navStartCol=col; A.navVc0=vlo(); A.navVc1=vhi(); }

// cursor segun la zona: manito en manijas, cruz en espectrograma, flecha resto
static HCURSOR pick_cursor(){
    static HCURSOR cA=LoadCursor(0,IDC_ARROW),cH=LoadCursor(0,IDC_HAND),cC=LoadCursor(0,IDC_CROSS);
    int mx=A.mx,my=A.my; if(A.spec.W<1)return cA;
    if(std::abs(my-panel_y0())<=4) return cH;                                  // divisor del panel
    if(my>=panel_y0()){ float wx0=(float)vlo()/A.spec.W*A.cW,wx1=(float)vhi()/A.spec.W*A.cW;
        if(std::fabs((double)mx-wx0)<=8||std::fabs((double)mx-wx1)<=8) return cH;  // manijas de la ventana
        if(my<panel_y0()+STRIP_H) return cC;                                   // tira espectro -> cruz
        return cA; }
    if(my>=main_y0()) return (A.view==1)?cC:cA;                                // espectro principal -> cruz
    return cA; }

// --- barras de filtro: deteccion y arrastre de manijas ---
static int fbar_pick(int mx,int my){ if(A.spec.W<1)return 0;   // barras en todas las vistas (V7 usa filtros locales)
    Bar bf=bar_freq(),bd=bar_db(),bg=bar_gain();
    auto onbar=[&](const Bar&b){ return mx>=b.x0-5&&mx<=b.x1+5&&my>=b.ytop-8&&my<=b.ybot+8; };
    if(onbar(bf)){ float v=bar_v(bf,my); return (std::fabs(v-flt_fHi())<std::fabs(v-flt_fLo()))?9:8; }
    if(onbar(bd)){ float v=bar_v(bd,my); return (std::fabs(v-flt_dbMax())<std::fabs(v-flt_dbMin()))?11:10; }
    if(onbar(bg)) return 12;   // ganancia (una manija)
    return 0; }
static void fbar_drag(int code,int my){ Bar bf=bar_freq(),bd=bar_db(),bg=bar_gain();
    if(code==12){ A.gain=bar_v(bg,my)*GAIN_MAX; A.gain=max(0.f,min(GAIN_MAX,A.gain)); return; }   // ganancia (no toca textura)
    if(code==8)flt_fLo()=min(flt_fHi()-0.002f,bar_v(bf,my));
    else if(code==9)flt_fHi()=max(flt_fLo()+0.002f,bar_v(bf,my));
    else if(code==10)flt_dbMin()=min(flt_dbMax()-0.002f,bar_v(bd,my));
    else if(code==11)flt_dbMax()=max(flt_dbMin()+0.002f,bar_v(bd,my));
    flt_fLo()=max(0.f,flt_fLo());flt_fHi()=min(1.f,flt_fHi());flt_dbMin()=max(0.f,flt_dbMin());flt_dbMax()=min(1.f,flt_dbMax());
    if(A.view!=7) upload_texture();              // V7: filtro local, no reconstruye la textura 2D
    eraser_reset_if_filter_changed();
    if(code>=8&&code<=11)A.refilterPending=true; }  // dB/freq cambiaron -> re-filtrar el sonido en vivo (incl. V7)

// posicion en pantalla (vista 2D) de una coord de espectro (col,row)
static void spec_to_main(float c,float r,float&sx,float&sy){
    sx=plotX0()+(float)((c-vlo())/(double)max(1,vhi()-vlo())*(plotX1()-plotX0()));
    sy=plotY0()+(float)((r-rlo())/(double)max(1,rhi()-rlo())*(plotY1()-plotY0())); }
// edicion: detecta manija del Det seleccionado. Devuelve: poligono -> indice de vertice;
// bbox -> 100+manija (0..3 esquinas, 4..7 medios, 8 mover); -1 nada.
static const int ED_MOVE = 1<<28;   // "mover toda la forma": centinela que NO colisiona con un indice de vertice (poligonos con muchos puntos)
static int edit_pick(int mx,int my){ A.editHole=-1; if(A.sel<0||A.sel>=(int)A.dets.size())return -1; const Det&d=A.dets[A.sel];
    auto hit=[&](float c,float r){ float sx,sy; spec_to_main(c,r,sx,sy); return (mx-sx)*(mx-sx)+(my-sy)*(my-sy)<=81; };
    if(d.kind==KIND_POLY&&d.px.size()>=3){ for(size_t k=0;k<d.px.size();++k) if(hit((float)d.px[k],(float)d.py[k]))return (int)k;
        for(size_t hh=0;hh<d.hx.size();++hh) for(size_t k=0;k<d.hx[hh].size();++k) if(hit((float)d.hx[hh][k],(float)d.hy[hh][k])){ A.editHole=(int)hh; return (int)k; } }  // vertice de HUECO
    else { float cx[8]={(float)d.x,(float)(d.x+d.w),(float)(d.x+d.w),(float)d.x, d.x+d.w*0.5f,d.x+d.w*0.5f,(float)d.x,(float)(d.x+d.w)};
        float cy[8]={(float)d.y,(float)d.y,(float)(d.y+d.h),(float)(d.y+d.h), (float)d.y,(float)(d.y+d.h),d.y+d.h*0.5f,d.y+d.h*0.5f};
        for(int k=0;k<8;++k) if(hit(cx[k],cy[k]))return 100+k; }
    int c,r; main_to_spec(mx,my,c,r);
    bool inside = (d.kind==KIND_POLY&&d.px.size()>=3) ? pt_in_poly(d.px,d.py,c+0.5f,r+0.5f)
                                                      : (c>=d.x&&c<d.x+d.w&&r>=d.y&&r<d.y+d.h);
    if(inside) return (d.kind==KIND_POLY) ? ED_MOVE : 108;   // mover toda la forma (poly: centinela seguro; bbox: 108=manija mover)
    return -1; }
// EDITAR: indice de la ARISTA del poligono seleccionado bajo el cursor (insertar despues de
// ese indice), o -1. Usa distancia punto-segmento en pantalla (umbral ~6px).
static int edge_pick(int mx,int my){ A.pickHole=-1; if(A.sel<0||A.sel>=(int)A.dets.size())return -1; const Det&d=A.dets[A.sel];
    if(d.kind!=KIND_POLY||d.px.size()<3)return -1;
    int ei=-1; double be=6.0*6.0;
    auto scan=[&](const std::vector<int>&xs,const std::vector<int>&ys,int hole){ int m=(int)xs.size(); if(m<3)return;
        for(int k=0;k<m;++k){ int j=(k+1)%m; float ax,ay,bx,by; spec_to_main((float)xs[k],(float)ys[k],ax,ay); spec_to_main((float)xs[j],(float)ys[j],bx,by);
            double vx=bx-ax,vy=by-ay,L2=vx*vx+vy*vy; double tt=L2>0?((mx-ax)*vx+(my-ay)*vy)/L2:0; tt=tt<0?0:(tt>1?1:tt);
            double px=ax+tt*vx,py=ay+tt*vy,dd=(mx-px)*(mx-px)+(my-py)*(my-py); if(dd<be){be=dd;ei=k;A.pickHole=hole;} } };
    scan(d.px,d.py,-1);                                              // contorno exterior
    for(size_t hh=0;hh<d.hx.size();++hh) scan(d.hx[hh],d.hy[hh],(int)hh);   // aristas de los HUECOS/anillos
    return ei; }
static void sync_bbox_from_poly(Det&d){ if(d.px.size()<2)return; int mnx=d.px[0],mxx=d.px[0],mny=d.py[0],mxy=d.py[0];
    for(size_t k=0;k<d.px.size();++k){mnx=min(mnx,d.px[k]);mxx=max(mxx,d.px[k]);mny=min(mny,d.py[k]);mxy=max(mxy,d.py[k]);}
    d.x=mnx;d.y=mny;d.w=max(1,mxx-mnx);d.h=max(1,mxy-mny); }
static void sync_poly_from_bbox(Det&d){ d.px={d.x,d.x+d.w,d.x+d.w,d.x}; d.py={d.y,d.y,d.y+d.h,d.y+d.h}; }
// aplica el arrastre de una manija de bbox (handle 0..8) a la posicion (c,r)
static void apply_bbox_handle(Det&d,int handle,int c,int r){ int x0=d.x,y0=d.y,x1=d.x+d.w,y1=d.y+d.h;
    switch(handle){ case 0:x0=c;y0=r;break; case 1:x1=c;y0=r;break; case 2:x1=c;y1=r;break; case 3:x0=c;y1=r;break;
        case 4:y0=r;break; case 5:y1=r;break; case 6:x0=c;break; case 7:x1=c;break;
        case 8:{ int w=d.w,hh=d.h; x0=c-w/2;y0=r-hh/2;x1=x0+w;y1=y0+hh; } break; }
    d.x=min(x0,x1);d.y=min(y0,y1);d.w=max(1,std::abs(x1-x0));d.h=max(1,std::abs(y1-y0)); sync_poly_from_bbox(d); }
// CORTE LIBRE: el trazo pintado (A.cutX/cutY) "borra" una franja del Det y este se
// separa en piezas por componentes conexos. Reemplaza la etiqueta por las piezas.
// interseccion de los segmentos p1->p2 (trazo de corte) y p3->p4 (arista del contorno).
// t = parametro a lo largo del trazo, u = a lo largo de la arista; (ix,iy) = punto de cruce.
static bool seg_isect(float x1,float y1,float x2,float y2,float x3,float y3,float x4,float y4,float&t,float&u,float&ix,float&iy){
    float d=(x2-x1)*(y4-y3)-(y2-y1)*(x4-x3); if(std::fabs(d)<1e-7f)return false;   // paralelos
    t=((x3-x1)*(y4-y3)-(y3-y1)*(x4-x3))/d; u=((x3-x1)*(y2-y1)-(y3-y1)*(x2-x1))/d;
    if(t<-1e-6f||t>1+1e-6f||u<-1e-6f||u>1+1e-6f)return false;
    ix=x1+t*(x2-x1); iy=y1+t*(y2-y1); return true; }

static double signed_area(const std::vector<int>&px,const std::vector<int>&py){ double a=0; int n=(int)px.size();
    for(int i=0,j=n-1;i<n;j=i++) a+=(double)px[j]*py[i]-(double)px[i]*py[j]; return a*0.5; }
static void dedup_poly(Det&p){ std::vector<int> ax,ay; for(size_t i=0;i<p.px.size();++i){ if(!ax.empty()&&ax.back()==p.px[i]&&ay.back()==p.py[i])continue; ax.push_back(p.px[i]); ay.push_back(p.py[i]); }   // quita vertices consecutivos repetidos
    while(ax.size()>1&&ax.front()==ax.back()&&ay.front()==ay.back()){ ax.pop_back(); ay.pop_back(); } p.px=ax; p.py=ay; }

// CORTE GEOMETRICO por la linea (cutX,cutY) que dibujo el usuario. CONSERVA los vertices ORIGINALES (NO re-detecta).
// Segun que cruza el trazo: exterior<->exterior = SEPARA en dos piezas ; exterior<->anillo = ABRE el anillo (lo conecta
// al borde, queda 1 pieza sin ese hueco). NO hace push_undo (lo hace el llamador una sola vez).
static void split_free(int idx){ if(idx<0||idx>=(int)A.dets.size()||A.cutX.size()<2)return;
    Det d=A.dets[idx];
    std::vector<int> px=d.px, py=d.py;                  // contorno EXTERIOR original
    if((int)px.size()<3){ px={d.x,d.x+d.w,d.x+d.w,d.x}; py={d.y,d.y,d.y+d.h,d.y+d.h}; }   // caja -> 4 esquinas
    int n=(int)px.size();
    // 1) cruces del trazo con TODAS las fronteras: loop 0 = exterior, loop k+1 = anillo k
    struct Cross{ double cutParam; int loop; int edge; float u; float ix,iy; };
    std::vector<Cross> cr;
    auto scan=[&](int loop,const std::vector<int>&qx,const std::vector<int>&qy){ int m=(int)qx.size(); if(m<3)return;
        for(size_t k=0;k+1<A.cutX.size();++k){ float ax=(float)A.cutX[k],ay=(float)A.cutY[k],bx=(float)A.cutX[k+1],by=(float)A.cutY[k+1];
            for(int i=0;i<m;++i){ int j=(i+1)%m; float t,u,ix,iy;
                if(seg_isect(ax,ay,bx,by,(float)qx[i],(float)qy[i],(float)qx[j],(float)qy[j],t,u,ix,iy)) cr.push_back({(double)k+t,loop,i,u,ix,iy}); } } };
    scan(0,px,py);
    for(size_t hh=0;hh<d.hx.size();++hh) scan((int)hh+1,d.hx[hh],d.hy[hh]);
    std::sort(cr.begin(),cr.end(),[](const Cross&a,const Cross&b){return a.cutParam<b.cutParam;});
    std::vector<Cross> cc; for(auto&x:cr){ if(cc.empty()||std::fabs(x.cutParam-cc.back().cutParam)>1e-4)cc.push_back(x); }
    if(cc.size()<2)return;
    // tramo del trazo DENTRO del material solido (dentro del exterior y fuera de los anillos): el divisor real
    int best=-1; double bestSpan=-1;
    for(size_t i=0;i+1<cc.size();++i){ double pm=(cc[i].cutParam+cc[i+1].cutParam)*0.5; int sg=(int)pm;
        if(sg<0)sg=0; if(sg>(int)A.cutX.size()-2)sg=(int)A.cutX.size()-2; double tt=pm-sg;
        float mx=(float)(A.cutX[sg]+(A.cutX[sg+1]-A.cutX[sg])*tt), my=(float)(A.cutY[sg]+(A.cutY[sg+1]-A.cutY[sg])*tt);
        if(pt_in_det(d,mx,my)){ double span=cc[i+1].cutParam-cc[i].cutParam; if(span>bestSpan){bestSpan=span;best=(int)i;} } }
    if(best<0)return;
    Cross A0=cc[best],B0=cc[best+1];
    if(std::fabs(A0.ix-B0.ix)+std::fabs(A0.iy-B0.iy)<1.f)return;
    int kA=(int)A0.cutParam,kB=(int)B0.cutParam;
    std::vector<std::pair<int,int>> inter; for(int k=kA+1;k<=kB;++k) inter.push_back({A.cutX[k],A.cutY[k]});   // trazo entre A0 y B0 (orden A0->B0)
    int PAx=(int)lround(A0.ix),PAy=(int)lround(A0.iy),PBx=(int)lround(B0.ix),PBy=(int)lround(B0.iy);

    if(A0.loop==0 && B0.loop==0){
        // ===== EXTERIOR <-> EXTERIOR: SEPARA en DOS piezas =====
        // frontera AUMENTADA: vertices originales (clave=indice) + PA,PB (clave=arista+u) -> PA/PB insertados en su lugar
        struct BP{ double key; int x,y; int tag; };
        std::vector<BP> bp; bp.reserve(n+2);
        for(int i=0;i<n;++i) bp.push_back({(double)i, px[i], py[i], 0});
        bp.push_back({(double)A0.edge + (double)A0.u, PAx, PAy, 1});
        bp.push_back({(double)B0.edge + (double)B0.u, PBx, PBy, 2});
        std::sort(bp.begin(),bp.end(),[](const BP&a,const BP&b){return a.key<b.key;});
        int posA=-1,posB=-1; for(int i=0;i<(int)bp.size();++i){ if(bp[i].tag==1)posA=i; else if(bp[i].tag==2)posB=i; }
        int Mn=(int)bp.size();
        Det p1,p2; p1.kind=p2.kind=KIND_POLY; p1.cls=p2.cls=d.cls;
        for(int i=posA;;i=(i+1)%Mn){ p1.px.push_back(bp[i].x); p1.py.push_back(bp[i].y); if(i==posB)break; }   // PA->PB + trazo reverso
        for(int t=(int)inter.size()-1;t>=0;--t){ p1.px.push_back(inter[t].first); p1.py.push_back(inter[t].second); }
        for(int i=posB;;i=(i+1)%Mn){ p2.px.push_back(bp[i].x); p2.py.push_back(bp[i].y); if(i==posA)break; }   // PB->PA + trazo
        for(auto&pt:inter){ p2.px.push_back(pt.first); p2.py.push_back(pt.second); }
        dedup_poly(p1); dedup_poly(p2);
        if(p1.px.size()<3||p2.px.size()<3)return;
        sync_bbox_from_poly(p1); sync_bbox_from_poly(p2);
        for(size_t hh=0;hh<d.hx.size();++hh){ if(d.hx[hh].size()<3)continue;                       // reparte los anillos ENTEROS por su centroide
            double cx=0,cy=0; for(size_t v=0;v<d.hx[hh].size();++v){cx+=d.hx[hh][v];cy+=d.hy[hh][v];} cx/=d.hx[hh].size(); cy/=d.hy[hh].size();
            if(pt_in_poly(p1.px,p1.py,(float)cx,(float)cy)){ p1.hx.push_back(d.hx[hh]); p1.hy.push_back(d.hy[hh]); }
            else if(pt_in_poly(p2.px,p2.py,(float)cx,(float)cy)){ p2.hx.push_back(d.hx[hh]); p2.hy.push_back(d.hy[hh]); } }
        A.dets.erase(A.dets.begin()+idx);
        A.dets.insert(A.dets.begin()+idx,p2); A.dets.insert(A.dets.begin()+idx,p1);
        A.sel=idx; invalidate_dets_caches(); return;
    }
    if((A0.loop==0)!=(B0.loop==0)){
        // ===== EXTERIOR <-> ANILLO: ABRE el anillo (lo une al borde por el trazo; queda 1 pieza sin ese hueco) =====
        Cross E=(A0.loop==0)?A0:B0, Hc=(A0.loop==0)?B0:A0;     // E=cruce en exterior, Hc=cruce en el anillo
        int hk=Hc.loop-1; if(hk<0||hk>=(int)d.hx.size())return;
        std::vector<int> hx=d.hx[hk], hy=d.hy[hk]; int hm=(int)hx.size(); if(hm<3)return;
        int PEx=(int)lround(E.ix),PEy=(int)lround(E.iy),PHx=(int)lround(Hc.ix),PHy=(int)lround(Hc.iy);
        // SNAP: si el cruce redondeado queda a <1.5px de un vertice de su arista, ajusta al vertice EXACTO -> evita que
        // la ranura recta PE-PH cruce la frontera por el redondeo cerca de un vertice (poligono no-simple, area erronea).
        auto snapV=[](float cx,float cy,const std::vector<int>&qx,const std::vector<int>&qy,int e,int&ox,int&oy){
            int m=(int)qx.size(); int a=e,b=(e+1)%m;
            double da=(cx-qx[a])*(cx-qx[a])+(cy-qy[a])*(cy-qy[a]), db=(cx-qx[b])*(cx-qx[b])+(cy-qy[b])*(cy-qy[b]);
            if(da<=db){ if(da<2.25){ox=qx[a];oy=qy[a];} } else { if(db<2.25){ox=qx[b];oy=qy[b];} } };
        snapV(E.ix,E.iy,px,py,E.edge,PEx,PEy); snapV(Hc.ix,Hc.iy,hx,hy,Hc.edge,PHx,PHy);
        std::vector<std::pair<int,int>> eh=inter; if(A0.loop!=0) std::reverse(eh.begin(),eh.end());   // trazo orientado E->H
        std::vector<std::pair<int,int>> AE; int posE=E.edge+1;                                        // exterior aumentado con PE
        for(int i=0;i<=E.edge;++i)AE.push_back({px[i],py[i]}); AE.push_back({PEx,PEy}); for(int i=E.edge+1;i<n;++i)AE.push_back({px[i],py[i]});
        std::vector<std::pair<int,int>> AH; int posH=Hc.edge+1;                                       // anillo aumentado con PH
        for(int i=0;i<=Hc.edge;++i)AH.push_back({hx[i],hy[i]}); AH.push_back({PHx,PHy}); for(int i=Hc.edge+1;i<hm;++i)AH.push_back({hx[i],hy[i]});
        if((signed_area(px,py)>0)==(signed_area(hx,hy)>0)){ std::reverse(AH.begin(),AH.end()); posH=(int)AH.size()-1-posH; }  // anillo en orientacion OPUESTA al exterior
        int AEn=(int)AE.size(),AHn=(int)AH.size();
        Det p; p.kind=KIND_POLY; p.cls=d.cls;
        p.px.push_back(PEx);p.py.push_back(PEy);                                                       // PE
        for(auto&c:eh){ p.px.push_back(c.first); p.py.push_back(c.second); }                           // trazo PE->PH
        for(int c=0;c<=AHn;++c){ int j=(posH+c)%AHn; p.px.push_back(AH[j].first); p.py.push_back(AH[j].second); }  // anillo: PH..vuelta..PH
        for(int t=(int)eh.size()-1;t>=0;--t){ p.px.push_back(eh[t].first); p.py.push_back(eh[t].second); }         // trazo PH->PE (reverso)
        p.px.push_back(PEx);p.py.push_back(PEy);                                                       // PE de nuevo
        for(int c=1;c<AEn;++c){ int j=(posE+c)%AEn; p.px.push_back(AE[j].first); p.py.push_back(AE[j].second); }   // exterior: resto del contorno
        dedup_poly(p);
        if(p.px.size()<3)return;
        for(size_t hh=0;hh<d.hx.size();++hh){ if((int)hh==hk)continue; p.hx.push_back(d.hx[hh]); p.hy.push_back(d.hy[hh]); }  // conserva los OTROS anillos
        sync_bbox_from_poly(p);
        A.dets[idx]=p; A.sel=idx; return;
    }
    // anillo<->anillo (u otros): no se separa por ahora
}

// --- Convex hull (Andrew's monotone chain) ---
static std::vector<std::pair<int,int>> convex_hull(std::vector<std::pair<int,int>> pts) {
    int n=(int)pts.size(); if(n<=2) return pts;
    std::sort(pts.begin(),pts.end());
    std::vector<std::pair<int,int>> hull;
    hull.reserve(n+1);
    for(int i=0;i<n;++i){ while(hull.size()>=2){
        auto& a=hull[hull.size()-2]; auto& b=hull[hull.size()-1];
        if((long long)(b.first-a.first)*(pts[i].second-a.second)-(long long)(b.second-a.second)*(pts[i].first-a.first)<=0) hull.pop_back();
        else break; }
        hull.push_back(pts[i]); }
    int k=(int)hull.size();
    for(int i=n-2;i>=0;--i){ while((int)hull.size()>=k+1){
        auto& a=hull[hull.size()-2]; auto& b=hull[hull.size()-1];
        if((long long)(b.first-a.first)*(pts[i].second-a.second)-(long long)(b.second-a.second)*(pts[i].first-a.first)<=0) hull.pop_back();
        else break; }
        hull.push_back(pts[i]); }
    if(hull.size()>1) hull.pop_back();
    return hull;
}

// --- Unir dos etiquetas: combina sus vertices en una sola forma (convex hull) ---
static void merge_dets(int i, int j) {
    if(i<0||j<0||i>=(int)A.dets.size()||j>=(int)A.dets.size()||i==j) return;
    Det& a=A.dets[i]; Det& b=A.dets[j];
    // Recopilar todos los vertices de ambas etiquetas
    std::vector<std::pair<int,int>> pts;
    auto add_verts=[&](const Det& d){
        const std::vector<int>& px=(d.kind==KIND_POLY&&d.px.size()>=3)?d.px:
            std::vector<int>{d.x,d.x+d.w,d.x+d.w,d.x};
        const std::vector<int>& py=(d.kind==KIND_POLY&&d.py.size()>=3)?d.py:
            std::vector<int>{d.y,d.y,d.y+d.h,d.y+d.h};
        for(size_t k=0;k<px.size();++k) pts.push_back({px[k],py[k]});
    };
    add_verts(a);
    add_verts(b);
    // Convex hull de todos los vertices
    auto hull=convex_hull(pts);
    // Crear nueva etiqueta
    Det nd;
    nd.kind=KIND_POLY;
    nd.cls=a.cls;
    nd.px.clear(); nd.py.clear();
    for(auto& p:hull){ nd.px.push_back(p.first); nd.py.push_back(p.second); }
    if(nd.px.size()<3){ nd.px={nd.x,nd.x+nd.w,nd.x+nd.w,nd.x}; nd.py={nd.y,nd.y,nd.y+nd.h,nd.y+nd.h}; }
    sync_bbox_from_poly(nd);
    // Eliminar la segunda y reemplazar la primera
    if(i>j) std::swap(i,j);
    A.dets.erase(A.dets.begin()+j);
    A.dets[i]=nd;
    A.sel=i; A.mergeFirst=-1;
    invalidate_dets_caches();
    std::cout<<"Etiquetas unidas: "<<nd.px.size()<<" vertices\n";
}

// --- Crear agujero: subtract selection rectangle from the label under cursor ---
static void create_hole_in_selection() {
    if(A.sc0<0||A.sc1<=A.sc0) return;
    int c0=min(A.sc0,A.sc1), c1=max(A.sc0,A.sc1);
    bool hasR=(A.sr0>=0&&A.sr1>=0);
    int r0=hasR?min(A.sr0,A.sr1):0, r1=hasR?max(A.sr0,A.sr1):A.spec.H;
    int W=A.spec.W, H=A.spec.H;
    // Find label that contains the center of the selection
    float cx=(float)(c0+c1)/2, cy=(float)(r0+r1)/2;
    int idx=-1;
    for(int i=0;i<(int)A.dets.size();++i)
        if(pt_in_det(A.dets[i],cx,cy)){ idx=i; break; }
    if(idx<0) return;
    Det& d=A.dets[idx];
    if(d.kind!=KIND_POLY||d.px.size()<3) return;
    push_undo();
    // Create hole polygon (rectangle from selection)
    std::vector<int> hpx={c0,c1,c1,c0};
    std::vector<int> hpy={r0,r0,r1,r1};
    d.hx.push_back(hpx);
    d.hy.push_back(hpy);
    sync_bbox_from_poly(d);
    A.dirty=true; invalidate_dets_caches();
    std::cout<<"Agujero creado en etiqueta #"<<idx<<"\n";
}

// --- Helper: polygon mask from Det ---
static Mask det_to_mask(const Det& d, int W, int H) {
    Mask mask((size_t)W*H, 0);
    const std::vector<int>& px=(d.kind==KIND_POLY&&d.px.size()>=3)?d.px:
        std::vector<int>{d.x,d.x+d.w,d.x+d.w,d.x};
    const std::vector<int>& py=(d.kind==KIND_POLY&&d.py.size()>=3)?d.py:
        std::vector<int>{d.y,d.y,d.y+d.h,d.y+d.h};
    int ylo=d.y, yhi=d.y+d.h;
    for(int y=std::max(0,ylo);y<std::min(H,yhi);++y){
        std::vector<int> nodes;
        int np=(int)px.size();
        for(int k=0;k<np;++k){
            int k2=(k+1)%np;
            float ya=(float)py[k], yb=(float)py[k2];
            if((ya<(float)y&&yb>=(float)y)||(yb<(float)y&&ya>=(float)y)){
                float xint=(float)px[k]+(float)(px[k2]-px[k])*(y-ya)/(yb-ya);
                nodes.push_back((int)xint);
            }
        }
        std::sort(nodes.begin(),nodes.end());
        for(size_t k=0;k+1<nodes.size();k+=2)
            for(int x=std::max(0,nodes[k]);x<std::min(W,nodes[k+1]+1);++x)
                mask[(size_t)y*W+x]=1;
    }
    // Subtract holes
    for(size_t hh=0;hh<d.hx.size();++hh){
        if((int)d.hx[hh].size()<3) continue;
        for(int y=std::max(0,ylo);y<std::min(H,yhi);++y){
            std::vector<int> nodes;
            int np=(int)d.hx[hh].size();
            for(int k=0;k<np;++k){
                int k2=(k+1)%np;
                float ya=(float)d.hy[hh][k], yb=(float)d.hy[hh][k2];
                if((ya<(float)y&&yb>=(float)y)||(yb<(float)y&&ya>=(float)y)){
                    float xint=(float)d.hx[hh][k]+(float)(d.hx[hh][k2]-d.hx[hh][k])*(y-ya)/(yb-ya);
                    nodes.push_back((int)xint);
                }
            }
            std::sort(nodes.begin(),nodes.end());
            for(size_t k=0;k+1<nodes.size();k+=2)
                for(int x=std::max(0,nodes[k]);x<std::min(W,nodes[k+1]+1);++x)
                    mask[(size_t)y*W+x]=0;
        }
    }
    return mask;
}

// --- Dilate/Erode a Det polygon using mask morphology ---
static void dilate_det(Det& d, int W, int H, int kw, int kh) {
    Mask mask=det_to_mask(d,W,H);
    Mask buf((size_t)W*H);
    dilate_into(mask,buf,W,H,kw,kh);
    // Trace boundary of dilated mask
    std::vector<int> lab((size_t)W*H,0); int next=0; int bestL=0; int bestA=0;
    const int dx8[8]={1,1,0,-1,-1,-1,0,1}; const int dy8[8]={0,1,1,1,0,-1,-1,-1};
    for(int y=0;y<H;++y) for(int x=0;x<W;++x){
        if(!buf[y*W+x]||lab[y*W+x]) continue;
        ++next; int area=0,minx=x,maxx=x,miny=y,maxy=y;
        std::queue<std::pair<int,int>> q; q.push({x,y}); lab[y*W+x]=next;
        while(!q.empty()){ auto[cx,cy]=q.front(); q.pop(); ++area;
            minx=std::min(minx,cx); maxx=std::max(maxx,cx); miny=std::min(miny,cy); maxy=std::max(maxy,cy);
            for(int k=0;k<8;++k){ int nx=cx+dx8[k],ny=cy+dy8[k];
                if(nx<0||nx>=W||ny<0||ny>=H) continue;
                if(buf[ny*W+nx]&&!lab[ny*W+nx]){ lab[ny*W+nx]=next; q.push({nx,ny}); } } }
        if(area>bestA){ bestA=area; bestL=next; } }
    if(bestL==0) return;
    int sx=-1,sy=-1;
    for(int y=0;y<H&&sx<0;++y) for(int x=0;x<W&&sx<0;++x) if(lab[y*W+x]==bestL){ sx=x; sy=y; }
    std::vector<int> bx,by; trace_boundary(lab,W,H,bestL,sx,sy,bx,by);
    d.kind=KIND_POLY; d.px.clear(); d.py.clear();
    for(size_t k=0;k<bx.size();k+=2){ d.px.push_back(bx[k]); d.py.push_back(by[k]); }
    if(d.px.size()<3){ d.px={d.x,d.x+d.w,d.x+d.w,d.x}; d.py={d.y,d.y,d.y+d.h,d.y+d.h}; }
    sync_bbox_from_poly(d);
}

static void erode_det(Det& d, int W, int H, int kw, int kh) {
    Mask mask=det_to_mask(d,W,H);
    Mask buf((size_t)W*H);
    erode_into(mask,buf,W,H,kw,kh);
    // Check if anything remains
    bool any=false; for(size_t k=0;k<buf.size();++k) if(buf[k]){ any=true; break; }
    if(!any){ d.px.clear(); d.py.clear(); return; }  // collapsed to nothing
    std::vector<int> lab((size_t)W*H,0); int next=0; int bestL=0; int bestA=0;
    const int dx8[8]={1,1,0,-1,-1,-1,0,1}; const int dy8[8]={0,1,1,1,0,-1,-1,-1};
    for(int y=0;y<H;++y) for(int x=0;x<W;++x){
        if(!buf[y*W+x]||lab[y*W+x]) continue;
        ++next; int area=0,minx=x,maxx=x,miny=y,maxy=y;
        std::queue<std::pair<int,int>> q; q.push({x,y}); lab[y*W+x]=next;
        while(!q.empty()){ auto[cx,cy]=q.front(); q.pop(); ++area;
            minx=std::min(minx,cx); maxx=std::max(maxx,cx); miny=std::min(miny,cy); maxy=std::max(maxy,cy);
            for(int k=0;k<8;++k){ int nx=cx+dx8[k],ny=cy+dy8[k];
                if(nx<0||nx>=W||ny<0||ny>=H) continue;
                if(buf[ny*W+nx]&&!lab[ny*W+nx]){ lab[ny*W+nx]=next; q.push({nx,ny}); } } }
        if(area>bestA){ bestA=area; bestL=next; } }
    if(bestL==0) return;
    int sx=-1,sy=-1;
    for(int y=0;y<H&&sx<0;++y) for(int x=0;x<W&&sx<0;++x) if(lab[y*W+x]==bestL){ sx=x; sy=y; }
    std::vector<int> bx,by; trace_boundary(lab,W,H,bestL,sx,sy,bx,by);
    d.kind=KIND_POLY; d.px.clear(); d.py.clear();
    for(size_t k=0;k<bx.size();k+=2){ d.px.push_back(bx[k]); d.py.push_back(by[k]); }
    if(d.px.size()<3){ d.px.clear(); d.py.clear(); return; }
    sync_bbox_from_poly(d);
}

// --- Grow selected label: dilate + merge with overlapping neighbors ---
static void grow_selected_label() {
    if(A.sel<0||A.sel>=(int)A.dets.size()) return;
    int W=A.spec.W, H=A.spec.H;
    Det& d=A.dets[A.sel];
    int curArea=d.w*d.h;
    // Guardar area original en el primer Ctrl++
    if(d.orig_area==0) d.orig_area=curArea;
    // Si ya esta al tamano original o mas grande, no hacer nada
    if(curArea>=d.orig_area) return;
    push_undo();
    dilate_det(d, W, H, 7, 5);
    // Check for overlap with other labels and merge
    bool merged=true;
    while(merged){
        merged=false;
        Mask m1=det_to_mask(d,W,H);
        for(int j=0;j<(int)A.dets.size();++j){
            if(j==A.sel) continue;
            Det& o=A.dets[j];
            if(d.x+d.w<=o.x||o.x+o.w<=d.x||d.y+d.h<=o.y||o.y+o.h<=d.y) continue;
            Mask m2=det_to_mask(o,W,H);
            bool overlap=false;
            for(size_t k=0;k<m1.size();++k) if(m1[k]&&m2[k]){ overlap=true; break; }
            if(overlap){
                std::vector<std::pair<int,int>> pts;
                for(size_t k=0;k<d.px.size();++k) pts.push_back({d.px[k],d.py[k]});
                for(size_t k=0;k<o.px.size();++k) pts.push_back({o.px[k],o.py[k]});
                auto hull=convex_hull(pts);
                d.px.clear(); d.py.clear();
                for(auto& p:hull){ d.px.push_back(p.first); d.py.push_back(p.second); }
                sync_bbox_from_poly(d);
                A.dets.erase(A.dets.begin()+j);
                if(j<A.sel) A.sel--;
                merged=true;
                invalidate_dets_caches();
                break;
            }
        }
    }
    // Si la nueva area supera la original, revertir
    if(d.w*d.h>d.orig_area){
        A.undo.pop_back();
        A.dirty=false; invalidate_dets_caches();
        std::cout<<"No se agranda: ya esta en tamano original\n";
        return;
    }
    A.dirty=true; invalidate_dets_caches();
    std::cout<<"Etiqueta agrandada: "<<d.px.size()<<" vertices\n";
}

// --- Shrink selected label: erode ---
static void shrink_selected_label() {
    if(A.sel<0||A.sel>=(int)A.dets.size()) return;
    int W=A.spec.W, H=A.spec.H;
    push_undo();
    Det& d=A.dets[A.sel];
    erode_det(d, W, H, 7, 5);
    if(d.px.empty()){
        A.dets.erase(A.dets.begin()+A.sel);
        A.sel=-1;
        std::cout<<"Etiqueta eliminada (colapsó)\n";
    } else {
        std::cout<<"Etiqueta achicada: "<<d.px.size()<<" vertices\n";
    }
    A.dirty=true; invalidate_dets_caches();
}

static LRESULT CALLBACK WndProc(HWND h,UINT m,WPARAM wp,LPARAM lp){
    switch(m){
    case WM_SIZE: A.cW=LOWORD(lp); A.cH=HIWORD(lp); layout_botones(); return 0;
    case WM_SETCURSOR: if(LOWORD(lp)==HTCLIENT){ POINT p; GetCursorPos(&p); ScreenToClient(h,&p); A.mx=p.x;A.my=p.y; SetCursor(pick_cursor()); return TRUE; } break;
    case WM_DROPFILES:{ HDROP hdrop=(HDROP)wp; char path[MAX_PATH]="";   // ARRASTRAR Y SOLTAR: abre el WAV soltado sobre la ventana
        UINT got=DragQueryFileA(hdrop,0,path,MAX_PATH); DragFinish(hdrop);   // toma el PRIMER archivo soltado
        if(got>0){ std::string p=path; size_t n=p.size();                    // solo intenta abrir si termina en .wav (insensible a mayusculas)
            bool isWav = n>=4 && p[n-4]=='.' && (p[n-3]=='w'||p[n-3]=='W') && (p[n-2]=='a'||p[n-2]=='A') && (p[n-1]=='v'||p[n-1]=='V');
            if(isWav){ SetForegroundWindow(h); load_audio(p); } }
        return 0; }
    case WM_LBUTTONDOWN:{ int mx=LOWORD(lp),my=HIWORD(lp);
        if(A.showAbout){ A.showAbout=false; return 0; }                          // cualquier clic cierra el panel Acerca de
        if(A.cmapOpen){ int ci=cmap_item_at(mx,my);                              // combo de mapa de color abierto
            if(ci>=0){ A.cmap=ci; build_cmap(); upload_texture(); }              // elige paleta -> reconstruye LUT y textura
            A.cmapOpen=false; return 0; }                                        // cualquier clic cierra el combo
        if(A.buffering){ float tx0,tx1,ty; bufslider(tx0,tx1,ty);                 // modal de buffer: arrastra la barra
            if(mx>=tx0-8&&mx<=tx1+8&&my>=ty-14&&my<=ty+14){ A.dragging=true; A.dragRegion=22; buffer_apply(mx); } return 0; }
        if(A.listOpen){ float lx,ly,lw,lh; list_rect(lx,ly,lw,lh);   // LISTA de etiquetas: clics dentro del panel
            if(mx>=lx&&mx<=lx+lw&&my>=ly&&my<=ly+lh){
                if(my<ly+LIST_HH){ A.listSortSize=!A.listSortSize; return 0; }   // cabecera = alterna orden (id / tamano)
                int lr=list_row_at(mx,my);
                if(lr>=0){ auto order=list_order(); int di=order[lr]; bool ctl=(wp&MK_CONTROL)!=0, shf=(wp&MK_SHIFT)!=0;
                    if(shf&&A.listAnchor>=0){ list_select_range(A.listAnchor,lr); A.listCursor=lr; }        // Shift = rango desde el ancla
                    else if(ctl){ list_toggle(di); A.listCursor=lr; A.listAnchor=lr; }                      // Ctrl = alterna una fila
                    else { A.listSel.clear(); A.listSel.push_back(di); A.listCursor=lr; A.listAnchor=lr; list_reveal(di); }   // clic = solo esa
                    A.sel=(A.listSel.size()==1)?A.listSel[0]:-1; }
                return 0; } }
        int bi=hit_boton(mx,my); if(bi>=0){ do_action(A.botones[bi].key); return 0; }
        if(A.spec.W>0&&in_scroll(my)){ A.dragging=true; A.dragRegion=30; scroll_center(mx); return 0; }   // barra de scroll horizontal
        int pc=hit_palette(mx,my); if(pc>=0){ A.clase_activa=pc; return 0; }   // paleta de clases
        int fb=fbar_pick(mx,my); if(fb){ A.dragging=true;A.dx0=A.dx1=mx;A.dy0=A.dy1=my;A.dragRegion=fb;A.navMode=0; fbar_drag(fb,my); return 0; }  // barras de filtro
        A.dragging=true; A.dx0=A.dx1=mx; A.dy0=A.dy1=my; A.navMode=0;
        bool ctrl=(wp&MK_CONTROL)!=0;
        if(std::abs(my-panel_y0())<=4){ A.dragRegion=5; return 0; }            // divisor: alto del panel
        if(my>=panel_y0()&&my<panel_y0()+STRIP_H) A.dragRegion=2;             // tira espectro (selecciona)
        else if(my>=panel_y0()+STRIP_H) A.dragRegion=3;                        // oscilograma
        else if(A.view==1&&my>=main_y0()&&my<panel_y0()){                      // espectro principal: segun herramienta
            int c,r; main_to_spec(mx,my,c,r);
            if(A.tool==T_BBOX){ A.dragRegion=1; }                              // arrastrar = caja
            else if(A.tool==T_POLY){ A.polyX.push_back(c); A.polyY.push_back(r); A.dragging=false; A.dragRegion=0; }  // poligono a mano: agrega vertice
            else if(A.tool==T_EDIT){
                int hd=(A.sel>=0)?edit_pick(mx,my):-1;            // vertice/manija (o ED_MOVE=interior) si HAY etiqueta seleccionada
                int ei=(A.sel>=0)?edge_pick(mx,my):-1;            // arista del poligono seleccionado bajo el cursor (agregar punto)
                bool isMove=(hd==ED_MOVE);                        // hd == "mover toda la forma" (clic en el interior del poligono)
                if(hd>=0&&!isMove){ push_undo(); A.dragRegion=21;A.editVert=hd;A.lastEditC=c;A.lastEditR=r;       // VERTICE/manija -> arrastra (snapshot p/ Ctrl+Z)
                    if(A.dets[A.sel].kind==KIND_POLY&&(A.editHole>=0||(hd>=0&&hd<(int)A.dets[A.sel].px.size()))){A.selDet=A.sel;A.selVert=hd;A.selVertHole=A.editHole;}  // marca el vertice (exterior O anillo) para que se VEA
                    else {A.selDet=A.selVert=-1;A.selVertHole=-1;} }
                else if(ei>=0){ A.dragRegion=23; A.pendEdge=ei; A.pendEdgeC=c; A.pendEdgeR=r; A.pendEdgeHole=A.pickHole; }   // clic SOBRE LA LINEA: AGREGAR punto (prioridad sobre mover) - se inserta en el LBUTTONUP si fue clic sin arrastre
                else if(isMove){ push_undo(); A.dragRegion=21;A.editVert=ED_MOVE;A.lastEditC=c;A.lastEditR=r;   // clic en el INTERIOR -> mover toda la forma
                    A.selDet=A.selVert=-1;A.selVertHole=-1; }
                else {                                            // clic que NO cae en vertice/arista/interior del poligono editado
                    if(A.sel<0) A.sel=caja_en(c,r);               // si no habia ninguno seleccionado, selecciona uno para empezar a editar; si ya habia, NO cambia (Editar solo trabaja sobre el seleccionado; Esc o Selec para cambiar)
                    A.selDet=A.selVert=-1; A.selVertHole=-1; A.selVerts.clear(); A.editVert=-1; A.editHole=-1; A.dragging=false;A.dragRegion=0; } }
            else if(A.tool==T_CUT){ A.dragRegion=7; A.cutX.push_back(c); A.cutY.push_back(r); }  // pinta el trazo de corte
            else if(A.tool==T_ERASER){ A.eraserDragging=true; A.eraserPrevC=c; A.eraserPrevR=r;
                eraser_paint(c,r); upload_texture(); A.dragRegion=7; }  // borrador: pinta y arrastra
            else if(A.tool==T_MERGE){  // UNIR: clic en una etiqueta para seleccionarla/combina
                int hit=caja_en(c,r);
                if(hit>=0){
                    if(A.mergeFirst<0){
                        A.mergeFirst=hit; A.sel=hit;
                        std::cout<<"Primera etiqueta seleccionada (#"<<hit<<"). Clic en la segunda para unir.\n";
                    } else if(hit!=A.mergeFirst){
                        push_undo();
                        merge_dets(A.mergeFirst, hit);
                    } else {
                        A.mergeFirst=-1; A.sel=-1;
                        std::cout<<"Seleccion cancelada.\n";
                    }
                }
                A.dragging=false; A.dragRegion=0;
            }
            else { A.selDet=A.selVert=-1; A.selVertHole=-1; A.selVerts.clear(); A.dragRegion=1; }   // T_SELEC: clic/arrastre selecciona el POLIGONO ENTERO (nunca un vertice)
            }
        else A.dragRegion=4;                                                  // 3D rotar
        if(A.dragRegion==4&&A.view>=2&&A.spec.W>0){ int a=pick_handle(mx,my); if(a>=0){A.scaleAxis=a;A.dragRegion=6;} }  // manija de eje
        if(A.dragRegion==2||A.dragRegion==3){ int W=A.spec.W; if(W>0){
            float wx0=(float)vlo()/W*A.cW,wx1=(float)vlo()*0+(float)vhi()/W*A.cW;
            if(ctrl) start_navdrag(mx);                                       // Ctrl+izq = mover/redim
            else if(std::fabs((double)mx-wx0)<=8){A.navMode=1;A.navStartCol=(int)((double)mx/A.cW*W);A.navVc0=vlo();A.navVc1=vhi();}  // manija izq
            else if(std::fabs((double)mx-wx1)<=8){A.navMode=2;A.navStartCol=(int)((double)mx/A.cW*W);A.navVc0=vlo();A.navVc1=vhi();}  // manija der
        } }                                                                   // (plano en el cuerpo = seleccionar)
        return 0; }
    case WM_MBUTTONDOWN:{ int mx=LOWORD(lp),my=HIWORD(lp); if(A.spec.W==0)return 0;  // boton central = ventana
        if(my>=panel_y0()){ A.dragging=true; A.dragRegion=(my<panel_y0()+STRIP_H)?2:3; A.dx0=A.dx1=mx;A.dy0=A.dy1=my; start_navdrag(mx); }
        return 0; }
    case WM_MBUTTONUP: A.dragging=false; A.navMode=0; return 0;
    case WM_MOUSEMOVE:{ int mx=LOWORD(lp),my=HIWORD(lp); A.mx=mx; A.my=my;          // (para tooltips)
        if(A.rbDrag){ A.rbx1=mx; A.rby1=my; return 0; }   // arrastre del rectangulo de seleccion de vertices (clic-der, Editar)
        if(A.dragging){
        if(A.dragRegion==5){ A.panelH=max(80,min((int)(A.cH*0.6),A.cH-my)); }        // redimensiona panel
        else if(A.navMode){ int W=A.spec.W; int col=(int)((double)mx/A.cW*W);
            if(A.navMode==1)A.vc0=max(0,min(vhi()-8,col)); else if(A.navMode==2)A.vc1=min(W,max(vlo()+8,col));
            else if(A.navMode==3){ int d=col-A.navStartCol,w=A.navVc1-A.navVc0,a=A.navVc0+d; a=max(0,min(W-w,a)); A.vc0=a;A.vc1=a+w; } }
        else if(A.dragRegion==6&&A.scaleAxis>=0){ double proj=(mx-A.dx1)*A.adx+(my-A.dy1)*A.ady;  // redimensiona el eje
            A.ax[A.scaleAxis]=max(0.4f,min(14.f,A.ax[A.scaleAxis]+(float)proj*0.012f)); }
        else if(A.dragRegion==30){ scroll_center(mx); }                           // barra de scroll horizontal: mueve la ventana
        else if(A.dragRegion==22){ buffer_apply(mx); }                            // modal de buffer
        else if(A.dragRegion==7){ int c,r; main_to_spec(mx,my,c,r);
            if(A.tool==T_ERASER){ if(A.eraserDragging){ eraser_line(A.eraserPrevC,A.eraserPrevR,c,r); A.eraserPrevC=c; A.eraserPrevR=r; upload_texture(); } }
            else if(A.cutX.empty()||A.cutX.back()!=c||A.cutY.back()!=r){A.cutX.push_back(c);A.cutY.push_back(r);} }  // pinta trazo de corte
        else if(A.dragRegion>=8&&A.dragRegion<=12){ fbar_drag(A.dragRegion,my); }   // barras de filtro / ganancia
        else if(A.dragRegion==21&&A.sel>=0&&A.sel<(int)A.dets.size()){ int c,r; main_to_spec(mx,my,c,r); Det&d=A.dets[A.sel];  // editar forma
            if(d.kind==KIND_POLY&&d.px.size()>=3){                     // POLIGONO: NO rectangularizar
                if(A.editHole>=0&&A.editHole<(int)d.hx.size()&&A.editVert>=0&&A.editVert<(int)d.hx[A.editHole].size()){ d.hx[A.editHole][A.editVert]=c; d.hy[A.editHole][A.editVert]=r; A.lastEditC=c;A.lastEditR=r; }  // mover vertice de ANILLO
                else if(A.editVert>=0&&A.editVert<(int)d.px.size()){ d.px[A.editVert]=c; d.py[A.editVert]=r; A.lastEditC=c;A.lastEditR=r; }   // mover UN vertice exterior
                else {                                                 // MOVER TODO el poligono (incl. anillos), ACOTADO al area del espectro por eje
                    int dc=c-A.lastEditC, dr=r-A.lastEditR; int W=A.spec.W,H=A.spec.H;
                    int minx=d.px[0],maxx=d.px[0],miny=d.py[0],maxy=d.py[0];
                    for(size_t k=0;k<d.px.size();++k){ if(d.px[k]<minx)minx=d.px[k]; if(d.px[k]>maxx)maxx=d.px[k]; if(d.py[k]<miny)miny=d.py[k]; if(d.py[k]>maxy)maxy=d.py[k]; }
                    if(minx+dc<0)dc=-minx; if(maxx+dc>W-1)dc=(W-1)-maxx;   // sin espacio horizontal -> dc=0 en ese borde (pero el otro eje sigue libre)
                    if(miny+dr<0)dr=-miny; if(maxy+dr>H-1)dr=(H-1)-maxy;   // sin espacio vertical -> dr=0 en ese borde
                    for(size_t k=0;k<d.px.size();++k){ d.px[k]+=dc; d.py[k]+=dr; }
                    for(auto&hxv:d.hx)for(auto&v:hxv)v+=dc; for(auto&hyv:d.hy)for(auto&v:hyv)v+=dr;
                    A.lastEditC+=dc; A.lastEditR+=dr; }                 // avanza por el desplazamiento APLICADO (clamp) -> sin lag al volver del borde
                sync_bbox_from_poly(d); }
            else if(A.editVert>=100){ apply_bbox_handle(d,A.editVert-100,c,r); A.lastEditC=c;A.lastEditR=r; }   // BBOX: manijas / mover (108)
            A.dirty=true; }
        else if(A.dragRegion==4){A.yaw+=(mx-A.dx1)*0.4f;A.pitch+=(my-A.dy1)*0.4f;A.pitch=max(-89.f,min(89.f,A.pitch));}
        A.dx1=mx;A.dy1=my; } return 0; }
    case WM_LBUTTONUP:{ A.dragging=false; if(A.spec.W==0){A.navMode=0;return 0;} int mx=A.dx1,my=A.dy1;
        bool click=(std::abs(A.dx1-A.dx0)<4&&std::abs(A.dy1-A.dy0)<4);
        if(A.navMode||A.dragRegion==5||A.dragRegion==6){ A.navMode=0; A.scaleAxis=-1; return 0; }  // ventana/divisor/manija eje
        if(A.dragRegion==22){A.dragRegion=0;return 0;}                        // modal de buffer
        if(A.dragRegion==30){A.dragRegion=0;return 0;}                        // barra de scroll horizontal
        if(A.dragRegion>=8&&A.dragRegion<=12){A.dragRegion=0;return 0;}        // barras de filtro / ganancia
        if(A.dragRegion==21){A.dragRegion=0;A.editVert=-1;A.editHole=-1;return 0;}   // editar
        if(A.dragRegion==23){ A.dragRegion=0;                                  // insercion de punto sobre la linea (exterior o anillo)
            if(click&&A.pendEdge>=0&&A.sel>=0&&A.sel<(int)A.dets.size()){ Det&d=A.dets[A.sel];   // solo si fue CLIC (no arrastre)
                if(d.kind==KIND_POLY){
                    if(A.pendEdgeHole>=0&&A.pendEdgeHole<(int)d.hx.size()&&A.pendEdge+1<=(int)d.hx[A.pendEdgeHole].size()){ push_undo();   // inserta en un HUECO/anillo
                        d.hx[A.pendEdgeHole].insert(d.hx[A.pendEdgeHole].begin()+A.pendEdge+1,A.pendEdgeC); d.hy[A.pendEdgeHole].insert(d.hy[A.pendEdgeHole].begin()+A.pendEdge+1,A.pendEdgeR);
                        A.dirty=true; A.selDet=A.selVert=-1; A.selVerts.clear(); }
                    else if(A.pendEdgeHole<0&&A.pendEdge+1<=(int)d.px.size()){ push_undo();                                              // inserta en el contorno exterior
                        d.px.insert(d.px.begin()+A.pendEdge+1,A.pendEdgeC); d.py.insert(d.py.begin()+A.pendEdge+1,A.pendEdgeR);
                        sync_bbox_from_poly(d); A.dirty=true; A.selDet=A.sel; A.selVert=A.pendEdge+1; A.selVertHole=-1; A.selVerts.clear(); } } }
            A.pendEdge=-1; A.pendEdgeHole=-1; return 0; }
        if(A.dragRegion==7){ A.dragRegion=0; A.eraserDragging=false; return 0; }   // corte libre / borrador: se sigue pintando
        if(A.dragRegion==1||A.dragRegion==2){ int c0,r0,c1,r1;                 // SELECCION (etiquetar)
            if(A.dragRegion==1){main_to_spec(A.dx0,A.dy0,c0,r0);main_to_spec(mx,my,c1,r1);}
            else{strip_to_spec(A.dx0,A.dy0,c0,r0);strip_to_spec(mx,my,c1,r1);}
            if(A.tool==T_BBOX&&A.dragRegion==1&&!click){                       // BBOX: arrastrar crea la caja directamente
                push_undo(); Det d; d.kind=KIND_BBOX; d.cls=A.clase_activa; d.x=min(c0,c1);d.y=min(r0,r1);
                d.w=max(1,std::abs(c1-c0));d.h=max(1,std::abs(r1-r0));
                d.px={d.x,d.x+d.w,d.x+d.w,d.x};d.py={d.y,d.y,d.y+d.h,d.y+d.h};
                A.dets.push_back(d); A.sel=(int)A.dets.size()-1; invalidate_dets_caches(); }
            else if(click){ if(A.modo_hilo){A.hilos.push_back(track_hilo(c0,r0)); if(!A.hilos.back().col.empty()){A.sc0=A.hilos.back().col.front();A.sc1=A.hilos.back().col.back();A.sr0=A.sr1=-1;}}
                       else { int hit=caja_en(c0,r0); A.cursor_col=c0;                            // clic simple = selecciona el POLIGONO ENTERO bajo el cursor
                              bool inRect=false;                                                    // ¿el clic cae DENTRO del rectangulo de seleccion vigente?
                              if(A.sc0>=0&&A.sc1>A.sc0){ int s0=min(A.sc0,A.sc1),s1=max(A.sc0,A.sc1); bool hasR=(A.sr0>=0&&A.sr1>=0);
                                  int rr0=hasR?min(A.sr0,A.sr1):0, rr1=hasR?max(A.sr0,A.sr1):A.spec.H; inRect=(c0>=s0&&c0<=s1&&r0>=rr0&&r0<=rr1); }
                              if(!inRect){ A.sc0=A.sc1=A.sr0=A.sr1=-1; }                            // clic FUERA del area de seleccion -> la quita (antes solo con Esc)
                              A.sel=hit; if(hit<0){ A.selDet=A.selVert=-1; A.selVertHole=-1; A.selVerts.clear(); } } }   // clic en vacio -> deselecciona todo
            else { A.sc0=min(c0,c1);A.sc1=max(c0,c1); A.sr0=min(r0,r1);A.sr1=max(r0,r1); A.sel=-1; } }   // nuevo rectangulo de seleccion -> no hay etiqueta UNICA seleccionada (Supr borra por area)
        else if(A.dragRegion==3){ int c0=(int)((double)A.dx0/A.cW*A.spec.W),c1=(int)((double)mx/A.cW*A.spec.W);
            if(click)A.cursor_col=max(0,min(A.spec.W-1,c0)); else {A.sc0=min(c0,c1);A.sc1=max(c0,c1);A.sr0=A.sr1=-1;} }
        return 0; }
    case WM_RBUTTONDOWN:{ if(A.spec.W==0)return 0; int mx=LOWORD(lp),my=HIWORD(lp); int col,row;
        bool inStrip=(my>=panel_y0()&&my<panel_y0()+STRIP_H);
        if(inStrip)strip_to_spec(mx,my,col,row); else main_to_spec(mx,my,col,row);
        if(A.listOpen){ int lr=list_row_at(mx,my);   // LISTA: clic-derecho borra (la fila bajo el cursor; o toda la seleccion si esa fila esta seleccionada)
            if(lr>=0){ auto order=list_order(); int di=order[lr]; if(!list_is_sel(di)){ A.listSel.clear(); A.listSel.push_back(di); }
                int k=list_delete_selected(); std::cout<<"Borradas "<<k<<" etiquetas (lista)\n"; return 0; }
            float lx,ly,lw,lh; list_rect(lx,ly,lw,lh); if(mx>=lx&&mx<=lx+lw&&my>=ly&&my<=ly+lh)return 0; }   // dentro del panel pero no en fila: consume
        if(A.tool==T_POLY&&!A.polyX.empty()){ finish_poly(); return 0; }  // poligono a mano: clic der CIERRA
        if(A.tool==T_CUT&&A.cutX.size()>=2){                                   // CORTE LIBRE: corta TODAS las etiquetas que cruza el trazo
            std::vector<int> tg; for(size_t k=0;k<A.cutX.size();++k){ int id=caja_en(A.cutX[k],A.cutY[k]); if(id>=0)tg.push_back(id); }
            if(tg.empty()){ int id=caja_en(col,row); if(id<0)id=A.sel; if(id>=0)tg.push_back(id); }
            std::sort(tg.begin(),tg.end()); tg.erase(std::unique(tg.begin(),tg.end()),tg.end());
            if(!tg.empty()){ show_busy("Cortando etiquetas..."); push_undo();
                for(int t=(int)tg.size()-1;t>=0;--t) split_free(tg[t]);          // descendente: no invalida indices menores
                A.dirty=true; }
            A.cutX.clear();A.cutY.clear(); return 0; }
        // EDITAR: arrastrar con BOTON DERECHO un rectangulo -> marca los vertices dentro de
        // cualquier poligono (se borran con Supr). Agregar punto = clic IZQUIERDO sobre la linea.
        if(A.tool==T_EDIT&&!inStrip&&my>=main_y0()&&my<panel_y0()){
            A.rbDrag=true; A.rbx0=A.rbx1=mx; A.rby0=A.rby1=my; A.selVerts.clear(); return 0; }
        int bi=caja_en(col,row); HMENU menu=CreatePopupMenu();
        const int CLS_BASE=100, NEW_BASE=200;        // 100+idx asignar ; 200+idx crear
        // etiqueta MAS GRANDE que contiene el punto: un poligono de FONDO que cubre casi todo (y suele
        // tener muchos anillos/huecos "sueltos") NO se puede seleccionar por clic (caja_en prefiere la mas
        // chica). bigHere permite borrarlo de un tiro -> sus anillos se van con el.
        int bigHere=-1; long bigA=-1;
        for(int i=0;i<(int)A.dets.size();++i){ const Det&d=A.dets[i];
            if(col<d.x||col>=d.x+d.w||row<d.y||row>=d.y+d.h)continue;
            bool in=(d.kind==KIND_POLY&&d.px.size()>=3)?pt_in_poly(d.px,d.py,col+0.5f,row+0.5f):true;
            if(in){ long a=(long)d.w*d.h; if(a>bigA){bigA=a;bigHere=i;} } }
        bool hasSel=(A.sc0>=0&&A.sc1>A.sc0);
        bool inSel=false;                                  // ¿el clic cae DENTRO del rectangulo de seleccion?
        if(hasSel){ int s0=min(A.sc0,A.sc1),s1=max(A.sc0,A.sc1); bool hasR=(A.sr0>=0&&A.sr1>=0);
            int r0=hasR?min(A.sr0,A.sr1):0, r1=hasR?max(A.sr0,A.sr1):A.spec.H; inSel=(col>=s0&&col<=s1&&row>=r0&&row<=r1); }
        bool selMenu = hasSel && (inSel || bi<0);          // clic DENTRO de la seleccion (aunque haya etiqueta debajo) -> menu de SELECCION
        if(selMenu){                                       // --- SOBRE LA SELECCION (uno o varios poligonos) ---
            AppendMenuA(menu,MF_STRING,4,"Reproducir seleccion");
            AppendMenuA(menu,MF_SEPARATOR,0,0);
            for(int k=0;k<(int)A.classes.size();++k)AppendMenuA(menu,MF_STRING,CLS_BASE+k,(std::string("Cambiar etiqueta a: ")+A.classes[k].name).c_str());  // cambia la clase de TODOS los poligonos de la seleccion
            AppendMenuA(menu,MF_SEPARATOR,0,0);
            AppendMenuA(menu,MF_STRING,12,"Autoetiquetar seleccion (poligonos)");   // auto-label SOLO el area, como poligonos
            AppendMenuA(menu,MF_STRING,13,"Autoetiquetar seleccion (cajas)");        // auto-label SOLO el area, como cajas
            AppendMenuA(menu,MF_SEPARATOR,0,0);
            if(A.tool!=T_SELECT){                                                      // en modo Seleccion NO se ofrecen "Crear: bio/antro" (el modo Seleccion solo selecciona/escucha)
                for(int k=0;k<(int)A.classes.size();++k)AppendMenuA(menu,MF_STRING,NEW_BASE+k,(std::string("Crear: ")+A.classes[k].name).c_str());
                AppendMenuA(menu,MF_SEPARATOR,0,0); }
            AppendMenuA(menu,MF_STRING,10,"Nueva etiqueta...");
            AppendMenuA(menu,MF_STRING,11,"Borrar etiquetas en la seleccion (Supr)");
            AppendMenuA(menu,MF_STRING,5,"Quitar seleccion"); }
        else if(bi>=0){ A.sel=bi;                          // --- SOBRE LA ETIQUETA: EDITAR / MEJORAR esa etiqueta ---
            AppendMenuA(menu,MF_STRING,7,"Reproducir etiqueta");
            AppendMenuA(menu,MF_STRING,14,"Editar");                                  // activa la herramienta Editar sobre esta etiqueta
            if(A.dets[bi].kind==KIND_POLY){ AppendMenuA(menu,MF_STRING,8,"Mejorar etiqueta");   // re-segmenta SOLO esa etiqueta
                AppendMenuA(menu,MF_STRING,9,"Buffer..."); }
            AppendMenuA(menu,MF_SEPARATOR,0,0);
            for(int k=0;k<(int)A.classes.size();++k)AppendMenuA(menu,MF_STRING,CLS_BASE+k,(std::string("Etiqueta: ")+A.classes[k].name).c_str());
            AppendMenuA(menu,MF_SEPARATOR,0,0); AppendMenuA(menu,MF_STRING,10,"Nueva etiqueta...");
            AppendMenuA(menu,MF_STRING,3,"Borrar");
            if(bigHere>=0&&bigHere!=bi) AppendMenuA(menu,MF_STRING,15,"Borrar la etiqueta mas grande aqui (la de fondo + sus anillos)"); }
        else { AppendMenuA(menu,MF_STRING,6,"Auto-etiquetar");
            if(bigHere>=0) AppendMenuA(menu,MF_STRING,15,"Borrar la etiqueta mas grande aqui (la de fondo + sus anillos)");
            AppendMenuA(menu,MF_STRING,10,"Nueva etiqueta..."); }
        POINT pt={mx,my}; ClientToScreen(h,&pt);
        int cmd=TrackPopupMenu(menu,TPM_RETURNCMD|TPM_LEFTALIGN,pt.x,pt.y,0,h,0); DestroyMenu(menu);
        if(cmd>=CLS_BASE&&cmd<NEW_BASE){                    // cambiar etiqueta (clase)
            if(selMenu){ push_undo(); int n=set_class_in_selection(cmd-CLS_BASE);   // a TODOS los poligonos de la seleccion (uno o varios)
                if(n)std::cout<<"Etiqueta cambiada en "<<n<<" poligonos (seleccion)\n"; else A.undo.pop_back(); }
            else if(bi>=0){ push_undo(); A.dets[bi].cls=cmd-CLS_BASE; A.dirty=true; invalidate_dets_caches(); } }   // un solo poligono
        else if(cmd>=NEW_BASE)crear_caja(cmd-NEW_BASE);
        else if(cmd==3&&bi>=0){push_undo();A.dets.erase(A.dets.begin()+bi);A.sel=-1;A.dirty=true;invalidate_dets_caches();}
        else if(cmd==15&&bigHere>=0){ push_undo(); int nh=(int)A.dets[bigHere].hx.size();   // borra el poligono de FONDO mas grande aqui, con TODOS sus anillos
            A.dets.erase(A.dets.begin()+bigHere); A.sel=-1; A.selDet=A.selVert=-1; A.selVerts.clear(); A.dirty=true; invalidate_dets_caches();
            std::cout<<"Borrada la etiqueta de fondo (con "<<nh<<" anillos)\n"; }
        else if(cmd==4)play_drag_sel();
        else if(cmd==5){A.sc0=A.sc1=A.sr0=A.sr1=-1;}
        else if(cmd==11){ int n=delete_in_selection(); std::cout<<"Borradas "<<n<<" etiquetas (seleccion)\n"; }
        else if(cmd==6)auto_segment();
        else if(cmd==12)auto_segment_in_selection(true);    // autoetiquetar el area como POLIGONOS
        else if(cmd==13)auto_segment_in_selection(false);   // autoetiquetar el area como CAJAS
        else if(cmd==7)play_det(bi);
        else if(cmd==8&&bi>=0){ show_busy("Mejorando etiqueta..."); push_undo(); improve_det(bi); }
        else if(cmd==9&&bi>=0){ push_undo(); A.buffering=true; A.bufSel=bi; A.bufOrig=A.dets[bi]; A.bufOrigBuf=A.autoBuffer; }  // abre modal de buffer
        else if(cmd==14&&bi>=0){ A.tool=T_EDIT; A.sel=bi; A.polyX.clear();A.polyY.clear();A.cutX.clear();A.cutY.clear();A.selDet=A.selVert=-1;A.selVerts.clear(); }  // Editar esta etiqueta
        else if(cmd==10){A.naming=true;A.nameBuf.clear();}
        return 0; }
    case WM_RBUTTONUP:{ if(!A.rbDrag)return 0; A.rbDrag=false;
        bool clic=(std::abs(A.rbx1-A.rbx0)<8&&std::abs(A.rby1-A.rby0)<8);
        if(clic){   // CLIC DERECHO simple = BORRA el vertice (exterior o de un anillo) del POLIGONO SELECCIONADO mas cercano al cursor (Editar no toca otros poligonos)
            int bestD=-1,bestK=-1,bestHole=-1; double bd=12*12;
            if(A.sel>=0&&A.sel<(int)A.dets.size()){ int i=A.sel; const Det&d=A.dets[i];
              if(d.kind==KIND_POLY&&d.px.size()>=3){
                for(size_t k=0;k<d.px.size();++k){ float sx,sy; spec_to_main((float)d.px[k],(float)d.py[k],sx,sy);
                    double dd=(A.rbx1-sx)*(A.rbx1-sx)+(A.rby1-sy)*(A.rby1-sy); if(dd<bd){bd=dd;bestD=i;bestK=(int)k;bestHole=-1;} }
                for(size_t hh=0;hh<d.hx.size();++hh) for(size_t k=0;k<d.hx[hh].size();++k){ float sx,sy; spec_to_main((float)d.hx[hh][k],(float)d.hy[hh][k],sx,sy);   // vertices de ANILLOS
                    double dd=(A.rbx1-sx)*(A.rbx1-sx)+(A.rby1-sy)*(A.rby1-sy); if(dd<bd){bd=dd;bestD=i;bestK=(int)k;bestHole=(int)hh;} } } }
            if(bestD>=0){ push_undo(); Det&d=A.dets[bestD];
                if(bestHole>=0){ if((int)d.hx[bestHole].size()<=3){ d.hx.erase(d.hx.begin()+bestHole); d.hy.erase(d.hy.begin()+bestHole); }
                    else { d.hx[bestHole].erase(d.hx[bestHole].begin()+bestK); d.hy[bestHole].erase(d.hy[bestHole].begin()+bestK); } }
                else if((int)d.px.size()<=3){ A.dets.erase(A.dets.begin()+bestD); A.sel=-1; invalidate_dets_caches(); }
                else { d.px.erase(d.px.begin()+bestK); d.py.erase(d.py.begin()+bestK); sync_bbox_from_poly(d); }
                A.selDet=A.selVert=-1; A.selVertHole=-1; A.dirty=true; std::cout<<"Vertice borrado (clic der)\n"; }
            return 0; }
        // ARRASTRE DERECHO = RECTANGULO que MARCA varios vertices (contorno exterior Y anillos internos; Supr los borra)
        int x0=min(A.rbx0,A.rbx1),x1=max(A.rbx0,A.rbx1),y0=min(A.rby0,A.rby1),y1=max(A.rby0,A.rby1);
        A.selVerts.clear();
        auto verticesIn=[&](int di)->std::vector<std::pair<int,int>>{ std::vector<std::pair<int,int>> r; const Det&d=A.dets[di]; if(d.kind!=KIND_POLY)return r;
            for(size_t k=0;k<d.px.size();++k){ float sx,sy; spec_to_main((float)d.px[k],(float)d.py[k],sx,sy);
                if(sx>=x0&&sx<=x1&&sy>=y0&&sy<=y1)r.push_back({-1,(int)k}); }                       // vertices del contorno EXTERIOR
            for(size_t hh=0;hh<d.hx.size();++hh) for(size_t k=0;k<d.hx[hh].size();++k){ float sx,sy; spec_to_main((float)d.hx[hh][k],(float)d.hy[hh][k],sx,sy);
                if(sx>=x0&&sx<=x1&&sy>=y0&&sy<=y1)r.push_back({(int)hh,(int)k}); }                  // vertices de los ANILLOS internos
            return r; };
        int target=-1; std::vector<std::pair<int,int>> vs;
        if(A.sel>=0&&A.sel<(int)A.dets.size()){ vs=verticesIn(A.sel); if(!vs.empty())target=A.sel; }   // SOLO el poligono seleccionado (Editar no marca vertices de otros)
        if(target>=0){ A.sel=target; A.selDet=target; A.selVert=-1; A.selVertHole=-1; A.selVerts=vs;
            std::cout<<"Vertices marcados: "<<vs.size()<<" (Supr para borrar)\n"; }
        return 0; }
    case WM_MOUSEWHEEL:{ int dz=GET_WHEEL_DELTA_WPARAM(wp);
        POINT pt={(LONG)(short)LOWORD(lp),(LONG)(short)HIWORD(lp)}; ScreenToClient(h,&pt);  // coords de PANTALLA con signo (pueden ser negativas en multi-monitor)
        if(A.listOpen){ float lx,ly,lw,lh; list_rect(lx,ly,lw,lh);   // rueda sobre la LISTA = scroll de la lista
            if(pt.x>=lx&&pt.x<=lx+lw&&pt.y>=ly&&pt.y<=ly+lh){ A.listScroll+=(dz>0?-3:3); if(A.listScroll<0)A.listScroll=0; return 0; } }
        if(A.spec.W>0 && pt.y>=panel_y0()){ scroll_pan(dz>0?-1:1); return 0; }   // rueda sobre el AREA INFERIOR (tira+oscilograma+scroll, donde se ve la ventana) = mueve la ventana de tiempo
        if(A.tool==T_ERASER&&A.view==1&&A.spec.W>0 && pt.x>=plotX0()-2&&pt.x<=plotX1()+2&&pt.y>=plotY0()-2&&pt.y<=plotY1()+2){   // rueda sobre espectrograma con borrador = ajustar radio
            A.eraserRadius=max(1,min(50,A.eraserRadius+(dz>0?1:-1))); return 0; }
        if(A.view==1&&A.spec.W>0 && pt.x>=plotX0()-2&&pt.x<=plotX1()+2&&pt.y>=plotY0()-2&&pt.y<=plotY1()+2){   // rueda sobre el espectrograma 2D = ZOOM hacia el cursor
            int c,r; main_to_spec(pt.x,pt.y,c,r); zoom2d(dz>0?0.8f:1.25f,c,r); return 0; }
        A.dist*=(dz>0)?0.9f:1.1f; A.dist=max(0.5f,min(20.f,A.dist)); return 0; }
    case WM_KEYDOWN:
        if(A.cmapOpen){   // navegacion del combo de mapa de color: flechas = previsualiza, Enter = aceptar
            if(wp==VK_DOWN){ A.cmap=(A.cmap+1)%CMAP_N; build_cmap(); upload_texture(); return 0; }
            if(wp==VK_UP){ A.cmap=(A.cmap+CMAP_N-1)%CMAP_N; build_cmap(); upload_texture(); return 0; }
            if(wp==VK_RETURN){ A.cmapOpen=false; return 0; } }
        if(A.listOpen){   // LISTA de etiquetas abierta: flechas navegan/seleccionan, Supr borra, Esc cierra
            int n=(int)A.dets.size(); bool shf=(GetKeyState(VK_SHIFT)&0x8000)!=0, ctl=(GetKeyState(VK_CONTROL)&0x8000)!=0;
            if((wp==VK_DOWN||wp==VK_UP)&&n>0){ int dir=(wp==VK_DOWN)?1:-1;
                A.listCursor=(A.listCursor<0)?0:max(0,min(n-1,A.listCursor+dir)); list_ensure_visible(A.listCursor);
                auto order=list_order(); int di=(A.listCursor>=0&&A.listCursor<(int)order.size())?order[A.listCursor]:-1;
                if(shf){ if(A.listAnchor<0)A.listAnchor=A.listCursor; list_select_range(A.listAnchor,A.listCursor); }   // Shift+flecha = extiende
                else if(!ctl){ A.listSel.clear(); if(di>=0)A.listSel.push_back(di); A.listAnchor=A.listCursor; list_reveal(di); }   // flecha sola = una; Ctrl+flecha = solo mueve cursor
                A.sel=(A.listSel.size()==1)?A.listSel[0]:-1; return 0; }
            if(wp==VK_PRIOR){ A.listScroll-=list_visrows(); if(A.listScroll<0)A.listScroll=0; return 0; }   // RePag
            if(wp==VK_NEXT){ A.listScroll+=list_visrows(); return 0; }                                       // AvPag
            if(wp==VK_DELETE){ int k=list_delete_selected(); std::cout<<"Borradas "<<k<<" etiquetas (lista)\n"; return 0; }
            if(wp==VK_ESCAPE){ A.listOpen=false; A.listSel.clear(); A.listCursor=A.listAnchor=-1; return 0; }
        }
        if((wp=='Z'||wp=='z')&&(GetKeyState(VK_CONTROL)&0x8000)){ do_undo(); return 0; }   // Ctrl+Z = deshacer
        if((wp=='Y'||wp=='y')&&(GetKeyState(VK_CONTROL)&0x8000)){ do_redo(); return 0; }   // Ctrl+Y = rehacer
        if(wp==VK_DELETE){
            if(A.tool==T_EDIT){                                                       // EDITAR: Supr SOLO borra vertices del poligono seleccionado (nunca la etiqueta entera)
                push_undo();
                if(delete_marked_vertex()){ std::cout<<"Vertice borrado\n"; return 0; }   // delete_marked_vertex ya marca A.dirty
                A.undo.pop_back(); return 0; }                                         // nada marcado -> no borra nada
            push_undo();
            if(A.sel>=0&&A.sel<(int)A.dets.size()){                                    // SELECCION: Supr borra la ETIQUETA seleccionada
                A.dets.erase(A.dets.begin()+A.sel); A.sel=-1; A.selDet=A.selVert=-1; A.selVertHole=-1; A.selVerts.clear(); A.dirty=true; invalidate_dets_caches();
                std::cout<<"Etiqueta borrada\n"; return 0; }
            if(A.sc0>=0&&A.sc1>A.sc0){
                int n=cut_in_selection(); if(n)std::cout<<"Cortadas "<<n<<" etiquetas (seleccion)\n"; else A.undo.pop_back(); return 0; }
            int n=delete_in_selection(); if(n)std::cout<<"Borradas "<<n<<" etiquetas (seleccion)\n"; else A.undo.pop_back(); return 0; }  // sin etiqueta sel.: borra por rectangulo de seleccion (o descarta el snapshot)
        if(wp==VK_ESCAPE){   // Esc: cancela el modo activo; NUNCA cierra la ventana (se sale con 'q')
            if(A.showAbout)A.showAbout=false;                                      // cierra el panel Acerca de
            else if(A.cmapOpen)A.cmapOpen=false;                                   // cierra el combo de mapa de color
            else if(A.buffering){ if(A.bufSel>=0&&A.bufSel<(int)A.dets.size())A.dets[A.bufSel]=A.bufOrig; A.autoBuffer=A.bufOrigBuf; A.buffering=false; }  // cancela: restaura
            else if(A.naming)A.naming=false; else if(!A.polyX.empty()){A.polyX.clear();A.polyY.clear();} else if(!A.cutX.empty()){A.cutX.clear();A.cutY.clear();}
            else if(A.tool==T_EDIT){ A.tool=T_SELECT; A.selDet=A.selVert=-1; A.selVerts.clear(); A.editVert=-1; A.editHole=-1; }   // Esc DESACTIVA la edicion
            else { A.selVerts.clear(); A.selDet=A.selVert=-1; A.sc0=A.sc1=A.sr0=A.sr1=-1; }   // limpia marcas/seleccion
            return 0; }
        // Ctrl + +/=: agrandar etiqueta seleccionada
        if((GetKeyState(VK_CONTROL)&0x8000)&&(wp==VK_OEM_PLUS||wp==VK_ADD||wp=='='||wp=='+')){ grow_selected_label(); return 0; }
        // Ctrl + -: achicar etiqueta seleccionada
        if((GetKeyState(VK_CONTROL)&0x8000)&&(wp==VK_OEM_MINUS||wp==VK_SUBTRACT||wp=='-')){ shrink_selected_label(); return 0; }
        return 0;
    case WM_CHAR:
        if(A.buffering){ if((int)wp==13)A.buffering=false; return 0; }   // Enter = aplicar buffer
        if(A.naming){ int ch=(int)wp;
            if(ch==13){ if(!A.nameBuf.empty())add_class(A.nameBuf); A.naming=false; layout_botones(); }
            else if(ch==27)A.naming=false;
            else if(ch==8){ if(!A.nameBuf.empty())A.nameBuf.pop_back(); }
            else if(ch>=32&&ch<127&&(int)A.nameBuf.size()<24)A.nameBuf.push_back((char)ch);
            return 0; }
        // Ctrl + +: agrandar etiqueta
        if((GetKeyState(VK_CONTROL)&0x8000)&&((int)wp=='+'||(int)wp=='=')){ grow_selected_label(); return 0; }
        // Ctrl + -: achicar etiqueta
        if((GetKeyState(VK_CONTROL)&0x8000)&&(int)wp=='-'){ shrink_selected_label(); return 0; }
        if((int)wp==26){ do_undo(); return 0; }   // Ctrl+Z llega como WM_CHAR 0x1A
        if((int)wp==25){ do_redo(); return 0; }   // Ctrl+Y llega como WM_CHAR 0x19
        if((int)wp==13){ if(!A.polyX.empty())finish_poly(); return 0; }   // Enter cierra el poligono a mano
        do_action((int)wp); return 0;
    case WM_DESTROY: PLAYER.stop(); PostQuitMessage(0); return 0;
    }
    return DefWindowProc(h,m,wp,lp);
}

int main(int argc,char**argv){
    A.out_dir=(argc>=3)?argv[2]:"."; layout_botones(); build_cmap();   // LUT de color inicial (Magma)
    HINSTANCE hi=GetModuleHandle(0); WNDCLASSA wc{}; wc.style=CS_OWNDC; wc.lpfnWndProc=WndProc;
    wc.hInstance=hi; wc.lpszClassName=CLS; wc.hCursor=LoadCursor(0,IDC_ARROW);
    wc.hIcon=LoadIcon(hi,MAKEINTRESOURCE(1)); RegisterClassA(&wc);
    HWND hwnd=CreateWindowA(CLS,"IIAP SachaAcoustic - espectrograma 2D / terreno 3D / rio / nube / ondas / quiver / volumen",
        WS_OVERLAPPEDWINDOW,CW_USEDEFAULT,CW_USEDEFAULT,A.cW,A.cH,0,0,hi,0);
    HDC hdc=GetDC(hwnd); PIXELFORMATDESCRIPTOR pfd{}; pfd.nSize=sizeof(pfd); pfd.nVersion=1;
    pfd.dwFlags=PFD_DRAW_TO_WINDOW|PFD_SUPPORT_OPENGL|PFD_DOUBLEBUFFER; pfd.iPixelType=PFD_TYPE_RGBA;
    pfd.cColorBits=24; pfd.cDepthBits=24; int pf=ChoosePixelFormat(hdc,&pfd); SetPixelFormat(hdc,pf,&pfd);
    HGLRC rc=wglCreateContext(hdc); wglMakeCurrent(hdc,rc); g_hdc=hdc; g_hwnd=hwnd; FONT.init(hdc); glEnable(GL_POINT_SMOOTH);
    ShowWindow(hwnd,SW_SHOW); UpdateWindow(hwnd); ayuda();
    DragAcceptFiles(hwnd,TRUE);   // habilita ARRASTRAR Y SOLTAR un WAV sobre la ventana (WM_DROPFILES)
    if(argc>=2) load_audio(argv[1]);
    if(argc>=4){ A.yaw=(float)atof(argv[3]); A.view=2; layout_botones(); }   // (preview) yaw inicial + vista 3D
    if(argc>=5){ A.dbMin=(float)atof(argv[4]); upload_texture(); eraser_reset_if_filter_changed(); }  // (preview) piso dB para ralear la superficie
    if(argc>=6){ A.pitch=(float)atof(argv[5]); }                              // (preview) pitch inicial
    MSG msg; bool run=true;
    int rfc=0;
    while(run){ while(PeekMessage(&msg,0,0,0,PM_REMOVE)){ if(msg.message==WM_QUIT){run=false;break;} TranslateMessage(&msg); DispatchMessage(&msg);} render(); SwapBuffers(hdc); autosave_tick();
        if(A.refilterPending && ++rfc>=12){ rfc=0; A.refilterPending=false; refilter_live(); }   // filtros en TIEMPO REAL sobre el sonido (~cada 140ms, incl. durante el arrastre)
        Sleep(10); }
    guardar(true);   // autosave al cerrar
    wglMakeCurrent(0,0); wglDeleteContext(rc); ReleaseDC(hwnd,hdc); return 0;
}
