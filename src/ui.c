#include "ui.h"
#include "video.h"
#include "font.h"
#include "game.h"
#include "cover.h"
#include "settings.h"
#include "config.h"
#include "colors.h"
#include <string.h>
#include <stdio.h>
#include "scraper.h"
#include "pgsettings_ui.h"

#define SCREEN_WIDTH 1920
#define SCREEN_HEIGHT 1080
#define FB_SIZE (SCREEN_WIDTH * SCREEN_HEIGHT * 4)

#define SAFE_X 64
#define SAFE_Y 40
#define SAFE_X1 (SCREEN_WIDTH - SAFE_X)
#define SAFE_Y1 (SCREEN_HEIGHT - SAFE_Y)

#define HEADER_H 100
#define FOOTER_H 70

#define PANEL_GAP 28
#define PANEL_Y 130
#define PANEL_BOT (SCREEN_HEIGHT - FOOTER_H - 40)
#define PANEL_H (PANEL_BOT - PANEL_Y)

#define TOTAL_PANEL_W (SAFE_X1 - SAFE_X - PANEL_GAP)
#define LEFT_PANE_X SAFE_X
#define LEFT_PANE_W (int)(TOTAL_PANEL_W * 0.52f)

#define RIGHT_PANE_X (LEFT_PANE_X + LEFT_PANE_W + PANEL_GAP)
#define RIGHT_PANE_W (TOTAL_PANEL_W - LEFT_PANE_W)

#define ITEM_H 64
#define ITEM_GAP 4

#define COVER_PADDING 28
#define COVER_X (RIGHT_PANE_X + COVER_PADDING)
#define COVER_Y (PANEL_Y + 70)
#define COVER_W 360
#define COVER_H (int)(PANEL_H * 0.55f)

#define SCROLLBAR_WIDTH      4
#define SCROLLBAR_PADDING   12
#define ROUND_RADIUS         8
#define ROUND_RADIUS_SMALL   6
#define ROUND_RADIUS_LARGE  16
#define PANEL_BORDER         2
#define SETTINGS_ROW_H      58
#define SETTINGS_PW        760
#define SETTINGS_PH        860

static GameDBInfo g_cached_info;
static int g_last_selected_info = -1;

static uint32_t get_panel_color(void) {
    int a = g_settings.panel_opacity * 255 / 100;
    return (a << 24) | (COLOR_PANEL & 0x00FFFFFF);
}

static void draw_wrapped_text(int x, int y, int max_w, int max_h, const char *text, uint32_t color, int size) {
    if (!text || !text[0]) return;
    int line_h = size + 6;
    int max_lines = max_h / line_h;
    if (max_lines < 1) max_lines = 1;

    char buf[1024];
    strncpy(buf, text, sizeof(buf)-1);
    buf[sizeof(buf)-1] = '\0';

    char line[512] = "";
    char *word = strtok(buf, " \n\r\t");
    int cy = y;
    int lines = 0;

    while (word && lines < max_lines) {
        char test[512];
        if (line[0]) snprintf(test, sizeof(test), "%s %s", line, word);
        else snprintf(test, sizeof(test), "%s", word);

        if (font_text_width(test, size) <= max_w) {
            snprintf(line, sizeof(line), "%s", test);
        } else {
            if (line[0]) {
                draw_text(x, cy, line, color, size);
                cy += line_h;
                lines++;
            }
            snprintf(line, sizeof(line), "%s", word);
        }
        word = strtok(NULL, " \n\r\t");
    }
    if (line[0] && lines < max_lines) {
        draw_text(x, cy, line, color, size);
    }
}

static void truncate_to_fit(const char *s, char *out, size_t out_len, int max_px, int size) {
    int full_width = font_text_width(s, size);
    if (full_width <= max_px) {
        snprintf(out, out_len, "%s", s);
        return;
    }
    int len = strlen(s);
    int keep = len;
    char temp[512];
    while (keep > 0) {
        strncpy(temp, s, keep);
        temp[keep] = '\0';
        int w = font_text_width(temp, size);
        if (w + font_text_width("...", size) <= max_px) {
            break;
        }
        keep--;
    }
    if (keep < 1) keep = 1;
    snprintf(out, out_len, "%.*s...", keep, s);
}

