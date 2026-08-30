#include "pgsettings_ui.h"
#include "video.h"
#include "font.h"
#include "colors.h"
#include "settings.h"
#include <string.h>
#include <orbis/Pad.h>
#include <stdio.h>
#include <math.h>

/* ---------- Full-page layout constants ---------- */
#define PG_MARGIN       40
#define PG_HEADER_H     70
#define PG_TAB_H        50
#define PG_TAB_GAP      6
#define PG_FOOTER_H     60
#define PG_ROW_H        58
#define PG_ROW_GAP      3
#define PG_DESC_H       70
#define PG_SCROLLBAR_W  5

#define PG_CONTENT_TOP  (PG_HEADER_H + PG_TAB_H + 20)
#define PG_CONTENT_BOT  (SCREEN_HEIGHT - PG_FOOTER_H - 20)
#define PG_CONTENT_H    (PG_CONTENT_BOT - PG_CONTENT_TOP)
#define PG_VISIBLE_ROWS (PG_CONTENT_H / (PG_ROW_H + PG_ROW_GAP))

#define PG_DROPDOWN_W   500
#define PG_DROPDOWN_H   500
#define PG_DROPDOWN_X   ((SCREEN_WIDTH - PG_DROPDOWN_W) / 2)
#define PG_DROPDOWN_Y   ((SCREEN_HEIGHT - PG_DROPDOWN_H) / 2)
#define PG_DROPDOWN_ROW 44

/* ---------- Helpers ---------- */
static void pg_draw_btn_hint(int x, int y, const char *btn, const char *lbl, uint32_t c) {
    int bw = font_text_width(btn, 20) + 12;
    int bh = 28;
    draw_rounded_rect(x, y, bw, bh, 4, COLOR_BORDER);
    draw_rounded_rect(x + 1, y + 1, bw - 2, bh - 2, 3, COLOR_CARD);
    draw_text(x + 6, y + 3, btn, c, 20);
    draw_text(x + bw + 8, y + 3, lbl, COLOR_TEXT, 20);
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
        double v = pgsettings_get_double(gs, f->id);
        if (f->step_f < 1.0)
            snprintf(buf, sizeof(buf), "%.1f", v);
        else
            snprintf(buf, sizeof(buf), "%.0f", v);
        return buf;
    }
    else if (f->type == FIELD_TEXT) {
        const char *val = pgsettings_get_str(gs, f->id);
        return val[0] ? val : "(empty)";
    }
    return "";
}

/* ---------- Slider (improved) ---------- */
static void pg_draw_slider(int x, int y, int w, int h, double min, double max, double step,
                            double value, int is_sel) {
    int track_y = y + h / 2 - 2;
    int track_w = w - 20;

    /* Track background */
    draw_rounded_rect(x, track_y, track_w, 4, 2, is_sel ? COLOR_BORDER : COLOR_BORDER);

    /* Fill */
    if (max > min) {
        double ratio = (value - min) / (max - min);
        if (ratio < 0.0) ratio = 0.0;
        if (ratio > 1.0) ratio = 1.0;
        int fill_w = (int)(track_w * ratio);
        if (fill_w > 0)
            draw_rounded_rect(x, track_y, fill_w, 4, 2, is_sel ? COLOR_ACCENT : COLOR_DIM);
    }

    /* Thumb */
    double ratio = (max > min) ? (value - min) / (max - min) : 0.0;
    if (ratio < 0.0) ratio = 0.0;
    if (ratio > 1.0) ratio = 1.0;
    int thumb_x = x + (int)(track_w * ratio) - 7;
    if (thumb_x < x - 3) thumb_x = x - 3;
    if (thumb_x > x + track_w - 11) thumb_x = x + track_w - 11;

    uint32_t thumb_c = is_sel ? COLOR_GOLD : COLOR_DIM;
    draw_rounded_rect(thumb_x, track_y - 6, 14, 16, 7, thumb_c);
    if (is_sel) {
        draw_rounded_rect(thumb_x + 3, track_y - 3, 8, 10, 4, COLOR_TEXT);
    }
}

