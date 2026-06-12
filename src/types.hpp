// types.hpp - tipos base sin dependencias.
#pragma once
#include <vector>
#include <string>
#include <cstdint>

// Imagen float (fila-mayor). at(y,x).
struct Img {
    int W = 0, H = 0;
    std::vector<float> d;
    Img() {}
    Img(int w, int h) : W(w), H(h), d((size_t)w * h, 0.f) {}
    float& at(int y, int x) { return d[(size_t)y * W + x]; }
    float at(int y, int x) const { return d[(size_t)y * W + x]; }
};

// Buffer RGB 8-bit (fila-mayor, 3 canales).
struct RGBImg {
    int W = 0, H = 0;
    std::vector<uint8_t> d;  // R,G,B por pixel
    RGBImg() {}
    RGBImg(int w, int h) : W(w), H(h), d((size_t)w * h * 3, 0) {}
};

// Tipo de forma de una deteccion.
enum DetKind { KIND_BBOX = 0, KIND_POLY = 1 };

// Deteccion: caja + poligono de segmentacion (coords del espectro).
struct Det {
    int x = 0, y = 0, w = 0, h = 0;       // bounding box
    std::vector<int> px, py;              // poligono exterior (segmentacion)
    std::vector<std::vector<int>> hx, hy; // HUECOS/anillos: cada par hx[i]/hy[i] es el contorno de un hueco (area que NO es la clase)
    int cls = 0;                          // indice en la tabla de clases (0=bio, 1=antro por defecto)
    int kind = KIND_BBOX;                 // KIND_BBOX o KIND_POLY
};

// Clase/etiqueta dinamica: nombre + color RGB de dibujo.
struct LabelClass {
    std::string name;
    unsigned char r = 0, g = 255, b = 0;
};

// Tabla de clases por defecto (bio=verde, antro=cian). El indice de Det.cls
// referencia a esta tabla; la app puede agregar mas en caliente.
inline std::vector<LabelClass> default_classes() {
    return {
        {"bio",   60, 230,  90},
        {"antro", 40, 220, 230},
    };
}

inline const char* nombre_clase(int c) { return c == 1 ? "antro" : "bio"; }
