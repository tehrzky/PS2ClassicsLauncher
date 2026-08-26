#include "memcard_ui.h"
#include "memcard.h"
#include "mcio.h"
#include "ps2mc.h"
#include "video.h"
#include "font.h"
#include "colors.h"
#include "settings.h"
#include "cover.h"
#include "debug.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <orbis/Pad.h>
#include <stdint.h>
#include <dirent.h>

#define SCREEN_W    1920
#define SCREEN_H    1080
#define SAFE_X      64
#define SAFE_Y      40
#define SAFE_X1     (SCREEN_W - SAFE_X)
#define SAFE_Y1     (SCREEN_H - SAFE_Y)

#define FOOTER_H    70
#define PANEL_GAP   28
#define PANEL_Y     130
#define PANEL_BOT   (SCREEN_H - FOOTER_H - 40)
#define PANEL_H     (PANEL_BOT - PANEL_Y)
#define PANEL_W     ((SAFE_X1 - SAFE_X - PANEL_GAP) / 2)
#define SLOT1_X     SAFE_X
#define SLOT2_X     (SLOT1_X + PANEL_W + PANEL_GAP)

#define ID_ROW_H    38
#define ID_GAP      6
#define GRID_TOP    (PANEL_Y + 170)
#define GRID_COLS   4
#define GRID_ROWS   4
#define CELL_GAP    10
#define CELL_W      ((PANEL_W - 40 - (GRID_COLS - 1) * CELL_GAP) / GRID_COLS)
#define CELL_H      (CELL_W * 3 / 4)

#define RADIUS      8
#define BORDER_W    2

/* Simple nearest-neighbor RGBA blit to framebuffer */
static void draw_icon_rgba(int x, int y, int dst_w, int dst_h,
                           const uint32_t *rgba, int src_w, int src_h)
{
    if (!rgba || src_w <= 0 || src_h <= 0) return;
    uint32_t *fb = (uint32_t *)framebuffer[current_buf];

    for (int dy = 0; dy < dst_h; dy++) {
        int sy = dy * src_h / dst_h;
        if (sy >= src_h) sy = src_h - 1;
        for (int dx = 0; dx < dst_w; dx++) {
            int sx = dx * src_w / dst_w;
            if (sx >= src_w) sx = src_w - 1;
            uint32_t c = rgba[sy * src_w + sx];
            if (((c >> 24) & 0xFF) > 0x40) {
                int px = x + dx;
                int py = y + dy;
                if (px >= 0 && px < SCREEN_W && py >= 0 && py < SCREEN_H)
                    fb[py * SCREEN_W + px] = c;
            }
        }
    }
}

/* ============================================================
   STATIC HELPERS
   ============================================================ */

static void truncate_fit(const char *s, char *out, size_t out_len, int max_px, int sz) {
    if (font_text_width(s, sz) <= max_px) {
        snprintf(out, out_len, "%s", s);
        return;
    }
    int len = strlen(s), keep = len;
    char temp[512];
    while (keep > 0) {
        strncpy(temp, s, keep); temp[keep] = '\0';
        if (font_text_width(temp, sz) + font_text_width("...", sz) <= max_px) break;
        keep--;
    }
    if (keep < 1) keep = 1;
    snprintf(out, out_len, "%.*s...", keep, s);
}

static void draw_btn_hint(int x, int y, const char *btn, const char *lbl, uint32_t c) {
    int bw = font_text_width(btn, 24) + 16;
    int bh = 36;
    draw_rounded_rect(x, y, bw, bh, 6, COLOR_BORDER);
    draw_rounded_rect(x + 1, y + 1, bw - 2, bh - 2, 5, COLOR_CARD);
    draw_text(x + 8, y + 6, btn, c, 24);
    draw_text(x + bw + 14, y + 6, lbl, COLOR_TEXT, 24);
}

/* ============================================================
   SLOT BACKGROUND & HEADER
   ============================================================ */

static void draw_slot_bg(int px, int is_active) {
    uint32_t bc = is_active ? COLOR_GOLD : COLOR_BORDER;
    draw_rounded_rect(px, PANEL_Y, PANEL_W, PANEL_H, RADIUS, bc);
    draw_rounded_rect(px + BORDER_W, PANEL_Y + BORDER_W,
                      PANEL_W - BORDER_W * 2, PANEL_H - BORDER_W * 2,
                      RADIUS - 1, COLOR_PANEL);
}

static void draw_slot_label(int px, const char *label, int is_active) {
    int tw = font_text_width_slot(label, 40, FONT_SLOT_BOLD);
    uint32_t c = is_active ? COLOR_GOLD : COLOR_DIM;
    draw_text_slot(px + (PANEL_W - tw) / 2, PANEL_Y + 12, label, c, 40, FONT_SLOT_BOLD);
    draw_rect(px + 20, PANEL_Y + 56, PANEL_W - 40, 2, is_active ? COLOR_GOLD : COLOR_BORDER);
}

/* ============================================================
   ID ROWS (EMULATOR & PS2 ID)
   ============================================================ */

