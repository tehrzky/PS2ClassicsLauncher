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
#include <pthread.h>
#include <orbis/Http.h>
#include <orbis/_types/http.h>
#include <orbis/Ssl.h>
#include <orbis/Net.h>
#include <orbis/NetCtl.h>
#include <orbis/Sysmodule.h>

static int net_initialized = 0;

char g_download_status[128] = {0};
int  g_download_active = 0;

static char *g_gamedb_data = NULL;
static size_t g_gamedb_size = 0;
static char *g_metadata_data = NULL;
static size_t g_metadata_size = 0;

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
    uint64_t contentLength = 0;

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

    const char *file_name = strrchr(url, '/') ? strrchr(url, '/') + 1 : url;
    snprintf(g_download_status, sizeof(g_download_status), "Downloading %s... 0%%", file_name);
    g_download_active = 1;

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

    sslId = sceSslInit(SSL_POOLSIZE);
    if (sslId < 0) {
        g_download_active = 0;
        return -1;
    }

    httpCtx = sceHttpInit(0, sslId, LIBHTTP_POOLSIZE);
    if (httpCtx < 0) {
        sceSslTerm();
        g_download_active = 0;
        return -1;
    }

    tmplId = sceHttpCreateTemplate(httpCtx, "PS2ClassicsLauncher/1.0",
                                   ORBIS_HTTP_VERSION_1_1, 0);
    if (tmplId < 0) goto cleanup;

    sceHttpsSetSslCallback(tmplId, ssl_callback, NULL);

    connId = sceHttpCreateConnection(tmplId, ip_str, scheme, port, 1);
    if (connId < 0) goto cleanup;

    reqId = sceHttpCreateRequest(connId, ORBIS_METHOD_GET, res_path, 0);
    if (reqId < 0) goto cleanup;

    sceHttpAddRequestHeader(reqId, "Host", host, 0);

    ret = sceHttpSendRequest(reqId, NULL, 0);
    if (ret < 0) goto cleanup;

    ret = sceHttpGetStatusCode(reqId, &statusCode);
    if (ret < 0 || statusCode != 200) goto cleanup;

    int is_exist = 0;
    sceHttpGetContentLength(reqId, &is_exist, &contentLength);

    fp = fopen(path, "wb");
    if (!fp) goto cleanup;

    char buf[4096];
    size_t total = 0;
    while ((ret = sceHttpReadData(reqId, buf, sizeof(buf))) > 0) {
        fwrite(buf, 1, ret, fp);
        total += ret;

        if (contentLength > 0) {
            int pct = (int)((total * 100) / contentLength);
            snprintf(g_download_status, sizeof(g_download_status), "Downloading %s... %d%%", file_name, pct);
        } else {
            snprintf(g_download_status, sizeof(g_download_status), "Downloading %s... %zu KB", file_name, total / 1024);
        }
    }
    fclose(fp);
    fp = NULL;

    sceHttpDeleteRequest(reqId);
    sceHttpDeleteConnection(connId);
    sceHttpDeleteTemplate(tmplId);
    sceHttpTerm(httpCtx);
    sceSslTerm();

    snprintf(g_download_status, sizeof(g_download_status), "Download Complete!");
    sceKernelUsleep(500000);
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
    if (sslId >= 0) sceSslTerm();
    return -1;
}

static void normalize_string(const char *src, char *dst, size_t dst_len)
{
    size_t j = 0;
    for (size_t i = 0; src[i] && j < dst_len - 1; i++) {
        char c = src[i];
        if (c == ' ' || c == ':' || c == '-' || c == '_' || c == '.' ||
            c == '\'' || c == '\"' || c == '!' || c == '?' || c == '&' ||
            c == '/' || c == '\\' || c == '(' || c == ')' || c == '[' ||
            c == ']' || c == ',' || c == ';')
            continue;
        if (c >= 'A' && c <= 'Z') c = c - 'A' + 'a';
        dst[j++] = c;
    }
    dst[j] = '\0';
}

static char *file_load(const char *path, size_t *out_size)
{
    FILE *fp = fopen(path, "rb");
    if (!fp) return NULL;
    fseek(fp, 0, SEEK_END);
    long size = ftell(fp);
    if (size <= 0 || size > 16 * 1024 * 1024) { fclose(fp); return NULL; }
    char *buf = malloc(size + 1);
    if (!buf) { fclose(fp); return NULL; }
    fseek(fp, 0, SEEK_SET);
    fread(buf, 1, size, fp);
    buf[size] = '\0';
    fclose(fp);
    if (out_size) *out_size = (size_t)size;
    return buf;
}

