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
    char display_name[256];
    char id[32];
} Game;

static Game games[256];
static int game_count = 0;
static int selected = 0;
static uint32_t *framebuffer[2];
static int video;
static int current_buf = 0;

typedef struct {
    char id[32];
    char name[256];
} GoodName;

static GoodName good_names[2048];
static int good_name_count = 0;

static void load_good_names(void) {
    int fd = open("/data/PS4ROMS/PS2ISO/goodnames.txt", O_RDONLY);
    if (fd < 0) return;
    char buf[65536];
    int n = read(fd, buf, sizeof(buf) - 1);
    close(fd);
    if (n <= 0) return;
    buf[n] = '\0';

    char *p = buf;
    while (*p && good_name_count < 2048) {
        while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') p++;
        if (*p == '#' || *p == '\0') {
            while (*p && *p != '\n') p++;
            continue;
        }
        char *eq = strchr(p, '=');
        if (!eq) break;
        int id_len = eq - p;
        if (id_len > 0 && id_len < 32) {
            strncpy(good_names[good_name_count].id, p, id_len);
            good_names[good_name_count].id[id_len] = '\0';

            char *name_start = eq + 1;
            char *name_end = name_start;
            while (*name_end && *name_end != '\n' && *name_end != '\r') name_end++;
            int name_len = name_end - name_start;
            if (name_len > 255) name_len = 255;
            strncpy(good_names[good_name_count].name, name_start, name_len);
            good_names[good_name_count].name[name_len] = '\0';
            good_name_count++;
        }
        p = name_end;
        while (*p && *p != '\n') p++;
    }
}

static const char* lookup_good_name(const char *disc_id) {
    for (int i = 0; i < good_name_count; i++) {
        if (strcasecmp(good_names[i].id, disc_id) == 0)
            return good_names[i].name;
    }
    return NULL;
}

static char* stristr(const char *haystack, const char *needle) {
    if (!needle || !needle[0]) return (char*)haystack;
    char *h = (char*)haystack;
    while (*h) {
        if (tolower((unsigned char)*h) == tolower((unsigned char)*needle)) {
            char *h2 = h + 1;
            const char *n2 = needle + 1;
            while (*n2 && tolower((unsigned char)*h2) == tolower((unsigned char)*n2)) {
                h2++; n2++;
            }
            if (!*n2) return h;
        }
        h++;
    }
    return NULL;
}

static void build_display_name(const char *iso_name, const char *disc_id, char *out, size_t out_len) {
    const char *good = lookup_good_name(disc_id);
    if (good) {
        const char *markers[] = {
            "(Disc 1)", "(Disc 2)", "(Disc 3)", "(Disc 4)",
            "[Disc 1]", "[Disc 2]", "[Disc 3]", "[Disc 4]",
            "Disc 1", "Disc 2", "Disc 3", "Disc 4", NULL
        };
        const char *found = NULL;
        for (int i = 0; markers[i]; i++) {
            if (stristr(iso_name, markers[i])) {
                found = markers[i];
                break;
            }
        }
        if (found) {
            snprintf(out, out_len, "%s %s", good, found);
        } else {
            strncpy(out, good, out_len - 1);
            out[out_len - 1] = '\0';
        }
    } else {
        strncpy(out, iso_name, out_len - 1);
        out[out_len - 1] = '\0';
        size_t len = strlen(out);
        if (len > 4 && (strcasecmp(out + len - 4, ".iso") == 0 || strcasecmp(out + len - 4, ".bin") == 0)) {
            out[len - 4] = '\0';
        }
    }
}

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
    return strcasecmp(((const Game*)a)->display_name, ((const Game*)b)->display_name);
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

        build_display_name(entry->d_name, games[game_count].id,
                           games[game_count].display_name,
                           sizeof(games[game_count].display_name));

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

