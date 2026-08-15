#include <orbis/libkernel.h>
#include <orbis/SystemService.h>
#include <orbis/Pad.h>
#include <orbis/VideoOut.h>
#include <dirent.h>
#include <sys/fcntl.h>
#include <sys/mman.h>
#include <unistd.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>

// ============ CONFIG ============
#define MASTER_CONFIG   "/data/PS4ROMS/PS2ISO/config-emu-ps4.txt"
#define ISO_DIR         "/data/PS4ROMS/PS2ISO/"
#define DEFAULT_CONFIG  "/data/PS4ROMS/PS2ISO/default.txt"
#define TEMP_CONFIG     "/data/PS4ROMS/PS2ISO/.launcher_temp.txt"
#define EMULATOR_TID    "CUSA03500"   // <-- CHANGE TO YOUR EMULATOR PKG TITLE ID

#define SCREEN_WIDTH    1920
#define SCREEN_HEIGHT   1080
#define FB_SIZE         (SCREEN_WIDTH * SCREEN_HEIGHT * 4)

// ============ EMBEDDED DEFAULT CONFIG ============
// Used if /data/PS4ROMS/PS2ISO/default.txt does not exist.
// User can place their own default.txt to override this.
static const char *embedded_default =
"--max-disc-num=1\n"
"--ps2-lang=system\n"
"--host-osd=0\n"
"--host-audio=1\n"
"--host-display-mode=normal\n"
"--gs-uprender=2x2\n"
"--gs-upscale=EdgeSmooth\n"
"--path-patches=\"/data/PS4ROMS/PS2ISO/patches/\"\n"
"--path-featuredata=\"/data/PS4ROMS/PS2ISO/feature_data/\"\n"
"--load-feature-lua=0\n"
"--trophy-support=0\n";

