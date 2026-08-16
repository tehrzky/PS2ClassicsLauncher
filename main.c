void _init(void) {}
void _fini(void) {}

#include <orbis/libkernel.h>
#include <orbis/SystemService.h>
#include <orbis/UserService.h>
#include <orbis/Pad.h>
#include <orbis/VideoOut.h>
#include <orbis/Sysmodule.h>
#include <dirent.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <unistd.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <stdarg.h>

// ============ CONFIG ============
#define MASTER_CONFIG   "/data/PS4ROMS/PS2ISO/config-emu-ps4.txt"
#define ISO_DIR         "/data/PS4ROMS/PS2ISO/"
#define DEFAULT_CONFIG  "/data/PS4ROMS/PS2ISO/default.txt"
#define TEMP_CONFIG     "/data/PS4ROMS/PS2ISO/.launcher_temp.txt"
#define EMULATOR_TID    "PCSX20042"

#define SCREEN_WIDTH    1920
#define SCREEN_HEIGHT   1080
#define FB_SIZE         (SCREEN_WIDTH * SCREEN_HEIGHT * 4)

// ============ COLORS (format is A8B8G8R8 => 0xAABBGGRR, NOT the usual ARGB) ============
#define COLOR_BLACK      0xFF000000
#define COLOR_WHITE      0xFFFFFFFF
#define COLOR_RED        0xFF0000FF   // A=FF B=00 G=00 R=FF
#define COLOR_GREEN      0xFF00FF00   // A=FF B=00 G=FF R=00
#define COLOR_BLUE       0xFFFF0000   // A=FF B=FF G=00 R=00
#define COLOR_YELLOW     0xFF00FFFF   // A=FF B=00 G=FF R=FF
#define COLOR_BG_NAVY    0xFF2E1A1A   // dark navy-ish background (B=2E G=1A R=1A)
#define COLOR_SELECT_BG  0xFF553333   // highlighted row background
#define COLOR_GRAY       0xFF888888
#define COLOR_DARKGRAY   0xFF666666

// ============ EMBEDDED DEFAULT CONFIG ============
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

// ============ DEBUG LOG ============
static void log_debug(const char *fmt, ...) {
    static int first = 1;
    int fd;
    if (first) {
        fd = open("/data/PS4ROMS/PS2ISO/launcher_log.txt", O_WRONLY | O_CREAT | O_TRUNC, 0777);
        first = 0;
    } else {
        fd = open("/data/PS4ROMS/PS2ISO/launcher_log.txt", O_WRONLY | O_CREAT | O_APPEND, 0777);
    }
    if (fd < 0) return;
    char buf[1024];
    va_list args;
    va_start(args, fmt);
    int n = vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    if (n > 0) {
        write(fd, buf, n);
        write(fd, "\n", 1);
    }
    close(fd);
}

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

// scale = how many screen pixels each font pixel becomes (1 = original 8x8, 3 = 24x24)
static void draw_char_scaled(int x, int y, char c, uint32_t color, int scale) {
    if (c < 32 || c > 127) return;
    const unsigned char *f = font8x8[c - 32];
    for (int row = 0; row < 8; row++) {
        for (int col = 0; col < 8; col++) {
            if (f[row] & (1 << (7 - col))) {
                draw_rect(x + col * scale, y + row * scale, scale, scale, color);
            }
        }
    }
}

static void draw_text_scaled(int x, int y, const char *s, uint32_t color, int scale) {
    while (*s) {
        draw_char_scaled(x, y, *s++, color, scale);
        x += 8 * scale;
    }
}

// Back-compat wrappers at scale 1, kept in case anything still calls the old names
static void draw_char(int x, int y, char c, uint32_t color) { draw_char_scaled(x, y, c, color, 1); }
static void draw_text(int x, int y, const char *s, uint32_t color) { draw_text_scaled(x, y, s, color, 1); }

