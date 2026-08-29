#ifndef PGSETTINGS_H
#define PGSETTINGS_H

#include "schema.h"

#define PGSETTINGS_MAX_VALUES 512
#define PGSETTINGS_MAX_VALUE_LEN 256
#define PGSETTINGS_MAX_DISC_ID_LEN 32

typedef struct {
    char field_id[SCHEMA_MAX_FIELD_ID_LEN];
    char value_str[PGSETTINGS_MAX_VALUE_LEN];
    int value_int;      /* for bool / int values */
    int is_set;         /* 1 = loaded from JSON, 0 = schema default */
} SettingValue;

typedef struct {
    char disc_id[PGSETTINGS_MAX_DISC_ID_LEN];
    char version[16];
    SettingValue values[PGSETTINGS_MAX_VALUES];
    int value_count;
} GameSettings;

/* Load per-game settings: schema defaults merged with saved JSON overrides.
 * Returns 0 on success (even if no JSON file exists — falls back to defaults). */
int pgsettings_load(const char *disc_id, const Schema *schema, GameSettings *out);

/* Save only the values that differ from schema defaults. */
int pgsettings_save(const char *disc_id, const GameSettings *settings, const Schema *schema);

/* Reset all values to schema defaults (clears is_set flags). */
void pgsettings_reset(const char *disc_id, const Schema *schema, GameSettings *out);

/* Generate emulator command lines from current settings.
 * Writes newline-separated commands into out_buf.
 * Returns number of bytes written, or -1 on error. */
int pgsettings_generate_commands(const GameSettings *settings, const Schema *schema,
                                    char *out_buf, size_t out_size);

/* Getters / setters */
const char *pgsettings_get_str(const GameSettings *gs, const char *field_id);
int pgsettings_get_int(const GameSettings *gs, const char *field_id);
void pgsettings_set_str(GameSettings *gs, const char *field_id, const char *value);
void pgsettings_set_int(GameSettings *gs, const char *field_id, int value);

/* Check if a value differs from its schema default. */
int pgsettings_is_modified(const GameSettings *gs, const Schema *schema, const char *field_id);

/* Check if any value in the entire settings object is modified. */
int pgsettings_any_modified(const GameSettings *gs, const Schema *schema);

#endif
