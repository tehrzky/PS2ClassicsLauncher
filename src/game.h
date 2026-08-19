#ifndef GAME_H
#define GAME_H

#include <stddef.h>

typedef struct {
    char path[512];
    char name[256];
    char display_name[256];
    char id[32];
    char emulator_name[64];
    char emulator_id[32];
} Game;

extern Game games[256];
extern int game_count;
extern int selected;

void scan_games(void);

#endif
