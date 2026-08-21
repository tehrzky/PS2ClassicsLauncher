#include "font.h"
#include "video.h"
#include "debug.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <dirent.h>
#include <unistd.h>
#include "settings.h"

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

// ===== Font list (scanned from directory) =====
typedef struct {
    char path[512];
    char name[64];
} FontEntry;

static FontEntry font_list[MAX_FONTS];
static int font_list_count = 0;

// ===== Loaded font slots =====
typedef struct {
    stbtt_fontinfo info;
    unsigned char *data;
    int loaded;
} FontSlot;

static FontSlot fonts[FONT_SLOT_COUNT];

static void font_load_into_slot(int slot, const char *path) {
    if (fonts[slot].loaded) {
        free(fonts[slot].data);
        fonts[slot].loaded = 0;
    }
    FILE *fp = fopen(path, "rb");
    if (!fp) return;
    fseek(fp, 0, SEEK_END);
    long size = ftell(fp);
    fseek(fp, 0, SEEK_SET);
    fonts[slot].data = (unsigned char*)malloc(size);
    if (!fonts[slot].data) { fclose(fp); return; }
    fread(fonts[slot].data, 1, size, fp);
    fclose(fp);

    int offset = stbtt_GetFontOffsetForIndex(fonts[slot].data, 0);
    if (stbtt_InitFont(&fonts[slot].info, fonts[slot].data, offset)) {
        fonts[slot].loaded = 1;
        log_debug("Font loaded slot %d: %s", slot, path);
    } else {
        free(fonts[slot].data);
        fonts[slot].data = NULL;
    }
}

void font_scan_directory(const char *dir) {
    font_list_count = 0;
    DIR *d = opendir(dir);
    if (!d) return;
    struct dirent *entry;
    while ((entry = readdir(d)) != NULL && font_list_count < MAX_FONTS) {
        int len = strlen(entry->d_name);
        if (len < 5) continue;
        char *ext = entry->d_name + len - 4;
        if (strcasecmp(ext, ".ttf") != 0 && strcasecmp(ext, ".otf") != 0) continue;

        snprintf(font_list[font_list_count].path, sizeof(font_list[0].path),
                 "%s/%s", dir, entry->d_name);
        strncpy(font_list[font_list_count].name, entry->d_name,
                sizeof(font_list[0].name) - 1);
        font_list[font_list_count].name[sizeof(font_list[0].name) - 1] = '\0';
        font_list_count++;
    }
    closedir(d);
    log_debug("Scanned %d fonts from %s", font_list_count, dir);
}

const char *font_get_list_name(int index) {
    if (index < 0 || index >= font_list_count) return "None";
    return font_list[index].name;
}

int font_get_list_count(void) {
    return font_list_count;
}

void font_load_slot(int slot, int index) {
    if (index < 0 || index >= font_list_count) return;
    font_load_into_slot(slot, font_list[index].path);
}

void font_cycle_slot(int slot, int delta) {
    if (font_list_count == 0) return;
    int *target = (slot == FONT_SLOT_TITLE) ? &g_settings.font_title : &g_settings.font_body;
    *target += delta;
    if (*target < 0) *target = font_list_count - 1;
    if (*target >= font_list_count) *target = 0;
    font_load_slot(slot, *target);
}

void font_init(void) {
    memset(fonts, 0, sizeof(fonts));

    char path[512];
    settings_get_path(path, sizeof(path), "assets/fonts");
    font_scan_directory(path);
    if (font_list_count == 0) {
        font_scan_directory("/app0/assets/fonts");
    }

    if (font_list_count > 0) {
        if (g_settings.font_body < 0 || g_settings.font_body >= font_list_count)
            g_settings.font_body = 0;
        if (g_settings.font_title < 0 || g_settings.font_title >= font_list_count)
            g_settings.font_title = (font_list_count > 1) ? 1 : 0;

        font_load_slot(FONT_SLOT_BODY, g_settings.font_body);
        font_load_slot(FONT_SLOT_TITLE, g_settings.font_title);
        if (font_list_count > 2) font_load_slot(FONT_SLOT_BOLD, 2);
        if (font_list_count > 3) font_load_slot(FONT_SLOT_ITALIC, 3);
    }
}