/* ---------- Field row ---------- */
static void pg_draw_field(const SchemaField *f, const GameSettings *gs,
                           int x, int y, int w, int is_sel, int is_modified) {
    /* Background */
    if (is_sel) {
        draw_rounded_rect(x, y, w, PG_ROW_H, 6, COLOR_CARD_SEL);
        draw_rect(x, y + 4, 3, PG_ROW_H - 8, COLOR_ACCENT);
    } else if (is_modified) {
        draw_rounded_rect(x, y, w, PG_ROW_H, 6, COLOR_CARD);
    }

    /* Label */
    uint32_t label_c = is_sel ? COLOR_TEXT : (is_modified ? COLOR_GOLD : COLOR_DIM);
    draw_text(x + 14, y + 14, f->label, label_c, 26);

    /* Value (right-aligned) */
    int vx = x + w - 300;
    int vw = 280;

    if (f->type == FIELD_SLIDER) {
        pg_draw_slider(vx, y + 6, vw, PG_ROW_H - 12,
                        f->min_f, f->max_f, f->step_f,
                        pgsettings_get_double(gs, f->id), is_sel);
        /* Numeric value to the right of slider */
        const char *num = get_field_display_value(f, gs);
        int nw = font_text_width(num, 20);
        draw_text(x + w - nw - 10, y + 16, num, is_sel ? COLOR_ACCENT : COLOR_DIM, 20);
    }
    else if (f->type == FIELD_TOGGLE || f->type == FIELD_CHECKBOX) {
        int on = pgsettings_get_int(gs, f->id);
        const char *txt = on ? "[ON]" : "[OFF]";
        uint32_t tc = on ? (is_sel ? COLOR_SUCCESS : COLOR_DIM) : (is_sel ? COLOR_ERROR : COLOR_MUTED);
        int tw = font_text_width(txt, 24);
        draw_text(x + w - tw - 14, y + 14, txt, tc, 24);
    }
    else if (f->type == FIELD_SELECT) {
        const char *val = get_field_display_value(f, gs);
        char buf[128];
        snprintf(buf, sizeof(buf), "[%s]", val);
        uint32_t tc = is_sel ? COLOR_GOLD : COLOR_DIM;
        int tw = font_text_width(buf, 24);
        draw_text(x + w - tw - 14, y + 14, buf, tc, 24);
        if (is_sel) {
            draw_text(x + w - tw - 36, y + 14, "\xE2\x96\xBC", COLOR_ACCENT, 18);
        }
    }
    else if (f->type == FIELD_TEXT) {
        const char *val = pgsettings_get_str(gs, f->id);
        char buf[128];
        snprintf(buf, sizeof(buf), "\"%s\"", val[0] ? val : "");
        uint32_t tc = is_sel ? COLOR_GOLD : COLOR_DIM;
        int tw = font_text_width(buf, 24);
        if (tw > vw) tw = vw;
        draw_text(x + w - tw - 14, y + 14, buf, tc, 24);
    }
}

