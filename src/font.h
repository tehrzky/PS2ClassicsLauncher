#ifndef FONT_H
#define FONT_H

#include <stdint.h>

#define FONT_WIDTH  8
#define FONT_HEIGHT 8

void draw_char_scaled(int x, int y, char c, uint32_t color, int scale);
void draw_text_scaled(int x, int y, const char *s, uint32_t color, int scale);

#endif