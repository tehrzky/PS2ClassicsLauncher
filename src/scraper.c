#include "scraper.h"
#include "settings.h"
#include "debug.h"
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include <stdbool.h>
#include <stdlib.h>
#include <stdarg.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <orbis/Http.h>
#include <orbis/_types/http.h>
#include <orbis/Ssl.h>
#include <orbis/Net.h>
#include <orbis/NetCtl.h>
#include <orbis/Sysmodule.h>

static int net_initialized = 0;

char g_download_status[128] = {0};
int  g_download_active = 0;

// Timeouts (microseconds) so a bad path fails fast instead of hanging the UI.
#define RESOLVE_TIMEOUT_USEC   (8  * 1000 * 1000)
#define CONNECT_TIMEOUT_USEC   (8  * 1000 * 1000)
#define SEND_TIMEOUT_USEC      (8  * 1000 * 1000)
#define RECV_TIMEOUT_USEC      (15 * 1000 * 1000)

static int ensure_net_init(void)
{
    if (net_initialized) return 0;

    sceSysmoduleLoadModuleInternal(ORBIS_SYSMODULE_INTERNAL_NET);
    sceSysmoduleLoadModuleInternal(ORBIS_SYSMODULE_INTERNAL_NETCTL);
    sceSysmoduleLoadModuleInternal(ORBIS_SYSMODULE_INTERNAL_HTTP);
    sceSysmoduleLoadModuleInternal(ORBIS_SYSMODULE_INTERNAL_SSL);

    int ret = sceNetInit(NULL, 1024 * 1024);
    if (ret < 0) {
        log_debug("sceNetInit failed: 0x%08X", ret);
        return -1;
    }

    ret = sceNetCtlInit();
    if (ret < 0) {
        log_debug("sceNetCtlInit failed: 0x%08X", ret);
        return -1;
    }

    net_initialized = 1;
    log_debug("Network stack initialized");
    return 0;
}

static int32_t ssl_callback(int32_t libsslCtxId, uint32_t verifyErr,
                            void * const sslCert[], int32_t certNum, void *userArg)
{
    (void)libsslCtxId; (void)certNum; (void)userArg; (void)sslCert;
    if (verifyErr != 0) {
        log_debug("SSL cert verify warning: 0x%08X (accepting anyway)", verifyErr);
    }
    return 1; // accept all certs — we don't ship a CA bundle
}

static int parse_url(const char *url, char *scheme, size_t scheme_len,
                     char *host, size_t host_len,
                     char *path, size_t path_len, int *port)
{
    const char *p = url;
    const char *scheme_end = strstr(p, "://");
    if (!scheme_end) return -1;

    size_t sLen = scheme_end - p;
    if (sLen >= scheme_len) return -1;
    strncpy(scheme, p, sLen);
    scheme[sLen] = '\0';

    p = scheme_end + 3;
    const char *path_start = strchr(p, '/');
    if (!path_start) {
        strncpy(host, p, host_len - 1);
        host[host_len - 1] = '\0';
        strncpy(path, "/", path_len);
        path[path_len - 1] = '\0';
    } else {
        size_t hLen = path_start - p;
        if (hLen >= host_len) return -1;
        strncpy(host, p, hLen);
        host[hLen] = '\0';
        strncpy(path, path_start, path_len - 1);
        path[path_len - 1] = '\0';
    }

    char *port_colon = strchr(host, ':');
    if (port_colon) {
        *port_colon = '\0';
        *port = atoi(port_colon + 1);
    } else {
        *port = (strcmp(scheme, "https") == 0) ? 443 : 80;
    }
    return 0;
}

static void set_status(const char *fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    vsnprintf(g_download_status, sizeof(g_download_status), fmt, args);
    va_end(args);
}