/* ---------- Description box ---------- */
static void pg_draw_description(const SchemaField *f, int x, int y, int w) {
    if (!f || !f->description[0] || !f->show_description) return;

    draw_rounded_rect(x, y, w, PG_DESC_H, 6, COLOR_PANEL);
    draw_text(x + 12, y + 8, "\xF0\x9F\x92\xA1", COLOR_GOLD, 20);

    /* Simple word wrap */
    char buf[512];
    strncpy(buf, f->description, sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = '\0';

    int line_h = 20;
    int max_lines = (PG_DESC_H - 16) / line_h;
    if (max_lines < 1) max_lines = 1;

    char line[256] = "";
    char *word = strtok(buf, " \n\r\t");
    int cy = y + 10;
    int lines = 0;
    int text_x = x + 40;
    int text_w = w - 56;

    while (word && lines < max_lines) {
        char test[512];
        if (line[0]) snprintf(test, sizeof(test), "%s %s", line, word);
        else snprintf(test, sizeof(test), "%s", word);

        if (font_text_width(test, 18) <= text_w) {
            snprintf(line, sizeof(line), "%s", test);
        } else {
            if (line[0]) {
                draw_text(text_x, cy, line, COLOR_DIM, 18);
                cy += line_h;
                lines++;
            }
            snprintf(line, sizeof(line), "%s", word);
        }
        word = strtok(NULL, " \n\r\t");
    }
    if (line[0] && lines < max_lines) {
        draw_text(text_x, cy, line, COLOR_DIM, 18);
    }
}

/* ---------- Scrollbar ---------- */
static void pg_draw_scrollbar(int total, int visible, int offset, int x, int y, int h) {
    if (total <= visible) return;
    draw_rect(x, y, PG_SCROLLBAR_W, h, COLOR_CARD);
    int thumb = h * visible / total;
    if (thumb < 20) thumb = 20;
    int ty = y + (h - thumb) * offset / (total - visible);
    draw_rect(x, ty, PG_SCROLLBAR_W, thumb, COLOR_ACCENT);
}

/* ---------- Dropdown overlay ---------- */
static void pg_draw_dropdown(const SchemaField *f, const GameSettings *gs, PGSettingsUIState *st) {
    if (!f || f->type != FIELD_SELECT || !st->dropdown_active) return;

    /* Dim background */
    draw_rect(0, 0, SCREEN_WIDTH, SCREEN_HEIGHT, 0xD0000000);

    /* Panel */
    draw_rounded_rect(PG_DROPDOWN_X - 4, PG_DROPDOWN_Y - 4, PG_DROPDOWN_W + 8, PG_DROPDOWN_H + 8, 12, COLOR_ACCENT);
    draw_rounded_rect(PG_DROPDOWN_X, PG_DROPDOWN_Y, PG_DROPDOWN_W, PG_DROPDOWN_H, 10, COLOR_PANEL);
    draw_rounded_rect(PG_DROPDOWN_X + 2, PG_DROPDOWN_Y + 2, PG_DROPDOWN_W - 4, PG_DROPDOWN_H - 4, 8, COLOR_BG);

    /* Title */
    char title[128];
    snprintf(title, sizeof(title), "Select: %s", f->label);
    int tw = font_text_width(title, 28);
    draw_text(PG_DROPDOWN_X + (PG_DROPDOWN_W - tw) / 2, PG_DROPDOWN_Y + 14, title, COLOR_GOLD, 28);
    draw_rect(PG_DROPDOWN_X + 20, PG_DROPDOWN_Y + 50, PG_DROPDOWN_W - 40, 2, COLOR_BORDER);

    /* Options */
    int list_top = PG_DROPDOWN_Y + 64;
    int list_h = PG_DROPDOWN_H - 80;
    int visible_opts = list_h / PG_DROPDOWN_ROW;
    if (visible_opts < 1) visible_opts = 1;

    /* Auto-scroll dropdown */
    if (st->dropdown_sel < st->dropdown_scroll)
        st->dropdown_scroll = st->dropdown_sel;
    if (st->dropdown_sel >= st->dropdown_scroll + visible_opts)
        st->dropdown_scroll = st->dropdown_sel - visible_opts + 1;
    if (st->dropdown_scroll < 0) st->dropdown_scroll = 0;
    if (st->dropdown_scroll > f->option_count - visible_opts)
        st->dropdown_scroll = f->option_count - visible_opts;
    if (st->dropdown_scroll < 0) st->dropdown_scroll = 0;

    int i;
    for (i = 0; i < visible_opts && (st->dropdown_scroll + i) < f->option_count; i++) {
        int idx = st->dropdown_scroll + i;
        int oy = list_top + i * PG_DROPDOWN_ROW;
        int is_sel = (idx == st->dropdown_sel);

        if (is_sel) {
            draw_rounded_rect(PG_DROPDOWN_X + 16, oy, PG_DROPDOWN_W - 32, PG_DROPDOWN_ROW - 2, 6, COLOR_CARD_SEL);
        }

        /* Checkmark for current value */
        const char *cur_val = pgsettings_get_str(gs, f->id);
        int is_current = (strcmp(f->options[idx].key, cur_val) == 0);

        int tx = PG_DROPDOWN_X + 30;
        if (is_current) {
            draw_text(tx, oy + 8, "\xE2\x9C\x93", COLOR_SUCCESS, 22);
            tx += 28;
        }

        draw_text(tx, oy + 8, f->options[idx].label, is_sel ? COLOR_TEXT : COLOR_DIM, 24);
    }

    /* Dropdown scrollbar */
    if (f->option_count > visible_opts) {
        pg_draw_scrollbar(f->option_count, visible_opts, st->dropdown_scroll,
                          PG_DROPDOWN_X + PG_DROPDOWN_W - 20, list_top, list_h);
    }

    /* Footer hint */
    draw_text(PG_DROPDOWN_X + 20, PG_DROPDOWN_Y + PG_DROPDOWN_H - 32,
              "[X] Select   [O] Cancel", COLOR_DIM, 18);
}

/* ---------- Header, Tabs, Footer ---------- */
static void pg_draw_header(const char *game_name) {
    char title[512];
    snprintf(title, sizeof(title), "PS2 Game Settings: %s", game_name);
    int tw = font_text_width_slot(title, 32, FONT_SLOT_TITLE);
    int tx = (SCREEN_WIDTH - tw) / 2;
    if (tx < PG_MARGIN) tx = PG_MARGIN;
    draw_text_slot(tx, 16, title, COLOR_GOLD, 32, FONT_SLOT_TITLE);
    draw_rect(PG_MARGIN, PG_HEADER_H - 4, SCREEN_WIDTH - PG_MARGIN * 2, 2, COLOR_BORDER);
}

static void pg_draw_tabs(const Schema *schema, int selected_tab) {
    int x = PG_MARGIN;
    int y = PG_HEADER_H + 6;
    int i;
    for (i = 0; i < schema->tab_count; i++) {
        const SchemaTab *t = &schema->tabs[i];
        char label[64];
        snprintf(label, sizeof(label), "%s %s", t->icon[0] ? t->icon : "", t->label);
        int lw = font_text_width(label, 22);
        int tw = lw + 24;
        int is_sel = (i == selected_tab);

        uint32_t bg = is_sel ? COLOR_ACCENT : COLOR_CARD;
        uint32_t fg = is_sel ? COLOR_TEXT : COLOR_DIM;

        draw_rounded_rect(x, y, tw, PG_TAB_H - 6, 8, bg);
        draw_text(x + 12, y + 10, label, fg, 22);
        x += tw + PG_TAB_GAP;
    }
    draw_rect(PG_MARGIN, y + PG_TAB_H - 2, SCREEN_WIDTH - PG_MARGIN * 2, 2, COLOR_BORDER);
}

static void pg_draw_footer(int dirty, int show_confirm, int confirm_sel) {
    int y = SCREEN_HEIGHT - PG_FOOTER_H + 8;
    draw_rect(0, SCREEN_HEIGHT - PG_FOOTER_H, SCREEN_WIDTH, 2, COLOR_BORDER);

    if (show_confirm) {
        const char *opts[3] = {"Save & Exit", "Discard Changes", "Cancel"};
        uint32_t colors[3] = {COLOR_SUCCESS, COLOR_ERROR, COLOR_DIM};
        int x = (SCREEN_WIDTH - 600) / 2;
        int i;
        for (i = 0; i < 3; i++) {
            uint32_t c = (i == confirm_sel) ? COLOR_ACCENT : COLOR_CARD;
            int tw = font_text_width(opts[i], 22);
            draw_rounded_rect(x, y, tw + 20, 36, 5, c);
            draw_text(x + 10, y + 5, opts[i], colors[i], 22);
            x += tw + 40;
        }
    } else {
        int x = PG_MARGIN;
        pg_draw_btn_hint(x, y, "^v", "Navigate", COLOR_DIM); x += 180;
        pg_draw_btn_hint(x, y, "<>", "Change", COLOR_DIM); x += 180;
        pg_draw_btn_hint(x, y, "L1/R1", "Tabs", COLOR_DIM); x += 200;
        pg_draw_btn_hint(x, y, "X", "Select", COLOR_ACCENT); x += 200;
        pg_draw_btn_hint(x, y, "TRI", "Reset", COLOR_DIM); x += 200;
        pg_draw_btn_hint(x, y, "O", dirty ? "Close*" : "Close", dirty ? COLOR_ERROR : COLOR_DIM);

        if (dirty) {
            const char *ind = "\xE2\x97\x8F UNSAVED";
            int iw = font_text_width(ind, 18);
            draw_text(SCREEN_WIDTH - iw - PG_MARGIN, y + 4, ind, COLOR_ERROR, 18);
        }
    }
}

/* ---------- Public draw function ---------- */
void draw_pgsettings_ui(const Schema *schema, GameSettings *settings,
                        PGSettingsUIState *st) {
    if (!schema || !settings || !st || !st->active) return;

    /* Full-screen background clear */
    draw_rect(0, 0, SCREEN_WIDTH, SCREEN_HEIGHT, COLOR_BG);

    pg_draw_header(st->game_name);
    pg_draw_tabs(schema, st->selected_tab);

    const SchemaTab *tab = &schema->tabs[st->selected_tab];
    int content_x = PG_MARGIN;
    int content_w = SCREEN_WIDTH - PG_MARGIN * 2 - PG_SCROLLBAR_W - 12;

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

    /* Description for selected field */
    if (st->selected_field >= 0 && st->selected_field < tab->field_count) {
        const SchemaField *sf = &tab->fields[st->selected_field];
        if (sf->show_description && sf->description[0]) {
            int desc_y = PG_CONTENT_TOP + PG_VISIBLE_ROWS * (PG_ROW_H + PG_ROW_GAP) + 6;
            if (desc_y + PG_DESC_H < PG_CONTENT_BOT) {
                pg_draw_description(sf, content_x, desc_y, content_w);
            }
        }
    }

    /* Scrollbar */
    pg_draw_scrollbar(tab->field_count, PG_VISIBLE_ROWS, st->scroll_offset,
                      SCREEN_WIDTH - PG_MARGIN - PG_SCROLLBAR_W, PG_CONTENT_TOP, PG_CONTENT_H);

    pg_draw_footer(st->dirty, st->show_confirm, st->confirm_sel);

    /* Dropdown overlay (on top of everything) */
    if (st->dropdown_active && st->selected_field >= 0 && st->selected_field < tab->field_count) {
        pg_draw_dropdown(&tab->fields[st->selected_field], settings, st);
    }
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
        double cur = pgsettings_get_double(gs, f->id);
        double next = cur + direction * f->step_f;
        if (next < f->min_f) next = f->min_f;
        if (next > f->max_f) next = f->max_f;
        /* Round to nearest step */
        if (f->step_f > 0) {
            next = f->min_f + round((next - f->min_f) / f->step_f) * f->step_f;
        }
        pgsettings_set_double(gs, f->id, next);
    }
}

