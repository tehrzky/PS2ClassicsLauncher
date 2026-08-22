#include "settings.h"
#include "debug.h"
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

AppSettings g_settings;

void settings_reset(void) {
    g_settings.auto_download_covers = 0;      // OFF by default
    g_settings.auto_download_gameindex = 0;   // OFF by default
    g_settings.cover_type = 0;
    strncpy(g_settings.scraper_base_url,
            "https://raw.githubusercontent.com/xlenore/ps2-covers/main",
            sizeof(g_settings.scraper_base_url) - 1);
    g_settings.scraper_base_url[sizeof(g_settings.scraper_base_url) - 1] = '\0';
    g_settings.font_body = 0;
    g_settings.font_title = 0;
    strncpy(g_settings.work_path, "/data/PS4ROMS/PS2ISO", sizeof(g_settings.work_path) - 1);
    g_settings.work_path[sizeof(g_settings.work_path) - 1] = '\0';
    strncpy(g_settings.master_config, "config-emu-ex.txt", sizeof(g_settings.master_config) - 1);
    g_settings.master_config[sizeof(g_settings.master_config) - 1] = '\0';
    g_settings.wallpaper[0] = '\0';
}

void settings_load(void) {
    settings_reset();
    char config_dir[512], settings_path[512];
    snprintf(config_dir,   sizeof(config_dir),   "%s/config", g_settings.work_path);
    snprintf(settings_path, sizeof(settings_path), "%s/config/launcher_settings.txt", g_settings.work_path);
    mkdir(config_dir, 0777);
    FILE *fp = fopen(settings_path, "r");
    if (!fp) {
        settings_save();
        return;
    }
    char line[512];
    while (fgets(line, sizeof(line), fp)) {
        char *nl = strchr(line, '\n');
        if (nl) *nl = '\0';
        char *eq = strchr(line, '=');
        if (!eq) continue;
        *eq = '\0';
        char *key = line;
        char *val = eq + 1;
        if (strcmp(key, "auto_download_covers") == 0) g_settings.auto_download_covers = atoi(val);
        else if (strcmp(key, "auto_download_gameindex") == 0) g_settings.auto_download_gameindex = atoi(val);
        else if (strcmp(key, "cover_type") == 0) g_settings.cover_type = atoi(val);
        else if (strcmp(key, "scraper_base_url") == 0) {
            strncpy(g_settings.scraper_base_url, val, sizeof(g_settings.scraper_base_url) - 1);
            g_settings.scraper_base_url[sizeof(g_settings.scraper_base_url) - 1] = '\0';
        }
        else if (strcmp(key, "font_body") == 0) g_settings.font_body = atoi(val);
        else if (strcmp(key, "font_title") == 0) g_settings.font_title = atoi(val);
        else if (strcmp(key, "work_path") == 0) {
            strncpy(g_settings.work_path, val, sizeof(g_settings.work_path) - 1);
            g_settings.work_path[sizeof(g_settings.work_path) - 1] = '\0';
        }
        else if (strcmp(key, "master_config") == 0) {
            strncpy(g_settings.master_config, val, sizeof(g_settings.master_config) - 1);
            g_settings.master_config[sizeof(g_settings.master_config) - 1] = '\0';
        }
        else if (strcmp(key, "wallpaper") == 0) {
            strncpy(g_settings.wallpaper, val, sizeof(g_settings.wallpaper) - 1);
            g_settings.wallpaper[sizeof(g_settings.wallpaper) - 1] = '\0';
        }
    }
    fclose(fp);
}

void settings_save(void) {
    char config_dir[512], settings_path[512];
    snprintf(config_dir,   sizeof(config_dir),   "%s/config", g_settings.work_path);
    snprintf(settings_path, sizeof(settings_path), "%s/config/launcher_settings.txt", g_settings.work_path);
    mkdir(config_dir, 0777);
    FILE *fp = fopen(settings_path, "w");
    if (!fp) return;
    fprintf(fp, "auto_download_covers=%d\n", g_settings.auto_download_covers);
    fprintf(fp, "auto_download_gameindex=%d\n", g_settings.auto_download_gameindex);
    fprintf(fp, "cover_type=%d\n", g_settings.cover_type);
    fprintf(fp, "scraper_base_url=%s\n", g_settings.scraper_base_url);
    fprintf(fp, "font_body=%d\n", g_settings.font_body);
    fprintf(fp, "font_title=%d\n", g_settings.font_title);
    fprintf(fp, "work_path=%s\n", g_settings.work_path);
    fprintf(fp, "master_config=%s\n", g_settings.master_config);
    fprintf(fp, "wallpaper=%s\n", g_settings.wallpaper);
    fclose(fp);
}

void settings_get_path(char *out, size_t out_len, const char *subpath) {
    if (subpath && subpath[0])
        snprintf(out, out_len, "%s/%s", g_settings.work_path, subpath);
    else
        strncpy(out, g_settings.work_path, out_len);
}

void settings_get_config_path(char *out, size_t out_len, const char *filename) {
    snprintf(out, out_len, "%s/config/%s", g_settings.work_path, filename);
}
