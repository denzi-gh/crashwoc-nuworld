#include "port/text3d.h"

#include <SDL3/SDL.h>

/* Uppercase Latin-1 characters in the original Text3D glyph encoding. */
static unsigned char nw_text3d_glyph(Uint32 c)
{
    static const unsigned char accents[] = {
        0xb7, 0xb5, 0xb6, 0xc7, 0x8e, 0, 0, 0x80,
        0xd4, 0x90, 0xd2, 0xd3, 0xde, 0xd6, 0xd7, 0xd8,
        0, 0xa5, 0xe3, 0xe0, 0xe2, 0xe5, 0x99, 0,
        0, 0xeb, 0xe9, 0xea, 0x9a,
    };
    if (c >= 0x20 && c <= 0x7e && c != '#') {
        return (unsigned char)c;
    }
    if (c >= 0xc0 && c - 0xc0 < SDL_arraysize(accents)) {
        return accents[c - 0xc0];
    }
    switch (c) {
        case 0xa1: return 0xad; /* ¡ */
        case 0xbf: return 0xa8; /* ¿ */
        case 0xba: return 0xf8; /* º */
        default: return 0;
    }
}

bool nw_text3d_encode(const char *text, char *out, size_t size)
{
    if (!text || !size) return false;
    size_t n = 0;
    while (*text) {
        Uint32 c = SDL_StepUTF8(&text, NULL);
        if (c == 0xdf) { /* Uppercase sharp S is SS in the original font. */
            if (n + 2 >= size) {
                return false;
            }
            out[n++] = 'S';
            out[n++] = 'S';
            continue;
        }
        if ((c >= 'a' && c <= 'z') || (c >= 0xe0 && c <= 0xfe && c != 0xf7)) {
            c -= 0x20;
        }
        unsigned char glyph = nw_text3d_glyph(c);
        if (!glyph || n + 1 >= size) {
            return false;
        }
        out[n++] = (char)glyph;
    }
    out[n] = '\0';
    return n != 0;
}
