#include "goodnames.h"
#include "debug.h"
#include <ctype.h>
#include <string.h>
#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>

static int is_all_ascii_printable(const char *s) {
    while (*s) {
        if (*s < 32 || *s > 126) return 0;
        s++;
    }
    return 1;
}

typedef struct {
    char id[32];
    char name[256];
} GoodName;

static GoodName good_names[12000];
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
    if (good_name_count < 12000) {
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

    while (fgets(line, sizeof(line), fp) && good_name_count < 12000) {
        size_t linelen = strlen(line);
        while (linelen > 0 && (line[linelen - 1] == '\n' || line[linelen - 1] == '\r')) {
            line[linelen - 1] = '\0';
            linelen--;
        }

        // ID line: XXXX-YYYYY:  (4 alphanum, dash, 5 digits, colon)
        if (linelen >= 11 && line[4] == '-' && line[10] == ':') {
            int valid = 1;
            for (int i = 0; i < 4; i++) {
                if (!isalnum((unsigned char)line[i])) { valid = 0; break; }
            }
            for (int i = 5; i < 10; i++) {
                if (!isdigit((unsigned char)line[i])) { valid = 0; break; }
            }
            if (valid) {
                strncpy(pending_id, line, 10);
                pending_id[10] = '\0';
            }
            continue;
        }

        // Name field: flexible whitespace before "name:"
        if (pending_id[0]) {
            char *name_ptr = strstr(line, "name:");
            if (name_ptr) {
                // Ensure "name:" is its own field, not part of another word
                if (name_ptr == line || isspace((unsigned char)*(name_ptr - 1))) {
                    name_ptr += 5; // skip "name:"
                    while (*name_ptr == ' ' || *name_ptr == '\t' || *name_ptr == ':')
                        name_ptr++;

                    char *start = name_ptr;
                    if (*start == '"') start++;

                    int len = (int)strlen(start);
                    while (len > 0 && (start[len - 1] == '"' ||
                                       isspace((unsigned char)start[len - 1]))) {
                        len--;
                    }
                    if (len > 0) {
                        start[len] = '\0';
                        add_or_override_good_name(pending_id, start);
                    }
                }
                pending_id[0] = '\0';
            }
        }
    }
    fclose(fp);
}

static void load_goodnames_txt(void) {
    FILE *fp = fopen("/data/PS4ROMS/PS2ISO/goodnames.txt", "r");
    if (!fp) return;

    char line[512];
    while (fgets(line, sizeof(line), fp) && good_name_count < 12000) {
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
    if (good && is_all_ascii_printable(good)) {
        const char *markers[] = {
            "(Disc 1)", "(Disc 2)", "(Disc 3)", "(Disc 4)",
            "[Disc 1]", "[Disc 2]", "[Disc 3]", "[Disc 4]",
            "Disc 1", "Disc 2", "Disc 3", "Disc 4", NULL
        };
        const char *found = NULL;
        for (int i = 0; markers[i]; i++) {
            if (stristr(iso_name, markers[i])) { found = markers[i]; break; }
        }
        if (found) {
            snprintf(out, out_len, "%s %s", good, found);
        } else {
            strncpy(out, good, out_len - 1);
            out[out_len - 1] = '\0';
        }
    } else {
        // Fallback to ISO filename if good name has unprintable chars
        strncpy(out, iso_name, out_len - 1);
        out[out_len - 1] = '\0';
        size_t len = strlen(out);
        if (len > 4 && (strcasecmp(out + len - 4, ".iso") == 0 || strcasecmp(out + len - 4, ".bin") == 0)) {
            out[len - 4] = '\0';
        }
    }
}
