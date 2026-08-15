// Stub for musl libc startup - called by crt1.o
int __libc_start_main(void *main_fn, int argc, char **argv, ...) {
    return ((int (*)(void))main_fn)();
}

#include <orbis/libkernel.h>
#include <orbis/VideoOut.h>
#include <string.h>

#define SCREEN_WIDTH  1920
#define SCREEN_HEIGHT 1080

int main(void) {
    int video = sceVideoOutOpen(ORBIS_VIDEO_USER_MAIN, ORBIS_VIDEO_OUT_BUS_MAIN, 0, 0);
    if (video < 0) return -1;
    
    size_t size = SCREEN_WIDTH * SCREEN_HEIGHT * 4;
    size_t aligned = (size + 0x1FFFFF) & ~0x1FFFFF;
    off_t directMem = 0;
    sceKernelAllocateDirectMemory(0, 0x180000000, aligned, 0x200000, 3, &directMem);
    void *addr = NULL;
    sceKernelMapDirectMemory(&addr, aligned, 3, 0, directMem, 0x200000);
    memset(addr, 0xFF, aligned);
    
    OrbisVideoOutBufferAttribute attr;
    sceVideoOutSetBufferAttribute(&attr, ORBIS_VIDEO_OUT_PIXEL_FORMAT_A8B8G8R8_SRGB,
                                  1, 0, SCREEN_WIDTH, SCREEN_HEIGHT, 0);
    sceVideoOutRegisterBuffers(video, 0, addr, 1, &attr);
    sceVideoOutSubmitFlip(video, 0, ORBIS_VIDEO_OUT_FLIP_VSYNC, 0);
    
    sceKernelSleep(5);
    return 0;
}
