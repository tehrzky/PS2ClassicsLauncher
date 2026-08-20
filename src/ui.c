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
#define COLOR_CARD_SELECTED 0xFF213145
#define COLOR_BORDER 0xFF2E4256
#define COLOR_ACCENT 0xFF3FA9F5
#define COLOR_GOLD 0xFFF5C542
#define COLOR_TEXT 0xFFF2F5F8
#define COLOR_TEXT_DIM 0xFFA6B4C4
#define COLOR_TEXT_MUTED 0xFF6C7C8E
#define COLOR_SUCCESS 0xFF3ED598
#define COLOR_ERROR 0xFFFF5C5C

#define HEADER_HEIGHT 100
#define FOOTER_HEIGHT 76

#define LIST_X SAFE_X
#define LIST_Y (HEADER_HEIGHT + 24)
#define LIST_WIDTH 1050
#define LIST_BOTTOM (SCREEN_HEIGHT - FOOTER_HEIGHT - 24)

#define SCROLLBAR_X (LIST_X + LIST_WIDTH + 8)
#define SCROLLBAR_WIDTH 4

#define DIVIDER_X (SCROLLBAR_X + 24)
#define RIGHT_PANE_X (DIVIDER_X + 24)
#define RIGHT_PANE_WIDTH (SAFE_X1 - RIGHT_PANE_X)

#define ITEM_HEIGHT 56
#define ITEM_GAP 6

// Cover box: 55% of right panel height
#define COVER_BOX_H (int)((LIST_BOTTOM - LIST_Y - 40) * 0.55f)
#define COVER_BOX_W (RIGHT_PANE_WIDTH - 40)
#define COVER_X (RIGHT_PANE_X + 20)
#define COVER_Y (LIST_Y + 20)

static void draw_triangle_right(int x, int y, int size, uint32_t color) {
    for (int row = 0; row < size; row++) {
        int half = size / 2;
        int width = (row <= half) ? (row + 1) : (size - row);
        draw_rect(x, y + row, width, 1, color);
    }
}

static void truncate_to_width(const char *s, char *out, size_t out_len, int max_width_px, int scale) {
    int max_chars = max_width_px / (FONT_WIDTH * scale);
    if (max_chars < 1) max_chars = 1;
    size_t len = strlen(s);
    if ((int)len <= max_chars || out_len < 5) {
        snprintf(out, out_len, "%s", s);
        return;
    }
    int keep = max_chars - 3;
    if (keep < 1) keep = 1;
    if (keep > (int)out_len - 4) keep = (int)out_len - 4;
    snprintf(out, out_len, "%.*s...", keep, s);
}

static void draw_button_hint(int x, int y, const char *button, const char *label, uint32_t button_color) {
    int pill_w = 8 * 2 * (int)strlen(button) + 20;
    int pill_h = 32;
    draw_rounded_rect(x, y, pill_w, pill_h, pill_h / 2, button_color);
    draw_text_scaled(x + 10, y + 8, button, COLOR_BG, 2);
    draw_text_scaled(x + pill_w + 14, y + 8, label, COLOR_TEXT_DIM, 2);
}