static const char *json_find_object_end(const char *start)
{
    int depth = 0;
    const char *p = start;
    while (*p) {
        if (*p == '{') depth++;
        else if (*p == '}') {
            depth--;
            if (depth == 0) return p;
        } else if (*p == '"') {
            p++;
            while (*p && *p != '"') {
                if (*p == '\\' && *(p+1)) p++;
                p++;
            }
        }
        p++;
    }
    return NULL;
}

static int json_get_string(const char *obj, const char *key, char *out, size_t out_len)
{
    char pattern[64];
    snprintf(pattern, sizeof(pattern), "\"%s\"", key);
    const char *p = strstr(obj, pattern);
    if (!p) return -1;
    p += strlen(pattern);
    while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r' || *p == ':') p++;
    if (*p != '"') return -1;
    p++;
    size_t i = 0;
    while (*p && *p != '"' && i < out_len - 1) {
        if (*p == '\\' && *(p+1)) {
            p++;
            out[i++] = *p++;
        } else {
            out[i++] = *p++;
        }
    }
    out[i] = '\0';
    return 0;
}

static int gamedb_lookup_serial(const char *serial,
                                char *title, size_t title_len,
                                char *developer, size_t dev_len,
                                char *publisher, size_t pub_len,
                                char *genre, size_t genre_len,
                                char *release_date, size_t date_len)
{
    if (!g_gamedb_data || !serial) return -1;

    char needle[64];
    snprintf(needle, sizeof(needle), "\"%s\"", serial);

    const char *found = strstr(g_gamedb_data, needle);
    if (!found) return -1;

    const char *obj_start = strchr(found, '{');
    if (!obj_start) return -1;

    const char *obj_end = json_find_object_end(obj_start);
    if (!obj_end) return -1;

    size_t obj_len = (size_t)(obj_end - obj_start + 1);
    char *obj = malloc(obj_len + 1);
    if (!obj) return -1;
    memcpy(obj, obj_start, obj_len);
    obj[obj_len] = '\0';

    if (title)       json_get_string(obj, "title", title, title_len);
    if (developer)   json_get_string(obj, "developer", developer, dev_len);
    if (publisher)   json_get_string(obj, "publisher", publisher, pub_len);
    if (genre)       json_get_string(obj, "genre", genre, genre_len);
    if (release_date) {
        if (json_get_string(obj, "release_date", release_date, date_len) < 0)
            if (json_get_string(obj, "date", release_date, date_len) < 0)
                json_get_string(obj, "release", release_date, date_len);
    }

    free(obj);
    return 0;
}

static int metadata_find_description(const char *normalized_title,
                                     char *out_desc, size_t out_len)
{
    if (!g_metadata_data || !normalized_title || !normalized_title[0])
        return -1;

    const char *p = g_metadata_data;
    while ((p = strstr(p, "\"name\"")) != NULL) {
        p += 6;
        while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r' || *p == ':') p++;
        if (*p != '"') continue;
        p++;

        char name_buf[256];
        size_t i = 0;
        while (*p && *p != '"' && i < sizeof(name_buf) - 1) {
            if (*p == '\\' && *(p+1)) { p++; name_buf[i++] = *p++; }
            else name_buf[i++] = *p++;
        }
        name_buf[i] = '\0';

        char norm_name[256];
        normalize_string(name_buf, norm_name, sizeof(norm_name));

        if (strcmp(norm_name, normalized_title) == 0) {
            const char *desc_p = strstr(p, "\"description\"");
            if (desc_p) {
                desc_p += 13;
                while (*desc_p == ' ' || *desc_p == '\t' || *desc_p == '\n' || *desc_p == '\r' || *desc_p == ':') desc_p++;
                if (*desc_p == '"') {
                    desc_p++;
                    size_t j = 0;
                    while (*desc_p && *desc_p != '"' && j < out_len - 1) {
                        if (*desc_p == '\\' && *(desc_p+1)) { desc_p++; out_desc[j++] = *desc_p++; }
                        else out_desc[j++] = *desc_p++;
                    }
                    out_desc[j] = '\0';
                    return 0;
                }
            }
        }
    }
    return -1;
}

