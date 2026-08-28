#ifndef SCRAPER_H
#define SCRAPER_H

#include <stddef.h>

extern char g_download_status[128];
extern int  g_download_active;

typedef struct {
    char title[256];
    char developer[128];
    char publisher[128];
    char genre[128];
    char release_date[32];
    char description[1024];
} GameDBInfo;

void scraper_init(void);
int scraper_get_game_info(const char *serial, const char *fallback_title, GameDBInfo *out);

void scraper_download_cover(const char *serial);
void scraper_force_download_cover(const char *serial);
void scraper_download_gameindex(void);
void scraper_force_download_gameindex(void);

#endif