static void draw_game_list(int selected_idx, int count) {
    int visible = (LIST_BOTTOM - LIST_Y) / (ITEM_HEIGHT + ITEM_GAP);
    if (visible < 1) visible = 1;
    int start = 0;
    if (selected_idx >= visible) start = selected_idx - visible + 1;
    if (start < 0) start = 0;

    for (int i = start; i < count && i < start + visible; i++) {
        int y_pos = LIST_Y + (i - start) * (ITEM_HEIGHT + ITEM_GAP);
        int is_sel = (i == selected_idx);

        uint32_t border = is_sel ? COLOR_ACCENT : COLOR_BORDER;
        uint32_t fill = is_sel ? COLOR_CARD_SELECTED : COLOR_CARD;

        draw_rounded_rect(LIST_X, y_pos, LIST_WIDTH, ITEM_HEIGHT, 8, border);
        draw_rounded_rect(LIST_X + 2, y_pos + 2, LIST_WIDTH - 4, ITEM_HEIGHT - 4, 6, fill);

        int text_x = LIST_X + 24;
        if (is_sel) {
            draw_rect(LIST_X, y_pos + 6, 5, ITEM_HEIGHT - 12, COLOR_ACCENT);
            draw_triangle_right(LIST_X + 16, y_pos + ITEM_HEIGHT / 2 - 7, 14, COLOR_GOLD);
            text_x = LIST_X + 42;
        }

        char title_buf[64];
        truncate_to_width(games[i].display_name, title_buf, sizeof(title_buf),
                          LIST_WIDTH - (text_x - LIST_X) - 16, 2);
        draw_text_scaled(text_x, y_pos + (ITEM_HEIGHT - 16) / 2,
                         title_buf, is_sel ? COLOR_GOLD : COLOR_TEXT, 2);
    }

    if (count > visible) {
        int track_h = LIST_BOTTOM - LIST_Y;
        draw_rect(SCROLLBAR_X, LIST_Y, SCROLLBAR_WIDTH, track_h, COLOR_BORDER);
        int thumb_h = track_h * visible / count;
        if (thumb_h < 24) thumb_h = 24;
        int thumb_y = LIST_Y + (track_h - thumb_h) * start / (count - visible > 0 ? count - visible : 1);
        draw_rect(SCROLLBAR_X, thumb_y, SCROLLBAR_WIDTH, thumb_h, COLOR_ACCENT);
    }
}

static void draw_game_details(int selected_idx) {
    if (selected_idx < 0 || selected_idx >= game_count) return;
    Game *g = &games[selected_idx];

    int panel_y = COVER_Y - 20;
    int panel_h = LIST_BOTTOM - panel_y;
    draw_rounded_rect(RIGHT_PANE_X - 20, panel_y, RIGHT_PANE_WIDTH + 40, panel_h, 14, COLOR_PANEL);

    // Draw cover with aspect ratio preserved
    cover_draw_fit(COVER_X, COVER_Y, COVER_BOX_W, COVER_BOX_H, g->id);

    int info_y = COVER_Y + COVER_BOX_H + 30;

    char title_buf[48];
    truncate_to_width(g->display_name, title_buf, sizeof(title_buf), RIGHT_PANE_WIDTH - 20, 3);
    draw_text_scaled(RIGHT_PANE_X + 10, info_y, title_buf, COLOR_GOLD, 3);

    info_y += 46;
    char id_label[64];
    snprintf(id_label, sizeof(id_label), "Disc ID: %s", g->id);
    draw_text_scaled(RIGHT_PANE_X + 10, info_y, id_label, COLOR_TEXT, 2);

    info_y += 34;
    if (strlen(g->id) >= 4) {
        char region_code[3];
        strncpy(region_code, g->id + 2, 2);
        region_code[2] = '\0';
        char region[24];
        if (strcmp(region_code, "US") == 0) snprintf(region, sizeof(region), "Region: USA");
        else if (strcmp(region_code, "EU") == 0) snprintf(region, sizeof(region), "Region: Europe");
        else if (strcmp(region_code, "JP") == 0) snprintf(region, sizeof(region), "Region: Japan");
        else snprintf(region, sizeof(region), "Region: %s", region_code);
        draw_text_scaled(RIGHT_PANE_X + 10, info_y, region, COLOR_TEXT_DIM, 2);
    }

    info_y += 34;
    char emu_label[80];
    if (g->emulator_name[0]) {
        snprintf(emu_label, sizeof(emu_label), "Emulator: %s", g->emulator_name);
    } else {
        snprintf(emu_label, sizeof(emu_label), "Emulator: Default");
    }
    draw_text_scaled(RIGHT_PANE_X + 10, info_y, emu_label, COLOR_TEXT_DIM, 2);

    info_y += 34;
    char emu_id_label[64];
    if (g->emulator_id[0]) {
        snprintf(emu_id_label, sizeof(emu_id_label), "Emulator ID: %s", g->emulator_id);
    } else {
        snprintf(emu_id_label, sizeof(emu_id_label), "Emulator ID: %s", EMULATOR_TID);
    }
    draw_text_scaled(RIGHT_PANE_X + 10, info_y, emu_id_label, COLOR_TEXT_DIM, 2);

    info_y += 34;
    char file_buf[80];
    truncate_to_width(g->name, file_buf, sizeof(file_buf), RIGHT_PANE_WIDTH - 20, 1);
    draw_text_scaled(RIGHT_PANE_X + 10, info_y, file_buf, COLOR_TEXT_MUTED, 1);

    info_y = panel_y + panel_h - 30;
    char index_str[32];
    snprintf(index_str, sizeof(index_str), "Game %d of %d", selected_idx + 1, game_count);
    int index_w = (int)strlen(index_str) * FONT_WIDTH * 1;
    draw_text_scaled(RIGHT_PANE_X + RIGHT_PANE_WIDTH - index_w - 10, info_y, index_str, COLOR_TEXT_MUTED, 1);
}

