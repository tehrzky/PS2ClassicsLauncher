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

#define SAFE_X 64
#define SAFE_Y 40
#define SAFE_X1 (SCREEN_WIDTH - SAFE_X)
#define SAFE_Y1 (SCREEN_HEIGHT - SAFE_Y)

#define COLOR_BG 0xFF0B141F
#define COLOR_PANEL 0xFF101924
#define COLOR_CARD 0xFF141F2B
#define COLOR_CARD_SEL 0xFFEDF2F7
#define COLOR_BORDER 0xFF4A6B8A
#define COLOR_GOLD 0xFFF5C542
#define COLOR_TEXT 0xFFFFFFFF
#define COLOR_TEXT_SEL 0xFF000000
#define COLOR_DIM 0xFFCBD5E1
#define COLOR_MUTED 0xFF64748B
#define COLOR_ACCENT 0xFF3FA9F5
#define COLOR_SUCCESS 0xFF3ED598
#define COLOR_ERROR 0xFFFF5C5C

#define HEADER_H 100
#define FOOTER_H 70

/* Panel layout aligned with 1:1 image measurements */
#define PANEL_GAP 28
#define PANEL_Y 120
#define PANEL_BOT (SCREEN_HEIGHT - FOOTER_H - 24)
#define PANEL_H (PANEL_BOT - PANEL_Y)

#define LEFT_PANE_X SAFE_X
#define LEFT_PANE_W 870

#define RIGHT_PANE_X (LEFT_PANE_X + LEFT_PANE_W + PANEL_GAP)
#define RIGHT_PANE_W (SAFE_X1 - RIGHT_PANE_X)

#define ITEM_H 62
#define ITEM_GAP 6

#define COVER_X (RIGHT_PANE_X + 20)
#define COVER_Y (PANEL_Y + 70)
#define COVER_W 340
#define COVER_H (int)(PANEL_H * 0.70f)

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
    int bw = font_text_width(btn, 20) + 16;
    int bh = 32;
    draw_rounded_rect(x, y, bw, bh, 6, COLOR_BORDER);
    draw_rounded_rect(x + 1, y + 1, bw - 2, bh - 2, 5, COLOR_CARD);
    draw_text(x + 8, y + 5, btn, c, 20);
    draw_text(x + bw + 12, y + 5, lbl, COLOR_TEXT, 20);
}

static void draw_game_list(int sel, int count) {
    /* Header area inside left panel */
    draw_text(LEFT_PANE_X + 16, PANEL_Y + 16, "SELECT A GAME", COLOR_TEXT, 28);
    draw_rect(LEFT_PANE_X + 16, PANEL_Y + 54, LEFT_PANE_W - 32, 2, COLOR_BORDER);

    int list_y = PANEL_Y + 68;
    int visible = (PANEL_BOT - list_y - 10) / (ITEM_H + ITEM_GAP);
    if (visible < 1) visible = 1;
    
    int start = 0;
    if (sel >= visible) start = sel - visible + 1;
    if (start < 0) start = 0;

    for (int i = start; i < count && i < start + visible; i++) {
        int yy = list_y + (i - start) * (ITEM_H + ITEM_GAP);
        int is_sel = (i == sel);

        if (is_sel) {
            draw_rounded_rect(LEFT_PANE_X + 12, yy, LEFT_PANE_W - 24, ITEM_H, 4, COLOR_CARD_SEL);
            
            char buf[64];
            snprintf(buf, sizeof(buf), "> %s", games[i].display_name);
            char tbuf[64];
            truncate_to_fit(buf, tbuf, sizeof(tbuf), LEFT_PANE_W - 60, 26);
            draw_text(LEFT_PANE_X + 24, yy + (ITEM_H - 26) / 2, tbuf, COLOR_TEXT_SEL, 26);
        } else {
            char tbuf[64];
            truncate_to_fit(games[i].display_name, tbuf, sizeof(tbuf), LEFT_PANE_W - 60, 24);
            draw_text(LEFT_PANE_X + 48, yy + (ITEM_H - 24) / 2, tbuf, COLOR_TEXT, 24);
        }
    }

    if (count > visible) {
        int track_h = PANEL_BOT - list_y - 20;
        draw_rect(LEFT_PANE_X + LEFT_PANE_W - 10, list_y, 4, track_h, COLOR_CARD);
        int thumb = track_h * visible / count;
        if (thumb < 24) thumb = 24;
        int ty = list_y + (track_h - thumb) * start / (count - visible > 0 ? count - visible : 1);
        draw_rect(LEFT_PANE_X + LEFT_PANE_W - 10, ty, 4, thumb, COLOR_ACCENT);
    }
}

