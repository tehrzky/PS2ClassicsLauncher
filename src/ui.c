#include "ui.h"
#include "video.h"
#include "font.h"
#include "game.h"
#include <string.h>
#include <stdio.h>

// ============ UI CONSTANTS ============
#define SCREEN_WIDTH    1920
#define SCREEN_HEIGHT   1080
#define FB_SIZE         (SCREEN_WIDTH * SCREEN_HEIGHT * 4)

// ============ COLORS ============
#define COLOR_BG          0xFF0A1A2A      // darker navy
#define COLOR_PANEL       0xFF1A2A3A      // panel background
#define COLOR_HIGHLIGHT   0xFF2A5A7A      // softer highlight (not too bright)
#define COLOR_BORDER      0xFF3A5A7A
#define COLOR_TEXT        0xFFFFFFFF
#define COLOR_TEXT_DIM    0xFFAAAAAA
#define COLOR_GOLD        0xFFFFD700
#define COLOR_ACCENT      0xFF00BFFF

// ============ LAYOUT ============
#define LEFT_PANE_WIDTH   640             // 1/3 of screen for game list
#define RIGHT_PANE_X      (LEFT_PANE_WIDTH + 10)
#define RIGHT_PANE_WIDTH  (SCREEN_WIDTH - RIGHT_PANE_X - 20)

#define HEADER_HEIGHT     80
#define FOOTER_HEIGHT     60
#define LIST_TOP          (HEADER_HEIGHT + 10)
#define LIST_BOTTOM       (SCREEN_HEIGHT - FOOTER_HEIGHT - 10)
#define ITEM_HEIGHT       48              // taller for bigger font
#define ITEM_PADDING      6

#define COVER_SIZE        350             // larger cover art
#define COVER_X           (RIGHT_PANE_X + (RIGHT_PANE_WIDTH - COVER_SIZE) / 2)
#define COVER_Y           (HEADER_HEIGHT + 30)

// ============ DRAWING HELPERS ============
static void draw_rounded_rect(int x, int y, int w, int h, int radius, uint32_t color) {
    draw_rect(x + radius, y, w - radius * 2, h, color);
    draw_rect(x, y + radius, w, h - radius * 2, color);
    for (int yy = 0; yy < radius; yy++) {
        for (int xx = 0; xx < radius; xx++) {
            if ((xx * xx + yy * yy) <= (radius * radius)) {
                draw_pixel(x + radius - xx, y + radius - yy, color);
                draw_pixel(x + w - radius + xx, y + radius - yy, color);
                draw_pixel(x + radius - xx, y + h - radius + yy, color);
                draw_pixel(x + w - radius + xx, y + h - radius + yy, color);
            }
        }
    }
}

// ============ DRAW COVER PLACEHOLDER ============
static void draw_cover_placeholder(int x, int y, int w, int h) {
    // Outer border (like a DVD case)
    draw_rounded_rect(x, y, w, h, 12, 0xFF444444);
    draw_rounded_rect(x + 4, y + 4, w - 8, h - 8, 8, 0xFF222222);
    
    // Inner disc shape
    int cx = x + w/2;
    int cy = y + h/2 - 10;
    int radius = w/3;
    for (int yy = -radius; yy <= radius; yy++) {
        for (int xx = -radius; xx <= radius; xx++) {
            if ((xx*xx + yy*yy) <= (radius*radius)) {
                draw_pixel(cx + xx, cy + yy, 0xFF666666);
            }
        }
    }
    // inner hole
    for (int yy = -12; yy <= 12; yy++) {
        for (int xx = -12; xx <= 12; xx++) {
            if ((xx*xx + yy*yy) <= (12*12)) {
                draw_pixel(cx + xx, cy + yy, 0xFF222222);
            }
        }
    }
    // small label
    draw_text_scaled(cx - 40, cy + radius + 20, "No Cover", COLOR_TEXT_DIM, 2);
}

// ============ DRAW GAME LIST ============
static void draw_game_list(int selected, int game_count) {
    int y = LIST_TOP;
    int visible = (LIST_BOTTOM - LIST_TOP) / ITEM_HEIGHT;
    int start = 0;
    if (selected >= visible) start = selected - visible + 1;
    if (start < 0) start = 0;
    
    for (int i = start; i < game_count && i < start + visible; i++) {
        int y_pos = LIST_TOP + (i - start) * ITEM_HEIGHT;
        uint32_t bg = (i == selected) ? COLOR_HIGHLIGHT : COLOR_BG;
        // highlight bar
        if (i == selected) {
            draw_rect(4, y_pos - 2, LEFT_PANE_WIDTH - 8, ITEM_HEIGHT - 4, bg);
            // small arrow indicator
            draw_text_scaled(10, y_pos + 4, "▶", COLOR_GOLD, 2);
        }
        // game title with larger font (scale 2)
        draw_text_scaled(30, y_pos + 4, games[i].display_name, COLOR_TEXT, 2);
        // draw small disc ID on the right of the list item
        char id_buf[16];
        snprintf(id_buf, sizeof(id_buf), "%s", games[i].id);
        draw_text_scaled(LEFT_PANE_WIDTH - 120, y_pos + 4, id_buf, COLOR_TEXT_DIM, 1);
    }
}

