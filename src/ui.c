#include "ui.h"
#include "video.h"
#include "font.h"
#include "game.h"
#include "cover.h"
#include "settings.h"
#include "config.h"
#include <string.h>
#include <stdio.h>

#define SCREEN_WIDTH 1920
#define SCREEN_HEIGHT 1080
#define FB_SIZE (SCREEN_WIDTH * SCREEN_HEIGHT * 4)

#define SAFE_X 56
#define SAFE_Y 32
#define SAFE_X1 (SCREEN_WIDTH - SAFE_X)
#define SAFE_Y1 (SCREEN_HEIGHT - SAFE_Y)

#define COLOR_BG        0xFF0B141F
#define COLOR_PANEL     0xFF141F2B
#define COLOR_CARD      0xFF1B2836
#define COLOR_CARD_SEL  0xFF213145
#define COLOR_BORDER    0xFF2E4256
#define COLOR_ACCENT    0xFF3FA9F5
#define COLOR_GOLD      0xFFF5C542
#define COLOR_TEXT      0xFFF2F5F8
#define COLOR_DIM       0xFFA6B4C4
#define COLOR_MUTED     0xFF6C7C8E
#define COLOR_SUCCESS   0xFF3ED598
#define COLOR_ERROR     0xFFFF5C5C

#define HEADER_H  90
#define FOOTER_H  64

#define LIST_X    SAFE_X
#define LIST_Y    (HEADER_H + 20)
#define LIST_W    1020
#define LIST_BOT  (SCREEN_HEIGHT - FOOTER_H - 20)

#define DIV_X     (LIST_X + LIST_W + 20)
#define RPane_X   (DIV_X + 20)
#define RPane_W   (SAFE_X1 - RPane_X)

#define ITEM_H    54
#define ITEM_GAP  4

#define COVER_H   (int)((LIST_BOT - LIST_Y - 30) * 0.55f)
#define COVER_W   (RPane_W - 40)
#define COVER_X   (RPane_X + 20)
#define COVER_Y   (LIST_Y + 20)

static unsigned char *bg_rgba = NULL;
static int bg_w = 0, bg_h = 0;
static unsigned char *logo_rgba = NULL;
static int logo_w = 0, logo_h = 0;

void ui_load_assets(void) {
    if (access("/app0/assets/bg.png", F_OK) == 0) {
        bg_rgba = stbi_load("/app0/assets/bg.png", &bg_w, &bg_h, NULL, 4);
    }
    if (access("/app0/assets/logo.png", F_OK) == 0) {
        logo_rgba = stbi_load("/app0/assets/logo.png", &logo_w, &logo_h, NULL, 4);
    }
}

static void truncate_to_fit(const char *s, char *out, size_t out_len, int max_px, int size) {
    int max_chars = max_px / (font_text_width("M", size) + 1);
    if (max_chars < 4) max_chars = 4;
    size_t len = strlen(s);
    if ((int)len <= max_chars || out_len < 5) {
        snprintf(out, out_len, "%s", s);
        return;
    }
    int keep = max_chars - 3;
    if (keep < 1) keep = 1;
    snprintf(out, out_len, "%.*s...", keep, s);
}

static void draw_btn_hint(int x, int y, const char *btn, const char *lbl, uint32_t c) {
    int bw = font_text_width(btn, 18) + 20;
    int bh = 28;
    draw_rounded_rect(x, y, bw, bh, bh / 2, c);
    draw_text(x + 10, y + 5, btn, COLOR_BG, 18);
    draw_text(x + bw + 12, y + 5, lbl, COLOR_DIM, 18);
}

static void draw_game_list(int sel, int count) {
    int visible = (LIST_BOT - LIST_Y) / (ITEM_H + ITEM_GAP);
    if (visible < 1) visible = 1;
    int start = 0;
    if (sel >= visible) start = sel - visible + 1;
    if (start < 0) start = 0;

    for (int i = start; i < count && i < start + visible; i++) {
        int yy = LIST_Y + (i - start) * (ITEM_H + ITEM_GAP);
        int is_sel = (i == sel);

        uint32_t border = is_sel ? COLOR_ACCENT : COLOR_BORDER;
        uint32_t fill   = is_sel ? COLOR_CARD_SEL : COLOR_CARD;

        draw_rounded_rect(LIST_X, yy, LIST_W, ITEM_H, 6, border);
        draw_rounded_rect(LIST_X + 1, yy + 1, LIST_W - 2, ITEM_H - 2, 5, fill);

        if (is_sel) {
            draw_rect(LIST_X, yy + 4, 3, ITEM_H - 8, COLOR_ACCENT);
        }

        char buf[64];
        truncate_to_fit(games[i].display_name, buf, sizeof(buf),
                        LIST_W - 40, 22);
        draw_text(LIST_X + 16, yy + (ITEM_H - 22) / 2 + 2, buf,
                  is_sel ? COLOR_GOLD : COLOR_TEXT, 22);
    }

    if (count > visible) {
        int track = LIST_BOT - LIST_Y;
        draw_rect(LIST_X + LIST_W + 4, LIST_Y, 3, track, COLOR_BORDER);
        int thumb = track * visible / count;
        if (thumb < 20) thumb = 20;
        int ty = LIST_Y + (track - thumb) * start / (count - visible > 0 ? count - visible : 1);
        draw_rect(LIST_X + LIST_W + 4, ty, 3, thumb, COLOR_ACCENT);
    }
}

static void draw_game_details(int sel) {
    if (sel < 0 || sel >= game_count) return;
    Game *g = &games[sel];

    int py = COVER_Y - 16;
    int ph = LIST_BOT - py;
    draw_rounded_rect(RPane_X - 16, py, RPane_W + 32, ph, 12, COLOR_PANEL);

    cover_draw_fit(COVER_X, COVER_Y, COVER_W, COVER_H, g->id);

    int iy = COVER_Y + COVER_H + 24;

    char tbuf[48];
    truncate_to_fit(g->display_name, tbuf, sizeof(tbuf), RPane_W - 20, 32);
    draw_text(RPane_X + 10, iy, tbuf, COLOR_GOLD, 32);
    iy += 42;

    char tmp[128];
    snprintf(tmp, sizeof(tmp), "Disc ID:  %s", g->id);
    draw_text(RPane_X + 10, iy, tmp, COLOR_TEXT, 18);
    iy += 28;

    if (strlen(g->id) >= 4) {
        char rc[3]; strncpy(rc, g->id + 2, 2); rc[2] = '\0';
        const char *reg = rc;
        if (strcmp(rc, "US") == 0) reg = "USA";
        else if (strcmp(rc, "EU") == 0) reg = "Europe";
        else if (strcmp(rc, "JP") == 0) reg = "Japan";
        snprintf(tmp, sizeof(tmp), "Region:   %s", reg);
        draw_text(RPane_X + 10, iy, tmp, COLOR_DIM, 18);
        iy += 28;
    }

    snprintf(tmp, sizeof(tmp), "Emulator: %s", g->emulator_name[0] ? g->emulator_name : "Default");
    draw_text(RPane_X + 10, iy, tmp, COLOR_DIM, 18);
    iy += 28;

    snprintf(tmp, sizeof(tmp), "Emu ID:   %s", g->emulator_id[0] ? g->emulator