static void draw_game_details(int sel, int total_games) {
    /* Header area inside right panel */
    draw_text(RIGHT_PANE_X + 16, PANEL_Y + 16, "GAME DETAILS", COLOR_TEXT, 28);
    
    char cnt[32];
    snprintf(cnt, sizeof(cnt), "Total Games: %d", total_games);
    int cw = font_text_width(cnt, 22);
    draw_text(RIGHT_PANE_X + RIGHT_PANE_W - cw - 20, PANEL_Y + 20, cnt, COLOR_DIM, 22);
    
    draw_rect(RIGHT_PANE_X + 16, PANEL_Y + 54, RIGHT_PANE_W - 32, 2, COLOR_BORDER);

    if (sel < 0 || sel >= game_count) return;
    Game *g = &games[sel];

    /* Cover Rendering */
    cover_draw_fit(COVER_X, COVER_Y, COVER_W, COVER_H, g->id);

    /* Text details layout matching image typography */
    int tx = COVER_X + COVER_W + 28;
    int ty = COVER_Y + 10;
    int line_gap = 38;

    /* TITLE */
    draw_text(tx, ty, "TITLE:", COLOR_TEXT, 20);
    ty += 24;
    char tbuf[48];
    truncate_to_fit(g->display_name, tbuf, sizeof(tbuf), RIGHT_PANE_X + RIGHT_PANE_W - tx - 20, 24);
    draw_text(tx, ty, tbuf, COLOR_TEXT, 24);
    ty += line_gap;

    /* GAME ID */
    draw_text(tx, ty, "GAME ID:", COLOR_TEXT, 20);
    ty += 24;
    draw_text(tx, ty, g->id, COLOR_TEXT, 24);
    ty += line_gap;

    /* EMULATOR */
    draw_text(tx, ty, "EMULATOR:", COLOR_TEXT, 20);
    ty += 24;
    const char *emu = g->emulator_name[0] ? g->emulator_name : "KOF Orochi Saga";
    draw_text(tx, ty, emu, COLOR_TEXT, 24);
    ty += line_gap;

    /* EMU ID */
    draw_text(tx, ty, "EMU ID:", COLOR_TEXT, 20);
    ty += 24;
    const char *emu_id = g->emulator_id[0] ? g->emulator_id : "PCSX20042";
    draw_text(tx, ty, emu_id, COLOR_TEXT, 24);
}

static void draw_header(void) {
    draw_rect(0, 0, SCREEN_WIDTH, HEADER_H, COLOR_BG);
    
    /* Top Brand Title */
    draw_text(SAFE_X, SAFE_Y, "PS2 ISO LAUNCHER", COLOR_ACCENT, 44);
    
    /* User Tag */
    int tw = font_text_width("tehrzky", 28);
    draw_text(SAFE_X1 - tw, SAFE_Y + 10, "tehrzky", COLOR_ACCENT, 28);

    /* Header Accent Divider Line */
    draw_rect(SAFE_X, HEADER_H, SCREEN_WIDTH - (SAFE_X * 2), 2, COLOR_ACCENT);
}

static void draw_footer(int in_settings) {
    int y = SCREEN_HEIGHT - FOOTER_H;
    draw_rect(0, y, SCREEN_WIDTH, FOOTER_H, COLOR_PANEL);
    draw_rect(0, y, SCREEN_WIDTH, 2, COLOR_ACCENT);

    int hy = y + 18;
    int x = SAFE_X + 20;

    if (in_settings) {
        draw_btn_hint(x, hy, "^v", "Navigate", COLOR_DIM); x += 220;
        draw_btn_hint(x, hy, "<>", "Change", COLOR_DIM); x += 220;
        draw_btn_hint(x, hy, "O", "Close", COLOR_ERROR);
    } else {
        draw_btn_hint(x, hy, "X", "LAUNCH", COLOR_ACCENT); x += 220;
        draw_btn_hint(x, hy, "TRI", "SETTINGS", COLOR_ACCENT); x += 240;
        draw_btn_hint(x, hy, "^v", "SCROLL", COLOR_DIM); x += 220;
        draw_btn_hint(x, hy, "HOLD L2", "FAST SCROLL", COLOR_DIM);
    }
}

void draw_launcher_ui(int game_count_visible, int selected_idx, int total_games) {
    memset(framebuffer[current_buf], 0, FB_SIZE);
    draw_rect(0, 0, SCREEN_WIDTH, SCREEN_HEIGHT, COLOR_BG);

    draw_header();

    /* Draw Left & Right Panels with Gold/Amber Outer Borders matching Image */
    draw_rounded_rect(LEFT_PANE_X, PANEL_Y, LEFT_PANE_W, PANEL_H, 8, COLOR_GOLD);
    draw_rounded_rect(LEFT_PANE_X + 2, PANEL_Y + 2, LEFT_PANE_W - 4, PANEL_H - 4, 6, COLOR_PANEL);

    draw_rounded_rect(RIGHT_PANE_X, PANEL_Y, RIGHT_PANE_W, PANEL_H, 8, COLOR_GOLD);
    draw_rounded_rect(RIGHT_PANE_X + 2, PANEL_Y + 2, RIGHT_PANE_W - 4, PANEL_H - 4, 6, COLOR_PANEL);

    draw_game_list(selected_idx, game_count_visible);
    draw_game_details(selected_idx, total_games);
    draw_footer(0);

    if (game_count_visible == 0) {
        const char *m1 = "NO ISO FILES FOUND";
        const char *m2 = "Place ISOs in working directory";
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

        draw_text(px + 40, ry + 14, settings_labels[i], is_sel ? COLOR_TEXT_SEL : COLOR_DIM, 22);

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
