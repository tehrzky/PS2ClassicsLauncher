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

    /* Dropdown overlay */
    int dropdown_active;     /* 1 = dropdown menu is open */
    int dropdown_sel;        /* selected option in dropdown */
    int dropdown_scroll;     /* scroll offset in dropdown */

    /* Hold-to-fast-scrub state for LEFT/RIGHT on a slider */
    int lr_hold_dir;         /* -1, 0, +1 : direction currently being held */
    int lr_hold_frames;      /* how many frames LEFT/RIGHT has been held */

    /* Result preview / text editor overlay (see pgsettings_textview.c) */
    int textview_active;
    int textview_scroll;
    int textview_edit_mode;  /* 0 = read-only preview, 1 = editing */
    int textview_cursor;     /* character offset into the buffer, edit mode only */
    char textview_buf[8192];

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

#endif