// Does the actual HTTP GET + file write once we already have a connId ready
// (either connected by hostname directly, or by resolved IP).
static int http_get_to_file(int32_t tmplId, int32_t connId, const char *url,
                            const char *path, const char *stage_label)
{
    int32_t reqId = -1;
    int32_t statusCode = 0;
    FILE *fp = NULL;
    int ret;

    reqId = sceHttpCreateRequestWithURL(connId, ORBIS_METHOD_GET, url, 0);
    if (reqId < 0) {
        int32_t err = 0;
        sceHttpGetLastErrno(connId, &err);
        log_debug("[%s] sceHttpCreateRequestWithURL failed: 0x%08X (errno 0x%08X)", stage_label, reqId, err);
        set_status("Failed: request (0x%08X)", reqId);
        return -1;
    }

    sceHttpSetSendTimeOut(reqId, SEND_TIMEOUT_USEC);
    sceHttpSetRecvTimeOut(reqId, RECV_TIMEOUT_USEC);
    sceHttpSetResolveTimeOut(reqId, RESOLVE_TIMEOUT_USEC);
    sceHttpSetConnectTimeOut(reqId, CONNECT_TIMEOUT_USEC);

    ret = sceHttpSendRequest(reqId, NULL, 0);
    if (ret < 0) {
        int32_t err = 0;
        sceHttpGetLastErrno(reqId, &err);
        log_debug("[%s] sceHttpSendRequest failed: 0x%08X (errno 0x%08X)", stage_label, ret, err);
        set_status("Failed: send (0x%08X)", ret);
        sceHttpDeleteRequest(reqId);
        return -1;
    }

    ret = sceHttpGetStatusCode(reqId, &statusCode);
    if (ret < 0) {
        log_debug("[%s] sceHttpGetStatusCode failed: 0x%08X", stage_label, ret);
        set_status("Failed: no status (0x%08X)", ret);
        sceHttpDeleteRequest(reqId);
        return -1;
    }
    log_debug("[%s] HTTP status code: %d", stage_label, statusCode);

    if (statusCode != 200) {
        log_debug("[%s] HTTP error: status=%d url=%s", stage_label, statusCode, url);
        set_status("Failed: HTTP %d", statusCode);
        sceHttpDeleteRequest(reqId);
        return -1;
    }

    fp = fopen(path, "wb");
    if (!fp) {
        log_debug("[%s] Failed to create file: %s", stage_label, path);
        set_status("Failed: can't write file");
        sceHttpDeleteRequest(reqId);
        return -1;
    }

    char buf[4096];
    size_t total = 0;
    while ((ret = sceHttpReadData(reqId, buf, sizeof(buf))) > 0) {
        fwrite(buf, 1, ret, fp);
        total += ret;
    }
    fclose(fp);

    if (ret < 0) {
        log_debug("[%s] sceHttpReadData error mid-stream: 0x%08X (got %zu bytes)", stage_label, ret, total);
        set_status("Failed: read error (0x%08X)", ret);
        sceHttpDeleteRequest(reqId);
        unlink(path); // don't leave a truncated/corrupt file behind
        return -1;
    }

    if (total == 0) {
        log_debug("[%s] Downloaded 0 bytes: %s", stage_label, path);
        set_status("Failed: empty response");
        sceHttpDeleteRequest(reqId);
        unlink(path);
        return -1;
    }

    log_debug("[%s] Downloaded %zu bytes: %s", stage_label, total, path);
    sceHttpDeleteRequest(reqId);
    return 0;
}

// Attempt 1: connect directly by hostname, letting the system resolve DNS
// and handle SNI/cert hostname matching itself. This is the normal path and
// works fine on most GoldHen builds — try it first.
static int try_direct(int32_t tmplId, const char *host, const char *scheme, int port,
                      const char *url, const char *path)
{
    int32_t connId = sceHttpCreateConnection(tmplId, host, scheme, (uint16_t)port, 1);
    if (connId < 0) {
        log_debug("[direct] sceHttpCreateConnection failed: 0x%08X", connId);
        return -1;
    }
    log_debug("[direct] sceHttpCreateConnection ok: %d (host=%s)", connId, host);

    int ret = http_get_to_file(tmplId, connId, url, path, "direct");
    sceHttpDeleteConnection(connId);
    return ret;
}