void font_cleanup(void) {
    for (int i = 0; i < FONT_SLOT_COUNT; i++) {
        if (fonts[i].data) { free(fonts[i].data); fonts[i].data = NULL; }
        fonts[i].loaded = 0;
    }
}

static FontSlot *font_resolve(int slot) {
    if (slot >= 0 && slot < FONT_SLOT_COUNT && fonts[slot].loaded) return &fonts[slot];
    if (fonts[FONT_SLOT_BODY].loaded) return &fonts[FONT_SLOT_BODY];
    return NULL;
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
static void draw_text_ttf(int x, int y, const char *text, uint32_t color, float size_px, FontSlot *f) {
    float scale = stbtt_ScaleForPixelHeight(&f->info, size_px);
    int ascent, baseline;
    stbtt_GetFontVMetrics(&f->info, &ascent, 0, 0);
    baseline = (int)(ascent * scale);

    uint8_t cr = (color >> 16) & 0xFF;
    uint8_t cg = (color >> 8) & 0xFF;
    uint8_t cb = color & 0xFF;

    int xpos = x;
    while (*text) {
        int c = (unsigned char)*text++;
        if (c < 32 || c > 126) continue;

        int w, h, xoff, yoff;
        unsigned char *bitmap = stbtt_GetCodepointBitmap(&f->info, 0, scale, c, &w, &h, &xoff, &yoff);

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
        stbtt_GetCodepointHMetrics(&f->info, c, &advance, &lsb);
        xpos += (int)(advance * scale);
    }
}

// ===== Public API =====
void draw_text_slot(int x, int y, const char *s, uint32_t color, int size_px, int slot) {
    FontSlot *f = font_resolve(slot);
    if (f) draw_text_ttf(x, y, s, color, (float)size_px, f);
    else {
        int sc = size_px / 16; if (sc < 1) sc = 1; if (sc > 4) sc = 4;
        draw_text_bitmap(x, y, s, color, sc);
    }
}

void draw_text(int x, int y, const char *s, uint32_t color, int size_px) {
    draw_text_slot(x, y, s, color, size_px, FONT_SLOT_BODY);
}

void draw_text_scaled(int x, int y, const char *s, uint32_t color, int scale) {
    int sz = 16;
    if (scale == 2) sz = 28;
    if (scale == 3) sz = 42;
    if (scale >= 4) sz = 56;
    draw_text(x, y, s, color, sz);
}

int font_text_width_slot(const char *s, int size_px, int slot) {
    FontSlot *f = font_resolve(slot);
    if (!f) {
        int sc = size_px / 16; if (sc < 1) sc = 1;
        return (int)strlen(s) * 8 * sc;
    }
    float scale = stbtt_ScaleForPixelHeight(&f->info, (float)size_px);
    int w = 0;
    while (*s) {
        int c = (unsigned char)*s++;
        if (c < 32 || c > 126) continue;
        int adv, lsb; stbtt_GetCodepointHMetrics(&f->info, c, &adv, &lsb);
        w += (int)(adv * scale);
    }
    return w;
}

int font_text_width(const char *s, int size_px) {
    return font_text_width_slot(s, size_px, FONT_SLOT_BODY);
}

int font_line_height(int size_px) {
    FontSlot *f = font_resolve(FONT_SLOT_BODY);
    if (!f) {
        int sc = size_px / 16; if (sc < 1) sc = 1;
        return 8 * sc;
    }
    float scale = stbtt_ScaleForPixelHeight(&f->info, (float)size_px);
    int a, d, lg; stbtt_GetFontVMetrics(&f->info, &a, &d, &lg);
    return (int)((a - d + lg) * scale);
}
