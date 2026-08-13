// autolabel.hpp - Autoetiquetado por vision clasica SIN OpenCV.
// Umbral adaptativo por banda + multi-resolucion + morfologia adaptativa + tracking temporal.
// Optimizacion: dilate_into/erode_into in-place + ping-pong en auto_label.
#pragma once
#include <vector>
#include <algorithm>
#include <cmath>
#include <queue>
#include "types.hpp"

enum { CLASE_BIO = 0, CLASE_ANTRO = 1 };

using Mask = std::vector<uint8_t>;

// Version original (retorna copia) — se mantiene para compatibilidad.
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

// Version in-place: escribe resultado en dst (debe tener m.size() elementos).
inline void dilate_into(const Mask& m, Mask& dst, int W, int H, int kw, int kh) {
    std::fill(dst.begin(), dst.end(), 0);
    int rx = kw / 2, ry = kh / 2;
    for (int y = 0; y < H; ++y)
        for (int x = 0; x < W; ++x) {
            if (!m[y * W + x]) continue;
            for (int dy = -ry; dy <= ry; ++dy)
                for (int dx = -rx; dx <= rx; ++dx) {
                    int ny = y + dy, nx = x + dx;
                    if (ny >= 0 && ny < H && nx >= 0 && nx < W) dst[ny * W + nx] = 1;
                }
        }
}

// Version original (retorna copia).
inline Mask erode(const Mask& m, int W, int H, int kw, int kh) {
    Mask o(m.size(), 1);
    int rx = kw / 2, ry = kh / 2;
    for (int y = 0; y < H; ++y)
        for (int x = 0; x < W; ++x) {
            bool all = true;
            for (int dy = -ry; dy <= ry && all; ++dy)
                for (int dx = -rx; dx <= rx; ++dx) {
                    int ny = y + dy, nx = x + dx;
                    if (ny < 0 || ny >= H || nx < 0 || nx >= W) continue;
                    if (!m[ny * W + nx]) { all = false; break; }
                }
            o[y * W + x] = all ? 1 : 0;
        }
    return o;
}

