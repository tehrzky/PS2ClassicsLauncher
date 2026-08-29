#include "json_parser.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

/* ---------- helpers ---------- */
static char *json_strdup(const char *s) {
    size_t n = strlen(s) + 1;
    char *p = (char *)malloc(n);
    if (p) memcpy(p, s, n);
    return p;
}

static void skip_ws(const char **p) {
    while (**p == ' ' || **p == '\t' || **p == '\n' || **p == '\r') (*p)++;
}

static int parse_string_raw(const char **p, char *out, size_t out_len) {
    if (**p != '"') return 0;
    (*p)++;
    size_t i = 0;
    while (**p && **p != '"' && i + 1 < out_len) {
        if (**p == '\\' && (*p)[1]) {
            (*p)++;
            switch (**p) {
                case 'n': out[i++] = '\n'; break;
                case 't': out[i++] = '\t'; break;
                case 'r': out[i++] = '\r'; break;
                case 'b': out[i++] = '\b'; break;
                case 'f': out[i++] = '\f'; break;
                case 'u': /* skip unicode escape */ (*p)+=4; break;
                default: out[i++] = **p; break;
            }
        } else {
            out[i++] = **p;
        }
        (*p)++;
    }
    out[i] = '\0';
    if (**p == '"') (*p)++;
    return 1;
}

static JsonValue *parse_value(const char **p);

static JsonValue *parse_object(const char **p) {
    JsonValue *v = (JsonValue *)calloc(1, sizeof(JsonValue));
    v->type = JSON_OBJECT;
    v->u.object.capacity = 8;
    v->u.object.keys = (char **)calloc(v->u.object.capacity, sizeof(char *));
    v->u.object.values = (JsonValue **)calloc(v->u.object.capacity, sizeof(JsonValue *));
    (*p)++; /* skip '{' */
    while (1) {
        skip_ws(p);
        if (**p == '}') { (*p)++; break; }
        char key[256];
        if (!parse_string_raw(p, key, sizeof(key))) break;
        skip_ws(p);
        if (**p == ':') (*p)++;
        JsonValue *val = parse_value(p);
        if (v->u.object.count >= v->u.object.capacity) {
            int nc = v->u.object.capacity * 2;
            v->u.object.keys = (char **)realloc(v->u.object.keys, nc * sizeof(char *));
            v->u.object.values = (JsonValue **)realloc(v->u.object.values, nc * sizeof(JsonValue *));
            v->u.object.capacity = nc;
        }
        v->u.object.keys[v->u.object.count] = json_strdup(key);
        v->u.object.values[v->u.object.count] = val;
        v->u.object.count++;
        skip_ws(p);
        if (**p == ',') (*p)++;
    }
    return v;
}

static JsonValue *parse_array(const char **p) {
    JsonValue *v = (JsonValue *)calloc(1, sizeof(JsonValue));
    v->type = JSON_ARRAY;
    v->u.array.capacity = 8;
    v->u.array.items = (JsonValue **)calloc(v->u.array.capacity, sizeof(JsonValue *));
    (*p)++; /* skip '[' */
    while (1) {
        skip_ws(p);
        if (**p == ']') { (*p)++; break; }
        JsonValue *item = parse_value(p);
        if (v->u.array.count >= v->u.array.capacity) {
            int nc = v->u.array.capacity * 2;
            v->u.array.items = (JsonValue **)realloc(v->u.array.items, nc * sizeof(JsonValue *));
            v->u.array.capacity = nc;
        }
        v->u.array.items[v->u.array.count++] = item;
        skip_ws(p);
        if (**p == ',') (*p)++;
    }
    return v;
}

