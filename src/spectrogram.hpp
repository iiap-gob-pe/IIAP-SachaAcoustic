// spectrogram.hpp - STFT -> espectrograma dB normalizado [0,1] (sin OpenCV).
// Img resultante: fila 0 = freq ALTA, ultima fila = freq BAJA (vista estandar).
#pragma once
#include <vector>
#include <stdexcept>
#include "types.hpp"
#include "fft.hpp"
#include "wav.hpp"

struct SpecParams {
    int nfft = 1024;
    int hop = 256;
    double dyn_range_db = 80.0;
};

inline Img compute_spectrogram(const AudioData& a, const SpecParams& P) {
    const auto& x = a.samples;
    const int nfft = P.nfft, hop = P.hop, nbins = nfft / 2 + 1;
    if (!is_pow2((size_t)nfft)) throw std::runtime_error("nfft debe ser potencia de 2");
    if ((int)x.size() < nfft) throw std::runtime_error("Audio mas corto que nfft");
    const int nframes = 1 + (int)((x.size() - nfft) / hop);

    std::vector<float> win(nfft);  // Hann
    for (int i = 0; i < nfft; ++i)
        win[i] = 0.5f * (1.0f - std::cos(2.0f * (float)M_PI * i / (nfft - 1)));

    // S[bin][frame] en dB (bin 0 = freq baja)
    Img S(nframes, nbins);
    std::vector<Cf> buf(nfft);
    float mx = -1e30f;
    for (int t = 0; t < nframes; ++t) {
        const int off = t * hop;
        for (int i = 0; i < nfft; ++i) buf[i] = Cf(x[off + i] * win[i], 0.0f);
        fft(buf);
        for (int b = 0; b < nbins; ++b) {
            float re = buf[b].real(), im = buf[b].imag();
            float db = 10.0f * std::log10(re * re + im * im + 1e-12f);
            S.at(b, t) = db;
            if (db > mx) mx = db;
        }
    }
    // normalizar a [0,1] con rango dinamico fijo y voltear (freq baja abajo)
    float lo = mx - (float)P.dyn_range_db;
    Img out(nframes, nbins);
    for (int b = 0; b < nbins; ++b) {
        int yr = nbins - 1 - b;  // voltear vertical
        for (int t = 0; t < nframes; ++t) {
            float v = (S.at(b, t) - lo) / (mx - lo + 1e-9f);
            v = v < 0 ? 0 : (v > 1 ? 1 : v);
            out.at(yr, t) = v;
        }
    }
    return out;  // 0..1
}
