#ifndef MCIO_COMPAT_H
#define MCIO_COMPAT_H

#include <stdint.h>
#include <stddef.h>
#include "debug.h"

/* Apollo's mcio.c calls LOG() for debug output */
#define LOG(fmt, ...) log_debug(fmt, ##__VA_ARGS__)

/* Apollo's mcio.c calls these to read/write the VMC file */
int read_buffer(const char *file_path, uint8_t **data, size_t *size);
int write_buffer(const char *file_path, uint8_t *data, size_t size);

#endif