static JsonValue *parse_value(const char **p) {
    skip_ws(p);
    if (**p == '{') return parse_object(p);
    if (**p == '[') return parse_array(p);
    if (**p == '"') {
        char buf[1024];
        parse_string_raw(p, buf, sizeof(buf));
        JsonValue *v = (JsonValue *)calloc(1, sizeof(JsonValue));
        v->type = JSON_STRING;
        v->u.str_val = json_strdup(buf);
        return v;
    }
    if (strncmp(*p, "true", 4) == 0) {
        *p += 4;
        JsonValue *v = (JsonValue *)calloc(1, sizeof(JsonValue));
        v->type = JSON_BOOL;
        v->u.bool_val = 1;
        return v;
    }
    if (strncmp(*p, "false", 5) == 0) {
        *p += 5;
        JsonValue *v = (JsonValue *)calloc(1, sizeof(JsonValue));
        v->type = JSON_BOOL;
        v->u.bool_val = 0;
        return v;
    }
    if (strncmp(*p, "null", 4) == 0) {
        *p += 4;
        JsonValue *v = (JsonValue *)calloc(1, sizeof(JsonValue));
        v->type = JSON_NULL;
        return v;
    }
    /* number */
    char numbuf[64];
    int ni = 0;
    while ((**p >= '0' && **p <= '9') || **p == '-' || **p == '+' || **p == '.' || **p == 'e' || **p == 'E') {
        if (ni < 63) numbuf[ni++] = **p;
        (*p)++;
    }
    numbuf[ni] = '\0';
    JsonValue *v = (JsonValue *)calloc(1, sizeof(JsonValue));
    v->type = JSON_NUMBER;
    v->u.num_val = atof(numbuf);
    return v;
}

/* ---------- public API ---------- */
JsonValue *json_parse_string(const char *str) {
    const char *p = str;
    return parse_value(&p);
}

JsonValue *json_parse_file(const char *path) {
    FILE *fp = fopen(path, "rb");
    if (!fp) return NULL;
    fseek(fp, 0, SEEK_END);
    long sz = ftell(fp);
    fseek(fp, 0, SEEK_SET);
    char *buf = (char *)malloc(sz + 1);
    if (!buf) { fclose(fp); return NULL; }
    fread(buf, 1, sz, fp);
    buf[sz] = '\0';
    fclose(fp);
    JsonValue *v = json_parse_string(buf);
    free(buf);
    return v;
}

void json_free(JsonValue *v) {
    if (!v) return;
    int i;
    switch (v->type) {
        case JSON_STRING:
            free(v->u.str_val);
            break;
        case JSON_ARRAY:
            for (i = 0; i < v->u.array.count; i++) json_free(v->u.array.items[i]);
            free(v->u.array.items);
            break;
        case JSON_OBJECT:
            for (i = 0; i < v->u.object.count; i++) {
                free(v->u.object.keys[i]);
                json_free(v->u.object.values[i]);
            }
            free(v->u.object.keys);
            free(v->u.object.values);
            break;
        default: break;
    }
    free(v);
}

JsonValue *json_object_get(JsonValue *obj, const char *key) {
    if (!obj || obj->type != JSON_OBJECT) return NULL;
    int i;
    for (i = 0; i < obj->u.object.count; i++) {
        if (strcmp(obj->u.object.keys[i], key) == 0)
            return obj->u.object.values[i];
    }
    return NULL;
}

const char *json_object_get_string(JsonValue *obj, const char *key, const char *def) {
    JsonValue *v = json_object_get(obj, key);
    if (v && v->type == JSON_STRING) return v->u.str_val;
    return def;
}

int json_object_get_int(JsonValue *obj, const char *key, int def) {
    JsonValue *v = json_object_get(obj, key);
    if (v && v->type == JSON_NUMBER) return (int)v->u.num_val;
    if (v && v->type == JSON_BOOL) return v->u.bool_val ? 1 : 0;
    return def;
}

int json_object_get_bool(JsonValue *obj, const char *key, int def) {
    JsonValue *v = json_object_get(obj, key);
    if (v && v->type == JSON_BOOL) return v->u.bool_val;
    if (v && v->type == JSON_NUMBER) return v->u.num_val != 0;
    return def;
}

JsonValue *json_array_get(JsonValue *arr, int index) {
    if (!arr || arr->type != JSON_ARRAY) return NULL;
    if (index < 0 || index >= arr->u.array.count) return NULL;
    return arr->u.array.items[index];
}

int json_array_len(JsonValue *arr) {
    if (!arr || arr->type != JSON_ARRAY) return 0;
    return arr->u.array.count;
}

/* ---------- serialize helpers ---------- */
static void serialize_value(JsonValue *v, char **out, size_t *len, size_t *cap);

static void ensure_cap(char **out, size_t *len, size_t *cap, size_t need) {
    while (*len + need >= *cap) {
        *cap = *cap ? *cap * 2 : 256;
        *out = (char *)realloc(*out, *cap);
    }
}

static void append_str(char **out, size_t *len, size_t *cap, const char *s) {
    size_t l = strlen(s);
    ensure_cap(out, len, cap, l + 1);
    memcpy(*out + *len, s, l + 1);
    *len += l;
}

