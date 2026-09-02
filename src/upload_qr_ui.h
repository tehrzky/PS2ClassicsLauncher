#ifndef UPLOAD_QR_UI_H
#define UPLOAD_QR_UI_H

/* Screen shown from the per-game settings UI: renders a QR code for the
 * local upload server's URL plus the address as plain text underneath, so
 * either scanning or manual entry works. All local -- see
 * local_upload_server.h. */

/* Screen shown from the per-game settings UI: renders a QR code for the
 * local upload server's URL plus the address as plain text underneath, so
 * either scanning or manual entry works. All local -- see
 * local_upload_server.h.
 *
 * display_name/disc_id/config_name/iso_path are forwarded to
 * local_upload_server_set_game_context() so the page that opens knows
 * which game it's managing files for (auto-filled filenames, CLI
 * template). config_name is the ISO basename (matches config.c's
 * gameconfig/<config_name>.txt convention), iso_path is the full ISO path. */
void upload_qr_ui_open(const char *display_name, const char *disc_id,
                       const char *config_name, const char *iso_path);
int  upload_qr_ui_is_open(void);

/* Returns 1 if input was consumed. */
int upload_qr_ui_handle_input(unsigned int pressed);

void draw_upload_qr_ui(void);

#endif
