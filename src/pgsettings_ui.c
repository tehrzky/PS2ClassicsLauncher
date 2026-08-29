#include "pgsettings_ui.h"
#include "video.h"
#include "font.h"
#include "colors.h"
#include "settings.h"
#include <string.h>
#include <stdio.h>

#define PG_PW        1200
#define PG_PH        900
#define PG_PX        ((SCREEN_WIDTH - PG_PW) / 2)
#define PG_PY        ((SCREEN_HEIGHT - PG_PH) / 2)

#define PG_HEADER_H   80
#define PG_TAB_H      56
#define PG_TAB_GAP    8
#define PG_FOOTER_H   70
#define PG_ROW_H      64
#define PG_ROW_GAP    4
#define PG_CONTENT_TOP  (PG_PY + PG_HEADER_H + PG_TAB_H + 16)
#define PG_CONTENT_BOT  (PG_PY + PG_PH - PG_FOOTER_H - 16)
#define PG_CONTENT_H    (PG_CONTENT_BOT - PG_CONTENT_TOP)
#define PG_VISIBLE_ROWS (PG_CONTENT_H / (PG_ROW_H + PG_ROW_GAP))

#define PG_DESC_H     80
#define PG_SCROLLBAR_W  6
#define PG_SCROLLBAR_PAD 16

#define PG_LEFT_MARGIN  40
#define PG_RIGHT_MARGIN 40
#define PG_VALUE_WIDTH  320

static void pg_draw_btn_hint(int x, int y, const char *btn, const char *lbl, uint32_t c) {
    int bw = font_text_width(btn, 22) + 14;
    int bh = 32;
    draw_rounded_rect(x, y, bw, bh, 5, COLOR_BORDER);
    draw_rounded_rect(x + 1, y + 1, bw - 2, bh - 2, 4, COLOR_CARD);
    draw_text(x + 7, y + 4, btn, c, 22);
    draw_text(x + bw + 10, y + 4, lbl, COLOR_TEXT, 22);
}

static void pg_draw_panel_bg(void) {
    /* Dim background */
    draw_rect(0, 0, SCREEN_WIDTH, PG_PY, 0xE60B141F);
    draw_rect(0, PG_PY + PG_PH, SCREEN_WIDTH, SCREEN_HEIGHT - PG_PY - PG_PH, 0xE60B141F);
    draw_rect(0, PG_PY, PG_PX, PG_PH, 0xE60B141F);
    draw_rect(PG_PX + PG_PW, PG_PY, SCREEN_WIDTH - PG_PX - PG_PW, PG_PH, 0xE60B141F);

    /* Main panel */
    draw_rounded_rect(PG_PX, PG_PY, PG_PW, PG_PH, 16, COLOR_PANEL);
    draw_rounded_rect(PG_PX + 2, PG_PY + 2, PG_PW - 4, PG_PH - 4, 14, COLOR_BG);
}

static void pg_draw_header(const char *game_name) {
    char title[512];
    snprintf(title, sizeof(title), "PS2 Game Settings: %s", game_name);
    int tw = font_text_width_slot(title, 36, FONT_SLOT_TITLE);
    int tx = PG_PX + (PG_PW - tw) / 2;
    if (tx < PG_PX + 20) tx = PG_PX + 20;
    draw_text_slot(tx, PG_PY + 20, title, COLOR_GOLD, 36, FONT_SLOT_TITLE);
    draw_rect(PG_PX + 20, PG_PY + PG_HEADER_H - 8, PG_PW - 40, 2, COLOR_BORDER);
}

