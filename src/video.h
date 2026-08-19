#ifndef VIDEO_H
#define VIDEO_H

#include <stdint.h>

extern uint32_t *framebuffer[2];
extern int current_buf;

int init_video(void);
void flip(void);
void draw_pixel(int x, int y, uint32_t color);
void draw_rect(int x, int y, int w, int h, uint32_t color);
void draw_rounded_rect(int x, int y, int w, int h, int radius, uint32_t color);

#endif
