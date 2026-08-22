#include "config.h"
#include "debug.h"
#include "settings.h"
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <dirent.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>
#include <stdio.h>

static int extract_header_field(const char *line_start, const char *line_end,
                                 const char *field_name,
                                 char *out, size_t out_size) {
    const char *p = line_start;
    if (p < line_end && *p == '#') p++;
    while (p < line_end && (*p == ' ' || *p == '\t')) p++;
    int name_len = (int)strlen(field_name);
    if (p + name_len > line_end) return 0;
    if (strncasecmp(p, field_name, name_len) != 0) return 0;
    // Ensure "Emulator" does NOT match inside "Emulator Title"
    if (p + name_len < line_end) {
        char c = *(p + name_len);
        if (c != ' ' && c != '\t' && c != ':') return 0;
    }
    p += name_len;
    while (p < line_end && (*p == ' ' || *p == '\t' || *p == ':')) p++;
    int val_len = (int)(line_end - p);
    if (val_len <= 0) return 0;
    if ((size_t)val_len >= out_size) val_len = out_size - 1;
    strncpy(out, p, val_len);
    out[val_len] = '\0';
    while (val_len > 0 && (out[val_len - 1] == ' ' || out[val_len - 1] == '\t')) {
        out[val_len - 1] = '\0'; val_len--;
    }
    return 1;
}

extern const char *embedded_default;

