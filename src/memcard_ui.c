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

#define ID_ROW_H    34
#define ID_GAP      6
#define GRID_TOP    (PANEL_Y + 155)
#define GRID_COLS   4
#define GRID_ROWS   4
#define CELL_GAP    10
#define CELL_W      ((PANEL_W - 40 - (GRID_COLS - 1) * CELL_GAP) / GRID_COLS)
#define CELL_H      (CELL_W * 3 / 4)

#define RADIUS      8
#define BORDER_W    2

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
    int bw = font_text_width(btn, 20) + 12;
    draw_rounded_rect(x, y, bw, 30, 4, COLOR_BORDER);
    draw_rounded_rect(x + 1, y + 1, bw - 2, 28, 3, COLOR_CARD);
    draw_text(x + 6, y + 3, btn, c, 20);
    draw_text(x + bw + 8, y + 3, lbl, COLOR_TEXT, 20);
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
    int tw = font_text_width_slot(label, 26, FONT_SLOT_BOLD);
    uint32_t c = is_active ? COLOR_GOLD : COLOR_DIM;
    draw_text_slot(px + (PANEL_W - tw) / 2, PANEL_Y + 10, label, c, 26, FONT_SLOT_BOLD);
    draw_rect(px + 20, PANEL_Y + 42, PANEL_W - 40, 2, is_active ? COLOR_GOLD : COLOR_BORDER);
}

/* ============================================================
   ID ROWS (EMULATOR & PS2 ID)
   ============================================================ */

static void draw_id_row(int px, int y, const char *label, const char *value,
                        int is_focused, int is_dd, int is_active) {
    uint32_t vc = (is_focused && is_active) ? COLOR_GOLD : COLOR_TEXT;

    if (is_focused && is_active) {
        draw_rounded_rect(px + 12, y - 2, PANEL_W - 24, ID_ROW_H + 4, 4, COLOR_CARD_SEL);
        draw_rect(px + 12, y, 3, ID_ROW_H, COLOR_ACCENT);
    }

    draw_text(px + 24, y + 3, label, COLOR_GOLD, 22);
    int lw = font_text_width(label, 22);

    char tbuf[256];
    truncate_fit(value, tbuf, sizeof(tbuf), PANEL_W - lw - 90, 22);
    draw_text(px + 24 + lw + 8, y + 3, tbuf, vc, 22);

    if (is_focused && is_active) {
        draw_text(px + PANEL_W - 36, y + 3, is_dd ? "v" : ">", COLOR_ACCENT, 22);
    }
}

static void draw_dropdown(int px, int y, const char **items, int count, int sel) {
    int item_h = 34, vis = count < 6 ? count : 6;
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
            draw_rect(px + 16, iy + 2, 3, item_h - 6, COLOR_ACCENT);
            draw_text(px + 28, iy + 5, items[i], COLOR_GOLD, 20);
        } else {
            draw_text(px + 28, iy + 5, items[i], COLOR_DIM, 20);
        }
    }
}

/* ============================================================
   GRID (NO BORDERS)
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
            /* Gold glow outline instead of border */
            draw_rounded_rect(cx - 4, cy - 4, CELL_W + 8, CELL_H + 8, 6, 0x33FFD700);
            draw_rounded_rect(cx - 2, cy - 2, CELL_W + 4, CELL_H + 4, 5, COLOR_GOLD);
        }

        if (i < slot->save_count) {
            VmcSaveEntry *se = &slot->saves[i];

            /* Icon area (placeholder for now — real icon extraction is Phase 2) */
            int ix = cx + 6, iy = cy + 6;
            int iw = CELL_W - 12, ih = CELL_H - 32;
            draw_rounded_rect(ix, iy, iw, ih, 3, COLOR_BG);

            /* Slot number top-left */
            char sn[8];
            snprintf(sn, sizeof(sn), "%02d", se->slot_num);
            draw_text(cx + 4, cy + 2, sn, is_sel ? COLOR_GOLD : COLOR_MUTED, 14);

            /* Blocks bottom-center */
            char blk[32];
            snprintf(blk, sizeof(blk), "%02d Blocks", se->blocks);
            int bw = font_text_width(blk, 13);
            draw_text(cx + (CELL_W - bw) / 2, cy + CELL_H - 22, blk,
                      is_sel ? COLOR_GOLD : COLOR_DIM, 13);
        }
        /* else: blank — no border, no text, no "EMPTY" label */
    }
}

