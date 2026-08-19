#include "cover.h"
#include "scraper.h"
#include "debug.h"
#include "video.h"
#include <stdlib.h>
#include <string.h>
#include <png.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>

// ============ STATIC DATA ============
static int cover_loaded = 0;
static uint32_t *cover_texture = NULL;
static int cover_width = 0;
static int cover_height = 0;
static char current_serial[32] = {0};

// ============ PNG LOADER ============
static uint32_t* load_png(const char *path, int *width, int *height) {
    FILE *fp = fopen(path, "rb");
    if (!fp) return NULL;

    png_structp png = png_create_read_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);
    if (!png) {
        fclose(fp);
        return NULL;
    }

    png_infop info = png_create_info_struct(png);
    if (!info) {
        png_destroy_read_struct(&png, NULL, NULL);
        fclose(fp);
        return NULL;
    }

    if (setjmp(png_jmpbuf(png))) {
        png_destroy_read_struct(&png, &info, NULL);
        fclose(fp);
        return NULL;
    }

    png_init_io(png, fp);
    png_read_info(png, info);

    *width = png_get_image_width(png, info);
    *height = png_get_image_height(png, info);
    png_byte color_type = png_get_color_type(png, info);
    png_byte bit_depth = png_get_bit_depth(png, info);

    // Convert to RGBA
    if (bit_depth == 16) png_set_strip_16(png);
    if (color_type == PNG_COLOR_TYPE_PALETTE) png_set_palette_to_rgb(png);
    if (color_type == PNG_COLOR_TYPE_GRAY && bit_depth < 8) png_set_expand_gray_1_2_4_to_8(png);
    if (png_get_valid(png, info, PNG_INFO_tRNS)) png_set_tRNS_to_alpha(png);
    if (color_type == PNG_COLOR_TYPE_RGB || color_type == PNG_COLOR_TYPE_GRAY)
        png_set_add_alpha(png, 0xFF, PNG_FILLER_AFTER);

    png_read_update_info(png, info);

    uint32_t *pixels = (uint32_t*)malloc((*width) * (*height) * sizeof(uint32_t));
    if (!pixels) {
        png_destroy_read_struct(&png, &info, NULL);
        fclose(fp);
        return NULL;
    }

    png_bytep *row_ptrs = (png_bytep*)malloc((*height) * sizeof(png_bytep));
    if (!row_ptrs) {
        free(pixels);
        png_destroy_read_struct(&png, &info, NULL);
        fclose(fp);
        return NULL;
    }

    for (int y = 0; y < *height; y++) {
        row_ptrs[y] = (png_bytep)&pixels[y * (*width)];
    }
    png_read_image(png, row_ptrs);

    free(row_ptrs);
    png_destroy_read_struct(&png, &info, NULL);
    fclose(fp);

    return pixels;
}

// ============ COVER LOADING ============
void cover_load(const char *serial) {
    if (cover_loaded && strcmp(current_serial, serial) == 0) {
        return; // Already loaded
    }

    // Free old texture
    if (cover_texture) {
        free(cover_texture);
        cover_texture = NULL;
    }
    cover_loaded = 0;
    cover_width = 0;
    cover_height = 0;

    if (!serial || strlen(serial) == 0) {
        strncpy(current_serial, "UNKNOWN", sizeof(current_serial) - 1);
        current_serial[sizeof(current_serial) - 1] = '\0';
        return;
    }

    strncpy(current_serial, serial, sizeof(current_serial) - 1);
    current_serial[sizeof(current_serial) - 1] = '\0';

    char cover_path[512];

    // Try 3D cover first (PNG)
    snprintf(cover_path, sizeof(cover_path), "/data/PS4ROMS/PS2ISO/covers/3d/%s.png", serial);
    if (access(cover_path, F_OK) == 0) {
        cover_texture = load_png(cover_path, &cover_width, &cover_height);
        if (cover_texture) {
            cover_loaded = 1;
            log_debug("Loaded 3D cover: %s", cover_path);
            return;
        }
    }

    // Try default cover (JPG - placeholder, you'd need libjpeg)
    snprintf(cover_path, sizeof(cover_path), "/data/PS4ROMS/PS2ISO/covers/default/%s.jpg", serial);
    if (access(cover_path, F_OK) == 0) {
        // For JPG, you'd need a JPEG loader. For now, we'll use placeholder.
        cover_loaded = 1;
        log_debug("Default cover found (JPG loader not implemented): %s", cover_path);
        return;
    }

    // If no cover found, download it
    log_debug("No cover found for %s, downloading...", serial);
    scraper_download_cover(serial);

    // Try loading again after download
    snprintf(cover_path, sizeof(cover_path), "/data/PS4ROMS/PS2ISO/covers/3d/%s.png", serial);
    if (access(cover_path, F_OK) == 0) {
        cover_texture = load_png(cover_path, &cover_width, &cover_height);
        if (cover_texture) {
            cover_loaded = 1;
            log_debug("Loaded 3D cover after download: %s", cover_path);
            return;
        }
    }

    cover_loaded = 1; // Mark as loaded even if we only have placeholder
}

// ============ DRAW COVER ============
void cover_draw(int x, int y, int w, int h, const char *serial) {
    if (!cover_loaded || strcmp(current_serial, serial) != 0) {
        cover_load(serial);
    }

    if (cover_texture && cover_width > 0 && cover_height > 0) {
        // Simple nearest-neighbor scaling
        for (int dy = 0; dy < h; dy++) {
            int src_y = (dy * cover_height) / h;
            for (int dx = 0; dx < w; dx++) {
                int src_x = (dx * cover_width) / w;
                uint32_t color = cover_texture[src_y * cover_width + src_x];
                draw_pixel(x + dx, y + dy, color);
            }
        }
    } else {
        // Fallback placeholder
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
}

void cover_cleanup(void) {
    if (cover_texture) {
        free(cover_texture);
        cover_texture = NULL;
    }
    cover_loaded = 0;
    cover_width = 0;
    cover_height = 0;
    current_serial[0] = '\0';
}