// ============ FONT ============
static const unsigned char font8x8[96][8] = {
    {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00},{0x00,0x00,0x00,0x5F,0x00,0x00,0x00,0x00},
    {0x00,0x00,0x07,0x00,0x07,0x00,0x00,0x00},{0x00,0x14,0x7F,0x14,0x7F,0x14,0x00,0x00},
    {0x00,0x24,0x2A,0x7F,0x2A,0x12,0x00,0x00},{0x00,0x23,0x13,0x08,0x64,0x62,0x00,0x00},
    {0x00,0x36,0x49,0x55,0x22,0x50,0x00,0x00},{0x00,0x00,0x00,0x07,0x00,0x00,0x00,0x00},
    {0x00,0x00,0x1C,0x22,0x41,0x00,0x00,0x00},{0x00,0x00,0x41,0x22,0x1C,0x00,0x00,0x00},
    {0x00,0x08,0x2A,0x1C,0x2A,0x08,0x00,0x00},{0x00,0x08,0x08,0x3E,0x08,0x08,0x00,0x00},
    {0x00,0x00,0x50,0x30,0x00,0x00,0x00,0x00},{0x00,0x08,0x08,0x08,0x08,0x08,0x00,0x00},
    {0x00,0x00,0x60,0x60,0x00,0x00,0x00,0x00},{0x00,0x20,0x10,0x08,0x04,0x02,0x00,0x00},
    {0x00,0x3E,0x51,0x49,0x45,0x3E,0x00,0x00},{0x00,0x00,0x42,0x7F,0x40,0x00,0x00,0x00},
    {0x00,0x42,0x61,0x51,0x49,0x46,0x00,0x00},{0x00,0x21,0x41,0x45,0x4B,0x31,0x00,0x00},
    {0x00,0x18,0x14,0x12,0x7F,0x10,0x00,0x00},{0x00,0x27,0x45,0x45,0x45,0x39,0x00,0x00},
    {0x00,0x3C,0x4A,0x49,0x49,0x30,0x00,0x00},{0x00,0x01,0x71,0x09,0x05,0x03,0x00,0x00},
    {0x00,0x36,0x49,0x49,0x49,0x36,0x00,0x00},{0x00,0x06,0x49,0x49,0x29,0x1E,0x00,0x00},
    {0x00,0x00,0x36,0x36,0x00,0x00,0x00,0x00},{0x00,0x00,0x56,0x36,0x00,0x00,0x00,0x00},
    {0x00,0x08,0x14,0x22,0x41,0x00,0x00,0x00},{0x00,0x14,0x14,0x14,0x14,0x14,0x00,0x00},
    {0x00,0x00,0x41,0x22,0x14,0x08,0x00,0x00},{0x00,0x02,0x01,0x51,0x09,0x06,0x00,0x00},
    {0x00,0x32,0x49,0x79,0x41,0x3E,0x00,0x00},{0x00,0x7E,0x11,0x11,0x11,0x7E,0x00,0x00},
    {0x00,0x7F,0x49,0x49,0x49,0x36,0x00,0x00},{0x00,0x3E,0x41,0x41,0x41,0x22,0x00,0x00},
    {0x00,0x7F,0x41,0x41,0x22,0x1C,0x00,0x00},{0x00,0x7F,0x49,0x49,0x49,0x41,0x00,0x00},
    {0x00,0x7F,0x09,0x09,0x09,0x01,0x00,0x00},{0x00,0x3E,0x41,0x49,0x49,0x7A,0x00,0x00},
    {0x00,0x7F,0x08,0x08,0x08,0x7F,0x00,0x00},{0x00,0x00,0x41,0x7F,0x41,0x00,0x00,0x00},
    {0x00,0x20,0x40,0x41,0x3F,0x01,0x00,0x00},{0x00,0x7F,0x08,0x14,0x22,0x41,0x00,0x00},
    {0x00,0x7F,0x40,0x40,0x40,0x40,0x00,0x00},{0x00,0x7F,0x02,0x0C,0x02,0x7F,0x00,0x00},
    {0x00,0x7F,0x04,0x08,0x10,0x7F,0x00,0x00},{0x00,0x3E,0x41,0x41,0x41,0x3E,0x00,0x00},
    {0x00,0x7F,0x09,0x09,0x09,0x06,0x00,0x00},{0x00,0x3E,0x41,0x51,0x21,0x5E,0x00,0x00},
    {0x00,0x7F,0x09,0x19,0x29,0x46,0x00,0x00},{0x00,0x46,0x49,0x49,0x49,0x31,0x00,0x00},
    {0x00,0x01,0x01,0x7F,0x01,0x01,0x00,0x00},{0x00,0x3F,0x40,0x40,0x40,0x3F,0x00,0x00},
    {0x00,0x1F,0x20,0x40,0x20,0x1F,0x00,0x00},{0x00,0x3F,0x40,0x38,0x40,0x3F,0x00,0x00},
    {0x00,0x63,0x14,0x08,0x14,0x63,0x00,0x00},{0x00,0x07,0x08,0x70,0x08,0x07,0x00,0x00},
    {0x00,0x61,0x51,0x49,0x45,0x43,0x00,0x00},{0x00,0x00,0x7F,0x41,0x41,0x00,0x00,0x00},
    {0x00,0x02,0x04,0x08,0x10,0x20,0x00,0x00},{0x00,0x00,0x41,0x41,0x7F,0x00,0x00,0x00},
    {0x00,0x04,0x02,0x01,0x02,0x04,0x00,0x00},{0x00,0x40,0x40,0x40,0x40,0x40,0x00,0x00},
    {0x00,0x00,0x01,0x02,0x04,0x00,0x00,0x00},{0x00,0x20,0x54,0x54,0x54,0x78,0x00,0x00},
    {0x00,0x7F,0x48,0x44,0x44,0x38,0x00,0x00},{0x00,0x38,0x44,0x44,0x44,0x20,0x00,0x00},
    {0x00,0x38,0x44,0x44,0x48,0x7F,0x00,0x00},{0x00,0x38,0x54,0x54,0x54,0x18,0x00,0x00},
    {0x00,0x08,0x7E,0x09,0x01,0x02,0x00,0x00},{0x00,0x0C,0x52,0x52,0x52,0x3E,0x00,0x00},
    {0x00,0x7F,0x08,0x04,0x04,0x78,0x00,0x00},{0x00,0x00,0x44,0x7D,0x40,0x00,0x00,0x00},
    {0x00,0x20,0x40,0x44,0x3D,0x00,0x00,0x00},{0x00,0x7F,0x10,0x28,0x44,0x00,0x00,0x00},
    {0x00,0x00,0x41,0x7F,0x40,0x00,0x00,0x00},{0x00,0x7C,0x04,0x18,0x04,0x78,0x00,0x00},
    {0x00,0x7C,0x08,0x04,0x04,0x78,0x00,0x00},{0x00,0x38,0x44,0x44,0x44,0x38,0x00,0x00},
    {0x00,0x7C,0x14,0x14,0x14,0x08,0x00,0x00},{0x00,0x08,0x14,0x14,0x18,0x7C,0x00,0x00},
    {0x00,0x7C,0x08,0x04,0x04,0x08,0x00,0x00},{0x00,0x48,0x54,0x54,0x54,0x20,0x00,0x00},
    {0x00,0x04,0x3F,0x44,0x40,0x20,0x00,0x00},{0x00,0x3C,0x40,0x40,0x20,0x7C,0x00,0x00},
    {0x00,0x1C,0x20,0x40,0x20,0x1C,0x00,0x00},{0x00,0x3C,0x40,0x30,0x40,0x3C,0x00,0x00},
    {0x00,0x44,0x28,0x10,0x28,0x44,0x00,0x00},{0x00,0x0C,0x50,0x50,0x50,0x3C,0x00,0x00},
    {0x00,0x44,0x64,0x54,0x4C,0x44,0x00,0x00},{0x00,0x00,0x08,0x36,0x41,0x00,0x00,0x00},
    {0x00,0x00,0x00,0x7F,0x00,0x00,0x00,0x00},{0x00,0x00,0x41,0x36,0x08,0x00,0x00,0x00},
    {0x00,0x08,0x08,0x2A,0x1C,0x08,0x00,0x00}
};