static void draw_btn_hint(int x, int y, const char *btn, const char *lbl, uint32_t c) {
    int bw = font_text_width(btn, 24) + 16;
    int bh = 36;
    draw_rounded_rect(x, y, bw, bh, 6, COLOR_BORDER);
    draw_rounded_rect(x + 1, y + 1, bw - 2, bh - 2, 5, COLOR_CARD);
    draw_text(x + 8, y + 6, btn, c, 24);
    draw_text(x + bw + 14, y + 6, lbl, COLOR_TEXT, 24);
}

static void draw_game_list(int sel, int count) {
    draw_text_slot(LEFT_PANE_X + 20, PANEL_Y + 16, "SELECT A GAME", COLOR_TEXT, 40, FONT_SLOT_BOLD);
    draw_rect(LEFT_PANE_X + 20, PANEL_Y + 54, LEFT_PANE_W - 40, PANEL_BORDER, COLOR_BORDER);

    int list_y = PANEL_Y + 66;
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
            draw_rect(LEFT_PANE_X + 12, yy + 2, 4, ITEM_H - 4, COLOR_GOLD);

            char tbuf[512];
            truncate_to_fit(games[i].display_name, tbuf, sizeof(tbuf), LEFT_PANE_W - 290, 38);
            draw_text(LEFT_PANE_X + 24, yy + (ITEM_H - 38) / 2, tbuf, COLOR_GOLD, 38);
        } else {
            char tbuf[512];
            truncate_to_fit(games[i].display_name, tbuf, sizeof(tbuf), LEFT_PANE_W - 290, 38);
            draw_text(LEFT_PANE_X + 44, yy + (ITEM_H - 38) / 2, tbuf, COLOR_TEXT, 38);
        }
    }

    if (count > visible) {
        int track_h = PANEL_BOT - list_y - 20;
        draw_rect(LEFT_PANE_X + LEFT_PANE_W - SCROLLBAR_PADDING, list_y, SCROLLBAR_WIDTH, track_h, COLOR_CARD);
        int thumb = track_h * visible / count;
        if (thumb < 24) thumb = 24;
        int ty = list_y + (track_h - thumb) * start / (count - visible > 0 ? count - visible : 1);
        draw_rect(LEFT_PANE_X + LEFT_PANE_W - SCROLLBAR_PADDING, ty, SCROLLBAR_WIDTH, thumb, COLOR_ACCENT);
    }
}

