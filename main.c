// ============ LAUNCHER UI DESIGN ============
// Colors
#define COLOR_PRIMARY      0xFF1A2A3A   // Dark blue background
#define COLOR_SECONDARY    0xFF2A3A4A   // Slightly lighter blue
#define COLOR_ACCENT       0xFF00BFFF   // Deep sky blue for highlights
#define COLOR_ACCENT_DARK  0xFF0080A0   // Darker accent
#define COLOR_GOLD         0xFFFFD700   // Gold for selected
#define COLOR_CARD         0xFF2A3A4A   // Card background
#define COLOR_CARD_BORDER  0xFF3A4A5A   // Card border
#define COLOR_TEXT_PRIMARY 0xFFFFFFFF   // White text
#define COLOR_TEXT_SECONDARY 0xFFAAAAAA // Gray text
#define COLOR_TEXT_MUTED   0xFF666666   // Dark gray
#define COLOR_SUCCESS      0xFF00FF00   // Green
#define COLOR_ERROR        0xFFFF0000   // Red

// ============ UI DRAWING FUNCTIONS ============

// Draw a rounded rectangle
static void draw_rounded_rect(int x, int y, int w, int h, int radius, uint32_t color) {
    // Simple rounded rect - draw the main rectangle with rounded corners
    draw_rect(x + radius, y, w - radius * 2, h, color);
    draw_rect(x, y + radius, w, h - radius * 2, color);
    
    // Corners (simple approximation)
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

// Draw a simple cover placeholder
static void draw_cover_placeholder(int x, int y, int w, int h, const char *text) {
    // Background
    draw_rounded_rect(x, y, w, h, 8, COLOR_CARD_BORDER);
    draw_rounded_rect(x + 2, y + 2, w - 4, h - 4, 6, COLOR_SECONDARY);
    
    // CD icon (simple circle with text)
    int cx = x + w / 2;
    int cy = y + h / 2 - 10;
    int radius = 40;
    
    // Draw CD circle
    for (int yy = -radius; yy <= radius; yy++) {
        for (int xx = -radius; xx <= radius; xx++) {
            if ((xx * xx + yy * yy) <= (radius * radius)) {
                draw_pixel(cx + xx, cy + yy, COLOR_TEXT_MUTED);
            }
        }
    }
    // Inner circle
    for (int yy = -10; yy <= 10; yy++) {
        for (int xx = -10; xx <= 10; xx++) {
            if ((xx * xx + yy * yy) <= (10 * 10)) {
                draw_pixel(cx + xx, cy + yy, COLOR_PRIMARY);
            }
        }
    }
    
    // Game name
    draw_text_scaled(x + 10, y + h - 30, text, COLOR_TEXT_SECONDARY, 1);
}

// Draw a game card
static void draw_game_card(int x, int y, int w, int h, const char *title, const char *subtitle, 
                           const char *disc_id, int is_selected) {
    // Card background
    uint32_t bg_color = is_selected ? COLOR_ACCENT_DARK : COLOR_CARD;
    draw_rounded_rect(x, y, w, h, 10, is_selected ? COLOR_ACCENT : COLOR_CARD_BORDER);
    draw_rounded_rect(x + 2, y + 2, w - 4, h - 4, 8, bg_color);
    
    // Cover placeholder (left side)
    int cover_size = h - 20;
    draw_cover_placeholder(x + 10, y + 10, cover_size, cover_size, "DVD");
    
    // Game info (right side)
    int info_x = x + cover_size + 30;
    int info_y = y + 15;
    
    // Title
    draw_text_scaled(info_x, info_y, title, 
                     is_selected ? COLOR_GOLD : COLOR_TEXT_PRIMARY, 2);
    
    // Subtitle/Disc ID
    info_y += 30;
    char info_text[128];
    if (disc_id && disc_id[0] != '\0') {
        snprintf(info_text, sizeof(info_text), "ID: %s", disc_id);
        draw_text_scaled(info_x, info_y, info_text, COLOR_TEXT_SECONDARY, 1);
    }
    
    info_y += 22;
    if (subtitle && subtitle[0] != '\0') {
        draw_text_scaled(info_x, info_y, subtitle, COLOR_TEXT_MUTED, 1);
    }
    
    // Selection indicator
    if (is_selected) {
        draw_text_scaled(x + w - 80, y + h - 30, "▶ SELECTED", COLOR_GOLD, 1);
    }
}

// Draw the main header
static void draw_header(int x, int y, int w) {
    // Header background
    draw_rect(x, y, w, 80, COLOR_PRIMARY);
    draw_rect(x, y + 78, w, 2, COLOR_ACCENT);
    
    // Title
    draw_text_scaled(x + 40, y + 20, "PS2 ISO LAUNCHER", COLOR_GOLD, 3);
    draw_text_scaled(x + 40, y + 55, "Select a game and press X to launch", COLOR_TEXT_SECONDARY, 1);
    
    // Version/status
    char status[64];
    snprintf(status, sizeof(status), "%d games loaded", game_count);
    draw_text_scaled(x + w - 200, y + 25, status, COLOR_TEXT_MUTED, 1);
}

// Draw footer
static void draw_footer(int x, int y, int w) {
    draw_rect(x, y, w, 2, COLOR_ACCENT);
    
    int y_pos = y + 15;
    draw_text_scaled(x + 40, y_pos, "[X] LAUNCH", COLOR_GOLD, 1);
    draw_text_scaled(x + 160, y_pos, "[UP/DOWN] SELECT", COLOR_TEXT_SECONDARY, 1);
    draw_text_scaled(x + 340, y_pos, "[O] BACK", COLOR_TEXT_SECONDARY, 1);
    
    // Controller hint
    const char *hint = "Press X to launch selected game";
    draw_text_scaled(x + w - 300, y_pos, hint, COLOR_TEXT_MUTED, 1);
}

// ============ REDESIGNED DRAWING LOOP ============
// Replace your main drawing loop with this:

static void draw_launcher_ui(void) {
    // Clear with primary color
    memset(framebuffer[current_buf], 0, FB_SIZE);
    draw_rect(0, 0, SCREEN_WIDTH, SCREEN_HEIGHT, COLOR_PRIMARY);
    
    // Background pattern (subtle grid)
    for (int y = 0; y < SCREEN_HEIGHT; y += 80) {
        for (int x = 0; x < SCREEN_WIDTH; x += 80) {
            draw_pixel(x, y, COLOR_SECONDARY);
        }
    }
    
    // Header
    draw_header(0, 0, SCREEN_WIDTH);
    
    // Game list area
    int list_x = 40;
    int list_y = 100;
    int card_width = SCREEN_WIDTH - 80;
    int card_height = 100;
    int card_spacing = 15;
    
    // Calculate visible games
    int visible = (SCREEN_HEIGHT - list_y - 80) / (card_height + card_spacing);
    int scroll = 0;
    if (selected >= visible) scroll = selected - visible + 1;
    
    // Draw game cards
    for (int i = scroll; i < game_count && i < scroll + visible; i++) {
        int y_pos = list_y + (i - scroll) * (card_height + card_spacing);
        
        char subtitle[128] = {0};
        if (games[i].id && games[i].id[0] != '\0') {
            // Try to get region from disc ID
            if (strlen(games[i].id) >= 4) {
                char region[4];
                strncpy(region, games[i].id + 2, 2);
                region[2] = '\0';
                
                char region_name[16];
                if (strcmp(region, "US") == 0) snprintf(region_name, sizeof(region_name), "USA");
                else if (strcmp(region, "EU") == 0) snprintf(region_name, sizeof(region_name), "Europe");
                else if (strcmp(region, "JP") == 0) snprintf(region_name, sizeof(region_name), "Japan");
                else snprintf(region_name, sizeof(region_name), "Unknown");
                
                snprintf(subtitle, sizeof(subtitle), "Region: %s", region_name);
            }
        }
        
        draw_game_card(list_x, y_pos, card_width, card_height, 
                       games[i].display_name, subtitle, 
                       games[i].id, (i == selected));
    }
    
    // Footer
    draw_footer(0, SCREEN_HEIGHT - 60, SCREEN_WIDTH);
    
    // "No games" message
    if (game_count == 0) {
        draw_text_scaled(SCREEN_WIDTH / 2 - 100, SCREEN_HEIGHT / 2, "NO ISO FILES FOUND", COLOR_ERROR, 3);
        draw_text_scaled(SCREEN_WIDTH / 2 - 120, SCREEN_HEIGHT / 2 + 50, "Place ISOs in /data/PS4ROMS/PS2ISO/", COLOR_TEXT_SECONDARY, 2);
    }
}

// ============ MAIN LOOP WITH NEW UI ============
// Replace your main loop's rendering section with:

    while (1) {
        if (pad >= 0) {
            scePadReadState(pad, &pad_data);
            unsigned int buttons = pad_data.buttons;
            unsigned int pressed = buttons & ~old_buttons;
            old_buttons = buttons;

            if (pressed & ORBIS_PAD_BUTTON_UP) {
                selected = (selected - 1 + game_count) % game_count;
            }
            if (pressed & ORBIS_PAD_BUTTON_DOWN) {
                selected = (selected + 1) % game_count;
            }
            if (pressed & ORBIS_PAD_BUTTON_CROSS) {
                log_debug("LAUNCH: %s", games[selected].display_name);
                char emu_tid[32] = {0};
                if (set_active_game(games[selected].path, games[selected].id,
                                    games[selected].name, emu_tid, sizeof(emu_tid))) {
                    // Show launch confirmation
                    memset(framebuffer[current_buf], 0, FB_SIZE);
                    draw_rect(0, 0, SCREEN_WIDTH, SCREEN_HEIGHT, COLOR_PRIMARY);
                    draw_text_scaled(200, 400, "LAUNCHING...", COLOR_GOLD, 4);
                    
                    char game_name_display[512];
                    snprintf(game_name_display, sizeof(game_name_display), "Game: %s", games[selected].display_name);
                    draw_text_scaled(200, 460, game_name_display, COLOR_TEXT_PRIMARY, 2);
                    draw_text_scaled(200, 500, "Config written successfully!", COLOR_SUCCESS, 2);
                    draw_text_scaled(200, 540, "Opening emulator...", COLOR_TEXT_SECONDARY, 2);
                    flip();
                    sceKernelSleep(2);
                    
                    // Launch the emulator
                    launch_emulator(emu_tid);
                } else {
                    log_debug("set_active_game FAILED for %s", games[selected].name);
                    // Show error
                    memset(framebuffer[current_buf], 0, FB_SIZE);
                    draw_rect(0, 0, SCREEN_WIDTH, SCREEN_HEIGHT, COLOR_PRIMARY);
                    draw_text_scaled(200, 400, "ERROR!", COLOR_ERROR, 4);
                    draw_text_scaled(200, 460, "Failed to write config", COLOR_TEXT_PRIMARY, 2);
                    draw_text_scaled(200, 500, "Check launcher_log.txt", COLOR_TEXT_SECONDARY, 2);
                    flip();
                    sceKernelSleep(3);
                }
            }
            if (pressed & ORBIS_PAD_BUTTON_CIRCLE) {
                // Exit or back
                log_debug("EXIT requested");
                break;
            }
        }

        // Draw the new UI
        draw_launcher_ui();
        
        flip();
        sceKernelUsleep(16666);
    }