// ============ CONFIG DISCOVERY ============
// Scans gameconfig/ for any .txt containing "#  Disc ID: <id>" (or similar).
// If multiple files match, the most recently modified one wins (handy for
// backups). Returns 1 if a matching file was found.
static int find_config_by_disc_id(const char *disc_id, char *out_path, size_t out_size) {
    if (!disc_id || disc_id[0] == '\0' || strcasecmp(disc_id, "UNKNOWN") == 0)
        return 0;

    DIR *dir = opendir(GAMECONFIG_DIR);
    if (!dir) return 0;

    struct dirent *entry;
    char best_path[700] = {0};
    time_t best_mtime = 0;

    while ((entry = readdir(dir)) != NULL) {
        int len = strlen(entry->d_name);
        if (len < 5) continue;
        if (strcasecmp(entry->d_name + len - 4, ".txt") != 0) continue;

        char path[700];
        snprintf(path, sizeof(path), "%s%s", GAMECONFIG_DIR, entry->d_name);

        int fd = open(path, O_RDONLY);
        if (fd < 0) continue;

        struct stat st;
        fstat(fd, &st);

        char buf[65536];
        int n = read(fd, buf, sizeof(buf) - 1);
        close(fd);
        if (n <= 0) continue;
        buf[n] = '\0';

        char search[64];
        snprintf(search, sizeof(search), "#  Disc ID:     %s", disc_id);
        char search2[64];
        snprintf(search2, sizeof(search2), "# Disc ID: %s", disc_id);

        if (strstr(buf, search) != NULL || strstr(buf, search2) != NULL) {
            if (st.st_mtime > best_mtime) {
                strncpy(best_path, path, sizeof(best_path) - 1);
                best_path[sizeof(best_path) - 1] = '\0';
                best_mtime = st.st_mtime;
            }
        }
    }
    closedir(dir);

    if (best_path[0]) {
        strncpy(out_path, best_path, out_size - 1);
        out_path[out_size - 1] = '\0';
        return 1;
    }
    return 0;
}

// ============ FALLBACK: case-insensitive basename match ============
static int find_config_by_filename(const char *game_name, char *out_path, size_t out_size) {
    DIR *dir = opendir(GAMECONFIG_DIR);
    if (!dir) return 0;

    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL) {
        int len = strlen(entry->d_name);
        if (len < 5) continue;
        if (strcasecmp(entry->d_name + len - 4, ".txt") != 0) continue;

        char basename[256];
        strncpy(basename, entry->d_name, len - 4);
        basename[len - 4] = '\0';

        if (strcasecmp(basename, game_name) == 0) {
            snprintf(out_path, out_size, "%s%s", GAMECONFIG_DIR, entry->d_name);
            closedir(dir);
            return 1;
        }
    }
    closedir(dir);
    return 0;
}