static void draw_game_details(int sel, int total_games) {
    draw_text_slot(RIGHT_PANE_X + 20, PANEL_Y + 16, "GAME DETAILS", COLOR_TEXT, 40, FONT_SLOT_BOLD);

    char cnt[32];
    snprintf(cnt, sizeof(cnt), "Total Games: %d", total_games);
    int cw = font_text_width(cnt, 26);
    draw_text(RIGHT_PANE_X + RIGHT_PANE_W - cw - 20, PANEL_Y + 20, cnt, COLOR_DIM, 26);

    draw_rect(RIGHT_PANE_X + 20, PANEL_Y + 54, RIGHT_PANE_W - 40, PANEL_BORDER, COLOR_BORDER);

    if (sel < 0 || sel >= game_count) return;
    Game *g = &games[sel];

    if (sel != g_last_selected_info) {
        scraper_get_game_info(g->id, g->display_name, &g_cached_info);
        scraper_download_cover_async(g->id);
        g_last_selected_info = sel;
    }

    cover_draw_fit(COVER_X, COVER_Y, COVER_W, COVER_H, g->id);

    int tx = COVER_X + COVER_W + 28;
    int ty = COVER_Y + 48;
    int line_gap = 32;

    draw_text_slot(tx, ty, "TITLE:", COLOR_GOLD, 28, FONT_SLOT_BOLD);
    ty += line_gap;
    char tbuf[512];
    truncate_to_fit(g->display_name, tbuf, sizeof(tbuf), RIGHT_PANE_W - 470, 36);
    draw_text(tx, ty, tbuf, COLOR_TEXT, 36);
    ty += line_gap + 16;

    draw_text_slot(tx, ty, "GAME ID:", COLOR_GOLD, 28, FONT_SLOT_BOLD);
    ty += line_gap;
    draw_text(tx, ty, g->id, COLOR_TEXT, 36);
    ty += line_gap + 16;

    draw_text_slot(tx, ty, "EMULATOR:", COLOR_GOLD, 28, FONT_SLOT_BOLD);
    ty += line_gap;
    const char *emu = g->emulator_name[0] ? g->emulator_name : "Default";
    draw_text(tx, ty, emu, COLOR_TEXT, 36);
    ty += line_gap + 16;

    draw_text_slot(tx, ty, "EMU ID:", COLOR_GOLD, 28, FONT_SLOT_BOLD);
    ty += line_gap;
    const char *emu_id = g->emulator_id[0] ? g->emulator_id : EMULATOR_TID;
    draw_text(tx, ty, emu_id, COLOR_TEXT, 36);

    int by = COVER_Y + COVER_H + 20;
    int bx = COVER_X;
    int bw = RIGHT_PANE_W - 56;

    char info[512], info_buf[512];

    const char *rel = g_cached_info.release_date[0] ? g_cached_info.release_date : (g->release_date[0] ? g->release_date : "N/A");
    const char *gnr = g_cached_info.genre[0] ? g_cached_info.genre : (g->genre[0] ? g->genre : "N/A");
    const char *dev = g_cached_info.developer[0] ? g_cached_info.developer : (g->developer[0] ? g->developer : "N/A");
    const char *pub = g_cached_info.publisher[0] ? g_cached_info.publisher : (g->publisher[0] ? g->publisher : "N/A");

    snprintf(info, sizeof(info), "RELEASE: %s  |  GENRE: %s", rel, gnr);
    truncate_to_fit(info, info_buf, sizeof(info_buf), bw, 24);
    draw_text(bx, by, info_buf, COLOR_DIM, 24);
    by += 28;

    snprintf(info, sizeof(info), "DEV: %s  |  PUB: %s", dev, pub);
    truncate_to_fit(info, info_buf, sizeof(info_buf), bw, 24);
    draw_text(bx, by, info_buf, COLOR_DIM, 24);
    by += 32;

    const char *desc = g_cached_info.description[0] ? g_cached_info.description : g->description;
    if (desc && desc[0]) {
        draw_text_slot(bx, by, "DESCRIPTION", COLOR_GOLD, 22, FONT_SLOT_BOLD);
        by += 24;
        draw_wrapped_text(bx, by, bw, PANEL_BOT - by - 16, desc, COLOR_TEXT, 20);
    }
}

static void draw_header(void) {
    draw_text_slot(SAFE_X, SAFE_Y, "PS2 ISO LAUNCHER", COLOR_ACCENT, 52, FONT_SLOT_TITLE);
    draw_text_slot(SAFE_X + 1, SAFE_Y, "PS2 ISO LAUNCHER", COLOR_ACCENT, 52, FONT_SLOT_TITLE);

    int tw = font_text_width_slot("tehrzky", 32, FONT_SLOT_TITLE);
    draw_text_slot(SAFE_X1 - tw, SAFE_Y + 8, "tehrzky", COLOR_ACCENT, 32, FONT_SLOT_TITLE);

    draw_rect(SAFE_X, HEADER_H, SCREEN_WIDTH - (SAFE_X * 2), PANEL_BORDER, COLOR_ACCENT);
}

