#ifndef FONT_H
#define FONT_H

#include <stdint.h>

#define FONT_SLOT_BODY   0
#define FONT_SLOT_TITLE  1
#define FONT_SLOT_BOLD   2
#define FONT_SLOT_ITALIC 3
#define FONT_SLOT_COUNT  4

#define MAX_FONTS 16

void font_init(void);
void font_cleanup(void);
void font_scan_directory(const char *dir);

const char *font_get_list_name(int index);
int font_get_list_count(void);
void font_load_slot(int slot, int index);
void font_cycle_slot(int slot, int delta);

void draw_text(int x, int y, const char *text, uint32_t color, int size_px);
void draw_text_slot(int x, int y, const char *text, uint32_t color, int size_px, int slot);
void draw_text_scaled(int x, int y, const char *text, uint32_t color, int scale);
int font_text_width(const char *text, int size_px);
int font_text_width_slot(const char *text, int size_px, int slot);
int font_line_height(int size_px);

#endif
