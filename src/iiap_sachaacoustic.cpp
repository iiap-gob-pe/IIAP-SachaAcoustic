// raven.cpp - Workstation bioacustica tipo Raven (Win32 + OpenGL, sin OpenCV).
//
// Vistas (1-5): 1) Espectrograma 2D  2) Terreno 3D (ejes configurables)
//   3) Mandala radial  4) Rio espectral 3D  5) Constelacion armonica.
// Panel inferior SIEMPRE visible: tira de espectrograma (para etiquetar incluso
// en 3D) + oscilograma. Barra de BOTONES arriba. Playhead con pausa/seek.
// Reproduccion de la seleccion recortada en TIEMPO y FRECUENCIA (paso-banda).
#define NOMINMAX
#include <windows.h>
#include <GL/gl.h>
#include <GL/glu.h>
#include <commdlg.h>
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
enum { T_SELECT = 0, T_BBOX = 1, T_POLY = 2, T_EDIT = 3, T_CUT = 4 };

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
    bool wand = true;                // en modo poligono: varita magica ON/OFF (OFF = trazar a mano)
    float wandFrac = 0.55f;          // varita: umbral relativo al valor del clic
    int autoBuffer = 2;             // buffer (px) de suavizado para auto-poligono
    float gain = 1.0f;               // ganancia de reproduccion (1=normalizado; >1 sube mas)
    bool hide_labels = false;        // ocultar etiquetas (cajas/poligonos) del dibujo
    float volFLo=0.f, volFHi=1.f, volDbMin=0.f, volDbMax=1.f;  // filtros LOCALES de la vista Volumen
    std::vector<int> polyX, polyY;   // poligono en construccion (modo poligono a mano)
    std::vector<int> cutX, cutY;     // trazo de corte LIBRE (modo Cortar); clic-der ejecuta
    bool naming = false; std::string nameBuf;   // modo "crear etiqueta": captura de texto
    bool dirty = false;              // hay cambios sin guardar -> autosave
    unsigned long long lastSig = 0; bool sigInit = false;   // firma de etiquetas para autosave
    bool buffering = false; int bufSel = -1; Det bufOrig; int bufOrigBuf = 0;  // modal de buffer (con copia para cancelar)
    int editVert = -1;               // poligono: vertice en edicion ; bbox: 100+manija, 108 mover
    int lastEditC = 0, lastEditR = 0;// ultima posicion (spec) al mover un poligono (para trasladar)
    int vr0 = 0, vr1 = 0;            // ventana VERTICAL (filas/freq) visible en la vista 2D; vr1=0 -> todo
    bool playMask = false;           // reproduccion limitada a la mascara de etiquetas (vista Volumen)
    int selDet = -1, selVert = -1;   // vertice marcado (selDet,selVert) de un poligono
    std::vector<int> selVerts;       // EDITAR: conjunto de vertices del poligono selDet marcados con rectangulo (clic-der)
    bool rbDrag=false; int rbx0=0,rby0=0,rbx1=0,rby1=0;   // rectangulo de seleccion de vertices con BOTON DERECHO (modo Editar)

    // seleccion en coords de espectro (tiempo c, frecuencia r)
    int sc0 = -1, sc1 = -1, sr0 = -1, sr1 = -1;
    int cursor_col = 0;          // posicion para empezar a reproducir / seek

    bool dragging = false; int dx0, dy0, dx1, dy1; int dragRegion = 0;
    bool modo_hilo = false;
    bool solo_banda = true;     // reproducir solo la banda (freq) de la seleccion
    bool rio_completo = false;  // rio: mostrar el hilo COMPLETO si algun punto pasa el filtro
    bool quiver_completo = true;// quiver: glifos completos por hilo (vs solo puntos que pasan)
    int mx = 0, my = 0;         // posicion del raton (para tooltips)
    bool show_hist = true;      // histograma (freq + dB) sobre el espectrograma 2D

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
static void show_busy(const std::string& msg);         // indicador de carga (def. mas abajo)
static void layout_botones();                           // (def. mas abajo) recalcula barra/paleta
static unsigned long long labels_sig();                 // (def. mas abajo) firma de etiquetas (autosave)
static const char* CLS = "RavenLikeGL";

static const int HUD_H = 20;
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
// filtros que controlan las barras: GLOBALES, salvo la vista Volumen (V9), que tiene
// sus propios filtros LOCALES (solo afectan a esa vista).
static float& flt_fLo(){ return A.view==9?A.volFLo:A.fLo; }
static float& flt_fHi(){ return A.view==9?A.volFHi:A.fHi; }
static float& flt_dbMin(){ return A.view==9?A.volDbMin:A.dbMin; }
static float& flt_dbMax(){ return A.view==9?A.volDbMax:A.dbMax; }
static void spec_to_main(float c,float r,float&sx,float&sy);   // def. mas abajo
static void sync_bbox_from_poly(Det&d);                        // def. mas abajo
// modal de buffer: rectangulo de la pista del slider {tx0,tx1,ty}
static void bufslider(float& tx0,float& tx1,float& ty){ float w=400,x=A.cW*0.5f-w*0.5f,y=A.cH*0.5f-45;
    tx0=x+24; tx1=x+w-24; ty=y+56; }

// X SIEMPRE = tiempo (dim 0). Rotar solo intercambia freq(1)/dB(2) entre Y y Z.
// dB SIEMPRE vertical (Y). Default: X=Tiempo (comparte la pared DELANTERA con dB -> "L"
// al frente), Z=Frecuencia (profundidad). 'r' intercambia tiempo/freq en el piso.
static const int PERM[2][3] = {{0,2,1},{1,2,0}};  // {X=t,Y=dB,Z=freq} , {X=freq,Y=dB,Z=t}
static const char* DIMN[3] = {"tiempo","freq","dB"};

static void colorf(float v, float& r, float& g, float& b) {
    v = v < 0 ? 0 : (v > 1 ? 1 : v); int i = (int)(v * 255.0f + 0.5f);
    r = MAGMA_LUT[i][0]/255.f; g = MAGMA_LUT[i][1]/255.f; b = MAGMA_LUT[i][2]/255.f;
}
static size_t next_pow2(size_t n){ size_t p=1; while(p<n) p<<=1; return p; }
// filtro: pasa si freq-fraccion en [fLo,fHi] y energia >= dbMin
static bool pass_filt(float f,float e){ return f>=A.fLo && f<=A.fHi && e>=A.dbMin && e<=A.dbMax; }

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
static float tnorm(int c){ int a=vlo(),b=vhi(); return (float)(c-a)/(float)max(1,b-a-1); }  // tiempo norm en la ventana
static bool cin(int c){ return c>=vlo() && c<vhi(); }
// ventana VERTICAL (filas/frecuencia) visible en la vista 2D
static int rlo(){ return max(0,A.vr0); }
static int rhi(){ int b=(A.vr1>A.vr0)?A.vr1:A.spec.H; return min(A.spec.H,b); }