static void draw_footer(int in_settings) {
    int y = SCREEN_HEIGHT - FOOTER_H;
    draw_rect(0, y, SCREEN_WIDTH, FOOTER_H, COLOR_PANEL);
    draw_rect(0, y, SCREEN_WIDTH, PANEL_BORDER, COLOR_ACCENT);

    int hy = y + 16;
    int x = SAFE_X + 20;

    if (in_settings) {
        draw_btn_hint(x, hy, "^v", "Navigate", COLOR_DIM); x += 240;
        draw_btn_hint(x, hy, "<>", "Change", COLOR_DIM); x += 240;
        draw_btn_hint(x, hy, "O", "Close", COLOR_ERROR);
    } else {
        draw_btn_hint(x, hy, "X", "LAUNCH", COLOR_ACCENT); x += 240;
        draw_btn_hint(x, hy, "SQU", "GAME CFG", COLOR_ACCENT); x += 260;
        draw_btn_hint(x, hy, "TRI", "SETTINGS", COLOR_ACCENT); x += 260;
        draw_btn_hint(x, hy, "^v", "SCROLL", COLOR_DIM); x += 240;
        draw_btn_hint(x, hy, "HOLD L2", "FAST SCROLL", COLOR_DIM);
    }
}

void draw_launcher_ui(int game_count_visible, int selected_idx, int total_games) {
    if (g_settings.wallpaper[0]) {
        cover_draw_wallpaper();
        if (g_settings.wallpaper_brightness < 100) {
            int alpha = (100 - g_settings.wallpaper_brightness) * 255 / 100;
            draw_rect(0, 0, SCREEN_WIDTH, SCREEN_HEIGHT, (uint32_t)(alpha << 24));
        }
    } else {
        memset(framebuffer[current_buf], 0, FB_SIZE);
        draw_rect(0, 0, SCREEN_WIDTH, SCREEN_HEIGHT, COLOR_BG);
    }

    draw_header();

    uint32_t panel_col = get_panel_color();

    draw_rounded_rect(LEFT_PANE_X, PANEL_Y, LEFT_PANE_W, PANEL_H, ROUND_RADIUS, COLOR_GOLD);
    draw_rounded_rect(LEFT_PANE_X + PANEL_BORDER, PANEL_Y + PANEL_BORDER, LEFT_PANE_W - PANEL_BORDER * 2, PANEL_H - PANEL_BORDER * 2, ROUND_RADIUS_SMALL, panel_col);

    draw_rounded_rect(RIGHT_PANE_X, PANEL_Y, RIGHT_PANE_W, PANEL_H, ROUND_RADIUS, COLOR_GOLD);
    draw_rounded_rect(RIGHT_PANE_X + PANEL_BORDER, PANEL_Y + PANEL_BORDER, RIGHT_PANE_W - PANEL_BORDER * 2, PANEL_H - PANEL_BORDER * 2, ROUND_RADIUS_SMALL, panel_col);

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

    if (g_download_active && g_download_status[0]) {
        int tw = font_text_width(g_download_status, 22);
        int tx = SCREEN_WIDTH / 2 - tw / 2 - 20;
        int ty = SCREEN_HEIGHT - FOOTER_H - 50;
        draw_rounded_rect(tx, ty, tw + 40, 36, 8, get_panel_color());
        draw_rounded_rect(tx + 1, ty + 1, tw + 38, 34, 7, COLOR_BG);
        draw_text(tx + 20, ty + 6, g_download_status, COLOR_ACCENT, 22);
    }
}

static const char *settings_labels[SETTINGS_ITEMS] = {
    "Auto Download Covers",
    "Auto Download GameIndex",
    "Cover Type",
    "Scraper URL",
    "Work Path",
    "Master Config",
    "Body Font",
    "Title Font",
    "Force Download GameIndex",
    "Force Download All Covers",
    "Panel Opacity",
    "Wallpaper Brightness"
};

