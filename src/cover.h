#ifndef COVER_H
#define COVER_H

void cover_load(const char *serial);
void cover_draw_fit(int x, int y, int box_w, int box_h, const char *serial);
void cover_draw_wallpaper(void);
void cover_free_wallpaper(void);
void cover_cleanup(void);

#endif
