// glfont.hpp - Texto en OpenGL via wglUseFontBitmaps (Win32).
#pragma once
#define NOMINMAX
#include <windows.h>
#include <GL/gl.h>
#include <string>

struct GLFont {
    GLuint base = 0;
    void init(HDC hdc) {
        HFONT f = CreateFontA(-14, 0, 0, 0, FW_NORMAL, 0, 0, 0, ANSI_CHARSET,
                              OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                              ANTIALIASED_QUALITY, FF_DONTCARE, "Consolas");
        HGDIOBJ old = SelectObject(hdc, f);
        base = glGenLists(96);
        wglUseFontBitmaps(hdc, 32, 96, base);
        SelectObject(hdc, old);
        DeleteObject(f);
    }
    // dibuja en la posicion de raster actual (fijada por glRasterPos*)
    void emit(const std::string& s) const {
        glPushAttrib(GL_LIST_BIT);
        glListBase(base - 32);
        glCallLists((GLsizei)s.size(), GL_UNSIGNED_BYTE, s.c_str());
        glPopAttrib();
    }
    void at2d(float x, float y, const std::string& s) const {
        glRasterPos2f(x, y); emit(s);
    }
    void at3d(float x, float y, float z, const std::string& s) const {
        glRasterPos3f(x, y, z); emit(s);
    }
};
