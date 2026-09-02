#include "pgsettings.h"
#include "json_parser.h"
#include "settings.h"
#include "debug.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <sys/stat.h>

static SettingValue *find_value(GameSettings *gs, const char *field_id) {
    int i;
    for (i = 0; i < gs->value_count; i++) {
        if (strcmp(gs->values[i].field_id, field_id) == 0)
            return &gs->values[i];
    }
    return NULL;
}

static SettingValue *add_value(GameSettings *gs, const char *field_id) {
    if (gs->value_count >= PGSETTINGS_MAX_VALUES) return NULL;
    SettingValue *v = &gs->values[gs->value_count++];
    memset(v, 0, sizeof(SettingValue));
    strncpy(v->field_id, field_id, SCHEMA_MAX_FIELD_ID_LEN - 1);
    v->field_id[SCHEMA_MAX_FIELD_ID_LEN - 1] = '\0';
    return v;
}

static void populate_defaults(const Schema *schema, GameSettings *gs) {
    int t, f;
    gs->value_count = 0;
    for (t = 0; t < schema->tab_count; t++) {
        for (f = 0; f < schema->tabs[t].field_count; f++) {
            const SchemaField *sf = &schema->tabs[t].fields[f];
            SettingValue *v = add_value(gs, sf->id);
            if (!v) continue;
            v->is_set = 0;
            if (sf->type == FIELD_SELECT || sf->type == FIELD_TEXT) {
                schema_get_default_str(sf, v->value_str, sizeof(v->value_str));
            } else if (sf->type == FIELD_SLIDER) {
                v->value_double = schema_get_default_double(sf);
                schema_get_default_str(sf, v->value_str, sizeof(v->value_str));
            } else {
                v->value_int = schema_get_default_int(sf);
            }
        }
    }
}

static void apply_json_overrides(JsonValue *values_obj, GameSettings *gs) {
    if (!values_obj || values_obj->type != JSON_OBJECT) return;
    int i;
    for (i = 0; i < values_obj->u.object.count; i++) {
        const char *key = values_obj->u.object.keys[i];
        JsonValue *val = values_obj->u.object.values[i];
        SettingValue *sv = find_value(gs, key);
        if (!sv) continue;

        if (val->type == JSON_STRING) {
            strncpy(sv->value_str, val->u.str_val, PGSETTINGS_MAX_VALUE_LEN - 1);
            sv->value_str[PGSETTINGS_MAX_VALUE_LEN - 1] = '\0';
            sv->is_set = 1;
        } else if (val->type == JSON_BOOL) {
            sv->value_int = val->u.bool_val ? 1 : 0;
            sv->is_set = 1;
        } else if (val->type == JSON_NUMBER) {
            sv->value_double = val->u.num_val;
            sv->value_int = (int)val->u.num_val;
            snprintf(sv->value_str, sizeof(sv->value_str), "%.10g", val->u.num_val);
            sv->is_set = 1;
        }
    }
}

int pgsettings_load(const char *disc_id, const Schema *schema, GameSettings *out) {
    if (!disc_id || !schema || !out) return -1;
    memset(out, 0, sizeof(GameSettings));
    strncpy(out->disc_id, disc_id, PGSETTINGS_MAX_DISC_ID_LEN - 1);
    out->disc_id[PGSETTINGS_MAX_DISC_ID_LEN - 1] = '\0';
    strncpy(out->version, schema->version, sizeof(out->version) - 1);
    out->version[sizeof(out->version) - 1] = '\0';

    populate_defaults(schema, out);

    char path[512];
    snprintf(path, sizeof(path), "%s/gamesettings/%s.json", g_settings.work_path, disc_id);

    JsonValue *root = json_parse_file(path);
    if (root) {
        if (root->type == JSON_OBJECT) {
            JsonValue *values = json_object_get(root, "values");
            if (values) apply_json_overrides(values, out);
        }
        json_free(root);
    }
    return 0;
}

