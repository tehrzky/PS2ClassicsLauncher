#ifndef SJIS_H
#define SJIS_H

#include <stddef.h>

/* Convert a Shift-JIS string to UTF-8.
 * src     : input SJIS bytes (need not be null-terminated)
 * src_len : number of bytes to read from src
 * dst     : output buffer
 * dst_len : size of output buffer
 */
void sjis_to_utf8(const char *src, size_t src_len, char *dst, size_t dst_len);

#endif