// ============ ISO DISC ID EXTRACTION ============
static int extract_disc_id(const char *path, char *out_id, size_t out_len) {
    int fd = open(path, O_RDONLY);
    if (fd < 0) return 0;

    char buf[131072];
    int n = read(fd, buf, sizeof(buf));
    close(fd);
    if (n <= 0) return 0;

    for (int i = 0; i < n - 30; i++) {
        if (strncmp(buf + i, "BOOT2", 5) == 0 || strncmp(buf + i, "BOOT", 4) == 0) {
            int limit = i + 256;
            if (limit > n) limit = n;
            for (int j = i; j < limit - 8; j++) {
                if (strncmp(buf + j, "cdrom0:", 7) == 0) {
                    char *p = buf + j + 7;
                    if (*p == '\\' || *p == '/') p++;
                    char raw[32] = {0};
                    int k = 0;
                    while (k < 31 && p[k] && p[k] != ';' && p[k] != '\n' && p[k] != '\r' && p[k] != ' ') {
                        raw[k] = p[k];
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
        }
    }

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

        strncpy(games[game_count].name, entry->d_name, 255);
        games[game_count].name[len - 4] = '\0';

        snprintf(games[game_count].path, sizeof(games[game_count].path),
                 "%s%s", ISO_DIR, entry->d_name);

        if (!extract_disc_id(games[game_count].path, games[game_count].id, sizeof(games[game_count].id))) {
            strncpy(games[game_count].id, "UNKNOWN", sizeof(games[game_count].id) - 1);
        }

        game_count++;
    }
    closedir(dir);
    qsort(games, game_count, sizeof(Game), name_compare);
}

// ============ FUZZY NAME MATCHING ============
static void normalize_name(char *dst, const char *src, size_t dst_len) {
    size_t j = 0;
    int in_brackets = 0;

    for (size_t i = 0; src[i] && j < dst_len - 1; i++) {
        char c = src[i];
        if (c == '(' || c == '[') in_brackets++;
        if (c == ')' || c == ']') { in_brackets--; continue; }
        if (in_brackets > 0) continue;
        if (isalnum((unsigned char)c) || c == ' ') {
            if (c == ' ') {
                if (j > 0 && dst[j-1] != ' ') dst[j++] = ' ';
            } else {
                dst[j++] = tolower((unsigned char)c);
            }
        }
    }
    if (j > 0 && dst[j-1] == ' ') j--;
    dst[j] = '\0';
}

static int fuzzy_match(const char *iso_name, const char *config_name) {
    char norm_iso[256], norm_cfg[256];
    normalize_name(norm_iso, iso_name, sizeof(norm_iso));
    normalize_name(norm_cfg, config_name, sizeof(norm_cfg));
    if (strstr(norm_iso, norm_cfg) != NULL) return 1;
    if (strstr(norm_cfg, norm_iso) != NULL) return 1;
    return 0;
}

// ============ EXISTING CONFIG LOOKUP ============
static int find_game_config(const char *disc_id, const char *game_name, char *out_path, size_t out_len) {
    DIR *dir = opendir(ISO_DIR "gameconfig/");
    if (!dir) return 0;

    struct dirent *entry;
    char best_match[256] = {0};

    while ((entry = readdir(dir)) != NULL) {
        int len = strlen(entry->d_name);
        if (len < 5) continue;
        if (strcasecmp(entry->d_name + len - 4, ".txt") != 0) continue;

        char cfg_name[256];
        strncpy(cfg_name, entry->d_name, len - 4);
        cfg_name[len - 4] = '\0';

        if (fuzzy_match(game_name, cfg_name)) {
            if (strcasecmp(game_name, cfg_name) == 0) {
                snprintf(out_path, out_len, "%sgameconfig/%s", ISO_DIR, entry->d_name);
                closedir(dir);
                return 1;
            }
            if (best_match[0] == '\0') {
                strncpy(best_match, entry->d_name, sizeof(best_match) - 1);
            }
        }
    }
    closedir(dir);

    if (best_match[0] != '\0') {
        snprintf(out_path, out_len, "%sgameconfig/%s", ISO_DIR, best_match);
        return 1;
    }

    snprintf(out_path, out_len, "%sgameconfig/%s.txt", ISO_DIR, disc_id);
    if (access(out_path, F_OK) == 0) return 1;

    return 0;
}

// ============ CONFIG GENERATION ============
static int set_active_game(const char *iso_path, const char *disc_id, const char *game_name) {
    char line_buf[2048];
    int n;

    char existing_config[512];
    int has_existing = find_game_config(disc_id, game_name, existing_config, sizeof(existing_config));

    int fd = open(TEMP_CONFIG, O_WRONLY | O_CREAT | O_TRUNC, 0777);
    if (fd < 0) return 0;

    if (has_existing) {
        int src = open(existing_config, O_RDONLY);
        if (src >= 0) {
            char buf[65536];
            int m = read(src, buf, sizeof(buf) - 1);
            close(src);
            if (m > 0) {
                buf[m] = '\0';
                char *p = buf;
                while (*p) {
                    char *line_end = p;
                    while (*line_end && *line_end != '\n') line_end++;
                    int line_len = line_end - p;

                    if (line_len > 0 && strncmp(p, "--image=", 8) == 0) { }
                    else if (line_len > 0 && strncmp(p, "--ps2-title-id=", 15) == 0) { }
                    else {
                        write(fd, p, line_len);
                        write(fd, "\n", 1);
                    }
                    p = line_end;
                    if (*p == '\n') p++;
                }
            }
        }
    } else {
        char template_buf[65536];
        int template_len = 0;

        int src = open(DEFAULT_CONFIG, O_RDONLY);
        if (src >= 0) {
            template_len = read(src, template_buf, sizeof(template_buf) - 1);
            close(src);
            if (template_len > 0) template_buf[template_len] = '\0';
        }
        if (template_len <= 0) {
            template_len = strlen(embedded_default);
            memcpy(template_buf, embedded_default, template_len);
            template_buf[template_len] = '\0';
        }
        write(fd, template_buf, template_len);
        if (template_len > 0 && template_buf[template_len - 1] != '\n')
            write(fd, "\n", 1);
    }

    n = snprintf(line_buf, sizeof(line_buf), "--image=\"%s\"\n", iso_path);
    write(fd, line_buf, n);
    n = snprintf(line_buf, sizeof(line_buf), "--ps2-title-id=%s\n", disc_id);
    write(fd, line_buf, n);
    close(fd);

    fd = open(MASTER_CONFIG, O_RDONLY);
    if (fd < 0) return 0;
    char buf[32768];
    int m = read(fd, buf, sizeof(buf) - 1);
    close(fd);
    if (m <= 0) return 0;
    buf[m] = '\0';

    fd = open(MASTER_CONFIG, O_WRONLY | O_CREAT | O_TRUNC, 0777);
    if (fd < 0) return 0;

    char *p = buf;
    while (*p) {
        char *line_end = p;
        while (*line_end && *line_end != '\n') line_end++;
        int line_len = line_end - p;

        if (line_len > 0 && strncmp(p, "--config=", 9) == 0 && p[0] != '#') {
        } else {
            write(fd, p, line_len);
            write(fd, "\n", 1);
        }
        p = line_end;
        if (*p == '\n') p++;
    }

    n = snprintf(line_buf, sizeof(line_buf), "--config=\"%s\"\n", TEMP_CONFIG);
    write(fd, line_buf, n);
    close(fd);
    return 1;
}

// ============ LAUNCH ============
static void launch_emulator(void) {
    sceSystemServiceLaunchApp(EMULATOR_TID, NULL, NULL);
}

// ============ VIDEO INIT ============
static int init_video(void) {
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

static void flip(void) {
    sceVideoOutSubmitFlip(video, current_buf, ORBIS_VIDEO_OUT_FLIP_VSYNC, 0);
    current_buf ^= 1;
}

// ============ MAIN ============
int main(void) {
    log_debug("=== START ===");

    sceSysmoduleLoadModuleInternal(ORBIS_SYSMODULE_INTERNAL_USER_SERVICE);
    sceSysmoduleLoadModuleInternal(ORBIS_SYSMODULE_INTERNAL_VIDEO_OUT);
    sceSysmoduleLoadModuleInternal(ORBIS_SYSMODULE_INTERNAL_PAD);
    log_debug("MODULES LOADED");

    if (init_video() < 0) {
        log_debug("VIDEO FAIL");
        return -1;
    }
    log_debug("VIDEO OK");

    // UserService MUST be initialized before GetInitialUser/scePadOpen will work.
    // (Previously commented out — that was masking an unrelated crash; video-out
    // is confirmed working now, so this is safe to run.)
    int userInitRc = sceUserServiceInitialize(NULL);
    log_debug("USER SERVICE INIT: %d", userInitRc);

    scePadInit();
    log_debug("PAD INIT DONE");

    int userId = 1;
    int ret = sceUserServiceGetInitialUser(&userId);
    log_debug("GetInitialUser: %d (uid=%d)", ret, userId);

    int pad = scePadOpen(userId, ORBIS_PAD_PORT_TYPE_STANDARD, 0, NULL);
    log_debug("PAD OPEN: %d (uid=%d)", pad, userId);

    scan_games();
    log_debug("GAMES: %d", game_count);

    if (game_count == 0) {
        draw_text_scaled(100, 100, "NO ISO FILES FOUND", COLOR_WHITE, 3);
        flip();
        sceKernelSleep(3);
        return 0;
    }

    OrbisPadData pad_data;
    unsigned int old_buttons = 0;

    while (1) {
        if (pad >= 0) {
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
                log_debug("LAUNCH: %s", games[selected].name);
                if (set_active_game(games[selected].path, games[selected].id, games[selected].name)) {
                    launch_emulator();
                }
            }
        }

        memset(framebuffer[current_buf], 0, FB_SIZE);
        draw_rect(0, 0, SCREEN_WIDTH, SCREEN_HEIGHT, COLOR_BG_NAVY);

        draw_text_scaled(80, 50, "PS2 ISO LAUNCHER", COLOR_WHITE, 3);

        int start_y = 140;
        int row_height = 34;      // 8px font * scale 3 (=24) + a little spacing
        int visible = (SCREEN_HEIGHT - start_y - 80) / row_height;
        int scroll = 0;
        if (selected >= visible) scroll = selected - visible + 1;

        for (int i = scroll; i < game_count && i < scroll + visible; i++) {
            int y = start_y + (i - scroll) * row_height;
            uint32_t color = (i == selected) ? COLOR_YELLOW : COLOR_WHITE;
            if (i == selected) {
                draw_rect(60, y - 4, SCREEN_WIDTH - 120, row_height, COLOR_SELECT_BG);
            }
            draw_text_scaled(80, y, games[i].name, color, 2);
        }

        draw_text_scaled(80, SCREEN_HEIGHT - 40, "[X] LAUNCH   [UP/DOWN] SELECT", COLOR_GRAY, 2);

        flip();
        sceKernelUsleep(16666);
    }

    return 0;
}
