#include "video.h"
#include "debug.h"
#include <orbis/VideoOut.h>
#include <orbis/libkernel.h>   // <-- ADD THIS for sceKernel functions
#include <sys/mman.h>
#include <string.h>

#define SCREEN_WIDTH    1920
#define SCREEN_HEIGHT   1080
#define FB_SIZE         (SCREEN_WIDTH * SCREEN_HEIGHT * 4)

uint32_t *framebuffer[2];
int current_buf = 0;
static int video;

int init_video(void) {
    video = sceVideoOutOpen(ORBIS_VIDEO_USER_MAIN, ORBIS_VIDEO_OUT_BUS_MAIN, 0, 0);
    if (video < 0) {
        log_debug("VIDEO OPEN FAIL: %d", video);
        return -1;
    }
    log_debug("VIDEO HANDLE: %d", video);

    size_t size = (FB_SIZE + 0x1FFFFF) & ~0x1FFFFF;
    log_debug("FB SIZE: %zu", size);

    for (int i = 0; i < 2; i++) {
        off_t directMem = 0;
        int r = sceKernelAllocateDirectMemory(0, 0x180000000, size, 0x200000, 3, &directMem);
        if (r < 0) {
            log_debug("ALLOC FAIL[%d]: %d", i, r);
            return -1;
        }
        void *addr = NULL;
        r = sceKernelMapDirectMemory(&addr, size, 0x33, 0, directMem, 0x200000);
        if (r < 0) {
            log_debug("MAP FAIL[%d]: %d", i, r);
            return -1;
        }
        memset(addr, 0, size);
        framebuffer[i] = (uint32_t*)addr;
        log_debug("BUFFER[%d]: %p", i, addr);
    }

    OrbisVideoOutBufferAttribute attr;
    memset(&attr, 0, sizeof(attr));
    sceVideoOutSetBufferAttribute(&attr, 0x80000000, 1, 0, SCREEN_WIDTH, SCREEN_HEIGHT, SCREEN_WIDTH);

    int r = sceVideoOutRegisterBuffers(video, 0, (void*)framebuffer, 2, &attr);
    if (r < 0) {
        log_debug("REGISTER BUFFERS FAIL: %d", r);
        return -1;
    }
    log_debug("REGISTER BUFFERS OK");

    r = sceVideoOutSetFlipRate(video, 0);
    log_debug("SET FLIP RATE: %d", r);

    return 0;
}

void flip(void) {
    sceVideoOutSubmitFlip(video, current_buf, ORBIS_VIDEO_OUT_FLIP_VSYNC, 0);
    current_buf ^= 1;
}

void draw_pixel(int x, int y, uint32_t color) {
    if (x < 0 || x >= SCREEN_WIDTH || y < 0 || y >= SCREEN_HEIGHT) return;
    framebuffer[current_buf][y * SCREEN_WIDTH + x] = color;
}

void draw_rect(int x, int y, int w, int h, uint32_t color) {
    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (x + w > SCREEN_WIDTH)  w = SCREEN_WIDTH - x;
    if (y + h > SCREEN_HEIGHT) h = SCREEN_HEIGHT - y;
    if (w <= 0 || h <= 0) return;

    uint32_t *dst = &framebuffer[current_buf][y * SCREEN_WIDTH + x];
    for (int yy = 0; yy < h; yy++) {
        for (int xx = 0; xx < w; xx++) dst[xx] = color;
        dst += SCREEN_WIDTH;
    }
}

void draw_rounded_rect(int x, int y, int w, int h, int radius, uint32_t color) {
    if (w <= 0 || h <= 0) return;
    draw_rect(x + radius, y, w - radius * 2, h, color);
    draw_rect(x, y + radius, w, h - radius * 2, color);
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

void draw_image_rgba(int x, int y, int w, int h, const unsigned char *rgba, int img_w, int img_h) {
    if (!rgba || img_w <= 0 || img_h <= 0) return;
    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (x + w > SCREEN_WIDTH)  w = SCREEN_WIDTH - x;
    if (y + h > SCREEN_HEIGHT) h = SCREEN_HEIGHT - y;
    if (w <= 0 || h <= 0) return;

    for (int dy = 0; dy < h; dy++) {
        int sy = dy * img_h / h;
        if (sy >= img_h) sy = img_h - 1;
        uint32_t *dst = &framebuffer[current_buf][(y + dy) * SCREEN_WIDTH + x];
        for (int dx = 0; dx < w; dx++) {
            int sx = dx * img_w / w;
            if (sx >= img_w) sx = img_w - 1;
            const unsigned char *p = rgba + (sy * img_w + sx) * 4;
            uint8_t alpha = p[3];
            if (alpha == 255) {
                dst[dx] = (0xFFu << 24) | ((uint32_t)p[0] << 16) | ((uint32_t)p[1] << 8) | (uint32_t)p[2];
            } else if (alpha > 0) {
                uint32_t dst_col = dst[dx];
                uint8_t dr = (dst_col >> 16) & 0xFF;
                uint8_t dg = (dst_col >> 8) & 0xFF;
                uint8_t db = dst_col & 0xFF;
                uint8_t r = (p[0] * alpha + dr * (255 - alpha)) / 255;
                uint8_t g = (p[1] * alpha + dg * (255 - alpha)) / 255;
                uint8_t b = (p[2] * alpha + db * (255 - alpha)) / 255;
                dst[dx] = (0xFFu << 24) | ((uint32_t)r << 16) | ((uint32_t)g << 8) | b;
            }
        }
    }
}

void draw_image_rgba_fit(int x, int y, int box_w, int box_h,
                          const unsigned char *rgba, int img_w, int img_h) {
    if (!rgba || img_w <= 0 || img_h <= 0) return;

    float img_aspect = (float)img_w / img_h;
    float box_aspect = (float)box_w / box_h;
    int draw_w, draw_h, off_x, off_y;

    if (img_aspect > box_aspect) {
        draw_w = box_w;
        draw_h = (int)(box_w / img_aspect);
        off_x = 0;
        off_y = (box_h - draw_h) / 2;
    } else {
        draw_h = box_h;
        draw_w = (int)(box_h * img_aspect);
        off_x = (box_w - draw_w) / 2;
        off_y = 0;
    }

    draw_image_rgba(x + off_x, y + off_y, draw_w, draw_h, rgba, img_w, img_h);
}
