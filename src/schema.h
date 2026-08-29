#ifndef SCHEMA_H
#define SCHEMA_H

#define SCHEMA_MAX_TABS 16
#define SCHEMA_MAX_FIELDS_PER_TAB 32
#define SCHEMA_MAX_OPTIONS_PER_FIELD 16
#define SCHEMA_MAX_COMMANDS_PER_OPTION 8
#define SCHEMA_MAX_COMMAND_LEN 128
#define SCHEMA_MAX_FIELD_ID_LEN 48
#define SCHEMA_MAX_LABEL_LEN 64
#define SCHEMA_MAX_DESC_LEN 512
#define SCHEMA_MAX_TEMPLATE_LEN 128
#define SCHEMA_MAX_TEXT_LEN 256

typedef enum {
    FIELD_SELECT,
    FIELD_TOGGLE,
    FIELD_CHECKBOX,
    FIELD_SLIDER,
    FIELD_TEXT
} FieldType;

typedef struct {
    char key[SCHEMA_MAX_FIELD_ID_LEN];
    char label[SCHEMA_MAX_LABEL_LEN];
    char commands[SCHEMA_MAX_COMMANDS_PER_OPTION][SCHEMA_MAX_COMMAND_LEN];
    int command_count;
} SchemaOption;

typedef struct {
    char id[SCHEMA_MAX_FIELD_ID_LEN];
    char label[SCHEMA_MAX_LABEL_LEN];
    FieldType type;
    char description[SCHEMA_MAX_DESC_LEN];

    /* select */
    SchemaOption options[SCHEMA_MAX_OPTIONS_PER_FIELD];
    int option_count;
    char default_value[SCHEMA_MAX_FIELD_ID_LEN];

    /* toggle / checkbox */
    int default_bool;
    char enabled_commands[SCHEMA_MAX_COMMANDS_PER_OPTION][SCHEMA_MAX_COMMAND_LEN];
    int enabled_count;
    char disabled_commands[SCHEMA_MAX_COMMANDS_PER_OPTION][SCHEMA_MAX_COMMAND_LEN];
    int disabled_count;

    /* slider */
    int min, max, step;
    char command_template[SCHEMA_MAX_TEMPLATE_LEN];

    /* text */
    int max_length;
    char default_text[SCHEMA_MAX_TEXT_LEN];
} SchemaField;

typedef struct {
    char id[SCHEMA_MAX_FIELD_ID_LEN];
    char label[SCHEMA_MAX_LABEL_LEN];
    char icon[8];
    SchemaField fields[SCHEMA_MAX_FIELDS_PER_TAB];
    int field_count;
} SchemaTab;

typedef struct {
    char version[16];
    SchemaTab tabs[SCHEMA_MAX_TABS];
    int tab_count;
} Schema;

/* Load schema from JSON file. Returns 0 on success, -1 on error. */
int schema_load(const char *path, Schema *out);

/* Find a field anywhere in the schema by its id. */
const SchemaField *schema_find_field(const Schema *s, const char *field_id);

/* Find a tab by id. */
const SchemaTab *schema_find_tab(const Schema *s, const char *tab_id);

/* Get the default value for a field as a string (into out_buf). */
void schema_get_default_str(const SchemaField *f, char *out, size_t out_size);

/* Get the default value for a field as an int. */
int schema_get_default_int(const SchemaField *f);

/* Count total fields across all tabs. */
int schema_total_fields(const Schema *s);

/* Get flat field index -> (tab_idx, field_idx) mapping. */
void schema_flat_index(const Schema *s, int flat_idx, int *tab_idx, int *field_idx);

/* Reverse: get flat index from tab/field indices. */
int schema_flat_index_from_tab_field(const Schema *s, int tab_idx, int field_idx);

#endif
