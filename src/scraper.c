#include "scraper.h"
#include "debug.h"
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include <stdbool.h>
#include <orbis/Http.h>
#include <orbis/_types/http.h>
#include <orbis/Ssl.h>
#include <orbis/Net.h>
#include <orbis/NetCtl.h>
#include <orbis/Sysmodule.h>

static int net_initialized = 0;

static int ensure_net_init(void)
{
    if (net_initialized)
        return 0;

    sceSysmoduleLoadModuleInternal(ORBIS_SYSMODULE_INTERNAL_NET);
    sceSysmoduleLoadModuleInternal(ORBIS_SYSMODULE_INTERNAL_NETCTL);
    sceSysmoduleLoadModuleInternal(ORBIS_SYSMODULE_INTERNAL_HTTP);
    sceSysmoduleLoadModuleInternal(ORBIS_SYSMODULE_INTERNAL_SSL);

    sceNetInit(NULL, 1024 * 1024);
    sceNetCtlInit();

    net_initialized = 1;
    log_debug("Network stack initialized");
    return 0;
}

// Must match OrbisHttpsCallback signature exactly
static int32_t ssl_callback(int32_t libsslCtxId, uint32_t verifyErr,
                            void * const sslCert[], int32_t certNum, void *userArg)
{
    (void)libsslCtxId; (void)verifyErr; (void)sslCert; (void)certNum; (void)userArg;
    return 1;
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

    // Create directory if needed
    char dir_path[512];
    strncpy(dir_path, path, sizeof(dir_path) - 1);
    dir_path[sizeof(dir_path) - 1] = '\0';
    char *last_slash = strrchr(dir_path, '/');
    if (last_slash) {
        *last_slash = '\0';
        mkdir(dir_path, 0777);
    }

    sslId = sceSslInit(SSL_POOLSIZE);
    if (sslId < 0) {
        log_debug("sceSslInit failed: 0x%08X", sslId);
        return -1;
    }
    log_debug("sceSslInit ok: %d", sslId);

    httpCtx = sceHttpInit(0, sslId, LIBHTTP_POOLSIZE);
    if (httpCtx < 0) {
        log_debug("sceHttpInit failed: 0x%08X", httpCtx);
        sceSslTerm();
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

    ret = sceHttpsSetSslCallback(tmplId, ssl_callback, NULL);
    log_debug("sceHttpsSetSslCallback returned: 0x%08X", ret);

    connId = sceHttpCreateConnectionWithURL(tmplId, url, 1);
    if (connId < 0) {
        log_debug("sceHttpCreateConnectionWithURL failed: 0x%08X", connId);
        goto cleanup;
    }
    log_debug("sceHttpCreateConnectionWithURL ok: %d", connId);

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
    return 0;

cleanup:
    if (fp) fclose(fp);
    if (reqId >= 0) sceHttpDeleteRequest(reqId);
    if (connId >= 0) sceHttpDeleteConnection(connId);
    if (tmplId >= 0) sceHttpDeleteTemplate(tmplId);
    if (httpCtx >= 0) sceHttpTerm(httpCtx);
    sceSslTerm();
    return -1;
}

void scraper_download_cover(const char *serial)
{
    if (!serial || strlen(serial) == 0) {
        log_debug("No serial provided for cover download");
        return;
    }

    char cover_path[512];
    char url[512];

    mkdir("/data/PS4ROMS/PS2ISO/covers", 0777);
    mkdir("/data/PS4ROMS/PS2ISO/covers/default", 0777);
    mkdir("/data/PS4ROMS/PS2ISO/covers/3d", 0777);

    snprintf(cover_path, sizeof(cover_path),
             "/data/PS4ROMS/PS2ISO/covers/default/%s.jpg", serial);
    if (access(cover_path, F_OK) != 0) {
        snprintf(url, sizeof(url),
                 "https://raw.githubusercontent.com/xlenore/ps2-covers/main/covers/default/%s.jpg",
                 serial);
        log_debug("Downloading default cover: %s", url);
        download_file(url, cover_path);
    }

    snprintf(cover_path, sizeof(cover_path),
             "/data/PS4ROMS/PS2ISO/covers/3d/%s.png", serial);
    if (access(cover_path, F_OK) != 0) {
        snprintf(url, sizeof(url),
                 "https://raw.githubusercontent.com/xlenore/ps2-covers/main/covers/3d/%s.png",
                 serial);
        log_debug("Downloading 3D cover: %s", url);
        download_file(url, cover_path);
    }
}

void scraper_download_gameindex(void)
{
    char path[512];
    snprintf(path, sizeof(path), "/data/PS4ROMS/PS2ISO/GameIndex.yaml");
    if (access(path, F_OK) == 0) {
        log_debug("GameIndex.yaml already exists");
        return;
    }
    char url[512];
    snprintf(url, sizeof(url),
             "https://raw.githubusercontent.com/xlenore/ps2-covers/main/tools/GameIndex.yaml");
    log_debug("Downloading GameIndex.yaml...");
    download_file(url, path);
}