// ============ PER-GAME CONFIG ============
// 1. Finds existing config by Disc ID inside the file contents.
// 2. Falls back to case-insensitive ISO basename match.
// 3. Creates a new formal config from template if nothing found.
// 4. Writes .launcher_temp.txt: copies gameconfig verbatim except
//    stripping --image= (single-disc), then appends fresh --image=.
//    --image-discN= lines are NEVER touched. --ps2-title-id= is
//    preserved from the gameconfig (so multi-disc shared IDs stick).
// 5. Parses # Emulator: line and returns the TID via out_emulator_tid.
static int set_active_game(const char *iso_path, const char *disc_id,
                           const char *game_name, char *out_emulator_tid, size_t tid_size) {
    char line_buf[2048];
    int n;

    mkdir("/data/PS4ROMS/PS2ISO/config", 0777);
    mkdir(GAMECONFIG_DIR, 0777);

    char game_config_path[700];
    int found = find_config_by_disc_id(disc_id, game_config_path, sizeof(game_config_path));

    if (!found) {
        found = find_config_by_filename(game_name, game_config_path, sizeof(game_config_path));
    }

    if (!found) {
        snprintf(game_config_path, sizeof(game_config_path), "%s%s.txt", GAMECONFIG_DIR, game_name);
    } else {
        log_debug("Using existing config: %s", game_config_path);
    }

    if (!found) {
        int gfd = open(game_config_path, O_WRONLY | O_CREAT | O_TRUNC, 0777);
        if (gfd < 0) {
            log_debug("set_active_game: failed to create %s", game_config_path);
        } else {
            char header[1024];
            int hlen = snprintf(header, sizeof(header),
                "# ============================================================\n"
                "#  Game:        %s\n"
                "#  Disc ID:     %s\n"
                "#  Emulator:    %s\n"
                "#  Emulator Title: %s\n"
                "#\n"
                "#  Note: \n"
                "# ============================================================\n"
                "\n",
                game_name, disc_id, EMULATOR_TID, "Default"
            );
            write(gfd, header, hlen);

            int dsrc = open(DEFAULT_CONFIG, O_RDONLY);
            char default_buf[65536];
            int dlen = 0;
            if (dsrc >= 0) {
                dlen = read(dsrc, default_buf, sizeof(default_buf) - 1);
                close(dsrc);
                if (dlen > 0) default_buf[dlen] = '\0';
            }
            if (dlen <= 0) {
                dlen = strlen(embedded_default);
                memcpy(default_buf, embedded_default, dlen);
            }
            write(gfd, default_buf, dlen);

            n = snprintf(line_buf, sizeof(line_buf), "--ps2-title-id=%s\n", disc_id);
            write(gfd, line_buf, n);

            close(gfd);
            log_debug("Created new gameconfig: %s", game_config_path);
        }
    }

    // Parse # Emulator: and write temp
    out_emulator_tid[0] = '\0';
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

                // Strip bare --image=, but keep --image-discN=
                int skip = 0;
                if (line_len > 0 && strncmp(p, "--image=", 8) == 0) {
                    if (line_len < 14 || strncmp(p, "--image-disc", 12) != 0) {
                        skip = 1;
                    }
                }

                if (!skip) {
                    write(fd, p, line_len);
                    write(fd, "\n", 1);

                    // Parse # Emulator: line
                    if (line_len > 12 && strncmp(p, "#  Emulator:", 12) == 0) {
                        char *tid_start = p + 12;
                        while (tid_start < line_end &&
                               (*tid_start == ' ' || *tid_start == '\t')) tid_start++;
                        int tid_len = line_end - tid_start;
                        if (tid_len > 0 && (size_t)tid_len < tid_size) {
                            strncpy(out_emulator_tid, tid_start, tid_len);
                            out_emulator_tid[tid_len] = '\0';
                        }
                    }
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
    close(fd);

    // Rewrite MASTER to point at TEMP
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
            // strip old --config=
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

    log_debug("WROTE MASTER CONFIG: %s (game config: %s, emulator: %s)",
              MASTER_CONFIG, game_config_path,
              out_emulator_tid[0] ? out_emulator_tid : EMULATOR_TID);
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

// ============ RAW SYSCALL HELPERS ============
static int ps4_load_prx(const char *path, int *mod_id) {
    return (int)syscall(594, path, 0, mod_id, 0);
}
static int ps4_dlsym(int mod_id, const char *symbol, void **addr) {
    return (int)syscall(591, (long)mod_id, symbol, addr);
}
// =============================================

// ============ LAUNCH ============
typedef struct {
    uint32_t sz;
    uint32_t user_id;
    uint32_t app_opt;
    uint32_t crash_report;
    uint32_t check_flag;
    uint32_t unk[2];
} LncAppParam;

#define SkipSystemUpdateCheck 0x20000

static void launch_emulator(const char *override_tid) {
    int userId = 0;
    int ret;
    int mod = -1;
    const char *tid = (override_tid && override_tid[0]) ? override_tid : EMULATOR_TID;

    log_debug("=== LAUNCHING EMULATOR ===");
    log_debug("EMULATOR_TID: %s", tid);

    // 1. Get the foreground user
    ret = sceUserServiceGetForegroundUser(&userId);
    if (ret < 0) {
        ret = sceUserServiceGetInitialUser(&userId);
        if (ret < 0) userId = 0;
    }
    log_debug("User ID: %d", userId);

    // 2. Load libSceLncUtil.sprx using raw syscall (more reliable than sceKernelLoadStartModule)
    const char *sprx_paths[] = {
        "/system/common/lib/libSceLncUtil.sprx",
        "/system/priv/lib/libSceLncUtil.sprx",
        "/system/lib/libSceLncUtil.sprx",
        NULL
    };

    for (int i = 0; sprx_paths[i] != NULL; i++) {
        mod = ps4_load_prx(sprx_paths[i], &mod);
        log_debug("ps4_load_prx(%s) = %d", sprx_paths[i], mod);
        if (mod >= 0) break;
    }

    if (mod < 0) {
        log_debug("LAUNCH FAILED: could not load libSceLncUtil.sprx");
        draw_text_scaled(80, 480, "LAUNCH FAILED: sprx not found", COLOR_RED, 2);
        draw_text_scaled(80, 520, "Check launcher_log.txt", COLOR_WHITE, 2);
        flip();
        sceKernelSleep(5);
        return;
    }

    // 3. Resolve sceLncUtilLaunchApp symbol using raw syscall
    void *launch_func = NULL;
    ret = ps4_dlsym(mod, "sceLncUtilLaunchApp", &launch_func);
    log_debug("ps4_dlsym(sceLncUtilLaunchApp) = 0x%08X, ptr = %p", ret, launch_func);

    if (ret != 0 || launch_func == NULL) {
        log_debug("LAUNCH FAILED: sceLncUtilLaunchApp symbol not found");
        draw_text_scaled(80, 480, "LAUNCH FAILED: symbol not found", COLOR_RED, 2);
        draw_text_scaled(80, 520, "Check launcher_log.txt", COLOR_WHITE, 2);
        flip();
        sceKernelSleep(5);
        return;
    }

    // 4. Prepare launch parameters (matching Itemzflow)
    LncAppParam param;
    memset(&param, 0, sizeof(param));
    param.sz = sizeof(LncAppParam);
    param.user_id = userId;
    param.app_opt = 0;
    param.crash_report = 0;
    param.check_flag = SkipSystemUpdateCheck;   // 0x20000

    typedef int (*LaunchApp_t)(const char *titleId, const char *args, void *param);
    LaunchApp_t sceLncUtilLaunchApp = (LaunchApp_t)launch_func;

    // 5. Launch the emulator
    log_debug("Calling sceLncUtilLaunchApp with TID: %s", tid);
    ret = sceLncUtilLaunchApp(tid, NULL, &param);
    log_debug("sceLncUtilLaunchApp returned: 0x%08X", ret);

    // 6. Check result
    if (ret == 0 || (unsigned int)ret == SCE_LNC_UTIL_ERROR_ALREADY_RUNNING) {
        log_debug("Launch OK, returning (launcher will be suspended)");
        sceKernelSleep(1);
        return;   // ← Do NOT call exit(0) – let the system suspend this app
    }

    // 7. Fallback: try sceSystemServiceLaunchApp (works without the sprx)
    log_debug("Falling back to sceSystemServiceLaunchApp");
    sceSystemServiceLaunchApp(tid, NULL, NULL);
    sceKernelSleep(1);
    return;   // ← Again, no exit
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

    load_good_names();
    log_debug("GOOD NAMES: %d loaded", good_name_count);

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
                log_debug("LAUNCH: %s", games[selected].display_name);
                char emu_tid[32] = {0};
                if (set_active_game(games[selected].path, games[selected].id,
                                    games[selected].name, emu_tid, sizeof(emu_tid))) {
                    launch_emulator(emu_tid);
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
            draw_text_scaled(80, y, games[i].display_name, color, 3);
        }

        draw_text_scaled(80, SCREEN_HEIGHT - 40, "[X] LAUNCH   [UP/DOWN] SELECT", COLOR_GRAY, 2);

        flip();
        sceKernelUsleep(16666);
    }

    return 0;
}
