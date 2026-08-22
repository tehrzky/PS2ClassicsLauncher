#ifndef SCRAPER_H
#define SCRAPER_H

extern char g_download_status[128];
extern int  g_download_active;

void scraper_download_cover(const char *serial);
void scraper_force_download_cover(const char *serial);
void scraper_download_gameindex(void);
void scraper_force_download_gameindex(void);

#endif