static void reset_field_to_default(const SchemaField *f, GameSettings *gs) {
    if (!f || !gs) return;
    if (f->type == FIELD_SELECT || f->type == FIELD_TEXT) {
        char def[PGSETTINGS_MAX_VALUE_LEN];
        schema_get_default_str(f, def, sizeof(def));
        pgsettings_set_str(gs, f->id, def);
    } else if (f->type == FIELD_SLIDER) {
        pgsettings_set_double(gs, f->id, schema_get_default_double(f));
    } else {
        pgsettings_set_int(gs, f->id, schema_get_default_int(f));
    }
}

int pgsettings_ui_handle_input(unsigned int pressed, unsigned int held,
                                const Schema *schema, GameSettings *settings,
                                PGSettingsUIState *st) {
    if (!st || !st->active) return 0;

    const SchemaTab *tab = &schema->tabs[st->selected_tab];

    /* Dropdown mode takes priority */
    if (st->dropdown_active) {
        const SchemaField *f = &tab->fields[st->selected_field];
        if (pressed & ORBIS_PAD_BUTTON_UP) {
            st->dropdown_sel = (st->dropdown_sel - 1 + f->option_count) % f->option_count;
        }
        if (pressed & ORBIS_PAD_BUTTON_DOWN) {
            st->dropdown_sel = (st->dropdown_sel + 1) % f->option_count;
        }
        if (pressed & ORBIS_PAD_BUTTON_CROSS) {
            pgsettings_set_str(settings, f->id, f->options[st->dropdown_sel].key);
            st->dropdown_active = 0;
            st->dirty = pgsettings_any_modified(settings, schema);
        }
        if (pressed & ORBIS_PAD_BUTTON_CIRCLE) {
            st->dropdown_active = 0;
        }
        return 1;
    }

    /* Confirm dialog */
    if (st->show_confirm) {
        if (pressed & ORBIS_PAD_BUTTON_LEFT) {
            st->confirm_sel = (st->confirm_sel - 1 + 3) % 3;
        }
        if (pressed & ORBIS_PAD_BUTTON_RIGHT) {
            st->confirm_sel = (st->confirm_sel + 1) % 3;
        }
        if (pressed & ORBIS_PAD_BUTTON_CROSS) {
            if (st->confirm_sel == 0) {
                pgsettings_save(st->disc_id, settings, schema);
                st->dirty = 0;
                st->show_confirm = 0;
                st->active = 0;
            } else if (st->confirm_sel == 1) {
                pgsettings_load(st->disc_id, schema, settings);
                st->dirty = 0;
                st->show_confirm = 0;
                st->active = 0;
            } else {
                st->show_confirm = 0;
            }
        }
        if (pressed & ORBIS_PAD_BUTTON_CIRCLE) {
            st->show_confirm = 0;
        }
        return 1;
    }

    /* Normal navigation */
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
            const SchemaField *f = &tab->fields[st->selected_field];
            if (f->type == FIELD_SELECT && f->option_count > 0) {
                /* Open dropdown */
                st->dropdown_active = 1;
                const char *cur = pgsettings_get_str(settings, f->id);
                st->dropdown_sel = 0;
                int i;
                for (i = 0; i < f->option_count; i++) {
                    if (strcmp(f->options[i].key, cur) == 0) {
                        st->dropdown_sel = i;
                        break;
                    }
                }
                st->dropdown_scroll = 0;
            } else {
                change_field_value(f, settings, 1);
                st->dirty = pgsettings_any_modified(settings, schema);
            }
        }
    }

    if (pressed & ORBIS_PAD_BUTTON_TRIANGLE) {
        if (st->selected_field >= 0 && st->selected_field < tab->field_count) {
            reset_field_to_default(&tab->fields[st->selected_field], settings);
            st->dirty = pgsettings_any_modified(settings, schema);
        }
    }

    if (pressed & ORBIS_PAD_BUTTON_SQUARE) {
        /* Reset entire tab */
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
    st->dropdown_active = 0;
    st->dropdown_sel = 0;
    st->dropdown_scroll = 0;
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