// ============ DATA ============
typedef struct {
    char path[512];
    char name[256];
    char id[32];
} Game;

static Game games[256];
static int game_count = 0;
static int selected = 0;
static uint32_t *framebuffer[2];
static int video;
static int current_buf = 0;

// ============ DRAWING ============
static void draw_pixel(int x, int y, uint32_t color) {
    if (x < 0 || x >= SCREEN_WIDTH || y < 0 || y >= SCREEN_HEIGHT) return;
    framebuffer[current_buf][y * SCREEN_WIDTH + x] = color;
}

static void draw_rect(int x, int y, int w, int h, uint32_t color) {
    for (int yy = y; yy < y + h; yy++)
        for (int xx = x; xx < x + w; xx++)
            draw_pixel(xx, yy, color);
}

static void draw_char(int x, int y, char c, uint32_t color) {
    if (c < 32 || c > 127) return;
    const unsigned char *f = font8x8[c - 32];
    for (int row = 0; row < 8; row++) {
        for (int col = 0; col < 8; col++) {
            if (f[row] & (1 << (7 - col))) {
                draw_pixel(x + col, y + row, color);
            }
        }
    }
}

static void draw_text(int x, int y, const char *s, uint32_t color) {
    while (*s) {
        draw_char(x, y, *s++, color);
        x += 8;
    }
}

// ============ ISO DISC ID EXTRACTION ============
static int extract_disc_id(const char *path, char *out_id, size_t out_len) {
    int fd = open(path, O_RDONLY);
    if (fd < 0) return 0;

    char buf[131072]; // 128KB
    int n = read(fd, buf, sizeof(buf));
    close(fd);
    if (n <= 0) return 0;

    // Strategy 1: Find "BOOT2" or "BOOT" then look for "cdrom0:\" pattern
    for (int i = 0; i < n - 30; i++) {
        if (strncmp(buf + i, "BOOT2", 5) == 0 || strncmp(buf + i, "BOOT", 4) == 0) {
            // Search forward for cdrom0: within next 256 bytes
            int limit = i + 256;
            if (limit > n) limit = n;
            for (int j = i; j < limit - 8; j++) {
                if (strncmp(buf + j, "cdrom0:", 7) == 0) {
                    char *p = buf + j + 7;
                    if (*p == '\\' || *p == '/') p++;
                    // p now points to "SCUS_971.01;1" or similar
                    char raw[32] = {0};
                    int k = 0;
                    while (k < 31 && p[k] && p[k] != ';' && p[k] != '\n' && p[k] != '\r' && p[k] != ' ') {
                        raw[k] = p[k];
                        k++;
                    }
                    // Convert SCUS_971.01 -> SCUS-97101
                    int fi = 0;
                    for (int r = 0; raw[r] && fi < (int)out_len - 1; r++) {
                        if (raw[r] == '_') out_id[fi++] = '-';
                        else if (raw[r] == '.') { /* skip dot */ }
                        else out_id[fi++] = raw[r];
                    }
                    out_id[fi] = '\0';
                    return 1;
                }
            }
        }
    }

    // Strategy 2: Direct pattern search for S???_###.## in first 128KB
    for (int i = 0; i < n - 15; i++) {
        char c0 = buf[i];
        char c1 = buf[i+1];
        char c2 = buf[i+2];
        char c3 = buf[i+3];
        if ((c0 == 'S' || c0 == 's') &&
            (c1 == 'C' || c1 == 'c' || c1 == 'L' || c1 == 'l') &&
            (c2 == 'U' || c2 == 'u' || c2 == 'E' || c2 == 'e' || c2 == 'L' || c2 == 'l') &&
            (c3 == 'S' || c3 == 's' || c3 == 'M' || c3 == 'm' || c3 == 'A' || c3 == 'a') &&
            buf[i+4] == '_' &&
            isdigit((unsigned char)buf[i+5])) {
            char raw[32] = {0};
            int k = 0;
            while (k < 31 && (isalnum((unsigned char)buf[i+k]) || buf[i+k] == '_' || buf[i+k] == '.')) {
                raw[k] = buf[i+k];
                k++;
            }
            int fi = 0;
            for (int r = 0; raw[r] && fi < (int)out_len - 1; r++) {
                if (raw[r] == '_') out_id[fi++] = '-';
                else if (raw[r] == '.') { }
                else out_id[fi++] = raw[r];
            }
            out_id[fi] = '\0';
            return 1;
        }
    }

    return 0;
}