static int find_config_by_disc_id(const char *disc_id, char *out_path, size_t out_size) {
    if (!disc_id || disc_id[0] == '\0' || strcasecmp(disc_id, "UNKNOWN") == 0)
        return 0;

    char gameconfig_dir[512];
    snprintf(gameconfig_dir, sizeof(gameconfig_dir), "%s/gameconfig/", g_settings.work_path);
    DIR *dir = opendir(gameconfig_dir);
    if (!dir) return 0;

    struct dirent *entry;
    char best_path[700] = {0};
    time_t best_mtime = 0;

    while ((entry = readdir(dir)) != NULL) {
        int len = strlen(entry->d_name);
        if (len < 5) continue;
        if (strcasecmp(entry->d_name + len - 4, ".txt") != 0) continue;

        char path[700];
        snprintf(path, sizeof(path), "%s%s", gameconfig_dir, entry->d_name);

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
        char search3[64];
        snprintf(search3, sizeof(search3), "--ps2-title-id=%s", disc_id);

        if (strstr(buf, search) != NULL || strstr(buf, search2) != NULL || strstr(buf, search3) != NULL) {
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

static int find_config_by_disc_id_filename(const char *disc_id, char *out_path, size_t out_size) {
    if (!disc_id || disc_id[0] == '\0' || strcasecmp(disc_id, "UNKNOWN") == 0)
        return 0;

    char gameconfig_dir[512];
    snprintf(gameconfig_dir, sizeof(gameconfig_dir), "%s/gameconfig/", g_settings.work_path);
    DIR *dir = opendir(gameconfig_dir);
    if (!dir) return 0;

    struct dirent *entry;
    char best_path[700] = {0};
    time_t best_mtime = 0;

    while ((entry = readdir(dir)) != NULL) {
        int len = strlen(entry->d_name);
        if (len < 5) continue;
        if (strcasecmp(entry->d_name + len - 4, ".txt") != 0) continue;

        char basename[256];
        int base_len = len - 4;
        if (base_len >= (int)sizeof(basename)) base_len = sizeof(basename) - 1;
        strncpy(basename, entry->d_name, base_len);
        basename[base_len] = '\0';

        int match = 0;
        if (strcasecmp(basename, disc_id) == 0) match = 1;
        else {
            int dlen = strlen(disc_id);
            int blen = strlen(basename);
            for (int i = 0; i <= blen - dlen; i++) {
                if (strncasecmp(basename + i, disc_id, dlen) == 0) { match = 1; break; }
            }
        }

        if (match) {
            char path[700];
            snprintf(path, sizeof(path), "%s%s", gameconfig_dir, entry->d_name);
            struct stat st;
            if (stat(path, &st) == 0) {
                if (st.st_mtime > best_mtime) {
                    strncpy(best_path, path, sizeof(best_path) - 1);
                    best_mtime = st.st_mtime;
                }
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
    char gameconfig_dir[512];
    snprintf(gameconfig_dir, sizeof(gameconfig_dir), "%s/gameconfig/", g_settings.work_path);
    DIR *dir = opendir(gameconfig_dir);
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
            snprintf(out_path, out_size, "%s%s", gameconfig_dir, entry->d_name);
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

    char config_dir[512], gameconfig_dir[512], default_config[512], temp_config[512], master_config[512];
    snprintf(config_dir,     sizeof(config_dir),     "%s/config", g_settings.work_path);
    snprintf(gameconfig_dir, sizeof(gameconfig_dir), "%s/gameconfig/", g_settings.work_path);
    snprintf(default_config, sizeof(default_config), "%s/config/default.txt", g_settings.work_path);
    snprintf(temp_config,    sizeof(temp_config),    "%s/config/.launcher_temp.txt", g_settings.work_path);
    snprintf(master_config,  sizeof(master_config),  "%s/config/config-emu-ex.txt", g_settings.work_path);
    mkdir(config_dir, 0777);
    mkdir(gameconfig_dir, 0777);

    char game_config_path[700];
    int found = find_config_by_disc_id(disc_id, game_config_path, sizeof(game_config_path));
    if (!found) {
        found = find_config_by_disc_id_filename(disc_id, game_config_path, sizeof(game_config_path));
    }
    if (!found) {
        found = find_config_by_filename(game_name, game_config_path, sizeof(game_config_path));
    }

    if (!found) {
        snprintf(game_config_path, sizeof(game_config_path), "%s%s.txt", gameconfig_dir, game_name);
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

            int dsrc = open(default_config, O_RDONLY);
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

            n = snprintf(line_buf, sizeof(line_buf), "--image=\"%s\"\n", iso_path);
            write(gfd, line_buf, n);

            close(gfd);
            log_debug("Created new gameconfig: %s", game_config_path);
        }
    }

    out_emulator_tid[0] = '\0';
    int fd = open(temp_config, O_WRONLY | O_CREAT | O_TRUNC, 0777);
    if (fd < 0) {
        log_debug("set_active_game: failed to open %s", temp_config);
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

                    char tid_buf[32] = {0};
                    if (extract_header_field(p, line_end, "Emulator", tid_buf, sizeof(tid_buf))) {
                        if (tid_buf[0] && strlen(tid_buf) < tid_size) {
                            strncpy(out_emulator_tid, tid_buf, tid_size - 1);
                            out_emulator_tid[tid_size - 1] = '\0';
                        }
                    }

                    char name_buf[64] = {0};
                    if (extract_header_field(p, line_end, "Emulator Title", name_buf, sizeof(name_buf))) {
                        log_debug("Emulator Title: %s", name_buf);
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

    fd = open(master_config, O_RDONLY);
    if (fd < 0) {
        log_debug("set_active_game: failed to open %s", master_config);
        return 0;
    }
    char buf[32768];
    int m = read(fd, buf, sizeof(buf) - 1);
    close(fd);
    if (m <= 0) {
        log_debug("set_active_game: %s empty or unreadable", master_config);
        return 0;
    }
    buf[m] = '\0';

    fd = open(master_config, O_WRONLY | O_CREAT | O_TRUNC, 0777);
    if (fd < 0) {
        log_debug("set_active_game: failed to rewrite %s", master_config);
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

    n = snprintf(line_buf, sizeof(line_buf), "--config=\"%s\"\n", temp_config);
    write(fd, line_buf, n);
    close(fd);

    log_debug("WROTE MASTER CONFIG: %s (game config: %s, emulator: %s)",
              master_config, game_config_path,
              out_emulator_tid[0] ? out_emulator_tid : EMULATOR_TID);
    return 1;
}

int config_get_game_emulator_info(const char *disc_id, const char *game_name,
                                   char *out_emu_name, size_t name_size,
                                   char *out_emu_id, size_t id_size) {
    char path[700];
    int found = find_config_by_disc_id(disc_id, path, sizeof(path));
    if (!found) found = find_config_by_disc_id_filename(disc_id, path, sizeof(path));
    if (!found) found = find_config_by_filename(game_name, path, sizeof(path));

    out_emu_name[0] = '\0';
    out_emu_id[0] = '\0';
    if (!found) return 0;

    int fd = open(path, O_RDONLY);
    if (fd < 0) return 0;

    char buf[65536];
    int n = read(fd, buf, sizeof(buf) - 1);
    close(fd);
    if (n <= 0) return 0;
    buf[n] = '\0';

    char *p = buf;
    while (*p) {
        char *line_end = p;
        while (*line_end && *line_end != '\n') line_end++;
        int line_len = line_end - p;

        extract_header_field(p, line_end, "Emulator", out_emu_id, id_size);
        extract_header_field(p, line_end, "Emulator Title", out_emu_name, name_size);
        p = line_end;
        if (*p == '\n') p++;
    }
    return 1;
}