static void draw_id_row(int px, int y, const char *label, const char *value,
                        int is_focused, int is_dd, int is_active) {
    uint32_t vc = (is_focused && is_active) ? COLOR_GOLD : COLOR_TEXT;

    if (is_focused && is_active) {
        draw_rounded_rect(px + 12, y - 2, PANEL_W - 24, ID_ROW_H + 4, 4, COLOR_CARD_SEL);
        draw_rect(px + 12, y, 3, ID_ROW_H, COLOR_GOLD);
    }

    draw_text(px + 24, y + 2, label, COLOR_GOLD, 28);
    int lw = font_text_width(label, 28);

    char tbuf[256];
    truncate_fit(value, tbuf, sizeof(tbuf), PANEL_W - lw - 90, 28);
    draw_text(px + 24 + lw + 8, y + 2, tbuf, vc, 28);

    if (is_focused && is_active && is_dd) {
        draw_text(px + PANEL_W - 36, y + 2, "v", COLOR_GOLD, 28);
    }
}

static void draw_dropdown(int px, int y, const char **items, int count, int sel) {
    int item_h = 40, vis = count < 6 ? count : 6;
    int dh = vis * item_h + 8;
    int dy = y + ID_ROW_H + 4;
    if (dy + dh > PANEL_BOT - 20) dy = PANEL_BOT - 20 - dh;

    draw_rounded_rect(px + 12, dy, PANEL_W - 24, dh, 6, COLOR_BG);
    draw_rounded_rect(px + 12, dy, PANEL_W - 24, dh, 6, COLOR_GOLD);
    draw_rounded_rect(px + 13, dy + 1, PANEL_W - 26, dh - 2, 5, COLOR_PANEL);

    int start = (sel >= vis) ? sel - vis + 1 : 0;
    for (int i = start; i < count && i < start + vis; i++) {
        int iy = dy + 4 + (i - start) * item_h;
        if (i == sel) {
            draw_rounded_rect(px + 16, iy, PANEL_W - 32, item_h - 2, 4, COLOR_CARD_SEL);
            draw_rect(px + 16, iy + 2, 3, item_h - 6, COLOR_GOLD);
            draw_text(px + 28, iy + 4, items[i], COLOR_GOLD, 28);
        } else {
            draw_text(px + 28, iy + 4, items[i], COLOR_DIM, 28);
        }
    }
}

/* ============================================================
   GRID
   ============================================================ */

static void draw_save_grid(int px, MemCardSlot *slot, int is_active) {
    int grid_w = GRID_COLS * CELL_W + (GRID_COLS - 1) * CELL_GAP;
    int start_x = px + (PANEL_W - grid_w) / 2;
    int start_y = GRID_TOP;
    int total = GRID_COLS * GRID_ROWS;

    for (int i = 0; i < total; i++) {
        int col = i % GRID_COLS;
        int row = i / GRID_COLS;
        int cx = start_x + col * (CELL_W + CELL_GAP);
        int cy = start_y + row * (CELL_H + CELL_GAP);

        int is_sel = (i == slot->save_idx && slot->focus_element == 2 && is_active);

        if (is_sel) {
            // Gold border with dark background
            draw_rounded_rect(cx - 4, cy - 4, CELL_W + 8, CELL_H + 8, 6, COLOR_GOLD);
            draw_rounded_rect(cx - 2, cy - 2, CELL_W + 4, CELL_H + 4, 5, COLOR_CARD_SEL);
        }

        if (i < slot->save_count) {
            VmcSaveEntry *se = &slot->saves[i];

            /* Icon area */
            int ix = cx + 6, iy = cy + 6;
            int iw = CELL_W - 12, ih = CELL_H - 36;
            if (se->icon_rgba && se->icon_w > 0 && se->icon_h > 0) {
                draw_icon_rgba(ix, iy, iw, ih, se->icon_rgba, se->icon_w, se->icon_h);
            } else {
                draw_rounded_rect(ix, iy, iw, ih, 3, COLOR_BG);
            }

            /* Slot number top-left (larger) */
            char sn[8];
            snprintf(sn, sizeof(sn), "%02d", se->slot_num);
            draw_text(cx + 4, cy + 2, sn, is_sel ? COLOR_GOLD : COLOR_MUTED, 18);

            /* Blocks bottom-center (larger) */
            char blk[32];
            snprintf(blk, sizeof(blk), "%02d Blocks", se->blocks);
            int bw = font_text_width(blk, 16);
            draw_text(cx + (CELL_W - bw) / 2, cy + CELL_H - 26, blk,
                      is_sel ? COLOR_GOLD : COLOR_DIM, 16);
        }
    }
}

/* ============================================================
   INSERT CARD STATE
   ============================================================ */

static void draw_insert_card(int px, int is_active) {
    const char *txt = "INSERT CARD";
    int tw = font_text_width_slot(txt, 48, FONT_SLOT_BOLD);
    uint32_t c = is_active ? 0xFF4A6B8A : 0xFF1E293B;
    int tx = px + (PANEL_W - tw) / 2;
    int ty = PANEL_Y + PANEL_H / 2 - 24;
    draw_text_slot(tx, ty, txt, c, 48, FONT_SLOT_BOLD);
}

