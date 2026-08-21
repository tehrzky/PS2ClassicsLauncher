#include "settings.h"
#include "debug.h"
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

#define SETTINGS_PATH "/data/PS4ROMS/PS2ISO/config/launcher_settings.txt"

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
    mkdir("/data/PS4ROMS/PS2ISO/config", 0777);
    FILE *fp = fopen(SETTINGS_PATH, "r");
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
    }
    fclose(fp);
}

void settings_save(void) {
    mkdir("/data/PS4ROMS/PS2ISO/config", 0777);
    FILE *fp = fopen(SETTINGS_PATH, "w");
    if (!fp) return;
    fprintf(fp, "auto_download_covers=%d\n", g_settings.auto_download_covers);
    fprintf(fp, "auto_download_gameindex=%d\n", g_settings.auto_download_gameindex);
    fprintf(fp, "cover_type=%d\n", g_settings.cover_type);
    fprintf(fp, "scraper_base_url=%s\n", g_settings.scraper_base_url);
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
