#include "font.h"
#include "video.h"
#include "debug.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define STB_TRUETYPE_IMPLEMENTATION
#include "stb_truetype.h"

// ===== Embedded 8x8 bitmap fallback =====
static unsigned char font8x8[96][8] = {
    {0,0,0,0,0,0,0,0}, {0,0,0,95,0,0,0,0}, {0,0,3,0,3,0,0,0},
    {0,20,127,20,127,20,0,0}, {0,36,42,127,42,18,0,0}, {0,35,19,8,100,98,0,0},
    {0,54,73,85,34,80,0,0}, {0,0,0,3,0,0,0,0}, {0,0,28,34,65,0,0,0},
    {0,0,65,34,28,0,0,0}, {0,8,42,28,42,8,0,0}, {0,8,8,62,8,8,0,0},
    {0,0,80,48,0,0,0,0}, {0,8,8,8,8,8,0,0}, {0,0,96,96,0,0,0,0},
    {0,32,16,8,4,2,0,0}, {0,62,81,73,69,62,0,0}, {0,0,66,127,64,0,0,0},
    {0,66,97,81,73,70,0,0}, {0,33,65,69,75,49,0,0}, {0,24,20,18,127,16,0,0},
    {0,39,69,69,69,57,0,0}, {0,60,74,73,73,48,0,0}, {0,1,113,9,5,3,0,0},
    {0,54,73,73,73,54,0,0}, {0,6,73,73,41,30,0,0}, {0,0,54,54,0,0,0,0},
    {0,0,86,54,0,0,0,0}, {0,8,20,34,65,0,0,0}, {0,20,20,20,20,20,0,0},
    {0,0,65,34,20,8,0,0}, {0,2,1,81,9,6,0,0}, {0,50,73,121,65,62,0,0},
    {0,126,17,17,17,126,0,0}, {0,127,73,73,73,54,0,0}, {0,62,65,65,65,34,0,0},
    {0,127,65,65,34,28,0,0}, {0,127,73,73,73,65,0,0}, {0,127,9,9,9,1,0,0},
    {0,62,65,73,73,122,0,0}, {0,127,8,8,8,127,0,0}, {0,0,65,127,65,0,0,0},
    {0,32,64,65,63,1,0,0}, {0,127,8,20,34,65,0,0}, {0,127,64,64,64,64,0,0},
    {0,127,2,12,2,127,0,0}, {0,127,4,8,16,127,0,0}, {0,62,65,65,65,62,0,0},
    {0,127,9,9,9,6,0,0}, {0,62,65,81,33,94,0,0}, {0,127,9,25,41,70,0,0},
    {0,70,73,73,73,49,0,0}, {0,1,1,127,1,1,0,0}, {0,63,64,64,64,63,0,0},
    {0,31,32,64,32,31,0,0}, {0,63,64,56,64,63,0,0}, {0,99,20,8,20,99,0,0},
    {0,7,8,112,8,7,0,0}, {0,97,81,73,69,67,0,0}, {0,0,127,65,65,0,0,0},
    {0,2,4,8,16,32,0,0}, {0,0,65,65,127,0,0,0}, {0,4,2,1,2,4,0,0},
    {0,64,64,64,64,64,0,0}, {0,0,0,1,2,4,0,0}, {0,32,84,84,84,120,0,0},
    {0,127,72,68,68,56,0,0}, {0,56,68,68,68,32,0,0}, {0,56,68,68,72,127,0,0},
    {0,56,84,84,84,24,0,0}, {0,8,126,9,1,2,0,0}, {0,12,82,82,82,62,0,0},
    {0,127,8,4,4,120,0,0}, {0,0,68,125,64,0,0,0}, {0,32,64,68,61,0,0,0},
    {0,127,16,40,68,0,0,0}, {0,0,65,127,64,0,0,0}, {0,124,4,24,4,120,0,0},
    {0,124,8,4,4,120,0,0}, {0,56,68,68,68,56,0,0}, {0,124,20,20,20,8,0,0},
    {0,8,20,20,24,124,0,0}, {0,124,8,4,4,8,0,0}, {0,72,84,84,84,32,0,0},
    {0,4,63,68,64,32,0,0}, {0,60,64,64,32,124,0,0}, {0,28,32,64,32,28,0,0},
    {0,60,64,48,64,60,0,0}, {0,68,40,16,40,68,0,0}, {0,12,80,80,80,60,0,0},
    {0,68,100,84,76,68,0,0}, {0,0,8,54,65,0,0,0}, {0,0,0,127,0,0,0,0},
    {0,0,65,54,8,0,0,0}, {0,8,8,42,28,8,0,0}
};

// ===== TTF state =====
static stbtt_fontinfo ttf_font;
static unsigned char *ttf_data = NULL;
static int ttf_loaded = 0;

