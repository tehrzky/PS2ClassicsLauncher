#include "cover.h"
#include "scraper.h"
#include "settings.h"
#include "debug.h"
#include "video.h"
#include "font.h"
#include <string.h>
#include <unistd.h>

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

static int cover_loaded = 0;
static char current_serial[32] = {0};
static unsigned char *cover_rgba = NULL;
static int cover_w = 0, cover_h = 0;

static unsigned char *wallpaper_rgba = NULL;
static int wallpaper_w = 0, wallpaper_h = 0;

void cover_load(const char *serial) {
    if (cover_loaded && strcmp(current_serial, serial) == 0) return;

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
    int preferred_3d = g_settings.cover_type == 1;

    if (preferred_3d) {
        snprintf(cover_path, sizeof(cover_path), "%s/covers/3d/%s.png", g_settings.work_path, serial);
        if (access(cover_path, F_OK) == 0) {
            cover_rgba = stbi_load(cover_path, &cover_w, &cover_h, NULL, 4);
            if (cover_rgba) { cover_loaded = 1; return; }
        }
        snprintf(cover_path, sizeof(cover_path), "%s/covers/default/%s.jpg", g_settings.work_path, serial);
        if (access(cover_path, F_OK) == 0) {
            cover_rgba = stbi_load(cover_path, &cover_w, &cover_h, NULL, 4);
            if (cover_rgba) { cover_loaded = 1; return; }
        }
    } else {
        snprintf(cover_path, sizeof(cover_path), "%s/covers/default/%s.jpg", g_settings.work_path, serial);
        if (access(cover_path, F_OK) == 0) {
            cover_rgba = stbi_load(cover_path, &cover_w, &cover_h, NULL, 4);
            if (cover_rgba) { cover_loaded = 1; return; }
        }
        snprintf(cover_path, sizeof(cover_path), "%s/covers/3d/%s.png", g_settings.work_path, serial);
        if (access(cover_path, F_OK) == 0) {
            cover_rgba = stbi_load(cover_path, &cover_w, &cover_h, NULL, 4);
            if (cover_rgba) { cover_loaded = 1; return; }
        }
    }

    if (g_settings.auto_download_covers) {
        log_debug("No cover found for %s, downloading...", serial);
        scraper_download_cover(serial);

        if (preferred_3d) {
            snprintf(cover_path, sizeof(cover_path), "%s/covers/3d/%s.png", g_settings.work_path, serial);
            if (access(cover_path, F_OK) == 0) {
                cover_rgba = stbi_load(cover_path, &cover_w, &cover_h, NULL, 4);
                if (cover_rgba) { cover_loaded = 1; return; }
            }
            snprintf(cover_path, sizeof(cover_path), "%s/covers/default/%s.jpg", g_settings.work_path, serial);
            if (access(cover_path, F_OK) == 0) {
                cover_rgba = stbi_load(cover_path, &cover_w, &cover_h, NULL, 4);
                if (cover_rgba) { cover_loaded = 1; return; }
            }
        } else {
            snprintf(cover_path, sizeof(cover_path), "%s/covers/default/%s.jpg", g_settings.work_path, serial);
            if (access(cover_path, F_OK) == 0) {
                cover_rgba = stbi_load(cover_path, &cover_w, &cover_h, NULL, 4);
                if (cover_rgba) { cover_loaded = 1; return; }
            }
            snprintf(cover_path, sizeof(cover_path), "%s/covers/3d/%s.png", g_settings.work_path, serial);
            if (access(cover_path, F_OK) == 0) {
                cover_rgba = stbi_load(cover_path, &cover_w, &cover_h, NULL, 4);
                if (cover_rgba) { cover_loaded = 1; return; }
            }
        }
    }

    cover_loaded = 1;
}

void cover_draw_fit(int x, int y, int box_w, int box_h, const char *serial) {
    if (!cover_loaded || strcmp(current_serial, serial) != 0) {
        cover_load(serial);
    }

    if (cover_rgba && cover_w > 0 && cover_h > 0) {
        draw_image_rgba_fit(x, y, box_w, box_h, cover_rgba, cover_w, cover_h);
        return;
    }

    int ph_h = (int)(box_h * 0.80f);
    int ph_w = ph_h * 3 / 4;
    int ph_x = x + (box_w - ph_w) / 2;
    int ph_y = y + (box_h - ph_h) / 2;

    draw_rounded_rect(ph_x, ph_y, ph_w, ph_h, 14, COLOR_CARD_SEL);
    draw_rounded_rect(ph_x + 3, ph_y + 3, ph_w - 6, ph_h - 6, 11, COLOR_CARD);

    int cx = ph_x + ph_w / 2;
    int cy = ph_y + ph_h / 2 - 12;
    int radius = ph_w / 4;

    for (int yy = -radius; yy <= radius; yy++) {
        for (int xx = -radius; xx <= radius; xx++) {
            if ((xx * xx + yy * yy) <= (radius * radius)) {
                draw_pixel(cx + xx, cy + yy, COLOR_MUTED);
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
    int label_w = font_text_width(label, 24);
    draw_text_scaled(cx - label_w / 2, cy + radius + 26, label, COLOR_MUTED, 2);
}

void cover_load_wallpaper(void) {
    if (wallpaper_rgba) return; // already loaded
    if (!g_settings.wallpaper[0]) return;

    char wp_path[512];
    snprintf(wp_path, sizeof(wp_path), "%s/%s", g_settings.work_path, g_settings.wallpaper);
    if (access(wp_path, F_OK) != 0) return;

    int channels = 0;
    wallpaper_rgba = stbi_load(wp_path, &wallpaper_w, &wallpaper_h, &channels, 4);
    if (!wallpaper_rgba) {
        wallpaper_w = 0; wallpaper_h = 0;
    }
}

void cover_draw_wallpaper(void) {
    cover_load_wallpaper();
    if (wallpaper_rgba && wallpaper_w > 0 && wallpaper_h > 0) {
        draw_image_rgba_fit(0, 0, SCREEN_WIDTH, SCREEN_HEIGHT, wallpaper_rgba, wallpaper_w, wallpaper_h);
    } else {
        memset(framebuffer[current_buf], 0, FB_SIZE);
        draw_rect(0, 0, SCREEN_WIDTH, SCREEN_HEIGHT, COLOR_BG);
    }
}

void cover_free_wallpaper(void) {
    if (wallpaper_rgba) {
        stbi_image_free(wallpaper_rgba);
        wallpaper_rgba = NULL;
        wallpaper_w = 0;
        wallpaper_h = 0;
    }
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