void draw_settings_ui(int selected_item, int in_per_game) {
    int pw = SETTINGS_PW;
    int ph = SETTINGS_PH;
    int px = (SCREEN_WIDTH - pw) / 2;
    int py = (SCREEN_HEIGHT - ph) / 2;

    draw_rect(0, 0, SCREEN_WIDTH, py, 0xE60B141F);
    draw_rect(0, py + ph, SCREEN_WIDTH, SCREEN_HEIGHT - py - ph, 0xE60B141F);
    draw_rect(0, py, px, ph, 0xE60B141F);
    draw_rect(px + pw, py, SCREEN_WIDTH - px - pw, ph, 0xE60B141F);

    draw_rounded_rect(px, py, pw, ph, ROUND_RADIUS_LARGE, get_panel_color());
    draw_rounded_rect(px + PANEL_BORDER, py + PANEL_BORDER, pw - PANEL_BORDER * 2, ph - PANEL_BORDER * 2, ROUND_RADIUS_LARGE - PANEL_BORDER, COLOR_BG);

    const char *title = in_per_game ? "PER-GAME SETTINGS" : "SETTINGS";
    int tw = font_text_width_slot(title, 40, FONT_SLOT_TITLE);
    draw_text_slot(px + (pw - tw) / 2, py + 20, title, COLOR_GOLD, 40, FONT_SLOT_TITLE);

    int row_h = SETTINGS_ROW_H;
    int start_y = py + 90;

    for (int i = 0; i < SETTINGS_ITEMS; i++) {
        int ry = start_y + i * row_h;
        int is_sel = (i == selected_item);

        if (is_sel) {
            draw_rounded_rect(px + 20, ry, pw - 40, row_h - 6, 8, COLOR_CARD_SEL);
            draw_rect(px + 20, ry + 6, 4, row_h - 18, COLOR_ACCENT);
        }

        draw_text(px + 40, ry + 12, settings_labels[i], is_sel ? COLOR_TEXT : COLOR_DIM, 28);

        char value[196] = {0};
        if (i == 0) snprintf(value, sizeof(value), "%s", g_settings.auto_download_covers ? "ON" : "OFF");
        else if (i == 1) snprintf(value, sizeof(value), "%s", g_settings.auto_download_gameindex ? "ON" : "OFF");
        else if (i == 2) snprintf(value, sizeof(value), "%s", g_settings.cover_type == 1 ? "3D" : "Default");
        else if (i == 3) {
            truncate_to_fit(g_settings.scraper_base_url, value, sizeof(value),
                            pw - 300, 26);
        }
        else if (i == 4) {
            truncate_to_fit(g_settings.work_path, value, sizeof(value),
                            pw - 300, 26);
        }
        else if (i == 5) {
            truncate_to_fit(g_settings.master_config, value, sizeof(value),
                            pw - 300, 26);
        }
        else if (i == 6) {
            const char *name = font_get_list_name(g_settings.font_body);
            truncate_to_fit(name, value, sizeof(value), pw - 300, 26);
        }
        else if (i == 7) {
            const char *name = font_get_list_name(g_settings.font_title);
            truncate_to_fit(name, value, sizeof(value), pw - 300, 26);
        }
        else if (i == 8) snprintf(value, sizeof(value), "Press X");
        else if (i == 9) snprintf(value, sizeof(value), "Press X");
        else if (i == 10) snprintf(value, sizeof(value), "%d%%", g_settings.panel_opacity);
        else if (i == 11) snprintf(value, sizeof(value), "%d%%", g_settings.wallpaper_brightness);

        int vw = font_text_width(value, 26);
        uint32_t vc = COLOR_DIM;
        if (i == 3 || i == 4 || i == 5) vc = COLOR_MUTED;
        else if (i == 8 || i == 9) vc = is_sel ? COLOR_SUCCESS : COLOR_MUTED;
        else vc = is_sel ? COLOR_ACCENT : COLOR_DIM;
        draw_text(px + pw - 40 - vw, ry + 12, value, vc, 26);
    }

    draw_footer(1);
}