/* ============================================================
   INFO BAR & STATS
   ============================================================ */

static void draw_info_bar(int px, MemCardSlot *slot, int is_active) {
    int iy = PANEL_BOT - 110;
    draw_rect(px + 20, iy - 4, PANEL_W - 40, 2, COLOR_BORDER);

    if (slot->state == SLOT_STATE_OFF || slot->state == SLOT_STATE_EMU_SEL) {
        const char *msg = "NO MEMORY CARD SELECTED";
        int mw = font_text_width(msg, 28);
        draw_text(px + (PANEL_W - mw) / 2, iy + 20, msg, COLOR_MUTED, 28);
        return;
    }

    if (slot->save_idx >= 0 && slot->save_idx < slot->save_count && slot->focus_element == 2) {
        VmcSaveEntry *se = &slot->saves[slot->save_idx];
        char tbuf[256];
        truncate_fit(se->title, tbuf, sizeof(tbuf), PANEL_W - 60, 36);
        draw_text(px + 24, iy + 6, tbuf, is_active ? COLOR_TEXT : COLOR_DIM, 36);

        char info[256];
        snprintf(info, sizeof(info), "DATA SLOT %d  (%s)", se->slot_num, se->dir_name);
        draw_text(px + 24, iy + 42, info, COLOR_DIM, 22);
    } else {
        const char *msg = "NO SAVE SELECTED";
        int mw = font_text_width(msg, 28);
        draw_text(px + (PANEL_W - mw) / 2, iy + 20, msg, COLOR_MUTED, 28);
    }
}

static void draw_stats(int px, MemCardSlot *slot) {
    if (slot->state != SLOT_STATE_VMC_SEL) return;

    int sy = PANEL_BOT - 32;
    int total = 0;
    for (int i = 0; i < slot->save_count; i++) total += slot->saves[i].blocks;
    int avail = 120 - total;
    if (avail < 0) avail = 0;

    char u[32], a[32];
    snprintf(u, sizeof(u), "USED: %02d BLOCKS", total);
    snprintf(a, sizeof(a), "AVAIL: %02d BLOCKS", avail);
    draw_text(px + 24, sy, u, COLOR_DIM, 18);
    int aw = font_text_width(a, 18);
    draw_text(px + PANEL_W - aw - 24, sy, a, COLOR_DIM, 18);
}

/* ============================================================
   ACTION MENU
   ============================================================ */

static const char *action_items[] = {
    "Copy Save to Other Slot",
    "Delete Save",
    "Export Save as .PSU",
    "Import Save from .PSU",
    "Backup Full VMC",
    "Import Full VMC",
    "Format VMC"
};
#define ACTION_COUNT 7

static void draw_action_menu(void) {
    int mw = 640, row_h = 58, mh = ACTION_COUNT * row_h + 80;
    int mx = (SCREEN_W - mw) / 2, my = (SCREEN_H - mh) / 2;

    draw_rect(0, 0, SCREEN_W, SCREEN_H, 0xE6000000);

    draw_rounded_rect(mx, my, mw, mh, 12, COLOR_GOLD);
    draw_rounded_rect(mx + 2, my + 2, mw - 4, mh - 4, 10, COLOR_PANEL);

    const char *title = "ACTION MENU";
    int tw = font_text_width_slot(title, 40, FONT_SLOT_TITLE);
    draw_text_slot(mx + (mw - tw) / 2, my + 18, title, COLOR_GOLD, 40, FONT_SLOT_TITLE);
    draw_rect(mx + 30, my + 62, mw - 60, 2, COLOR_BORDER);

    MemCardSlot *slot = &g_slots[g_active_slot];
    for (int i = 0; i < ACTION_COUNT; i++) {
        int ry = my + 74 + i * row_h;
        int enabled = 1;

        if (i == 0 && (slot->state != SLOT_STATE_VMC_SEL || slot->save_idx < 0)) enabled = 0;
        if (i == 1 && (slot->state != SLOT_STATE_VMC_SEL || slot->save_idx < 0)) enabled = 0;
        if (i == 2 && (slot->state != SLOT_STATE_VMC_SEL || slot->save_idx < 0)) enabled = 0;
        if (i == 3 && slot->state != SLOT_STATE_VMC_SEL) enabled = 0;
        if (i == 4 && slot->state != SLOT_STATE_VMC_SEL) enabled = 0;
        if (i == 5 && slot->state == SLOT_STATE_OFF) enabled = 0;
        if (i == 6 && slot->state != SLOT_STATE_VMC_SEL) enabled = 0;

        uint32_t tc = enabled ? COLOR_DIM : 0xFF475569;
        if (i == g_memcard_action_sel && enabled) {
            draw_rounded_rect(mx + 20, ry, mw - 40, row_h - 6, 6, COLOR_CARD_SEL);
            draw_rect(mx + 20, ry + 4, 3, row_h - 14, COLOR_GOLD);
            tc = COLOR_GOLD;
        }
        draw_text(mx + 36, ry + 14, action_items[i], tc, 28);
    }
}