int pgsettings_save(const char *disc_id, const GameSettings *settings, const Schema *schema) {
    if (!disc_id || !settings || !schema) return -1;

    JsonValue *root = json_new_object();
    json_object_set(root, "disc_id", json_new_string(settings->disc_id));
    json_object_set(root, "version", json_new_string(schema->version));

    JsonValue *values = json_new_object();
    int i;
    for (i = 0; i < settings->value_count; i++) {
        const SettingValue *sv = &settings->values[i];
        if (!sv->is_set) continue;

        const SchemaField *sf = schema_find_field(schema, sv->field_id);
        if (!sf) continue;

        if (sf->type == FIELD_SELECT || sf->type == FIELD_TEXT) {
            char def_str[PGSETTINGS_MAX_VALUE_LEN];
            schema_get_default_str(sf, def_str, sizeof(def_str));
            if (strcmp(sv->value_str, def_str) == 0) continue;
            json_object_set(values, sv->field_id, json_new_string(sv->value_str));
        } else if (sf->type == FIELD_SLIDER) {
            double def_d = schema_get_default_double(sf);
            if (fabs(sv->value_double - def_d) < 0.0001) continue;
            json_object_set(values, sv->field_id, json_new_number(sv->value_double));
        } else {
            int def_int = schema_get_default_int(sf);
            if (sv->value_int == def_int) continue;
            json_object_set(values, sv->field_id, json_new_number(sv->value_int));
        }
    }
    json_object_set(root, "values", values);

    char *json_str = json_serialize(root);
    json_free(root);
    if (!json_str) return -1;

    char dir[512];
    snprintf(dir, sizeof(dir), "%s/gamesettings", g_settings.work_path);
    mkdir(dir, 0777);

    char path[512];
    snprintf(path, sizeof(path), "%s/%s.json", dir, disc_id);
    FILE *fp = fopen(path, "w");
    if (!fp) { free(json_str); return -1; }
    fprintf(fp, "%s\n", json_str);
    fclose(fp);
    free(json_str);
    log_debug("pgsettings saved: %s", path);
    return 0;
}

void pgsettings_reset(const char *disc_id, const Schema *schema, GameSettings *out) {
    if (!disc_id || !schema || !out) return;
    memset(out, 0, sizeof(GameSettings));
    strncpy(out->disc_id, disc_id, PGSETTINGS_MAX_DISC_ID_LEN - 1);
    out->disc_id[PGSETTINGS_MAX_DISC_ID_LEN - 1] = '\0';
    strncpy(out->version, schema->version, sizeof(out->version) - 1);
    populate_defaults(schema, out);
}

/* Extract flag prefix from a command like "--volume=50" -> "volume" */
static void extract_flag_prefix(const char *cmd, char *out, size_t out_size) {
    out[0] = '\0';
    if (!cmd || cmd[0] != '-' || cmd[1] != '-') return;
    size_t i;
    for (i = 2; i < strlen(cmd) && i < out_size - 1; i++) {
        if (cmd[i] == '=' || cmd[i] == ' ') break;
        out[i - 2] = cmd[i];
    }
    out[i - 2] = '\0';
}

static int prefix_exists(const char prefixes[][64], int count, const char *prefix) {
    int i;
    for (i = 0; i < count; i++) {
        if (strcmp(prefixes[i], prefix) == 0) return 1;
    }
    return 0;
}

