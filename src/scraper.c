#include "scraper.h"
#include "debug.h"
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include <orbis/Http.h>
#include <orbis/Ssl.h>
#include <orbis/Net.h>
#include <orbis/NetCtl.h>
#include <orbis/Sysmodule.h>

// Suppress C99 warnings if headers omit these declarations
extern int sceNetInit(void *mem, int memSize);
extern int sceNetCtlInit(void);

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
    return 0;
}

// Accept any cert (homebrew convenience; avoids CA store issues)
static int ssl_callback(int a, int b, void *c)
{
    (void)a; (void)b; (void)c;
    return 1;
}

static int download_file(const char *url, const char *path)
{
    int ret;
    int httpCtx = -1, tmplId = -1, connId = -1, reqId = -1;
    FILE *fp = NULL;
    int statusCode = 0;

    if (ensure_net_init() < 0) {
        log_debug("Network init failed");
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

    ret = sceSslInit(1024 * 1024);
    if (ret < 0) {
        log_debug("sceSslInit failed: 0x%08X", ret);
        return -1;
    }

    httpCtx = sceHttpInit(1024 * 1024);
    if (httpCtx < 0) {
        log_debug("sceHttpInit failed: 0x%08X", httpCtx);
        sceSslTerm();
        return -1;
    }

    tmplId = sceHttpCreateTemplate(httpCtx, "PS2ClassicsLauncher/1.0", 1, 0);
    if (tmplId < 0) {
        log_debug("sceHttpCreateTemplate failed: 0x%08X", tmplId);
        goto cleanup;
    }

    sceHttpsSetSslCallback(tmplId, ssl_callback, NULL);

    connId = sceHttpCreateConnectionWithURL(tmplId, url, 0);
    if (connId < 0) {
        log_debug("sceHttpCreateConnectionWithURL failed: 0x%08X", connId);
        goto cleanup;
    }

    reqId = sceHttpCreateRequestWithURL(connId, SCE_HTTP_METHOD_GET, url, 0);
    if (reqId < 0) {
        log_debug("sceHttpCreateRequestWithURL failed: 0x%08X", reqId);
        goto cleanup;
    }

    ret = sceHttpSendRequest(reqId, NULL, 0);
    if (ret < 0) {
        log_debug("sceHttpSendRequest failed: 0x%08X", ret);
        goto cleanup;
    }

    ret = sceHttpGetStatusCode(reqId, &statusCode);
    if (ret < 0) {
        log_debug("sceHttpGetStatusCode failed: 0x%08X", ret);
        goto cleanup;
    }

    if (statusCode != 200) {
        log_debug("HTTP error: %d", statusCode);
        goto cleanup;
    }

    fp = fopen(path, "wb");
    if (!fp) {
        log_debug("Failed to create file: %s", path);
        goto cleanup;
    }

    char buf[4096];
    while ((ret = sceHttpReadData(reqId, buf, sizeof(buf))) > 0) {
        fwrite(buf, 1, ret, fp);
    }

    log_debug("Downloaded: %s", path);
    fclose(fp);

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

    snprintf(cover_path, sizeof(cover_path), "/data/PS4ROMS/PS2ISO/covers/default/%s.jpg", serial);
    if (access(cover_path, F_OK) != 0) {
        snprintf(url, sizeof(url), "https://raw.githubusercontent.com/xlenore/ps2-covers/main/covers/default/%s.jpg", serial);
        log_debug("Downloading default cover: %s", url);
        download_file(url, cover_path);
    }

    snprintf(cover_path, sizeof(cover_path), "/data/PS4ROMS/PS2ISO/covers/3d/%s.png", serial);
    if (access(cover_path, F_OK) != 0) {
        snprintf(url, sizeof(url), "https://raw.githubusercontent.com/xlenore/ps2-covers/main/covers/3d/%s.png", serial);
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
    snprintf(url, sizeof(url), "https://raw.githubusercontent.com/xlenore/ps2-covers/main/tools/GameIndex.yaml");
    log_debug("Downloading GameIndex.yaml...");
    download_file(url, path);
}
