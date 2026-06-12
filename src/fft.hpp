// fft.hpp - FFT radix-2 iterativa (Cooley-Tukey) para tamanos potencia de 2.
// Twiddle factors pre-computados y cacheados por tamano.
#pragma once
#include <complex>
#include <vector>
#include <cmath>
#include <unordered_map>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

using Cf = std::complex<float>;

inline bool is_pow2(size_t n) { return n && !(n & (n - 1)); }

// Twiddle factors cacheados: por cada tamano de FFT, se almacenan
// los factores wlen^k para cada stage (len=2,4,8,...,n).
struct TwiddleCache {
    std::unordered_map<size_t, std::vector<Cf>> data;
};
static TwiddleCache g_twiddles;

inline void precompute_twiddles(size_t n) {
    if (g_twiddles.data.count(n)) return;
    auto& table = g_twiddles.data[n];
    // Para cada stage: len = 2, 4, 8, ..., n
    // En cada stage necesitamos len/2 twiddles (wlen^0 .. wlen^(len/2-1))
    for (size_t len = 2; len <= n; len <<= 1) {
        float ang = -2.0f * (float)M_PI / (float)len;
        Cf wlen(std::cos(ang), std::sin(ang));
        Cf w(1.0f, 0.0f);
        for (size_t k = 0; k < len / 2; ++k) {
            table.push_back(w);
            w *= wlen;
        }
    }
}

inline void fft(std::vector<Cf>& a);  // fwd decl

// FFT inversa (via conjugado). Normaliza por N.
inline void ifft(std::vector<Cf>& a) {
    for (auto& z : a) z = std::conj(z);
    fft(a);
    float inv = 1.0f / (float)a.size();
    for (auto& z : a) z = std::conj(z) * inv;
}

inline void fft(std::vector<Cf>& a) {
    size_t n = a.size();
    // permutacion bit-reversa
    for (size_t i = 1, j = 0; i < n; ++i) {
        size_t bit = n >> 1;
        for (; j & bit; bit >>= 1) j ^= bit;
        j ^= bit;
        if (i < j) std::swap(a[i], a[j]);
    }
    // twiddle factors pre-computados
    precompute_twiddles(n);
    const auto& tbl = g_twiddles.data[n];
    size_t tbl_idx = 0;
    for (size_t len = 2; len <= n; len <<= 1) {
        for (size_t i = 0; i < n; i += len) {
            for (size_t k = 0; k < len / 2; ++k) {
                Cf w = tbl[tbl_idx + k];
                Cf u = a[i + k];
                Cf v = a[i + k + len / 2] * w;
                a[i + k] = u + v;
                a[i + k + len / 2] = u - v;
            }
        }
        tbl_idx += len / 2;
    }
}