static void draw_header(int total_games) {
    draw_rect(0, 0, SCREEN_WIDTH, HEADER_HEIGHT, COLOR_BG);
    draw_rect(0, 0, SCREEN_WIDTH, HEADER_HEIGHT - 2, COLOR_PANEL);
    draw_rect(0, HEADER_HEIGHT - 3, SCREEN_WIDTH, 3, COLOR_ACCENT);

    draw_text_scaled(SAFE_X, SAFE_Y, "PS2 ISO LAUNCHER", COLOR_GOLD, 3);

    char count_str[32];
    snprintf(count_str, sizeof(count_str), "%d GAMES", total_games);
    int w = (int)strlen(count_str) * FONT_WIDTH * 2;
    draw_text_scaled(SAFE_X1 - w, SAFE_Y + 6, count_str, COLOR_TEXT_DIM, 2);

    draw_text_scaled(SAFE_X, SAFE_Y + 48, "Select a game and press X to launch", COLOR_TEXT_DIM, 1);
}

static void draw_footer(int in_settings) {
    int y = SCREEN_HEIGHT - FOOTER_HEIGHT;
    draw_rect(0, y, SCREEN_WIDTH, FOOTER_HEIGHT, COLOR_PANEL);
    draw_rect(0, y, SCREEN_WIDTH, 3, COLOR_ACCENT);

    int hint_y = SAFE_Y1 - 32;
    int x = SAFE_X;
    if (in_settings) {
        draw_button_hint(x, hint_y, "^v", "Navigate", COLOR_TEXT_DIM); x += 220;
        draw_button_hint(x, hint_y, "<>", "Change", COLOR_TEXT_DIM); x += 220;
        draw_button_hint(x, hint_y, "O", "Close", COLOR_ERROR);
    } else {
        draw_button_hint(x, hint_y, "X", "Launch", COLOR_ACCENT); x += 190;
        draw_button_hint(x, hint_y, "^v", "Select", COLOR_TEXT_DIM); x += 220;
        draw_button_hint(x, hint_y, "L2", "Fast", COLOR_TEXT_DIM); x += 220;
        draw_button_hint(x, hint_y, "TRI", "Settings", COLOR_ACCENT); x += 260;
        draw_button_hint(x, hint_y, "O", "Exit", COLOR_ERROR);
    }
}

