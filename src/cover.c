#include "cover.h"
#include "scraper.h"
#include "debug.h"
#include "video.h"
#include "font.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

static int cover_loaded = 0;
static char current_serial[32] = {0};
static unsigned char *cover_rgba = NULL;
static int cover_w = 0, cover_h = 0;

void cover_load(const char *serial) {
    if (cover_loaded && strcmp(current_serial, serial) == 0) {
        return;
    }

    if (cover_rgba) {
        stbi_image_free(cover_rgba);
        cover_rgba = NULL;
    }
    cover_w = 0;
    cover_h = 0;
    cover_loaded = 0;

    if (!serial || strlen(serial) == 0) {
        strncpy(current_serial, "UNKNOWN", sizeof(current_serial) - 1);
        current_serial[sizeof(current_serial) - 1] = '\0';
        cover_loaded = 1;
        return;
    }

    strncpy(current_serial, serial, sizeof(current_serial) - 1);
    current_serial[sizeof(current_serial) - 1] = '\0';

    char cover_path[512];

    snprintf(cover_path, sizeof(cover_path), "/data/PS4ROMS/PS2ISO/covers/3d/%s.png", serial);
    if (access(cover_path, F_OK) == 0) {
        cover_rgba = stbi_load(cover_path, &cover_w, &cover_h, NULL, 4);
        if (cover_rgba) {
            cover_loaded = 1;
            log_debug("3D cover loaded: %s (%dx%d)", cover_path, cover_w, cover_h);
            return;
        }
    }

    snprintf(cover_path, sizeof(cover_path), "/data/PS4ROMS/PS2ISO/covers/default/%s.jpg", serial);
    if (access(cover_path, F_OK) == 0) {
        cover_rgba = stbi_load(cover_path, &cover_w, &cover_h, NULL, 4);
        if (cover_rgba) {
            cover_loaded = 1;
            log_debug("Default cover loaded: %s (%dx%d)", cover_path, cover_w, cover_h);
            return;
        }
    }

    log_debug("No cover found for %s, downloading...", serial);
    scraper_download_cover(serial);

    snprintf(cover_path, sizeof(cover_path), "/data/PS4ROMS/PS2ISO/covers/3d/%s.png", serial);
    if (access(cover_path, F_OK) == 0) {
        cover_rgba = stbi_load(cover_path, &cover_w, &cover_h, NULL, 4);
        if (cover_rgba) {
            cover_loaded = 1;
            log_debug("3D cover downloaded+loaded: %s", cover_path);
            return;
        }
    }

    snprintf(cover_path, sizeof(cover_path), "/data/PS4ROMS/PS2ISO/covers/default/%s.jpg", serial);
    if (access(cover_path, F_OK) == 0) {
        cover_rgba = stbi_load(cover_path, &cover_w, &cover_h, NULL, 4);
        if (cover_rgba) {
            cover_loaded = 1;
            log_debug("Default cover downloaded+loaded: %s", cover_path);
            return;
        }
    }

    cover_loaded = 1;
}

void cover_draw(int x, int y, int w, int h, const char *serial) {
    if (!cover_loaded || strcmp(current_serial, serial) != 0) {
        cover_load(serial);
    }

    if (cover_rgba && cover_w > 0 && cover_h > 0) {
        draw_image_rgba(x, y, w, h, cover_rgba, cover_w, cover_h);
        return;
    }

    draw_rounded_rect(x, y, w, h, 14, 0xFF2E4256);
    draw_rounded_rect(x + 3, y + 3, w - 6, h - 6, 11, 0xFF141F2B);

    int cx = x + w / 2;
    int cy = y + h / 2 - 12;
    int radius = w / 4;

    for (int yy = -radius; yy <= radius; yy++) {
        for (int xx = -radius; xx <= radius; xx++) {
            if ((xx * xx + yy * yy) <= (radius * radius)) {
                draw_pixel(cx + xx, cy + yy, 0xFF6C7C8E);
            }
        }
    }
    int hole = radius / 4;
    for (int yy = -hole; yy <= hole; yy++) {
        for (int xx = -hole; xx <= hole; xx++) {
            if ((xx * xx + yy * yy) <= (hole * hole)) {
                draw_pixel(cx + xx, cy + yy, 0xFF141F2B);
            }
        }
    }

    const char *label = "NO COVER";
    int label_w = (int)strlen(label) * FONT_WIDTH * 2;
    draw_text_scaled(cx - label_w / 2, cy + radius + 26, label, 0xFF6C7C8E, 2);
}

void cover_cleanup(void) {
    if (cover_rgba) {
        stbi_image_free(cover_rgba);
        cover_rgba = NULL;
    }
    cover_loaded = 0;
    cover_w = 0;
    cover_h = 0;
    current_serial[0] = '\0';
}
