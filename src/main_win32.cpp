// main_win32.cpp - Etiquetador de espectrogramas con GUI Win32 (sin OpenCV).
//
// Flujo: WAV -> STFT -> espectrograma -> realce FIEL asinh (no elimina datos)
//        -> autoetiquetado (cajas + segmentacion) -> edicion -> COCO + BMP.
//
// Controles:
//   raton izq (arrastrar) : crear caja (clase activa)   izq (click): seleccionar
//   raton der (click)     : borrar caja bajo el cursor
//   a: re-autoetiquetar   v: realzado/original   g: gate de ruido on/off
//   b/n: clase activa bio/antro   1/2: fijar seleccion bio/antro
//   +/-: estrictez ruido K (si gate on)   d: borrar sel   c: limpiar
//   s: guardar (COCO+BMP)   q/ESC: salir
#define NOMINMAX
#include <windows.h>
#include <algorithm>
#include <string>
#include <vector>
#include <iostream>
using std::min; using std::max;
#include "wav.hpp"
#include "spectrogram.hpp"
#include "enhance.hpp"
#include "autolabel.hpp"
#include "coco.hpp"
#include "bmp.hpp"

struct App {
    Img spec, enhanced;
    RGBImg color;
    std::vector<uint8_t> dib;   // BGR padded, top-down
    int imgW = 0, imgH = 0, stride = 0;
    double scale = 1.0;
    std::vector<Det> dets;
    int sel = -1, clase_activa = CLASE_BIO;
    bool mostrar_realzado = true, usar_gate = false, dibujando = false;
    float K = 4.0f;            // estrictez de ruido del semi-etiquetado
    int area_min = 30;         // area minima de una deteccion
    POINT p0{0, 0}, p1{0, 0};
    std::string wav_path, out_dir;
};
static App A;
static const char* CLS_NAME = "EtiquetadorEspectro";

static void recompute_enhanced() {
    Img base = enhance_asinh(A.spec, 0.07);
    A.enhanced = base;  // asinh fiel por defecto
}

static void rebuild_dib() {
    const Img& src = A.mostrar_realzado ? A.enhanced : A.spec;
    A.color = colorize_magma(src);
    A.imgW = A.color.W; A.imgH = A.color.H;
    A.stride = ((A.imgW * 3 + 3) / 4) * 4;
    A.dib.assign((size_t)A.stride * A.imgH, 0);
    for (int y = 0; y < A.imgH; ++y) {
        uint8_t* dst = &A.dib[(size_t)y * A.stride];
        for (int x = 0; x < A.imgW; ++x) {
            size_t s = ((size_t)y * A.imgW + x) * 3;
            dst[x * 3 + 0] = A.color.d[s + 2];  // B
            dst[x * 3 + 1] = A.color.d[s + 1];  // G
            dst[x * 3 + 2] = A.color.d[s + 0];  // R
        }
    }
}

static POINT to_spec(int dx, int dy) {
    return {(LONG)(dx / A.scale), (LONG)(dy / A.scale)};
}
static int caja_en(POINT s) {
    for (int i = (int)A.dets.size() - 1; i >= 0; --i) {
        const Det& d = A.dets[i];
        if (s.x >= d.x && s.x < d.x + d.w && s.y >= d.y && s.y < d.y + d.h) return i;
    }
    return -1;
}

