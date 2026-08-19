#include "scraper.h"
#include "debug.h"
#include <orbis/libkernel.h>
#include <orbis/Sysmodule.h>
#include <orbis/Net.h>
#include <orbis/NetCtl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <netinet/in.h>

// ============ HTTP HELPER ============
static int download_file(const char *url, const char *path) {
    // Load network modules
    sceSysmoduleLoadModuleInternal(ORBIS_SYSMODULE_INTERNAL_NET);
    sceSysmoduleLoadModuleInternal(ORBIS_SYSMODULE_INTERNAL_NETCTL);
    
    // Initialize network
    SceNetInitParam netparam;
    static char net_memory[1024 * 1024];
    netparam.memory = net_memory;
    netparam.size = sizeof(net_memory);
    netparam.flags = 0;
    sceNetInit(&netparam);
    sceNetCtlInit();

    // Create directory if it doesn't exist
    char dir_path[512];
    strncpy(dir_path, path, sizeof(dir_path));
    char *last_slash = strrchr(dir_path, '/');
    if (last_slash) {
        *last_slash = '\0';
        mkdir(dir_path, 0777);
    }

    // Parse URL
    char host[256] = {0};
    char resource[512] = {0};
    if (sscanf(url, "https://%255[^/]%511s", host, resource) != 2) {
        log_debug("Failed to parse URL: %s", url);
        return -1;
    }

    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) {
        log_debug("Failed to create socket");
        return -1;
    }

    struct hostent *server = gethostbyname(host);
    if (!server) {
        log_debug("Failed to resolve host: %s", host);
        close(sock);
        return -1;
    }

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    memcpy(&addr.sin_addr.s_addr, server->h_addr, server->h_length);
    addr.sin_port = htons(443);

    if (connect(sock, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        log_debug("Failed to connect to %s", host);
        close(sock);
        return -1;
    }

    char request[1024];
    snprintf(request, sizeof(request),
             "GET %s HTTP/1.1\r\n"
             "Host: %s\r\n"
             "Connection: close\r\n"
             "User-Agent: PS4-Launcher/1.0\r\n"
             "\r\n",
             resource, host);

    if (send(sock, request, strlen(request), 0) < 0) {
        log_debug("Failed to send request");
        close(sock);
        return -1;
    }

    // Read response headers
    char buffer[4096];
    int bytes = recv(sock, buffer, sizeof(buffer) - 1, 0);
    if (bytes <= 0) {
        log_debug("Failed to read response");
        close(sock);
        return -1;
    }
    buffer[bytes] = '\0';

    // Check for HTTP 200
    if (strstr(buffer, "HTTP/1.1 200") == NULL && strstr(buffer, "HTTP/1.0 200") == NULL) {
        log_debug("HTTP error: %s", buffer);
        close(sock);
        return -1;
    }

    // Find the start of the body
    char *body = strstr(buffer, "\r\n\r\n");
    if (!body) {
        close(sock);
        return -1;
    }
    body += 4;

    // Write to file
    FILE *fp = fopen(path, "wb");
    if (!fp) {
        log_debug("Failed to create file: %s", path);
        close(sock);
        return -1;
    }

    // Write the first chunk
    int remaining = bytes - (body - buffer);
    fwrite(body, 1, remaining, fp);

    // Read the rest
    while ((bytes = recv(sock, buffer, sizeof(buffer), 0)) > 0) {
        fwrite(buffer, 1, bytes, fp);
    }

    fclose(fp);
    close(sock);
    log_debug("Downloaded: %s", path);
    return 0;
}

// ============ SCRAPER: Download cover art ============
void scraper_download_cover(const char *serial) {
    if (!serial || strlen(serial) == 0) {
        log_debug("No serial provided for cover download");
        return;
    }

    char cover_path[512];
    char url[512];

    // Create directories
    mkdir("/data/PS4ROMS/PS2ISO/covers", 0777);
    mkdir("/data/PS4ROMS/PS2ISO/covers/default", 0777);
    mkdir("/data/PS4ROMS/PS2ISO/covers/3d", 0777);

    // Download default cover (JPG)
    snprintf(cover_path, sizeof(cover_path), "/data/PS4ROMS/PS2ISO/covers/default/%s.jpg", serial);
    if (access(cover_path, F_OK) != 0) {
        snprintf(url, sizeof(url), "https://raw.githubusercontent.com/xlenore/ps2-covers/main/covers/default/%s.jpg", serial);
        log_debug("Downloading default cover: %s", url);
        download_file(url, cover_path);
    }

    // Download 3D cover (PNG)
    snprintf(cover_path, sizeof(cover_path), "/data/PS4ROMS/PS2ISO/covers/3d/%s.png", serial);
    if (access(cover_path, F_OK) != 0) {
        snprintf(url, sizeof(url), "https://raw.githubusercontent.com/xlenore/ps2-covers/main/covers/3d/%s.png", serial);
        log_debug("Downloading 3D cover: %s", url);
        download_file(url, cover_path);
    }
}
