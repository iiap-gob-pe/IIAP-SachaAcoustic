// autolabel.hpp - Autoetiquetado por vision clasica SIN OpenCV.
// Umbral por banda (mediana+K*MAD) -> morfologia -> componentes conexos ->
// bounding box + poligono (trazado de contorno de Moore).
#pragma once
#include <vector>
#include <algorithm>
#include <cmath>
#include <queue>
#include "types.hpp"

enum { CLASE_BIO = 0, CLASE_ANTRO = 1 };

using Mask = std::vector<uint8_t>;

inline Mask dilate(const Mask& m, int W, int H, int kw, int kh) {
    Mask o(m.size(), 0);
    int rx = kw / 2, ry = kh / 2;
    for (int y = 0; y < H; ++y)
        for (int x = 0; x < W; ++x) {
            if (!m[y * W + x]) continue;
            for (int dy = -ry; dy <= ry; ++dy)
                for (int dx = -rx; dx <= rx; ++dx) {
                    int ny = y + dy, nx = x + dx;
                    if (ny >= 0 && ny < H && nx >= 0 && nx < W) o[ny * W + nx] = 1;
                }
        }
    return o;
}

inline Mask erode(const Mask& m, int W, int H, int kw, int kh) {
    Mask o(m.size(), 1);
    int rx = kw / 2, ry = kh / 2;
    for (int y = 0; y < H; ++y)
        for (int x = 0; x < W; ++x) {
            bool all = true;
            for (int dy = -ry; dy <= ry && all; ++dy)
                for (int dx = -rx; dx <= rx; ++dx) {
                    int ny = y + dy, nx = x + dx;
                    if (ny < 0 || ny >= H || nx < 0 || nx >= W) continue;  // fuera de imagen = PRESENTE -> no erosiona el borde (la señal que llega al borde se conserva)
                    if (!m[ny * W + nx]) { all = false; break; }
                }
            o[y * W + x] = all ? 1 : 0;
        }
    return o;
}

// Trazado de contorno de Moore (8-vec) sobre los pixeles con label objetivo.
inline void trace_boundary(const std::vector<int>& lab, int W, int H, int target,
                           int sx, int sy, std::vector<int>& px, std::vector<int>& py) {
    const int dx8[8] = {1, 1, 0, -1, -1, -1, 0, 1};
    const int dy8[8] = {0, 1, 1, 1, 0, -1, -1, -1};
    auto is = [&](int x, int y) {
        return x >= 0 && x < W && y >= 0 && y < H && lab[y * W + x] == target;
    };
    int cx = sx, cy = sy, dir = 7, steps = 0, maxsteps = 8 * (W + H) * 4;
    int startx = sx, starty = sy;
    do {
        px.push_back(cx); py.push_back(cy);
        bool found = false;
        int sdir = (dir + 6) & 7;  // retrocede para girar a la izquierda
        for (int i = 0; i < 8; ++i) {
            int nd = (sdir + i) & 7;
            int nx = cx + dx8[nd], ny = cy + dy8[nd];
            if (is(nx, ny)) { cx = nx; cy = ny; dir = nd; found = true; break; }
        }
        if (!found) break;  // pixel aislado
        if (++steps > maxsteps) break;
    } while (!(cx == startx && cy == starty));
}

