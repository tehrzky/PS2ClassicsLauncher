#ifndef SETTINGS_H
#define SETTINGS_H

#include <stddef.h>

typedef struct {
    int auto_download_covers;
    int auto_download_gameindex;
    int cover_type;
    int font_body;
    int font_title;
    char scraper_base_url[256];
    char work_path[256];
    char master_config[64];
    char wallpaper[64];
} AppSettings;

extern AppSettings g_settings;

void settings_load(void);
void settings_save(void);
void settings_reset(void);
void settings_get_path(char *out, size_t out_len, const char *subpath);
void settings_get_config_path(char *out, size_t out_len, const char *filename);

#endif