void scraper_init(void)
{
    char path[512];

    snprintf(path, sizeof(path), "%s/config/PS2.data.json", g_settings.work_path);
    if (g_gamedb_data) { free(g_gamedb_data); g_gamedb_data = NULL; }
    g_gamedb_data = file_load(path, &g_gamedb_size);

    snprintf(path, sizeof(path), "%s/config/ps2_metadata.json", g_settings.work_path);
    if (g_metadata_data) { free(g_metadata_data); g_metadata_data = NULL; }
    g_metadata_data = file_load(path, &g_metadata_size);
}

int scraper_get_game_info(const char *serial, const char *fallback_title, GameDBInfo *out)
{
    if (!out) return -1;
    memset(out, 0, sizeof(GameDBInfo));

    if (!serial || !serial[0]) return -1;

    gamedb_lookup_serial(serial,
                         out->title, sizeof(out->title),
                         out->developer, sizeof(out->developer),
                         out->publisher, sizeof(out->publisher),
                         out->genre, sizeof(out->genre),
                         out->release_date, sizeof(out->release_date));

    const char *match_title = (out->title[0]) ? out->title : fallback_title;
    if (match_title && match_title[0]) {
        char normalized[256];
        normalize_string(match_title, normalized, sizeof(normalized));
        metadata_find_description(normalized, out->description, sizeof(out->description));
    }

    return 0;
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
            if (download_file(url, cover_path) < 0) mark_no_cover(serial);
        }
        snprintf(cover_path, sizeof(cover_path), "%s/covers/default/%s.jpg", g_settings.work_path, serial);
        if (access(cover_path, F_OK) != 0) {
            build_cover_url(url, sizeof(url), serial, 0);
            if (download_file(url, cover_path) < 0) mark_no_cover(serial);
        }
    } else {
        snprintf(cover_path, sizeof(cover_path), "%s/covers/default/%s.jpg", g_settings.work_path, serial);
        if (access(cover_path, F_OK) != 0) {
            build_cover_url(url, sizeof(url), serial, 0);
            if (download_file(url, cover_path) < 0) mark_no_cover(serial);
        }
        snprintf(cover_path, sizeof(cover_path), "%s/covers/3d/%s.png", g_settings.work_path, serial);
        if (access(cover_path, F_OK) != 0) {
            build_cover_url(url, sizeof(url), serial, 1);
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
    download_file(url, cover_path);

    snprintf(cover_path, sizeof(cover_path), "%s/covers/3d/%s.png", g_settings.work_path, serial);
    build_cover_url(url, sizeof(url), serial, 1);
    download_file(url, cover_path);
}

void scraper_download_gameindex(void)
{
    if (!g_settings.auto_download_gameindex) return;

    char path[512];
    snprintf(path, sizeof(path), "%s/config/PS2.data.json", g_settings.work_path);

    char config_dir[512];
    snprintf(config_dir, sizeof(config_dir), "%s/config", g_settings.work_path);
    mkdir(config_dir, 0777);

    struct stat st;
    if (stat(path, &st) == 0 && st.st_size > 100) return;

    const char *url = "https://github.com/niemasd/GameDB-PS2/releases/latest/download/PS2.data.json";
    download_file(url, path);
}

void scraper_force_download_gameindex(void)
{
    char path[512];
    snprintf(path, sizeof(path), "%s/config/PS2.data.json", g_settings.work_path);

    char config_dir[512];
    snprintf(config_dir, sizeof(config_dir), "%s/config", g_settings.work_path);
    mkdir(config_dir, 0777);

    const char *url = "https://github.com/niemasd/GameDB-PS2/releases/latest/download/PS2.data.json";
    download_file(url, path);
}

/* ------------------------------------------------------------------ */
/*  Asynchronous Threading Support                                    */
/* ------------------------------------------------------------------ */
static void *async_cover_thread(void *arg) {
    char *serial = (char*)arg;
    scraper_download_cover(serial);
    free(serial);
    return NULL;
}

void scraper_download_cover_async(const char *serial) {
    if (!serial || !serial[0]) return;
    pthread_t t;
    char *arg = strdup(serial);
    pthread_create(&t, NULL, async_cover_thread, arg);
    pthread_detach(t);
}

static void *async_gameindex_thread(void *arg) {
    (void)arg;
    scraper_download_gameindex();
    return NULL;
}

void scraper_download_gameindex_async(void) {
    pthread_t t;
    pthread_create(&t, NULL, async_gameindex_thread, NULL);
    pthread_detach(t);
}