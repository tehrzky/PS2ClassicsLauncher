#ifndef PGSETTINGS_TEXTVIEW_H
#define PGSETTINGS_TEXTVIEW_H

#include "schema.h"
#include "pgsettings.h"
#include "pgsettings_ui.h"

/* "Check the result" overlay for the per-game settings screen.
 *
 * Opens as a read-only scrollable dump of exactly what
 * pgsettings_generate_commands() would write out for the game's *current*
 * (possibly unsaved) settings -- i.e. what the emulator will actually be
 * launched with. That's the fast, always-available part devs asked for.
 *
 * Pressing CROSS switches to edit mode, which hands text entry to the
 * system on-screen keyboard (sceImeDialog) rather than a hand-rolled
 * virtual keyboard -- see the .c file for why, and for the two lines you
 * need to wire up once the Orbis IME headers are added to the project.
 */

void pgsettings_textview_open(PGSettingsUIState *st, const Schema *schema, const GameSettings *settings);

/* Returns 1 if input was consumed by the textview (caller should not also
 * feed it to pgsettings_ui_handle_input). */
int pgsettings_textview_handle_input(unsigned int pressed, unsigned int held, PGSettingsUIState *st);

void draw_pgsettings_textview(PGSettingsUIState *st);

#endif
