#include "local_upload_server.h"
#include "settings.h"
#include "debug.h"
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <strings.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/stat.h>
#include <orbis/Net.h>
#include <orbis/NetCtl.h>
#include <orbis/Sysmodule.h>

static int s_running = 0;
static int s_listen_fd = -1;
static pthread_t s_thread;
static char s_ip_cache[64] = "0.0.0.0";

/* ---------- Network bring-up ----------
 * scraper.c already does the equivalent of this for outbound HTTP. If you
 * end up wiring both into the same build, consider factoring this into one
 * shared net_init() so sceNetInit()/sceNetCtlInit() are only ever called
 * once -- most SDKs tolerate a second call, but no need to rely on that. */
static int s_net_inited = 0;
static char *strcasestr_local(const char *haystack, const char *needle);
static int ensure_net_init(void) {
    if (s_net_inited) return 0;
    sceSysmoduleLoadModuleInternal(ORBIS_SYSMODULE_INTERNAL_NET);
    sceSysmoduleLoadModuleInternal(ORBIS_SYSMODULE_INTERNAL_NETCTL);
    if (sceNetInit(NULL, 1024 * 1024) < 0) return -1;
    if (sceNetCtlInit() < 0) return -1;
    s_net_inited = 1;
    return 0;
}

/* NOTE: the exact enum/struct names for reading the console's own IP vary
 * a bit between OpenOrbis header versions. This is the common shape
 * (sceNetCtlGetInfo(ORBIS_NET_CTL_INFO_IP_ADDRESS, &info) filling
 * info.ip_address as a dotted-decimal string) -- if it doesn't match your
 * orbis/NetCtl.h, adjust this one function; everything else is unaffected,
 * and the UI always shows the manual IP:port text too so this is a
 * convenience, not a hard dependency. */
static void refresh_ip_cache(void) {
#ifdef ORBIS_NET_CTL_INFO_IP_ADDRESS
    OrbisNetCtlInfo info;
    if (sceNetCtlGetInfo(ORBIS_NET_CTL_INFO_IP_ADDRESS, &info) == 0) {
        strncpy(s_ip_cache, info.ip_address, sizeof(s_ip_cache) - 1);
        s_ip_cache[sizeof(s_ip_cache) - 1] = '\0';
    }
#endif
}

void local_upload_server_get_url(char *out, size_t out_len) {
    if (!out || out_len == 0) return;
    snprintf(out, out_len, "http://%s:%d/", s_ip_cache, LOCAL_UPLOAD_SERVER_PORT);
}

/* ---------- The one page this server serves ----------
 * Self-contained: no external CSS/JS/fonts, nothing fetched from the
 * internet. Works from any phone or PC browser on the same LAN. */
static const char *UPLOAD_PAGE_HTML =
"<!doctype html><html><head><meta charset=utf-8>"
"<meta name=viewport content=\"width=device-width,initial-scale=1\">"
"<title>PS4 Patch Upload</title>"
"<style>body{font-family:sans-serif;max-width:640px;margin:24px auto;padding:0 16px;background:#0b141f;color:#eee}"
"textarea{width:100%;box-sizing:border-box;height:260px;font-family:monospace;font-size:13px}"
"input[type=text]{width:100%;box-sizing:border-box;padding:8px;margin:8px 0}"
"button{padding:10px 18px;font-size:16px}"
"#status{margin-top:12px;font-weight:bold}</style></head><body>"
"<h2>Upload a .lua patch to this PS4</h2>"
"<p>Pick a file, or paste the patch text below, name it, and Save. It goes "
"straight into this console's patches folder -- nothing leaves your network.</p>"
"<input type=file id=f accept=\".lua,.txt\">"
"<textarea id=t placeholder=\"...or paste the Lua patch here\"></textarea>"
"<input type=text id=name placeholder=\"filename.lua\">"
"<button onclick=doSave()>Save to PS4</button>"
"<div id=status></div>"
"<script>"
"document.getElementById('f').addEventListener('change', function(e){"
"  var file = e.target.files[0]; if(!file) return;"
"  document.getElementById('name').value = file.name;"
"  var r = new FileReader();"
"  r.onload = function(){ document.getElementById('t').value = r.result; };"
"  r.readAsText(file);"
"});"
"function doSave(){"
"  var name = document.getElementById('name').value.trim();"
"  var body = document.getElementById('t').value;"
"  if(!name){ document.getElementById('status').textContent='Enter a filename first.'; return; }"
"  fetch('/upload/save?name=' + encodeURIComponent(name), {method:'POST', body: body})"
"    .then(function(r){ return r.text().then(function(t){ return {ok:r.ok, t:t}; }); })"
"    .then(function(res){ document.getElementById('status').textContent = res.t; })"
"    .catch(function(err){ document.getElementById('status').textContent = 'Failed: ' + err; });"
"}"
"</script></body></html>";