void font_init(void) {
    FILE *fp = fopen("/data/PS4ROMS/PS2ISO/assets/font/font.ttf", "rb");
    if (!fp) fp = fopen("/app0/assets/font.ttf", "rb");
    if (!fp) {
        log_debug("No TTF found, using bitmap font");
        return;
    }
    fseek(fp, 0, SEEK_END);
    long size = ftell(fp);
    fseek(fp, 0, SEEK_SET);
    ttf_data = (unsigned char*)malloc(size);
    if (!ttf_data) { fclose(fp); return; }
    fread(ttf_data, 1, size, fp);
    fclose(fp);

    int offset = stbtt_GetFontOffsetForIndex(ttf_data, 0);
    if (stbtt_InitFont(&ttf_font, ttf_data, offset)) {
        ttf_loaded = 1;
        log_debug("TTF loaded: %ld bytes", size);
    } else {
        free(ttf_data); ttf_data = NULL;
    }
}

void font_cleanup(void) {
    if (ttf_data) { free(ttf_data); ttf_data = NULL; ttf_loaded = 0; }
}

// ===== Bitmap fallback =====
static void draw_text_bitmap(int x, int y, const char *s, uint32_t color, int scale) {
    while (*s) {
        char c = *s++;
        if (c < 32 || c > 127) continue;
        const unsigned char *f = font8x8[(int)c - 32];
        for (int row = 0; row < 8; row++) {
            for (int col = 0; col < 8; col++) {
                if (f[col] & (1 << row)) {
                    draw_rect(x + col * scale, y + row * scale, scale, scale, color);
                }
            }
        }
        x += 8 * scale;
    }
}

// ===== TTF rendering =====
static void draw_text_ttf(int x, int y, const char *text, uint32_t color, float size_px) {
    float scale = stbtt_ScaleForPixelHeight(&ttf_font, size_px);
    int ascent, baseline;
    stbtt_GetFontVMetrics(&ttf_font, &ascent, 0, 0);
    baseline = (int)(ascent * scale);

    uint8_t cr = (color >> 16) & 0xFF;
    uint8_t cg = (color >> 8)  & 0xFF;
    uint8_t cb = color & 0xFF;

    int xpos = x;
    while (*text) {
        int c = (unsigned char)*text++;
        if (c < 32 || c > 126) continue;

        int w, h, xoff, yoff;
        unsigned char *bitmap = stbtt_GetCodepointBitmap(&ttf_font, 0, scale, c, &w, &h, &xoff, &yoff);

        for (int j = 0; j < h; j++) {
            int py = y + j + baseline + yoff;
            if (py < 0 || py >= SCREEN_HEIGHT) continue;
            uint32_t *dst_row = &framebuffer[current_buf][py * SCREEN_WIDTH];
            for (int i = 0; i < w; i++) {
                int px = xpos + i + xoff;
                if (px < 0 || px >= SCREEN_WIDTH) continue;
                uint8_t alpha = bitmap[j * w + i];
                if (alpha == 0) continue;
                if (alpha == 255) {
                    dst_row[px] = (0xFFu << 24) | ((uint32_t)cr << 16) | ((uint32_t)cg << 8) | cb;
                } else {
                    uint32_t dst = dst_row[px];
                    uint8_t dr = (dst >> 16) & 0xFF, dg = (dst >> 8) & 0xFF, db = dst & 0xFF;
                    uint8_t r = (cr * alpha + dr * (255 - alpha)) / 255;
                    uint8_t g = (cg * alpha + dg * (255 - alpha)) / 255;
                    uint8_t b = (cb * alpha + db * (255 - alpha)) / 255;
                    dst_row[px] = (0xFFu << 24) | ((uint32_t)r << 16) | ((uint32_t)g << 8) | b;
                }
            }
        }
        stbtt_FreeBitmap(bitmap, NULL);

        int advance, lsb;
        stbtt_GetCodepointHMetrics(&ttf_font, c, &advance, &lsb);
        xpos += (int)(advance * scale);
    }
}

// ===== Public API =====
void draw_text(int x, int y, const char *s, uint32_t color, int size_px) {
    if (ttf_loaded) draw_text_ttf(x, y, s, color, (float)size_px);
    else {
        int sc = size_px / 16; if (sc < 1) sc = 1; if (sc > 3) sc = 3;
        draw_text_bitmap(x, y, s, color, sc);
    }
}

void draw_text_scaled(int x, int y, const char *s, uint32_t color, int scale) {
    int sz = 16;
    if (scale == 2) sz = 24;
    if (scale == 3) sz = 36;
    if (scale >= 4) sz = 48;
    draw_text(x, y, s, color, sz);
}

int font_text_width(const char *s, int size_px) {
    if (!ttf_loaded) {
        int sc = size_px / 16; if (sc < 1) sc = 1;
        return (int)strlen(s) * 8 * sc;
    }
    float scale = stbtt_ScaleForPixelHeight(&ttf_font, (float)size_px);
    int w = 0;
    while (*s) {
        int c = (unsigned char)*s++;
        if (c < 32 || c > 126) continue;
        int adv, lsb; stbtt_GetCodepointHMetrics(&ttf_font, c, &adv, &lsb);
        w += (int)(adv * scale);
    }
    return w;
}

int font_line_height(int size_px) {
    if (!ttf_loaded) {
        int sc = size_px / 16; if (sc < 1) sc = 1;
        return 8 * sc;
    }
    float scale = stbtt_ScaleForPixelHeight(&ttf_font, (float)size_px);
    int a, d, lg; stbtt_GetFontVMetrics(&ttf_font, &a, &d, &lg);
    return (int)((a - d + lg) * scale);
}
