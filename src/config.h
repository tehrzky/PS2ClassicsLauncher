#ifndef CONFIG_H
#define CONFIG_H

#include <stddef.h>

#define EMULATOR_TID "PCSX20042"

int set_active_game(const char *iso_path, const char *disc_id,
                    const char *game_name, char *out_emulator_tid, size_t tid_size);

#endif