#ifndef UI_H
#define UI_H

#include <stdint.h>

#define SETTINGS_ITEMS 10

void draw_launcher_ui(int game_count_visible, int selected_idx, int total_games);
void draw_settings_ui(int selected_item, int in_per_game);

#endif