/* ============================================================
   CONFIRMATION DIALOG
   ============================================================ */

static void draw_confirm_dialog(void) {
    int mw = 640, mh = 260;
    int mx = (SCREEN_W - mw) / 2, my = (SCREEN_H - mh) / 2;

    draw_rect(0, 0, SCREEN_W, SCREEN_H, 0xE6000000);

    draw_rounded_rect(mx, my, mw, mh, 12, COLOR_GOLD);
    draw_rounded_rect(mx + 2, my + 2, mw - 4, mh - 4, 10, COLOR_PANEL);

    int tw = font_text_width_slot(g_confirm_title, 36, FONT_SLOT_BOLD);
    draw_text_slot(mx + (mw - tw) / 2, my + 22, g_confirm_title, COLOR_GOLD, 36, FONT_SLOT_BOLD);
    draw_rect(mx + 30, my + 62, mw - 60, 2, COLOR_BORDER);

    draw_text(mx + 30, my + 80, g_confirm_msg, COLOR_TEXT, 28);

    int by = my + mh - 70;
    int no_x = mx + mw / 2 - 160;
    int yes_x = mx + mw / 2 + 40;

    uint32_t no_c = (g_confirm_dialog_sel == 0) ? COLOR_GOLD : COLOR_DIM;
    uint32_t yes_c = (g_confirm_dialog_sel == 1) ? COLOR_ERROR : COLOR_DIM;

    draw_rounded_rect(no_x, by, 140, 48, 6, (g_confirm_dialog_sel == 0) ? COLOR_CARD_SEL : COLOR_CARD);
    draw_text(no_x + 34, by + 12, "[O] NO", no_c, 24);

    draw_rounded_rect(yes_x, by, 140, 48, 6, (g_confirm_dialog_sel == 1) ? 0x33FF4444 : COLOR_CARD);
    draw_text(yes_x + 28, by + 12, "[X] YES", yes_c, 24);
}

/* ============================================================
   HEADER & FOOTER
   ============================================================ */

static void draw_header(void) {
    draw_text_slot(SAFE_X, SAFE_Y, "PS2 ISO LAUNCHER", COLOR_ACCENT, 52, FONT_SLOT_TITLE);
    draw_text_slot(SAFE_X + 1, SAFE_Y, "PS2 ISO LAUNCHER", COLOR_ACCENT, 52, FONT_SLOT_TITLE);

    const char *page = "MEMORY CARD MANAGER";
    int pw = font_text_width_slot(page, 28, FONT_SLOT_TITLE);
    draw_text_slot(SAFE_X1 - pw, SAFE_Y + 12, page, COLOR_GOLD, 28, FONT_SLOT_TITLE);

    draw_rect(SAFE_X, 100, SCREEN_W - SAFE_X * 2, 2, COLOR_ACCENT);
}

static void draw_footer(void) {
    int y = SCREEN_H - FOOTER_H;
    draw_rect(0, y, SCREEN_W, FOOTER_H, COLOR_PANEL);
    draw_rect(0, y, SCREEN_W, 2, COLOR_ACCENT);

    int hy = y + 16;
    int x = SAFE_X + 20;

    draw_btn_hint(x, hy, "X", "SELECT", COLOR_ACCENT);       x += 240;
    draw_btn_hint(x, hy, "TRI", "ACTIONS", COLOR_ACCENT);    x += 260;
    draw_btn_hint(x, hy, "<>", "SWITCH SLOT", COLOR_DIM);   x += 260;
    draw_btn_hint(x, hy, "^v", "NAVIGATE", COLOR_DIM);       x += 240;
    draw_btn_hint(x, hy, "O", "BACK", COLOR_ERROR);
}

/* ============================================================
   TOAST NOTIFICATION
   ============================================================ */

static void draw_toast(void)
{
    if (g_toast_timer <= 0) return;

    int tw = font_text_width(g_toast_msg, 28);
    int tx = (SCREEN_W - tw) / 2;
    int ty = SCREEN_H - 140;

    draw_rounded_rect(tx - 30, ty - 16, tw + 60, 56, 10, 0xDD000000);
    draw_rounded_rect(tx - 28, ty - 14, tw + 56, 52, 8, COLOR_GOLD);
    draw_text(tx, ty + 2, g_toast_msg, COLOR_TEXT, 28);
}

/* ============================================================
   PSU FILE PICKER
   ============================================================ */