// ============ DRAW RIGHT PANE (DETAILS) ============
static void draw_game_details(int selected) {
    if (selected < 0 || selected >= game_count) return;
    Game *g = &games[selected];
    
    // Draw cover art (large)
    draw_cover_placeholder(COVER_X, COVER_Y, COVER_SIZE, COVER_SIZE);
    
    // Game title (large)
    int info_y = COVER_Y + COVER_SIZE + 30;
    draw_text_scaled(RIGHT_PANE_X + 20, info_y, g->display_name, COLOR_GOLD, 3);
    
    // Disc ID
    info_y += 45;
    char id_label[64];
    snprintf(id_label, sizeof(id_label), "Disc ID: %s", g->id);
    draw_text_scaled(RIGHT_PANE_X + 20, info_y, id_label, COLOR_TEXT, 2);
    
    // Region (derive from ID)
    if (strlen(g->id) >= 4) {
        char region_code[3];
        strncpy(region_code, g->id + 2, 2);
        region_code[2] = '\0';
        char region[16];
        if (strcmp(region_code, "US") == 0) snprintf(region, sizeof(region), "Region: USA");
        else if (strcmp(region_code, "EU") == 0) snprintf(region, sizeof(region), "Region: Europe");
        else if (strcmp(region_code, "JP") == 0) snprintf(region, sizeof(region), "Region: Japan");
        else snprintf(region, sizeof(region), "Region: %s", region_code);
        info_y += 35;
        draw_text_scaled(RIGHT_PANE_X + 20, info_y, region, COLOR_TEXT_DIM, 2);
    }
    
    // ISO filename (optional)
    info_y += 35;
    draw_text_scaled(RIGHT_PANE_X + 20, info_y, g->name, COLOR_TEXT_DIM, 1);
}

// ============ DRAW HEADER ============
static void draw_header(int game_count) {
    draw_rect(0, 0, SCREEN_WIDTH, HEADER_HEIGHT, COLOR_BG);
    draw_rect(0, HEADER_HEIGHT-2, SCREEN_WIDTH, 2, COLOR_ACCENT);
    
    draw_text_scaled(30, 18, "PS2 ISO LAUNCHER", COLOR_GOLD, 3);
    char count_str[64];
    snprintf(count_str, sizeof(count_str), "%d games", game_count);
    draw_text_scaled(SCREEN_WIDTH - 200, 22, count_str, COLOR_TEXT_DIM, 2);
}

// ============ DRAW FOOTER ============
static void draw_footer(void) {
    int y = SCREEN_HEIGHT - FOOTER_HEIGHT;
    draw_rect(0, y, SCREEN_WIDTH, FOOTER_HEIGHT, COLOR_BG);
    draw_rect(0, y, SCREEN_WIDTH, 2, COLOR_ACCENT);
    
    draw_text_scaled(30, y + 18, "[X] Launch   [UP/DOWN] Select   [L2+UP/DOWN] Fast Scroll   [O] Exit", COLOR_TEXT_DIM, 2);
}

// ============ MAIN DRAW FUNCTION ============
void draw_launcher_ui(int game_count, int selected, int total_games) {
    // Clear screen
    memset(framebuffer[current_buf], 0, FB_SIZE);
    draw_rect(0, 0, SCREEN_WIDTH, SCREEN_HEIGHT, COLOR_BG);
    
    // Draw divider line between panes
    draw_rect(LEFT_PANE_WIDTH, 0, 2, SCREEN_HEIGHT, COLOR_BORDER);
    
    // Header
    draw_header(total_games);
    
    // Game list (left pane)
    draw_game_list(selected, game_count);
    
    // Details (right pane)
    draw_game_details(selected);
    
    // Footer
    draw_footer();
    
    // If no games
    if (game_count == 0) {
        draw_text_scaled(SCREEN_WIDTH/2 - 150, SCREEN_HEIGHT/2, "NO ISO FILES FOUND", 0xFFFF0000, 3);
        draw_text_scaled(SCREEN_WIDTH/2 - 200, SCREEN_HEIGHT/2 + 60, "Place ISOs in /data/PS4ROMS/PS2ISO/", COLOR_TEXT_DIM, 2);
    }
}