static void pg_draw_tabs(const Schema *schema, int selected_tab) {
    int x = PG_PX + 20;
    int y = PG_PY + PG_HEADER_H + 8;
    int i;
    for (i = 0; i < schema->tab_count; i++) {
        const SchemaTab *t = &schema->tabs[i];
        char label[64];
        snprintf(label, sizeof(label), "%s %s", t->icon[0] ? t->icon : "", t->label);
        int lw = font_text_width(label, 24);
        int tw = lw + 28;
        int is_sel = (i == selected_tab);

        uint32_t bg = is_sel ? COLOR_ACCENT : COLOR_CARD;
        uint32_t fg = is_sel ? COLOR_TEXT : COLOR_DIM;

        draw_rounded_rect(x, y, tw, PG_TAB_H - 8, 10, bg);
        draw_text(x + 14, y + 10, label, fg, 24);
        x += tw + PG_TAB_GAP;
    }
    draw_rect(PG_PX + 20, y + PG_TAB_H - 4, PG_PW - 40, 2, COLOR_BORDER);
}

static const char *get_field_display_value(const SchemaField *f, const GameSettings *gs) {
    static char buf[256];
    if (f->type == FIELD_SELECT) {
        const char *val = pgsettings_get_str(gs, f->id);
        int i;
        for (i = 0; i < f->option_count; i++) {
            if (strcmp(f->options[i].key, val) == 0)
                return f->options[i].label;
        }
        return val;
    }
    else if (f->type == FIELD_TOGGLE || f->type == FIELD_CHECKBOX) {
        return pgsettings_get_int(gs, f->id) ? "ON" : "OFF";
    }
    else if (f->type == FIELD_SLIDER) {
        snprintf(buf, sizeof(buf), "%d", pgsettings_get_int(gs, f->id));
        return buf;
    }
    else if (f->type == FIELD_TEXT) {
        const char *val = pgsettings_get_str(gs, f->id);
        return val[0] ? val : "(empty)";
    }
    return "";
}

static void pg_draw_slider(int x, int y, int w, int h, int min, int max, int value, int is_sel) {
    int track_y = y + h / 2 - 3;
    int track_w = w - 24;
    draw_rounded_rect(x, track_y, track_w, 6, 3, is_sel ? COLOR_BORDER : COLOR_CARD);

    if (max > min) {
        int fill_w = track_w * (value - min) / (max - min);
        if (fill_w < 0) fill_w = 0;
        if (fill_w > track_w) fill_w = track_w;
        if (fill_w > 0)
            draw_rounded_rect(x, track_y, fill_w, 6, 3, is_sel ? COLOR_ACCENT : COLOR_DIM);
    }

    int thumb_x = x + track_w * (value - min) / (max - min) - 8;
    if (thumb_x < x) thumb_x = x;
    if (thumb_x > x + track_w - 16) thumb_x = x + track_w - 16;
    draw_rounded_rect(thumb_x, track_y - 5, 16, 16, 8, is_sel ? COLOR_GOLD : COLOR_DIM);
}

