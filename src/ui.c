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

#define COLOR_BG 0xFF0B141F
#define COLOR_PANEL 0xFF141F2B
#define COLOR_CARD 0xFF1B2836
#define COLOR_CARD_SEL 0xFF213145
#define COLOR_BORDER 0xFF2E4256
#define COLOR_ACCENT 0xFF3FA9F5
#define COLOR_GOLD 0xFFF5C542
#define COLOR_TEXT 0xFFF2F5F8
#define COLOR_DIM 0xFFA6B4C4
#define COLOR_MUTED 0xFF6C7C8E
#define COLOR_SUCCESS 0xFF3ED598
#define COLOR_ERROR 0xFFFF5C5C

#define HEADER_H 90
#define FOOTER_H 64

#define LIST_X SAFE_X
#define LIST_Y (HEADER_H + 20)
#define LIST_W 1020
#define LIST_BOT (SCREEN_HEIGHT - FOOTER_H - 20)

#define DIV_X (LIST_X + LIST_W + 20)
#define RPANE_X (DIV_X + 20)
#define RPANE_W (SAFE_X1 - RPANE_X)

#define ITEM_H 54
#define ITEM_GAP 4

#define COVER_H (int)((LIST_BOT - LIST_Y - 30) * 0.55f)
#define COVER_W (RPANE_W - 40)
#define COVER_X (RPANE_X + 20)
#define COVER_Y (LIST_Y + 20)

static void truncate_to_fit(const char *s, char *out, size_t out_len, int max_px, int size) {
    int chw = font_text_width("M", size);
    int max_chars = max_px / (chw + 1);
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
        truncate_to_fit(games[i].display_name, buf, sizeof(buf), LIST_W - 40, 22);
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
    draw_rounded_rect(RPANE_X - 16, py, RPANE_W + 32, ph, 12, COLOR_PANEL);

    cover_draw_fit(COVER_X, COVER_Y, COVER_W, COVER_H, g->id);

    int iy = COVER_Y + COVER_H + 24;

    char tbuf[48];
    truncate_to_fit(g->display_name, tbuf, sizeof(tbuf), RPANE_W - 20, 32);
    draw_text(RPANE_X + 10, iy, tbuf, COLOR_GOLD, 32);
    iy += 42;

    char tmp[128];
    snprintf(tmp, sizeof(tmp), "Disc ID: %s", g->id);
    draw_text(RPANE_X + 10, iy, tmp, COLOR_TEXT, 18);
    iy += 28;

    if (strlen(g->id) >= 4) {
        char rc[3]; strncpy(rc, g->id + 2, 2); rc[2] = '\0';
        const char *reg = rc;
        if (strcmp(rc, "US") == 0) reg = "USA";
        else if (strcmp(rc, "EU") == 0) reg = "Europe";
        else if (strcmp(rc, "JP") == 0) reg = "Japan";
        snprintf(tmp, sizeof(tmp), "Region: %s", reg);
        draw_text(RPANE_X + 10, iy, tmp, COLOR_DIM, 18);
        iy += 28;
    }

    snprintf(tmp, sizeof(tmp), "Emulator: %s", g->emulator_name[0] ? g->emulator_name : "Default");
    draw_text(RPANE_X + 10, iy, tmp, COLOR_DIM, 18);
    iy += 28;

    snprintf(tmp, sizeof(tmp), "Emu ID: %s", g->emulator_id[0] ? g->emulator_id : EMULATOR_TID);
    draw_text(RPANE_X + 10, iy, tmp, COLOR_DIM, 18);
    iy += 28;

    truncate_to_fit(g->name, tmp, sizeof(tmp), RPANE_W - 20, 16);
    draw_text(RPANE_X + 10, iy, tmp, COLOR_MUTED, 16);

    iy = py + ph - 24;
    char idx[32];
    snprintf(idx, sizeof(idx), "Game %d of %d", sel + 1, game_count);
    int iw = font_text_width(idx, 16);
    draw_text(RPANE_X + RPANE_W - iw - 10, iy, idx, COLOR_MUTED, 16);
}

static void draw_header(int total_games) {
    draw_rect(0, 0, SCREEN_WIDTH, HEADER_H, COLOR_BG);
    draw_rect(0, HEADER_H - 3, SCREEN_WIDTH, 3, COLOR_ACCENT);

    draw_text(SAFE_X, SAFE_Y + 8, "PS2 ISO LAUNCHER", COLOR_GOLD, 40);
    draw_text(SAFE_X + 420, SAFE_Y + 22, "by tehrzky", COLOR_MUTED, 18);

    char cnt[32];
    snprintf(cnt, sizeof(cnt), "%d GAMES", total_games);
    int cw = font_text_width(cnt, 22);
    draw_text(SAFE_X1 - cw - 10, SAFE_Y + 8, cnt, COLOR_DIM, 22);

    draw_text(SAFE_X, SAFE_Y + 52, "Select a game and press X to launch", COLOR_DIM, 16);
}

