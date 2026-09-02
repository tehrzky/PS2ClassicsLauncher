#include "local_upload_server.h"
#include "settings.h"
#include "debug.h"
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <ctype.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/stat.h>
#include <dirent.h>
#include <orbis/Net.h>
#include <orbis/NetCtl.h>
#include <orbis/Sysmodule.h>

static int s_running = 0;
static int s_listen_fd = -1;
static pthread_t s_thread;
static char s_ip_cache[64] = "0.0.0.0";

/* ---------- Game context (set once per game, before opening the QR screen) ---------- */
static char s_game_display_name[256] = "";
static char s_game_disc_id[32] = "";
static char s_game_config_name[256] = ""; /* ISO basename -- keys gameconfig/<name>.txt */
static char s_game_iso_path[512] = "";
static pthread_mutex_t s_ctx_lock = PTHREAD_MUTEX_INITIALIZER;

void local_upload_server_set_game_context(const char *display_name, const char *disc_id,
                                           const char *config_name, const char *iso_path) {
    pthread_mutex_lock(&s_ctx_lock);
    if (display_name) { strncpy(s_game_display_name, display_name, sizeof(s_game_display_name) - 1); s_game_display_name[sizeof(s_game_display_name)-1]=0; }
    if (disc_id)      { strncpy(s_game_disc_id, disc_id, sizeof(s_game_disc_id) - 1); s_game_disc_id[sizeof(s_game_disc_id)-1]=0; }
    if (config_name)  { strncpy(s_game_config_name, config_name, sizeof(s_game_config_name) - 1); s_game_config_name[sizeof(s_game_config_name)-1]=0; }
    if (iso_path)     { strncpy(s_game_iso_path, iso_path, sizeof(s_game_iso_path) - 1); s_game_iso_path[sizeof(s_game_iso_path)-1]=0; }
    pthread_mutex_unlock(&s_ctx_lock);
}

/* ---------- Network bring-up ----------
 * scraper.c already does the equivalent of this for outbound HTTP. If you
 * end up wiring both into the same build, consider factoring this into one
 * shared net_init() so sceNetInit()/sceNetCtlInit() are only ever called
 * once -- most SDKs tolerate a second call, but no need to rely on that. */
static int s_net_inited = 0;
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

/* ---------- tiny helpers ---------- */