/* ============================================================
   INSERT CARD STATE
   ============================================================ */

static void draw_insert_card(int px, int is_active) {
    const char *txt = "INSERT CARD";
    int tw = font_text_width_slot(txt, 48, FONT_SLOT_BOLD);
    uint32_t c = is_active ? 0xFF4A6B8A : 0xFF1E293B; /* dim blue if active, darker if not */
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
        int mw = font_text_width(msg, 24);
        draw_text(px + (PANEL_W - mw) / 2, iy + 20, msg, COLOR_MUTED, 24);
        return;
    }

    if (slot->save_idx >= 0 && slot->save_idx < slot->save_count && slot->focus_element == 2) {
        VmcSaveEntry *se = &slot->saves[slot->save_idx];
        char tbuf[256];
        truncate_fit(se->title, tbuf, sizeof(tbuf), PANEL_W - 60, 26);
        draw_text(px + 24, iy + 8, tbuf, is_active ? COLOR_TEXT : COLOR_DIM, 26);

        char info[256];
        snprintf(info, sizeof(info), "DATA SLOT %d  (%s)", se->slot_num, se->dir_name);
        draw_text(px + 24, iy + 38, info, COLOR_DIM, 18);
    } else {
        const char *msg = "NO SAVE SELECTED";
        int mw = font_text_width(msg, 22);
        draw_text(px + (PANEL_W - mw) / 2, iy + 20, msg, COLOR_MUTED, 22);
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
    draw_text(px + 24, sy, u, COLOR_DIM, 16);
    int aw = font_text_width(a, 16);
    draw_text(px + PANEL_W - aw - 24, sy, a, COLOR_DIM, 16);
}

/* ============================================================
   ACTION MENU
   ============================================================ */

static const char *action_items[] = {
    "Copy Save to Other Slot",
    "Delete Save",
    "Export Save as .PSU",
    "Backup Full VMC",
    "Import Full VMC",
    "Format VMC"
};
#define ACTION_COUNT 6

static void draw_action_menu(void) {
    int mw = 520, row_h = 48, mh = ACTION_COUNT * row_h + 50;
    int mx = (SCREEN_W - mw) / 2, my = (SCREEN_H - mh) / 2;

    draw_rect(0, 0, SCREEN_W, SCREEN_H, 0xE6000000);

    draw_rounded_rect(mx, my, mw, mh, 12, COLOR_GOLD);
    draw_rounded_rect(mx + 2, my + 2, mw - 4, mh - 4, 10, COLOR_PANEL);

    const char *title = "ACTION MENU";
    int tw = font_text_width_slot(title, 28, FONT_SLOT_BOLD);
    draw_text_slot(mx + (mw - tw) / 2, my + 14, title, COLOR_GOLD, 28, FONT_SLOT_BOLD);
    draw_rect(mx + 30, my + 48, mw - 60, 2, COLOR_BORDER);

    MemCardSlot *slot = &g_slots[g_active_slot];
    for (int i = 0; i < ACTION_COUNT; i++) {
        int ry = my + 58 + i * row_h;
        int enabled = 1;

        /* Disable items based on state */
        if (i == 0 && (slot->state != SLOT_STATE_VMC_SEL || slot->save_idx < 0)) enabled = 0;
        if (i == 1 && (slot->state != SLOT_STATE_VMC_SEL || slot->save_idx < 0)) enabled = 0;
        if (i == 2 && (slot->state != SLOT_STATE_VMC_SEL || slot->save_idx < 0)) enabled = 0;
        if (i == 3 && slot->state != SLOT_STATE_VMC_SEL) enabled = 0;
        if (i == 4 && slot->state == SLOT_STATE_OFF) enabled = 0;
        if (i == 5 && slot->state != SLOT_STATE_VMC_SEL) enabled = 0;

        uint32_t tc = enabled ? COLOR_DIM : 0xFF475569;
        if (i == g_memcard_action_sel && enabled) {
            draw_rounded_rect(mx + 20, ry, mw - 40, row_h - 6, 6, COLOR_CARD_SEL);
            draw_rect(mx + 20, ry + 4, 3, row_h - 14, COLOR_ACCENT);
            tc = COLOR_GOLD;
        }
        draw_text(mx + 36, ry + 10, action_items[i], tc, 22);
    }
}

