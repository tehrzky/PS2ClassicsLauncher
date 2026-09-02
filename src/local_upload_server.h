#ifndef LOCAL_UPLOAD_SERVER_H
#define LOCAL_UPLOAD_SERVER_H

#include <stddef.h>

/* A tiny HTTP/1.1 server that runs entirely on the PS4, on your LAN only.
 * Nothing is hosted anywhere else -- this is the "shortcut for FTP" the
 * dev asked for: a phone or PC on the same network opens a page served
 * directly by the console, and can upload/browse/edit either a Lua patch
 * (goes in patches/) or a CLI gameconfig .txt (goes in gameconfig/,
 * auto-templated from config/default.txt if one doesn't exist yet). No
 * internet, no accounts, no tokens -- writes land straight on this
 * console's filesystem.
 *
 * Uses plain BSD sockets (matches the <netdb.h>/<arpa/inet.h> style
 * already used in scraper.c), single accept()-loop thread -- this is a
 * low-traffic local tool, not something that needs to handle concurrent
 * connections.
 */

#define LOCAL_UPLOAD_SERVER_PORT 8080

/* Starts the background server thread. Safe to call multiple times --
 * a second call while already running is a no-op. Returns 0 on success. */
int local_upload_server_start(void);

/* Stops the server (optional -- e.g. on app shutdown). */
void local_upload_server_stop(void);

int local_upload_server_is_running(void);

/* "http://<lan-ip>:8080/" for the QR code. If the IP can't be read back
 * from sceNetCtl (name may need adjusting for your exact SDK headers --
 * see the NOTE in the .c file), this falls back to a placeholder and the
 * UI should rely on showing the raw IP the PS4's own network settings
 * screen reports, which is why the manual-address fallback exists at all. */
void local_upload_server_get_url(char *out, size_t out_len);

/* Tells the server which game the landing page / auto-filled filenames /
 * auto-generated CLI template are for. Call this right before opening the
 * QR screen from the per-game settings UI (once per game, cheap). All
 * strings are copied in, safe to pass locals. config_name is the ISO
 * basename used to key gameconfig/<config_name>.txt (matches config.c's
 * own convention), iso_path is the full path written into the generated
 * template's --image= line. */
void local_upload_server_set_game_context(const char *display_name,
                                           const char *disc_id,
                                           const char *config_name,
                                           const char *iso_path);

#endif