// Attempt 2 (fallback): manually resolve DNS ourselves and connect by IP,
// while still sending the full URL (with real hostname) in the request so
// SNI / Host header still work. Some GoldHen versions block or interfere
// with the console's normal DNS resolution for homebrew apps — this path
// works around that.
static int try_manual_dns(int32_t tmplId, const char *host, const char *scheme, int port,
                          const char *url, const char *path)
{
    struct hostent *server = gethostbyname(host);
    if (!server || !server->h_addr_list || !server->h_addr_list[0]) {
        log_debug("[manual-dns] gethostbyname failed for: %s", host);
        set_status("Failed: DNS resolve");
        return -1;
    }
    struct in_addr **addr_list = (struct in_addr **)server->h_addr_list;
    char ip_str[32];
    strncpy(ip_str, inet_ntoa(*addr_list[0]), sizeof(ip_str) - 1);
    ip_str[sizeof(ip_str) - 1] = '\0';
    log_debug("[manual-dns] Resolved %s -> %s", host, ip_str);

    int32_t connId = sceHttpCreateConnection(tmplId, ip_str, scheme, (uint16_t)port, 1);
    if (connId < 0) {
        log_debug("[manual-dns] sceHttpCreateConnection failed: 0x%08X", connId);
        set_status("Failed: connect (0x%08X)", connId);
        return -1;
    }
    log_debug("[manual-dns] sceHttpCreateConnection ok: %d (ip=%s)", connId, ip_str);

    int ret = http_get_to_file(tmplId, connId, url, path, "manual-dns");
    sceHttpDeleteConnection(connId);
    return ret;
}

static int download_file(const char *url, const char *path)
{
    int32_t sslId = -1, httpCtx = -1, tmplId = -1;

    log_debug("download_file: %s -> %s", url, path);

    if (ensure_net_init() < 0) {
        log_debug("ensure_net_init failed");
        set_status("Failed: network init");
        return -1;
    }

    char dir_path[512];
    strncpy(dir_path, path, sizeof(dir_path) - 1);
    dir_path[sizeof(dir_path) - 1] = '\0';
    char *last_slash = strrchr(dir_path, '/');
    if (last_slash) {
        *last_slash = '\0';
        mkdir(dir_path, 0777);
    }

    char scheme[16] = {0};
    char host[256] = {0};
    char res_path[512] = {0};
    int port = 443;

    if (parse_url(url, scheme, sizeof(scheme), host, sizeof(host),
                  res_path, sizeof(res_path), &port) < 0) {
        log_debug("Failed to parse URL: %s", url);
        set_status("Failed: bad URL");
        return -1;
    }
    log_debug("URL parsed: scheme=%s host=%s path=%s port=%d", scheme, host, res_path, port);

    set_status("Downloading: %s", strrchr(url, '/') ? strrchr(url, '/') + 1 : url);
    g_download_active = 1;

    sslId = sceSslInit(SSL_POOLSIZE);
    if (sslId < 0) {
        log_debug("sceSslInit failed: 0x%08X", sslId);
        set_status("Failed: SSL init (0x%08X)", sslId);
        g_download_active = 0;
        return -1;
    }
    log_debug("sceSslInit ok: %d", sslId);

    httpCtx = sceHttpInit(0, sslId, LIBHTTP_POOLSIZE);
    if (httpCtx < 0) {
        log_debug("sceHttpInit failed: 0x%08X", httpCtx);
        set_status("Failed: HTTP init (0x%08X)", httpCtx);
        sceSslTerm();
        g_download_active = 0;
        return -1;
    }
    log_debug("sceHttpInit ok: %d", httpCtx);

    tmplId = sceHttpCreateTemplate(httpCtx, "PS2ClassicsLauncher/1.0",
                                   ORBIS_HTTP_VERSION_1_1, 0);
    if (tmplId < 0) {
        log_debug("sceHttpCreateTemplate failed: 0x%08X", tmplId);
        set_status("Failed: template (0x%08X)", tmplId);
        goto cleanup;
    }
    log_debug("sceHttpCreateTemplate ok: %d", tmplId);

    sceHttpsSetSslCallback(tmplId, ssl_callback, NULL);
    sceHttpSetResolveTimeOut(tmplId, RESOLVE_TIMEOUT_USEC);
    sceHttpSetConnectTimeOut(tmplId, CONNECT_TIMEOUT_USEC);
    sceHttpSetSendTimeOut(tmplId, SEND_TIMEOUT_USEC);
    sceHttpSetRecvTimeOut(tmplId, RECV_TIMEOUT_USEC);

    // Try the normal path first...
    if (try_direct(tmplId, host, scheme, port, url, path) == 0) {
        goto success;
    }
    log_debug("Direct connection failed, falling back to manual DNS resolve...");

    // ...then fall back to manual-DNS-resolve + connect-by-IP.
    if (try_manual_dns(tmplId, host, scheme, port, url, path) == 0) {
        goto success;
    }

    log_debug("Both connection methods failed for: %s", url);
    goto cleanup;

success:
    sceHttpDeleteTemplate(tmplId);
    sceHttpTerm(httpCtx);
    sceSslTerm();
    g_download_active = 0;
    // Leave g_download_status showing the filename briefly rather than
    // blanking it instantly; caller UI can clear it on next redraw if desired.
    return 0;

cleanup:
    g_download_active = 0;
    // NOTE: g_download_status intentionally left set to the failure reason
    // (set_status was called above) instead of being blanked, so a status
    // line drawn after this call can show the user what went wrong.
    if (tmplId >= 0) sceHttpDeleteTemplate(tmplId);
    if (httpCtx >= 0) sceHttpTerm(httpCtx);
    sceSslTerm();
    return -1;
}