/* ============================================================
   CONFIRMATION DIALOG
   ============================================================ */

static void draw_confirm_dialog(void) {
    int mw = 560, mh = 220;
    int mx = (SCREEN_W - mw) / 2, my = (SCREEN_H - mh) / 2;

    draw_rect(0, 0, SCREEN_W, SCREEN_H, 0xE6000000);

    draw_rounded_rect(mx, my, mw, mh, 12, COLOR_GOLD);
    draw_rounded_rect(mx + 2, my + 2, mw - 4, mh - 4, 10, COLOR_PANEL);

    int tw = font_text_width_slot(g_confirm_title, 28, FONT_SLOT_BOLD);
    draw_text_slot(mx + (mw - tw) / 2, my + 20, g_confirm_title, COLOR_GOLD, 28, FONT_SLOT_BOLD);
    draw_rect(mx + 30, my + 54, mw - 60, 2, COLOR_BORDER);

    /* Message (multi-line if needed) */
    draw_text(mx + 30, my + 70, g_confirm_msg, COLOR_TEXT, 22);

    /* Buttons: NO (default) | YES */
    int by = my + mh - 60;
    int no_x = mx + mw / 2 - 140;
    int yes_x = mx + mw / 2 + 20;

    uint32_t no_c = (g_confirm_dialog_sel == 0) ? COLOR_GOLD : COLOR_DIM;
    uint32_t yes_c = (g_confirm_dialog_sel == 1) ? COLOR_ERROR : COLOR_DIM;

    draw_rounded_rect(no_x, by, 120, 40, 6, (g_confirm_dialog_sel == 0) ? COLOR_CARD_SEL : COLOR_CARD);
    draw_text(no_x + 30, by + 8, "[O] NO", no_c, 22);

    draw_rounded_rect(yes_x, by, 120, 40, 6, (g_confirm_dialog_sel == 1) ? 0x33FF4444 : COLOR_CARD);
    draw_text(yes_x + 24, by + 8, "[X] YES", yes_c, 22);
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

    int hy = y + 18;
    int x = SAFE_X + 20;

    draw_btn_hint(x, hy, "X", "SELECT", COLOR_ACCENT);       x += 180;
    draw_btn_hint(x, hy, "TRI", "ACTIONS", COLOR_ACCENT);    x += 200;
    draw_btn_hint(x, hy, "<>", "SWITCH SLOT", COLOR_DIM);   x += 240;
    draw_btn_hint(x, hy, "^v", "NAVIGATE", COLOR_DIM);       x += 200;
    draw_btn_hint(x, hy, "O", "BACK", COLOR_ERROR);
}

/* ============================================================
   MAIN DRAW
   ============================================================ */

void draw_memcard_ui(void) {
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

        int emu_y = PANEL_Y + 56;
        int disc_y = emu_y + ID_ROW_H + ID_GAP;

        /* Emulator ID row */
        const char *emu_val = (slot->emulator_idx >= 0 && slot->emulator_idx < g_emulator_count)
                              ? g_emulators[slot->emulator_idx].id : "OFF";
        draw_id_row(px, emu_y, "EMULATOR ID:", emu_val,
                    slot->focus_element == 0, slot->in_dropdown == 1, is_active);

        if (slot->in_dropdown == 1 && is_active) {
            const char *items[MAX_EMULATORS];
            for (int i = 0; i < g_emulator_count; i++) items[i] = g_emulators[i].id;
            draw_dropdown(px, emu_y, items, g_emulator_count, slot->dropdown_sel);
        }

        /* PS2 ID row */
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

        /* Grid or INSERT CARD */
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

    if (g_memcard_action_menu_open) draw_action_menu();
    if (g_confirm_dialog_open) draw_confirm_dialog();
}

/* ============================================================
   INPUT HANDLING
   ============================================================ */

static void move_grid_cursor(MemCardSlot *slot, int dx, int dy) {
    if (slot->save_count <= 0) { slot->save_idx = -1; return; }
    int cols = GRID_COLS;
    int row = slot->save_idx / cols;
    int col = slot->save_idx % cols;
    row += dy; col += dx;
    if (col < 0) col = 0;
    if (col >= cols) col = cols - 1;
    int new_idx = row * cols + col;
    if (new_idx < 0) new_idx = 0;
    if (new_idx >= slot->save_count) new_idx = slot->save_count - 1;
    slot->save_idx = new_idx;
}

