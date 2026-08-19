#include "ui.h"
#include "video.h"
#include "font.h"
#include "game.h"
#include <string.h>
#include <stdio.h>
#include <time.h>

// ============ UI CONSTANTS ============
#define SCREEN_WIDTH    1920
#define SCREEN_HEIGHT   1080
#define FB_SIZE         (SCREEN_WIDTH * SCREEN_HEIGHT * 4)

// ============ UI COLORS ============
#define COLOR_PRIMARY      0xFF1A2A3A
#define COLOR_SECONDARY    0xFF2A3A4A
#define COLOR_ACCENT       0xFF00BFFF
#define COLOR_ACCENT_DARK  0xFF0080A0
#define COLOR_GOLD         0xFFFFD700
#define COLOR_CARD         0xFF2A3A4A
#define COLOR_CARD_BORDER  0xFF3A4A5A
#define COLOR_TEXT_PRIMARY 0xFFFFFFFF
#define COLOR_TEXT_SECONDARY 0xFFCCCCCC   // Brighter gray
#define COLOR_TEXT_MUTED   0xFF999999     // Lighter muted
#define COLOR_HIGHLIGHT    0xFF336699     // Darker blue highlight (less bright)
#define COLOR_SUCCESS      0xFF00FF00
#define COLOR_ERROR        0xFFFF0000

static int scroll_timer = 0;
static int scroll_delay = 6;  // Frames per scroll step (slow)
static int fast_scroll = 0;   // Fast scroll active

void set_fast_scroll(int active) {
    fast_scroll = active;
}

