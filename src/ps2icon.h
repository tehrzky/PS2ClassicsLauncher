#ifndef PS2ICON_H
#define PS2ICON_H

#include <stdint.h>
#include <stddef.h>

/* Decode a PS2 icon0.ico file to RGBA32 (0xAARRGGBB format).
 * Supports:
 *   - Standard 16x16 4bpp paletted icon (160 bytes)
 *   - 128x128 32bpp raw RGBA icon (65536 bytes)
 * Returns 1 on success, 0 on failure.
 * On success, *out_rgba is malloc'd and must be freed by caller. */
int ps2icon_decode(const uint8_t *ico_data, size_t ico_size,
                   uint32_t **out_rgba, int *out_w, int *out_h);

/* Parse icon.sys title (offset 0xC0, up to 68 bytes, SJIS).
 * out_title must be at least 128 bytes. */
void ps2icon_parse_title(const uint8_t *icon_sys, size_t icon_sys_size,
                         char *out_title, size_t out_len);

#endif