// ============ GAME SCANNING ============
static int name_compare(const void *a, const void *b) {
    return strcasecmp(((const Game*)a)->name, ((const Game*)b)->name);
}

static void scan_games(void) {
    DIR *dir = opendir(ISO_DIR);
    if (!dir) return;
    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL && game_count < 256) {
        int len = strlen(entry->d_name);
        if (len < 5) continue;
        char *ext = entry->d_name + len - 4;
        if (strcasecmp(ext, ".iso") != 0 && strcasecmp(ext, ".bin") != 0) continue;

        // Name = filename without extension
        strncpy(games[game_count].name, entry->d_name, 255);
        games[game_count].name[len - 4] = '\0';

        // Full path
        snprintf(games[game_count].path, sizeof(games[game_count].path),
                 "%s%s", ISO_DIR, entry->d_name);

        // Extract disc ID
        if (!extract_disc_id(games[game_count].path, games[game_count].id, sizeof(games[game_count].id))) {
            strncpy(games[game_count].id, "UNKNOWN", sizeof(games[game_count].id) - 1);
        }

        game_count++;
    }
    closedir(dir);
    qsort(games, game_count, sizeof(Game), name_compare);
}

// ============ CONFIG GENERATION ============
static int set_active_game(const char *iso_path, const char *disc_id) {
    // 1. Read default template (file or embedded)
    char template_buf[65536];
    int template_len = 0;

    int fd = open(DEFAULT_CONFIG, O_RDONLY);
    if (fd >= 0) {
        template_len = read(fd, template_buf, sizeof(template_buf) - 1);
        close(fd);
        if (template_len > 0) template_buf[template_len] = '\0';
    }
    if (template_len <= 0) {
        // Use embedded default
        template_len = strlen(embedded_default);
        memcpy(template_buf, embedded_default, template_len);
        template_buf[template_len] = '\0';
    }

    // 2. Write temp config: template + image path + disc id
    fd = open(TEMP_CONFIG, O_WRONLY | O_CREAT | O_TRUNC, 0777);
    if (fd < 0) return 0;

    dprintf(fd, "# Auto-generated by PS2 Launcher\n");
    dprintf(fd, "# Game: %s\n", disc_id);
    dprintf(fd, "%s\n", template_buf);
    dprintf(fd, "--image=\"%s\"\n", iso_path);
    dprintf(fd, "--ps2-title-id=%s\n", disc_id);
    close(fd);

    // 3. Update master config to point to temp config
    fd = open(MASTER_CONFIG, O_RDONLY);
    if (fd < 0) return 0;
    char buf[32768];
    int n = read(fd, buf, sizeof(buf) - 1);
    close(fd);
    if (n <= 0) return 0;
    buf[n] = '\0';

    fd = open(MASTER_CONFIG, O_WRONLY | O_CREAT | O_TRUNC, 0777);
    if (fd < 0) return 0;

    // Copy all lines except active --config= lines
    char *p = buf;
    while (*p) {
        char *line_end = p;
        while (*line_end && *line_end != '\n') line_end++;
        int line_len = line_end - p;

        if (line_len > 0 && strncmp(p, "--config=", 9) == 0 && p[0] != '#') {
            // skip active config line
        } else {
            write(fd, p, line_len);
            write(fd, "\n", 1);
        }

        p = line_end;
        if (*p == '\n') p++;
    }

    dprintf(fd, "--config=\"%s\"\n", TEMP_CONFIG);
    close(fd);
    return 1;
}

