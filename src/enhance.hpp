// enhance.hpp - Realce FIEL (monotono, NO elimina datos) + colormap magma.
// Familia elegida en los experimentos: asinh / mu-law / log / gamma.
// PCEN se descarto por normalizar y distorsionar el sonido.
#pragma once
#include <vector>
#include <algorithm>
#include <cmath>
#include "types.hpp"
#include "magma_lut.hpp"

// Helper: percentiles usando nth_element (O(N)).
inline void find_percentiles(const std::vector<float>& v, float& lo, float& hi,
                             double p_lo = 0.1, double p_hi = 99.9) {
    auto pct = [&](double p) {
        size_t i = (size_t)std::min(std::max(0.0, p / 100.0 * (v.size() - 1)),
                                    (double)(v.size() - 1));
        std::vector<float> tmp = v;
        std::nth_element(tmp.begin(), tmp.begin() + i, tmp.end());
        return tmp[i];
    };
    lo = pct(p_lo);
    hi = pct(p_hi);
}

// asinh fusionado: encuentra percentiles y aplica asinh normalizado en UN solo pase.
// Produce resultado identico a percentile_rescale + enhance_asinh separados.
inline Img enhance_asinh(const Img& g_in, double beta = 0.07) {
    const auto& src = g_in.d;
    int W = g_in.W, H = g_in.H;
    // Paso 1: encontrar percentiles p0.1 y p99.9
    float lo, hi;
    find_percentiles(src, lo, hi);
    float den = std::max(1e-6f, hi - lo);
    float norm = (float)std::asinh(1.0 / beta);
    // Paso 2: normalizar + asinh en un solo pase (sin imagen intermedia)
    Img out(W, H);
    for (size_t i = 0; i < src.size(); ++i) {
        float x = (src[i] - lo) / den;
        x = x < 0 ? 0 : (x > 1 ? 1 : x);
        out.d[i] = (float)std::asinh(x / beta) / norm;
    }
    return out;
}

// mu-law (companding de audio). mu mayor = mas compresion.
// Fusión: percentiles + mu-law en un solo pase.
inline Img enhance_mulaw(const Img& g_in, double mu = 100.0) {
    const auto& src = g_in.d;
    int W = g_in.W, H = g_in.H;
    float lo, hi;
    find_percentiles(src, lo, hi);
    float den = std::max(1e-6f, hi - lo);
    float mu_f = (float)mu;
    float log_den = (float)std::log1p(mu);
    Img out(W, H);
    for (size_t i = 0; i < src.size(); ++i) {
        float x = (src[i] - lo) / den;
        x = x < 0 ? 0 : (x > 1 ? 1 : x);
        out.d[i] = (float)std::log1p(mu_f * x) / log_den;
    }
    return out;
}

// gamma: percentiles + gamma en un solo pase.
inline Img enhance_gamma(const Img& g_in, double gamma = 0.45) {
    const auto& src = g_in.d;
    int W = g_in.W, H = g_in.H;
    float lo, hi;
    find_percentiles(src, lo, hi);
    float den = std::max(1e-6f, hi - lo);
    float g = (float)gamma;
    Img out(W, H);
    for (size_t i = 0; i < src.size(); ++i) {
        float x = (src[i] - lo) / den;
        x = x < 0 ? 0 : (x > 1 ? 1 : x);
        out.d[i] = std::pow(x, g);
    }
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
