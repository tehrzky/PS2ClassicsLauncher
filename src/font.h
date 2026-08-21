#ifndef FONT_H
#define FONT_H

#include <stdint.h>

void font_init(void);
void font_cleanup(void);

void draw_text(int x, int y, const char *s, uint32_t color, int size_px);
void draw_text_scaled(int x, int y, const char *s, uint32_t color, int scale);

int font_text_width(const char *s, int size_px);
int font_line_height(int size_px);

#endif
