// bmp.hpp - Lee/escribe RGBImg a BMP de 24 bits (sin dependencias).
#pragma once
#include <cstdint>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>
#include "types.hpp"

inline bool write_bmp(const std::string& path, const RGBImg& im) {
    std::ofstream f(path, std::ios::binary);
    if (!f) return false;
    int W = im.W, H = im.H;
    int row = W * 3;
    int pad = (4 - (row % 4)) % 4;
    int data = (row + pad) * H;
    int fsize = 54 + data;

    auto u16 = [&](uint16_t v) { f.put(v & 0xFF); f.put((v >> 8) & 0xFF); };
    auto u32 = [&](uint32_t v) { for (int i = 0; i < 4; ++i) f.put((v >> (8 * i)) & 0xFF); };

    f.put('B'); f.put('M'); u32(fsize); u32(0); u32(54);          // file header
    u32(40); u32(W); u32(H); u16(1); u16(24);                     // info header
    u32(0); u32(data); u32(2835); u32(2835); u32(0); u32(0);

    std::vector<uint8_t> padb(pad, 0);
    for (int y = H - 1; y >= 0; --y) {                            // BMP: abajo->arriba
        for (int x = 0; x < W; ++x) {
            size_t i = ((size_t)y * W + x) * 3;
            f.put(im.d[i + 2]); f.put(im.d[i + 1]); f.put(im.d[i + 0]);  // BGR
        }
        if (pad) f.write((char*)padb.data(), pad);
    }
    return true;
}

// Lee un BMP de 24 bits y devuelve RGBImg. Si falla, devuelve imagen vacia.
inline RGBImg read_bmp(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return {};
    // Cabecera
    char hdr[54];
    f.read(hdr, 54);
    if (!f || hdr[0] != 'B' || hdr[1] != 'M') return {};
    int W = *(int*)&hdr[18];
    int H = *(int*)&hdr[22];
    int bpp = *(uint16_t*)&hdr[28];
    if (W <= 0 || H <= 0 || bpp != 24) return {};
    int row = W * 3;
    int pad = (4 - (row % 4)) % 4;
    RGBImg out(W, H);
    for (int y = H - 1; y >= 0; --y) {
        for (int x = 0; x < W; ++x) {
            uint8_t bgr[3];
            f.read((char*)bgr, 3);
            if (!f) return {};
            size_t i = ((size_t)y * W + x) * 3;
            out.d[i] = bgr[2]; out.d[i + 1] = bgr[1]; out.d[i + 2] = bgr[0]; // BGR -> RGB
        }
        if (pad) f.seekg(pad, std::ios::cur);
    }
    return out;
}
