#include "ps2icon.h"
#include "sjis.h"
#include <string.h>
#include <stdlib.h>

void ps2icon_parse_title(const uint8_t *icon_sys, size_t icon_sys_size,
                         char *out_title, size_t out_len)
{
    memset(out_title, 0, out_len);
    if (!icon_sys || icon_sys_size < 0xC0 + 68) return;

    const uint8_t *src = icon_sys + 0xC0;
    int len = 68;

    /* Trim trailing spaces and nulls */
    while (len > 0 && (src[len - 1] == ' ' || src[len - 1] == '\0'))
        len--;
    if (len <= 0) return;

    /* Detect SJIS */
    int has_sjis = 0;
    for (int i = 0; i < len; i++) {
        if (src[i] >= 0x80) {
            has_sjis = 1;
            break;
        }
    }

    if (has_sjis) {
        sjis_to_utf8((const char *)src, len, out_title, out_len);
    } else {
        if ((size_t)len >= out_len) len = (int)out_len - 1;
        memcpy(out_title, src, len);
        out_title[len] = '\0';
    }
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
