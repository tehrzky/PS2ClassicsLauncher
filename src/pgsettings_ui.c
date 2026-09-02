#include "pgsettings_ui.h"
#include "pgsettings_textview.h"
#include "upload_qr_ui.h"
#include "game.h"
#include "video.h"
#include "font.h"
#include "colors.h"
#include "settings.h"
#include <string.h>
#include <orbis/Pad.h>
#include <stdio.h>
#include <math.h>

/* Frames LEFT/RIGHT must be held on a slider before hold-to-fast-scrub
 * kicks in, and how it accelerates after that. Tuned for a 60fps loop;
 * scale these if the settings screen runs at a different rate.
 * Cut down twice now per feedback -- first 18->6, now down to near-
 * immediate (2 frames, ~33ms) since it still felt delayed at 6. */
#define PG_HOLD_DELAY_FRAMES   2    /* ~0.03s -- basically starts on the next frame */
#define PG_HOLD_RAMP_FRAMES    3    /* every N frames past the delay, repeat gets faster */
#define PG_HOLD_MIN_INTERVAL   1    /* fastest repeat rate, in frames between steps */
#define PG_HOLD_MULT_FRAMES    12   /* every N frames past the delay, step size doubles-ish */
#define PG_HOLD_MAX_MULT       8    /* cap on how many normal steps one repeat applies */

/* NOTE on persistence: per-game field edits ARE saved (gamesettings/
 * <discid>.json, via pgsettings_save() on CIRCLE + confirm) -- separate
 * from that, gamesettings/<discid>_schema.json is a *schema* override
 * (which fields/tabs exist at all for this game), merged in by the
 * caller before this screen opens. See schema_merge_game_override() and
 * main.c. */

/* ---------- Consistent Screen Layout & Safe Area ---------- */
#define SCREEN_WIDTH        1920
#define SCREEN_HEIGHT       1080

#define SAFE_X              64
#define SAFE_Y              40
#define SAFE_X1             (SCREEN_WIDTH - SAFE_X)
#define SAFE_Y1             (SCREEN_HEIGHT - SAFE_Y)

#define HEADER_H            100
#define FOOTER_H            70
#define PANEL_BORDER        2
#define ROUND_RADIUS        8
#define ROUND_RADIUS_SMALL  6
#define ROUND_RADIUS_LARGE  16

#define PG_TAB_H            44
#define PG_TAB_GAP          8
#define PG_ROW_H            58
#define PG_ROW_GAP          4
#define PG_DESC_H           142  /* bigger box for larger text; costs 1 fewer visible row, on purpose */
#define PG_SCROLLBAR_W      4

#define PG_CONTENT_TOP      (HEADER_H + PG_TAB_H + 24)
#define PG_CONTENT_BOT      (SCREEN_HEIGHT - FOOTER_H - 30)
#define PG_CONTENT_H        (PG_CONTENT_BOT - PG_CONTENT_TOP)
#define PG_VISIBLE_ROWS     ((PG_CONTENT_H - PG_DESC_H - 12) / (PG_ROW_H + PG_ROW_GAP))

#define PG_DROPDOWN_W       600
#define PG_DROPDOWN_H       480
#define PG_DROPDOWN_X       ((SCREEN_WIDTH - PG_DROPDOWN_W) / 2)
#define PG_DROPDOWN_Y       ((SCREEN_HEIGHT - PG_DROPDOWN_H) / 2)
#define PG_DROPDOWN_ROW     48

/* ---------- Helpers ---------- */
static uint32_t pg_get_panel_color(void) {
    int a = g_settings.panel_opacity * 255 / 100;
    return (a << 24) | (COLOR_PANEL & 0x00FFFFFF);
}

static void pg_draw_btn_hint(int x, int y, const char *btn, const char *lbl, uint32_t c) {
    int bw = font_text_width(btn, 24) + 16;
    int bh = 36;
    draw_rounded_rect(x, y, bw, bh, 6, COLOR_BORDER);
    draw_rounded_rect(x + 1, y + 1, bw - 2, bh - 2, 5, COLOR_CARD);
    draw_text(x + 8, y + 6, btn, c, 24);
    draw_text(x + bw + 14, y + 6, lbl, COLOR_TEXT, 24);
}

