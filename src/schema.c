#include "schema.h"
#include "json_parser.h"
#include <string.h>
#include <stdio.h>

static FieldType parse_type(const char *s) {
    if (strcmp(s, "select") == 0) return FIELD_SELECT;
    if (strcmp(s, "toggle") == 0) return FIELD_TOGGLE;
    if (strcmp(s, "checkbox") == 0) return FIELD_CHECKBOX;
    if (strcmp(s, "slider") == 0) return FIELD_SLIDER;
    if (strcmp(s, "text") == 0) return FIELD_TEXT;
    return FIELD_SELECT;
}

static int parse_commands(JsonValue *arr, char out[][SCHEMA_MAX_COMMAND_LEN], int max_count) {
    if (!arr || arr->type != JSON_ARRAY) return 0;
    int i, n = 0;
    for (i = 0; i < arr->u.array.count && n < max_count; i++) {
        JsonValue *item = arr->u.array.items[i];
        if (item && item->type == JSON_STRING) {
            strncpy(out[n], item->u.str_val, SCHEMA_MAX_COMMAND_LEN - 1);
            out[n][SCHEMA_MAX_COMMAND_LEN - 1] = '\0';
            n++;
        }
    }
    return n;
}

static int parse_options(JsonValue *obj, SchemaOption *out, int max_count) {
    if (!obj || obj->type != JSON_OBJECT) return 0;
    int i, n = 0;
    for (i = 0; i < obj->u.object.count && n < max_count; i++) {
        strncpy(out[n].key, obj->u.object.keys[i], SCHEMA_MAX_FIELD_ID_LEN - 1);
        out[n].key[SCHEMA_MAX_FIELD_ID_LEN - 1] = '\0';
        JsonValue *opt = obj->u.object.values[i];
        if (opt && opt->type == JSON_OBJECT) {
            const char *lbl = json_object_get_string(opt, "label", out[n].key);
            strncpy(out[n].label, lbl, SCHEMA_MAX_LABEL_LEN - 1);
            out[n].label[SCHEMA_MAX_LABEL_LEN - 1] = '\0';
            JsonValue *cmds = json_object_get(opt, "commands");
            out[n].command_count = parse_commands(cmds, out[n].commands, SCHEMA_MAX_COMMANDS_PER_OPTION);
        }
        n++;
    }
    return n;
}

static int parse_field(JsonValue *obj, SchemaField *f) {
    if (!obj || obj->type != JSON_OBJECT) return -1;
    const char *id = json_object_get_string(obj, "id", "");
    strncpy(f->id, id, SCHEMA_MAX_FIELD_ID_LEN - 1);
    f->id[SCHEMA_MAX_FIELD_ID_LEN - 1] = '\0';

    const char *lbl = json_object_get_string(obj, "label", f->id);
    strncpy(f->label, lbl, SCHEMA_MAX_LABEL_LEN - 1);
    f->label[SCHEMA_MAX_LABEL_LEN - 1] = '\0';

    const char *tstr = json_object_get_string(obj, "type", "select");
    f->type = parse_type(tstr);

    const char *desc = json_object_get_string(obj, "description", "");
    strncpy(f->description, desc, SCHEMA_MAX_DESC_LEN - 1);
    f->description[SCHEMA_MAX_DESC_LEN - 1] = '\0';

    f->show_description = json_object_get_bool(obj, "show_description", 1);

    if (f->type == FIELD_SELECT) {
        JsonValue *opts = json_object_get(obj, "options");
        f->option_count = parse_options(opts, f->options, SCHEMA_MAX_OPTIONS_PER_FIELD);
        const char *def = json_object_get_string(obj, "default", "");
        strncpy(f->default_value, def, SCHEMA_MAX_FIELD_ID_LEN - 1);
        f->default_value[SCHEMA_MAX_FIELD_ID_LEN - 1] = '\0';
    }
    else if (f->type == FIELD_TOGGLE || f->type == FIELD_CHECKBOX) {
        f->default_bool = json_object_get_bool(obj, "default", 0);
        JsonValue *en = json_object_get(obj, "enabled_commands");
        f->enabled_count = parse_commands(en, f->enabled_commands, SCHEMA_MAX_COMMANDS_PER_OPTION);
        JsonValue *dis = json_object_get(obj, "disabled_commands");
        f->disabled_count = parse_commands(dis, f->disabled_commands, SCHEMA_MAX_COMMANDS_PER_OPTION);
    }
    else if (f->type == FIELD_SLIDER) {
        f->min_f = json_object_get_double(obj, "min", 0.0);
        f->max_f = json_object_get_double(obj, "max", 100.0);
        f->step_f = json_object_get_double(obj, "step", 1.0);
        f->default_f = json_object_get_double(obj, "default", f->min_f);
        const char *tmpl = json_object_get_string(obj, "command_template", "");
        strncpy(f->command_template, tmpl, SCHEMA_MAX_TEMPLATE_LEN - 1);
        f->command_template[SCHEMA_MAX_TEMPLATE_LEN - 1] = '\0';
    }
    else if (f->type == FIELD_TEXT) {
        f->max_length = json_object_get_int(obj, "max_length", 128);
        const char *def = json_object_get_string(obj, "default", "");
        strncpy(f->default_text, def, SCHEMA_MAX_TEXT_LEN - 1);
        f->default_text[SCHEMA_MAX_TEXT_LEN - 1] = '\0';
    }
    return 0;
}