static void build_cover_url(char *out, size_t out_len, const char *serial, int is_3d)
{
    const char *base = g_settings.scraper_base_url;
    if (is_3d) {
        snprintf(out, out_len, "%s/covers/3d/%s.png", base, serial);
    } else {
        snprintf(out, out_len, "%s/covers/default/%s.jpg", base, serial);
    }
}

static void build_gameindex_url(char *out, size_t out_len)
{
    snprintf(out, out_len, "%s/tools/GameIndex.yaml", g_settings.scraper_base_url);
}

static void mark_no_cover(const char *serial)
{
    char path[512];
    snprintf(path, sizeof(path), "%s/covers/.%s.nocover", g_settings.work_path, serial);
    FILE *fp = fopen(path, "w");
    if (fp) fclose(fp);
}

static int has_no_cover_marker(const char *serial)
{
    char path[512];
    snprintf(path, sizeof(path), "%s/covers/.%s.nocover", g_settings.work_path, serial);
    return access(path, F_OK) == 0;
}

void scraper_download_cover(const char *serial)
{
    if (!serial || strlen(serial) == 0) return;
    if (!g_settings.auto_download_covers) return;
    if (has_no_cover_marker(serial)) return;

    char cover_path[512];
    char url[512];

    char covers_dir[512], default_dir[512], d3_dir[512];
    snprintf(covers_dir,  sizeof(covers_dir),  "%s/covers", g_settings.work_path);
    snprintf(default_dir,  sizeof(default_dir),  "%s/covers/default", g_settings.work_path);
    snprintf(d3_dir,       sizeof(d3_dir),       "%s/covers/3d", g_settings.work_path);
    mkdir(covers_dir, 0777);
    mkdir(default_dir, 0777);
    mkdir(d3_dir, 0777);

    int preferred_3d = g_settings.cover_type == 1;
    int got_any = 0;

    if (preferred_3d) {
        snprintf(cover_path, sizeof(cover_path), "%s/covers/3d/%s.png", g_settings.work_path, serial);
        if (access(cover_path, F_OK) != 0) {
            build_cover_url(url, sizeof(url), serial, 1);
            log_debug("Downloading 3D cover: %s", url);
            if (download_file(url, cover_path) == 0) got_any = 1;
        } else got_any = 1;
        snprintf(cover_path, sizeof(cover_path), "%s/covers/default/%s.jpg", g_settings.work_path, serial);
        if (access(cover_path, F_OK) != 0) {
            build_cover_url(url, sizeof(url), serial, 0);
            log_debug("Downloading default cover: %s", url);
            if (download_file(url, cover_path) == 0) got_any = 1;
        } else got_any = 1;
    } else {
        snprintf(cover_path, sizeof(cover_path), "%s/covers/default/%s.jpg", g_settings.work_path, serial);
        if (access(cover_path, F_OK) != 0) {
            build_cover_url(url, sizeof(url), serial, 0);
            log_debug("Downloading default cover: %s", url);
            if (download_file(url, cover_path) == 0) got_any = 1;
        } else got_any = 1;
        snprintf(cover_path, sizeof(cover_path), "%s/covers/3d/%s.png", g_settings.work_path, serial);
        if (access(cover_path, F_OK) != 0) {
            build_cover_url(url, sizeof(url), serial, 1);
            log_debug("Downloading 3D cover: %s", url);
            if (download_file(url, cover_path) == 0) got_any = 1;
        } else got_any = 1;
    }

    // Only mark "no cover" if BOTH variants genuinely failed — previously a
    // single failed variant (e.g. no 3D art available) could poison future
    // attempts to fetch the default cover too.
    if (!got_any) mark_no_cover(serial);
}

