// fft.hpp - FFT radix-2 iterativa (Cooley-Tukey) para tamanos potencia de 2.
#pragma once
#include <complex>
#include <vector>
#include <cmath>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

using Cf = std::complex<float>;

inline bool is_pow2(size_t n) { return n && !(n & (n - 1)); }

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
    for (size_t len = 2; len <= n; len <<= 1) {
        float ang = -2.0f * (float)M_PI / (float)len;
        Cf wlen(std::cos(ang), std::sin(ang));
        for (size_t i = 0; i < n; i += len) {
            Cf w(1.0f, 0.0f);
            for (size_t k = 0; k < len / 2; ++k) {
                Cf u = a[i + k];
                Cf v = a[i + k + len / 2] * w;
                a[i + k] = u + v;
                a[i + k + len / 2] = u - v;
                w *= wlen;
            }
        }
    }
}
