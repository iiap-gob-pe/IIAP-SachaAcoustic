// wav.hpp - Lector minimo de WAV (RIFF). Soporta PCM 16/24/32 y float32.
// Devuelve audio mono (mezcla de canales) en float [-1,1] + sample rate.
#pragma once
#include <cstdint>
#include <cstring>
#include <fstream>
#include <stdexcept>
#include <string>
#include <vector>

struct AudioData {
    std::vector<float> samples;  // mono
    int sample_rate = 0;
};

inline AudioData load_wav(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) throw std::runtime_error("No se pudo abrir WAV: " + path);

    auto rd_u32 = [&](void) { uint32_t v; f.read((char*)&v, 4); return v; };
    auto rd_u16 = [&](void) { uint16_t v; f.read((char*)&v, 2); return v; };

    char riff[4]; f.read(riff, 4);
    if (std::strncmp(riff, "RIFF", 4) != 0) throw std::runtime_error("No es RIFF/WAV");
    rd_u32();  // tamano total
    char wave[4]; f.read(wave, 4);
    if (std::strncmp(wave, "WAVE", 4) != 0) throw std::runtime_error("No es WAVE");

    uint16_t fmt = 0, channels = 0, bits = 0;
    uint32_t rate = 0;
    std::vector<uint8_t> data;

    while (f && f.peek() != EOF) {
        char id[4]; f.read(id, 4);
        if (f.gcount() < 4) break;
        uint32_t sz = rd_u32();
        if (std::strncmp(id, "fmt ", 4) == 0) {
            fmt = rd_u16(); channels = rd_u16(); rate = rd_u32();
            rd_u32();        // byte rate
            rd_u16();        // block align
            bits = rd_u16();
            if (sz > 16) f.seekg(sz - 16, std::ios::cur);  // extension
        } else if (std::strncmp(id, "data", 4) == 0) {
            data.resize(sz);
            f.read((char*)data.data(), sz);
        } else {
            f.seekg(sz + (sz & 1), std::ios::cur);  // chunk desconocido (par)
            continue;
        }
        if (sz & 1) f.seekg(1, std::ios::cur);  // padding a par
    }
    if (channels == 0 || rate == 0 || data.empty())
        throw std::runtime_error("WAV sin fmt/data validos");

    AudioData out; out.sample_rate = (int)rate;
    size_t bytes_per_sample = bits / 8;
    size_t total = data.size() / (bytes_per_sample * channels);
    out.samples.reserve(total);

    const uint8_t* p = data.data();
    for (size_t i = 0; i < total; ++i) {
        float acc = 0.f;
        for (int c = 0; c < channels; ++c) {
            float v = 0.f;
            if (fmt == 3 && bits == 32) {              // IEEE float32
                float s; std::memcpy(&s, p, 4); v = s;
            } else if (bits == 16) {                    // PCM16
                int16_t s; std::memcpy(&s, p, 2); v = s / 32768.f;
            } else if (bits == 24) {                    // PCM24
                int32_t s = (p[0]) | (p[1] << 8) | (p[2] << 16);
                if (s & 0x800000) s |= ~0xFFFFFF;       // signo
                v = s / 8388608.f;
            } else if (bits == 32) {                    // PCM32
                int32_t s; std::memcpy(&s, p, 4); v = s / 2147483648.f;
            } else {
                throw std::runtime_error("Formato WAV no soportado: bits=" +
                                         std::to_string(bits));
            }
            acc += v;
            p += bytes_per_sample;
        }
        out.samples.push_back(acc / channels);  // mezcla a mono
    }
    return out;
}