/* ---------- tiny helpers ---------- */

/* Keep only characters safe for a filename, force a .lua extension, and
 * refuse anything that tries to escape the patches directory. This server
 * has no auth (it's LAN-only by design, like the existing FTP workflow),
 * so this sanitization is the one thing standing between "upload a patch"
 * and "write anywhere on the filesystem" -- don't relax it. */
static void sanitize_lua_filename(const char *in, char *out, size_t out_len) {
    size_t oi = 0;
    for (size_t i = 0; in[i] && oi < out_len - 6; i++) {
        char c = in[i];
        if (isalnum((unsigned char)c) || c == '_' || c == '-' || c == '.') {
            if (c == '.' && (in[i+1] == '.')) continue; /* no ".." */
            out[oi++] = c;
        }
    }
    out[oi] = '\0';
    if (oi == 0) strncpy(out, "patch", out_len - 1);
    size_t len = strlen(out);
    if (len < 4 || strcasecmp(out + len - 4, ".lua") != 0) {
        strncat(out, ".lua", out_len - strlen(out) - 1);
    }
}

static void url_decode(const char *in, char *out, size_t out_len) {
    size_t oi = 0;
    for (size_t i = 0; in[i] && oi < out_len - 1; i++) {
        if (in[i] == '%' && isxdigit((unsigned char)in[i+1]) && isxdigit((unsigned char)in[i+2])) {
            char hex[3] = { in[i+1], in[i+2], 0 };
            out[oi++] = (char)strtol(hex, NULL, 16);
            i += 2;
        } else if (in[i] == '+') {
            out[oi++] = ' ';
        } else {
            out[oi++] = in[i];
        }
    }
    out[oi] = '\0';
}

static void send_all(int fd, const char *data, size_t len) {
    size_t sent = 0;
    while (sent < len) {
        ssize_t n = send(fd, data + sent, len - sent, 0);
        if (n <= 0) return;
        sent += (size_t)n;
    }
}

static void respond(int fd, int code, const char *status, const char *content_type, const char *body) {
    char header[256];
    size_t body_len = body ? strlen(body) : 0;
    int hl = snprintf(header, sizeof(header),
                       "HTTP/1.1 %d %s\r\nContent-Type: %s\r\nContent-Length: %zu\r\nConnection: close\r\n\r\n",
                       code, status, content_type, body_len);
    send_all(fd, header, (size_t)hl);
    if (body_len) send_all(fd, body, body_len);
}

#define REQ_BUF_SIZE 16384

