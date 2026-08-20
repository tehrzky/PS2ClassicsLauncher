#ifndef SCRAPER_H
#define SCRAPER_H

void scraper_download_cover(const char *serial);
void scraper_download_gameindex(void);
void scraper_force_download_cover(const char *serial);
void scraper_force_download_gameindex(void);

#endif
