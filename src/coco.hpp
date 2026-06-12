// coco.hpp - Exporta/importa etiquetas en formato COCO (deteccion + segmentacion).
#pragma once
#include <fstream>
#include <string>
#include <vector>
#include <algorithm>
#include <iterator>
#include "types.hpp"

struct LabelMeta {
    std::string audio_file = "audio.wav";
    double signal_db = -80;     // dB donde se considera SENAL (piso fijado por el oyente)
    double db_hi = 0;           // techo de dB del filtro
    double f_lo = 0, f_hi = 0;  // banda de frecuencia (Hz)
    int sample_rate = 0;
    std::string mask_file = ""; // BMP de la mascara semantica (opcional)
};

inline void export_coco(const std::string& json_path,
                        const std::string& image_file,
                        int img_w, int img_h,
                        const std::vector<Det>& dets,
                        const LabelMeta& meta,
                        const std::vector<LabelClass>& classes) {
    auto js=[](std::string s){ for(auto&ch:s) if(ch=='\\')ch='/'; return s; };  // JSON-safe (\\ -> /)
    std::string af=js(meta.audio_file), imf=js(image_file), mf=js(meta.mask_file);
    std::ofstream o(json_path);
    o << "{\n";
    o << "  \"info\": {\"description\": \"IIAP SachaAcoustic - etiquetado paisaje sonoro\""
      << ", \"audio_file\": \"" << af << "\""
      << ", \"sample_rate\": " << meta.sample_rate
      << ", \"signal_db_threshold\": " << meta.signal_db    // dB donde se considera senal
      << ", \"db_range\": [" << meta.signal_db << ", " << meta.db_hi << "]"
      << ", \"freq_range_hz\": [" << meta.f_lo << ", " << meta.f_hi << "]";
    if (!mf.empty()) o << ", \"semantic_mask\": \"" << mf << "\"";
    o << "}\n,\n";
    o << "  \"images\": [\n";
    o << "    {\"id\": 1, \"file_name\": \"" << imf << "\", \"width\": "
      << img_w << ", \"height\": " << img_h
      << ", \"signal_db_threshold\": " << meta.signal_db << "}\n";
    o << "  ],\n";
    o << "  \"categories\": [\n";
    for (size_t i = 0; i < classes.size(); ++i) {
        o << "    {\"id\": " << i << ", \"name\": \"" << js(classes[i].name) << "\"}";
        o << (i + 1 < classes.size() ? ",\n" : "\n");
    }
    o << "  ],\n";
    o << "  \"annotations\": [\n";
    for (size_t i = 0; i < dets.size(); ++i) {
        const Det& d = dets[i];
        o << "    {\"id\": " << (i + 1) << ", \"image_id\": 1, \"category_id\": "
          << d.cls << ", \"iscrowd\": 0, \"signal_db_threshold\": " << meta.signal_db
          << ", \"kind\": \"" << (d.kind == KIND_POLY ? "polygon" : "bbox") << "\", ";
        o << "\"bbox\": [" << d.x << ", " << d.y << ", " << d.w << ", " << d.h << "], ";
        o << "\"area\": " << (d.w * d.h) << ", ";
        o << "\"segmentation\": [[";                       // primer poligono = contorno exterior
        for (size_t k = 0; k < d.px.size(); ++k) {
            o << d.px[k] << ", " << d.py[k];
            if (k + 1 < d.px.size()) o << ", ";
        }
        o << "]";
        for (size_t hi = 0; hi < d.hx.size(); ++hi) {       // poligonos siguientes = HUECOS/anillos (estilo COCO multi-poligono)
            o << ", [";
            for (size_t k = 0; k < d.hx[hi].size(); ++k) { o << d.hx[hi][k] << ", " << d.hy[hi][k]; if (k + 1 < d.hx[hi].size()) o << ", "; }
            o << "]";
        }
        o << "]}";
        if (i + 1 < dets.size()) o << ",";
        o << "\n";
    }
    o << "  ]\n}\n";
}

