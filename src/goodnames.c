#include "goodnames.h"
#include "debug.h"
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <ctype.h>
#include <stdlib.h>
#include <stdio.h>      // <-- ADD THIS for snprintf

typedef struct {
    char id[32];
    char name[256];
} GoodName;

static GoodName good_names[2048];
static int good_name_count = 0;

void load_good_names(void) {
    int fd = open("/data/PS4ROMS/PS2ISO/goodnames.txt", O_RDONLY);
    if (fd < 0) return;
    char buf[65536];
    int n = read(fd, buf, sizeof(buf) - 1);
    close(fd);
    if (n <= 0) return;
    buf[n] = '\0';

    char *p = buf;
    while (*p && good_name_count < 2048) {
        while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') p++;
        if (*p == '#' || *p == '\0') {
            while (*p && *p != '\n') p++;
            if (*p == '\n') p++;
            continue;
        }
        char *eq = strchr(p, '=');
        if (!eq) break;
        int id_len = eq - p;
        if (id_len > 0 && id_len < 32) {
            strncpy(good_names[good_name_count].id, p, id_len);
            good_names[good_name_count].id[id_len] = '\0';

            char *name_start = eq + 1;
            char *name_end = name_start;
            while (*name_end && *name_end != '\n' && *name_end != '\r') name_end++;
            int name_len = name_end - name_start;
            if (name_len > 255) name_len = 255;
            strncpy(good_names[good_name_count].name, name_start, name_len);
            good_names[good_name_count].name[name_len] = '\0';
            good_name_count++;
            
            p = name_end;  // Now inside the if block where name_end is declared
        } else {
            // Invalid ID, skip to next line
            while (*p && *p != '\n') p++;
        }
        while (*p && *p != '\n') p++;
        if (*p == '\n') p++;
    }
}

const char* lookup_good_name(const char *disc_id) {
    for (int i = 0; i < good_name_count; i++) {
        if (strcasecmp(good_names[i].id, disc_id) == 0)
            return good_names[i].name;
    }
    return NULL;
}

static char* stristr(const char *haystack, const char *needle) {
    if (!needle || !needle[0]) return (char*)haystack;
    char *h = (char*)haystack;
    while (*h) {
        if (tolower((unsigned char)*h) == tolower((unsigned char)*needle)) {
            char *h2 = h + 1;
            const char *n2 = needle + 1;
            while (*n2 && tolower((unsigned char)*h2) == tolower((unsigned char)*n2)) {
                h2++; n2++;
            }
            if (!*n2) return h;
        }
        h++;
    }
    return NULL;
}

void build_display_name(const char *iso_name, const char *disc_id, char *out, size_t out_len) {
    const char *good = lookup_good_name(disc_id);
    if (good) {
        const char *markers[] = {
            "(Disc 1)", "(Disc 2)", "(Disc 3)", "(Disc 4)",
            "[Disc 1]", "[Disc 2]", "[Disc 3]", "[Disc 4]",
            "Disc 1", "Disc 2", "Disc 3", "Disc 4", NULL
        };
        const char *found = NULL;
        for (int i = 0; markers[i]; i++) {
            if (stristr(iso_name, markers[i])) {
                found = markers[i];
                break;
            }
        }
        if (found) {
            snprintf(out, out_len, "%s %s", good, found);
        } else {
            strncpy(out, good, out_len - 1);
            out[out_len - 1] = '\0';
        }
    } else {
        strncpy(out, iso_name, out_len - 1);
        out[out_len - 1] = '\0';
        size_t len = strlen(out);
        if (len > 4 && (strcasecmp(out + len - 4, ".iso") == 0 || strcasecmp(out + len - 4, ".bin") == 0)) {
            out[len - 4] = '\0';
        }
    }
}