// Detecta HUECOS (fondo encerrado) dentro del bbox [minx,miny]-[maxx,maxy] de un componente:
// inunda el fondo (mask==0) desde el borde del bbox; el fondo NO alcanzado son huecos (anillos).
// Cada hueco con area>=holeMin se traza y se decima; contornos en coords GLOBALES -> hxs/hys.
inline void find_holes(const Mask& mask, int W, int H, int minx, int miny, int maxx, int maxy,
                       int dec, int holeMin,
                       std::vector<std::vector<int>>& hxs, std::vector<std::vector<int>>& hys) {
    int bw = maxx - minx + 1, bh = maxy - miny + 1; if (bw < 3 || bh < 3) return;
    auto bg = [&](int xx, int yy) { return mask[(size_t)(miny + yy) * W + (minx + xx)] == 0; };  // es fondo
    std::vector<uint8_t> reach((size_t)bw * bh, 0);
    std::vector<std::pair<int,int>> q;
    auto pushIf = [&](int xx, int yy) { if (xx < 0 || xx >= bw || yy < 0 || yy >= bh) return;
        size_t i = (size_t)yy * bw + xx; if (!reach[i] && bg(xx, yy)) { reach[i] = 1; q.push_back({xx, yy}); } };
    for (int xx = 0; xx < bw; ++xx) { pushIf(xx, 0); pushIf(xx, bh - 1); }
    for (int yy = 0; yy < bh; ++yy) { pushIf(0, yy); pushIf(bw - 1, yy); }
    const int d4x[4] = {1,-1,0,0}, d4y[4] = {0,0,1,-1};
    while (!q.empty()) { auto [cx, cy] = q.back(); q.pop_back(); for (int k = 0; k < 4; ++k) pushIf(cx + d4x[k], cy + d4y[k]); }
    std::vector<int> hl((size_t)bw * bh, 0); int hid = 0;       // agrupar el fondo NO alcanzado = huecos
    for (int yy = 0; yy < bh; ++yy) for (int xx = 0; xx < bw; ++xx) {
        size_t i = (size_t)yy * bw + xx; if (reach[i] || !bg(xx, yy) || hl[i]) continue; ++hid;
        std::vector<std::pair<int,int>> s2; s2.push_back({xx, yy}); hl[i] = hid; int a = 0, ssx = xx, ssy = yy;
        while (!s2.empty()) { auto [cx, cy] = s2.back(); s2.pop_back(); ++a;
            for (int k = 0; k < 4; ++k) { int nx = cx + d4x[k], ny = cy + d4y[k]; if (nx < 0 || nx >= bw || ny < 0 || ny >= bh) continue;
                size_t ni = (size_t)ny * bw + nx; if (!reach[ni] && bg(nx, ny) && !hl[ni]) { hl[ni] = hid; s2.push_back({nx, ny}); } } }
        if (a < holeMin) continue;
        std::vector<int> bx, by; trace_boundary(hl, bw, bh, hid, ssx, ssy, bx, by);
        std::vector<int> hx, hy; for (size_t j = 0; j < bx.size(); j += dec) { hx.push_back(bx[j] + minx); hy.push_back(by[j] + miny); }
        if (hx.size() >= 3) { hxs.push_back(std::move(hx)); hys.push_back(std::move(hy)); }
    }
}

// Suavizado box 3x3 (separable) - reduce speckle antes de umbralizar. O(W*H).
inline Img smooth3x3(const Img& e) {
    const int W = e.W, H = e.H;
    Img t(W, H), o(W, H);
    for (int y = 0; y < H; ++y)            // horizontal
        for (int x = 0; x < W; ++x) {
            float a = e.at(y, std::max(0, x - 1)), b = e.at(y, x), c = e.at(y, std::min(W - 1, x + 1));
            t.at(y, x) = (a + b + c) / 3.f;
        }
    for (int y = 0; y < H; ++y)            // vertical
        for (int x = 0; x < W; ++x) {
            float a = t.at(std::max(0, y - 1), x), b = t.at(y, x), c = t.at(std::min(H - 1, y + 1), x);
            o.at(y, x) = (a + b + c) / 3.f;
        }
    return o;
}

