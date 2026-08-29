#include "debug.h"
#include <fcntl.h>
#include <unistd.h>
#include <stdio.h>
#include <stdarg.h>
#include <sys/file.h>
#include <pthread.h>

#define LOG_PATH "/data/PS4ROMS/PS2ISO/launcher_log.txt"
#define LOG_BUF_SIZE 4096

static pthread_mutex_t g_log_mutex = PTHREAD_MUTEX_INITIALIZER;
static int g_log_fd = -1;

void log_debug(const char *fmt, ...) {
    if (!fmt) return;

    pthread_mutex_lock(&g_log_mutex);

    if (g_log_fd < 0) {
        g_log_fd = open(LOG_PATH, O_WRONLY | O_CREAT | O_TRUNC, 0777);
        if (g_log_fd < 0) {
            pthread_mutex_unlock(&g_log_mutex);
            return;
        }
    }

    char buf[LOG_BUF_SIZE];
    va_list args;
    va_start(args, fmt);
    int n = vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);

    if (n > 0) {
        // Clamp write length to buffer capacity if truncated
        size_t write_len = (n >= LOG_BUF_SIZE) ? (LOG_BUF_SIZE - 1) : (size_t)n;
        
        flock(g_log_fd, LOCK_EX);
        write(g_log_fd, buf, write_len);
        write(g_log_fd, "\n", 1);
        flock(g_log_fd, LOCK_UN);
    }

    pthread_mutex_unlock(&g_log_mutex);
}

void log_cleanup(void) {
    pthread_mutex_lock(&g_log_mutex);
    if (g_log_fd >= 0) {
        close(g_log_fd);
        g_log_fd = -1;
    }
    pthread_mutex_unlock(&g_log_mutex);
}
