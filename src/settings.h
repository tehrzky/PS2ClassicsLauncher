#ifndef SETTINGS_H
#define SETTINGS_H

typedef struct {
    int auto_download_covers;      // 0=manual, 1=auto
    int auto_download_gameindex;   // 0=manual, 1=auto
    int cover_type;                // 0=default(2D), 1=3D
    char scraper_base_url[256];
} AppSettings;

extern AppSettings g_settings;

void settings_load(void);
void settings_save(void);
void settings_reset(void);

#endif