void draw_rounded_rect(int x, int y, int w, int h, int radius, uint32_t color) {
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

// Draw cover placeholder (scaled up)
void draw_cover_placeholder(int x, int y, int w, int h, const char *text) {
    draw_rounded_rect(x, y, w, h, 8, COLOR_CARD_BORDER);
    draw_rounded_rect(x + 2, y + 2, w - 4, h - 4, 6, COLOR_SECONDARY);
    
    int cx = x + w / 2;
    int cy = y + h / 2 - 10;
    int radius = 60;  // Larger CD icon
    
    for (int yy = -radius; yy <= radius; yy++) {
        for (int xx = -radius; xx <= radius; xx++) {
            if ((xx * xx + yy * yy) <= (radius * radius)) {
                draw_pixel(cx + xx, cy + yy, COLOR_TEXT_MUTED);
            }
        }
    }
    for (int yy = -15; yy <= 15; yy++) {
        for (int xx = -15; xx <= 15; xx++) {
            if ((xx * xx + yy * yy) <= (15 * 15)) {
                draw_pixel(cx + xx, cy + yy, COLOR_PRIMARY);
            }
        }
    }
    
    // Game name on the cover
    draw_text_scaled(x + 10, y + h - 35, text, COLOR_TEXT_SECONDARY, 2);
}

// Draw game list on left side
void draw_game_list(int x, int y, int w, int h, int game_count, int scroll_pos) {
    // Background
    draw_rect(x, y, w, h, COLOR_SECONDARY);
    
    int row_height = 50;  // Taller rows
    int visible = h / row_height;
    int total = game_count;
    
    // Draw list items
    for (int i = scroll_pos; i < total && i < scroll_pos + visible; i++) {
        int y_pos = y + (i - scroll_pos) * row_height;
        int is_selected = (i == selected);
        
        // Highlight
        if (is_selected) {
            draw_rect(x + 4, y_pos + 2, w - 8, row_height - 4, COLOR_HIGHLIGHT);
        }
        
        // Game name (larger font)
        draw_text_scaled(x + 15, y_pos + 12, games[i].display_name, 
                         is_selected ? COLOR_GOLD : COLOR_TEXT_PRIMARY, 2);
        
        // Show disc ID in smaller text
        if (games[i].id[0] != '\0') {
            char id_text[64];
            snprintf(id_text, sizeof(id_text), "[%s]", games[i].id);
            draw_text_scaled(x + w - 200, y_pos + 15, id_text, COLOR_TEXT_MUTED, 1);
        }
        
        // Small separator line
        if (i < total - 1) {
            draw_rect(x + 10, y_pos + row_height - 1, w - 20, 1, COLOR_CARD_BORDER);
        }
    }
    
    // Scroll indicator
    if (total > visible) {
        float ratio = (float)scroll_pos / (total - visible);
        int bar_y = y + 10 + ratio * (h - 20);
        draw_rect(x + w - 6, bar_y, 4, 20, COLOR_ACCENT);
    }
}

// Draw right panel with game details
void draw_game_details(int x, int y, int w, int h, Game *game) {
    // Background
    draw_rounded_rect(x, y, w, h, 10, COLOR_CARD_BORDER);
    draw_rounded_rect(x + 2, y + 2, w - 4, h - 4, 8, COLOR_CARD);
    
    int padding = 20;
    int current_y = y + padding;
    
    // Cover placeholder (larger)
    int cover_size = w - padding * 2;
    draw_cover_placeholder(x + padding, current_y, cover_size, cover_size, "DVD");
    current_y += cover_size + padding;
    
    // Game name (large)
    draw_text_scaled(x + padding, current_y, game->display_name, COLOR_GOLD, 3);
    current_y += 50;
    
    // Disc ID
    if (game->id[0] != '\0') {
        char id_text[128];
        snprintf(id_text, sizeof(id_text), "ID: %s", game->id);
        draw_text_scaled(x + padding, current_y, id_text, COLOR_TEXT_SECONDARY, 2);
        current_y += 35;
    }
    
    // Region
    if (game->id[0] != '\0' && strlen(game->id) >= 4) {
        char region[4];
        strncpy(region, game->id + 2, 2);
        region[2] = '\0';
        char region_name[16];
        if (strcmp(region, "US") == 0) snprintf(region_name, sizeof(region_name), "Region: USA");
        else if (strcmp(region, "EU") == 0) snprintf(region_name, sizeof(region_name), "Region: Europe");
        else if (strcmp(region, "JP") == 0) snprintf(region_name, sizeof(region_name), "Region: Japan");
        else snprintf(region_name, sizeof(region_name), "Region: Unknown");
        draw_text_scaled(x + padding, current_y, region_name, COLOR_TEXT_MUTED, 2);
        current_y += 35;
    }
    
    // ISO filename (smaller)
    char file_text[256];
    snprintf(file_text, sizeof(file_text), "File: %s", game->name);
    draw_text_scaled(x + padding, current_y, file_text, COLOR_TEXT_MUTED, 1);
}

void draw_header(int x, int y, int w, int game_count) {
    draw_rect(x, y, w, 70, COLOR_PRIMARY);
    draw_rect(x, y + 68, w, 2, COLOR_ACCENT);
    
    draw_text_scaled(x + 30, y + 18, "PS2 ISO LAUNCHER", COLOR_GOLD, 4);
    
    char status[64];
    snprintf(status, sizeof(status), "%d games loaded", game_count);
    draw_text_scaled(x + w - 180, y + 24, status, COLOR_TEXT_SECONDARY, 2);
}

void draw_footer(int x, int y, int w) {
    draw_rect(x, y, w, 2, COLOR_ACCENT);
    
    int y_pos = y + 15;
    draw_text_scaled(x + 30, y_pos, "[X] Launch", COLOR_GOLD, 2);
    draw_text_scaled(x + 150, y_pos, "[UP/DOWN] Select", COLOR_TEXT_SECONDARY, 2);
    draw_text_scaled(x + 340, y_pos, "[L2+UP/DOWN] Fast", COLOR_TEXT_SECONDARY, 2);
    draw_text_scaled(x + 530, y_pos, "[O] Exit", COLOR_TEXT_SECONDARY, 2);
}

void draw_launcher_ui(int game_count, int selected) {
    memset(framebuffer[current_buf], 0, FB_SIZE);
    draw_rect(0, 0, SCREEN_WIDTH, SCREEN_HEIGHT, COLOR_PRIMARY);
    
    // Subtle grid
    for (int y = 0; y < SCREEN_HEIGHT; y += 60) {
        for (int x = 0; x < SCREEN_WIDTH; x += 60) {
            draw_pixel(x, y, COLOR_SECONDARY);
        }
    }
    
    draw_header(0, 0, SCREEN_WIDTH, game_count);
    
    int header_h = 70;
    int footer_h = 60;
    int left_panel_w = SCREEN_WIDTH * 0.55;  // 55% for game list
    int right_panel_w = SCREEN_WIDTH - left_panel_w;
    int panel_y = header_h;
    int panel_h = SCREEN_HEIGHT - header_h - footer_h;
    
    // Game list (left)
    draw_game_list(0, panel_y, left_panel_w, panel_h, game_count, 
                   selected < (panel_h / 50) ? 0 : selected - (panel_h / 50) + 1);
    
    // Right panel (game details)
    if (game_count > 0) {
        draw_game_details(left_panel_w, panel_y, right_panel_w, panel_h, &games[selected]);
    } else {
        draw_text_scaled(left_panel_w + 50, panel_y + 200, "No games found", COLOR_TEXT_MUTED, 3);
        draw_text_scaled(left_panel_w + 50, panel_y + 250, "Place ISOs in /data/PS4ROMS/PS2ISO/", COLOR_TEXT_MUTED, 2);
    }
    
    draw_footer(0, SCREEN_HEIGHT - footer_h, SCREEN_WIDTH);
}
