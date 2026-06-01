// audio_play.hpp - Reproduccion asincrona de un buffer float [-1,1] (winmm),
// con posicion (para el playhead), pausa y reanudar.
#pragma once
#define NOMINMAX
#include <windows.h>
#include <mmsystem.h>
#include <vector>
#include <cstdint>

struct AudioPlayer {
    HWAVEOUT hwo = 0;
    WAVEHDR hdr{};
    std::vector<int16_t> buf;
    bool playing = false, paused = false;
    int sr = 0;
    size_t i0 = 0;          // muestra absoluta donde empezo el buffer
    size_t nsamples = 0;    // longitud del buffer reproducido

    void stop() {
        if (hwo) {
            waveOutReset(hwo);
            waveOutUnprepareHeader(hwo, &hdr, sizeof(hdr));
            waveOutClose(hwo);
            hwo = 0;
        }
        playing = false; paused = false;
    }

    // Reproduce ya un buffer de muestras 16-bit (mono) desde la muestra abs i0.
    void play_buffer(std::vector<int16_t>&& b, int srate, size_t abs_i0) {
        stop();
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
        return (long long)i0 + mt.u.sample;
    }
};
