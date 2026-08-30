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

// Core functions
void scraper_init(void);
int scraper_get_game_info(const char *serial, const char *fallback_title, GameDBInfo *out);

void scraper_download_cover(const char *serial);
void scraper_force_download_cover(const char *serial);  // kept for single use
void scraper_download_gameindex(void);
void scraper_force_download_gameindex(void);

// Background download system
void scraper_start_background_downloads(void);
void scraper_queue_cover_download(const char *serial);  // queues only if missing
void scraper_stop_background_downloads(void);
int scraper_is_cover_downloading(const char *serial);

void scraper_cleanup(void);

#endif