// ---------------- analisis: crestas, picos, hilos, oscilograma ----------
static void build_rios() {
    A.rios.clear(); const Img& e = A.enh; int W=e.W,H=e.H; float thr=0.35f;
    std::vector<Hilo> act;
    for (int c=0;c<W;++c){
        std::vector<int> pk;
        for(int r=1;r<H-1;++r){ float v=e.at(r,c); if(v>thr&&v>=e.at(r-1,c)&&v>=e.at(r+1,c)) pk.push_back(r);}
        std::vector<bool> used(act.size(),false);
        for(int pr:pk){ int best=-1,bd=6; for(size_t k=0;k<act.size();++k){ if(used[k])continue; int d=std::abs(act[k].row.back()-pr); if(d<bd){bd=d;best=(int)k;}}
            if(best>=0){act[best].col.push_back(c);act[best].row.push_back(pr);used[best]=true;}
            else{Hilo h;h.col.push_back(c);h.row.push_back(pr);act.push_back(h);used.push_back(true);}}
        std::vector<Hilo> nxt;
        for(size_t k=0;k<act.size();++k){ if(used[k]&&act[k].col.back()==c) nxt.push_back(act[k]); else if(act[k].col.size()>=8) A.rios.push_back(act[k]); }
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
    for(int r=0;r<H;++r){ float f=(float)(H-1-r)/(H-1);
        for(int x=0;x<W;++x){ float e=A.enh.at(r,x); size_t i=((size_t)r*W+x)*3;
            if(pass_filt(f,e)){ int k=(int)(min(1.f,max(0.f,e))*255+0.5f);
                c.d[i]=MAGMA_LUT[k][0];c.d[i+1]=MAGMA_LUT[k][1];c.d[i+2]=MAGMA_LUT[k][2]; }
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

// varita magica: crece region conexa desde (c0,r0) sobre A.enh con umbral relativo;
// crea un Det poligono editable de la clase activa.
static void magic_wand(int c0,int r0){ int W=A.enh.W,H=A.enh.H; if(W<2||H<2)return;
    float seed=A.enh.at(r0,c0); float thr=max(0.10f,seed*A.wandFrac);
    std::vector<uint8_t> vis((size_t)W*H,0); std::vector<int> lab((size_t)W*H,0);
    std::vector<std::pair<int,int>> st; st.push_back({c0,r0}); vis[(size_t)r0*W+c0]=1;
    int minx=c0,maxx=c0,miny=r0,maxy=r0,cnt=0;
    const int dx8[8]={1,1,0,-1,-1,-1,0,1},dy8[8]={0,1,1,1,0,-1,-1,-1};
    while(!st.empty()){ auto[cx,cy]=st.back(); st.pop_back(); ++cnt;
        lab[(size_t)cy*W+cx]=1;
        minx=min(minx,cx);maxx=max(maxx,cx);miny=min(miny,cy);maxy=max(maxy,cy);
        for(int k=0;k<8;++k){ int nx=cx+dx8[k],ny=cy+dy8[k]; if(nx<0||nx>=W||ny<0||ny>=H)continue;
            size_t ni=(size_t)ny*W+nx; if(vis[ni])continue; if(A.enh.at(ny,nx)>=thr){vis[ni]=1;st.push_back({nx,ny});} } }
    if(cnt<6)return;
    Det d; d.kind=KIND_POLY; d.cls=A.clase_activa; d.x=minx;d.y=miny;d.w=maxx-minx+1;d.h=maxy-miny+1;
    std::vector<int> bx,by; trace_boundary(lab,W,H,1,c0,r0,bx,by);
    for(size_t i=0;i<bx.size();i+=3){ d.px.push_back(bx[i]); d.py.push_back(by[i]); }
    if(d.px.size()<3){ d.px={d.x,d.x+d.w,d.x+d.w,d.x}; d.py={d.y,d.y,d.y+d.h,d.y+d.h}; }
    A.dets.push_back(std::move(d)); A.sel=(int)A.dets.size()-1; }
// cierra el poligono en construccion como un Det de la clase activa
static void finish_poly(){ if(A.polyX.size()<3){A.polyX.clear();A.polyY.clear();return;}
    Det d; d.kind=KIND_POLY; d.cls=A.clase_activa; d.px=A.polyX; d.py=A.polyY;
    int mnx=d.px[0],mxx=d.px[0],mny=d.py[0],mxy=d.py[0];
    for(size_t k=0;k<d.px.size();++k){mnx=min(mnx,d.px[k]);mxx=max(mxx,d.px[k]);mny=min(mny,d.py[k]);mxy=max(mxy,d.py[k]);}
    d.x=mnx;d.y=mny;d.w=max(1,mxx-mnx);d.h=max(1,mxy-mny);
    A.dets.push_back(std::move(d)); A.sel=(int)A.dets.size()-1; A.polyX.clear();A.polyY.clear(); }
static bool load_audio(const std::string& path){ try{
    { size_t p=path.find_last_of("/\\"); show_busy(std::string("Cargando ")+((p==std::string::npos)?path:path.substr(p+1))+" ..."); }
    A.audio=load_wav(path); A.sr=A.audio.sample_rate; A.spec=compute_spectrogram(A.audio,A.P);
    { size_t p=path.find_last_of("/\\"); A.fname=(p==std::string::npos)?path:path.substr(p+1); }   // nombre del archivo
    A.enh=enhance_asinh(A.spec,0.07); A.dets.clear();   // NO auto-etiquetar al cargar (lo hace el boton Auto)
    A.hilos.clear(); A.sel=-1; A.sc0=A.sc1=A.sr0=A.sr1=-1; A.cursor_col=0; A.polyX.clear(); A.polyY.clear(); A.cutX.clear(); A.cutY.clear(); A.selDet=A.selVert=-1; A.selVerts.clear(); A.rbDrag=false;
    A.vr0=0; A.vr1=A.spec.H; A.sigInit=false; A.dirty=false;   // ventana freq completa + base de autosave
    A.vc0=0; A.vc1=A.spec.W;                                   // ventana = todo el audio
    double dur=A.sr?(double)A.audio.samples.size()/A.sr:0;     // eje X segun duracion
    A.tlen=(float)max(2.5,min(14.0,dur*0.7)); A.crosslen=1.7f;  // tiempo largo, freq/dB mas grandes
    A.ax[0]=A.tlen; A.ax[1]=A.ax[2]=A.crosslen;                 // escalas iniciales del cubo (editables con manijas)
    A.dist=max(5.0f,A.tlen*1.5f+A.crosslen*1.6f); A.yaw=18; A.pitch=34;  // ortoedro: frente=+Z, derecha=+X; vista frontal-elevada
    build_rios(); build_picos(); build_env(); upload_texture();
    // auto-cargar etiquetas guardadas: si en out_dir existe <nombre_audio>.json (COCO), cargarlas
    { std::string stem=A.fname; size_t dp=stem.find_last_of('.'); if(dp!=std::string::npos)stem=stem.substr(0,dp);
      std::string jp=A.out_dir+"/"+stem+".json"; std::ifstream tf(jp.c_str());
      if(tf.good()){ tf.close(); std::vector<Det> in=import_coco(jp,A.classes);
        if(!in.empty()){ A.dets=in; if(A.clase_activa>=(int)A.classes.size())A.clase_activa=0; layout_botones();
          std::cout<<"Etiquetas cargadas de "<<jp<<": "<<in.size()<<"\n"; } } }
    A.lastSig=labels_sig(); A.sigInit=true;   // base del autosave = estado recien cargado
    std::cout<<"Cargado "<<path<<": "<<A.spec.W<<"x"<<A.spec.H<<", "<<A.dets.size()<<" det, "<<A.rios.size()<<" crestas\n";
    return true;}catch(const std::exception&e){std::cerr<<"Error: "<<e.what()<<"\n";return false;} }
static bool open_dialog(){ char f[MAX_PATH]=""; OPENFILENAMEA o{}; o.lStructSize=sizeof(o);
    o.lpstrFilter="WAV\0*.wav\0Todos\0*.*\0"; o.lpstrFile=f; o.nMaxFile=MAX_PATH;
    o.Flags=OFN_FILEMUSTEXIST|OFN_PATHMUSTEXIST; if(GetOpenFileNameA(&o)) return load_audio(f); return false; }

// ---------------- reproduccion con filtros aplicados al SONIDO ----------
// STFT enmascarada (mismo criterio que el display: pass_filt sobre freq+dB) +
// banda de la seleccion (si solo_banda) -> reconstruccion overlap-add (WOLA).
static void play_filtered(int c0,int c1,bool useSel,float selLo,float selHi){
    const auto& s=A.audio.samples; if(s.empty())return;
    int W=A.spec.W,H=A.spec.H, nfft=A.P.nfft, hop=A.P.hop;
    c0=max(0,min(W-1,c0)); c1=max(c0+1,min(W,c1));
    size_t i0=(size_t)c0*hop, i1=min(s.size(),(size_t)c1*hop+nfft); if(i1<=i0)return;
    size_t outN=i1-i0;
    std::vector<float> out(outN,0.f), wsum(outN,0.f), win(nfft);
    for(int k=0;k<nfft;++k) win[k]=0.5f*(1-std::cos(2.0f*(float)M_PI*k/(nfft-1)));
    std::vector<Cf> X(nfft);
    // filtro activo para el SONIDO: global, salvo en la vista Volumen (V9) que usa el suyo local
    float qfLo=(A.view==9)?A.volFLo:A.fLo, qfHi=(A.view==9)?A.volFHi:A.fHi;
    float qdLo=(A.view==9)?A.volDbMin:A.dbMin, qdHi=(A.view==9)?A.volDbMax:A.dbMax;
    bool useMask=A.playMask;         // limitar a la mascara de etiquetas (solo lo que esta dentro de las etiquetas)
    bool anyFilt = (qfLo>0||qfHi<1||qdLo>0||qdHi<1||useSel||useMask);
    for(int c=c0;c<c1;++c){ size_t off=(size_t)c*hop; int cc=min(c,W-1);
        for(int k=0;k<nfft;++k){ size_t idx=off+k; float sv=(idx<s.size())?s[idx]:0.f; X[k]=Cf(sv*win[k],0); }
        fft(X);
        if(anyFilt) for(int k=0;k<nfft;++k){ int b=(k<=nfft/2)?k:(nfft-k); float f=(float)b/(H-1);
            int r=(H-1)-b; if(r<0)r=0; if(r>=H)r=H-1; float e=A.enh.at(r,cc);
            bool keep=(f>=qfLo&&f<=qfHi&&e>=qdLo&&e<=qdHi); if(useSel&&(f<selLo||f>selHi))keep=false;
            if(keep&&useMask){ bool in=false;       // ¿(cc,r) cae dentro de alguna etiqueta?
                for(const Det&d:A.dets){ if(cc<d.x||cc>=d.x+d.w||r<d.y||r>=d.y+d.h)continue;
                    if(d.kind==KIND_POLY&&d.px.size()>=3){ if(pt_in_poly(d.px,d.py,cc+0.5f,r+0.5f)){in=true;break;} }
                    else { in=true; break; } }
                if(!in)keep=false; }
            if(!keep) X[k]=Cf(0,0); }
        ifft(X);
        size_t base=off-i0;
        for(int k=0;k<nfft;++k){ size_t p=base+k; if(p<outN){ out[p]+=X[k].real()*win[k]; wsum[p]+=win[k]*win[k]; } }
    }
    float mx=1e-6f; for(size_t i=0;i<outN;++i){ if(wsum[i]>1e-6f)out[i]/=wsum[i]; mx=max(mx,std::fabs(out[i])); }
    float g=0.9f*A.gain/mx; std::vector<int16_t> pcm(outN);   // ganancia: >1 satura (sube volumen)
    for(size_t i=0;i<outN;++i){ float v=out[i]*g; v=v<-1?-1:(v>1?1:v); pcm[i]=(int16_t)(v*32767); }
    int fade=min((int)(outN/2),max(1,A.sr/150));   // rampa ~7ms: evita el GOLPE/click al iniciar y al terminar
    for(int i=0;i<fade;++i){ float gg=(float)i/fade; pcm[i]=(int16_t)(pcm[i]*gg); pcm[outN-1-i]=(int16_t)(pcm[outN-1-i]*gg); }
    A.playC1=c1; A.playUseSel=useSel; A.playSelLo=selLo; A.playSelHi=selHi; A.playCtx=true;  // contexto p/ re-filtrar en vivo
    PLAYER.play_buffer(std::move(pcm),A.sr,i0);
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
// Play principal: reproduce la ventana visible. En la vista VOLUMEN (V9) reproduce SOLO
// lo que esta DENTRO de las etiquetas (mascara) y con los filtros locales aplicados.
static void play_window(){ if(A.audio.samples.empty())return;
    A.playMask=(A.view==9); play_filtered(vlo(),vhi(),false,0,1); }

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
        {"2D",'1',-1,0,"Vista espectrograma 2D"},{"3D",'2',-1,0,"Vista terreno 3D"},{"Mand",'3',-1,0,"Vista mandala radial"},
        {"Rio",'4',-1,0,"Vista rio espectral (crestas)"},{"Const",'5',-1,0,"Vista constelacion armonica"},
        {"Nube",'6',-1,0,"Vista nube de puntos"},{"Ondas",'7',-1,0,"Vista ondas 3D"},{"Quiver",'8',-1,0,"Vista Quiver3D (glifos por hilo)"},
        {"Vol",'9',-1,0,"Vista volumen 3D: nube de puntos densa por etiqueta (freq y dB)"},
        {0,0,0,0,0},
        // --- Reproduccion ---
        {"",' ',1,0,"Reproducir la ventana visible"},{"",'k',2,0,"Pausar / reanudar"},{"",'.',3,0,"Detener"},
        {"Banda",'B',-1,0,"Reproducir SOLO la banda (tiempo + frecuencia)"},
        {0,0,0,0,0},
        // --- Navegacion ---
        {"",'T',6,0,"Ver todo el audio"},{"",'Z',4,0,"Acercar la ventana (zoom +)"},{"",'U',5,0,"Alejar la ventana (zoom -)"},
        {0,0,0,0,0},
        // --- Display / filtro ---
        {"Hist",'I',-1,0,"Histograma (frecuencia + dB) sobre el espectro"},
        {"Senal",'f',-1,0,"Fijar valor de senal: pasa el techo de dB actual a piso y abre el techo (muestra de ese valor a 0 dB)"},
        {0,0,0,0,0},
        // --- Etiquetado ---
        {"Selec",'S',-1,0,"Herramienta: seleccionar / escuchar"},
        {"BBox",'Y',-1,0,"Etiquetar BOUNDING BOX (arrastra un rectangulo)"},
        {"Poly",'P',-1,0,"Etiquetar POLIGONO (varita, o clic a clic; clic-der/Enter cierra)"},
        {"Varita",'V',-1,0,"Poligono: activar/desactivar VARITA MAGICA (auto vs a mano)"},
        {"Editar",'E',-1,0,"Editar forma (mueve vertices de poligono / manijas de caja)"},
        {"Cortar",'X',-1,0,"Corte LIBRE: pinta el trazo sobre la etiqueta y clic-derecho corta"},
        {"Auto",'a',-1,0,"Auto-etiquetar SOBRE EL FILTRO, con la forma activa (poligono o bbox)"},
        {"+Etiq",'N',-1,0,"Crear etiqueta nueva (escribe el nombre)"},
        {"Ocultar",'O',-1,0,"Ocultar / mostrar las etiquetas"},
        {"Limpiar",'c',-1,0,"Borrar TODAS las etiquetas"},
        {0,0,0,0,0},
        // --- 3D ---
        {"Ejes",'r',-1,0,"Intercambia los ejes frecuencia / dB"},{"Res-",'[',-1,0,"Menos resolucion 3D"},{"Res+",']',-1,0,"Mas resolucion 3D"},
        {"Crestas",'H',-1,4,"Crestas completas: muestra el hilo entero si algun punto pasa el filtro"},  // solo vista Rio
        {"Glifos",'G',-1,8,"Glifos completos: dibuja todos los palos del hilo si algun punto pasa el filtro"}, // solo Quiver
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
    for(int r=0;r<H;++r){ float f=(float)(H-1-r)/(H-1);
        for(int c=0;c<W;++c){ float e=A.enh.at(r,c); if(!pass_filt(f,e)) s.at(r,c)=0.f; } }
    return s; }
// Auto-etiquetar: detecta sobre el espectro FILTRADO; si el filtro deja todo vacio,
// reintenta sobre el espectro completo para que SIEMPRE proponga algo. Muestra etiquetas.
static void auto_segment(){ if(A.spec.W<1)return;
    show_busy("Auto-etiquetando...");
    std::vector<Det> d=auto_label(filtered_spec(),A.K,A.area_min,A.shape_poly,A.shape_poly?A.autoBuffer:0);
    if(d.empty()) d=auto_label(A.spec,A.K,A.area_min,A.shape_poly,A.shape_poly?A.autoBuffer:0);  // respaldo: espectro crudo
    A.dets=std::move(d); A.sel=-1; A.hide_labels=false;       // asegurar que se vean
    std::cout<<"Auto-etiquetado: "<<A.dets.size()<<" etiquetas\n"; }
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
    nd.x+=x0; nd.y+=y0; nd.cls=d.cls; nd.kind=KIND_POLY; d=nd; }
// modal de buffer: fija autoBuffer desde la posicion del raton y re-mejora la etiqueta
// (siempre desde la copia original, para que el slider no acumule).
static void buffer_apply(int mx){ float tx0,tx1,ty; bufslider(tx0,tx1,ty);
    float v=(mx-tx0)/(tx1-tx0>1?tx1-tx0:1); v=v<0?0:(v>1?1:v);
    A.autoBuffer=(int)(v*BUF_MAX+0.5f);
    if(A.bufSel>=0&&A.bufSel<(int)A.dets.size()){ A.dets[A.bufSel]=A.bufOrig; improve_det(A.bufSel); } }

static void draw_tex_quad(float x0,float y0,float x1,float y1,float u0=0.f,float u1=1.f,float v0=0.f,float v1=1.f){
    glEnable(GL_TEXTURE_2D); glBindTexture(GL_TEXTURE_2D,A.tex); glColor3f(1,1,1);
    glBegin(GL_QUADS); glTexCoord2f(u0,v0);glVertex2f(x0,y0); glTexCoord2f(u1,v0);glVertex2f(x1,y0);
    glTexCoord2f(u1,v1);glVertex2f(x1,y1); glTexCoord2f(u0,v1);glVertex2f(x0,y1); glEnd();
    glDisable(GL_TEXTURE_2D); }

// cajas/seleccion/playhead en [x0,y0]-[x1,y1]; columnas [cLo,cHi]->[x0,x1], filas [rLo,rHi]->[y0,y1]
static void draw_overlays(float x0,float y0,float x1,float y1,bool conHilos,int cLo,int cHi,int rLo=0,int rHi=0){
    int H=A.spec.H; if(rHi<=rLo)rHi=H; int span=max(1,cHi-cLo),rspan=max(1,rHi-rLo); float sx=(x1-x0)/span, sy=(y1-y0)/rspan;
    auto xc=[&](float c){ float x=x0+(c-cLo)*sx; return x<x0?x0:(x>x1?x1:x); };
    auto yc=[&](float r){ float y=y0+(r-rLo)*sy; return y<y0?y0:(y>y1?y1:y); };
    if(A.sc0>=0&&A.sc1>A.sc0){ glEnable(GL_BLEND);glBlendFunc(GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA);
        glColor4f(1,1,1,0.18f); float yy0=(A.sr0>=0)?yc((float)min(A.sr0,A.sr1)):y0, yy1=(A.sr1>=0)?yc((float)max(A.sr0,A.sr1)):y1;
        glBegin(GL_QUADS);glVertex2f(xc(A.sc0),yy0);glVertex2f(xc(A.sc1),yy0);glVertex2f(xc(A.sc1),yy1);glVertex2f(xc(A.sc0),yy1);glEnd(); glDisable(GL_BLEND);}
    if(!A.hide_labels) for(int i=0;i<(int)A.dets.size();++i){const Det&d=A.dets[i];
        float cr,cg,cb; class_color(d.cls,cr,cg,cb); glColor3f(cr,cg,cb); glLineWidth(i==A.sel?3.f:1.5f);
        if(d.kind==KIND_POLY&&d.px.size()>=3){ glBegin(GL_LINE_LOOP);
            for(size_t k=0;k<d.px.size();++k)glVertex2f(xc((float)d.px[k]),yc((float)d.py[k])); glEnd(); }
        else { glBegin(GL_LINE_LOOP);glVertex2f(xc(d.x),yc(d.y));glVertex2f(xc(d.x+d.w),yc(d.y));
            glVertex2f(xc(d.x+d.w),yc(d.y+d.h));glVertex2f(xc(d.x),yc(d.y+d.h));glEnd();} }
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
static void render_mandala(){ int mh=main_h(); glViewport(0,A.cH-panel_y0(),A.cW,mh); glDisable(GL_DEPTH_TEST);
    glMatrixMode(GL_PROJECTION);glLoadIdentity(); float ar=(float)A.cW/mh; gluOrtho2D(-ar,ar,-1,1);
    glMatrixMode(GL_MODELVIEW);glLoadIdentity(); int W=A.enh.W,H=A.enh.H;
    int sc=max(1,W/min(720,A.res3d*2)),sr=max(1,H/max(60,A.res3d)); const float TP=6.2831853f;
    for(int c=vlo();c+sc<vhi();c+=sc){ glBegin(GL_QUAD_STRIP); for(int r=0;r<=H-1;r+=sr) for(int cc=c;cc<=c+sc;cc+=sc){
        float ang=TP*tnorm(cc),rad=0.12f+0.85f*(float)(H-1-r)/(H-1); float e=A.enh.at(min(r,H-1),min(cc,W-1));
        float cr,cg,cb;colorf(e,cr,cg,cb);glColor3f(cr,cg,cb); glVertex2f(rad*std::cos(ang),rad*std::sin(ang)); } glEnd(); }
    int pc=playhead_col(); if(cin(pc)){ float ang=TP*tnorm(pc); glColor3f(1,1,0.3f);glLineWidth(2.f);
        glBegin(GL_LINES);glVertex2f(0.12f*std::cos(ang),0.12f*std::sin(ang));glVertex2f(0.97f*std::cos(ang),0.97f*std::sin(ang));glEnd(); } }
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
// Vista 8: QUIVER3D - por cada hilo, palos verticales (eje dB) entre la curva
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
static void render_const3d(){ setup_3d(); int W=A.enh.W,H=A.enh.H; glPointSize(3.f); glBegin(GL_POINTS);
    for(auto&pk:A.picos){int c=pk[0],r=pk[1];float t=tnorm(c),f=(float)(H-1-r)/(H-1),e=A.enh.at(r,c);
        if(!pass_filt(f,e)||!cin(c))continue; float X,Y,Z;map_axes(t,f,e,X,Y,Z);float cr,cg,cb;colorf(e,cr,cg,cb);glColor3f(cr,cg,cb);glVertex3f(X,Y,Z);} glEnd();
    glColor4f(0.6f,0.8f,1.f,0.5f);glEnable(GL_BLEND);glBlendFunc(GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA);glBegin(GL_LINES);
    std::vector<std::vector<int>> pc(W); for(auto&pk:A.picos)pc[pk[0]].push_back(pk[1]);
    for(int c=vlo();c<vhi();++c){auto&v=pc[c];if(v.size()<2)continue;std::sort(v.begin(),v.end(),[](int a,int b){return a>b;});
        float fb0=(float)(H-1-v[0]); for(size_t i=1;i<v.size();++i){float fb=(float)(H-1-v[i]),ra=fb/(fb0+1e-3f);
            if(std::fabs(ra-std::round(ra))<0.08f&&ra>=1.8f){float t=tnorm(c),f0=fb0/(H-1),f1=fb/(H-1),e0=A.enh.at(v[0],c),e1=A.enh.at(v[i],c);
                float X0,Y0,Z0,X1,Y1,Z1;map_axes(t,f0,e0,X0,Y0,Z0);map_axes(t,f1,e1,X1,Y1,Z1);glVertex3f(X0,Y0,Z0);glVertex3f(X1,Y1,Z1);}}}
    glEnd();glDisable(GL_BLEND); draw_boxes_3d(); draw_playhead_3d(); draw_axes_labels(); }
// Vista 7: ondas 3D del sonido (cascada de formas de onda en el tiempo)
static void render_waves3d(){ setup_3d(); const auto&s=A.audio.samples; float C=A.crosslen,L=A.tlen;
    if(s.empty()){ draw_cube_frame(C,C,L); return; }
    int NSEG=max(20,min(160,A.res3d)); int SAMP=max(60,min(400,A.res3d*2));
    size_t Ns=(size_t)vlo()*A.P.hop, Ne=min(s.size(),(size_t)vhi()*A.P.hop); if(Ne<=Ns+1){Ns=0;Ne=s.size();}
    size_t WN=Ne-Ns;
    for(int sg=0;sg<NSEG;++sg){ float z=((float)sg/(NSEG-1)*2-1)*L;
        size_t i0=Ns+(size_t)sg*WN/NSEG,i1=Ns+(size_t)(sg+1)*WN/NSEG; if(i1<=i0)i1=i0+1;
        glBegin(GL_LINE_STRIP);
        for(int k=0;k<SAMP;++k){ float fx=(float)k/(SAMP-1); size_t idx=i0+(size_t)(fx*(i1-i0-1)); if(idx>=Ne)idx=Ne-1;
            float amp=s[idx],x=(fx*2-1)*C,y=max(-1.f,min(1.f,amp*3.0f))*C;
            float cr,cg,cb;colorf(min(1.f,std::fabs(amp)*3.0f),cr,cg,cb);glColor3f(cr,cg,cb); glVertex3f(x,y,z);} glEnd(); }
    int pc=playhead_col(); if(cin(pc)){ float z=(tnorm(pc)*2-1)*L;
        glEnable(GL_BLEND);glBlendFunc(GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA);glColor4f(1,1,0.2f,0.22f);
        glBegin(GL_QUADS);glVertex3f(-C,-C,z);glVertex3f(C,-C,z);glVertex3f(C,C,z);glVertex3f(-C,C,z);glEnd();glDisable(GL_BLEND);}
    glColor3f(0.30f,0.30f,0.34f); glLineWidth(1.f);   // marco: X,Y=C  Z(tiempo)=L
    float cc[8][3]={{-C,-C,-L},{C,-C,-L},{C,C,-L},{-C,C,-L},{-C,-C,L},{C,-C,L},{C,C,L},{-C,C,L}};
    int ed[12][2]={{0,1},{1,2},{2,3},{3,0},{4,5},{5,6},{6,7},{7,4},{0,4},{1,5},{2,6},{3,7}};
    glBegin(GL_LINES); for(auto&e:ed){glVertex3fv(cc[e[0]]);glVertex3fv(cc[e[1]]);} glEnd();
    glColor3f(1,1,1);
    FONT.at3d(C+0.05f,-C,-L,"X=t.intra"); FONT.at3d(-C,C+0.08f,-L,"Y=amplitud"); FONT.at3d(-C,-C,L+0.08f,"Z=tiempo");
    glColor3f(0.85f,0.85f,0.9f); float ts[5]={0,.25f,.5f,.75f,1}; double dur=A.sr?(double)WN/A.sr:0, off=A.sr?(double)Ns/A.sr:0;
    for(float t:ts){ char b[32]; float v=t*2-1; snprintf(b,32,"%.2fs",off+t*dur); FONT.at3d(-C,-C-0.06f,v*L,b);
        snprintf(b,32,"%+.2f",v); FONT.at3d(-C-0.06f,v*C,-L,b); } }

// Vista 9: VOLUMEN como NUBE DE PUNTOS DENSA por cada poligono etiquetado.
// UN punto por (tiempo, frecuencia) en su valor REAL de dB (no toda la columna), con
// sub-muestreo del eje de FRECUENCIA (SUBF). Respeta los filtros LOCALES de la vista
// (barras volFLo/volFHi y volDbMin/volDbMax), que NO afectan a las demas vistas.
static void render_volume3d(){ setup_3d(); int W=A.enh.W,H=A.enh.H; if(W<2||H<2){draw_axes_labels();return;}
    glEnable(GL_BLEND);glBlendFunc(GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA); glPointSize(2.8f);
    const int SUBF=4;                       // sub-divisiones por fila -> mas puntos en frecuencia
    glBegin(GL_POINTS);
    for(const Det&d:A.dets){ if(d.kind!=KIND_POLY||d.px.size()<3)continue;
        float cr0,cg0,cb0; class_color(d.cls,cr0,cg0,cb0);
        int x0=max(0,d.x),x1=min(W-1,d.x+d.w),y0=max(0,d.y),y1=min(H-1,d.y+d.h);
        for(int c=x0;c<=x1;++c){ if(!cin(c))continue; float t=tnorm(c);
            for(int r=y0;r<=y1;++r){ if(!pt_in_poly(d.px,d.py,c+0.5f,r+0.5f))continue;
                for(int s=0;s<SUBF;++s){ float rr=r+(float)s/SUBF; int r2=min(H-1,r+1); float fr=rr-r;
                    float e=A.enh.at(r,c)*(1-fr)+A.enh.at(r2,c)*fr;   // dB interpolado entre filas
                    float f=(float)(H-1-rr)/(H-1);
                    if(f<A.volFLo||f>A.volFHi||e<A.volDbMin||e>A.volDbMax)continue;   // FILTROS LOCALES
                    float X,Y,Z; map_axes(t,f,e,X,Y,Z);              // punto SOLO en el dB real
                    float pr,pg,pb; colorf(e,pr,pg,pb);              // color = dB (magma) + tinte de clase
                    glColor4f(min(1.f,pr*0.7f+cr0*0.35f),min(1.f,pg*0.7f+cg0*0.35f),min(1.f,pb*0.7f+cb0*0.35f), min(1.f,0.5f+e*0.5f)); glVertex3f(X,Y,Z); } } } }
    glEnd(); glDisable(GL_BLEND);
    draw_boxes_3d(); draw_playhead_3d(); draw_axes_labels(); }


// manijas de edicion del Det seleccionado (poligono: vertices; bbox: 8 manijas)
static void draw_edit_handles(float X0,float Y0,float X1,float Y1,int cLo,int cHi){
    (void)X0;(void)Y0;(void)X1;(void)Y1;(void)cLo;(void)cHi;
    if(A.sel<0||A.sel>=(int)A.dets.size())return; const Det&d=A.dets[A.sel];
    auto P=[&](float c,float r){ float sx,sy; spec_to_main(c,r,sx,sy); glVertex2f(sx,sy); };  // respeta el zoom 2D
    glColor3f(1,1,0.3f); glPointSize(9.f); glBegin(GL_POINTS);
    if(d.kind==KIND_POLY&&d.px.size()>=3){ for(size_t k=0;k<d.px.size();++k) P((float)d.px[k],(float)d.py[k]); }
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
        if(b.key=='H'&&A.rio_completo)on=true; if(b.key=='I'&&A.show_hist)on=true;
        if(b.key=='G'&&A.quiver_completo)on=true;
        if(b.key=='S'&&A.tool==T_SELECT)on=true;
        if(b.key=='Y'&&A.tool==T_BBOX)on=true; if(b.key=='P'&&A.tool==T_POLY)on=true;
        if(b.key=='V'&&A.wand)on=true; if(b.key=='E'&&A.tool==T_EDIT)on=true;
        if(b.key=='X'&&A.tool==T_CUT)on=true; if(b.key=='O'&&A.hide_labels)on=true;
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
    const char* vn[]={"","Espectro2D","Terreno3D","Mandala","RioEspectral","Constelacion","NubePuntos","Ondas3D","Quiver3D","Volumen"};
    const char* tn[]={"Selec","BBox","Poligono","Editar","Cortar"};
    std::string s=std::string("IIAP SachaAcoustic | ")+A.fname+" | V"+std::to_string(A.view)+":"+vn[A.view]+
        "  etiq="+std::to_string(A.dets.size())+"  clase="+class_name(A.clase_activa)+"  tool="+tn[A.tool]+
        (A.tool==T_POLY?(A.wand?"(varita)":"(a mano)"):"")+
        "  play="+(A.solo_banda?"banda(t+f)":"toda-freq(t)")+(PLAYER.paused?"  [PAUSA]":"");
    if(A.view>=2){const int*p=PERM[A.axperm];s+=std::string("  ejes X=")+DIMN[p[0]]+" Y="+DIMN[p[1]]+" Z="+DIMN[p[2]]+"  res3d="+std::to_string(A.res3d);}
    if(A.spec.W>0){ double ny=A.sr*0.5,dr=A.P.dyn_range_db; char fb[120];
        snprintf(fb,120,"  filtro f=%.0f-%.0fHz dB=%.0f..%.0f",A.fLo*ny,A.fHi*ny,(A.dbMin-1)*dr,(A.dbMax-1)*dr); s+=fb; }
    glColor3f(1,1,1); FONT.at2d(6,14,s); }

// Histograma SOBRE el espectrograma 2D: derecha = E por frecuencia (en banda dB);
// abajo-izq = histograma de dB. Bandas del filtro resaltadas.
static void render_hist_overlay(){
    int W=A.enh.W,H=A.enh.H; if(W<1)return;
    float y0=main_y0(), y1=panel_y0(), mh=y1-y0;
    int NB=96; std::vector<float> dbh(NB,0.f), fprof(H,0.f); float maxd=1e-6f,maxf=1e-6f;
    for(int r=0;r<H;++r){ float acc=0;
        for(int c=0;c<W;++c){ float e=A.enh.at(r,c); int bi=(int)(e*NB); if(bi<0)bi=0; if(bi>=NB)bi=NB-1; dbh[bi]+=1.f;
            if(e>=A.dbMin&&e<=A.dbMax) acc+=e; }
        fprof[r]=acc; if(acc>maxf)maxf=acc; }
    for(int i=0;i<NB;++i) if(dbh[i]>maxd)maxd=dbh[i];
    glEnable(GL_BLEND);glBlendFunc(GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA);
    // panel derecho: histograma de FRECUENCIA (alineado al eje de frecuencia)
    float PW=140, rgt=A.cW-BARS_RIGHT, px0=rgt-PW;       // deja sitio a las barras de filtro
    glColor4f(0,0,0,0.5f);glBegin(GL_QUADS);glVertex2f(px0,y0);glVertex2f(rgt,y0);glVertex2f(rgt,y1);glVertex2f(px0,y1);glEnd();
    for(int r=0;r<H;++r){ float f=(float)(H-1-r)/(H-1),yy=y0+(float)r/H*mh,yh=mh/H+1.0f,Lp=fprof[r]/maxf*(PW-10);
        if(f>=A.fLo&&f<=A.fHi)glColor4f(0.2f,1.f,1.f,0.95f);else glColor4f(0.55f,0.55f,0.6f,0.7f);
        glBegin(GL_QUADS);glVertex2f(px0+5,yy);glVertex2f(px0+5+Lp,yy);glVertex2f(px0+5+Lp,yy+yh);glVertex2f(px0+5,yy+yh);glEnd(); }
    glColor3f(1,1,0.3f);glLineWidth(1.f);glBegin(GL_LINES);
    glVertex2f(px0,y0+(1-A.fLo)*mh);glVertex2f(rgt,y0+(1-A.fLo)*mh);
    glVertex2f(px0,y0+(1-A.fHi)*mh);glVertex2f(rgt,y0+(1-A.fHi)*mh);glEnd();
    double ny=A.sr*0.5,dr=A.P.dyn_range_db; char b[40];
    glColor3f(1,1,1);FONT.at2d(px0+5,y0+12,"E por frecuencia");
    snprintf(b,40,"%.0fHz",A.fHi*ny);FONT.at2d(px0+5,y0+(1-A.fHi)*mh-2,b);
    snprintf(b,40,"%.0fHz",A.fLo*ny);FONT.at2d(px0+5,y0+(1-A.fLo)*mh-2,b);
    // abajo-izquierda: histograma de dB
    float hx0=14,hx1=314,hy1=y1-16,hy0=y1-104;
    glColor4f(0,0,0,0.5f);glBegin(GL_QUADS);glVertex2f(hx0-5,hy0-16);glVertex2f(hx1+5,hy0-16);glVertex2f(hx1+5,hy1+6);glVertex2f(hx0-5,hy1+6);glEnd();
    for(int i=0;i<NB;++i){ float e=(i+0.5f)/NB,x=hx0+(float)i/NB*(hx1-hx0),bw=(hx1-hx0)/NB+1,hh=dbh[i]/maxd*(hy1-hy0);
        if(e>=A.dbMin&&e<=A.dbMax)glColor4f(1.f,0.6f,0.2f,0.95f);else glColor4f(0.5f,0.5f,0.55f,0.7f);
        glBegin(GL_QUADS);glVertex2f(x,hy1);glVertex2f(x+bw,hy1);glVertex2f(x+bw,hy1-hh);glVertex2f(x,hy1-hh);glEnd(); }
    glColor3f(1,1,0.3f);glBegin(GL_LINES);
    glVertex2f(hx0+A.dbMin*(hx1-hx0),hy0);glVertex2f(hx0+A.dbMin*(hx1-hx0),hy1);
    glVertex2f(hx0+A.dbMax*(hx1-hx0),hy0);glVertex2f(hx0+A.dbMax*(hx1-hx0),hy1);glEnd();
    glColor3f(1,1,1);FONT.at2d(hx0,hy0-4,"Histograma dB (banda=filtro)");
    snprintf(b,40,"%.0f",(A.dbMin-1)*dr);FONT.at2d(hx0+A.dbMin*(hx1-hx0)-8,hy1+12,b);
    snprintf(b,40,"%.0fdB",(A.dbMax-1)*dr);FONT.at2d(hx0+A.dbMax*(hx1-hx0)-8,hy1+12,b);
    glDisable(GL_BLEND);
}
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

// leyenda/tooltip de la vista Volumen (detalle de lo que se muestra y los ejes)
static void render_volume_legend(){ ortho2d(); const int*p=PERM[A.axperm];
    double ny=A.sr*0.5,dr=A.P.dyn_range_db; double dur=A.sr?(double)A.audio.samples.size()/A.sr:0;
    int npoly=0; for(auto&d:A.dets) if(d.kind==KIND_POLY)++npoly;
    float x=8,y=main_y0()+6,w=380,lh=15,n=8,hh=n*lh+12; char b[110];
    glEnable(GL_BLEND);glBlendFunc(GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA);glColor4f(0,0,0,0.72f);
    glBegin(GL_QUADS);glVertex2f(x,y);glVertex2f(x+w,y);glVertex2f(x+w,y+hh);glVertex2f(x,y+hh);glEnd();
    glColor3f(0.5f,0.7f,1.f);glLineWidth(1.f);glBegin(GL_LINE_LOOP);glVertex2f(x,y);glVertex2f(x+w,y);glVertex2f(x+w,y+hh);glVertex2f(x,y+hh);glEnd();
    glDisable(GL_BLEND); glColor3f(1,1,1); float ty=y+14;
    FONT.at2d(x+8,ty,"VOLUMEN 3D - nube de puntos por etiqueta"); ty+=lh;
    FONT.at2d(x+8,ty,"Cada punto = dB REAL en (tiempo, frecuencia)"); ty+=lh;
    FONT.at2d(x+8,ty,"dentro de la etiqueta, segun los filtros locales."); ty+=lh;
    glColor3f(0.85f,0.85f,0.9f);
    snprintf(b,110,"Ejes: X=%s  Y=%s  Z=%s   color=dB",DIMN[p[0]],DIMN[p[1]],DIMN[p[2]]); FONT.at2d(x+8,ty,b); ty+=lh;
    snprintf(b,110,"Rango: t 0-%.2fs  freq 0-%.0fHz  dB -%.0f..0",dur,ny,dr); FONT.at2d(x+8,ty,b); ty+=lh;
    glColor3f(0.6f,0.9f,1.f); FONT.at2d(x+8,ty,"Barras der. (SOLO esta vista): Frecuencia, dB, Vol"); ty+=lh;
    glColor3f(0.85f,0.85f,0.9f); snprintf(b,110,"Etiquetas poligono mostradas: %d",npoly); FONT.at2d(x+8,ty,b); ty+=lh;
    glColor3f(0.7f,0.8f,1.f); FONT.at2d(x+8,ty,"arrastrar=rotar  rueda=zoom  [ ]=resolucion"); }

// ejes dB Y FRECUENCIA de las vistas 3D = banda del FILTRO (barras) + MARGEN. Solo afecta
// al DIBUJO 3D (map_axes); NO toca el sonido. En la vista Volumen usa el filtro local.
static void update_db_axis(){ if(A.spec.W<1){A.dbAxisLo=0;A.dbAxisHi=1;A.fAxisLo=0;A.fAxisHi=1;return;}
    float dlo=(A.view==9)?A.volDbMin:A.dbMin, dhi=(A.view==9)?A.volDbMax:A.dbMax;
    float dm=5.0f/(float)A.P.dyn_range_db;                        // margen dB = 5 dB
    A.dbAxisLo=max(0.f, dlo-dm); A.dbAxisHi=min(1.f, dhi+dm);
    if(A.dbAxisHi<A.dbAxisLo+0.06f)A.dbAxisHi=min(1.f,A.dbAxisLo+0.06f);
    float flo=(A.view==9)?A.volFLo:A.fLo, fhi=(A.view==9)?A.volFHi:A.fHi;
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
            if((A.tool==T_SELECT||A.tool==T_EDIT)&&A.selDet>=0&&A.selDet<(int)A.dets.size()){   // vertices marcados (Selec/Editar): resaltados en rojo
                const Det&d=A.dets[A.selDet]; if(d.kind==KIND_POLY){ glColor3f(1,0.3f,0.3f); glPointSize(11.f); glBegin(GL_POINTS);
                    if(A.selVert>=0&&A.selVert<(int)d.px.size()){ float sx,sy; spec_to_main((float)d.px[A.selVert],(float)d.py[A.selVert],sx,sy); glVertex2f(sx,sy); }
                    for(int k:A.selVerts){ if(k>=0&&k<(int)d.px.size()){ float sx,sy; spec_to_main((float)d.px[k],(float)d.py[k],sx,sy); glVertex2f(sx,sy); } }
                    glEnd(); glPointSize(1.f); } }
            if(A.rbDrag){ glColor3f(1,0.5f,0.3f); glLineWidth(1.f); glBegin(GL_LINE_LOOP);   // rectangulo de seleccion de vertices (clic-der)
                glVertex2f(A.rbx0,A.rby0);glVertex2f(A.rbx1,A.rby0);glVertex2f(A.rbx1,A.rby1);glVertex2f(A.rbx0,A.rby1);glEnd(); }
            if(A.show_hist) render_hist_overlay();                          // histograma sobre el espectro
            render_axes2d();                                                // ejes 2D (solo vista 2D)
            if(A.dragging&&A.dragRegion==1){glColor3f(1,1,1);glLineWidth(1.f);glBegin(GL_LINE_LOOP);glVertex2f(A.dx0,A.dy0);glVertex2f(A.dx1,A.dy0);glVertex2f(A.dx1,A.dy1);glVertex2f(A.dx0,A.dy1);glEnd();}
            if(A.tool==T_CUT&&A.cutX.size()>=1){ glColor3f(1,0.35f,0.35f);glLineWidth(2.5f);  // trazo de corte libre
                glBegin(GL_LINE_STRIP);for(size_t k=0;k<A.cutX.size();++k){float sx,sy;spec_to_main((float)A.cutX[k],(float)A.cutY[k],sx,sy);glVertex2f(sx,sy);}glEnd(); } }
        else if(A.view==2) render_terrain3d();
        else if(A.view==3) render_mandala();
        else if(A.view==4) render_rios3d();
        else if(A.view==5) render_const3d();
        else if(A.view==6) render_pointcloud();
        else if(A.view==7) render_waves3d();
        else if(A.view==8) render_quiver3d();
        else if(A.view==9){ render_volume3d(); render_volume_legend(); }
        if(A.view>=2&&A.view<=8) render_colorbar3d();                        // leyenda Nivel(dB) en 3D (no en V9: tiene leyenda)
        render_panel();
        render_filter_bars();                                               // barras de filtro en TODAS las vistas (V9 = filtros locales)
        if(A.dragging&&(A.dragRegion==2||A.dragRegion==3)){ortho2d();glColor3f(1,1,1);glLineWidth(1.f);glBegin(GL_LINE_LOOP);glVertex2f(A.dx0,A.dy0);glVertex2f(A.dx1,A.dy0);glVertex2f(A.dx1,A.dy1);glVertex2f(A.dx0,A.dy1);glEnd();}
    } else { ortho2d(); glColor3f(1,1,1); FONT.at2d(20,A.cH/2,"Pulsa 'Abrir' (o tecla o) para cargar un WAV..."); }
    render_toolbar(); render_hud(); render_tooltip(); render_naming(); render_buffer_modal(); }

// ---------------- acciones ----------
static int caja_en(int c,int r){ for(int i=(int)A.dets.size()-1;i>=0;--i){const Det&d=A.dets[i]; if(c>=d.x&&c<d.x+d.w&&r>=d.y&&r<d.y+d.h)return i;} return -1; }
// borra las etiquetas cuyo CENTRO cae dentro de la caja de seleccion (tiempo + frecuencia
// si la hay). Devuelve cuantas borro. Si no hay seleccion, borra la etiqueta seleccionada.
static int delete_in_selection(){
    if(A.sc0>=0&&A.sc1>A.sc0){ int c0=min(A.sc0,A.sc1),c1=max(A.sc0,A.sc1);
        bool hasR=(A.sr0>=0&&A.sr1>=0); int r0=hasR?min(A.sr0,A.sr1):0, r1=hasR?max(A.sr0,A.sr1):A.spec.H;
        int n=0;
        for(int i=(int)A.dets.size()-1;i>=0;--i){ const Det&d=A.dets[i];
            int cx=d.x+d.w/2, cy=d.y+d.h/2;                       // centro de la etiqueta
            if(cx>=c0&&cx<=c1&&cy>=r0&&cy<=r1){ A.dets.erase(A.dets.begin()+i); ++n; } }
        if(n){ A.sel=-1; A.dirty=true; } return n; }
    if(A.sel>=0&&A.sel<(int)A.dets.size()){ A.dets.erase(A.dets.begin()+A.sel); A.sel=-1; A.dirty=true; return 1; }
    return 0; }
// borra los vertices MARCADOS del poligono A.selDet: el conjunto A.selVerts (rectangulo
// con clic-der) o, si esta vacio, el unico A.selVert. Si quedaria con <3 vertices, borra la
// etiqueta entera. Devuelve true si actuo.
static bool delete_marked_vertex(){ if(A.selDet<0||A.selDet>=(int)A.dets.size())return false;
    Det&d=A.dets[A.selDet]; if(d.kind!=KIND_POLY)return false;
    std::vector<int> vs=A.selVerts; if(vs.empty()&&A.selVert>=0)vs.push_back(A.selVert);
    if(vs.empty())return false;
    std::sort(vs.begin(),vs.end()); vs.erase(std::unique(vs.begin(),vs.end()),vs.end());
    if((int)d.px.size()-(int)vs.size()<3){ A.dets.erase(A.dets.begin()+A.selDet); }   // quedaria <3 -> borra etiqueta
    else { for(int i=(int)vs.size()-1;i>=0;--i){ int k=vs[i]; if(k>=0&&k<(int)d.px.size()){ d.px.erase(d.px.begin()+k); d.py.erase(d.py.begin()+k); } } sync_bbox_from_poly(d); }
    A.selDet=A.selVert=-1; A.selVerts.clear(); A.dirty=true; return true; }
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
static unsigned long long labels_sig(){ unsigned long long s=1469598103934665603ULL;
    auto mix=[&](long long v){ s=(s^(unsigned long long)v)*1099511628211ULL; };
    mix((long long)A.dets.size()); mix((long long)A.classes.size());
    for(const Det&d:A.dets){ mix(d.x);mix(d.y);mix(d.w);mix(d.h);mix(d.cls);mix(d.kind);mix((long long)d.px.size());
        for(size_t k=0;k<d.px.size();++k){ mix(d.px[k]); mix(d.py[k]); } }
    return s; }
// autosave: si la firma cambio y no se esta arrastrando/escribiendo, guarda en silencio
static void autosave_tick(){ static int t=0; if(++t<100)return; t=0;   // ~cada 1 s
    if(A.spec.W<1||A.fname=="(sin archivo)"||A.dragging||A.naming||A.buffering)return;
    if(!A.sigInit)return;                                              // base se fija al cargar
    unsigned long long sg=labels_sig();
    if(sg!=A.lastSig){ guardar(true); A.lastSig=sg; } }
static void cargar_raven(){ char f[MAX_PATH]=""; OPENFILENAMEA o{}; o.lStructSize=sizeof(o);
    o.lpstrFilter="Raven selection table\0*.txt\0Todos\0*.*\0"; o.lpstrFile=f; o.nMaxFile=MAX_PATH;
    o.Flags=OFN_FILEMUSTEXIST|OFN_PATHMUSTEXIST; if(!GetOpenFileNameA(&o))return; if(A.spec.W<1)return;
    show_busy("Cargando etiquetas Raven...");
    RavenGeom g{A.spec.W,A.spec.H,A.sr,A.P.hop};
    std::vector<Det> in=import_raven(f,g,A.classes); for(auto&d:in)A.dets.push_back(d);
    A.sel=-1; layout_botones(); std::cout<<"Cargadas "<<in.size()<<" etiquetas Raven de "<<f<<"\n"; }
static void crear_caja(int cls){ if(A.sc0<0||A.sc1<=A.sc0)return; Det d; d.x=A.sc0; d.w=A.sc1-A.sc0;
    if(A.sr0>=0&&A.sr1>=0){d.y=min(A.sr0,A.sr1);d.h=max(1,std::abs(A.sr1-A.sr0));}else{d.y=0;d.h=A.spec.H;}
    d.px={d.x,d.x+d.w,d.x+d.w,d.x}; d.py={d.y,d.y,d.y+d.h,d.y+d.h}; d.cls=cls;
    A.dets.push_back(d); A.sel=(int)A.dets.size()-1; }
// Fijar valor de senal: el humano baja el TECHO de dB escuchando hasta que el
// sonido se vuelve ruido; al pulsar, ese techo (umbral de senal) pasa a ser el
// PISO y el techo se abre al maximo -> el filtro muestra de ese valor a 0 dB.
static void set_signal_value(){ if(A.spec.W<1)return; float thr=A.dbMax;
    A.dbMin=max(0.f,min(0.98f,thr)); A.dbMax=1.0f; upload_texture(); A.refilterPending=true;
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
    else if(c>='1'&&c<='9'){ A.view=c-'0'; layout_botones(); }   // relayout: botones segun la vista
    else if(c=='G')A.quiver_completo=!A.quiver_completo;          // Quiver: glifos completos
    else if(c=='B')A.solo_banda=!A.solo_banda;
    else if(c=='S'){A.tool=T_SELECT;A.polyX.clear();A.polyY.clear();A.cutX.clear();A.cutY.clear();A.selDet=A.selVert=-1;A.selVerts.clear();}
    else if(c=='Y'){A.tool=T_BBOX;A.shape_poly=false;A.polyX.clear();A.polyY.clear();A.cutX.clear();A.cutY.clear();A.selDet=A.selVert=-1;A.selVerts.clear();}   // etiquetar bounding box
    else if(c=='P'){A.tool=T_POLY;A.shape_poly=true;A.cutX.clear();A.cutY.clear();A.selDet=A.selVert=-1;A.selVerts.clear();}                 // etiquetar poligono
    else if(c=='V')A.wand=!A.wand;                    // varita magica ON/OFF (modo poligono)
    else if(c=='E'){A.tool=T_EDIT;A.polyX.clear();A.polyY.clear();A.cutX.clear();A.cutY.clear();A.selDet=A.selVert=-1;A.selVerts.clear();}   // editar forma
    else if(c=='X'){A.tool=T_CUT;A.polyX.clear();A.polyY.clear();A.cutX.clear();A.cutY.clear();A.selDet=A.selVert=-1;A.selVerts.clear();}    // corte libre
    else if(c=='N'){ A.naming=true; A.nameBuf.clear(); }  // crear etiqueta (captura de texto)
    else if(c=='R')cargar_raven();                    // cargar Raven
    else if(c=='H')A.rio_completo=!A.rio_completo;   // rio: hilos completos vs corte por vertice
    else if(c=='I')A.show_hist=!A.show_hist;          // histograma sobre el espectrograma
    else if(c=='O')A.hide_labels=!A.hide_labels;      // ocultar / mostrar etiquetas
    else if(c=='T'){A.vc0=0;A.vc1=A.spec.W;A.vr0=0;A.vr1=A.spec.H;}   // ver todo (tiempo + frecuencia)
    else if(c=='Z')zoom2d(0.7f,(vlo()+vhi())/2,(rlo()+rhi())/2);       // zoom + (centro)
    else if(c=='U')zoom2d(1.4f,(vlo()+vhi())/2,(rlo()+rhi())/2);       // zoom -
    else if(c==']'||c=='+'||c=='=')A.res3d=min(600,A.res3d+40);
    else if(c=='['||c=='-'||c=='_')A.res3d=max(48,A.res3d-40);
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
        upload_texture(); A.refilterPending=true; }           // re-filtra el sonido en vivo
    else if(c=='o')open_dialog();
    else if(c==' '){ if(PLAYER.playing&&PLAYER.cur_sample()>=0)PLAYER.pause_toggle(); else play_window(); }  // espacio = play/pausa
    else if(c=='p')play_filtered(0,A.spec.W,false,0,1);   // todo, con filtros aplicados
    else if(c=='k')PLAYER.pause_toggle();
    else if(c=='.')PLAYER.stop();
    else if(c=='r'&&A.view>=2)A.axperm=(A.axperm+1)%2;  // solo intercambia freq/dB (X=tiempo fijo)
    else if(c=='a'&&A.spec.W>0)auto_segment();
    else if(c=='l'&&A.sc0>=0&&A.sc1>A.sc0){ Det d; d.x=A.sc0;d.w=A.sc1-A.sc0;
        if(A.sr0>=0&&A.sr1>=0){d.y=min(A.sr0,A.sr1);d.h=std::abs(A.sr1-A.sr0);}else{d.y=0;d.h=A.spec.H;}
        d.px={d.x,d.x+d.w,d.x+d.w,d.x};d.py={d.y,d.y,d.y+d.h,d.y+d.h};d.cls=A.clase_activa;A.dets.push_back(d);}
    else if(c=='t')A.modo_hilo=!A.modo_hilo;
    else if(c=='b')A.clase_activa=0;                              // bio
    else if(c=='n'&&A.classes.size()>1)A.clase_activa=1;          // antro
    else if(c=='z'&&A.sel>=0)A.dets[A.sel].cls=A.clase_activa;    // asigna la clase activa al seleccionado
    else if(c=='d'&&A.sel>=0){A.dets.erase(A.dets.begin()+A.sel);A.sel=-1;}
    else if(c=='c'){A.dets.clear();A.hilos.clear();A.sel=-1;A.polyX.clear();A.polyY.clear();A.cutX.clear();A.cutY.clear();A.selDet=A.selVert=-1;A.selVerts.clear();}
    else if(c=='s')guardar();
}
static void ayuda(){ std::cout<<"\n=== raven.exe ===\n Botones arriba o teclas: o abrir, 1-5 vistas, ESPACIO play sel, p todo, k pausa, . stop,\n"
    " a auto, l caja, t hilo, b/n clase, z/x set sel, d borra, c limpia, s guarda, r ejes(3D), q salir.\n"
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
static int fbar_pick(int mx,int my){ if(A.spec.W<1)return 0;   // barras en todas las vistas (V9 usa filtros locales)
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
    if(A.view!=9) upload_texture();              // V9: filtro local, no reconstruye la textura 2D
    if(code>=8&&code<=11)A.refilterPending=true; }  // dB/freq cambiaron -> re-filtrar el sonido en vivo (incl. V9)

// posicion en pantalla (vista 2D) de una coord de espectro (col,row)
static void spec_to_main(float c,float r,float&sx,float&sy){
    sx=plotX0()+(float)((c-vlo())/(double)max(1,vhi()-vlo())*(plotX1()-plotX0()));
    sy=plotY0()+(float)((r-rlo())/(double)max(1,rhi()-rlo())*(plotY1()-plotY0())); }
// edicion: detecta manija del Det seleccionado. Devuelve: poligono -> indice de vertice;
// bbox -> 100+manija (0..3 esquinas, 4..7 medios, 8 mover); -1 nada.
static int edit_pick(int mx,int my){ if(A.sel<0||A.sel>=(int)A.dets.size())return -1; const Det&d=A.dets[A.sel];
    auto hit=[&](float c,float r){ float sx,sy; spec_to_main(c,r,sx,sy); return (mx-sx)*(mx-sx)+(my-sy)*(my-sy)<=81; };
    if(d.kind==KIND_POLY&&d.px.size()>=3){ for(size_t k=0;k<d.px.size();++k) if(hit((float)d.px[k],(float)d.py[k]))return (int)k; }
    else { float cx[8]={(float)d.x,(float)(d.x+d.w),(float)(d.x+d.w),(float)d.x, d.x+d.w*0.5f,d.x+d.w*0.5f,(float)d.x,(float)(d.x+d.w)};
        float cy[8]={(float)d.y,(float)d.y,(float)(d.y+d.h),(float)(d.y+d.h), (float)d.y,(float)(d.y+d.h),d.y+d.h*0.5f,d.y+d.h*0.5f};
        for(int k=0;k<8;++k) if(hit(cx[k],cy[k]))return 100+k; }
    int c,r; main_to_spec(mx,my,c,r);
    bool inside = (d.kind==KIND_POLY&&d.px.size()>=3) ? pt_in_poly(d.px,d.py,c+0.5f,r+0.5f)
                                                      : (c>=d.x&&c<d.x+d.w&&r>=d.y&&r<d.y+d.h);
    if(inside) return 108;   // mover (solo si el clic cae DENTRO de la forma, no en su bbox vacio)
    return -1; }
// EDITAR: indice de la ARISTA del poligono seleccionado bajo el cursor (insertar despues de
// ese indice), o -1. Usa distancia punto-segmento en pantalla (umbral ~6px).
static int edge_pick(int mx,int my){ if(A.sel<0||A.sel>=(int)A.dets.size())return -1; const Det&d=A.dets[A.sel];
    if(d.kind!=KIND_POLY||d.px.size()<3)return -1; int n=(int)d.px.size();
    int ei=-1; double be=6.0*6.0;
    for(int k=0;k<n;++k){ int j=(k+1)%n; float ax,ay,bx,by; spec_to_main((float)d.px[k],(float)d.py[k],ax,ay); spec_to_main((float)d.px[j],(float)d.py[j],bx,by);
        double vx=bx-ax,vy=by-ay,L2=vx*vx+vy*vy; double tt=L2>0?((mx-ax)*vx+(my-ay)*vy)/L2:0; tt=tt<0?0:(tt>1?1:tt);
        double px=ax+tt*vx,py=ay+tt*vy,dd=(mx-px)*(mx-px)+(my-py)*(my-py); if(dd<be){be=dd;ei=k;} }
    return ei; }
// herramienta SELEC: busca el vertice de poligono mas cercano al raton (de CUALQUIER
// etiqueta) dentro de un radio; guarda en A.selDet/A.selVert. Devuelve true si encontro.
static bool pick_vertex(int mx,int my){ A.selDet=-1; A.selVert=-1; double best=100; // radio^2 = 10px
    for(int i=0;i<(int)A.dets.size();++i){ const Det&d=A.dets[i]; if(d.kind!=KIND_POLY||d.px.size()<3)continue;
        for(size_t k=0;k<d.px.size();++k){ float sx,sy; spec_to_main((float)d.px[k],(float)d.py[k],sx,sy);
            double dd=(mx-sx)*(mx-sx)+(my-sy)*(my-sy); if(dd<best){best=dd;A.selDet=i;A.selVert=(int)k;} } }
    return A.selDet>=0; }
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
static void split_free(int idx){ if(idx<0||idx>=(int)A.dets.size()||A.cutX.size()<2)return;
    Det d=A.dets[idx]; int W=A.spec.W,H=A.spec.H;
    int x0=max(0,d.x-1),y0=max(0,d.y-1),x1=min(W-1,d.x+d.w+1),y1=min(H-1,d.y+d.h+1);
    int w=x1-x0+1,h=y1-y0+1; if(w<3||h<3)return;
    std::vector<uint8_t> m((size_t)w*h,0);
    for(int r=0;r<h;++r)for(int c=0;c<w;++c){ float gx=x0+c+0.5f,gy=y0+r+0.5f;
        bool in=(d.kind==KIND_POLY&&d.px.size()>=3)?pt_in_poly(d.px,d.py,gx,gy):(gx>=d.x&&gx<d.x+d.w&&gy>=d.y&&gy<d.y+d.h);
        if(in)m[(size_t)r*w+c]=1; }
    int R=2; auto erase=[&](int gx,int gy){ for(int dy=-R;dy<=R;++dy)for(int dx=-R;dx<=R;++dx){ int cx=gx-x0+dx,cy=gy-y0+dy;
        if(cx>=0&&cx<w&&cy>=0&&cy<h)m[(size_t)cy*w+cx]=0; } };
    for(size_t k=0;k+1<A.cutX.size();++k){ int ax=A.cutX[k],ay=A.cutY[k],bx=A.cutX[k+1],by=A.cutY[k+1];
        int steps=max(std::abs(bx-ax),std::abs(by-ay))+1; for(int s=0;s<=steps;++s){ float tt=(float)s/steps; erase((int)(ax+(bx-ax)*tt),(int)(ay+(by-ay)*tt)); } }
    std::vector<int> lab((size_t)w*h,0); int next=0; const int dx8[8]={1,1,0,-1,-1,-1,0,1},dy8[8]={0,1,1,1,0,-1,-1,-1};
    std::vector<Det> pieces;
    for(int r=0;r<h;++r)for(int c=0;c<w;++c){ if(!m[(size_t)r*w+c]||lab[(size_t)r*w+c])continue; ++next;
        std::vector<std::pair<int,int>> st; st.push_back({c,r}); lab[(size_t)r*w+c]=next; int area=0,sx=c,sy=r;
        while(!st.empty()){ auto[cx,cy]=st.back();st.pop_back();++area;
            for(int k=0;k<8;++k){int nx=cx+dx8[k],ny=cy+dy8[k]; if(nx<0||nx>=w||ny<0||ny>=h)continue; size_t ni=(size_t)ny*w+nx; if(m[ni]&&!lab[ni]){lab[ni]=next;st.push_back({nx,ny});}}}
        if(area<8)continue;
        std::vector<int> bxv,byv; trace_boundary(lab,w,h,next,sx,sy,bxv,byv);
        Det p; p.kind=KIND_POLY; p.cls=d.cls;
        for(size_t i=0;i<bxv.size();i+=3){ p.px.push_back(bxv[i]+x0); p.py.push_back(byv[i]+y0); }
        if(p.px.size()<3)continue; sync_bbox_from_poly(p); pieces.push_back(std::move(p)); }
    if(pieces.size()<2)return;                          // no se separo: no hacer nada
    A.dets.erase(A.dets.begin()+idx);
    for(size_t k=0;k<pieces.size();++k)A.dets.insert(A.dets.begin()+idx+k,pieces[k]);
    A.sel=idx; }

static LRESULT CALLBACK WndProc(HWND h,UINT m,WPARAM wp,LPARAM lp){
    switch(m){
    case WM_SIZE: A.cW=LOWORD(lp); A.cH=HIWORD(lp); layout_botones(); return 0;
    case WM_SETCURSOR: if(LOWORD(lp)==HTCLIENT){ POINT p; GetCursorPos(&p); ScreenToClient(h,&p); A.mx=p.x;A.my=p.y; SetCursor(pick_cursor()); return TRUE; } break;
    case WM_LBUTTONDOWN:{ int mx=LOWORD(lp),my=HIWORD(lp);
        if(A.buffering){ float tx0,tx1,ty; bufslider(tx0,tx1,ty);                 // modal de buffer: arrastra la barra
            if(mx>=tx0-8&&mx<=tx1+8&&my>=ty-14&&my<=ty+14){ A.dragging=true; A.dragRegion=22; buffer_apply(mx); } return 0; }
        int bi=hit_boton(mx,my); if(bi>=0){ do_action(A.botones[bi].key); return 0; }
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
            else if(A.tool==T_POLY){
                if(A.wand){ A.dragging=false;A.dragRegion=0; magic_wand(c,r); } // varita ON
                else { A.polyX.push_back(c); A.polyY.push_back(r); A.dragging=false; A.dragRegion=0; } }  // a mano: agrega vertice
            else if(A.tool==T_EDIT){
                int hd=(A.sel>=0)?edit_pick(mx,my):-1;            // solo si HAY etiqueta seleccionada
                int ei=(hd<0&&A.sel>=0)?edge_pick(mx,my):-1;
                if(hd>=0){ A.dragRegion=21;A.editVert=hd;A.lastEditC=c;A.lastEditR=r;       // vertice/manija -> arrastra
                    if(A.dets[A.sel].kind==KIND_POLY&&hd<(int)A.dets[A.sel].px.size()){A.selDet=A.sel;A.selVert=hd;} else {A.selDet=A.selVert=-1;} }
                else if(ei>=0){ Det&d=A.dets[A.sel];                                        // clic izq SOLO SOBRE LA LINEA -> agrega punto y lo arrastra
                    d.px.insert(d.px.begin()+ei+1,c); d.py.insert(d.py.begin()+ei+1,r); sync_bbox_from_poly(d); A.dirty=true;
                    A.dragRegion=21;A.editVert=ei+1;A.lastEditC=c;A.lastEditR=r;A.selDet=A.sel;A.selVert=ei+1; A.selVerts.clear(); }
                else { A.sel=caja_en(c,r); A.selDet=A.selVert=-1; A.selVerts.clear(); A.dragging=false;A.dragRegion=0; } }  // dentro/fuera: solo SELECCIONA la etiqueta (NO agrega punto)
            else if(A.tool==T_CUT){ A.dragRegion=7; A.cutX.push_back(c); A.cutY.push_back(r); }  // pinta el trazo de corte
            else { if(pick_vertex(mx,my)){ A.dragging=false; A.dragRegion=0; }   // T_SELEC: clic sobre vertice -> lo marca (Supr lo borra)
                   else { A.selDet=A.selVert=-1; A.dragRegion=1; } }             // si no, arrastra para seleccionar
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
        else if(A.dragRegion==22){ buffer_apply(mx); }                            // modal de buffer
        else if(A.dragRegion==7){ int c,r; main_to_spec(mx,my,c,r); if(A.cutX.empty()||A.cutX.back()!=c||A.cutY.back()!=r){A.cutX.push_back(c);A.cutY.push_back(r);} }  // pinta trazo de corte
        else if(A.dragRegion>=8&&A.dragRegion<=12){ fbar_drag(A.dragRegion,my); }   // barras de filtro / ganancia
        else if(A.dragRegion==21&&A.sel>=0&&A.sel<(int)A.dets.size()){ int c,r; main_to_spec(mx,my,c,r); Det&d=A.dets[A.sel];  // editar forma
            if(d.kind==KIND_POLY&&d.px.size()>=3){                     // POLIGONO: NO rectangularizar
                if(A.editVert>=0&&A.editVert<(int)d.px.size()){ d.px[A.editVert]=c; d.py[A.editVert]=r; }   // mover un vertice
                else { int dc=c-A.lastEditC, dr=r-A.lastEditR; for(size_t k=0;k<d.px.size();++k){ d.px[k]+=dc; d.py[k]+=dr; } }  // mover TODO (trasladar)
                sync_bbox_from_poly(d); }
            else if(A.editVert>=100)apply_bbox_handle(d,A.editVert-100,c,r);   // BBOX: manijas
            A.lastEditC=c; A.lastEditR=r; A.dirty=true; }
        else if(A.dragRegion==4){A.yaw+=(mx-A.dx1)*0.4f;A.pitch+=(my-A.dy1)*0.4f;A.pitch=max(-89.f,min(89.f,A.pitch));}
        A.dx1=mx;A.dy1=my; } return 0; }
    case WM_LBUTTONUP:{ A.dragging=false; if(A.spec.W==0){A.navMode=0;return 0;} int mx=A.dx1,my=A.dy1;
        bool click=(std::abs(A.dx1-A.dx0)<4&&std::abs(A.dy1-A.dy0)<4);
        if(A.navMode||A.dragRegion==5||A.dragRegion==6){ A.navMode=0; A.scaleAxis=-1; return 0; }  // ventana/divisor/manija eje
        if(A.dragRegion==22){A.dragRegion=0;return 0;}                        // modal de buffer
        if(A.dragRegion>=8&&A.dragRegion<=12){A.dragRegion=0;return 0;}        // barras de filtro / ganancia
        if(A.dragRegion==21){A.dragRegion=0;A.editVert=-1;return 0;}           // editar
        if(A.dragRegion==7){ A.dragRegion=0; return 0; }   // corte libre: se sigue pintando; clic-der ejecuta
        if(A.dragRegion==1||A.dragRegion==2){ int c0,r0,c1,r1;                 // SELECCION (etiquetar)
            if(A.dragRegion==1){main_to_spec(A.dx0,A.dy0,c0,r0);main_to_spec(mx,my,c1,r1);}
            else{strip_to_spec(A.dx0,A.dy0,c0,r0);strip_to_spec(mx,my,c1,r1);}
            if(A.tool==T_BBOX&&A.dragRegion==1&&!click){                       // BBOX: arrastrar crea la caja directamente
                Det d; d.kind=KIND_BBOX; d.cls=A.clase_activa; d.x=min(c0,c1);d.y=min(r0,r1);
                d.w=max(1,std::abs(c1-c0));d.h=max(1,std::abs(r1-r0));
                d.px={d.x,d.x+d.w,d.x+d.w,d.x};d.py={d.y,d.y,d.y+d.h,d.y+d.h};
                A.dets.push_back(d); A.sel=(int)A.dets.size()-1; }
            else if(click){ if(A.modo_hilo){A.hilos.push_back(track_hilo(c0,r0)); if(!A.hilos.back().col.empty()){A.sc0=A.hilos.back().col.front();A.sc1=A.hilos.back().col.back();A.sr0=A.sr1=-1;}}
                       else { A.sel=caja_en(c0,r0); A.cursor_col=c0; } }
            else { A.sc0=min(c0,c1);A.sc1=max(c0,c1); A.sr0=min(r0,r1);A.sr1=max(r0,r1); } }
        else if(A.dragRegion==3){ int c0=(int)((double)A.dx0/A.cW*A.spec.W),c1=(int)((double)mx/A.cW*A.spec.W);
            if(click)A.cursor_col=max(0,min(A.spec.W-1,c0)); else {A.sc0=min(c0,c1);A.sc1=max(c0,c1);A.sr0=A.sr1=-1;} }
        return 0; }
    case WM_RBUTTONDOWN:{ if(A.spec.W==0)return 0; int mx=LOWORD(lp),my=HIWORD(lp); int col,row;
        bool inStrip=(my>=panel_y0()&&my<panel_y0()+STRIP_H);
        if(inStrip)strip_to_spec(mx,my,col,row); else main_to_spec(mx,my,col,row);
        if(A.tool==T_POLY&&!A.wand&&!A.polyX.empty()){ finish_poly(); return 0; }  // poligono a mano: clic der CIERRA
        if(A.tool==T_CUT&&A.cutX.size()>=2){ int idx=caja_en(A.cutX.front(),A.cutY.front()); if(idx<0)idx=caja_en(col,row); if(idx<0)idx=A.sel;
            show_busy("Cortando etiqueta..."); split_free(idx); A.cutX.clear();A.cutY.clear(); A.dirty=true; return 0; }  // corte libre
        // EDITAR: arrastrar con BOTON DERECHO un rectangulo -> marca los vertices dentro de
        // cualquier poligono (se borran con Supr). Agregar punto = clic IZQUIERDO sobre la linea.
        if(A.tool==T_EDIT&&!inStrip&&my>=main_y0()&&my<panel_y0()){
            A.rbDrag=true; A.rbx0=A.rbx1=mx; A.rby0=A.rby1=my; A.selVerts.clear(); return 0; }
        int bi=caja_en(col,row); HMENU menu=CreatePopupMenu();
        const int CLS_BASE=100, NEW_BASE=200;        // 100+idx asignar ; 200+idx crear
        bool hasSel=(A.sc0>=0&&A.sc1>A.sc0);
        if(bi>=0){ A.sel=bi;
            AppendMenuA(menu,MF_STRING,7,"Reproducir etiqueta");                 // reproducir LA etiqueta
            if(hasSel)AppendMenuA(menu,MF_STRING,4,"Reproducir seleccion");       // reproducir el area seleccionada
            if(A.dets[bi].kind==KIND_POLY){ AppendMenuA(menu,MF_STRING,8,"Auto-mejorar etiqueta (segun filtro)");  // re-segmenta la region
                AppendMenuA(menu,MF_STRING,9,"Buffer..."); }                                                       // modal de buffer
            AppendMenuA(menu,MF_SEPARATOR,0,0);
            for(int k=0;k<(int)A.classes.size();++k)AppendMenuA(menu,MF_STRING,CLS_BASE+k,(std::string("Etiqueta: ")+A.classes[k].name).c_str());
            AppendMenuA(menu,MF_SEPARATOR,0,0); AppendMenuA(menu,MF_STRING,10,"Nueva etiqueta...");
            AppendMenuA(menu,MF_STRING,3,"Borrar"); }
        else if(hasSel){ AppendMenuA(menu,MF_STRING,4,"Reproducir seleccion");
            AppendMenuA(menu,MF_SEPARATOR,0,0);
            for(int k=0;k<(int)A.classes.size();++k)AppendMenuA(menu,MF_STRING,NEW_BASE+k,(std::string("Crear: ")+A.classes[k].name).c_str());
            AppendMenuA(menu,MF_SEPARATOR,0,0); AppendMenuA(menu,MF_STRING,10,"Nueva etiqueta...");
            AppendMenuA(menu,MF_STRING,11,"Borrar etiquetas en la seleccion (Supr)");   // borra las de dentro
            AppendMenuA(menu,MF_STRING,5,"Quitar seleccion"); }
        else { AppendMenuA(menu,MF_STRING,6,"Auto-etiquetar"); AppendMenuA(menu,MF_STRING,10,"Nueva etiqueta..."); }
        POINT pt={mx,my}; ClientToScreen(h,&pt);
        int cmd=TrackPopupMenu(menu,TPM_RETURNCMD|TPM_LEFTALIGN,pt.x,pt.y,0,h,0); DestroyMenu(menu);
        if(cmd>=CLS_BASE&&cmd<NEW_BASE&&bi>=0)A.dets[bi].cls=cmd-CLS_BASE;
        else if(cmd>=NEW_BASE)crear_caja(cmd-NEW_BASE);
        else if(cmd==3&&bi>=0){A.dets.erase(A.dets.begin()+bi);A.sel=-1;}
        else if(cmd==4)play_drag_sel();
        else if(cmd==5){A.sc0=A.sc1=A.sr0=A.sr1=-1;}
        else if(cmd==11){ int n=delete_in_selection(); std::cout<<"Borradas "<<n<<" etiquetas (seleccion)\n"; }
        else if(cmd==6)auto_segment();
        else if(cmd==7)play_det(bi);
        else if(cmd==8&&bi>=0){ show_busy("Mejorando etiqueta..."); improve_det(bi); }
        else if(cmd==9&&bi>=0){ A.buffering=true; A.bufSel=bi; A.bufOrig=A.dets[bi]; A.bufOrigBuf=A.autoBuffer; }  // abre modal de buffer
        else if(cmd==10){A.naming=true;A.nameBuf.clear();}
        return 0; }
    case WM_RBUTTONUP:{ if(!A.rbDrag)return 0; A.rbDrag=false;   // fin del rectangulo de seleccion de vertices (Editar)
        int x0=min(A.rbx0,A.rbx1),x1=max(A.rbx0,A.rbx1),y0=min(A.rby0,A.rby1),y1=max(A.rby0,A.rby1);
        A.selVerts.clear();
        bool clic=(std::abs(A.rbx1-A.rbx0)<4&&std::abs(A.rby1-A.rby0)<4);
        // elegir el poligono objetivo: el seleccionado, o el primero con vertices en el rect/cerca
        auto verticesIn=[&](int di)->std::vector<int>{ std::vector<int> r; const Det&d=A.dets[di]; if(d.kind!=KIND_POLY)return r;
            if(clic){ double bd=12*12; int vi=-1; for(size_t k=0;k<d.px.size();++k){ float sx,sy; spec_to_main((float)d.px[k],(float)d.py[k],sx,sy);
                        double dd=(A.rbx1-sx)*(A.rbx1-sx)+(A.rby1-sy)*(A.rby1-sy); if(dd<bd){bd=dd;vi=(int)k;} } if(vi>=0)r.push_back(vi); }
            else for(size_t k=0;k<d.px.size();++k){ float sx,sy; spec_to_main((float)d.px[k],(float)d.py[k],sx,sy);
                    if(sx>=x0&&sx<=x1&&sy>=y0&&sy<=y1)r.push_back((int)k); }
            return r; };
        int target=-1; std::vector<int> vs;
        if(A.sel>=0&&A.sel<(int)A.dets.size()){ vs=verticesIn(A.sel); if(!vs.empty())target=A.sel; }
        if(target<0) for(int i=0;i<(int)A.dets.size();++i){ auto v=verticesIn(i); if(!v.empty()){target=i;vs=v;break;} }
        if(target>=0){ A.sel=target; A.selDet=target; A.selVerts=vs;
            std::cout<<"Vertices marcados: "<<vs.size()<<" (Supr para borrar)\n"; }
        return 0; }
    case WM_MOUSEWHEEL:{ int dz=GET_WHEEL_DELTA_WPARAM(wp);
        if(A.view==1&&A.spec.W>0){ POINT pt={LOWORD(lp),HIWORD(lp)}; ScreenToClient(h,&pt);   // rueda = ZOOM 2D hacia el cursor
            if(pt.x>=plotX0()-2&&pt.x<=plotX1()+2&&pt.y>=plotY0()-2&&pt.y<=plotY1()+2){ int c,r; main_to_spec(pt.x,pt.y,c,r); zoom2d(dz>0?0.8f:1.25f,c,r); return 0; } }
        A.dist*=(dz>0)?0.9f:1.1f; A.dist=max(0.5f,min(20.f,A.dist)); return 0; }
    case WM_KEYDOWN:
        if(wp==VK_DELETE){ if(delete_marked_vertex()){ std::cout<<"Vertice borrado\n"; return 0; }  // Supr: si hay vertice marcado, borra solo ese
            int n=delete_in_selection(); if(n)std::cout<<"Borradas "<<n<<" etiquetas (seleccion)\n"; return 0; }  // si no, borra etiquetas de la seleccion
        if(wp==VK_ESCAPE){   // Esc: cancela el modo activo; NUNCA cierra la ventana (se sale con 'q')
            if(A.buffering){ if(A.bufSel>=0&&A.bufSel<(int)A.dets.size())A.dets[A.bufSel]=A.bufOrig; A.autoBuffer=A.bufOrigBuf; A.buffering=false; }  // cancela: restaura
            else if(A.naming)A.naming=false; else if(!A.polyX.empty()){A.polyX.clear();A.polyY.clear();} else if(!A.cutX.empty()){A.cutX.clear();A.cutY.clear();}
            else { A.selVerts.clear(); A.selDet=A.selVert=-1; A.sc0=A.sc1=A.sr0=A.sr1=-1; }   // limpia marcas/seleccion
            return 0; } return 0;
    case WM_CHAR:
        if(A.buffering){ if((int)wp==13)A.buffering=false; return 0; }   // Enter = aplicar buffer
        if(A.naming){ int ch=(int)wp;
            if(ch==13){ if(!A.nameBuf.empty())add_class(A.nameBuf); A.naming=false; layout_botones(); }
            else if(ch==27)A.naming=false;
            else if(ch==8){ if(!A.nameBuf.empty())A.nameBuf.pop_back(); }
            else if(ch>=32&&ch<127&&(int)A.nameBuf.size()<24)A.nameBuf.push_back((char)ch);
            return 0; }
        if((int)wp==13){ if(!A.polyX.empty())finish_poly(); return 0; }   // Enter cierra el poligono a mano
        do_action((int)wp); return 0;
    case WM_DESTROY: PLAYER.stop(); PostQuitMessage(0); return 0;
    }
    return DefWindowProc(h,m,wp,lp);
}

int main(int argc,char**argv){
    A.out_dir=(argc>=3)?argv[2]:"."; layout_botones();
    HINSTANCE hi=GetModuleHandle(0); WNDCLASSA wc{}; wc.style=CS_OWNDC; wc.lpfnWndProc=WndProc;
    wc.hInstance=hi; wc.lpszClassName=CLS; wc.hCursor=LoadCursor(0,IDC_ARROW);
    wc.hIcon=LoadIcon(hi,MAKEINTRESOURCE(1)); RegisterClassA(&wc);
    HWND hwnd=CreateWindowA(CLS,"IIAP SachaAcoustic - espectrograma / 3D / mandala / rio / constelacion / ondas",
        WS_OVERLAPPEDWINDOW,CW_USEDEFAULT,CW_USEDEFAULT,A.cW,A.cH,0,0,hi,0);
    HDC hdc=GetDC(hwnd); PIXELFORMATDESCRIPTOR pfd{}; pfd.nSize=sizeof(pfd); pfd.nVersion=1;
    pfd.dwFlags=PFD_DRAW_TO_WINDOW|PFD_SUPPORT_OPENGL|PFD_DOUBLEBUFFER; pfd.iPixelType=PFD_TYPE_RGBA;
    pfd.cColorBits=24; pfd.cDepthBits=24; int pf=ChoosePixelFormat(hdc,&pfd); SetPixelFormat(hdc,pf,&pfd);
    HGLRC rc=wglCreateContext(hdc); wglMakeCurrent(hdc,rc); g_hdc=hdc; FONT.init(hdc); glEnable(GL_POINT_SMOOTH);
    ShowWindow(hwnd,SW_SHOW); UpdateWindow(hwnd); ayuda();
    if(argc>=2) load_audio(argv[1]);
    if(argc>=4){ A.yaw=(float)atof(argv[3]); A.view=2; layout_botones(); }   // (preview) yaw inicial + vista 3D
    if(argc>=5){ A.dbMin=(float)atof(argv[4]); upload_texture(); }            // (preview) piso dB para ralear la superficie
    if(argc>=6){ A.pitch=(float)atof(argv[5]); }                              // (preview) pitch inicial
    MSG msg; bool run=true;
    int rfc=0;
    while(run){ while(PeekMessage(&msg,0,0,0,PM_REMOVE)){ if(msg.message==WM_QUIT){run=false;break;} TranslateMessage(&msg); DispatchMessage(&msg);} render(); SwapBuffers(hdc); autosave_tick();
        if(A.refilterPending && ++rfc>=12){ rfc=0; A.refilterPending=false; refilter_live(); }   // filtros en TIEMPO REAL sobre el sonido (~cada 140ms, incl. durante el arrastre)
        Sleep(10); }
    guardar(true);   // autosave al cerrar
    wglMakeCurrent(0,0); wglDeleteContext(rc); ReleaseDC(hwnd,hdc); return 0;
}