void scraper_force_download_cover(const char *serial)
{
    if (!serial || strlen(serial) == 0) return;

    char path[512];
    snprintf(path, sizeof(path), "%s/covers/.%s.nocover", g_settings.work_path, serial);
    unlink(path);

    char cover_path[512];
    char url[512];

    char covers_dir[512], default_dir[512], d3_dir[512];
    snprintf(covers_dir,  sizeof(covers_dir),  "%s/covers", g_settings.work_path);
    snprintf(default_dir,  sizeof(default_dir),  "%s/covers/default", g_settings.work_path);
    snprintf(d3_dir,       sizeof(d3_dir),       "%s/covers/3d", g_settings.work_path);
    mkdir(covers_dir, 0777);
    mkdir(default_dir, 0777);
    mkdir(d3_dir, 0777);

    snprintf(cover_path, sizeof(cover_path), "%s/covers/default/%s.jpg", g_settings.work_path, serial);
    build_cover_url(url, sizeof(url), serial, 0);
    log_debug("Force downloading default cover: %s", url);
    download_file(url, cover_path);

    snprintf(cover_path, sizeof(cover_path), "%s/covers/3d/%s.png", g_settings.work_path, serial);
    build_cover_url(url, sizeof(url), serial, 1);
    log_debug("Force downloading 3D cover: %s", url);
    download_file(url, cover_path);
}

void scraper_download_gameindex(void)
{
    if (!g_settings.auto_download_gameindex) return;

    char path[512];
    snprintf(path, sizeof(path), "%s/config/GameIndex.yaml", g_settings.work_path);

    char config_dir[512];
    snprintf(config_dir, sizeof(config_dir), "%s/config", g_settings.work_path);
    mkdir(config_dir, 0777);

    struct stat st;
    if (stat(path, &st) == 0 && st.st_size > 100) {
        log_debug("GameIndex.yaml already exists (%ld bytes)", st.st_size);
        return;
    }

    char url[512];
    build_gameindex_url(url, sizeof(url));
    log_debug("Downloading GameIndex.yaml...");
    download_file(url, path);
}

void scraper_force_download_gameindex(void)
{
    char path[512];
    snprintf(path, sizeof(path), "%s/config/GameIndex.yaml", g_settings.work_path);

    char config_dir[512];
    snprintf(config_dir, sizeof(config_dir), "%s/config", g_settings.work_path);
    mkdir(config_dir, 0777);

    char url[512];
    build_gameindex_url(url, sizeof(url));
    log_debug("Force downloading GameIndex.yaml...");
    int ret = download_file(url, path);
    log_debug("Force download GameIndex.yaml result: %s", ret == 0 ? "SUCCESS" : g_download_status);
}
