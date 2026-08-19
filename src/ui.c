#include "ui.h"
#include "video.h"
#include "font.h"
#include "game.h"
#include "config.h"     // <-- ADD THIS for EMULATOR_TID
#include <string.h>
#include <stdio.h>

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
#define COLOR_TEXT_SECONDARY 0xFFAABBCC
#define COLOR_TEXT_MUTED   0xFF8899AA
#define COLOR_SUCCESS      0xFF00FF00
#define COLOR_ERROR        0xFFFF0000
#define COLOR_HIGHLIGHT    0xFF2A4A5A
#define COLOR_SELECTED_BG  0xFF2A3A5A
#define COLOR_DIVIDER      0xFF3A4A5A

// ============ EXTERNAL VARIABLES ============
extern Game games[256];
extern int game_count;
extern int selected;

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

static void draw_cover_placeholder(int x, int y, int w, int h) {
    // Border
    draw_rounded_rect(x, y, w, h, 12, COLOR_DIVIDER);
    draw_rounded_rect(x + 3, y + 3, w - 6, h - 6, 10, COLOR_SECONDARY);
    
    // CD/DVD icon
    int cx = x + w / 2;
    int cy = y + h / 2;
    int radius = (w < h ? w : h) / 4;
    if (radius > 80) radius = 80;
    
    // Outer circle
    for (int yy = -radius; yy <= radius; yy++) {
        for (int xx = -radius; xx <= radius; xx++) {
            if ((xx * xx + yy * yy) <= (radius * radius)) {
                draw_pixel(cx + xx, cy + yy, COLOR_TEXT_MUTED);
            }
        }
    }
    // Inner circle
    int inner = radius / 3;
    for (int yy = -inner; yy <= inner; yy++) {
        for (int xx = -inner; xx <= inner; xx++) {
            if ((xx * xx + yy * yy) <= (inner * inner)) {
                draw_pixel(cx + xx, cy + yy, COLOR_PRIMARY);
            }
        }
    }
    // Center hole
    int hole = inner / 3;
    for (int yy = -hole; yy <= hole; yy++) {
        for (int xx = -hole; xx <= hole; xx++) {
            if ((xx * xx + yy * yy) <= (hole * hole)) {
                draw_pixel(cx + xx, cy + yy, COLOR_SECONDARY);
            }
        }
    }
}

static void draw_header(int x, int y, int w) {
    draw_rect(x, y, w, 70, COLOR_PRIMARY);
    draw_rect(x, y + 68, w, 2, COLOR_ACCENT);
    
    draw_text_scaled(x + 40, y + 15, "PS2 ISO LAUNCHER", COLOR_GOLD, 3);
    draw_text_scaled(x + 40, y + 48, "Select a game and press X to launch", COLOR_TEXT_SECONDARY, 1);
    
    char status[64];
    snprintf(status, sizeof(status), "%d games loaded", game_count);
    draw_text_scaled(x + w - 200, y + 20, status, COLOR_TEXT_MUTED, 1);
}

static void draw_footer(int x, int y, int w) {
    draw_rect(x, y, w, 2, COLOR_ACCENT);
    
    int y_pos = y + 15;
    draw_text_scaled(x + 40, y_pos, "[X] LAUNCH", COLOR_GOLD, 1);
    draw_text_scaled(x + 160, y_pos, "[UP/DOWN] SELECT", COLOR_TEXT_SECONDARY, 1);
    draw_text_scaled(x + 340, y_pos, "[L2+UP/DOWN] FAST", COLOR_TEXT_SECONDARY, 1);
    draw_text_scaled(x + 500, y_pos, "[O] BACK", COLOR_TEXT_SECONDARY, 1);
}

// ============ GAME LIST (LEFT PANEL) ============
static void draw_game_list(int x, int y, int w, int h) {
    // List background
    draw_rect(x, y, w, h, COLOR_PRIMARY);
    draw_rect(x + w - 1, y, 1, h, COLOR_DIVIDER);
    
    int item_height = 32;
    int padding = 10;
    int start_y = y + padding;
    int visible = (h - padding * 2) / item_height;
    int scroll = 0;
    
    if (selected >= visible) scroll = selected - visible + 1;
    if (scroll < 0) scroll = 0;
    
    for (int i = scroll; i < game_count && i < scroll + visible; i++) {
        int item_y = start_y + (i - scroll) * item_height;
        int is_selected = (i == selected);
        
        // Highlight selected
        if (is_selected) {
            draw_rect(x + 5, item_y - 2, w - 10, item_height, COLOR_SELECTED_BG);
            draw_rect(x + 5, item_y - 2, 4, item_height, COLOR_ACCENT);
        }
        
        // Game name
        uint32_t color = is_selected ? COLOR_GOLD : COLOR_TEXT_PRIMARY;
        draw_text_scaled(x + 20, item_y, games[i].display_name, color, 1);
    }
}

