#include "debug.h"
#include <fcntl.h>
#include <unistd.h>
#include <stdio.h>
#include <stdarg.h>

void log_debug(const char *fmt, ...) {
    static int first = 1;
    int fd;
    if (first) {
        fd = open("/data/PS4ROMS/PS2ISO/launcher_log.txt", O_WRONLY | O_CREAT | O_TRUNC, 0777);
        first = 0;
    } else {
        fd = open("/data/PS4ROMS/PS2ISO/launcher_log.txt", O_WRONLY | O_CREAT | O_APPEND, 0777);
    }
    if (fd < 0) return;
    char buf[1024];
    va_list args;
    va_start(args, fmt);
    int n = vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    if (n > 0) {
        write(fd, buf, n);
        write(fd, "\n", 1);
    }
    close(fd);
}