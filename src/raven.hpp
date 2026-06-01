// raven.hpp - Guardar/Cargar etiquetas en formato Raven Pro (selection table).
// TSV con cabecera estandar; mapea cajas<->(tiempo s, frecuencia Hz). Sin deps.
#pragma once
#include <fstream>
#include <sstream>
#include <string>
#include <vector>
#include <algorithm>
#include <cmath>
#include "types.hpp"

// Convierte coords de espectro (col,row) a tiempo(s)/freq(Hz) y viceversa.
// El espectrograma tiene la fila 0 = freq ALTA, fila H-1 = freq BAJA.
struct RavenGeom {
    int W = 0, H = 0, sr = 0, hop = 0;
    double ny() const { return sr * 0.5; }
    double t_of(int col) const { return sr ? (double)col * hop / sr : 0.0; }
    int    col_of(double t) const { return hop ? (int)std::lround(t * sr / hop) : 0; }
    double f_of(int row) const { return ny() * (double)(H - 1 - row) / std::max(1, H - 1); }
    int    row_of(double f) const { return (H - 1) - (int)std::lround(f / std::max(1e-9, ny()) * (H - 1)); }
};

inline void export_raven(const std::string& path, const std::vector<Det>& dets,
                         const std::vector<LabelClass>& classes, const RavenGeom& g) {
    std::ofstream o(path);
    o << "Selection\tView\tChannel\tBegin Time (s)\tEnd Time (s)\t"
         "Low Freq (Hz)\tHigh Freq (Hz)\tAnnotation\n";
    for (size_t i = 0; i < dets.size(); ++i) {
        const Det& d = dets[i];
        double bt = g.t_of(d.x), et = g.t_of(d.x + d.w);
        double hf = g.f_of(d.y), lf = g.f_of(d.y + d.h);   // y arriba = freq alta
        const char* name = (d.cls >= 0 && d.cls < (int)classes.size())
                               ? classes[d.cls].name.c_str() : "bio";
        o << (i + 1) << "\tSpectrogram 1\t1\t" << bt << "\t" << et << "\t"
          << lf << "\t" << hf << "\t" << name << "\n";
    }
}

// Lee una selection table de Raven. Crea cajas (KIND_BBOX). Las anotaciones
// desconocidas se agregan a `classes` con un color de paleta.
inline std::vector<Det> import_raven(const std::string& path, const RavenGeom& g,
                                     std::vector<LabelClass>& classes) {
    static const unsigned char PAL[][3] = {
        {230,120,60},{200,90,220},{250,210,70},{90,160,250},
        {240,80,120},{120,230,160},{180,180,180},{250,140,180}};
    std::vector<Det> dets;
    std::ifstream f(path);
    if (!f) return dets;
    std::string line;
    if (!std::getline(f, line)) return dets;             // cabecera
    // localizar columnas por nombre
    std::vector<std::string> hd; { std::stringstream ss(line); std::string c;
        while (std::getline(ss, c, '\t')) hd.push_back(c); }
    auto col = [&](const std::string& key) -> int {
        for (size_t i = 0; i < hd.size(); ++i)
            if (hd[i].find(key) != std::string::npos) return (int)i;
        return -1; };
    int ci_bt = col("Begin Time"), ci_et = col("End Time");
    int ci_lf = col("Low Freq"), ci_hf = col("High Freq"), ci_an = col("Annotation");
    if (ci_bt < 0 || ci_et < 0) return dets;
    auto find_or_add = [&](const std::string& name) -> int {
        if (name.empty()) return 0;
        for (size_t i = 0; i < classes.size(); ++i) if (classes[i].name == name) return (int)i;
        const unsigned char* p = PAL[classes.size() % 8];
        classes.push_back({name, p[0], p[1], p[2]});
        return (int)classes.size() - 1; };
    while (std::getline(f, line)) {
        if (line.empty()) continue;
        std::vector<std::string> tok; { std::stringstream ss(line); std::string c;
            while (std::getline(ss, c, '\t')) tok.push_back(c); }
        auto val = [&](int i) -> std::string { return (i >= 0 && i < (int)tok.size()) ? tok[i] : ""; };
        try {
            double bt = std::stod(val(ci_bt)), et = std::stod(val(ci_et));
            double lf = ci_lf >= 0 && !val(ci_lf).empty() ? std::stod(val(ci_lf)) : 0;
            double hf = ci_hf >= 0 && !val(ci_hf).empty() ? std::stod(val(ci_hf)) : g.ny();
            std::string an = val(ci_an);
            Det d; d.kind = KIND_BBOX;
            d.x = std::max(0, g.col_of(bt));
            int x1 = std::max(d.x + 1, g.col_of(et)); d.w = x1 - d.x;
            d.y = std::max(0, g.row_of(hf));
            int y1 = std::max(d.y + 1, g.row_of(lf)); d.h = y1 - d.y;
            d.cls = find_or_add(an);
            d.px = {d.x, d.x + d.w, d.x + d.w, d.x};
            d.py = {d.y, d.y, d.y + d.h, d.y + d.h};
            dets.push_back(std::move(d));
        } catch (...) { /* salta filas mal formadas */ }
    }
    return dets;
}