// ============ GAME DETAILS (RIGHT PANEL) ============
static void draw_game_details(int x, int y, int w, int h) {
    // Background
    draw_rect(x, y, w, h, COLOR_PRIMARY);
    
    if (game_count == 0 || selected < 0 || selected >= game_count) {
        draw_text_scaled(x + 30, y + 50, "No game selected", COLOR_TEXT_MUTED, 2);
        return;
    }
    
    Game *game = &games[selected];
    int cover_size = w - 60;
    if (cover_size > 350) cover_size = 350;
    int cover_x = x + (w - cover_size) / 2;
    int cover_y = y + 20;
    
    // Cover art
    draw_cover_placeholder(cover_x, cover_y, cover_size, cover_size);
    
    // Divider
    int divider_y = cover_y + cover_size + 25;
    draw_rect(x + 20, divider_y, w - 40, 1, COLOR_DIVIDER);
    
    // Game info
    int info_x = x + 30;
    int info_y = divider_y + 20;
    
    // Title (large)
    draw_text_scaled(info_x, info_y, game->display_name, COLOR_GOLD, 2);
    info_y += 35;
    
    // ID
    char id_text[128];
    snprintf(id_text, sizeof(id_text), "ID: %s", game->id);
    draw_text_scaled(info_x, info_y, id_text, COLOR_TEXT_SECONDARY, 1);
    info_y += 25;
    
    // Region (from disc ID)
    char region_text[128] = "Region: Unknown";
    if (game->id[0] != '\0' && strlen(game->id) >= 4) {
        char region[4];
        strncpy(region, game->id + 2, 2);
        region[2] = '\0';
        
        if (strcmp(region, "US") == 0) snprintf(region_text, sizeof(region_text), "Region: USA");
        else if (strcmp(region, "EU") == 0) snprintf(region_text, sizeof(region_text), "Region: Europe");
        else if (strcmp(region, "JP") == 0) snprintf(region_text, sizeof(region_text), "Region: Japan");
        else snprintf(region_text, sizeof(region_text), "Region: %s", region);
    }
    draw_text_scaled(info_x, info_y, region_text, COLOR_TEXT_SECONDARY, 1);
    info_y += 25;
    
    // Disc
    char disc_text[128];
    snprintf(disc_text, sizeof(disc_text), "Disc: 1/1");
    draw_text_scaled(info_x, info_y, disc_text, COLOR_TEXT_SECONDARY, 1);
    info_y += 25;
    
    // Size
    char size_text[128];
    snprintf(size_text, sizeof(size_text), "Size: --");
    draw_text_scaled(info_x, info_y, size_text, COLOR_TEXT_SECONDARY, 1);
    info_y += 25;
    
    // Status
    char status_text[128];
    snprintf(status_text, sizeof(status_text), "Status: ✓ Configured");
    draw_text_scaled(info_x, info_y, status_text, COLOR_SUCCESS, 1);
    info_y += 25;
    
    // Emulator
    draw_text_scaled(info_x, info_y, "Emulator:", COLOR_TEXT_MUTED, 1);
    info_y += 25;
    
    char emu_text[128];
    snprintf(emu_text, sizeof(emu_text), "Emulator ID: %s", EMULATOR_TID);
    draw_text_scaled(info_x, info_y, emu_text, COLOR_TEXT_SECONDARY, 1);
}

// ============ MAIN UI ============
void draw_launcher_ui(int game_count, int selected) {
    // Clear screen
    memset(framebuffer[current_buf], 0, FB_SIZE);
    draw_rect(0, 0, SCREEN_WIDTH, SCREEN_HEIGHT, COLOR_PRIMARY);
    
    // Header
    draw_header(0, 0, SCREEN_WIDTH);
    
    // Calculate panels
    int header_height = 70;
    int footer_height = 50;
    int content_y = header_height;
    int content_height = SCREEN_HEIGHT - header_height - footer_height;
    int left_width = SCREEN_WIDTH * 60 / 100;  // 60% for game list
    int right_width = SCREEN_WIDTH - left_width;
    
    // Game list (left panel)
    draw_game_list(0, content_y, left_width, content_height);
    
    // Game details (right panel)
    draw_game_details(left_width, content_y, right_width, content_height);
    
    // Footer
    draw_footer(0, SCREEN_HEIGHT - footer_height, SCREEN_WIDTH);
}