static void draw_psu_picker(void)
{
    if (!g_psu_picker_open) return;

    int mw = 680, row_h = 50;
    int mh = (g_psu_file_count + 2) * row_h + 60;
    if (mh > 560) mh = 560;
    int mx = (SCREEN_W - mw) / 2, my = (SCREEN_H - mh) / 2;

    draw_rect(0, 0, SCREEN_W, SCREEN_H, 0xE6000000);
    draw_rounded_rect(mx, my, mw, mh, 12, COLOR_GOLD);
    draw_rounded_rect(mx + 2, my + 2, mw - 4, mh - 4, 10, COLOR_PANEL);

    const char *title = "SELECT .PSU FILE TO IMPORT";
    int tw = font_text_width_slot(title, 28, FONT_SLOT_BOLD);
    draw_text_slot(mx + (mw - tw) / 2, my + 18, title, COLOR_GOLD, 28, FONT_SLOT_BOLD);
    draw_rect(mx + 30, my + 54, mw - 60, 2, COLOR_BORDER);

    if (g_psu_file_count == 0) {
        draw_text(mx + 30, my + 80,
                  "No .PSU files found in /mnt/usb0/PS2SAVES/", COLOR_DIM, 24);
    } else {
        for (int i = 0; i < g_psu_file_count; i++) {
            int ry = my + 64 + i * row_h;
            const char *fname = strrchr(g_psu_files[i], '/');
            if (fname) fname++; else fname = g_psu_files[i];

            if (i == g_psu_picker_sel) {
                draw_rounded_rect(mx + 20, ry, mw - 40, row_h - 6, 6, COLOR_CARD_SEL);
                draw_rect(mx + 20, ry + 4, 3, row_h - 14, COLOR_GOLD);
                draw_text(mx + 36, ry + 12, fname, COLOR_GOLD, 24);
            } else {
                draw_text(mx + 36, ry + 12, fname, COLOR_DIM, 24);
            }
        }
    }
}

/* ============================================================
   MAIN DRAW
   ============================================================ */

void draw_memcard_ui(void) {
    memcard_update_toast();
    if (g_settings.wallpaper[0]) {
        cover_draw_wallpaper();
    } else {
        memset(framebuffer[current_buf], 0, FB_SIZE);
        draw_rect(0, 0, SCREEN_W, SCREEN_H, COLOR_BG);
    }

    draw_header();

    for (int si = 0; si < 2; si++) {
        int px = (si == 0) ? SLOT1_X : SLOT2_X;
        MemCardSlot *slot = &g_slots[si];
        int is_active = (g_active_slot == si);

        draw_slot_bg(px, is_active);
        draw_slot_label(px, (si == 0) ? "SLOT 1" : "SLOT 2", is_active);

        int emu_y = PANEL_Y + 64;
        int disc_y = emu_y + ID_ROW_H + ID_GAP;

        const char *emu_val = (slot->emulator_idx >= 0 && slot->emulator_idx < g_emulator_count)
                              ? g_emulators[slot->emulator_idx].id : "OFF";
        draw_id_row(px, emu_y, "EMULATOR ID:", emu_val,
                    slot->focus_element == 0, slot->in_dropdown == 1, is_active);

        if (slot->in_dropdown == 1 && is_active) {
            const char *items[MAX_EMULATORS];
            for (int i = 0; i < g_emulator_count; i++) items[i] = g_emulators[i].id;
            draw_dropdown(px, emu_y, items, g_emulator_count, slot->dropdown_sel);
        }

        const char *disc_val = "OFF";
        if (slot->state == SLOT_STATE_EMU_SEL || slot->state == SLOT_STATE_VMC_SEL) {
            if (slot->vmc_idx >= 0 && slot->vmc_idx < slot->vmc_count) {
                disc_val = slot->vmc_files[slot->vmc_idx].display_name;
            } else {
                disc_val = "---";
            }
        }
        draw_id_row(px, disc_y, "PS2 ID:", disc_val,
                    slot->focus_element == 1, slot->in_dropdown == 2, is_active);

        if (slot->in_dropdown == 2 && is_active) {
            const char *items[MAX_VMC_FILES];
            for (int i = 0; i < slot->vmc_count; i++) items[i] = slot->vmc_files[i].display_name;
            draw_dropdown(px, disc_y, items, slot->vmc_count, slot->dropdown_sel);
        }

        if (slot->in_dropdown == 0) {
            if (slot->state == SLOT_STATE_VMC_SEL) {
                draw_save_grid(px, slot, is_active);
                draw_info_bar(px, slot, is_active);
                draw_stats(px, slot);
            } else {
                draw_insert_card(px, is_active);
                draw_info_bar(px, slot, is_active);
            }
        }
    }

    draw_footer();

    if (g_psu_picker_open) draw_psu_picker();
    if (g_memcard_action_menu_open) draw_action_menu();
    if (g_confirm_dialog_open) draw_confirm_dialog();
    if (g_toast_timer > 0) draw_toast();
}

/* ============================================================
   INPUT HANDLING
   ============================================================ */

static void move_grid_cursor(MemCardSlot *slot, int dx, int dy) {
    if (slot->save_count <= 0) { slot->save_idx = -1; return; }
    int cols = GRID_COLS;
    int row = slot->save_idx / cols;
    int col = slot->save_idx % cols;
    row += dy;
    col += dx;
    if (col < 0) col = 0;
    if (col >= cols) col = cols - 1;
    int new_idx = row * cols + col;
    if (new_idx < 0) new_idx = 0;
    if (new_idx >= slot->save_count) new_idx = slot->save_count - 1;
    slot->save_idx = new_idx;
}