// Version in-place: escribe resultado en dst.
inline void erode_into(const Mask& m, Mask& dst, int W, int H, int kw, int kh) {
    std::fill(dst.begin(), dst.end(), 1);
    int rx = kw / 2, ry = kh / 2;
    for (int y = 0; y < H; ++y)
        for (int x = 0; x < W; ++x) {
            bool all = true;
            for (int dy = -ry; dy <= ry && all; ++dy)
                for (int dx = -rx; dx <= rx; ++dx) {
                    int ny = y + dy, nx = x + dx;
                    if (ny < 0 || ny >= H || nx < 0 || nx >= W) continue;
                    if (!m[ny * W + nx]) { all = false; break; }
                }
            dst[y * W + x] = all ? 1 : 0;
        }
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

// --- Umbral adaptativo por banda de frecuencia ---
// Divide el eje de frecuencia en n_bands bandas y estima el piso de ruido
// usando percentil bajo (noise_pct) + MAD robusta por banda.
// El percentil bajo evita que señales continuas (coros de insectos, ranas)
// contaminen la estimación de ruido y eleven el umbral.
// Retorna un vector de umbrales, uno por fila, interpolado entre bandas.
inline std::vector<float> estimate_noise_floor(const Img& e, int K, int n_bands,
                                               float noise_pct = 10.0f) {
    int W = e.W, H = e.H;
    std::vector<float> thr(H);
    if (n_bands < 1) n_bands = 1;
    std::vector<float> band_thr(n_bands);
    int band_h = std::max(1, H / n_bands);
    for (int b = 0; b < n_bands; ++b) {
        int y0 = b * band_h;
        int y1 = (b == n_bands - 1) ? H : std::min(y0 + band_h, H);
        if (y0 >= y1) { band_thr[b] = 0; continue; }
        // Recopilar todos los valores de esta banda
        size_t count = (size_t)W * (y1 - y0);
        std::vector<float> vals(count);
        size_t idx = 0;
        for (int y = y0; y < y1; ++y)
            for (int x = 0; x < W; ++x)
                vals[idx++] = e.at(y, x);
        // Percentil bajo: estimación robusta del piso de ruido
        // (no se sesga aunque la señal llene toda la banda)
        size_t pct_idx = (size_t)(noise_pct / 100.0f * (count - 1));
        pct_idx = std::min(pct_idx, count - 1);
        std::nth_element(vals.begin(), vals.begin() + pct_idx, vals.end());
        float noise_floor = vals[pct_idx];
        // MAD robusta: calcula dispersión de las muestras BAJAS (ruido)
        // Solo usa valores por debajo de la mediana para la MAD
        size_t mid = count / 2;
        std::nth_element(vals.begin(), vals.begin() + mid, vals.end());
        float med = vals[mid];
        // Filtrar solo valores cercanos al noise_floor (dentro del percentil 50)
        // para estimar la dispersión del ruido sin contaminar con la señal
        size_t n_noise = 0;
        for (size_t i = 0; i < count; ++i)
            if (vals[i] <= med) vals[n_noise++] = vals[i];
        if (n_noise > 1) {
            for (size_t i = 0; i < n_noise; ++i) vals[i] = std::fabs(vals[i] - noise_floor);
            size_t mid2 = n_noise / 2;
            std::nth_element(vals.begin(), vals.begin() + mid2, vals.begin() + n_noise);
            float mad = vals[mid2] * 1.4826f + 1e-6f;
            band_thr[b] = noise_floor + (float)K * mad;
        } else {
            band_thr[b] = noise_floor + (float)K * 0.01f;
        }
    }
    // Interpolar entre bandas para cada fila
    for (int y = 0; y < H; ++y) {
        float pos = (float)y / (float)H * (float)n_bands - 0.5f;
        int b0 = std::max(0, std::min(n_bands - 1, (int)std::floor(pos)));
        int b1 = std::max(0, std::min(n_bands - 1, b0 + 1));
        float t = pos - (float)b0;
        t = t < 0 ? 0 : (t > 1 ? 1 : t);
        thr[y] = band_thr[b0] * (1.0f - t) + band_thr[b1] * t;
    }
    return thr;
}

// --- Filtro de densidad: elimina pixeles aislados ---
// Para cada pixel activo, cuenta sus vecinos activos en una ventana r vecinos.
// Si tiene menos de min_neighbors vecinos activos, se elimina.
// Esto quita partículas dispersas tras el filtro dB sin perder regiones densas.
inline void remove_sparse_pixels(Mask& mask, int W, int H, int r = 1, int min_neighbors = 2) {
    Mask tmp(mask.size());
    for (int y = 0; y < H; ++y) {
        for (int x = 0; x < W; ++x) {
            if (!mask[y * W + x]) { tmp[y * W + x] = 0; continue; }
            int count = 0;
            for (int dy = -r; dy <= r; ++dy)
                for (int dx = -r; dx <= r; ++dx) {
                    int ny = y + dy, nx = x + dx;
                    if (ny >= 0 && ny < H && nx >= 0 && nx < W && mask[ny * W + nx])
                        ++count;
                }
            // count incluye el pixel mismo; necesitamos al menos min_neighbors EXTRAS
            tmp[y * W + x] = (count - 1 >= min_neighbors) ? 1 : 0;
        }
    }
    mask = tmp;
}

// --- Tracking temporal: elimina detecciones menores que min_persistence frames ---
// Para cada fila, cuenta run-length de píxeles activos en x.
// Si el run-length < min_persistence, los marca como inactivos.
inline void temporal_consistency_check(Mask& mask, int W, int H, int min_persistence) {
    if (min_persistence <= 1) return;
    // Trabajar por filas: cada fila es un "frame" temporal (columna = tiempo)
    // En el espectrograma: columnas = tiempo, filas = frecuencia
    // Queremos que un evento dure al menos min_persistence columnas
    // Iterar por cada fila y, buscar runs de 1s en x (tiempo)
    for (int y = 0; y < H; ++y) {
        int run_start = -1;
        for (int x = 0; x <= W; ++x) {
            bool active = (x < W) && (mask[y * W + x] != 0);
            if (active && run_start < 0) {
                run_start = x;
            } else if (!active && run_start >= 0) {
                int run_len = x - run_start;
                if (run_len < min_persistence) {
                    for (int xx = run_start; xx < x; ++xx)
                        mask[y * W + xx] = 0;
                }
                run_start = -1;
            }
        }
    }
}

// --- Morfología adaptativa: ajusta kernel según densidad local ---
// Calcula densidad local en ventanas 16x16 y ajusta el kernel de close.
// Zonas densas → kernel pequeño; zonas sparse → kernel grande.
inline void adaptive_close(const Mask& src, Mask& tmp, Mask& dst, int W, int H,
                           int base_kw, int base_kh) {
    const int WIN = 16;
    // Paso 1: close con kernel base
    erode_into(src, tmp, W, H, base_kw, base_kh);
    dilate_into(tmp, dst, W, H, base_kw, base_kh);
    // Paso 2: para regiones sparse, aplicar close extra con kernel más grande
    int big_kw = std::min(15, base_kw * 2 + 1);
    int big_kh = std::min(7, base_kh * 2 + 1);
    Mask tmp2((size_t)W * H);
    erode_into(src, tmp2, W, H, big_kw, big_kh);
    dilate_into(tmp2, tmp, W, H, big_kw, big_kh);
    // Fusionar: si la región es sparse (densidad < 0.15), usar el resultado grande
    for (int y = 0; y < H; ++y) {
        for (int x = 0; x < W; ++x) {
            // Calcular densidad local
            int x0 = std::max(0, x - WIN / 2), x1 = std::min(W, x + WIN / 2);
            int y0 = std::max(0, y - WIN / 2), y1 = std::min(H, y + WIN / 2);
            int total = 0, active = 0;
            for (int yy = y0; yy < y1; ++yy)
                for (int xx = x0; xx < x1; ++xx) {
                    ++total;
                    if (src[yy * W + xx]) ++active;
                }
            float density = (float)active / (float)total;
            // Si densidad baja y el close grande detectó algo que el base no: usar grande
            if (density < 0.15f && tmp[y * W + x] && !dst[y * W + x])
                dst[y * W + x] = 1;
        }
    }
}

// --- Análisis multi-resolución ---
// Ejecuta detección en múltiples escalas y fusiona las máscaras.
// Retorna la máscara fusionada.
inline Mask multires_detect(const Img& e, float K, int n_scales) {
    int W = e.W, H = e.H;
    Mask fused((size_t)W * H, 0);
    // Escalas: diferentes tamaños de suavizado y close
    struct Scale { int smooth_r; int close_kw, close_kh; };
    std::vector<Scale> scales;
    if (n_scales >= 3) scales.push_back({0, 3, 3});    // fina
    scales.push_back({1, 9, 3});                         // media (default)
    if (n_scales >= 3) scales.push_back({2, 15, 5});   // gruesa
    if (n_scales == 2) { scales.clear(); scales.push_back({1, 7, 3}); scales.push_back({2, 13, 5}); }

    std::vector<float> band_thr = estimate_noise_floor(e, K, 8, 10.0f);
    Mask buf_a((size_t)W * H), buf_b((size_t)W * H);

    for (auto& sc : scales) {
        // Suavizado
        Img proc = e;
        for (int r = 0; r < sc.smooth_r; ++r) proc = smooth3x3(proc);
        // Umbral por banda
        Mask mask((size_t)W * H, 0);
        for (int y = 0; y < H; ++y)
            for (int x = 0; x < W; ++x)
                if (proc.at(y, x) > band_thr[y]) mask[y * W + x] = 1;
        // Close en esta escala
        erode_into(mask, buf_a, W, H, sc.close_kw, sc.close_kh);
        dilate_into(buf_a, mask, W, H, sc.close_kw, sc.close_kh);
        // Fusionar con OR
        for (size_t i = 0; i < fused.size(); ++i)
            if (mask[i]) fused[i] = 1;
    }
    return fused;
}

// Suavizado de Chaikin: subdivide cada segmento en 2 puntos a 1/4 y 3/4,
// produciendo curvas suaves a partir de poligonos angulares.
// iters: numero de iteraciones (1 = suficiente para la mayoria de los casos).
inline void chaikin_smooth(std::vector<int>& px, std::vector<int>& py, int iters = 1) {
    if (px.size() < 3) return;
    for (int iter = 0; iter < iters; ++iter) {
        int n = (int)px.size();
        std::vector<int> nx, ny;
        for (int i = 0; i < n; ++i) {
            int j = (i + 1) % n;
            nx.push_back((3 * px[i] + px[j]) / 4);
            ny.push_back((3 * py[i] + py[j]) / 4);
            nx.push_back((px[i] + 3 * px[j]) / 4);
            ny.push_back((py[i] + 3 * py[j]) / 4);
        }
        px = std::move(nx);
        py = std::move(ny);
    }
}

// e: espectrograma realzado 0..1. K: estrictez de ruido. area_min: descarta
// manchas pequenas. make_poly: true -> contorno (poligono); false -> rectangulo
// (bounding box). buffer: dilatacion extra (px) que UNE detecciones cercanas y deja
// un margen alrededor -> menos etiquetas y poligono mas suave/menos detallado.
// n_bands: bandas para umbral adaptativo. multires: analisis multi-resolucion.
// adaptive_morph: morfologia adaptativa. min_persistence: frames minimos para tracking.
// min_neighbors: vecinos minimos para filtro de densidad (0 = desactivado).
// island_sep: separacion minima entre islas (px). Si 2 componentes estan unidos por
// un puente mas delgado que 2*island_sep+1, se separan. 0 = desactivado.
inline std::vector<Det> auto_label(const Img& e_in, float K = 4.5f, int area_min = 60,
                                   bool make_poly = true, int buffer = 0,
                                   int n_bands = 8, bool multires = true,
                                   int n_scales = 3, bool adaptive_morph = true,
                                   int min_persistence = 2, int min_neighbors = 2,
                                   int island_sep = 2) {
    const int W = e_in.W, H = e_in.H;
    Img e = smooth3x3(e_in);              // suavizado previo (robustez al ruido)

    // --- Fase 1: Umbralización adaptativa por banda ---
    Mask mask((size_t)W * H, 0);
    std::vector<float> band_thr = estimate_noise_floor(e, K, n_bands, 10.0f);
    for (int y = 0; y < H; ++y)
        for (int x = 0; x < W; ++x)
            if (e.at(y, x) > band_thr[y]) mask[y * W + x] = 1;

    // Umbral GLOBAL adicional: percentil bajo para no sesgarse con señales continuas
    {
        size_t N = (size_t)W * H;
        std::vector<float> tmp(N);
        for (size_t i = 0; i < N; ++i) tmp[i] = e.d[i];
        // Percentil 10 como piso de ruido global
        size_t gidx = (size_t)(0.10f * (N - 1));
        gidx = std::min(gidx, N - 1);
        std::nth_element(tmp.begin(), tmp.begin() + gidx, tmp.end());
        float gnoise = tmp[gidx];
        // MAD solo con valores bajos (ruido)
        size_t gmid = N / 2;
        std::nth_element(tmp.begin(), tmp.begin() + gmid, tmp.end());
        float gmed = tmp[gmid];
        size_t n_lo = 0;
        for (size_t i = 0; i < N; ++i)
            if (tmp[i] <= gmed) tmp[n_lo++] = tmp[i];
        if (n_lo > 1) {
            for (size_t i = 0; i < n_lo; ++i) tmp[i] = std::fabs(tmp[i] - gnoise);
            size_t mid2 = n_lo / 2;
            std::nth_element(tmp.begin(), tmp.begin() + mid2, tmp.begin() + n_lo);
            float gmad = tmp[mid2] * 1.4826f + 1e-6f;
            float gthr = gnoise + K * gmad;
            for (size_t i = 0; i < N; ++i) if (e.d[i] > gthr) mask[i] = 1;
        }
    }

    // --- Fase 2: Tracking temporal (eliminar transitorios cortos) ---
    temporal_consistency_check(mask, W, H, min_persistence);

    // --- Fase 3: Morfología ---
    Mask buf_a((size_t)W * H), buf_b((size_t)W * H);
    if (multires) {
        // Multi-resolución: OR de múltiples escalas
        mask = multires_detect(e, K, n_scales);
    } else if (adaptive_morph) {
        // Morfología adaptativa: open fijo + close adaptativo
        erode_into(mask, buf_a, W, H, 3, 3);
        dilate_into(buf_a, buf_b, W, H, 3, 3);     // open
        adaptive_close(buf_b, buf_a, mask, W, H, 9, 3);
    } else {
        // Original: open(3x3) + close(9x3)
        erode_into(mask, buf_a, W, H, 3, 3);
        dilate_into(buf_a, buf_b, W, H, 3, 3);     // open
        erode_into(buf_b, buf_a, W, H, 9, 3);
        dilate_into(buf_a, mask, W, H, 9, 3);      // close
    }

    // --- Fase 4: Separación de islas (romper puentes delgados) ---
    // Apertura para encontrar componentes separados. La máscara resultante
    // se usa SOLO para determinar qué píxeles pertenecen a qué componente.
    // Los contornos se trazan sobre la máscara ORIGINAL (antes de la apertura)
    // para capturar TODOS los píxeles de cada forma.
    Mask separated((size_t)W * H, 0);
    if (island_sep > 0) {
        int ek = 2 * island_sep + 1;
        erode_into(mask, buf_a, W, H, ek, ek);
        dilate_into(buf_a, separated, W, H, ek, ek);   // apertura → separación
    } else {
        separated = mask;   // sin separación: usar la máscara tal cual
    }

    // --- Fase 5: Filtro de densidad (quitar partículas dispersas) ---
    if (min_neighbors > 0) remove_sparse_pixels(separated, W, H, 1, min_neighbors);

    int dec = std::max(3, 3 + buffer * 2);

    // --- Fase 6: Encontrar componentes en la máscara SEPARADA ---
    std::vector<int> lab((size_t)W * H, 0);
    int next = 0;
    const int dx8[8] = {1, 1, 0, -1, -1, -1, 0, 1};
    const int dy8[8] = {0, 1, 1, 1, 0, -1, -1, -1};
    for (int y = 0; y < H; ++y)
        for (int x = 0; x < W; ++x) {
            if (!separated[y * W + x] || lab[y * W + x]) continue;
            ++next;
            std::queue<std::pair<int, int>> q;
            q.push({x, y}); lab[y * W + x] = next;
            while (!q.empty()) {
                auto [cx, cy] = q.front(); q.pop();
                for (int k = 0; k < 8; ++k) {
                    int nx = cx + dx8[k], ny = cy + dy8[k];
                    if (nx < 0 || nx >= W || ny < 0 || ny >= H) continue;
                    if (separated[ny * W + nx] && !lab[ny * W + nx]) {
                        lab[ny * W + nx] = next; q.push({nx, ny});
                    }
                }
            }
        }

    // --- Fase 7: Re-expandir etiquetas sobre la máscara ORIGINAL ---
    // Cada componente de separated "reclama" píxeles en mask (original) que
    // estén conectados a él pero NO reclamados por otro componente. Los puentes
    // en mask se asignan al componente más cercano, evitando que dos formas se mezclen.
    for (int label_id = 1; label_id <= next; ++label_id) {
        // Encontrar bbox del componente en separated
        int lx0 = W, lx1 = 0, ly0 = H, ly1 = 0;
        for (int y = 0; y < H; ++y)
            for (int x = 0; x < W; ++x)
                if (lab[y * W + x] == label_id) {
                    lx0 = std::min(lx0, x); lx1 = std::max(lx1, x);
                    ly0 = std::min(ly0, y); ly1 = std::max(ly1, y);
                }
        // Flood-fill en mask (original) desde los píxeles del componente,
        // reclamando solo píxeles no etiquetados
        for (int y = ly0; y <= ly1; ++y)
            for (int x = lx0; x <= lx1; ++x) {
                if (!mask[y * W + x] || lab[y * W + x]) continue;
                bool adj = false;
                for (int k = 0; k < 8 && !adj; ++k) {
                    int nx = x + dx8[k], ny = y + dy8[k];
                    if (nx >= 0 && nx < W && ny >= 0 && ny < H && lab[ny * W + nx] == label_id)
                        adj = true;
                }
                if (!adj) continue;
                std::queue<std::pair<int, int>> q;
                q.push({x, y}); lab[y * W + x] = label_id;
                while (!q.empty()) {
                    auto [cx, cy] = q.front(); q.pop();
                    for (int k = 0; k < 8; ++k) {
                        int nx = cx + dx8[k], ny = cy + dy8[k];
                        if (nx < 0 || nx >= W || ny < 0 || ny >= H) continue;
                        if (mask[ny * W + nx] && !lab[ny * W + nx]) {
                            lab[ny * W + nx] = label_id; q.push({nx, ny});
                        }
                    }
                }
            }
    }

    // --- Fase 8: Generar detecciones con contornos completos ---
    std::vector<Det> dets;
    for (int label_id = 1; label_id <= next; ++label_id) {
        int minx = W, maxx = 0, miny = H, maxy = 0, area = 0;
        for (int y = 0; y < H; ++y)
            for (int x = 0; x < W; ++x)
                if (lab[y * W + x] == label_id) {
                    minx = std::min(minx, x); maxx = std::max(maxx, x);
                    miny = std::min(miny, y); maxy = std::max(maxy, y);
                    ++area;
                }
        if (area < area_min) continue;
        Det d;
        d.x = minx; d.y = miny; d.w = maxx - minx + 1; d.h = maxy - miny + 1;
        int cy = d.y + d.h / 2;
        d.cls = (cy > 0.75 * H) ? CLASE_ANTRO : CLASE_BIO;
        if (make_poly) {
            d.kind = KIND_POLY;
            std::vector<int> bx, by;
            // Buscar un pixel semilla dentro de este componente
            int sx = -1, sy = -1;
            for (int y = miny; y <= maxy && sx < 0; ++y)
                for (int x = minx; x <= maxx && sx < 0; ++x)
                    if (lab[y * W + x] == label_id) { sx = x; sy = y; }
            if (sx >= 0) trace_boundary(lab, W, H, label_id, sx, sy, bx, by);
            for (size_t i = 0; i < bx.size(); i += dec) { d.px.push_back(bx[i]); d.py.push_back(by[i]); }
            if (d.px.size() < 3) { d.px = {d.x, d.x + d.w, d.x + d.w, d.x};
                d.py = {d.y, d.y, d.y + d.h, d.y + d.h}; }
            else {
                chaikin_smooth(d.px, d.py, 1);
                find_holes(mask, W, H, minx, miny, maxx, maxy, dec, std::max(8, area_min / 4), d.hx, d.hy);
            }
        } else {
            d.kind = KIND_BBOX;
            d.px = {d.x, d.x + d.w, d.x + d.w, d.x};
            d.py = {d.y, d.y, d.y + d.h, d.y + d.h};
        }
        dets.push_back(std::move(d));
    }
    return dets;
}