void draw_launcher_ui(int game_count_visible, int selected_idx, int total_games) {
    memset(framebuffer[current_buf], 0, FB_SIZE);
    draw_rect(0, 0, SCREEN_WIDTH, SCREEN_HEIGHT, COLOR_BG);

    draw_rect(DIVIDER_X, HEADER_HEIGHT + 10, 2, SCREEN_HEIGHT - HEADER_HEIGHT - FOOTER_HEIGHT - 20, COLOR_BORDER);

    draw_header(total_games);
    draw_game_list(selected_idx, game_count_visible);
    draw_game_details(selected_idx);
    draw_footer(0);

    if (game_count_visible == 0) {
        const char *msg1 = "NO ISO FILES FOUND";
        const char *msg2 = "Place ISOs in /data/PS4ROMS/PS2ISO/";
        int w1 = (int)strlen(msg1) * FONT_WIDTH * 3;
        int w2 = (int)strlen(msg2) * FONT_WIDTH * 2;
        draw_text_scaled(SCREEN_WIDTH / 2 - w1 / 2, SCREEN_HEIGHT / 2 - 20, msg1, COLOR_ERROR, 3);
        draw_text_scaled(SCREEN_WIDTH / 2 - w2 / 2, SCREEN_HEIGHT / 2 + 30, msg2, COLOR_TEXT_DIM, 2);
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
    // Darken background
    for (int yy = 0; yy < SCREEN_HEIGHT; yy++) {
        for (int xx = 0; xx < SCREEN_WIDTH; xx++) {
            uint32_t c = framebuffer[current_buf][yy * SCREEN_WIDTH + xx];
            uint8_t a = (c >> 24) & 0xFF;
            uint8_t r = (c >> 16) & 0xFF;
            uint8_t g = (c >> 8)  & 0xFF;
            uint8_t b = c & 0xFF;
            r = (r * 3) / 10;
            g = (g * 3) / 10;
            b = (b * 3) / 10;
            framebuffer[current_buf][yy * SCREEN_WIDTH + xx] = (a << 24) | (r << 16) | (g << 8) | b;
        }
    }

    int pw = 700;
    int ph = 520;
    int px = (SCREEN_WIDTH - pw) / 2;
    int py = (SCREEN_HEIGHT - ph) / 2;

    draw_rounded_rect(px, py, pw, ph, 16, COLOR_PANEL);
    draw_rounded_rect(px + 2, py + 2, pw - 4, ph - 4, 14, COLOR_BG);

    const char *title = in_per_game ? "PER-GAME SETTINGS" : "SETTINGS";
    int tw = (int)strlen(title) * FONT_WIDTH * 3;
    draw_text_scaled(px + (pw - tw) / 2, py + 20, title, COLOR_GOLD, 3);

    int row_h = 56;
    int start_y = py + 90;

    for (int i = 0; i < SETTINGS_ITEMS; i++) {
        int ry = start_y + i * row_h;
        int is_sel = (i == selected_item);

        if (is_sel) {
            draw_rounded_rect(px + 20, ry, pw - 40, row_h - 6, 8, COLOR_CARD_SELECTED);
            draw_rect(px + 20, ry + 6, 4, row_h - 18, COLOR_ACCENT);
        }

        draw_text_scaled(px + 40, ry + 14, settings_labels[i], is_sel ? COLOR_TEXT : COLOR_TEXT_DIM, 2);

        char value[128] = {0};
        if (i == 0) snprintf(value, sizeof(value), "%s", g_settings.auto_download_covers ? "ON" : "OFF");
        else if (i == 1) snprintf(value, sizeof(value), "%s", g_settings.auto_download_gameindex ? "ON" : "OFF");
        else if (i == 2) snprintf(value, sizeof(value), "%s", g_settings.cover_type == 1 ? "3D" : "Default");
        else if (i == 3) {
            strncpy(value, g_settings.scraper_base_url, sizeof(value) - 1);
            value[sizeof(value) - 1] = '\0';
        }
        else if (i == 4) snprintf(value, sizeof(value), "Press X");
        else if (i == 5) snprintf(value, sizeof(value), "Press X");

        int vw = (int)strlen(value) * FONT_WIDTH * 2;
        uint32_t vc = (i == 3) ? COLOR_TEXT_MUTED : (is_sel ? COLOR_ACCENT : COLOR_TEXT_DIM);
        if (i >= 4) vc = is_sel ? COLOR_SUCCESS : COLOR_TEXT_MUTED;
        draw_text_scaled(px + pw - 40 - vw, ry + 14, value, vc, 2);
    }

    draw_footer(1);
}