static void switch_slot(int new_slot, int keep_focus) {
    if (new_slot == g_active_slot) return;
    MemCardSlot *old = &g_slots[g_active_slot];
    MemCardSlot *new = &g_slots[new_slot];
    int old_focus = old->focus_element;
    int old_col = 0;
    if (old_focus == 2 && old->save_count > 0 && old->save_idx >= 0) {
        old_col = old->save_idx % GRID_COLS;
    }
    g_active_slot = new_slot;
    if (keep_focus) {
        new->focus_element = old_focus;
        if (old_focus == 2) {
            if (new->save_count > 0) {
                int target = old_col;
                if (target >= new->save_count) target = new->save_count - 1;
                new->save_idx = target;
            } else {
                new->save_idx = -1;
            }
        }
    } else {
        new->focus_element = 2;
        if (new->save_count > 0) {
            new->save_idx = (old_col < new->save_count) ? old_col : new->save_count - 1;
        } else {
            new->save_idx = -1;
        }
    }
}

/* Confirmation callbacks */
static int pending_delete_slot = -1;
static int pending_copy_src = -1;
static int pending_copy_dst = -1;
static int pending_format_slot = -1;

static void do_delete_save(void) {
    if (pending_delete_slot >= 0) {
        memcard_delete_save(pending_delete_slot);
        memcard_show_toast("Save deleted");
        pending_delete_slot = -1;
    }
}

static void do_copy_save(void) {
    if (pending_copy_src >= 0 && pending_copy_dst >= 0) {
        memcard_copy_save_between_slots(pending_copy_src, pending_copy_dst);
        pending_copy_src = -1;
        pending_copy_dst = -1;
    }
}

static void do_format_vmc(void) {
    if (pending_format_slot >= 0) {
        memcard_format_vmc(pending_format_slot);
        pending_format_slot = -1;
    }
}