static void pg_draw_field(const SchemaField *f, const GameSettings *gs, 
                           int x, int y, int w, int is_sel, int is_modified) {
    /* Row background */
    if (is_sel) {
        draw_rounded_rect(x, y, w, PG_ROW_H, 8, COLOR_CARD_SEL);
        draw_rect(x, y + 4, 4, PG_ROW_H - 8, COLOR_ACCENT);
    } else if (is_modified) {
        draw_rounded_rect(x, y, w, PG_ROW_H, 8, 0xFF1A2B3C);
    }

    /* Label */
    uint32_t label_color = is_sel ? COLOR_TEXT : (is_modified ? COLOR_GOLD : COLOR_DIM);
    draw_text(x + 16, y + 16, f->label, label_color, 28);

    /* Value area (right-aligned) */
    int vx = x + w - PG_VALUE_WIDTH - 16;
    int vw = PG_VALUE_WIDTH;

    if (f->type == FIELD_SLIDER) {
        pg_draw_slider(vx, y + 8, vw, PG_ROW_H - 16, f->min, f->max, 
                        pgsettings_get_int(gs, f->id), is_sel);
        /* Show numeric value next to slider */
        char num[32];
        snprintf(num, sizeof(num), "%d", pgsettings_get_int(gs, f->id));
        int nw = font_text_width(num, 22);
        draw_text(vx + vw + 10, y + 18, num, is_sel ? COLOR_ACCENT : COLOR_DIM, 22);
    }
    else if (f->type == FIELD_TOGGLE || f->type == FIELD_CHECKBOX) {
        int on = pgsettings_get_int(gs, f->id);
        const char *txt = on ? "[ON]" : "[OFF]";
        uint32_t tc = on ? COLOR_SUCCESS : COLOR_ERROR;
        if (is_sel) tc = on ? COLOR_SUCCESS : COLOR_ERROR;
        else tc = on ? COLOR_DIM : COLOR_MUTED;
        int tw = font_text_width(txt, 26);
        draw_text(vx + vw - tw, y + 16, txt, tc, 26);
    }
    else if (f->type == FIELD_SELECT) {
        const char *val = get_field_display_value(f, gs);
        char buf[128];
        snprintf(buf, sizeof(buf), "[%s]", val);
        uint32_t tc = is_sel ? COLOR_GOLD : COLOR_DIM;
        int tw = font_text_width(buf, 26);
        draw_text(vx + vw - tw, y + 16, buf, tc, 26);
        if (is_sel) {
            draw_text(vx + vw + 10, y + 16, "\xE2\x96\xBC", COLOR_ACCENT, 20); /* down arrow */
        }
    }
    else if (f->type == FIELD_TEXT) {
        const char *val = pgsettings_get_str(gs, f->id);
        char buf[128];
        snprintf(buf, sizeof(buf), "\"%s\"", val[0] ? val : "");
        uint32_t tc = is_sel ? COLOR_GOLD : COLOR_DIM;
        int tw = font_text_width(buf, 26);
        if (tw > vw) tw = vw;
        draw_text(vx + vw - tw, y + 16, buf, tc, 26);
    }
}

