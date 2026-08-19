#ifndef GOODNAMES_H
#define GOODNAMES_H

#include <stddef.h>

void load_good_names(void);
const char* lookup_good_name(const char *disc_id);
void build_display_name(const char *iso_name, const char *disc_id, char *out, size_t out_len);

#endif