#ifndef CONFIG_H
#define CONFIG_H

#include <stddef.h>

#define EMULATOR_TID "PCSX20042"

int set_active_game(const char *iso_path, const char *disc_id,
                    const char *game_name, char *out_emulator_tid, size_t tid_size);
int config_get_game_emulator_info(const char *disc_id, const char *game_name,
                                   char *out_emu_name, size_t name_size,
                                   char *out_emu_id, size_t id_size);
#endif
