#include "ps2icon.h"
#include "sjis.h"
#include <string.h>
#include <stdlib.h>
#include "mcio.h"




void ps2icon_parse_title(const uint8_t *icon_sys, size_t icon_sys_size,
                         char *out_title, size_t out_len)
{
    out_title[0] = '\0';
    if (!icon_sys || icon_sys_size < 0xC1) return;
    if (memcmp(icon_sys, "PS2D", 4) != 0) return;

    const uint8_t *src = icon_sys + 0xC0;
    int max = 68;
    if (icon_sys_size < 0xC0 + (size_t)max)
        max = (int)(icon_sys_size - 0xC0);

    int j = 0;
    for (int i = 0; i < max && j < (int)out_len - 1; i++) {
        uint8_t c = src[i];
        if (c == '\0') break;

        if (c < 0x80) {
            /* Half-width ASCII / katakana / control — pass through */
            out_title[j++] = (char)c;
        } else if (i + 1 < max) {
            uint16_t sjis = ((uint16_t)c << 8) | src[i + 1];
            char ascii = '?';

            /* Fullwidth numbers ０-９ */
            if (sjis >= 0x824F && sjis <= 0x8258)
                ascii = (char)('0' + (sjis - 0x824F));
            /* Fullwidth A-Z */
            else if (sjis >= 0x8260 && sjis <= 0x8279)
                ascii = (char)('A' + (sjis - 0x8260));
            /* Fullwidth a-z */
            else if (sjis >= 0x8281 && sjis <= 0x829A)
                ascii = (char)('a' + (sjis - 0x8281));
            /* Fullwidth space */
            else if (sjis == 0x8140)
                ascii = ' ';
            /* Fullwidth period ． */
            else if (sjis == 0x8144)
                ascii = '.';
            /* Fullwidth comma ， */
            else if (sjis == 0x8143)
                ascii = ',';
            /* Fullwidth colon ： */
            else if (sjis == 0x8146)
                ascii = ':';
            /* Fullwidth semicolon ； */
            else if (sjis == 0x8147)
                ascii = ';';
            /* Fullwidth slash ／ */
            else if (sjis == 0x814A)
                ascii = '/';
            /* Fullwidth dash － */
            else if (sjis == 0x815F)
                ascii = '-';
            /* Fullwidth tilde ～ */
            else if (sjis == 0x814B)
                ascii = '~';
            /* Fullwidth exclamation ！ */
            else if (sjis == 0x8149)
                ascii = '!';
            /* Fullwidth question ？ */
            else if (sjis == 0x8148)
                ascii = '?';
            /* Fullwidth left paren （ */
            else if (sjis == 0x8152)
                ascii = '(';
            /* Fullwidth right paren ） */
            else if (sjis == 0x8153)
                ascii = ')';
            /* Fullwidth plus ＋ */
            else if (sjis == 0x815E)
                ascii = '+';
            /* Fullwidth equals ＝ */
            else if (sjis == 0x8160)
                ascii = '=';
            /* Fullwidth less ＜ */
            else if (sjis == 0x8161)
                ascii = '<';
            /* Fullwidth greater ＞ */
            else if (sjis == 0x8162)
                ascii = '>';
            /* Fullwidth yen ￥ */
            else if (sjis == 0x8163)
                ascii = '\\';
            /* Fullwidth dollar ＄ */
            else if (sjis == 0x8164)
                ascii = '$';
            /* Fullwidth percent ％ */
            else if (sjis == 0x8165)
                ascii = '%';
            /* Fullwidth hash ＃ */
            else if (sjis == 0x8166)
                ascii = '#';
            /* Fullwidth ampersand ＆ */
            else if (sjis == 0x8167)
                ascii = '&';
            /* Fullwidth asterisk ＊ */
            else if (sjis == 0x8168)
                ascii = '*';
            /* Fullwidth at ＠ */
            else if (sjis == 0x8169)
                ascii = '@';

            out_title[j++] = ascii;
            i++; /* consumed 2 bytes */
        }
    }
    out_title[j] = '\0';
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

uint8_t* getIconPS2(const char* folder, const char* iconfile)
{
    int fd;
    uint8_t *buf, *out;
    char filePath[256];
    struct io_dirent st;

    snprintf(filePath, sizeof(filePath), "%s/%s", folder, iconfile);
    mcio_mcStat(filePath, &st);

    fd = mcio_mcOpen(filePath, sceMcFileAttrReadable | sceMcFileAttrFile);
    if (fd < 0)
        return calloc(128 * 128, sizeof(uint32_t));

    buf = malloc(st.stat.size);
    mcio_mcRead(fd, buf, st.stat.size);
    mcio_mcClose(fd);

    out = ps2IconTexture(buf);
    free(buf);

    return out;
}