// e: espectrograma realzado 0..1. K: estrictez de ruido. area_min: descarta
// manchas pequenas. make_poly: true -> contorno (poligono); false -> rectangulo
// (bounding box). buffer: dilatacion extra (px) que UNE detecciones cercanas y deja
// un margen alrededor -> menos etiquetas y poligono mas suave/menos detallado.
inline std::vector<Det> auto_label(const Img& e_in, float K = 4.5f, int area_min = 60,
                                   bool make_poly = true, int buffer = 0) {
    const int W = e_in.W, H = e_in.H;
    Img e = smooth3x3(e_in);              // suavizado previo (robustez al ruido)
    Mask mask((size_t)W * H, 0);
    std::vector<float> row(W), tmp(W);
    for (int y = 0; y < H; ++y) {
        for (int x = 0; x < W; ++x) row[x] = e.at(y, x);
        tmp = row;
        std::nth_element(tmp.begin(), tmp.begin() + W / 2, tmp.end());
        float med = tmp[W / 2];
        for (int x = 0; x < W; ++x) tmp[x] = std::fabs(row[x] - med);
        std::nth_element(tmp.begin(), tmp.begin() + W / 2, tmp.end());
        float mad = tmp[W / 2] * 1.4826f + 1e-6f;
        for (int x = 0; x < W; ++x)
            if (row[x] - med > K * mad) mask[y * W + x] = 1;
    }
    // Umbral GLOBAL adicional: las BANDAS GRAVES ESTACIONARIAS llenan toda la fila,
    // asi que su mediana por-fila es alta y el test por-fila NO las marca. Con un
    // umbral global (mediana+K*MAD de toda la imagen) capturamos esas bandas fuertes.
    {
        size_t N = (size_t)W * H; std::vector<float> all(e.d);
        std::nth_element(all.begin(), all.begin() + N / 2, all.end());
        float gmed = all[N / 2];
        for (size_t i = 0; i < N; ++i) all[i] = std::fabs(e.d[i] - gmed);
        std::nth_element(all.begin(), all.begin() + N / 2, all.end());
        float gmad = all[N / 2] * 1.4826f + 1e-6f;
        float gthr = gmed + K * gmad;
        for (size_t i = 0; i < N; ++i) if (e.d[i] > gthr) mask[i] = 1;
    }
    // open (3x3) quita motas; close (9x3) une el mismo canto
    mask = dilate(erode(mask, W, H, 3, 3), W, H, 3, 3);
    mask = erode(dilate(mask, W, H, 9, 3), W, H, 9, 3);
    if (buffer > 0) {                     // BUFFER: une detecciones cercanas + margen
        int k = 2 * buffer + 1;
        mask = dilate(mask, W, H, k, k);  // (sin erosionar -> deja el margen "buffer")
    }
    int dec = std::max(3, 3 + buffer * 2);// decimado del contorno (mas grueso con buffer)

    // componentes conexos (BFS 8-vec)
    std::vector<int> lab((size_t)W * H, 0);
    std::vector<Det> dets;
    int next = 0;
    const int dx8[8] = {1, 1, 0, -1, -1, -1, 0, 1};
    const int dy8[8] = {0, 1, 1, 1, 0, -1, -1, -1};
    for (int y = 0; y < H; ++y)
        for (int x = 0; x < W; ++x) {
            if (!mask[y * W + x] || lab[y * W + x]) continue;
            ++next;
            int minx = x, maxx = x, miny = y, maxy = y, area = 0;
            std::queue<std::pair<int, int>> q;
            q.push({x, y}); lab[y * W + x] = next;
            while (!q.empty()) {
                auto [cx, cy] = q.front(); q.pop(); ++area;
                minx = std::min(minx, cx); maxx = std::max(maxx, cx);
                miny = std::min(miny, cy); maxy = std::max(maxy, cy);
                for (int k = 0; k < 8; ++k) {
                    int nx = cx + dx8[k], ny = cy + dy8[k];
                    if (nx < 0 || nx >= W || ny < 0 || ny >= H) continue;
                    if (mask[ny * W + nx] && !lab[ny * W + nx]) {
                        lab[ny * W + nx] = next; q.push({nx, ny});
                    }
                }
            }
            if (area < area_min) continue;
            Det d;
            d.x = minx; d.y = miny; d.w = maxx - minx + 1; d.h = maxy - miny + 1;
            int cy = d.y + d.h / 2;
            d.cls = (cy > 0.75 * H) ? CLASE_ANTRO : CLASE_BIO;
            if (make_poly) {                 // poligono: contorno trazado y decimado
                d.kind = KIND_POLY;
                std::vector<int> bx, by;
                trace_boundary(lab, W, H, next, x, y, bx, by);
                for (size_t i = 0; i < bx.size(); i += dec) { d.px.push_back(bx[i]); d.py.push_back(by[i]); }
                if (d.px.size() < 3) { d.px = {d.x, d.x + d.w, d.x + d.w, d.x};
                    d.py = {d.y, d.y, d.y + d.h, d.y + d.h}; }
                else find_holes(mask, W, H, minx, miny, maxx, maxy, dec, std::max(8, area_min / 4), d.hx, d.hy);  // ANILLOS: huecos internos
            } else {                         // bounding box (rectangulo)
                d.kind = KIND_BBOX;
                d.px = {d.x, d.x + d.w, d.x + d.w, d.x};
                d.py = {d.y, d.y, d.y + d.h, d.y + d.h};
            }
            dets.push_back(std::move(d));
        }
    return dets;
}
