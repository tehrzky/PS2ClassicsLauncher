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
#include <sys/stat.h>
#include <unistd.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <stdarg.h>
#include <stdint.h>

// ============ ERROR CODES ============
#define SCE_LNC_UTIL_ERROR_ALREADY_RUNNING 0x80D00504
// ============ CONFIG ============
#define MASTER_CONFIG   "/data/PS4ROMS/PS2ISO/config/config-emu-ex.txt"
#define ISO_DIR         "/data/PS4ROMS/PS2ISO/"
#define GAMECONFIG_DIR  "/data/PS4ROMS/PS2ISO/gameconfig/"
#define DEFAULT_CONFIG  "/data/PS4ROMS/PS2ISO/config/default.txt"
#define TEMP_CONFIG     "/data/PS4ROMS/PS2ISO/config/.launcher_temp.txt"
#define EMULATOR_TID    "PCSX20042"

#define SCREEN_WIDTH    1920
#define SCREEN_HEIGHT   1080
#define FB_SIZE         (SCREEN_WIDTH * SCREEN_HEIGHT * 4)

// ============ COLORS ============
#define COLOR_BLACK      0xFF000000
#define COLOR_WHITE      0xFFFFFFFF
#define COLOR_RED        0xFF0000FF
#define COLOR_GREEN      0xFF00FF00
#define COLOR_BLUE       0xFFFF0000
#define COLOR_YELLOW     0xFF00FFFF
#define COLOR_BG_NAVY    0xFF2E1A1A
#define COLOR_SELECT_BG  0xFF553333
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
static unsigned char font8x8[96][8] = {
    /* 32 space  */ {0,  0,  0,  0,  0,  0,  0,  0},
    /* 33 !      */ {0,  0,  0, 95,  0,  0,  0,  0},
    /* 34 "      */ {0,  0,  3,  0,  3,  0,  0,  0},
    /* 35 #      */ {0, 20,127, 20,127, 20,  0,  0},
    /* 36 $      */ {0, 36, 42,127, 42, 18,  0,  0},
    /* 37 %      */ {0, 35, 19,  8,100, 98,  0,  0},
    /* 38 &      */ {0, 54, 73, 85, 34, 80,  0,  0},
    /* 39 '      */ {0,  0,  0,  3,  0,  0,  0,  0},
    /* 40 (      */ {0,  0, 28, 34, 65,  0,  0,  0},
    /* 41 )      */ {0,  0, 65, 34, 28,  0,  0,  0},
    /* 42 *      */ {0,  8, 42, 28, 42,  8,  0,  0},
    /* 43 +      */ {0,  8,  8, 62,  8,  8,  0,  0},
    /* 44 ,      */ {0,  0, 80, 48,  0,  0,  0,  0},
    /* 45 -      */ {0,  8,  8,  8,  8,  8,  0,  0},
    /* 46 .      */ {0,  0, 96, 96,  0,  0,  0,  0},
    /* 47 /      */ {0, 32, 16,  8,  4,  2,  0,  0},
    /* 48 0      */ {0, 62, 81, 73, 69, 62,  0,  0},
    /* 49 1      */ {0,  0, 66,127, 64,  0,  0,  0},
    /* 50 2      */ {0, 66, 97, 81, 73, 70,  0,  0},
    /* 51 3      */ {0, 33, 65, 69, 75, 49,  0,  0},
    /* 52 4      */ {0, 24, 20, 18,127, 16,  0,  0},
    /* 53 5      */ {0, 39, 69, 69, 69, 57,  0,  0},
    /* 54 6      */ {0, 60, 74, 73, 73, 48,  0,  0},
    /* 55 7      */ {0,  1,113,  9,  5,  3,  0,  0},
    /* 56 8      */ {0, 54, 73, 73, 73, 54,  0,  0},
    /* 57 9      */ {0,  6, 73, 73, 41, 30,  0,  0},
    /* 58 :      */ {0,  0, 54, 54,  0,  0,  0,  0},
    /* 59 ;      */ {0,  0, 86, 54,  0,  0,  0,  0},
    /* 60 <      */ {0,  8, 20, 34, 65,  0,  0,  0},
    /* 61 =      */ {0, 20, 20, 20, 20, 20,  0,  0},
    /* 62 >      */ {0,  0, 65, 34, 20,  8,  0,  0},
    /* 63 ?      */ {0,  2,  1, 81,  9,  6,  0,  0},
    /* 64 @      */ {0, 50, 73,121, 65, 62,  0,  0},
    /* 65 A      */ {0,126, 17, 17, 17,126,  0,  0},
    /* 66 B      */ {0,127, 73, 73, 73, 54,  0,  0},
    /* 67 C      */ {0, 62, 65, 65, 65, 34,  0,  0},
    /* 68 D      */ {0,127, 65, 65, 34, 28,  0,  0},
    /* 69 E      */ {0,127, 73, 73, 73, 65,  0,  0},
    /* 70 F      */ {0,127,  9,  9,  9,  1,  0,  0},
    /* 71 G      */ {0, 62, 65, 73, 73,122,  0,  0},
    /* 72 H      */ {0,127,  8,  8,  8,127,  0,  0},
    /* 73 I      */ {0,  0, 65,127, 65,  0,  0,  0},
    /* 74 J      */ {0, 32, 64, 65, 63,  1,  0,  0},
    /* 75 K      */ {0,127,  8, 20, 34, 65,  0,  0},
    /* 76 L      */ {0,127, 64, 64, 64, 64,  0,  0},
    /* 77 M      */ {0,127,  2, 12,  2,127,  0,  0},
    /* 78 N      */ {0,127,  4,  8, 16,127,  0,  0},
    /* 79 O      */ {0, 62, 65, 65, 65, 62,  0,  0},
    /* 80 P      */ {0,127,  9,  9,  9,  6,  0,  0},
    /* 81 Q      */ {0, 62, 65, 81, 33, 94,  0,  0},
    /* 82 R      */ {0,127,  9, 25, 41, 70,  0,  0},
    /* 83 S      */ {0, 70, 73, 73, 73, 49,  0,  0},
    /* 84 T      */ {0,  1,  1,127,  1,  1,  0,  0},
    /* 85 U      */ {0, 63, 64, 64, 64, 63,  0,  0},
    /* 86 V      */ {0, 31, 32, 64, 32, 31,  0,  0},
    /* 87 W      */ {0, 63, 64, 56, 64, 63,  0,  0},
    /* 88 X      */ {0, 99, 20,  8, 20, 99,  0,  0},
    /* 89 Y      */ {0,  7,  8,112,  8,  7,  0,  0},
    /* 90 Z      */ {0, 97, 81, 73, 69, 67,  0,  0},
    /* 91 [      */ {0,  0,127, 65, 65,  0,  0,  0},
    /* 92 \      */ {0,  2,  4,  8, 16, 32,  0,  0},
    /* 93 ]      */ {0,  0, 65, 65,127,  0,  0,  0},
    /* 94 ^      */ {0,  4,  2,  1,  2,  4,  0,  0},
    /* 95 _      */ {0, 64, 64, 64, 64, 64,  0,  0},
    /* 96 `      */ {0,  0,  0,  1,  2,  4,  0,  0},
    /* 97 a      */ {0, 32, 84, 84, 84,120,  0,  0},
    /* 98 b      */ {0,127, 72, 68, 68, 56,  0,  0},
    /* 99 c      */ {0, 56, 68, 68, 68, 32,  0,  0},
    /*100 d      */ {0, 56, 68, 68, 72,127,  0,  0},
    /*101 e      */ {0, 56, 84, 84, 84, 24,  0,  0},
    /*102 f      */ {0,  8,126,  9,  1,  2,  0,  0},
    /*103 g      */ {0, 12, 82, 82, 82, 62,  0,  0},
    /*104 h      */ {0,127,  8,  4,  4,120,  0,  0},
    /*105 i      */ {0,  0, 68,125, 64,  0,  0,  0},
    /*106 j      */ {0, 32, 64, 68, 61,  0,  0,  0},
    /*107 k      */ {0,127, 16, 40, 68,  0,  0,  0},
    /*108 l      */ {0,  0, 65,127, 64,  0,  0,  0},
    /*109 m      */ {0,124,  4, 24,  4,120,  0,  0},
    /*110 n      */ {0,124,  8,  4,  4,120,  0,  0},
    /*111 o      */ {0, 56, 68, 68, 68, 56,  0,  0},
    /*112 p      */ {0,124, 20, 20, 20,  8,  0,  0},
    /*113 q      */ {0,  8, 20, 20, 24,124,  0,  0},
    /*114 r      */ {0,124,  8,  4,  4,  8,  0,  0},
    /*115 s      */ {0, 72, 84, 84, 84, 32,  0,  0},
    /*116 t      */ {0,  4, 63, 68, 64, 32,  0,  0},
    /*117 u      */ {0, 60, 64, 64, 32,124,  0,  0},
    /*118 v      */ {0, 28, 32, 64, 32, 28,  0,  0},
    /*119 w      */ {0, 60, 64, 48, 64, 60,  0,  0},
    /*120 x      */ {0, 68, 40, 16, 40, 68,  0,  0},
    /*121 y      */ {0, 12, 80, 80, 80, 60,  0,  0},
    /*122 z      */ {0, 68,100, 84, 76, 68,  0,  0},
    /*123 {      */ {0,  0,  8, 54, 65,  0,  0,  0},
    /*124 |      */ {0,  0,  0,127,  0,  0,  0,  0},
    /*125 }      */ {0,  0, 65, 54,  8,  0,  0,  0},
    /*126 ~      */ {0,  8,  8, 42, 28,  8,  0,  0}
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

static void draw_char_scaled(int x, int y, char c, uint32_t color, int scale) {
    if (c < 32 || c > 127) return;
    const unsigned char *f = font8x8[c - 32];

    for (int row = 0; row < 8; row++) {
        for (int col = 0; col < 8; col++) {
            if (f[col] & (1 << row)) {
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

static void draw_char(int x, int y, char c, uint32_t color) { draw_char_scaled(x, y, c, color, 1); }
static void draw_text(int x, int y, const char *s, uint32_t color) { draw_text_scaled(x, y, s, color, 1); }

// ============ helper: little-endian 32-bit read ============
static uint32_t read_le32(const unsigned char *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

// ============ ISO9660-BASED DISC ID EXTRACTION ============
// Properly parses the ISO filesystem (Primary Volume Descriptor -> root
// directory -> SYSTEM.CNF's real on-disc location) instead of blindly
// byte-scanning the first N bytes of the file, which is unreliable because
// SYSTEM.CNF's actual data can be located anywhere in the image.
static int extract_disc_id(const char *path, char *out_id, size_t out_len) {
    int fd = open(path, O_RDONLY);
    if (fd < 0) return 0;

    unsigned char sector[2048];

    // Primary Volume Descriptor is always at LBA 16 for ISO9660
    if (lseek(fd, (off_t)16 * 2048, SEEK_SET) < 0) { close(fd); return 0; }
    if (read(fd, sector, 2048) != 2048) { close(fd); return 0; }
    if (sector[0] != 1 || memcmp(sector + 1, "CD001", 5) != 0) { close(fd); return 0; }

    // Root directory record starts at offset 156 within the PVD
    const unsigned char *root = sector + 156;
    uint32_t root_lba  = read_le32(root + 2);
    uint32_t root_size = read_le32(root + 10);

    if (root_lba == 0 || root_size == 0 || root_size > 1024 * 1024) { close(fd); return 0; }

    unsigned char *dir = (unsigned char*)malloc(((root_size + 2047) / 2048) * 2048);
    if (!dir) { close(fd); return 0; }

    if (lseek(fd, (off_t)root_lba * 2048, SEEK_SET) < 0) { free(dir); close(fd); return 0; }
    if (read(fd, dir, root_size) != (int)root_size) { free(dir); close(fd); return 0; }

    uint32_t cnf_lba = 0, cnf_size = 0;
    uint32_t pos = 0;
    while (pos + 33 < root_size) {
        unsigned char len = dir[pos];
        if (len == 0) {
            // zero-length record means padding to the next 2048-byte sector
            uint32_t next = ((pos / 2048) + 1) * 2048;
            if (next <= pos) break;
            pos = next;
            continue;
        }
        unsigned char fi_len = dir[pos + 32];
        const char *name = (const char*)(dir + pos + 33);

        if ((fi_len == 10 && strncasecmp(name, "SYSTEM.CNF", 10) == 0) ||
            (fi_len == 12 && strncasecmp(name, "SYSTEM.CNF;1", 12) == 0)) {
            cnf_lba  = read_le32(dir + pos + 2);
            cnf_size = read_le32(dir + pos + 10);
            break;
        }
        pos += len;
    }
    free(dir);

    if (cnf_lba == 0) { close(fd); return 0; }

    char cnf_buf[2048];
    size_t to_read = (cnf_size > 0 && cnf_size < sizeof(cnf_buf) - 1) ? cnf_size : sizeof(cnf_buf) - 1;
    if (lseek(fd, (off_t)cnf_lba * 2048, SEEK_SET) < 0) { close(fd); return 0; }
    int n = read(fd, cnf_buf, to_read);
    close(fd);
    if (n <= 0) return 0;
    cnf_buf[n] = '\0';

    // SYSTEM.CNF contains a line like: BOOT2 = cdrom0:\SLUS_213.85;1
    char *p = strstr(cnf_buf, "cdrom0:");
    if (!p) return 0;
    p += 7;
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
        else if (raw[r] == '.') { /* skip the dot, e.g. SLUS_213.85 -> SLUS-21385 */ }
        else out_id[fi++] = raw[r];
    }
    out_id[fi] = '\0';
    return fi > 0;
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
            log_debug("DISC ID extraction FAILED for: %s", games[game_count].name);
        }

        game_count++;
    }
    closedir(dir);
    qsort(games, game_count, sizeof(Game), name_compare);
}

// ============ CHECK IF APP IS INSTALLED (informational only, not a launch gate) ============
static int check_app_installed(const char *title_id) {
    const char *search_paths[] = {
        "/user/app/%s/sce_sys/param.sfo",
        "/data/app/%s/sce_sys/param.sfo",
        "/system/app/%s/sce_sys/param.sfo",
        NULL
    };

    char path[512];
    for (int i = 0; search_paths[i] != NULL; i++) {
        snprintf(path, sizeof(path), search_paths[i], title_id);
        int fd = open(path, O_RDONLY);
        if (fd >= 0) {
            close(fd);
            log_debug("App %s is installed at: %s", title_id, path);
            return 1;
        }
    }

    log_debug("App %s check inconclusive (sandbox may not expose these paths)", title_id);
    return 0;
}

// ============ PER-GAME CONFIG: save/reuse by exact game name in gameconfig/ ============
// If /data/PS4ROMS/PS2ISO/gameconfig/<game name>.txt already exists, it is
// left untouched (so manual per-game tweaks persist). If it doesn't exist,
// it's created from the default template. Either way, the live --image=
// and --ps2-title-id= lines are written fresh into TEMP_CONFIG each launch.
static int set_active_game(const char *iso_path, const char *disc_id, const char *game_name) {
    char line_buf[2048];
    int n;

    mkdir("/data/PS4ROMS/PS2ISO/config", 0777);
    mkdir(GAMECONFIG_DIR, 0777);

    char game_config_path[700];
    snprintf(game_config_path, sizeof(game_config_path), "%s%s.txt", GAMECONFIG_DIR, game_name);

    int has_existing = (access(game_config_path, F_OK) == 0);

    if (!has_existing) {
        int gfd = open(game_config_path, O_WRONLY | O_CREAT | O_TRUNC, 0777);
        if (gfd < 0) {
            log_debug("set_active_game: failed to create %s", game_config_path);
        } else {
            int dlen = 0;
            int dsrc = open(DEFAULT_CONFIG, O_RDONLY);
            char default_buf[65536];
            if (dsrc >= 0) {
                dlen = read(dsrc, default_buf, sizeof(default_buf) - 1);
                close(dsrc);
                if (dlen > 0) default_buf[dlen] = '\0';
            }
            if (dlen <= 0) {
                dlen = strlen(embedded_default);
                memcpy(default_buf, embedded_default, dlen);
                default_buf[dlen] = '\0';
            }
            write(gfd, default_buf, dlen);
            close(gfd);
            log_debug("Created new gameconfig: %s", game_config_path);
        }
    }

    int fd = open(TEMP_CONFIG, O_WRONLY | O_CREAT | O_TRUNC, 0777);
    if (fd < 0) {
        log_debug("set_active_game: failed to open %s", TEMP_CONFIG);
        return 0;
    }

    int src = open(game_config_path, O_RDONLY);
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
    } else {
        log_debug("set_active_game: could not read %s", game_config_path);
    }

    n = snprintf(line_buf, sizeof(line_buf), "--image=\"%s\"\n", iso_path);
    write(fd, line_buf, n);
    n = snprintf(line_buf, sizeof(line_buf), "--ps2-title-id=%s\n", disc_id);
    write(fd, line_buf, n);
    close(fd);

    fd = open(MASTER_CONFIG, O_RDONLY);
    if (fd < 0) {
        log_debug("set_active_game: failed to open %s", MASTER_CONFIG);
        return 0;
    }
    char buf[32768];
    int m = read(fd, buf, sizeof(buf) - 1);
    close(fd);
    if (m <= 0) {
        log_debug("set_active_game: %s empty or unreadable", MASTER_CONFIG);
        return 0;
    }
    buf[m] = '\0';

    fd = open(MASTER_CONFIG, O_WRONLY | O_CREAT | O_TRUNC, 0777);
    if (fd < 0) {
        log_debug("set_active_game: failed to rewrite %s", MASTER_CONFIG);
        return 0;
    }

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
    log_debug("WROTE MASTER CONFIG: %s (game config: %s)", MASTER_CONFIG, game_config_path);
    return 1;
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


// ============ LAUNCH ============
static int ps4_load_prx(const char *path, int *mod_id) {
    return (int)syscall(594, path, 0, mod_id, 0);
}

static int ps4_dlsym(int mod_id, const char *symbol, void **addr) {
    return (int)syscall(591, (long)mod_id, symbol, addr);
}

static int ps4_get_module_list(int *handles, int max, uint64_t *count) {
    return (int)syscall(592, handles, (int)max, count);
}

static void *find_symbol(const char *symbol) {
    int handles[256];
    uint64_t count = 0;
    void *addr = NULL;

    int r = ps4_get_module_list(handles, 256, &count);
    log_debug("ps4_get_module_list: ret=%d count=%llu", r, (unsigned long long)count);
    if (r != 0) return NULL;

    for (uint64_t i = 0; i < count; i++) {
        if (ps4_dlsym(handles[i], symbol, &addr) == 0 && addr != NULL) {
            log_debug("Found %s in handle 0x%x at %p", symbol, handles[i], addr);
            return addr;
        }
    }
    log_debug("Symbol %s not found in any of %llu modules", symbol, (unsigned long long)count);
    return NULL;
}

static void launch_emulator(void) {
    log_debug("=== LAUNCHING EMULATOR ===");
    log_debug("EMULATOR_TID: %s", EMULATOR_TID);

    // Method 1: sceLncUtilLaunchApp — designed exactly for launching other apps
    void *lnc_fn = find_symbol("sceLncUtilLaunchApp");
    if (lnc_fn) {
        typedef int (*LncLaunch_t)(const char *, const char *, void *);
        int ret = ((LncLaunch_t)lnc_fn)(EMULATOR_TID, NULL, NULL);
        log_debug("sceLncUtilLaunchApp returned: 0x%08X", ret);
        sceKernelSleep(5);
        log_debug("Still alive after sceLncUtilLaunchApp");
    }

    // Method 2: sceSystemServiceLoadExec with title ID
    void *exec_fn = find_symbol("sceSystemServiceLoadExec");
    if (exec_fn) {
        typedef void (*LoadExec_t)(const char *, void *);
        log_debug("Calling sceSystemServiceLoadExec(%s)...", EMULATOR_TID);
        ((LoadExec_t)exec_fn)(EMULATOR_TID, NULL);
        log_debug("sceSystemServiceLoadExec returned");
        sceKernelSleep(5);
    }

    log_debug("ALL METHODS FAILED");
    draw_text_scaled(80, 480, "LAUNCH FAILED!", COLOR_RED, 3);
    draw_text_scaled(80, 530, "Check launcher_log.txt", COLOR_WHITE, 2);
    flip();
    sceKernelSleep(5);
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

    check_app_installed(EMULATOR_TID); // informational log only, does not block launch

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
                } else {
                    log_debug("set_active_game FAILED for %s", games[selected].name);
                }
            }
        }

        memset(framebuffer[current_buf], 0, FB_SIZE);
        draw_rect(0, 0, SCREEN_WIDTH, SCREEN_HEIGHT, COLOR_BG_NAVY);

        draw_text_scaled(80, 50, "PS2 ISO LAUNCHER", COLOR_WHITE, 3);

        int start_y = 140;
        int row_height = 42;
        int visible = (SCREEN_HEIGHT - start_y - 100) / row_height;
        int scroll = 0;
        if (selected >= visible) scroll = selected - visible + 1;

        for (int i = scroll; i < game_count && i < scroll + visible; i++) {
            int y = start_y + (i - scroll) * row_height;
            uint32_t color = (i == selected) ? COLOR_YELLOW : COLOR_WHITE;
            if (i == selected) {
                draw_rect(60, y - 4, SCREEN_WIDTH - 120, row_height, COLOR_SELECT_BG);
            }
            draw_text_scaled(80, y, games[i].name, color, 3);
        }

        draw_text_scaled(80, SCREEN_HEIGHT - 40, "[X] LAUNCH   [UP/DOWN] SELECT", COLOR_GRAY, 2);

        flip();
        sceKernelUsleep(16666);
    }

    return 0;
}
