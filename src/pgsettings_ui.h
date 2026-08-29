#ifndef PGSETTINGS_UI_H
#define PGSETTINGS_UI_H

#include "schema.h"
#include "pgsettings.h"

/* UI state for per-game settings */
typedef struct {
    int active;              /* 1 = UI is open */
    int selected_tab;
    int selected_field;      /* index within current tab */
    int scroll_offset;       /* first visible field in current tab */
    int dirty;               /* 1 = unsaved changes */
    int show_confirm;        /* 1 = showing save/discard dialog */
    int confirm_sel;         /* 0=Save, 1=Discard, 2=Cancel */
    char game_name[256];
    char disc_id[32];
} PGSettingsUIState;

/* Initialize state (call when opening UI) */
void pgsettings_ui_init(PGSettingsUIState *st, const char *game_name, const char *disc_id);

/* Handle controller input. Returns 1 if input was consumed. */
int pgsettings_ui_handle_input(unsigned int pressed, unsigned int held,
                                const Schema *schema, GameSettings *settings,
                                PGSettingsUIState *st);

/* Draw the per-game settings UI. */
void draw_pgsettings_ui(const Schema *schema, GameSettings *settings,
                        PGSettingsUIState *st);

/* Get path for saving/loading per-game settings JSON */
void pgsettings_ui_get_path(const char *disc_id, char *out, size_t out_len);

#endif