static void paint(HWND hwnd) {
    PAINTSTRUCT ps; HDC hdc = BeginPaint(hwnd, &ps);
    int dW = (int)(A.imgW * A.scale), dH = (int)(A.imgH * A.scale);

    BITMAPINFO bmi; ZeroMemory(&bmi, sizeof(bmi));
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = A.imgW;
    bmi.bmiHeader.biHeight = -A.imgH;       // top-down
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 24;
    bmi.bmiHeader.biCompression = BI_RGB;
    SetStretchBltMode(hdc, COLORONCOLOR);
    StretchDIBits(hdc, 0, 20, dW, dH, 0, 0, A.imgW, A.imgH,
                  A.dib.data(), &bmi, DIB_RGB_COLORS, SRCCOPY);

    SetBkMode(hdc, TRANSPARENT);
    HBRUSH hollow = (HBRUSH)GetStockObject(NULL_BRUSH);
    for (int i = 0; i < (int)A.dets.size(); ++i) {
        const Det& d = A.dets[i];
        COLORREF col = (d.cls == CLASE_ANTRO) ? RGB(0, 255, 255) : RGB(0, 255, 0);
        HPEN pen = CreatePen(PS_SOLID, (i == A.sel) ? 3 : 1, col);
        HGDIOBJ op = SelectObject(hdc, pen); HGDIOBJ ob = SelectObject(hdc, hollow);
        int x0 = (int)(d.x * A.scale), y0 = (int)(d.y * A.scale) + 20;
        int x1 = (int)((d.x + d.w) * A.scale), y1 = (int)((d.y + d.h) * A.scale) + 20;
        if (d.kind == KIND_POLY && d.px.size() >= 3) {
            std::vector<POINT> pts(d.px.size() + 1);
            for (size_t k = 0; k < d.px.size(); ++k)
                pts[k] = {(LONG)(d.px[k] * A.scale), (LONG)(d.py[k] * A.scale) + 20};
            pts[d.px.size()] = pts[0];
            Polyline(hdc, pts.data(), (int)pts.size());
        } else {
            Rectangle(hdc, x0, y0, x1, y1);
        }
        SetTextColor(hdc, col);
        TextOutA(hdc, x0 + 2, (y0 > 22 ? y0 - 15 : y0), nombre_clase(d.cls),
                 (int)strlen(nombre_clase(d.cls)));
        SelectObject(hdc, op); SelectObject(hdc, ob); DeleteObject(pen);
    }
    if (A.dibujando) {
        HPEN pen = CreatePen(PS_DOT, 1, RGB(255, 255, 255));
        HGDIOBJ op = SelectObject(hdc, pen), ob = SelectObject(hdc, hollow);
        Rectangle(hdc, A.p0.x, A.p0.y, A.p1.x, A.p1.y);
        SelectObject(hdc, op); SelectObject(hdc, ob); DeleteObject(pen);
    }
    // barra de estado
    RECT top{0, 0, dW, 20}; HBRUSH bk = CreateSolidBrush(RGB(0, 0, 0));
    FillRect(hdc, &top, bk); DeleteObject(bk);
    SetTextColor(hdc, RGB(255, 255, 255));
    std::string s = "clase=" + std::string(nombre_clase(A.clase_activa)) +
                    "  gate=" + (A.usar_gate ? "ON" : "OFF") +
                    "  cajas=" + std::to_string(A.dets.size()) +
                    (A.mostrar_realzado ? "  [asinh]" : "  [original]") +
                    "  (a=auto v=ver g=gate b/n=clase 1/2=set d=borra c=limpia s=guarda q=sal)";
    TextOutA(hdc, 4, 3, s.c_str(), (int)s.size());
    EndPaint(hwnd, &ps);
}

static void guardar() {
    std::string base = A.out_dir + "/etiquetas";
    write_bmp(base + ".bmp", colorize_magma(A.enhanced));
    LabelMeta m; m.audio_file = "audio.wav";
    export_coco(base + ".json", "etiquetas.bmp", A.spec.W, A.spec.H, A.dets, m, default_classes());
    std::cout << "Guardado: " << base << ".json y " << base << ".bmp\n";
}