static void handle_client(int fd) {
    static char buf[REQ_BUF_SIZE];
    size_t total = 0;
    int header_end = -1;

    /* Read until we have the full header block (\r\n\r\n). Bodies larger
     * than what fits after that point are read separately below, driven
     * by Content-Length -- patches are small text files, this is plenty. */
    while (total < REQ_BUF_SIZE - 1) {
        ssize_t n = recv(fd, buf + total, REQ_BUF_SIZE - 1 - total, 0);
        if (n <= 0) { close(fd); return; }
        total += (size_t)n;
        buf[total] = '\0';
        char *marker = strstr(buf, "\r\n\r\n");
        if (marker) { header_end = (int)(marker - buf) + 4; break; }
    }
    if (header_end < 0) { respond(fd, 400, "Bad Request", "text/plain", "Request too large"); close(fd); return; }

    char method[8] = {0}, path[512] = {0};
    sscanf(buf, "%7s %511s", method, path);

    int content_length = 0;
    char *cl = strcasestr_local(buf, "Content-Length:");
    if (cl) content_length = atoi(cl + strlen("Content-Length:"));

    /* Make sure we've actually got the whole body before we act on it. */
    int have_body = (int)total - header_end;
    while (have_body < content_length && total < REQ_BUF_SIZE - 1) {
        ssize_t n = recv(fd, buf + total, REQ_BUF_SIZE - 1 - total, 0);
        if (n <= 0) break;
        total += (size_t)n;
        have_body += (int)n;
    }
    const char *body = buf + header_end;
    if (have_body < 0) have_body = 0;
    if (have_body > content_length) have_body = content_length;

    if (strcmp(method, "GET") == 0 && (strcmp(path, "/") == 0 || strncmp(path, "/upload", 7) == 0)) {
        respond(fd, 200, "OK", "text/html; charset=utf-8", UPLOAD_PAGE_HTML);
    } else if (strcmp(method, "POST") == 0 && strncmp(path, "/upload/save", 12) == 0) {
        char raw_name[128] = {0};
        char *q = strstr(path, "name=");
        if (q) {
            char decoded[128];
            url_decode(q + 5, decoded, sizeof(decoded));
            strncpy(raw_name, decoded, sizeof(raw_name) - 1);
        }
        char safe_name[128];
        sanitize_lua_filename(raw_name, safe_name, sizeof(safe_name));

        char dir[512], filepath[768];
        snprintf(dir, sizeof(dir), "%s/patches", g_settings.work_path);
        mkdir(dir, 0777);
        snprintf(filepath, sizeof(filepath), "%s/%s", dir, safe_name);

        FILE *fp = fopen(filepath, "wb");
        if (fp) {
            if (have_body > 0) fwrite(body, 1, (size_t)have_body, fp);
            fclose(fp);
            char msg[256];
            snprintf(msg, sizeof(msg), "Saved as patches/%s (%d bytes). You can close this page.", safe_name, have_body);
            respond(fd, 200, "OK", "text/plain", msg);
            log_debug("local_upload_server: wrote %s (%d bytes)", filepath, have_body);
        } else {
            respond(fd, 500, "Internal Server Error", "text/plain", "Could not write file on the PS4 side.");
        }
    } else {
        respond(fd, 404, "Not Found", "text/plain", "Not found");
    }
    close(fd);
}

/* Local case-insensitive substring search -- avoids relying on a specific
 * libc providing strcasestr on this target. */
static char *strcasestr_local(const char *haystack, const char *needle) {
    size_t nlen = strlen(needle);
    for (const char *p = haystack; *p; p++) {
        if (strncasecmp(p, needle, nlen) == 0) return (char *)p;
    }
    return NULL;
}

static void *server_thread_func(void *arg) {
    (void)arg;
    while (s_running) {
        int client = accept(s_listen_fd, NULL, NULL);
        if (client < 0) {
            if (!s_running) break;
            continue;
        }
        handle_client(client);
    }
    return NULL;
}

int local_upload_server_is_running(void) { return s_running; }

int local_upload_server_start(void) {
    if (s_running) return 0;
    if (ensure_net_init() < 0) return -1;
    refresh_ip_cache();

    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) return -1;

    int yes = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(LOCAL_UPLOAD_SERVER_PORT);

    if (bind(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) { close(fd); return -1; }
    if (listen(fd, 4) < 0) { close(fd); return -1; }

    s_listen_fd = fd;
    s_running = 1;
    if (pthread_create(&s_thread, NULL, server_thread_func, NULL) != 0) {
        s_running = 0;
        close(fd);
        s_listen_fd = -1;
        return -1;
    }
    log_debug("local_upload_server: listening on port %d", LOCAL_UPLOAD_SERVER_PORT);
    return 0;
}

void local_upload_server_stop(void) {
    if (!s_running) return;
    s_running = 0;
    if (s_listen_fd >= 0) { close(s_listen_fd); s_listen_fd = -1; }
    pthread_join(s_thread, NULL);
}
