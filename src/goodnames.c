#include "goodnames.h"
#include "debug.h"
#include <ctype.h>
#include <string.h>
#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>

typedef struct {
    char id[32];
    char name[256];
} GoodName;

static GoodName good_names[2048];
static int good_name_count = 0;

static void trim_trailing(char *s) {
    int len = (int)strlen(s);
    while (len > 0 && isspace((unsigned char)s[len - 1])) {
        s[len - 1] = '\0';
        len--;
    }
}

static void add_or_override_good_name(const char *id, const char *name) {
    for (int i = 0; i < good_name_count; i++) {
        if (strcasecmp(good_names[i].id, id) == 0) {
            strncpy(good_names[i].name, name, 255);
            good_names[i].name[255] = '\0';
            return;
        }
    }
    if (good_name_count < 2048) {
        strncpy(good_names[good_name_count].id, id, 31);
        good_names[good_name_count].id[31] = '\0';
        strncpy(good_names[good_name_count].name, name, 255);
        good_names[good_name_count].name[255] = '\0';
        good_name_count++;
    }
}

static void load_gameindex_yaml(void) {
    FILE *fp = fopen("/data/PS4ROMS/PS2ISO/GameIndex.yaml", "r");
    if (!fp) return;

    char line[512];
    char pending_id[32] = {0};

    while (fgets(line, sizeof(line), fp) && good_name_count < 2048) {
        size_t linelen = strlen(line);
        while (linelen > 0 && (line[linelen - 1] == '\n' || line[linelen - 1] == '\r')) {
            line[linelen - 1] = '\0';
            linelen--;
        }

        if (linelen >= 11 &&
            isupper((unsigned char)line[0]) &&
            isupper((unsigned char)line[1]) &&
            isupper((unsigned char)line[2]) &&
            isupper((unsigned char)line[3]) &&
            line[4] == '-' &&
            isdigit((unsigned char)line[5]) &&
            isdigit((unsigned char)line[6]) &&
            isdigit((unsigned char)line[7]) &&
            isdigit((unsigned char)line[8]) &&
            isdigit((unsigned char)line[9]) &&
            line[10] == ':') {
            strncpy(pending_id, line, 11);
            pending_id[11] = '\0';
            continue;
        }

        if (pending_id[0] && strncmp(line, "  name:", 7) == 0) {
            char *start = strchr(line, '"');
            if (start) {
                start++;
                char *end = strchr(start, '"');
                if (end) {
                    int len = end - start;
                    if (len > 255) len = 255;
                    if (len > 0) {
                        add_or_override_good_name(pending_id, start);
                    }
                }
            }
            pending_id[0] = '\0';
        }
    }
    fclose(fp);
}

static void load_goodnames_txt(void) {
    FILE *fp = fopen("/data/PS4ROMS/PS2ISO/goodnames.txt", "r");
    if (!fp) return;

    char line[512];
    while (fgets(line, sizeof(line), fp) && good_name_count < 2048) {
        // Strip trailing \r and \n
        size_t len = strlen(line);
        while (len > 0 && (line[len - 1] == '\n' || line[len - 1] == '\r')) {
            line[len - 1] = '\0';
            len--;
        }

        // Skip empty lines and comments
        if (len == 0 || line[0] == '#') continue;

        // Find = ONLY on this line
        char *eq = strchr(line, '=');
        if (!eq) continue;  // malformed line — skip it, DON'T break

        *eq = '\0';
        char *id = line;
        char *name = eq + 1;

        trim_trailing(id);
        // name trailing spaces are less common, but safe to trim too
        trim_trailing(name);

        size_t id_len = strlen(id);
        size_t name_len = strlen(name);
        if (id_len > 0 && id_len < 32 && name_len > 0 && name_len < 256) {
            add_or_override_good_name(id, name);
        }
    }
    fclose(fp);
}

void load_good_names(void) {
    load_gameindex_yaml();
    load_goodnames_txt();
    log_debug("Loaded %d good names", good_name_count);
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