static void draw_footer(int in_settings) {
    int y = SCREEN_HEIGHT - FOOTER_H;
    draw_rect(0, y, SCREEN_WIDTH, FOOTER_H, COLOR_PANEL);
    draw_rect(0, y, SCREEN_WIDTH, 3, COLOR_ACCENT);

    int hy = SAFE_Y1 - 30;
    int x = SAFE_X;
    if (in_settings) {
        draw_btn_hint(x, hy, "^v", "Navigate", COLOR_DIM); x += 220;
        draw_btn_hint(x, hy, "<>", "Change", COLOR_DIM); x += 220;
        draw_btn_hint(x, hy, "O", "Close", COLOR_ERROR);
    } else {
        draw_btn_hint(x, hy, "X", "Launch", COLOR_ACCENT); x += 190;
        draw_btn_hint(x, hy, "^v", "Select", COLOR_DIM); x += 220;
        draw_btn_hint(x, hy, "L2", "Fast", COLOR_DIM); x += 220;
        draw_btn_hint(x, hy, "TRI", "Settings", COLOR_ACCENT); x += 260;
        draw_btn_hint(x, hy, "O", "Exit", COLOR_ERROR);
    }
}

void draw_launcher_ui(int game_count_visible, int selected_idx, int total_games) {
    memset(framebuffer[current_buf], 0, FB_SIZE);
    draw_rect(0, 0, SCREEN_WIDTH, SCREEN_HEIGHT, COLOR_BG);

    draw_rect(DIV_X, HEADER_H + 10, 2, SCREEN_HEIGHT - HEADER_H - FOOTER_H - 20, COLOR_BORDER);

    draw_header(total_games);
    draw_game_list(selected_idx, game_count_visible);
    draw_game_details(selected_idx);
    draw_footer(0);

    if (game_count_visible == 0) {
        const char *m1 = "NO ISO FILES FOUND";
        const char *m2 = "Place ISOs in /data/PS4ROMS/PS2ISO/";
        int w1 = font_text_width(m1, 36);
        int w2 = font_text_width(m2, 24);
        draw_text(SCREEN_WIDTH / 2 - w1 / 2, SCREEN_HEIGHT / 2 - 20, m1, COLOR_ERROR, 36);
        draw_text(SCREEN_WIDTH / 2 - w2 / 2, SCREEN_HEIGHT / 2 + 30, m2, COLOR_DIM, 24);
    }
}

static const char *settings_labels[SETTINGS_ITEMS] = {
    "Auto Download Covers",
    "Auto Download GameIndex",
    "Cover Type",
    "Scraper URL",
    "Force Download GameIndex",
    "Force Download All Covers"
};

void draw_settings_ui(int selected_item, int in_per_game) {
    int pw = 700;
    int ph = 520;
    int px = (SCREEN_WIDTH - pw) / 2;
    int py = (SCREEN_HEIGHT - ph) / 2;

    // FAST darkening: 4 solid rects instead of 2M pixels
    draw_rect(0, 0, SCREEN_WIDTH, py, 0xE60B141F);
    draw_rect(0, py + ph, SCREEN_WIDTH, SCREEN_HEIGHT - py - ph, 0xE60B141F);
    draw_rect(0, py, px, ph, 0xE60B141F);
    draw_rect(px + pw, py, SCREEN_WIDTH - px - pw, ph, 0xE60B141F);

    draw_rounded_rect(px, py, pw, ph, 16, COLOR_PANEL);
    draw_rounded_rect(px + 2, py + 2, pw - 4, ph - 4, 14, COLOR_BG);

    const char *title = in_per_game ? "PER-GAME SETTINGS" : "SETTINGS";
    int tw = font_text_width(title, 36);
    draw_text(px + (pw - tw) / 2, py + 20, title, COLOR_GOLD, 36);

    int row_h = 56;
    int start_y = py + 90;

    for (int i = 0; i < SETTINGS_ITEMS; i++) {
        int ry = start_y + i * row_h;
        int is_sel = (i == selected_item);

        if (is_sel) {
            draw_rounded_rect(px + 20, ry, pw - 40, row_h - 6, 8, COLOR_CARD_SEL);
            draw_rect(px + 20, ry + 6, 4, row_h - 18, COLOR_ACCENT);
        }

        draw_text(px + 40, ry + 14, settings_labels[i], is_sel ? COLOR_TEXT : COLOR_DIM, 22);

        char value[128] = {0};
        if (i == 0) snprintf(value, sizeof(value), "%s", g_settings.auto_download_covers ? "ON" : "OFF");
        else if (i == 1) snprintf(value, sizeof(value), "%s", g_settings.auto_download_gameindex ? "ON" : "OFF");
        else if (i == 2) snprintf(value, sizeof(value), "%s", g_settings.cover_type == 1 ? "3D" : "Default");
        else if (i == 3) {
            strncpy(value, g_settings.scraper_base_url, 30);
            value[30] = '\0';
            if (strlen(g_settings.scraper_base_url) > 30) strcat(value, "...");
        }
        else if (i == 4) snprintf(value, sizeof(value), "Press X");
        else if (i == 5) snprintf(value, sizeof(value), "Press X");

        int vw = font_text_width(value, 22);
        uint32_t vc = (i == 3) ? COLOR_MUTED : (is_sel ? COLOR_ACCENT : COLOR_DIM);
        if (i >= 4) vc = is_sel ? COLOR_SUCCESS : COLOR_MUTED;
        draw_text(px + pw - 40 - vw, ry + 14, value, vc, 22);
    }

    draw_footer(1);
}
