#ifndef LOCAL_UPLOAD_SERVER_H
#define LOCAL_UPLOAD_SERVER_H

#include <stddef.h>

/* A tiny HTTP/1.1 server that runs entirely on the PS4, on your LAN only.
 * Nothing is hosted anywhere else -- this is the "shortcut for FTP" the
 * dev asked for: a phone or PC on the same network opens a page served
 * directly by the console, uploads a .lua patch, and it's written
 * straight into the patches folder. No internet, no accounts, no tokens.
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

#endif
