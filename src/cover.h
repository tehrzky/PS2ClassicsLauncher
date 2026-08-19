#ifndef COVER_H
#define COVER_H

#include <stdint.h>

// Load a cover for a given serial/disc ID
void cover_load(const char *serial);

// Draw a cover at position (x, y) with size (w, h) for a given serial
void cover_draw(int x, int y, int w, int h, const char *serial);

// Free cover texture memory (call when exiting)
void cover_cleanup(void);

#endif