static const char *get_field_display_value(const SchemaField *f, const GameSettings *gs) {
    static char buf[256];
    if (f->type == FIELD_SELECT) {
        const char *val = pgsettings_get_str(gs, f->id);
        for (int i = 0; i < f->option_count; i++) {
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

/* ---------- Components ---------- */
static void pg_draw_slider(int x, int y, int w, int h, double min, double max, double step,
                            double value, int is_sel) {
    int track_y = y + h / 2 - 2;
    int track_w = w - 20;

    draw_rounded_rect(x, track_y, track_w, 4, 2, COLOR_BORDER);

    if (max > min) {
        double ratio = (value - min) / (max - min);
        if (ratio < 0.0) ratio = 0.0;
        if (ratio > 1.0) ratio = 1.0;
        int fill_w = (int)(track_w * ratio);
        if (fill_w > 0)
            draw_rounded_rect(x, track_y, fill_w, 4, 2, is_sel ? COLOR_ACCENT : COLOR_DIM);
    }

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

static void pg_draw_field(const SchemaField *f, const GameSettings *gs,
                           int x, int y, int w, int is_sel, int is_modified) {
    if (is_sel) {
        draw_rounded_rect(x, y, w, PG_ROW_H, 8, COLOR_CARD_SEL);
        draw_rect(x + 2, y + 6, 4, PG_ROW_H - 12, COLOR_ACCENT);
    } else if (is_modified) {
        draw_rounded_rect(x, y, w, PG_ROW_H, 8, COLOR_CARD);
    }

    /* Row background/left-accent already carries the "selected" signal,
     * so the label itself just stays white/legible instead of dimming --
     * only actually-modified (non-selected) rows get the gold tint. */
    uint32_t label_c = is_modified && !is_sel ? COLOR_GOLD : COLOR_TEXT;
    draw_text(x + 20, y + 14, f->label, label_c, 28);

    int vx = x + w - 320;
    int vw = 300;

    if (f->type == FIELD_SLIDER) {
        pg_draw_slider(vx, y + 6, vw - 60, PG_ROW_H - 12,
                        f->min_f, f->max_f, f->step_f,
                        pgsettings_get_double(gs, f->id), is_sel);
        const char *num = get_field_display_value(f, gs);
        int nw = font_text_width(num, 24);
        draw_text(x + w - nw - 20, y + 14, num, is_sel ? COLOR_ACCENT : COLOR_DIM, 24);
    }
    else if (f->type == FIELD_TOGGLE || f->type == FIELD_CHECKBOX) {
        int on = pgsettings_get_int(gs, f->id);
        const char *txt = on ? "ON" : "OFF";
        uint32_t tc = is_sel ? COLOR_ACCENT : COLOR_DIM;
        int tw = font_text_width(txt, 26);
        draw_text(x + w - tw - 20, y + 14, txt, tc, 26);
    }
    else if (f->type == FIELD_SELECT) {
        const char *val = get_field_display_value(f, gs);
        uint32_t tc = is_sel ? COLOR_ACCENT : COLOR_DIM;
        int tw = font_text_width(val, 26);
        draw_text(x + w - tw - 20, y + 14, val, tc, 26);
    }
    else if (f->type == FIELD_TEXT) {
        const char *val = pgsettings_get_str(gs, f->id);
        uint32_t tc = is_sel ? COLOR_ACCENT : COLOR_DIM;
        int tw = font_text_width(val, 26);
        if (tw > vw) tw = vw;
        draw_text(x + w - tw - 20, y + 14, val, tc, 26);
    }
}

static void pg_draw_description(const SchemaField *f, int x, int y, int w) {
    if (!f || !f->description[0] || !f->show_description) return;

    draw_rounded_rect(x, y, w, PG_DESC_H, ROUND_RADIUS, COLOR_BORDER);
    draw_rounded_rect(x + 1, y + 1, w - 2, PG_DESC_H - 2, ROUND_RADIUS_SMALL, COLOR_CARD);

    draw_text_slot(x + 18, y + 12, "INFO:", COLOR_GOLD, 24, FONT_SLOT_BOLD);

    char buf[512];
    strncpy(buf, f->description, sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = '\0';

    int font_size = 24;
    int line_h = 30;
    int max_lines = (PG_DESC_H - 20) / line_h;
    char line[256] = "";
    char *word = strtok(buf, " \n\r\t");
    int cy = y + 46;
    int lines = 0;
    int text_x = x + 18;
    int text_w = w - 36;

    while (word && lines < max_lines) {
        char test[512];
        if (line[0]) snprintf(test, sizeof(test), "%s %s", line, word);
        else snprintf(test, sizeof(test), "%s", word);

        if (font_text_width(test, font_size) <= text_w) {
            snprintf(line, sizeof(line), "%s", test);
        } else {
            if (line[0]) {
                draw_text(text_x, cy, line, COLOR_TEXT, font_size);
                cy += line_h;
                lines++;
            }
            snprintf(line, sizeof(line), "%s", word);
        }
        word = strtok(NULL, " \n\r\t");
    }
    if (line[0] && lines < max_lines) {
        draw_text(text_x, cy, line, COLOR_TEXT, font_size);
    }
}

static void pg_draw_scrollbar(int total, int visible, int offset, int x, int y, int h) {
    if (total <= visible) return;
    draw_rect(x, y, PG_SCROLLBAR_W, h, COLOR_CARD);
    int thumb = h * visible / total;
    if (thumb < 24) thumb = 24;
    int ty = y + (h - thumb) * offset / (total - visible);
    draw_rect(x, ty, PG_SCROLLBAR_W, thumb, COLOR_ACCENT);
}

static void pg_draw_dropdown(const SchemaField *f, const GameSettings *gs, PGSettingsUIState *st) {
    if (!f || f->type != FIELD_SELECT || !st->dropdown_active) return;

    draw_rect(0, 0, SCREEN_WIDTH, SCREEN_HEIGHT, 0xD0000000);

    draw_rounded_rect(PG_DROPDOWN_X, PG_DROPDOWN_Y, PG_DROPDOWN_W, PG_DROPDOWN_H, ROUND_RADIUS_LARGE, COLOR_PANEL);
    draw_rounded_rect(PG_DROPDOWN_X + PANEL_BORDER, PG_DROPDOWN_Y + PANEL_BORDER,
                      PG_DROPDOWN_W - PANEL_BORDER * 2, PG_DROPDOWN_H - PANEL_BORDER * 2,
                      ROUND_RADIUS_LARGE - PANEL_BORDER, COLOR_BG);

    char title[128];
    snprintf(title, sizeof(title), "Select: %s", f->label);
    int tw = font_text_width_slot(title, 32, FONT_SLOT_TITLE);
    draw_text_slot(PG_DROPDOWN_X + (PG_DROPDOWN_W - tw) / 2, PG_DROPDOWN_Y + 18, title, COLOR_GOLD, 32, FONT_SLOT_TITLE);
    draw_rect(PG_DROPDOWN_X + 20, PG_DROPDOWN_Y + 56, PG_DROPDOWN_W - 40, PANEL_BORDER, COLOR_BORDER);

    int list_top = PG_DROPDOWN_Y + 68;
    int list_h = PG_DROPDOWN_H - 120;
    int visible_opts = list_h / PG_DROPDOWN_ROW;
    if (visible_opts < 1) visible_opts = 1;

    if (st->dropdown_sel < st->dropdown_scroll)
        st->dropdown_scroll = st->dropdown_sel;
    if (st->dropdown_sel >= st->dropdown_scroll + visible_opts)
        st->dropdown_scroll = st->dropdown_sel - visible_opts + 1;
    if (st->dropdown_scroll < 0) st->dropdown_scroll = 0;
    if (st->dropdown_scroll > f->option_count - visible_opts)
        st->dropdown_scroll = f->option_count - visible_opts;
    if (st->dropdown_scroll < 0) st->dropdown_scroll = 0;

    for (int i = 0; i < visible_opts && (st->dropdown_scroll + i) < f->option_count; i++) {
        int idx = st->dropdown_scroll + i;
        int oy = list_top + i * PG_DROPDOWN_ROW;
        int is_sel = (idx == st->dropdown_sel);

        if (is_sel) {
            draw_rounded_rect(PG_DROPDOWN_X + 16, oy, PG_DROPDOWN_W - 32, PG_DROPDOWN_ROW - 4, 6, COLOR_CARD_SEL);
            draw_rect(PG_DROPDOWN_X + 16, oy + 4, 4, PG_DROPDOWN_ROW - 12, COLOR_ACCENT);
        }

        const char *cur_val = pgsettings_get_str(gs, f->id);
        int is_current = (strcmp(f->options[idx].key, cur_val) == 0);

        uint32_t tc = is_sel ? COLOR_TEXT : (is_current ? COLOR_GOLD : COLOR_DIM);
        draw_text(PG_DROPDOWN_X + 32, oy + 8, f->options[idx].label, tc, 26);
    }

    if (f->option_count > visible_opts) {
        pg_draw_scrollbar(f->option_count, visible_opts, st->dropdown_scroll,
                          PG_DROPDOWN_X + PG_DROPDOWN_W - 16, list_top, list_h);
    }

    int hy = PG_DROPDOWN_Y + PG_DROPDOWN_H - 44;
    draw_text(PG_DROPDOWN_X + 24, hy, "X Select     O Cancel", COLOR_DIM, 22);
}

/* ---------- Header, Tabs, Footer ---------- */
static void pg_truncate_to_width(char *text, int max_w, int size_px, int slot) {
    if (font_text_width_slot(text, size_px, slot) <= max_w) return;
    size_t len = strlen(text);
    while (len > 0) {
        text[len] = '\0';
        char test[520];
        snprintf(test, sizeof(test), "%s...", text);
        if (font_text_width_slot(test, size_px, slot) <= max_w) {
            strcpy(text, test);
            return;
        }
        len--;
    }
}

static void pg_draw_header(const char *game_name) {
    const char *right_label = "PER-GAME SETTINGS";
    int right_size = 32;
    int right_w = font_text_width_slot(right_label, right_size, FONT_SLOT_TITLE);

    int gap = 40;
    int max_title_w = (SAFE_X1 - gap - right_w) - SAFE_X;

    char title[512];
    snprintf(title, sizeof(title), "%s", game_name ? game_name : "");
    pg_truncate_to_width(title, max_title_w, 52, FONT_SLOT_TITLE);

    draw_text_slot(SAFE_X, SAFE_Y, title, COLOR_ACCENT, 52, FONT_SLOT_TITLE);
    draw_text_slot(SAFE_X + 1, SAFE_Y, title, COLOR_ACCENT, 52, FONT_SLOT_TITLE);

    draw_text_slot(SAFE_X1 - right_w, SAFE_Y + 8, right_label, COLOR_GOLD, right_size, FONT_SLOT_TITLE);

    draw_rect(SAFE_X, HEADER_H, SCREEN_WIDTH - (SAFE_X * 2), PANEL_BORDER, COLOR_ACCENT);
}

/* Darker than COLOR_ACCENT so the white tab label reads clearly against
 * it -- darkened once already (75% of accent), still wasn't enough, now
 * ~55% of accent. */
#define PG_TAB_SEL_BG 0xFF235D87

static void pg_draw_tabs(const Schema *schema, int selected_tab) {
    int x = SAFE_X;
    int y = HEADER_H + 12;

    for (int i = 0; i < schema->tab_count; i++) {
        const SchemaTab *t = &schema->tabs[i];
        char label[64];
        snprintf(label, sizeof(label), "%s %s", t->icon[0] ? t->icon : "", t->label);
        int tw = font_text_width(label, 24) + 28;
        int is_sel = (i == selected_tab);

        uint32_t bg = is_sel ? PG_TAB_SEL_BG : COLOR_CARD;
        uint32_t fg = is_sel ? COLOR_TEXT : COLOR_DIM;

        draw_rounded_rect(x, y, tw, PG_TAB_H, 6, bg);
        draw_text(x + 14, y + 8, label, fg, 24);
        x += tw + PG_TAB_GAP;
    }
}

static void pg_draw_footer(int dirty, int show_confirm, int confirm_sel) {
    int y = SCREEN_HEIGHT - FOOTER_H;
    draw_rect(0, y, SCREEN_WIDTH, FOOTER_H, COLOR_PANEL);
    draw_rect(0, y, SCREEN_WIDTH, PANEL_BORDER, COLOR_ACCENT);

    int hy = y + 16;
    int x = SAFE_X + 20;

    if (show_confirm) {
        const char *opts[3] = {"Save & Exit", "Discard", "Cancel"};
        uint32_t colors[3] = {COLOR_SUCCESS, COLOR_ERROR, COLOR_DIM};
        int cx = (SCREEN_WIDTH - 540) / 2;

        for (int i = 0; i < 3; i++) {
            uint32_t c = (i == confirm_sel) ? COLOR_ACCENT : COLOR_CARD;
            int tw = font_text_width(opts[i], 24);
            draw_rounded_rect(cx, hy, tw + 24, 36, 6, c);
            draw_text(cx + 12, hy + 6, opts[i], colors[i], 24);
            cx += tw + 44;
        }
    } else {
        pg_draw_btn_hint(x, hy, "^v", "Navigate", COLOR_DIM); x += 220;
        pg_draw_btn_hint(x, hy, "<>", "Change", COLOR_DIM); x += 220;
        pg_draw_btn_hint(x, hy, "L1/R1", "Tabs", COLOR_DIM); x += 200;
        pg_draw_btn_hint(x, hy, "X", "Select", COLOR_ACCENT); x += 200;
        pg_draw_btn_hint(x, hy, "TRI", "Reset", COLOR_DIM); x += 200;
        pg_draw_btn_hint(x, hy, "OPT", "Check Result", COLOR_DIM); x += 260;
        pg_draw_btn_hint(x, hy, "PAD", "Upload/Edit", COLOR_DIM); x += 240;
        pg_draw_btn_hint(x, hy, "O", dirty ? "Close*" : "Close", dirty ? COLOR_ERROR : COLOR_DIM);

        if (dirty) {
            const char *ind = "UNSAVED";
            int iw = font_text_width(ind, 22);
            draw_text(SAFE_X1 - iw, hy + 6, ind, COLOR_ERROR, 22);
        }
    }
}

/* ---------- Main Draw Router ---------- */
void draw_pgsettings_ui(const Schema *schema, GameSettings *settings,
                        PGSettingsUIState *st) {
    if (!schema || !settings || !st || !st->active) return;

    if (g_settings.wallpaper[0]) {
        cover_draw_wallpaper();
        if (g_settings.wallpaper_brightness < 100) {
            int alpha = (100 - g_settings.wallpaper_brightness) * 255 / 100;
            draw_rect(0, 0, SCREEN_WIDTH, SCREEN_HEIGHT, (alpha << 24));
        }
    } else {
        draw_rect(0, 0, SCREEN_WIDTH, SCREEN_HEIGHT, COLOR_BG);
    }

    pg_draw_header(st->game_name);
    pg_draw_tabs(schema, st->selected_tab);

    /* Panel Backdrop Container */
    int panel_w = SAFE_X1 - SAFE_X;
    int panel_h = PG_CONTENT_H + 20;
    int panel_y = PG_CONTENT_TOP - 10;

    draw_rounded_rect(SAFE_X, panel_y, panel_w, panel_h, ROUND_RADIUS, COLOR_GOLD);
    uint32_t panel_col = pg_get_panel_color();
    draw_rounded_rect(SAFE_X + PANEL_BORDER, panel_y + PANEL_BORDER,
                      panel_w - PANEL_BORDER * 2, panel_h - PANEL_BORDER * 2,
                      ROUND_RADIUS_SMALL, panel_col);

    const SchemaTab *tab = &schema->tabs[st->selected_tab];
    int content_x = SAFE_X + 16;
    int content_w = panel_w - 48;

    if (st->selected_field < st->scroll_offset)
        st->scroll_offset = st->selected_field;
    if (st->selected_field >= st->scroll_offset + PG_VISIBLE_ROWS)
        st->scroll_offset = st->selected_field - PG_VISIBLE_ROWS + 1;
    if (st->scroll_offset < 0) st->scroll_offset = 0;
    if (st->scroll_offset > tab->field_count - PG_VISIBLE_ROWS)
        st->scroll_offset = tab->field_count - PG_VISIBLE_ROWS;
    if (st->scroll_offset < 0) st->scroll_offset = 0;

    for (int i = 0; i < PG_VISIBLE_ROWS && (st->scroll_offset + i) < tab->field_count; i++) {
        int fidx = st->scroll_offset + i;
        const SchemaField *f = &tab->fields[fidx];
        int fy = PG_CONTENT_TOP + i * (PG_ROW_H + PG_ROW_GAP);
        int is_sel = (fidx == st->selected_field);
        int is_mod = pgsettings_is_modified(settings, schema, f->id);
        pg_draw_field(f, settings, content_x, fy, content_w, is_sel, is_mod);
    }

    if (st->selected_field >= 0 && st->selected_field < tab->field_count) {
        const SchemaField *sf = &tab->fields[st->selected_field];
        if (sf->show_description && sf->description[0]) {
            int desc_y = PG_CONTENT_TOP + PG_VISIBLE_ROWS * (PG_ROW_H + PG_ROW_GAP) + 4;
            pg_draw_description(sf, content_x, desc_y, content_w);
        }
    }

    pg_draw_scrollbar(tab->field_count, PG_VISIBLE_ROWS, st->scroll_offset,
                      SAFE_X1 - 20, PG_CONTENT_TOP, PG_VISIBLE_ROWS * (PG_ROW_H + PG_ROW_GAP));

    pg_draw_footer(st->dirty, st->show_confirm, st->confirm_sel);

    if (st->dropdown_active && st->selected_field >= 0 && st->selected_field < tab->field_count) {
        pg_draw_dropdown(&tab->fields[st->selected_field], settings, st);
    }

    draw_pgsettings_textview(st);
    draw_upload_qr_ui();
}

/* ---------- Input Logic ---------- */
static void change_field_value(const SchemaField *f, GameSettings *gs, int direction) {
    if (!f || !gs) return;

    if (f->type == FIELD_SELECT) {
        const char *cur = pgsettings_get_str(gs, f->id);
        int idx = -1;
        for (int i = 0; i < f->option_count; i++) {
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

    if (upload_qr_ui_is_open()) {
        return upload_qr_ui_handle_input(pressed);
    }

    if (st->textview_active) {
        return pgsettings_textview_handle_input(pressed, held, st);
    }

    const SchemaTab *tab = &schema->tabs[st->selected_tab];

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
        st->lr_hold_dir = -1;
        st->lr_hold_frames = 0;
    }
    if (pressed & ORBIS_PAD_BUTTON_RIGHT) {
        if (st->selected_field >= 0 && st->selected_field < tab->field_count) {
            change_field_value(&tab->fields[st->selected_field], settings, 1);
            st->dirty = pgsettings_any_modified(settings, schema);
        }
        st->lr_hold_dir = 1;
        st->lr_hold_frames = 0;
    }

    /* Hold-to-fast-scrub: keep LEFT/RIGHT pressed on a slider and it
     * accelerates instead of forcing repeated single taps. Only sliders --
     * selects/toggles have a small, fixed number of values so a fast
     * scrub isn't meaningful for them and would just overshoot. */
    {
        unsigned int lr_held = held & (ORBIS_PAD_BUTTON_LEFT | ORBIS_PAD_BUTTON_RIGHT);
        const SchemaField *hf = (st->selected_field >= 0 && st->selected_field < tab->field_count)
                                 ? &tab->fields[st->selected_field] : NULL;

        if (hf && hf->type == FIELD_SLIDER && lr_held && st->lr_hold_dir != 0) {
            st->lr_hold_frames++;
            if (st->lr_hold_frames > PG_HOLD_DELAY_FRAMES) {
                int since = st->lr_hold_frames - PG_HOLD_DELAY_FRAMES;
                int interval = PG_HOLD_RAMP_FRAMES - since / PG_HOLD_RAMP_FRAMES;
                if (interval < PG_HOLD_MIN_INTERVAL) interval = PG_HOLD_MIN_INTERVAL;
                if (since % interval == 0) {
                    int mult = 1 + since / PG_HOLD_MULT_FRAMES;
                    if (mult > PG_HOLD_MAX_MULT) mult = PG_HOLD_MAX_MULT;
                    for (int m = 0; m < mult; m++) {
                        change_field_value(hf, settings, st->lr_hold_dir);
                    }
                    st->dirty = pgsettings_any_modified(settings, schema);
                }
            }
        } else if (!lr_held) {
            st->lr_hold_frames = 0;
            st->lr_hold_dir = 0;
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
                st->dropdown_active = 1;
                const char *cur = pgsettings_get_str(settings, f->id);
                st->dropdown_sel = 0;
                for (int i = 0; i < f->option_count; i++) {
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
        for (int i = 0; i < tab->field_count; i++) {
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

    if (pressed & ORBIS_PAD_BUTTON_OPTIONS) {
        pgsettings_textview_open(st, schema, settings);
    }

    /* NOTE: verify ORBIS_PAD_BUTTON_TOUCH_PAD matches your orbis/Pad.h --
     * same caveat as ORBIS_PAD_BUTTON_OPTIONS above, swap if needed. */
    if (pressed & ORBIS_PAD_BUTTON_TOUCH_PAD) {
        /* `selected` is the same game this settings screen is open for --
         * main.c only enters this screen for games[selected]. */
        if (selected >= 0 && selected < game_count) {
            upload_qr_ui_open(games[selected].display_name, games[selected].id,
                               games[selected].name, games[selected].path);
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