static void serialize_string(const char *s, char **out, size_t *len, size_t *cap) {
    append_str(out, len, cap, "\"");
    while (*s) {
        if (*s == '"' || *s == '\\') {
            char esc[3] = {'\\', *s, 0};
            append_str(out, len, cap, esc);
        } else if (*s == '\n') append_str(out, len, cap, "\\n");
        else if (*s == '\t') append_str(out, len, cap, "\\t");
        else if (*s == '\r') append_str(out, len, cap, "\\r");
        else {
            char ch[2] = {*s, 0};
            append_str(out, len, cap, ch);
        }
        s++;
    }
    append_str(out, len, cap, "\"");
}

static void serialize_value(JsonValue *v, char **out, size_t *len, size_t *cap) {
    int i;
    char buf[64];
    if (!v) { append_str(out, len, cap, "null"); return; }
    switch (v->type) {
        case JSON_NULL: append_str(out, len, cap, "null"); break;
        case JSON_BOOL: append_str(out, len, cap, v->u.bool_val ? "true" : "false"); break;
        case JSON_NUMBER:
            snprintf(buf, sizeof(buf), "%.0f", v->u.num_val);
            append_str(out, len, cap, buf);
            break;
        case JSON_STRING:
            serialize_string(v->u.str_val, out, len, cap);
            break;
        case JSON_ARRAY:
            append_str(out, len, cap, "[");
            for (i = 0; i < v->u.array.count; i++) {
                if (i > 0) append_str(out, len, cap, ",");
                serialize_value(v->u.array.items[i], out, len, cap);
            }
            append_str(out, len, cap, "]");
            break;
        case JSON_OBJECT:
            append_str(out, len, cap, "{");
            for (i = 0; i < v->u.object.count; i++) {
                if (i > 0) append_str(out, len, cap, ",");
                serialize_string(v->u.object.keys[i], out, len, cap);
                append_str(out, len, cap, ":");
                serialize_value(v->u.object.values[i], out, len, cap);
            }
            append_str(out, len, cap, "}");
            break;
    }
}

char *json_serialize(JsonValue *v) {
    size_t len = 0, cap = 0;
    char *out = NULL;
    serialize_value(v, &out, &len, &cap);
    return out;
}

/* ---------- build helpers ---------- */
JsonValue *json_new_object(void) {
    JsonValue *v = (JsonValue *)calloc(1, sizeof(JsonValue));
    v->type = JSON_OBJECT;
    v->u.object.capacity = 8;
    v->u.object.keys = (char **)calloc(v->u.object.capacity, sizeof(char *));
    v->u.object.values = (JsonValue **)calloc(v->u.object.capacity, sizeof(JsonValue *));
    return v;
}

JsonValue *json_new_string(const char *s) {
    JsonValue *v = (JsonValue *)calloc(1, sizeof(JsonValue));
    v->type = JSON_STRING;
    v->u.str_val = json_strdup(s);
    return v;
}

JsonValue *json_new_number(double n) {
    JsonValue *v = (JsonValue *)calloc(1, sizeof(JsonValue));
    v->type = JSON_NUMBER;
    v->u.num_val = n;
    return v;
}

JsonValue *json_new_bool(int b) {
    JsonValue *v = (JsonValue *)calloc(1, sizeof(JsonValue));
    v->type = JSON_BOOL;
    v->u.bool_val = b ? 1 : 0;
    return v;
}

void json_object_set(JsonValue *obj, const char *key, JsonValue *val) {
    if (!obj || obj->type != JSON_OBJECT) return;
    /* replace if exists */
    int i;
    for (i = 0; i < obj->u.object.count; i++) {
        if (strcmp(obj->u.object.keys[i], key) == 0) {
            json_free(obj->u.object.values[i]);
            obj->u.object.values[i] = val;
            return;
        }
    }
    if (obj->u.object.count >= obj->u.object.capacity) {
        int nc = obj->u.object.capacity * 2;
        obj->u.object.keys = (char **)realloc(obj->u.object.keys, nc * sizeof(char *));
        obj->u.object.values = (JsonValue **)realloc(obj->u.object.values, nc * sizeof(JsonValue *));
        obj->u.object.capacity = nc;
    }
    obj->u.object.keys[obj->u.object.count] = json_strdup(key);
    obj->u.object.values[obj->u.object.count] = val;
    obj->u.object.count++;
}