// Importa un COCO escrito por esta app (formato propio, estable). Reemplaza `classes`
// con las categorias del archivo (colores de paleta) y devuelve las detecciones con
// su poligono/caja y clase. Tolerante: parseo por busqueda de claves.
inline std::vector<Det> import_coco(const std::string& json_path, std::vector<LabelClass>& classes) {
    std::ifstream f(json_path);
    std::vector<Det> dets;
    if (!f) return dets;
    std::string s((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    static const unsigned char PAL[][3] = {{60,230,90},{40,220,230},{230,120,60},{200,90,220},
        {250,210,70},{90,160,250},{240,80,120},{120,230,160},{200,200,200},{250,140,180}};
    auto ints_in = [&](size_t a, size_t b) {                 // extrae enteros (con signo) en [a,b)
        std::vector<int> v; long cur = 0; bool in = false, neg = false;
        for (size_t i = a; i < b && i < s.size(); ++i) { char ch = s[i];
            if (ch == '-') { neg = true; in = true; cur = 0; }
            else if (ch >= '0' && ch <= '9') { in = true; cur = cur * 10 + (ch - '0'); }
            else { if (in) v.push_back((int)(neg ? -cur : cur)); in = false; neg = false; cur = 0; } }
        if (in) v.push_back((int)(neg ? -cur : cur)); return v; };
    // --- categorias -> reconstruye la tabla de clases ---
    size_t cpos = s.find("\"categories\"");
    if (cpos != std::string::npos) {
        size_t cend = s.find(']', cpos); if (cend == std::string::npos) cend = s.size();
        std::vector<std::pair<int,std::string>> cats; size_t q = cpos + 12;
        while (q < cend) {
            size_t idp = s.find("\"id\"", q); if (idp == std::string::npos || idp > cend) break;
            auto idv = ints_in(s.find(':', idp) + 1, s.find(',', idp)); int id = idv.empty() ? (int)cats.size() : idv[0];
            size_t np = s.find("\"name\"", idp); if (np == std::string::npos || np > cend) break;
            size_t q1 = s.find('"', s.find(':', np) + 1); size_t q2 = (q1 == std::string::npos) ? q1 : s.find('"', q1 + 1);
            if (q1 == std::string::npos || q2 == std::string::npos) break;
            cats.push_back({id, s.substr(q1 + 1, q2 - q1 - 1)}); q = q2 + 1; }
        if (!cats.empty()) {
            int mx = 0; for (auto& c : cats) mx = std::max(mx, c.first);
            classes.assign((size_t)mx + 1, LabelClass{});
            for (int i = 0; i <= mx; ++i) { const unsigned char* p = PAL[i % 10]; classes[i] = {std::string("cls") + std::to_string(i), p[0], p[1], p[2]}; }
            for (auto& c : cats) { const unsigned char* p = PAL[((c.first) % 10 + 10) % 10]; classes[c.first] = {c.second, p[0], p[1], p[2]}; } }
    }
    // --- anotaciones ---
    size_t apos = s.find("\"annotations\""); if (apos == std::string::npos) return dets;
    size_t cur = apos;
    while (true) {
        size_t cidp = s.find("\"category_id\"", cur); if (cidp == std::string::npos) break;
        auto cv = ints_in(s.find(':', cidp) + 1, s.find(',', cidp)); int cls = cv.empty() ? 0 : cv[0];
        size_t kp = s.find("\"kind\"", cidp); std::string kind = "bbox";
        if (kp != std::string::npos) { size_t q1 = s.find('"', s.find(':', kp) + 1); size_t q2 = (q1==std::string::npos)?q1:s.find('"', q1 + 1);
            if (q1 != std::string::npos && q2 != std::string::npos) kind = s.substr(q1 + 1, q2 - q1 - 1); }
        size_t bp = s.find("\"bbox\"", cidp); if (bp == std::string::npos) break;
        size_t b0 = s.find('[', bp), b1 = s.find(']', bp);
        auto bb = ints_in(b0 + 1, b1);
        size_t sp = s.find("\"segmentation\"", bp);
        Det d; d.cls = cls; d.kind = (kind == "polygon") ? KIND_POLY : KIND_BBOX;
        d.x = bb.size() > 0 ? bb[0] : 0; d.y = bb.size() > 1 ? bb[1] : 0;
        d.w = bb.size() > 2 ? bb[2] : 1; d.h = bb.size() > 3 ? bb[3] : 1;
        size_t adv = b1;
        if (sp != std::string::npos) {
            size_t segOpen = s.find('[', sp), segEnd = s.find("]]", sp);    // array de segmentation: [ [ext], [hueco1], ... ]
            if (segOpen != std::string::npos && segEnd != std::string::npos && segEnd > segOpen) {
                size_t inner = segOpen + 1; int polyIdx = 0;
                while (true) {
                    size_t a = s.find('[', inner); if (a == std::string::npos || a > segEnd) break;
                    size_t b = s.find(']', a);      if (b == std::string::npos || b > segEnd + 1) break;
                    auto v = ints_in(a + 1, b);
                    if (polyIdx == 0) { for (size_t k = 0; k + 1 < v.size(); k += 2) { d.px.push_back(v[k]); d.py.push_back(v[k + 1]); } }
                    else { std::vector<int> hx, hy; for (size_t k = 0; k + 1 < v.size(); k += 2) { hx.push_back(v[k]); hy.push_back(v[k + 1]); }
                        if (hx.size() >= 3) { d.hx.push_back(std::move(hx)); d.hy.push_back(std::move(hy)); } }   // huecos/anillos
                    ++polyIdx; inner = b + 1;
                }
                adv = segEnd + 2;
            }
        }
        if (d.px.size() < 3) { d.px = {d.x, d.x + d.w, d.x + d.w, d.x}; d.py = {d.y, d.y, d.y + d.h, d.y + d.h}; d.kind = KIND_BBOX; }
        dets.push_back(std::move(d));
        cur = adv;
    }
    return dets;
}