static char *strcasestr_local(const char *haystack, const char *needle) {
    size_t nlen = strlen(needle);
    for (const char *p = haystack; *p; p++) {
        if (strncasecmp(p, needle, nlen) == 0) return (char *)p;
    }
    return NULL;
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

/* Pulls ?key=value out of a request path (path may or may not have a
 * query string; only the first match for `key` is returned, decoded). */
static int get_query_param(const char *path, const char *key, char *out, size_t out_len) {
    out[0] = '\0';
    const char *q = strchr(path, '?');
    if (!q) return 0;
    q++;
    size_t klen = strlen(key);
    while (*q) {
        const char *eq = strchr(q, '=');
        const char *amp = strchr(q, '&');
        if (!eq) break;
        if (!amp) amp = q + strlen(q);
        if ((size_t)(eq - q) == klen && strncmp(q, key, klen) == 0) {
            size_t vlen = (size_t)(amp - eq - 1);
            char raw[512];
            if (vlen >= sizeof(raw)) vlen = sizeof(raw) - 1;
            memcpy(raw, eq + 1, vlen);
            raw[vlen] = '\0';
            url_decode(raw, out, out_len);
            return 1;
        }
        if (*amp == '\0') break;
        q = amp + 1;
    }
    return 0;
}

/* Keep filenames from doing anything except naming a file inside the
 * target folder. This server has no auth (LAN-only by design, same trust
 * model as the existing FTP workflow), so this is the one thing standing
 * between "save a config" and "write anywhere on the filesystem" -- don't
 * relax it. Deliberately more permissive than a slug (game titles have
 * spaces, parens, apostrophes, dashes -- "Ace Combat 04 - Shattered Skies
 * (USA)" needs to survive intact to match gameconfig's own naming), but
 * path separators and ".." are always stripped, and the extension is
 * always forced server-side regardless of what the client sent. */
static void sanitize_filename(const char *in, const char *forced_ext, char *out, size_t out_len) {
    size_t oi = 0;
    for (size_t i = 0; in[i] && oi < out_len - 8; i++) {
        char c = in[i];
        if (c == '/' || c == '\\') continue;
        if (c == '.' && in[i+1] == '.') continue;
        if (isalnum((unsigned char)c) || strchr(" _-().,'!&[]", c)) {
            out[oi++] = c;
        }
    }
    while (oi > 0 && out[oi-1] == ' ') oi--; /* trim trailing spaces */
    out[oi] = '\0';
    if (oi == 0) strncpy(out, "config", out_len - 1);

    size_t elen = strlen(forced_ext);
    size_t len = strlen(out);
    if (len < elen || strcasecmp(out + len - elen, forced_ext) != 0) {
        strncat(out, forced_ext, out_len - strlen(out) - 1);
    }
}

static void send_all(int fd, const char *data, size_t len) {
    size_t sent = 0;
    while (sent < len) {
        ssize_t n = send(fd, data + sent, len - sent, 0);
        if (n <= 0) return;
        sent += (size_t)n;
    }
}

static void respond(int fd, int code, const char *status, const char *content_type, const char *body, size_t body_len) {
    char header[256];
    int hl = snprintf(header, sizeof(header),
                       "HTTP/1.1 %d %s\r\nContent-Type: %s\r\nContent-Length: %zu\r\nConnection: close\r\n\r\n",
                       code, status, content_type, body_len);
    send_all(fd, header, (size_t)hl);
    if (body_len) send_all(fd, body, body_len);
}

static void respond_text(int fd, int code, const char *status, const char *content_type, const char *body) {
    respond(fd, code, status, content_type, body, body ? strlen(body) : 0);
}

/* ---------- folder helpers ---------- */

static void get_dir(const char *type, char *out, size_t out_len) {
    if (strcmp(type, "lua") == 0)
        snprintf(out, out_len, "%s/patches", g_settings.work_path);
    else
        snprintf(out, out_len, "%s/gameconfig", g_settings.work_path);
    mkdir(out, 0777);
}

static const char *ext_for_type(const char *type) {
    return strcmp(type, "lua") == 0 ? ".lua" : ".txt";
}

/* JSON array of filenames (extension-filtered, alpha sorted) in the given
 * folder. Small enough to build with plain snprintf -- no need for a
 * general JSON encoder for one string array. */
static int cmp_str(const void *a, const void *b) {
    return strcasecmp(*(const char **)a, *(const char **)b);
}

static void list_files_json(const char *type, char *out, size_t out_len) {
    char dir[512];
    get_dir(type, dir, sizeof(dir));
    const char *ext = ext_for_type(type);
    size_t ext_len = strlen(ext);

    DIR *d = opendir(dir);
    size_t oi = 0;
    out[oi++] = '[';
    if (d) {
        char *names[256];
        int count = 0;
        struct dirent *entry;
        while ((entry = readdir(d)) != NULL && count < 256) {
            size_t len = strlen(entry->d_name);
            if (len <= ext_len) continue;
            if (strcasecmp(entry->d_name + len - ext_len, ext) != 0) continue;
            names[count++] = strdup(entry->d_name);
        }
        closedir(d);
        qsort(names, count, sizeof(char *), cmp_str);
        for (int i = 0; i < count; i++) {
            if (i > 0 && oi < out_len - 1) out[oi++] = ',';
            oi += snprintf(out + oi, out_len - oi, "\"");
            for (char *p = names[i]; *p && oi < out_len - 3; p++) {
                if (*p == '"' || *p == '\\') out[oi++] = '\\';
                out[oi++] = *p;
            }
            oi += snprintf(out + oi, out_len - oi, "\"");
            free(names[i]);
        }
    }
    if (oi < out_len - 1) out[oi++] = ']';
    out[oi] = '\0';
}

/* ---------- CLI template ----------
 * Mirrors the header format + fallback body config.c's set_active_game()
 * already generates for a brand-new gameconfig entry, so a file created
 * from here looks identical to one auto-created by actually launching the
 * game. This duplicates a little of that logic rather than refactoring
 * config.c's static helpers into a public function -- worth unifying
 * later, but keeping this file self-contained for now. */
static const char *CLI_FALLBACK_BODY =
"--max-disc-num=1\n"
"--ps2-lang=system\n"
"--host-osd=0\n"
"--host-audio=1\n"
"--host-display-mode=normal\n"
"--gs-uprender=2x2\n"
"--gs-upscale=EdgeSmooth\n"
"--path-patches=\"/data/PS4ROMS/PS2ISO/patches/\"\n"
"--path-featuredata=\"/data/PS4ROMS/PS2ISO/feature_data/\"\n"
"--load-feature-lua=0\n"
"--trophy-support=0\n";

static void build_cli_template(char *out, size_t out_len) {
    char header[1024];
    snprintf(header, sizeof(header),
        "# ============================================================\n"
        "#  Game:        %s\n"
        "#  Disc ID:     %s\n"
        "#  Emulator:    \n"
        "#  Emulator Title: Default\n"
        "#\n"
        "#  Note: \n"
        "# ============================================================\n"
        "\n",
        s_game_display_name[0] ? s_game_display_name : "Unknown",
        s_game_disc_id[0] ? s_game_disc_id : "UNKNOWN");

    char default_body[16384];
    int blen = 0;
    char default_path[512];
    snprintf(default_path, sizeof(default_path), "%s/config/default.txt", g_settings.work_path);
    FILE *fp = fopen(default_path, "rb");
    if (fp) {
        blen = (int)fread(default_body, 1, sizeof(default_body) - 1, fp);
        fclose(fp);
        if (blen < 0) blen = 0;
        default_body[blen] = '\0';
    }
    if (blen <= 0) {
        strncpy(default_body, CLI_FALLBACK_BODY, sizeof(default_body) - 1);
        default_body[sizeof(default_body) - 1] = '\0';
        blen = (int)strlen(default_body);
    }

    snprintf(out, out_len, "%s%s--ps2-title-id=%s\n--image=\"%s\"\n",
             header, default_body,
             s_game_disc_id[0] ? s_game_disc_id : "UNKNOWN",
             s_game_iso_path);
}

/* ---------- The pages ----------
 * All self-contained: no external CSS/JS/fonts, nothing fetched from the
 * internet. One shared script handles both the Lua and CLI manager pages
 * (they're the same UI shape -- dropdown of existing files, textarea,
 * name box, word-wrap toggle, save), parameterized by `TYPE`. */

static const char *PAGE_CSS =
"body{font-family:sans-serif;max-width:760px;margin:20px auto;padding:0 16px;background:#0b141f;color:#eee}"
"textarea{width:100%;box-sizing:border-box;height:340px;font-family:monospace;font-size:13px;background:#111c29;color:#e8f0fa;border:1px solid #345;padding:8px}"
"input[type=text]{width:100%;box-sizing:border-box;padding:8px;margin:6px 0;background:#111c29;color:#eee;border:1px solid #345}"
"select{width:100%;box-sizing:border-box;padding:8px;margin:6px 0;background:#111c29;color:#eee;border:1px solid #345}"
"button{padding:10px 18px;font-size:16px;margin-right:8px;margin-top:6px}"
"label{font-size:14px;color:#9ab}"
"#status{margin-top:12px;font-weight:bold}"
".nowrap{white-space:pre;overflow-x:auto}"
".wrap{white-space:pre-wrap;word-break:break-word}"
"a{color:#6cf}";

static void write_landing_page(char *out, size_t out_len) {
    snprintf(out, out_len,
        "<!doctype html><html><head><meta charset=utf-8>"
        "<meta name=viewport content=\"width=device-width,initial-scale=1\">"
        "<title>%s</title><style>%s</style></head><body>"
        "<h2>%s</h2>"
        "<p>Disc ID: <b>%s</b></p>"
        "<p>What do you want to manage for this game?</p>"
        "<p><a href=\"/lua\"><button>Lua Patch</button></a>"
        "<a href=\"/cli\"><button>CLI Config</button></a></p>"
        "</body></html>",
        s_game_display_name[0] ? s_game_display_name : "Patch Upload",
        PAGE_CSS,
        s_game_display_name[0] ? s_game_display_name : "(no game selected)",
        s_game_disc_id[0] ? s_game_disc_id : "-");
}

/* type: "lua" or "cli". default_name is what the name box is pre-filled
 * with (editable, not locked). */
static void write_manager_page(const char *type, char *out, size_t out_len) {
    char default_name[300];
    if (strcmp(type, "lua") == 0) {
        snprintf(default_name, sizeof(default_name), "%s_config.lua",
                  s_game_disc_id[0] ? s_game_disc_id : "UNKNOWN");
    } else {
        snprintf(default_name, sizeof(default_name), "%s.txt",
                  s_game_config_name[0] ? s_game_config_name : "config");
    }

    snprintf(out, out_len,
"<!doctype html><html><head><meta charset=utf-8>"
"<meta name=viewport content=\"width=device-width,initial-scale=1\">"
"<title>%s - %s</title><style>%s</style></head><body>"
"<p><a href=\"/\">&larr; %s</a></p>"
"<h2>%s</h2>"
"<label>Existing files</label>"
"<select id=existing><option value=\"\">-- New / current --</option></select>"
"<label>File</label>"
"<input type=file id=f>"
"<label>Filename (auto-named, but you can rename it -- e.g. for a backup)</label>"
"<input type=text id=name value=\"%s\">"
"<textarea id=t class=nowrap></textarea>"
"<div><label><input type=checkbox id=wrap> Word wrap</label></div>"
"<button onclick=doSave()>Save to PS4</button>"
"<div id=status></div>"
"<script>"
"var TYPE='%s';"
"function refreshList(cb){"
"  fetch('/api/list?type='+TYPE).then(function(r){return r.json();}).then(function(list){"
"    var sel=document.getElementById('existing');"
"    while(sel.options.length>1) sel.remove(1);"
"    list.forEach(function(n){var o=document.createElement('option');o.value=n;o.textContent=n;sel.appendChild(o);});"
"    if(cb) cb();"
"  });"
"}"
"function loadDefault(){"
"  var name=document.getElementById('name').value;"
"  fetch('/api/file?type='+TYPE+'&name='+encodeURIComponent(name)).then(function(r){"
"    if(r.ok) return r.text();"
"    if(TYPE=='cli') return fetch('/api/template?type=cli').then(function(r2){return r2.text();});"
"    return '';"
"  }).then(function(t){ document.getElementById('t').value = t; });"
"}"
"document.getElementById('existing').addEventListener('change', function(e){"
"  var n=e.target.value; if(!n) { loadDefault(); return; }"
"  document.getElementById('name').value = n;"
"  fetch('/api/file?type='+TYPE+'&name='+encodeURIComponent(n)).then(function(r){return r.text();})"
"    .then(function(t){ document.getElementById('t').value = t; });"
"});"
"document.getElementById('f').addEventListener('change', function(e){"
"  var file = e.target.files[0]; if(!file) return;"
"  document.getElementById('name').value = file.name;"
"  var r = new FileReader();"
"  r.onload = function(){ document.getElementById('t').value = r.result; };"
"  r.readAsText(file);"
"});"
"document.getElementById('wrap').addEventListener('change', function(e){"
"  document.getElementById('t').className = e.target.checked ? 'wrap' : 'nowrap';"
"});"
"function doSave(){"
"  var name = document.getElementById('name').value.trim();"
"  var body = document.getElementById('t').value;"
"  if(!name){ document.getElementById('status').textContent='Enter a filename first.'; return; }"
"  fetch('/api/save?type='+TYPE+'&name=' + encodeURIComponent(name), {method:'POST', body: body})"
"    .then(function(r){ return r.text().then(function(t){ return {ok:r.ok, t:t}; }); })"
"    .then(function(res){ document.getElementById('status').textContent = res.t; refreshList(); })"
"    .catch(function(err){ document.getElementById('status').textContent = 'Failed: ' + err; });"
"}"
"refreshList(loadDefault);"
"</script></body></html>",
        s_game_display_name[0] ? s_game_display_name : "PS4", type,
        PAGE_CSS,
        s_game_display_name[0] ? s_game_display_name : "back",
        strcmp(type, "lua") == 0 ? "Lua Patch" : "CLI Config",
        default_name, type);
}

/* ---------- request handling ---------- */

#define REQ_BUF_SIZE 16384

static void handle_client(int fd) {
    static char buf[REQ_BUF_SIZE];
    static char page_buf[32768];
    static char file_buf[65536];
    size_t total = 0;
    int header_end = -1;

    while (total < REQ_BUF_SIZE - 1) {
        ssize_t n = recv(fd, buf + total, REQ_BUF_SIZE - 1 - total, 0);
        if (n <= 0) { close(fd); return; }
        total += (size_t)n;
        buf[total] = '\0';
        char *marker = strstr(buf, "\r\n\r\n");
        if (marker) { header_end = (int)(marker - buf) + 4; break; }
    }
    if (header_end < 0) { respond_text(fd, 400, "Bad Request", "text/plain", "Request too large"); close(fd); return; }

    char method[8] = {0}, full_path[512] = {0};
    sscanf(buf, "%7s %511s", method, full_path);

    char path_only[512];
    strncpy(path_only, full_path, sizeof(path_only) - 1);
    path_only[sizeof(path_only)-1] = '\0';
    char *qmark = strchr(path_only, '?');
    if (qmark) *qmark = '\0';

    int content_length = 0;
    char *cl = strcasestr_local(buf, "Content-Length:");
    if (cl) content_length = atoi(cl + strlen("Content-Length:"));

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

    char type[8] = "lua";
    get_query_param(full_path, "type", type, sizeof(type));
    if (strcmp(type, "lua") != 0 && strcmp(type, "cli") != 0) strncpy(type, "lua", sizeof(type));

    pthread_mutex_lock(&s_ctx_lock);

    if (strcmp(method, "GET") == 0 && strcmp(path_only, "/") == 0) {
        write_landing_page(page_buf, sizeof(page_buf));
        respond_text(fd, 200, "OK", "text/html; charset=utf-8", page_buf);

    } else if (strcmp(method, "GET") == 0 && (strcmp(path_only, "/lua") == 0 || strcmp(path_only, "/cli") == 0)) {
        write_manager_page(path_only + 1, page_buf, sizeof(page_buf));
        respond_text(fd, 200, "OK", "text/html; charset=utf-8", page_buf);

    } else if (strcmp(method, "GET") == 0 && strcmp(path_only, "/api/list") == 0) {
        list_files_json(type, page_buf, sizeof(page_buf));
        respond_text(fd, 200, "OK", "application/json", page_buf);

    } else if (strcmp(method, "GET") == 0 && strcmp(path_only, "/api/template") == 0) {
        build_cli_template(page_buf, sizeof(page_buf));
        respond_text(fd, 200, "OK", "text/plain; charset=utf-8", page_buf);

    } else if (strcmp(method, "GET") == 0 && strcmp(path_only, "/api/file") == 0) {
        char raw_name[300] = {0};
        get_query_param(full_path, "name", raw_name, sizeof(raw_name));
        char safe_name[300];
        sanitize_filename(raw_name, ext_for_type(type), safe_name, sizeof(safe_name));

        char dir[512], filepath[768];
        get_dir(type, dir, sizeof(dir));
        snprintf(filepath, sizeof(filepath), "%s/%s", dir, safe_name);

        FILE *fp = fopen(filepath, "rb");
        if (fp) {
            size_t n = fread(file_buf, 1, sizeof(file_buf) - 1, fp);
            fclose(fp);
            file_buf[n] = '\0';
            respond(fd, 200, "OK", "text/plain; charset=utf-8", file_buf, n);
        } else {
            respond_text(fd, 404, "Not Found", "text/plain", "");
        }

    } else if (strcmp(method, "POST") == 0 && strcmp(path_only, "/api/save") == 0) {
        char raw_name[300] = {0};
        get_query_param(full_path, "name", raw_name, sizeof(raw_name));
        char safe_name[300];
        sanitize_filename(raw_name, ext_for_type(type), safe_name, sizeof(safe_name));

        char dir[512], filepath[768];
        get_dir(type, dir, sizeof(dir));
        snprintf(filepath, sizeof(filepath), "%s/%s", dir, safe_name);

        FILE *fp = fopen(filepath, "wb");
        if (fp) {
            if (have_body > 0) fwrite(body, 1, (size_t)have_body, fp);
            fclose(fp);
            char msg[300];
            snprintf(msg, sizeof(msg), "Saved as %s/%s (%d bytes).", type, safe_name, have_body);
            respond_text(fd, 200, "OK", "text/plain", msg);
            log_debug("local_upload_server: wrote %s (%d bytes)", filepath, have_body);
        } else {
            respond_text(fd, 500, "Internal Server Error", "text/plain", "Could not write file on the PS4 side.");
        }

    } else {
        respond_text(fd, 404, "Not Found", "text/plain", "Not found");
    }

    pthread_mutex_unlock(&s_ctx_lock);
    close(fd);
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