static void pg_draw_description(const SchemaField *f, int x, int y, int w) {
    if (!f || !f->description[0]) return;

    draw_rounded_rect(x, y, w, PG_DESC_H, 8, 0xFF1A2530);

    /* Draw lightbulb icon and text */
    draw_text(x + 12, y + 10, "\xF0\x9F\x92\xA1", COLOR_GOLD, 22);

    /* Wrap description text */
    char buf[512];
    strncpy(buf, f->description, sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = '\0';

    int line_h = 22;
    int max_lines = (PG_DESC_H - 20) / line_h;
    if (max_lines < 1) max_lines = 1;

    char line[256] = "";
    char *word = strtok(buf, " \n\r\t");
    int cy = y + 12;
    int lines = 0;
    int text_x = x + 42;
    int text_w = w - 56;

    while (word && lines < max_lines) {
        char test[512];
        if (line[0]) snprintf(test, sizeof(test), "%s %s", line, word);
        else snprintf(test, sizeof(test), "%s", word);

        if (font_text_width(test, 20) <= text_w) {
            snprintf(line, sizeof(line), "%s", test);
        } else {
            if (line[0]) {
                draw_text(text_x, cy, line, COLOR_DIM, 20);
                cy += line_h;
                lines++;
            }
            snprintf(line, sizeof(line), "%s", word);
        }
        word = strtok(NULL, " \n\r\t");
    }
    if (line[0] && lines < max_lines) {
        draw_text(text_x, cy, line, COLOR_DIM, 20);
    }
}

static void pg_draw_scrollbar(const Schema *schema, int selected_tab, int scroll_offset) {
    const SchemaTab *t = &schema->tabs[selected_tab];
    int total = t->field_count;
    if (total <= PG_VISIBLE_ROWS) return;

    int track_h = PG_CONTENT_H - 20;
    int sx = PG_PX + PG_PW - PG_SCROLLBAR_PAD - PG_SCROLLBAR_W;
    int sy = PG_CONTENT_TOP + 10;

    draw_rect(sx, sy, PG_SCROLLBAR_W, track_h, COLOR_CARD);

    int thumb = track_h * PG_VISIBLE_ROWS / total;
    if (thumb < 24) thumb = 24;
    int ty = sy + (track_h - thumb) * scroll_offset / (total - PG_VISIBLE_ROWS);
    draw_rect(sx, ty, PG_SCROLLBAR_W, thumb, COLOR_ACCENT);
}

static void pg_draw_footer(int dirty, int show_confirm, int confirm_sel) {
    int y = PG_PY + PG_PH - PG_FOOTER_H + 10;
    draw_rect(PG_PX, PG_PY + PG_PH - PG_FOOTER_H, PG_PW, 2, COLOR_BORDER);

    if (show_confirm) {
        const char *opts[3] = {"Save & Exit", "Discard Changes", "Cancel"};
        uint32_t colors[3] = {COLOR_SUCCESS, COLOR_ERROR, COLOR_DIM};
        int x = PG_PX + 40;
        int i;
        for (i = 0; i < 3; i++) {
            uint32_t c = (i == confirm_sel) ? COLOR_ACCENT : COLOR_CARD;
            int tw = font_text_width(opts[i], 24);
            draw_rounded_rect(x, y, tw + 24, 40, 6, c);
            draw_text(x + 12, y + 6, opts[i], colors[i], 24);
            x += tw + 40;
        }
    } else {
        int x = PG_PX + 20;
        pg_draw_btn_hint(x, y, "^v", "Navigate", COLOR_DIM); x += 200;
        pg_draw_btn_hint(x, y, "<>", "Change", COLOR_DIM); x += 200;
        pg_draw_btn_hint(x, y, "L1/R1", "Tabs", COLOR_DIM); x += 220;
        pg_draw_btn_hint(x, y, "X", "Activate", COLOR_ACCENT); x += 220;
        pg_draw_btn_hint(x, y, "TRI", "Reset Field", COLOR_DIM); x += 240;
        pg_draw_btn_hint(x, y, "O", dirty ? "Close*" : "Close", dirty ? COLOR_ERROR : COLOR_DIM);

        /* Dirty indicator */
        if (dirty) {
            const char *ind = "\xE2\x97\x8F UNSAVED";
            int iw = font_text_width(ind, 20);
            draw_text(PG_PX + PG_PW - iw - 20, y + 4, ind, COLOR_ERROR, 20);
        }
    }
}

/* ---------- Public draw function ---------- */
void draw_pgsettings_ui(const Schema *schema, GameSettings *settings,
                        PGSettingsUIState *st) {
    if (!schema || !settings || !st || !st->active) return;

    pg_draw_panel_bg();
    pg_draw_header(st->game_name);
    pg_draw_tabs(schema, st->selected_tab);

    const SchemaTab *tab = &schema->tabs[st->selected_tab];
    int content_x = PG_PX + PG_LEFT_MARGIN;
    int content_w = PG_PW - PG_LEFT_MARGIN - PG_RIGHT_MARGIN - PG_SCROLLBAR_PAD - 10;

    /* Auto-scroll */
    if (st->selected_field < st->scroll_offset)
        st->scroll_offset = st->selected_field;
    if (st->selected_field >= st->scroll_offset + PG_VISIBLE_ROWS)
        st->scroll_offset = st->selected_field - PG_VISIBLE_ROWS + 1;
    if (st->scroll_offset < 0) st->scroll_offset = 0;
    if (st->scroll_offset > tab->field_count - PG_VISIBLE_ROWS)
        st->scroll_offset = tab->field_count - PG_VISIBLE_ROWS;
    if (st->scroll_offset < 0) st->scroll_offset = 0;

    /* Draw visible fields */
    int i;
    for (i = 0; i < PG_VISIBLE_ROWS && (st->scroll_offset + i) < tab->field_count; i++) {
        int fidx = st->scroll_offset + i;
        const SchemaField *f = &tab->fields[fidx];
        int fy = PG_CONTENT_TOP + i * (PG_ROW_H + PG_ROW_GAP);
        int is_sel = (fidx == st->selected_field);
        int is_mod = pgsettings_is_modified(settings, schema, f->id);
        pg_draw_field(f, settings, content_x, fy, content_w, is_sel, is_mod);
    }

    /* Draw description for selected field */
    if (st->selected_field >= 0 && st->selected_field < tab->field_count) {
        const SchemaField *sf = &tab->fields[st->selected_field];
        if (sf->description[0]) {
            int desc_y = PG_CONTENT_TOP + PG_VISIBLE_ROWS * (PG_ROW_H + PG_ROW_GAP) + 8;
            if (desc_y + PG_DESC_H < PG_CONTENT_BOT) {
                pg_draw_description(sf, content_x, desc_y, content_w);
            }
        }
    }

    pg_draw_scrollbar(schema, st->selected_tab, st->scroll_offset);
    pg_draw_footer(st->dirty, st->show_confirm, st->confirm_sel);
}

/* ---------- Input handling ---------- */

static void change_field_value(const SchemaField *f, GameSettings *gs, int direction) {
    if (!f || !gs) return;

    if (f->type == FIELD_SELECT) {
        const char *cur = pgsettings_get_str(gs, f->id);
        int idx = -1, i;
        for (i = 0; i < f->option_count; i++) {
            if (strcmp(f->options[i].key, cur) == 0) { idx = i; break; }
        }
        if (idx < 0) idx = 0;
        idx += direction;
        if (idx < 0) idx = f->option_count - 1;
        if (idx >= f->option_count) idx = 0;
        pgsettings_set_str(gs, f->id, f->options[idx].key);
    }
    else if (f->type == FIELD_TOGGLE || f->type == FIELD_CHECKBOX) {
        int cur = pgsettings_get_int(gs, f->id);
        pgsettings_set_int(gs, f->id, cur ? 0 : 1);
    }
    else if (f->type == FIELD_SLIDER) {
        int cur = pgsettings_get_int(gs, f->id);
        cur += direction * f->step;
        if (cur < f->min) cur = f->min;
        if (cur > f->max) cur = f->max;
        pgsettings_set_int(gs, f->id, cur);
    }
}

static void reset_field_to_default(const SchemaField *f, GameSettings *gs) {
    if (!f || !gs) return;
    if (f->type == FIELD_SELECT || f->type == FIELD_TEXT) {
        char def[PGSETTINGS_MAX_VALUE_LEN];
        schema_get_default_str(f, def, sizeof(def));
        pgsettings_set_str(gs, f->id, def);
    } else {
        pgsettings_set_int(gs, f->id, schema_get_default_int(f));
    }
}

int pgsettings_ui_handle_input(unsigned int pressed, unsigned int held,
                                const Schema *schema, GameSettings *settings,
                                PGSettingsUIState *st) {
    if (!st || !st->active) return 0;

    if (st->show_confirm) {
        if (pressed & ORBIS_PAD_BUTTON_LEFT) {
            st->confirm_sel = (st->confirm_sel - 1 + 3) % 3;
        }
        if (pressed & ORBIS_PAD_BUTTON_RIGHT) {
            st->confirm_sel = (st->confirm_sel + 1) % 3;
        }
        if (pressed & ORBIS_PAD_BUTTON_CROSS) {
            if (st->confirm_sel == 0) {
                /* Save & Exit */
                pgsettings_save(st->disc_id, settings, schema);
                st->dirty = 0;
                st->show_confirm = 0;
                st->active = 0;
            } else if (st->confirm_sel == 1) {
                /* Discard & Exit */
                pgsettings_load(st->disc_id, schema, settings);
                st->dirty = 0;
                st->show_confirm = 0;
                st->active = 0;
            } else {
                /* Cancel */
                st->show_confirm = 0;
            }
        }
        if (pressed & ORBIS_PAD_BUTTON_CIRCLE) {
            st->show_confirm = 0;
        }
        return 1;
    }

    const SchemaTab *tab = &schema->tabs[st->selected_tab];

    if (pressed & ORBIS_PAD_BUTTON_UP) {
        st->selected_field = (st->selected_field - 1 + tab->field_count) % tab->field_count;
    }
    if (pressed & ORBIS_PAD_BUTTON_DOWN) {
        st->selected_field = (st->selected_field + 1) % tab->field_count;
    }

    if (pressed & ORBIS_PAD_BUTTON_LEFT) {
        if (st->selected_field >= 0 && st->selected_field < tab->field_count) {
            change_field_value(&tab->fields[st->selected_field], settings, -1);
            st->dirty = pgsettings_any_modified(settings, schema);
        }
    }
    if (pressed & ORBIS_PAD_BUTTON_RIGHT) {
        if (st->selected_field >= 0 && st->selected_field < tab->field_count) {
            change_field_value(&tab->fields[st->selected_field], settings, 1);
            st->dirty = pgsettings_any_modified(settings, schema);
        }
    }

    if (pressed & ORBIS_PAD_BUTTON_L1) {
        st->selected_tab = (st->selected_tab - 1 + schema->tab_count) % schema->tab_count;
        st->selected_field = 0;
        st->scroll_offset = 0;
    }
    if (pressed & ORBIS_PAD_BUTTON_R1) {
        st->selected_tab = (st->selected_tab + 1) % schema->tab_count;
        st->selected_field = 0;
        st->scroll_offset = 0;
    }

    if (pressed & ORBIS_PAD_BUTTON_CROSS) {
        if (st->selected_field >= 0 && st->selected_field < tab->field_count) {
            change_field_value(&tab->fields[st->selected_field], settings, 1);
            st->dirty = pgsettings_any_modified(settings, schema);
        }
    }

    if (pressed & ORBIS_PAD_BUTTON_TRIANGLE) {
        if (st->selected_field >= 0 && st->selected_field < tab->field_count) {
            reset_field_to_default(&tab->fields[st->selected_field], settings);
            st->dirty = pgsettings_any_modified(settings, schema);
        }
    }

    if (pressed & ORBIS_PAD_BUTTON_SQUARE) {
        /* Reset entire tab to defaults */
        int i;
        for (i = 0; i < tab->field_count; i++) {
            reset_field_to_default(&tab->fields[i], settings);
        }
        st->dirty = pgsettings_any_modified(settings, schema);
    }

    if (pressed & ORBIS_PAD_BUTTON_CIRCLE) {
        if (st->dirty) {
            st->show_confirm = 1;
            st->confirm_sel = 0;
        } else {
            st->active = 0;
        }
    }

    return 1;
}

void pgsettings_ui_init(PGSettingsUIState *st, const char *game_name, const char *disc_id) {
    if (!st) return;
    memset(st, 0, sizeof(PGSettingsUIState));
    st->active = 1;
    st->selected_tab = 0;
    st->selected_field = 0;
    st->scroll_offset = 0;
    st->dirty = 0;
    st->show_confirm = 0;
    st->confirm_sel = 0;
    if (game_name) {
        strncpy(st->game_name, game_name, sizeof(st->game_name) - 1);
        st->game_name[sizeof(st->game_name) - 1] = '\0';
    }
    if (disc_id) {
        strncpy(st->disc_id, disc_id, sizeof(st->disc_id) - 1);
        st->disc_id[sizeof(st->disc_id) - 1] = '\0';
    }
}

void pgsettings_ui_get_path(const char *disc_id, char *out, size_t out_len) {
    if (!disc_id || !out || out_len == 0) return;
    snprintf(out, out_len, "%s/gamesettings/%s.json", g_settings.work_path, disc_id);
}