/* Internal generator with optional prefix deduplication */
static int pgsettings_generate_commands_internal(const GameSettings *settings, const Schema *schema,
                                    char *out_buf, size_t out_size,
                                    const char existing_prefixes[][64], int existing_count) {
    if (!settings || !schema || !out_buf || out_size == 0) return -1;
    out_buf[0] = '\0';
    size_t pos = 0;
    int i;

    char local_prefixes[256][64];
    int local_count = 0;
    const char (*prefixes)[64] = existing_prefixes ? existing_prefixes : local_prefixes;
    int pcount = existing_count >= 0 ? existing_count : local_count;
    int *pcount_ptr = existing_count >= 0 ? (int *)&existing_count : &local_count;

    for (i = 0; i < settings->value_count; i++) {
        const SettingValue *sv = &settings->values[i];
        const SchemaField *sf = schema_find_field(schema, sv->field_id);
        if (!sf) continue;

        int cmd_count = 0;
        char cmds[SCHEMA_MAX_COMMANDS_PER_OPTION][SCHEMA_MAX_COMMAND_LEN];

        if (sf->type == FIELD_SELECT) {
            int j;
            for (j = 0; j < sf->option_count; j++) {
                if (strcmp(sf->options[j].key, sv->value_str) == 0) {
                    int k;
                    for (k = 0; k < sf->options[j].command_count && cmd_count < SCHEMA_MAX_COMMANDS_PER_OPTION; k++) {
                        strncpy(cmds[cmd_count], sf->options[j].commands[k], SCHEMA_MAX_COMMAND_LEN - 1);
                        cmds[cmd_count][SCHEMA_MAX_COMMAND_LEN - 1] = '\0';
                        cmd_count++;
                    }
                    break;
                }
            }
        }
        else if (sf->type == FIELD_TOGGLE || sf->type == FIELD_CHECKBOX) {
            int j, src_count;
            const char (*src)[SCHEMA_MAX_COMMAND_LEN];
            if (sv->value_int) {
                src = sf->enabled_commands;
                src_count = sf->enabled_count;
            } else {
                src = sf->disabled_commands;
                src_count = sf->disabled_count;
            }
            for (j = 0; j < src_count && cmd_count < SCHEMA_MAX_COMMANDS_PER_OPTION; j++) {
                strncpy(cmds[cmd_count], src[j], SCHEMA_MAX_COMMAND_LEN - 1);
                cmds[cmd_count][SCHEMA_MAX_COMMAND_LEN - 1] = '\0';
                cmd_count++;
            }
        }
        else if (sf->type == FIELD_SLIDER) {
            if (sf->command_template[0]) {
                char cmd[SCHEMA_MAX_COMMAND_LEN];
                const char *p = sf->command_template;
                char *d = cmd;
                size_t rem = sizeof(cmd) - 1;
                while (*p && rem > 0) {
                    if (strncmp(p, "{value}", 7) == 0) {
                        char numbuf[64];
                        if (sf->step_f < 1.0)
                            snprintf(numbuf, sizeof(numbuf), "%.1f", sv->value_double);
                        else
                            snprintf(numbuf, sizeof(numbuf), "%.0f", sv->value_double);
                        size_t nl = strlen(numbuf);
                        if (nl < rem) {
                            memcpy(d, numbuf, nl);
                            d += nl;
                            rem -= nl;
                        }
                        p += 7;
                    } else {
                        *d++ = *p++;
                        rem--;
                    }
                }
                *d = '\0';
                strncpy(cmds[cmd_count], cmd, SCHEMA_MAX_COMMAND_LEN - 1);
                cmds[cmd_count][SCHEMA_MAX_COMMAND_LEN - 1] = '\0';
                cmd_count++;
            }
        }

        int j;
        for (j = 0; j < cmd_count; j++) {
            char prefix[64];
            extract_flag_prefix(cmds[j], prefix, sizeof(prefix));
            if (prefix[0] && prefix_exists(prefixes, pcount, prefix)) continue;

            if (pcount < 256) {
                strncpy((char *)prefixes[pcount], prefix, 63);
                pcount++;
            }

            size_t cl = strlen(cmds[j]);
            if (pos + cl + 2 >= out_size) break;
            if (pos > 0) {
                out_buf[pos++] = '\n';
            }
            memcpy(out_buf + pos, cmds[j], cl);
            pos += cl;
            out_buf[pos] = '\0';
        }
    }
    return (int)pos;
}

int pgsettings_generate_commands(const GameSettings *settings, const Schema *schema,
                                    char *out_buf, size_t out_size) {
    return pgsettings_generate_commands_internal(settings, schema, out_buf, out_size, NULL, -1);
}