static int parse_tab(JsonValue *obj, SchemaTab *t) {
    if (!obj || obj->type != JSON_OBJECT) return -1;
    const char *id = json_object_get_string(obj, "id", "");
    strncpy(t->id, id, SCHEMA_MAX_FIELD_ID_LEN - 1);
    t->id[SCHEMA_MAX_FIELD_ID_LEN - 1] = '\0';

    const char *lbl = json_object_get_string(obj, "label", t->id);
    strncpy(t->label, lbl, SCHEMA_MAX_LABEL_LEN - 1);
    t->label[SCHEMA_MAX_LABEL_LEN - 1] = '\0';

    const char *icon = json_object_get_string(obj, "icon", "");
    strncpy(t->icon, icon, sizeof(t->icon) - 1);
    t->icon[sizeof(t->icon) - 1] = '\0';

    JsonValue *fields = json_object_get(obj, "fields");
    if (fields && fields->type == JSON_ARRAY) {
        int i;
        for (i = 0; i < fields->u.array.count && t->field_count < SCHEMA_MAX_FIELDS_PER_TAB; i++) {
            if (parse_field(fields->u.array.items[i], &t->fields[t->field_count]) == 0)
                t->field_count++;
        }
    }
    return 0;
}

int schema_load(const char *path, Schema *out) {
    memset(out, 0, sizeof(Schema));
    JsonValue *root = json_parse_file(path);
    if (!root) return -1;
    if (root->type != JSON_OBJECT) { json_free(root); return -1; }

    const char *ver = json_object_get_string(root, "version", "1.0.0");
    strncpy(out->version, ver, sizeof(out->version) - 1);
    out->version[sizeof(out->version) - 1] = '\0';

    JsonValue *tabs = json_object_get(root, "tabs");
    if (tabs && tabs->type == JSON_ARRAY) {
        int i;
        for (i = 0; i < tabs->u.array.count && out->tab_count < SCHEMA_MAX_TABS; i++) {
            if (parse_tab(tabs->u.array.items[i], &out->tabs[out->tab_count]) == 0)
                out->tab_count++;
        }
    }

    json_free(root);
    return 0;
}

/* ---------- merge override ---------- */
static SchemaTab *find_tab_mut(Schema *s, const char *tab_id) {
    int i;
    for (i = 0; i < s->tab_count; i++) {
        if (strcmp(s->tabs[i].id, tab_id) == 0)
            return &s->tabs[i];
    }
    return NULL;
}

static SchemaField *find_field_mut(SchemaTab *t, const char *field_id) {
    int i;
    for (i = 0; i < t->field_count; i++) {
        if (strcmp(t->fields[i].id, field_id) == 0)
            return &t->fields[i];
    }
    return NULL;
}

static void merge_tab(Schema *master, const SchemaTab *override) {
    SchemaTab *mt = find_tab_mut(master, override->id);
    if (!mt) {
        /* Append new tab */
        if (master->tab_count >= SCHEMA_MAX_TABS) return;
        mt = &master->tabs[master->tab_count++];
        memcpy(mt, override, sizeof(SchemaTab));
        return;
    }
    /* Merge fields into existing tab */
    int i;
    for (i = 0; i < override->field_count; i++) {
        const SchemaField *of = &override->fields[i];
        SchemaField *mf = find_field_mut(mt, of->id);
        if (!mf) {
            /* Append new field */
            if (mt->field_count >= SCHEMA_MAX_FIELDS_PER_TAB) continue;
            mf = &mt->fields[mt->field_count++];
        }
        /* Replace field data */
        memcpy(mf, of, sizeof(SchemaField));
    }
}

