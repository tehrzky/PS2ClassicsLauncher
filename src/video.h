#ifndef VIDEO_H
#define VIDEO_H
#define SCREEN_WIDTH 1920
#define SCREEN_HEIGHT 1080
#define FB_SIZE (SCREEN_WIDTH * SCREEN_HEIGHT * 4)


#include <stdint.h>

extern uint32_t *framebuffer[2];
extern int current_buf;

int init_video(void);
void flip(void);
void draw_pixel(int x, int y, uint32_t color);
void draw_rect(int x, int y, int w, int h, uint32_t color);
void draw_rounded_rect(int x, int y, int w, int h, int radius, uint32_t color);
void draw_image_rgba(int x, int y, int w, int h, const unsigned char *rgba, int img_w, int img_h);
void draw_image_rgba_fit(int x, int y, int box_w, int box_h,
                         const unsigned char *rgba, int img_w, int img_h);

#endif
