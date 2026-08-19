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
    for (int yy = y; yy < y + h; yy++)
        for (int xx = x; xx < x + w; xx++)
            draw_pixel(xx, yy, color);
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
