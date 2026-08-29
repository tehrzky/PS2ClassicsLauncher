#ifndef JSON_PARSER_H
#define JSON_PARSER_H

typedef enum {
    JSON_NULL,
    JSON_BOOL,
    JSON_NUMBER,
    JSON_STRING,
    JSON_ARRAY,
    JSON_OBJECT
} JsonType;

typedef struct JsonValue JsonValue;

struct JsonValue {
    JsonType type;
    union {
        int bool_val;
        double num_val;
        char *str_val;
        struct {
            JsonValue **items;
            int count;
            int capacity;
        } array;
        struct {
            char **keys;
            JsonValue **values;
            int count;
            int capacity;
        } object;
    } u;
};

JsonValue *json_parse_file(const char *path);
JsonValue *json_parse_string(const char *str);
void json_free(JsonValue *v);

JsonValue *json_object_get(JsonValue *obj, const char *key);
const char *json_object_get_string(JsonValue *obj, const char *key, const char *def);
int json_object_get_int(JsonValue *obj, const char *key, int def);
int json_object_get_bool(JsonValue *obj, const char *key, int def);
double json_object_get_double(JsonValue *obj, const char *key, double def);

JsonValue *json_array_get(JsonValue *arr, int index);
int json_array_len(JsonValue *arr);

/* Serialize to newly allocated string (caller must free) */
char *json_serialize(JsonValue *v);

/* Build helpers */
JsonValue *json_new_object(void);
JsonValue *json_new_string(const char *s);
JsonValue *json_new_number(double n);
JsonValue *json_new_bool(int b);
void json_object_set(JsonValue *obj, const char *key, JsonValue *val);

#endif
