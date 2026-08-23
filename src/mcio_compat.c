#include "mcio_compat.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

int read_buffer(const char *file_path, uint8_t **data, size_t *size) {
    FILE *fp = fopen(file_path, "rb");
    if (!fp) {
        LOG("read_buffer: fopen failed: %s", file_path);
        return -1;
    }
    struct stat st;
    if (fstat(fileno(fp), &st) < 0) {
        fclose(fp);
        return -1;
    }
    *size = st.st_size;
    *data = (uint8_t *)malloc(*size);
    if (!*data) {
        fclose(fp);
        return -1;
    }
    size_t n = fread(*data, 1, *size, fp);
    fclose(fp);
    if (n != *size) {
        free(*data);
        *data = NULL;
        return -1;
    }
    return 0;
}

int write_buffer(const char *file_path, uint8_t *data, size_t size) {
    FILE *fp = fopen(file_path, "wb");
    if (!fp) {
        LOG("write_buffer: fopen failed: %s", file_path);
        return -1;
    }
    size_t n = fwrite(data, 1, size, fp);
    fclose(fp);
    if (n != size) {
        LOG("write_buffer: fwrite incomplete: %zu/%zu", n, size);
        return -1;
    }
    return 0;
}