void memcard_ui_handle_input(unsigned int pressed, unsigned int buttons) {
    MemCardSlot *slot = &g_slots[g_active_slot];

    /* === PSU FILE PICKER === */
    if (g_psu_picker_open) {
        if (pressed & ORBIS_PAD_BUTTON_CIRCLE) {
            g_psu_picker_open = 0;
            return;
        }
        if (pressed & ORBIS_PAD_BUTTON_UP) {
            if (g_psu_file_count > 0) {
                g_psu_picker_sel = (g_psu_picker_sel - 1 + g_psu_file_count) % g_psu_file_count;
            }
        }
        if (pressed & ORBIS_PAD_BUTTON_DOWN) {
            if (g_psu_file_count > 0) {
                g_psu_picker_sel = (g_psu_picker_sel + 1) % g_psu_file_count;
            }
        }
        if (pressed & ORBIS_PAD_BUTTON_CROSS) {
            if (g_psu_file_count > 0) {
                int ok = memcard_import_save_psu(g_active_slot,
                                                 g_psu_files[g_psu_picker_sel]);
                memcard_show_toast(ok ? "Import successful" : "Import failed");
            }
            g_psu_picker_open = 0;
        }
        return;
    }

    /* === CONFIRMATION DIALOG === */
    if (g_confirm_dialog_open) {
        if (pressed & ORBIS_PAD_BUTTON_LEFT)  g_confirm_dialog_sel = 0;
        if (pressed & ORBIS_PAD_BUTTON_RIGHT) g_confirm_dialog_sel = 1;
        if (pressed & ORBIS_PAD_BUTTON_CIRCLE) {
            memcard_confirm_no();
            return;
        }
        if (pressed & ORBIS_PAD_BUTTON_CROSS) {
            if (g_confirm_dialog_sel == 1) memcard_confirm_yes();
            else memcard_confirm_no();
            return;
        }
        return;
    }

    /* === ACTION MENU === */
    if (g_memcard_action_menu_open) {
        if (pressed & ORBIS_PAD_BUTTON_CIRCLE) {
            g_memcard_action_menu_open = 0;
            return;
        }
        if (pressed & ORBIS_PAD_BUTTON_UP) {
            g_memcard_action_sel = (g_memcard_action_sel - 1 + ACTION_COUNT) % ACTION_COUNT;
        }
        if (pressed & ORBIS_PAD_BUTTON_DOWN) {
            g_memcard_action_sel = (g_memcard_action_sel + 1) % ACTION_COUNT;
        }
        if (pressed & ORBIS_PAD_BUTTON_CROSS) {
            int other = (g_active_slot == 0) ? 1 : 0;
            MemCardSlot *other_slot = &g_slots[other];

            switch (g_memcard_action_sel) {
                case 0: /* Copy to other slot */
                    if (slot->state == SLOT_STATE_VMC_SEL && slot->save_idx >= 0 &&
                        other_slot->state == SLOT_STATE_VMC_SEL) {
                        memcard_unload_current_vmc();
                        if (mcio_vmcInit(other_slot->loaded_vmc_path) == sceMcResSucceed) {
                            struct io_dirent st;
                            if (mcio_mcStat(slot->saves[slot->save_idx].dir_name, &st) == 0) {
                                pending_copy_src = g_active_slot;
                                pending_copy_dst = other;
                                char msg[256];
                                snprintf(msg, sizeof(msg),
                                    "Save %s already exists in Slot %d.\nOverwrite?",
                                    slot->saves[slot->save_idx].dir_name, other + 1);
                                memcard_show_confirm("CONFIRM OVERWRITE", msg, do_copy_save, NULL);
                            } else {
                                memcard_copy_save_between_slots(g_active_slot, other);
                                memcard_show_toast("Save copied");
                            }
                            mcio_vmcFinish();
                        }
                    }
                    break;

                case 1: /* Delete save */
                    if (slot->state == SLOT_STATE_VMC_SEL && slot->save_idx >= 0) {
                        pending_delete_slot = g_active_slot;
                        char msg[128];
                        snprintf(msg, sizeof(msg), "Delete save %s?\nThis cannot be undone.",
                                 slot->saves[slot->save_idx].dir_name);
                        memcard_show_confirm("CONFIRM DELETE", msg, do_delete_save, NULL);
                    }
                    break;

                case 2: /* Export .PSU */
                    if (slot->state == SLOT_STATE_VMC_SEL && slot->save_idx >= 0) {
                        int ok = memcard_export_save_psu(g_active_slot, "/mnt/usb0");
                        memcard_show_toast(ok ? "Exported to USB" : "Export failed");
                    }
                    break;

                case 3: /* Import Save from .PSU */
                    if (slot->state == SLOT_STATE_VMC_SEL) {
                        memcard_scan_psu_files();
                        g_psu_picker_open = 1;
                        g_psu_picker_sel = 0;
                    }
                    break;

                case 4: /* Backup full VMC */
                    if (slot->state == SLOT_STATE_VMC_SEL) {
                        int ok = memcard_backup_vmc_to_usb(g_active_slot, "/mnt/usb0");
                        memcard_show_toast(ok ? "VMC backed up" : "Backup failed");
                    }
                    break;

                case 5: /* Import full VMC */
                    if (slot->state != SLOT_STATE_OFF) {
                        DIR *usbdir = opendir("/mnt/usb0/PS2VMC");
                        if (usbdir) {
                            struct dirent *ent;
                            char found[512] = {0};
                            while ((ent = readdir(usbdir)) != NULL) {
                                int len = strlen(ent->d_name);
                                if (len < 5) continue;
                                const char *ext = ent->d_name + len - 4;
                                if (strcasecmp(ext, ".VM2") == 0 ||
                                    strcasecmp(ext, ".bin") == 0 ||
                                    strcasecmp(ext, ".vmc") == 0) {
                                    snprintf(found, sizeof(found),
                                             "/mnt/usb0/PS2VMC/%s", ent->d_name);
                                    break;
                                }
                            }
                            closedir(usbdir);
                            if (found[0]) {
                                int ok = memcard_import_vmc_from_usb(g_active_slot, found);
                                memcard_show_toast(ok ? "VMC imported" : "Import failed");
                                memcard_refresh_slot(g_active_slot);
                            } else {
                                memcard_show_toast("No VMC found on USB");
                            }
                        } else {
                            memcard_show_toast("USB not mounted");
                        }
                    }
                    break;

                case 6: /* Format VMC */
                    if (slot->state == SLOT_STATE_VMC_SEL) {
                        pending_format_slot = g_active_slot;
                        memcard_show_confirm("CONFIRM FORMAT",
                            "Format this VMC?\nAll saves will be lost!", do_format_vmc, NULL);
                    }
                    break;
            }
            g_memcard_action_menu_open = 0;
        }
        return;
    }

    /* === DROPDOWN MODE === */
    if (slot->in_dropdown) {
        if (pressed & ORBIS_PAD_BUTTON_CIRCLE) {
            slot->in_dropdown = 0;
            return;
        }
        if (pressed & ORBIS_PAD_BUTTON_UP) {
            slot->dropdown_sel--;
            int max = (slot->in_dropdown == 1) ? g_emulator_count : slot->vmc_count;
            if (slot->dropdown_sel < 0) slot->dropdown_sel = max - 1;
        }
        if (pressed & ORBIS_PAD_BUTTON_DOWN) {
            slot->dropdown_sel++;
            int max = (slot->in_dropdown == 1) ? g_emulator_count : slot->vmc_count;
            if (slot->dropdown_sel >= max) slot->dropdown_sel = 0;
        }
        if (pressed & ORBIS_PAD_BUTTON_CROSS) {
            if (slot->in_dropdown == 1) {
                if (slot->emulator_idx != slot->dropdown_sel) {
                    slot->emulator_idx = slot->dropdown_sel;
                    slot->vmc_idx = -1;
                    slot->save_idx = -1;
                    slot->state = SLOT_STATE_EMU_SEL;
                    memcard_scan_vmc_files(g_active_slot);
                    if (slot->vmc_count > 0) {
                        slot->vmc_idx = 0;
                        memcard_load_vmc(g_active_slot);
                        slot->state = SLOT_STATE_VMC_SEL;
                    }
                }
            } else if (slot->in_dropdown == 2) {
                if (slot->vmc_idx != slot->dropdown_sel) {
                    slot->vmc_idx = slot->dropdown_sel;
                    slot->save_idx = -1;
                    memcard_load_vmc(g_active_slot);
                    slot->state = (slot->vmc_loaded) ? SLOT_STATE_VMC_SEL : SLOT_STATE_EMU_SEL;
                }
            }
            slot->in_dropdown = 0;
        }
        return;
    }

    /* === NORMAL NAVIGATION === */

    /* LEFT */
    if (pressed & ORBIS_PAD_BUTTON_LEFT) {
        if (slot->focus_element == 2 && slot->state == SLOT_STATE_VMC_SEL) {
            if (slot->save_count > 0 && slot->save_idx >= 0) {
                int col = slot->save_idx % GRID_COLS;
                if (col == 0) {
                    if (g_active_slot == 1) {
                        switch_slot(0, 1); // keep focus on grid
                    }
                } else {
                    move_grid_cursor(slot, -1, 0);
                }
            }
        } else {
            if (g_active_slot == 1) {
                switch_slot(0, 1); // keep same focus element
            } else if (slot->focus_element > 0) {
                slot->focus_element--;
            }
        }
    }

    /* RIGHT */
    if (pressed & ORBIS_PAD_BUTTON_RIGHT) {
        if (slot->focus_element == 2 && slot->state == SLOT_STATE_VMC_SEL) {
            if (slot->save_count > 0 && slot->save_idx >= 0) {
                int col = slot->save_idx % GRID_COLS;
                int last_col = (slot->save_count - 1) % GRID_COLS;
                if (col == last_col || slot->save_idx == slot->save_count - 1) {
                    if (g_active_slot == 0) {
                        switch_slot(1, 1); // keep focus on grid
                    }
                } else {
                    move_grid_cursor(slot, 1, 0);
                }
            }
        } else {
            if (g_active_slot == 0) {
                switch_slot(1, 1); // keep same focus element
            } else if (slot->focus_element < 2) {
                slot->focus_element++;
            }
        }
    }

    /* UP */
    if (pressed & ORBIS_PAD_BUTTON_UP) {
        if (slot->focus_element == 2 && slot->state == SLOT_STATE_VMC_SEL) {
            if (slot->save_count > 0 && slot->save_idx >= 0) {
                int row = slot->save_idx / GRID_COLS;
                if (row == 0) {
                    slot->focus_element = 1; // go to PS2 ID
                } else {
                    move_grid_cursor(slot, 0, -1);
                }
            }
        } else {
            if (slot->focus_element > 0) {
                slot->focus_element--;
            } else {
                // wrap to bottom of grid
                slot->focus_element = 2;
                if (slot->save_count > 0) {
                    slot->save_idx = slot->save_count - 1;
                }
            }
        }
    }

    /* DOWN */
    if (pressed & ORBIS_PAD_BUTTON_DOWN) {
        if (slot->focus_element == 2 && slot->state == SLOT_STATE_VMC_SEL) {
            move_grid_cursor(slot, 0, 1);
        } else {
            if (slot->focus_element < 2) {
                slot->focus_element++; // move to next row (Emu->PS2, PS2->grid)
                if (slot->focus_element == 2) {
                    if (slot->save_count > 0 && slot->save_idx < 0) {
                        slot->save_idx = 0;
                    }
                }
            } else {
                // already at grid, wrap to top
                slot->focus_element = 0;
            }
        }
    }

    /* CROSS: Open dropdown */
    if (pressed & ORBIS_PAD_BUTTON_CROSS) {
        if (slot->focus_element == 0) {
            slot->in_dropdown = 1;
            slot->dropdown_sel = slot->emulator_idx;
            if (slot->dropdown_sel < 0) slot->dropdown_sel = 0;
        } else if (slot->focus_element == 1 && slot->vmc_count > 0) {
            slot->in_dropdown = 2;
            slot->dropdown_sel = slot->vmc_idx;
            if (slot->dropdown_sel < 0) slot->dropdown_sel = 0;
        }
    }

    /* TRIANGLE: Open action menu */
    if (pressed & ORBIS_PAD_BUTTON_TRIANGLE) {
        if (slot->state == SLOT_STATE_VMC_SEL) {
            g_memcard_action_menu_open = 1;
            g_memcard_action_sel = 0;
        } else if (slot->state == SLOT_STATE_EMU_SEL) {
            g_memcard_action_menu_open = 1;
            g_memcard_action_sel = 3;
        }
    }
}
