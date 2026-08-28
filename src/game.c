#include "game.h"
#include "debug.h"
#include "goodnames.h"
#include "config.h"
#include "settings.h"
#include <dirent.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdint.h>
#include <stdio.h>

Game games[256];
int game_count = 0;
int selected = 0;

static uint32_t read_le32(const unsigned char *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static int extract_disc_id(const char *path, char *out_id, size_t out_len) {
    int fd = open(path, O_RDONLY);
    if (fd < 0) return 0;

    unsigned char sector[2048];

    if (lseek(fd, (off_t)16 * 2048, SEEK_SET) < 0) { close(fd); return 0; }
    if (read(fd, sector, 2048) != 2048) { close(fd); return 0; }
    if (sector[0] != 1 || memcmp(sector + 1, "CD001", 5) != 0) { close(fd); return 0; }

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
        else if (raw[r] == '.') { }
        else out_id[fi++] = raw[r];
    }
    out_id[fi] = '\0';
    return fi > 0;
}

static int name_compare(const void *a, const void *b) {
    return strcasecmp(((const Game*)a)->display_name, ((const Game*)b)->display_name);
}

void scan_games(void) {
    char iso_dir[512];
    snprintf(iso_dir, sizeof(iso_dir), "%s/", g_settings.work_path);
    DIR *dir = opendir(iso_dir);
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
                 "%s%s", iso_dir, entry->d_name);

        if (!extract_disc_id(games[game_count].path, games[game_count].id, sizeof(games[game_count].id))) {
            strncpy(games[game_count].id, "UNKNOWN", sizeof(games[game_count].id) - 1);
            log_debug("DISC ID extraction FAILED for: %s", games[game_count].name);
        }

        build_display_name(entry->d_name, games[game_count].id,
                       games[game_count].display_name,
                       sizeof(games[game_count].display_name));

        config_get_game_emulator_info(games[game_count].id, games[game_count].name,
                                   games[game_count].emulator_name,
                                   sizeof(games[game_count].emulator_name),
                                   games[game_count].emulator_id,
                                   sizeof(games[game_count].emulator_id));

        // Pull GameDB + LaunchBox metadata
        GameDBInfo info;
        if (scraper_get_game_info(games[game_count].id, games[game_count].display_name, &info) == 0) {
            strncpy(games[game_count].description, info.description, sizeof(games[game_count].description) - 1);
            strncpy(games[game_count].developer, info.developer, sizeof(games[game_count].developer) - 1);
            strncpy(games[game_count].publisher, info.publisher, sizeof(games[game_count].publisher) - 1);
            strncpy(games[game_count].genre, info.genre, sizeof(games[game_count].genre) - 1);
            strncpy(games[game_count].release_date, info.release_date, sizeof(games[game_count].release_date) - 1);
        }

        game_count++;
    }
    closedir(dir);
    qsort(games, game_count, sizeof(Game), name_compare);
}