static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_PAINT: paint(hwnd); return 0;
    case WM_LBUTTONDOWN:
        A.dibujando = true; A.p0 = A.p1 = {LOWORD(lp), HIWORD(lp)}; return 0;
    case WM_MOUSEMOVE:
        if (A.dibujando) { A.p1 = {LOWORD(lp), HIWORD(lp)}; InvalidateRect(hwnd, 0, FALSE); }
        return 0;
    case WM_LBUTTONUP: {
        A.dibujando = false;
        int x0 = min(A.p0.x, A.p1.x), y0 = min(A.p0.y, A.p1.y);
        int x1 = max(A.p0.x, A.p1.x), y1 = max(A.p0.y, A.p1.y);
        if (x1 - x0 < 4 || y1 - y0 < 4) {            // click = seleccionar
            A.sel = caja_en(to_spec(A.p1.x, A.p1.y - 20));
        } else {                                      // arrastre = nueva caja
            POINT e0 = to_spec(x0, y0 - 20), e1 = to_spec(x1, y1 - 20);
            Det d; d.x = e0.x; d.y = e0.y; d.w = e1.x - e0.x; d.h = e1.y - e0.y;
            d.px = {d.x, d.x + d.w, d.x + d.w, d.x};
            d.py = {d.y, d.y, d.y + d.h, d.y + d.h};
            d.cls = A.clase_activa;
            A.dets.push_back(d); A.sel = (int)A.dets.size() - 1;
        }
        InvalidateRect(hwnd, 0, FALSE); return 0;
    }
    case WM_RBUTTONDOWN: {
        int i = caja_en(to_spec(LOWORD(lp), HIWORD(lp) - 20));
        if (i >= 0) { A.dets.erase(A.dets.begin() + i); A.sel = -1; InvalidateRect(hwnd, 0, FALSE); }
        return 0;
    }
    case WM_KEYDOWN:
        if (wp == VK_ESCAPE) PostQuitMessage(0);
        return 0;
    case WM_CHAR: {
        int c = (int)wp; bool relabel = false, redisp = false;
        if (c == 'q') PostQuitMessage(0);
        else if (c == 'a') { A.dets = auto_label(A.spec, A.K, A.area_min); A.sel = -1; }
        else if (c == 'v') { A.mostrar_realzado = !A.mostrar_realzado; redisp = true; }
        else if (c == 'g') { A.usar_gate = !A.usar_gate; recompute_enhanced(); redisp = true; }
        else if (c == 'b') A.clase_activa = CLASE_BIO;
        else if (c == 'n') A.clase_activa = CLASE_ANTRO;
        else if (c == '1' && A.sel >= 0) A.dets[A.sel].cls = CLASE_BIO;
        else if (c == '2' && A.sel >= 0) A.dets[A.sel].cls = CLASE_ANTRO;
        else if (c == '+' || c == '=') { A.K += 0.5f; A.dets = auto_label(A.spec, A.K, A.area_min); }
        else if (c == '-' || c == '_') { A.K = max(1.0f, A.K - 0.5f); A.dets = auto_label(A.spec, A.K, A.area_min); }
        else if (c == 'd' && A.sel >= 0) { A.dets.erase(A.dets.begin() + A.sel); A.sel = -1; }
        else if (c == 'c') { A.dets.clear(); A.sel = -1; }
        else if (c == 's') guardar();
        if (redisp) rebuild_dib();
        InvalidateRect(hwnd, 0, FALSE); return 0;
    }
    case WM_DESTROY: PostQuitMessage(0); return 0;
    }
    return DefWindowProc(hwnd, msg, wp, lp);
}

int main(int argc, char** argv) {
    if (argc < 2) { std::cerr << "Uso: etiquetador <audio.wav> [carpeta_salida]\n"; return 1; }
    A.wav_path = argv[1]; A.out_dir = (argc >= 3) ? argv[2] : ".";
    try {
        AudioData a = load_wav(A.wav_path);
        std::cout << "WAV: " << a.samples.size() << " muestras @ " << a.sample_rate << " Hz\n";
        A.spec = compute_spectrogram(a, SpecParams{});
        recompute_enhanced();
        A.dets = auto_label(A.spec, A.K, A.area_min);
        std::cout << "Espectro " << A.spec.W << "x" << A.spec.H
                  << ", autoetiquetado: " << A.dets.size() << " detecciones\n";
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << "\n"; return 2;
    }
    rebuild_dib();

    // escalar para caber en pantalla
    int scrW = GetSystemMetrics(SM_CXSCREEN) - 80;
    int scrH = GetSystemMetrics(SM_CYSCREEN) - 140;
    double sx = (double)scrW / A.imgW, sy = (double)scrH / A.imgH;
    A.scale = min(1.5, min(sx, sy)); if (A.scale < 0.2) A.scale = 0.2;

    HINSTANCE hi = GetModuleHandle(0);
    WNDCLASSA wc; ZeroMemory(&wc, sizeof(wc));
    wc.lpfnWndProc = WndProc; wc.hInstance = hi; wc.lpszClassName = CLS_NAME;
    wc.hCursor = LoadCursor(0, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)GetStockObject(BLACK_BRUSH);
    RegisterClassA(&wc);

    int wW = (int)(A.imgW * A.scale) + 16, wH = (int)(A.imgH * A.scale) + 59;
    HWND hwnd = CreateWindowA(CLS_NAME, "Etiquetador de espectrograma (bio/antro)",
                              WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT,
                              wW, wH, 0, 0, hi, 0);
    ShowWindow(hwnd, SW_SHOW); UpdateWindow(hwnd);

    MSG m;
    while (GetMessage(&m, 0, 0, 0)) { TranslateMessage(&m); DispatchMessage(&m); }
    return 0;
}
