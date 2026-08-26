#include "ps2icon.h"
#include "sjis.h"
#include <string.h>
#include <stdlib.h>

void ps2icon_parse_title(const uint8_t *icon_sys, size_t icon_sys_size,
                         char *out_title, size_t out_len)
{
    out[0] = '\0';
    if (!icon_sys || icon_sys_size < 0xC0 + 1) return;

    /* Verify magic */
    if (memcmp(icon_sys, "PS2D", 4) != 0) return;

    /* Title is at offset 0xC0, max 68 bytes, null-terminated S-JIS */
    const uint8_t *title_src = icon_sys + 0xC0;
    int max_title = 68;
    if (icon_sys_size < 0xC0 + max_title)
        max_title = icon_sys_size - 0xC0;

    int j = 0;
    for (int i = 0; i < max_title && j < out_len - 1; i++) {
        uint8_t c = title_src[i];
        if (c == '\0') break;

        if (c < 0x80) {
            /* Direct ASCII (half-width) */
            out[j++] = c;
        } else if (i + 1 < max_title) {
            /* S-JIS full-width -> ASCII conversion */
            uint16_t sjis = (c << 8) | title_src[i + 1];
            if (sjis >= 0x8140 && sjis <= 0x817E) {
                out[j++] = sjis - 0x8140 + 0x20;        /* ! through ~ */
            } else if (sjis >= 0x8180 && sjis <= 0x818E) {
                out[j++] = sjis - 0x8180 + 0x7F;        /* DEL area */
            } else if (sjis >= 0x824F && sjis <= 0x8258) {
                out[j++] = sjis - 0x824F + '0';         /* 0-9 */
            } else if (sjis >= 0x8260 && sjis <= 0x8279) {
                out[j++] = sjis - 0x8260 + 'A';         /* A-Z */
            } else if (sjis >= 0x8281 && sjis <= 0x829A) {
                out[j++] = sjis - 0x8281 + 'a';         /* a-z */
            } else if (sjis == 0x8140) {
                out[j++] = ' ';                          /* full-width space */
            } else if (sjis == 0x8143) {
                out[j++] = '-';                          /* full-width dash */
            } else if (sjis == 0x8144) {
                out[j++] = '-';                          /* full-width minus */
            } else if (sjis == 0x8146) {
                out[j++] = ':';                          /* full-width colon */
            } else if (sjis == 0x8147) {
                out[j++] = ';';                          /* full-width semicolon */
            } else if (sjis == 0x8148) {
                out[j++] = '?';                          /* full-width question */
            } else if (sjis == 0x8149) {
                out[j++] = '!';                          /* full-width exclamation */
            } else if (sjis == 0x814A) {
                out[j++] = '/';                          /* full-width slash */
            } else if (sjis == 0x815E) {
                out[j++] = '/';                          /* full-width divide */
            } else {
                out[j++] = '?';                          /* unknown S-JIS */
            }
            i++; /* consumed 2 bytes */
        }
    }
    out[j] = '\0';
}

int ps2icon_decode(const uint8_t *ico_data, size_t ico_size,
                   uint32_t **out_rgba, int *out_w, int *out_h)
{
    *out_rgba = NULL;
    *out_w = 0;
    *out_h = 0;

    if (!ico_data || ico_size < 160) return 0;

    /* Standard PS2 icon: 16x16, 4bpp, 16-color RGB555 palette
     * 32 bytes palette (16 x 2 bytes) + 128 bytes bitmap */
    if (ico_size >= 160) {
        const uint16_t *palette = (const uint16_t *)ico_data;
        const uint8_t  *bitmap  = ico_data + 32;

        int w = 16, h = 16;
        uint32_t *rgba = (uint32_t *)malloc(w * h * sizeof(uint32_t));
        if (!rgba) return 0;

        for (int y = 0; y < h; y++) {
            for (int x = 0; x < w; x++) {
                int idx = y * w + x;
                int byte_idx = idx / 2;
                int shift = (idx & 1) ? 0 : 4;
                int nibble = (bitmap[byte_idx] >> shift) & 0x0F;

                uint16_t p = palette[nibble];

                /* RGB555 -> ARGB8888
                 * Bit 15 = transparency (1 = opaque, 0 = transparent) */
                uint8_t r = ((p >> 10) & 0x1F) << 3;
                uint8_t g = ((p >>  5) & 0x1F) << 3;
                uint8_t b = ((p >>  0) & 0x1F) << 3;
                uint8_t a = (p & 0x8000) ? 0xFF : 0x00;

                /* Fill lower bits for smoother color ramps */
                r |= (r >> 5);
                g |= (g >> 5);
                b |= (b >> 5);

                rgba[idx] = ((uint32_t)a << 24) | ((uint32_t)r << 16) |
                            ((uint32_t)g << 8) | (uint32_t)b;
            }
        }

        *out_rgba = rgba;
        *out_w = w;
        *out_h = h;
        return 1;
    }

    return 0;
}