/* Version that accepts existing prefixes to skip duplicates */
int pgsettings_generate_commands_dedup(const GameSettings *settings, const Schema *schema,
                                        char *out_buf, size_t out_size,
                                        const char existing_prefixes[][64], int existing_count) {
    return pgsettings_generate_commands_internal(settings, schema, out_buf, out_size,
                                                  existing_prefixes, existing_count);
}

const char *pgsettings_get_str(const GameSettings *gs, const char *field_id) {
    if (!gs || !field_id) return "";
    int i;
    for (i = 0; i < gs->value_count; i++) {
        if (strcmp(gs->values[i].field_id, field_id) == 0)
            return gs->values[i].value_str;
    }
    return "";
}

int pgsettings_get_int(const GameSettings *gs, const char *field_id) {
    if (!gs || !field_id) return 0;
    int i;
    for (i = 0; i < gs->value_count; i++) {
        if (strcmp(gs->values[i].field_id, field_id) == 0)
            return gs->values[i].value_int;
    }
    return 0;
}

double pgsettings_get_double(const GameSettings *gs, const char *field_id) {
    if (!gs || !field_id) return 0.0;
    int i;
    for (i = 0; i < gs->value_count; i++) {
        if (strcmp(gs->values[i].field_id, field_id) == 0)
            return gs->values[i].value_double;
    }
    return 0.0;
}

void pgsettings_set_str(GameSettings *gs, const char *field_id, const char *value) {
    if (!gs || !field_id || !value) return;
    SettingValue *sv = find_value((GameSettings *)gs, field_id);
    if (!sv) sv = add_value((GameSettings *)gs, field_id);
    if (!sv) return;
    strncpy(sv->value_str, value, PGSETTINGS_MAX_VALUE_LEN - 1);
    sv->value_str[PGSETTINGS_MAX_VALUE_LEN - 1] = '\0';
    sv->is_set = 1;
}

void pgsettings_set_int(GameSettings *gs, const char *field_id, int value) {
    if (!gs || !field_id) return;
    SettingValue *sv = find_value((GameSettings *)gs, field_id);
    if (!sv) sv = add_value((GameSettings *)gs, field_id);
    if (!sv) return;
    sv->value_int = value;
    sv->is_set = 1;
}

void pgsettings_set_double(GameSettings *gs, const char *field_id, double value) {
    if (!gs || !field_id) return;
    SettingValue *sv = find_value((GameSettings *)gs, field_id);
    if (!sv) sv = add_value((GameSettings *)gs, field_id);
    if (!sv) return;
    sv->value_double = value;
    snprintf(sv->value_str, sizeof(sv->value_str), "%.10g", value);
    sv->is_set = 1;
}

int pgsettings_is_modified(const GameSettings *gs, const Schema *schema, const char *field_id) {
    if (!gs || !schema || !field_id) return 0;
    const SchemaField *sf = schema_find_field(schema, field_id);
    if (!sf) return 0;
    int i;
    for (i = 0; i < gs->value_count; i++) {
        if (strcmp(gs->values[i].field_id, field_id) == 0) {
            if (!gs->values[i].is_set) return 0;
            if (sf->type == FIELD_SELECT || sf->type == FIELD_TEXT) {
                char def[PGSETTINGS_MAX_VALUE_LEN];
                schema_get_default_str(sf, def, sizeof(def));
                return strcmp(gs->values[i].value_str, def) != 0;
            } else if (sf->type == FIELD_SLIDER) {
                double def_d = schema_get_default_double(sf);
                return fabs(gs->values[i].value_double - def_d) > 0.0001;
            } else {
                return gs->values[i].value_int != schema_get_default_int(sf);
            }
        }
    }
    return 0;
}

int pgsettings_any_modified(const GameSettings *gs, const Schema *schema) {
    if (!gs || !schema) return 0;
    int i;
    for (i = 0; i < gs->value_count; i++) {
        if (pgsettings_is_modified(gs, schema, gs->values[i].field_id))
            return 1;
    }
    return 0;
}
