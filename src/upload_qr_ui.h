#ifndef UPLOAD_QR_UI_H
#define UPLOAD_QR_UI_H

/* Screen shown from the per-game settings UI: renders a QR code for the
 * local upload server's URL plus the address as plain text underneath, so
 * either scanning or manual entry works. All local -- see
 * local_upload_server.h. */

void upload_qr_ui_open(void);
int  upload_qr_ui_is_open(void);

/* Returns 1 if input was consumed. */
int upload_qr_ui_handle_input(unsigned int pressed);

void draw_upload_qr_ui(void);

#endif