int schema_merge_game_override(Schema *master, const char *disc_id, const char *work_path) {
    if (!master || !disc_id || !work_path) return -1;
    char path[512];
    snprintf(path, sizeof(path), "%s/gamesettings/%s_schema.json", work_path, disc_id);

    JsonValue *root = json_parse_file(path);
    if (!root) return 0; /* no override file = no error */
    if (root->type != JSON_OBJECT) { json_free(root); return -1; }

    JsonValue *tabs = json_object_get(root, "tabs");
    if (tabs && tabs->type == JSON_ARRAY) {
        int i;
        for (i = 0; i < tabs->u.array.count; i++) {
            SchemaTab tmp;
            memset(&tmp, 0, sizeof(SchemaTab));
            if (parse_tab(tabs->u.array.items[i], &tmp) == 0)
                merge_tab(master, &tmp);
        }
    }
    json_free(root);
    return 0;
}

/* ---------- lookup ---------- */
const SchemaField *schema_find_field(const Schema *s, const char *field_id) {
    if (!s || !field_id) return NULL;
    int t, f;
    for (t = 0; t < s->tab_count; t++) {
        for (f = 0; f < s->tabs[t].field_count; f++) {
            if (strcmp(s->tabs[t].fields[f].id, field_id) == 0)
                return &s->tabs[t].fields[f];
        }
    }
    return NULL;
}

const SchemaTab *schema_find_tab(const Schema *s, const char *tab_id) {
    if (!s || !tab_id) return NULL;
    int i;
    for (i = 0; i < s->tab_count; i++) {
        if (strcmp(s->tabs[i].id, tab_id) == 0)
            return &s->tabs[i];
    }
    return NULL;
}

void schema_get_default_str(const SchemaField *f, char *out, size_t out_size) {
    if (!f || !out || out_size == 0) return;
    out[0] = '\0';
    if (f->type == FIELD_SELECT) {
        strncpy(out, f->default_value, out_size - 1);
    } else if (f->type == FIELD_TEXT) {
        strncpy(out, f->default_text, out_size - 1);
    } else if (f->type == FIELD_TOGGLE || f->type == FIELD_CHECKBOX) {
        strncpy(out, f->default_bool ? "true" : "false", out_size - 1);
    } else if (f->type == FIELD_SLIDER) {
        /* Use %.1f for float, %.0f for integer-like */
        if (f->step_f < 1.0)
            snprintf(out, out_size, "%.1f", f->default_f);
        else
            snprintf(out, out_size, "%.0f", f->default_f);
    }
    out[out_size - 1] = '\0';
}

int schema_get_default_int(const SchemaField *f) {
    if (!f) return 0;
    if (f->type == FIELD_TOGGLE || f->type == FIELD_CHECKBOX)
        return f->default_bool;
    if (f->type == FIELD_SLIDER)
        return (int)(f->default_f + 0.5);
    return 0;
}

double schema_get_default_double(const SchemaField *f) {
    if (!f) return 0.0;
    if (f->type == FIELD_SLIDER)
        return f->default_f;
    if (f->type == FIELD_TOGGLE || f->type == FIELD_CHECKBOX)
        return f->default_bool ? 1.0 : 0.0;
    return 0.0;
}

int schema_total_fields(const Schema *s) {
    if (!s) return 0;
    int t, total = 0;
    for (t = 0; t < s->tab_count; t++)
        total += s->tabs[t].field_count;
    return total;
}

void schema_flat_index(const Schema *s, int flat_idx, int *tab_idx, int *field_idx) {
    *tab_idx = 0;
    *field_idx = 0;
    if (!s) return;
    int t, cum = 0;
    for (t = 0; t < s->tab_count; t++) {
        if (flat_idx < cum + s->tabs[t].field_count) {
            *tab_idx = t;
            *field_idx = flat_idx - cum;
            return;
        }
        cum += s->tabs[t].field_count;
    }
    if (s->tab_count > 0) {
        *tab_idx = s->tab_count - 1;
        *field_idx = s->tabs[*tab_idx].field_count - 1;
    }
}

int schema_flat_index_from_tab_field(const Schema *s, int tab_idx, int field_idx) {
    if (!s || tab_idx < 0 || tab_idx >= s->tab_count) return 0;
    int t, cum = 0;
    for (t = 0; t < tab_idx; t++)
        cum += s->tabs[t].field_count;
    return cum + field_idx;
}
