#include "config.h"
#include "debug.h"
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <dirent.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>

#define MASTER_CONFIG   "/data/PS4ROMS/PS2ISO/config/config-emu-ex.txt"
#define GAMECONFIG_DIR  "/data/PS4ROMS/PS2ISO/gameconfig/"
#define DEFAULT_CONFIG  "/data/PS4ROMS/PS2ISO/config/default.txt"
#define TEMP_CONFIG     "/data/PS4ROMS/PS2ISO/config/.launcher_temp.txt"

extern const char *embedded_default;

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

int set_active_game(const char *iso_path, const char *disc_id,
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

                int skip = 0;
                if (line_len > 0 && strncmp(p, "--image=", 8) == 0) {
                    if (line_len < 14 || strncmp(p, "--image-disc", 12) != 0) {
                        skip = 1;
                    }
                }

                if (!skip) {
                    write(fd, p, line_len);
                    write(fd, "\n", 1);

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

    log_debug("WROTE MASTER CONFIG: %s (game config: %s, emulator: %s)",
              MASTER_CONFIG, game_config_path,
              out_emulator_tid[0] ? out_emulator_tid : EMULATOR_TID);
    return 1;
}