// ============ LAUNCH ============
extern int32_t sceSystemServiceLaunchApp(const char* titleId, const char* args, void* reserved);

static void launch_emulator(void) {
    sceSystemServiceLaunchApp(EMULATOR_TID, NULL, NULL);
}

// ============ VIDEO INIT ============
static int init_video(void) {
    video = sceVideoOutOpen(ORBIS_VIDEO_USER_MAIN, ORBIS_VIDEO_OUT_BUS_MAIN, 0, 0);
    if (video < 0) return -1;

    size_t size = (FB_SIZE + 0x1FFFFF) & ~0x1FFFFF;
    for (int i = 0; i < 2; i++) {
        off_t directMem = 0;
        int r = sceKernelAllocateDirectMemory(0, 0x180000000, size, 0x200000, 3, &directMem);
        if (r < 0) return -1;
        void *addr = NULL;
        r = sceKernelMapDirectMemory(&addr, size, 3, 0, directMem, 0x200000);
        if (r < 0) return -1;
        memset(addr, 0, size);
        framebuffer[i] = (uint32_t*)addr;
    }

    OrbisVideoOutBufferAttribute attr;
    sceVideoOutSetBufferAttribute(&attr, ORBIS_VIDEO_OUT_PIXEL_FORMAT_A8B8G8R8_SRGB,
                                  1, 0, 0, 0, 0, 0);
    sceVideoOutRegisterBuffers(video, 0, (void*)framebuffer, 2, &attr);
    sceVideoOutSetFlipRate(video, 0);
    return 0;
}

static void flip(void) {
    sceVideoOutSubmitFlip(video, current_buf, ORBIS_VIDEO_OUT_FLIP_VSYNC, 0);
    current_buf ^= 1;
}

// ============ MAIN ============
int main(void) {
    if (init_video() < 0) return -1;

    scePadInit();
    int pad = scePadOpen(ORBIS_USER_SERVICE_USER_ID_SYSTEM, ORBIS_PAD_PORT_TYPE_STANDARD, 0, NULL);

    scan_games();
    if (game_count == 0) {
        draw_text(100, 100, "NO ISO FILES FOUND IN /data/PS4ROMS/PS2ISO/", 0xFFFFFFFF);
        flip();
        sceKernelSleep(3);
        return 0;
    }

    OrbisPadData pad_data;
    unsigned int old_buttons = 0;

    while (1) {
        scePadReadState(pad, &pad_data);
        unsigned int buttons = pad_data.buttons;
        unsigned int pressed = buttons & ~old_buttons;
        old_buttons = buttons;

        if (pressed & ORBIS_PAD_BUTTON_UP) {
            selected = (selected - 1 + game_count) % game_count;
        }
        if (pressed & ORBIS_PAD_BUTTON_DOWN) {
            selected = (selected + 1) % game_count;
        }
        if (pressed & ORBIS_PAD_BUTTON_CROSS) {
            if (set_active_game(games[selected].path, games[selected].id)) {
                launch_emulator();
            }
        }

        // Render
        memset(framebuffer[current_buf], 0, FB_SIZE);
        draw_rect(0, 0, SCREEN_WIDTH, SCREEN_HEIGHT, 0xFF1A1A2E);

        draw_text(80, 60, "PS2 ISO LAUNCHER", 0xFFFFFFFF);
        draw_text(80, 90, "==================", 0xFFFFFFFF);

        int start_y = 140;
        int visible = 28;
        int scroll = 0;
        if (selected >= visible) scroll = selected - visible + 1;

        for (int i = scroll; i < game_count && i < scroll + visible; i++) {
            int y = start_y + (i - scroll) * 28;
            uint32_t color = (i == selected) ? 0xFFFFFF00 : 0xFFCCCCCC;
            if (i == selected) {
                draw_rect(70, y - 4, 800, 24, 0xFF333355);
            }
            draw_text(80, y, games[i].name, color);
            draw_text(500, y, games[i].id, (i == selected) ? 0xFFFFFF00 : 0xFF888888);
        }

        // Footer info
        draw_text(80, SCREEN_HEIGHT - 80, games[selected].path, 0xFF666666);
        draw_text(80, SCREEN_HEIGHT - 50, "[X] LAUNCH    [UP/DOWN] SELECT", 0xFF888888);

        flip();
        sceKernelUsleep(16666);
    }

    return 0;
}
