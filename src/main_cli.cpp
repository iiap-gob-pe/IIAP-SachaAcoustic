// main_cli.cpp - Prueba del nucleo SIN GUI: WAV -> espectro -> asinh ->
// autoetiquetado -> BMP con cajas + COCO. Para verificar la cadena.
#include <iostream>
#include <string>
#include "wav.hpp"
#include "spectrogram.hpp"
#include "enhance.hpp"
#include "autolabel.hpp"
#include "coco.hpp"
#include "bmp.hpp"

static void draw_rect(RGBImg& im, const Det& d, uint8_t r, uint8_t g, uint8_t b) {
    auto px = [&](int x, int y) {
        if (x < 0 || x >= im.W || y < 0 || y >= im.H) return;
        size_t i = ((size_t)y * im.W + x) * 3;
        im.d[i] = r; im.d[i + 1] = g; im.d[i + 2] = b;
    };
    for (int x = d.x; x < d.x + d.w; ++x) { px(x, d.y); px(x, d.y + d.h - 1); }
    for (int y = d.y; y < d.y + d.h; ++y) { px(d.x, y); px(d.x + d.w - 1, y); }
}

int main(int argc, char** argv) {
    if (argc < 2) { std::cerr << "Uso: cli <audio.wav> [salida_base]\n"; return 1; }
    std::string base = (argc >= 3) ? argv[2] : "salida_cli";
    try {
        AudioData a = load_wav(argv[1]);
        std::cout << "WAV: " << a.samples.size() << " muestras @ " << a.sample_rate << " Hz\n";
        Img spec = compute_spectrogram(a, SpecParams{});
        std::cout << "Espectro: " << spec.W << "x" << spec.H << "\n";
        Img enh = enhance_asinh(spec, 0.07);
        // Detectar sobre el espectro CRUDO (SNR por banda real), no sobre el
        // realzado (asinh comprime el contraste).
        std::vector<Det> dets = auto_label(spec, 4.0f, 25);
        std::cout << "Detecciones: " << dets.size() << "\n";
        RGBImg color = colorize_magma(enh);
        for (auto& d : dets) {
            if (d.cls == CLASE_ANTRO) draw_rect(color, d, 0, 255, 255);
            else draw_rect(color, d, 0, 255, 0);
        }
        write_bmp(base + ".bmp", color);
        LabelMeta m; m.audio_file=argv[1]; m.sample_rate=a.sample_rate;
        m.signal_db=-80; m.db_hi=0; m.f_lo=0; m.f_hi=a.sample_rate*0.5;   // sin umbral manual en CLI
        export_coco(base + ".json", base + ".bmp", spec.W, spec.H, dets, m, default_classes());
        std::cout << "Guardado: " << base << ".bmp y " << base << ".json\n";
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << "\n"; return 2;
    }
    return 0;
}
