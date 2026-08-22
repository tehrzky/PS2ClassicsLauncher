#include "scraper.h"
#include "settings.h"
#include "debug.h"
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include <stdbool.h>
#include <stdlib.h>
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
    (void)libsslCtxId; (void)verifyErr; (void)sslCert; (void)certNum; (void)userArg;
    return 1;
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

static int download_file(const char *url, const char *path)
{
    int ret;
    int32_t sslId = -1, httpCtx = -1;
    int32_t tmplId = -1, connId = -1, reqId = -1;
    FILE *fp = NULL;
    int32_t statusCode = 0;

    log_debug("download_file: %s -> %s", url, path);

    if (ensure_net_init() < 0) {
        log_debug("ensure_net_init failed");
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
        return -1;
    }
    log_debug("URL parsed: scheme=%s host=%s path=%s port=%d", scheme, host, res_path, port);

    snprintf(g_download_status, sizeof(g_download_status), "Downloading: %s",
             strrchr(url, '/') ? strrchr(url, '/') + 1 : url);
    g_download_active = 1;

    // Manual DNS bypass — GoldHen blocks Sony DNS, so we resolve ourselves
    struct hostent *server = gethostbyname(host);
    if (!server) {
        log_debug("gethostbyname failed for: %s", host);
        g_download_active = 0;
        return -1;
    }
    struct in_addr **addr_list = (struct in_addr **)server->h_addr_list;
    char ip_str[32];
    strncpy(ip_str, inet_ntoa(*addr_list[0]), sizeof(ip_str) - 1);
    ip_str[sizeof(ip_str) - 1] = '\0';
    log_debug("Resolved %s -> %s", host, ip_str);

    sslId = sceSslInit(SSL_POOLSIZE);
    if (sslId < 0) {
        log_debug("sceSslInit failed: 0x%08X", sslId);
        g_download_active = 0;
        return -1;
    }
    log_debug("sceSslInit ok: %d", sslId);

    httpCtx = sceHttpInit(0, sslId, LIBHTTP_POOLSIZE);
    if (httpCtx < 0) {
        log_debug("sceHttpInit failed: 0x%08X", httpCtx);
        sceSslTerm();
        g_download_active = 0;
        return -1;
    }
    log_debug("sceHttpInit ok: %d", httpCtx);

    tmplId = sceHttpCreateTemplate(httpCtx, "PS2ClassicsLauncher/1.0",
                                   ORBIS_HTTP_VERSION_1_1, 0);
    if (tmplId < 0) {
        log_debug("sceHttpCreateTemplate failed: 0x%08X", tmplId);
        goto cleanup;
    }
    log_debug("sceHttpCreateTemplate ok: %d", tmplId);

    sceHttpsSetSslCallback(tmplId, ssl_callback, NULL);

    // Connect by resolved IP — bypasses blocked Sony DNS
    connId = sceHttpCreateConnection(tmplId, ip_str, scheme, port, 1);
    if (connId < 0) {
        log_debug("sceHttpCreateConnection failed: 0x%08X", connId);
        goto cleanup;
    }
    log_debug("sceHttpCreateConnection ok: %d", connId);

    // Create request with FULL URL — gives the HTTP/SSL layer the hostname for SNI
    reqId = sceHttpCreateRequestWithURL(connId, ORBIS_METHOD_GET, url, 0);
    if (reqId < 0) {
        log_debug("sceHttpCreateRequestWithURL failed: 0x%08X", reqId);
        goto cleanup;
    }
    log_debug("sceHttpCreateRequestWithURL ok: %d", reqId);

    ret = sceHttpSendRequest(reqId, NULL, 0);
    if (ret < 0) {
        log_debug("sceHttpSendRequest failed: 0x%08X", ret);
        goto cleanup;
    }
    log_debug("sceHttpSendRequest ok");

    ret = sceHttpGetStatusCode(reqId, &statusCode);
    if (ret < 0) {
        log_debug("sceHttpGetStatusCode failed: 0x%08X", ret);
        goto cleanup;
    }
    log_debug("HTTP status code: %d", statusCode);

    if (statusCode != 200) {
        log_debug("HTTP error: status=%d", statusCode);
        goto cleanup;
    }

    fp = fopen(path, "wb");
    if (!fp) {
        log_debug("Failed to create file: %s", path);
        goto cleanup;
    }

    char buf[4096];
    size_t total = 0;
    while ((ret = sceHttpReadData(reqId, buf, sizeof(buf))) > 0) {
        fwrite(buf, 1, ret, fp);
        total += ret;
    }
    fclose(fp);
    log_debug("Downloaded %zu bytes: %s", total, path);

    sceHttpDeleteRequest(reqId);
    sceHttpDeleteConnection(connId);
    sceHttpDeleteTemplate(tmplId);
    sceHttpTerm(httpCtx);
    sceSslTerm();
    g_download_active = 0;
    g_download_status[0] = '\0';
    return 0;

cleanup:
    g_download_active = 0;
    g_download_status[0] = '\0';
    if (fp) fclose(fp);
    if (reqId >= 0) sceHttpDeleteRequest(reqId);
    if (connId >= 0) sceHttpDeleteConnection(connId);
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

    if (preferred_3d) {
        snprintf(cover_path, sizeof(cover_path), "%s/covers/3d/%s.png", g_settings.work_path, serial);
        if (access(cover_path, F_OK) != 0) {
            build_cover_url(url, sizeof(url), serial, 1);
            log_debug("Downloading 3D cover: %s", url);
            if (download_file(url, cover_path) < 0) mark_no_cover(serial);
        }
        snprintf(cover_path, sizeof(cover_path), "%s/covers/default/%s.jpg", g_settings.work_path, serial);
        if (access(cover_path, F_OK) != 0) {
            build_cover_url(url, sizeof(url), serial, 0);
            log_debug("Downloading default cover: %s", url);
            if (download_file(url, cover_path) < 0) mark_no_cover(serial);
        }
    } else {
        snprintf(cover_path, sizeof(cover_path), "%s/covers/default/%s.jpg", g_settings.work_path, serial);
        if (access(cover_path, F_OK) != 0) {
            build_cover_url(url, sizeof(url), serial, 0);
            log_debug("Downloading default cover: %s", url);
            if (download_file(url, cover_path) < 0) mark_no_cover(serial);
        }
        snprintf(cover_path, sizeof(cover_path), "%s/covers/3d/%s.png", g_settings.work_path, serial);
        if (access(cover_path, F_OK) != 0) {
            build_cover_url(url, sizeof(url), serial, 1);
            log_debug("Downloading 3D cover: %s", url);
            if (download_file(url, cover_path) < 0) mark_no_cover(serial);
        }
    }
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
    download_file(url, path);
}