/* Confirmation callbacks */
static int pending_delete_slot = -1;
static int pending_copy_src = -1;
static int pending_copy_dst = -1;
static int pending_format_slot = -1;

static void do_delete_save(void) {
    if (pending_delete_slot >= 0) {
        memcard_delete_save(pending_delete_slot);
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
                        /* Check if destination already has this save dir */
                        memcard_unload_current_vmc();
                        if (mcio_vmcInit(other_slot->loaded_vmc_path) == sceMcResSucceed) {
                            struct io_dirent st;
                            if (mcio_mcStat(slot->saves[slot->save_idx].dir_name, &st) == 0) {
                                /* Exists — confirm overwrite */
                                pending_copy_src = g_active_slot;
                                pending_copy_dst = other;
                                char msg[256];
                                snprintf(msg, sizeof(msg),
                                    "Save %s already exists in Slot %d.\\nOverwrite?",
                                    slot->saves[slot->save_idx].dir_name, other + 1);
                                memcard_show_confirm("CONFIRM OVERWRITE", msg, do_copy_save, NULL);
                            } else {
                                /* Safe to copy */
                                memcard_copy_save_between_slots(g_active_slot, other);
                            }
                            mcio_vmcFinish();
                        }
                    }
                    break;

                case 1: /* Delete save */
                    if (slot->state == SLOT_STATE_VMC_SEL && slot->save_idx >= 0) {
                        pending_delete_slot = g_active_slot;
                        char msg[128];
                        snprintf(msg, sizeof(msg), "Delete save %s?\\nThis cannot be undone.",
                                 slot->saves[slot->save_idx].dir_name);
                        memcard_show_confirm("CONFIRM DELETE", msg, do_delete_save, NULL);
                    }
                    break;

                case 2: /* Export .PSU */
                    /* Phase 2 stub */
                    break;

                case 3: /* Backup full VMC */
                    if (slot->state == SLOT_STATE_VMC_SEL) {
                        memcard_backup_vmc_to_usb(g_active_slot, "/mnt/usb0");
                    }
                    break;

                case 4: /* Import full VMC */
                    /* TODO: file picker or scan USB for .VM2 files */
                    break;

                case 5: /* Format VMC */
                    if (slot->state == SLOT_STATE_VMC_SEL) {
                        pending_format_slot = g_active_slot;
                        memcard_show_confirm("CONFIRM FORMAT",
                            "Format this VMC?\\nAll saves will be lost!", do_format_vmc, NULL);
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
    if (pressed & ORBIS_PAD_BUTTON_LEFT) {
        if (g_active_slot == 1) {
            g_active_slot = 0;
        } else if (slot->focus_element > 0) {
            slot->focus_element--;
        }
    }
    if (pressed & ORBIS_PAD_BUTTON_RIGHT) {
        if (g_active_slot == 0) {
            g_active_slot = 1;
        } else if (slot->focus_element < 2) {
            slot->focus_element++;
        }
    }

    if (pressed & ORBIS_PAD_BUTTON_UP) {
        if (slot->focus_element == 2) {
            move_grid_cursor(slot, 0, -1);
        } else {
            slot->focus_element--;
            if (slot->focus_element < 0) slot->focus_element = 2;
        }
    }
    if (pressed & ORBIS_PAD_BUTTON_DOWN) {
        if (slot->focus_element == 2) {
            move_grid_cursor(slot, 0, 1);
        } else {
            slot->focus_element++;
            if (slot->focus_element > 2) slot->focus_element = 0;
        }
    }

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

    if (pressed & ORBIS_PAD_BUTTON_TRIANGLE) {
        if (slot->state == SLOT_STATE_VMC_SEL) {
            g_memcard_action_menu_open = 1;
            g_memcard_action_sel = 0;
        } else if (slot->state == SLOT_STATE_EMU_SEL) {
            /* Show VMC-only actions */
            g_memcard_action_menu_open = 1;
            g_memcard_action_sel = 3; /* Jump to Backup VMC */
        }
    }

    if (slot->focus_element == 2) {
        if (pressed & ORBIS_PAD_BUTTON_L1) move_grid_cursor(slot, -1, 0);
        if (pressed & ORBIS_PAD_BUTTON_R1) move_grid_cursor(slot, 1, 0);
    }
}
