// enhance.hpp - Realce FIEL (monotono, NO elimina datos) + colormap magma.
// Familia elegida en los experimentos: asinh / mu-law / log / gamma.
// PCEN se descarto por normalizar y distorsionar el sonido.
#pragma once
#include <vector>
#include <algorithm>
#include <cmath>
#include "types.hpp"
#include "magma_lut.hpp"

// Normaliza a [0,1] con percentiles suaves (p0.1-p99.9): casi no recorta.
inline Img percentile_rescale(const Img& m, double p_lo = 0.1, double p_hi = 99.9) {
    std::vector<float> v = m.d;
    std::sort(v.begin(), v.end());
    auto pct = [&](double p) {
        size_t i = (size_t)std::min(std::max(0.0, p / 100.0 * (v.size() - 1)),
                                    (double)(v.size() - 1));
        return v[i];
    };
    float lo = pct(p_lo), hi = pct(p_hi), den = std::max(1e-6f, hi - lo);
    Img out(m.W, m.H);
    for (size_t i = 0; i < m.d.size(); ++i) {
        float x = (m.d[i] - lo) / den;
        out.d[i] = x < 0 ? 0 : (x > 1 ? 1 : x);
    }
    return out;
}

// asinh: lineal para chicos, log para grandes. beta chico = levanta debiles.
inline Img enhance_asinh(const Img& g_in, double beta = 0.07) {
    Img g = percentile_rescale(g_in);
    Img out(g.W, g.H);
    float norm = (float)std::asinh(1.0 / beta);
    for (size_t i = 0; i < g.d.size(); ++i)
        out.d[i] = (float)std::asinh(g.d[i] / beta) / norm;
    return out;
}

// mu-law (companding de audio). mu mayor = mas compresion.
inline Img enhance_mulaw(const Img& g_in, double mu = 100.0) {
    Img g = percentile_rescale(g_in);
    Img out(g.W, g.H);
    float den = (float)std::log1p(mu);
    for (size_t i = 0; i < g.d.size(); ++i)
        out.d[i] = (float)std::log1p(mu * g.d[i]) / den;
    return out;
}

inline Img enhance_gamma(const Img& g_in, double gamma = 0.45) {
    Img g = percentile_rescale(g_in);
    Img out(g.W, g.H);
    for (size_t i = 0; i < g.d.size(); ++i)
        out.d[i] = std::pow(g.d[i], (float)gamma);
    return out;
}

// Colorea 0..1 -> RGB con paleta magma.
inline RGBImg colorize_magma(const Img& f01) {
    RGBImg out(f01.W, f01.H);
    for (size_t i = 0; i < f01.d.size(); ++i) {
        float v = f01.d[i]; v = v < 0 ? 0 : (v > 1 ? 1 : v);
        int idx = (int)(v * 255.0f + 0.5f);
        out.d[3 * i + 0] = MAGMA_LUT[idx][0];
        out.d[3 * i + 1] = MAGMA_LUT[idx][1];
        out.d[3 * i + 2] = MAGMA_LUT[idx][2];
    }
    return out;
}
