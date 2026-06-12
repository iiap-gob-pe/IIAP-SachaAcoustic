// audio_play.hpp - Reproduccion asincrona de un buffer float [-1,1] (winmm),
// con posicion (para el playhead), pausa y reanudar.
#pragma once
#define NOMINMAX
#include <windows.h>
#include <mmsystem.h>
#include <vector>
#include <cstdint>
#include <cmath>

struct AudioPlayer {
    HWAVEOUT hwo = 0;
    WAVEHDR hdr{};
    std::vector<int16_t> buf;
    bool playing = false, paused = false;
    int sr = 0;
    size_t i0 = 0;          // muestra absoluta donde empezo el buffer (en el sr ORIGINAL)
    size_t nsamples = 0;    // longitud del buffer reproducido (en el sr de reproduccion)
    double rate_ratio = 1.0; // muestras_originales / muestras_reproducidas (por remuestreo)

    void stop() {
        if (hwo) {
            waveOutReset(hwo);
            waveOutUnprepareHeader(hwo, &hdr, sizeof(hdr));
            waveOutClose(hwo);
            hwo = 0;
        }
        playing = false; paused = false;
    }

    // Reproduce ya un buffer de muestras 16-bit (mono) desde la muestra abs i0, a 'speed'x.
    // speed<1 = mas lento (y mas grave): util como EXPANSION TEMPORAL para oir el ultrasonido
    // (al reproducir lento, las altas frecuencias bajan a la banda audible). speed>1 = mas rapido.
    void play_buffer(std::vector<int16_t>&& b, int srate, size_t abs_i0, double speed = 1.0) {
        stop();
        // Reproducimos siempre a 48 kHz (tasa universal). El REMUESTREO con un FILTRO PASO-BAJO
        // anti-alias (sinc enventanado, Hann) hace dos cosas: (1) baja tasas altas (96/192/384 kHz)
        // sin que el ultrasonido se "pliegue" (aliasing) y (2) aplica la VELOCIDAD. El paso efectivo
        // step = srate*speed/48000: 1 muestra reproducida = 'step' muestras originales (= rate_ratio,
        // que corrige el playhead). step>1 decima (corte anti-alias); step<=1 interpola (banda completa).
        const int MAXR = 48000;
        rate_ratio = 1.0;
        bool needResample = (srate > MAXR || speed != 1.0);
        if (needResample && b.size() > 2) {
            double step = (double)srate * speed / (double)MAXR;     // muestras de entrada por muestra de salida
            if (step < 1e-6) step = 1e-6;
            size_t outN = (size_t)(b.size() / step); if (outN < 1) outN = 1;
            double fc = 0.45 / (step > 1.0 ? step : 1.0);           // corte = Nyquist de salida (rel. a la entrada)
            int half = (int)(step * 12.0); if (half < 12) half = 12; if (half > 48) half = 48;
            int taps = 2 * half + 1;
            const int NPH = 64;                              // banco POLIFASE: kernel sinc/Hann PRECALCULADO por fase
            const double PI = 3.14159265358979323846;        // (evita sin/cos por muestra -> sin lag en audios largos)
            std::vector<double> ker((size_t)NPH * taps);     // ker[fase*taps+m], normalizado (ganancia DC=1)
            for (int p = 0; p < NPH; ++p) {
                double frac = (double)p / NPH, sum = 0.0;
                for (int m = 0; m < taps; ++m) {
                    double x = (double)(m - half) - frac;                                  // distancia al centro (muestras entrada)
                    double sinc = (std::fabs(x) < 1e-9) ? 2.0 * fc : std::sin(2.0 * PI * fc * x) / (PI * x);
                    double win = 0.5 + 0.5 * std::cos(PI * x / (half + 1));                 // ventana de Hann
                    double w = sinc * win; ker[(size_t)p * taps + m] = w; sum += w;
                }
                if (sum != 0.0) for (int m = 0; m < taps; ++m) ker[(size_t)p * taps + m] /= sum;
            }
            std::vector<int16_t> rb(outN); long n = (long)b.size();
            for (size_t j = 0; j < outN; ++j) {
                double center = (double)j * step; long base = (long)std::floor(center);
                int p = (int)((center - base) * NPH + 0.5); if (p >= NPH) { p -= NPH; base += 1; }   // fase mas cercana
                const double* kp = &ker[(size_t)p * taps]; double acc = 0.0; long k0 = base - half;
                for (int m = 0; m < taps; ++m) { long k = k0 + m; if (k >= 0 && k < n) acc += (double)b[k] * kp[m]; }
                if (acc > 32767.0) acc = 32767.0; if (acc < -32768.0) acc = -32768.0;
                rb[j] = (int16_t)acc;
            }
            rate_ratio = step;                               // 1 muestra reproducida = step muestras originales
            b = std::move(rb); srate = MAXR;
        }
        buf = std::move(b); sr = srate; i0 = abs_i0; nsamples = buf.size();
        if (buf.empty()) return;
        WAVEFORMATEX wf{};
        wf.wFormatTag = WAVE_FORMAT_PCM; wf.nChannels = 1;
        wf.nSamplesPerSec = srate; wf.wBitsPerSample = 16;
        wf.nBlockAlign = 2; wf.nAvgBytesPerSec = srate * 2;
        if (waveOutOpen(&hwo, WAVE_MAPPER, &wf, 0, 0, CALLBACK_NULL) != MMSYSERR_NOERROR) {
            hwo = 0; return;
        }
        hdr = WAVEHDR{};
        hdr.lpData = (LPSTR)buf.data();
        hdr.dwBufferLength = (DWORD)(buf.size() * sizeof(int16_t));
        waveOutPrepareHeader(hwo, &hdr, sizeof(hdr));
        waveOutWrite(hwo, &hdr, sizeof(hdr));
        playing = true; paused = false;
    }

    // Reproduce s[a..b) (float). Si b<=a reproduce todo.
    void play(const std::vector<float>& s, int srate, size_t a = 0, size_t b = 0) {
        if (s.empty()) return;
        if (b <= a || b > s.size()) b = s.size();
        if (a >= s.size()) a = 0;
        std::vector<int16_t> pcm(b - a);
        for (size_t i = a; i < b; ++i) {
            float v = s[i]; v = v < -1 ? -1 : (v > 1 ? 1 : v);
            pcm[i - a] = (int16_t)(v * 32767);
        }
        play_buffer(std::move(pcm), srate, a);
    }

    void pause_toggle() {
        if (!hwo || !playing) return;
        if (paused) { waveOutRestart(hwo); paused = false; }
        else { waveOutPause(hwo); paused = true; }
    }

    // muestra absoluta actual de reproduccion (para el playhead)
    long long cur_sample() const {
        if (!hwo || !playing) return -1;
        MMTIME mt{}; mt.wType = TIME_SAMPLES;
        waveOutGetPosition((HWAVEOUT)hwo, &mt, sizeof(mt));
        if (mt.wType != TIME_SAMPLES) return -1;
        if (mt.u.sample >= nsamples) return -1;   // termino
        // mt.u.sample esta en el dominio REPRODUCIDO; lo escalamos al dominio ORIGINAL
        // (donde estan i0, hop y las columnas del espectro) con rate_ratio.
        return (long long)i0 + (long long)(mt.u.sample * rate_ratio);
    }
};
