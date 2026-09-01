#include "pgsettings_textview.h"
#include "video.h"
#include "font.h"
#include "colors.h"
#include <orbis/Pad.h>
#include <string.h>
#include <stdio.h>

#define SCREEN_WIDTH   1920
#define SCREEN_HEIGHT  1080

#define TV_X       160
#define TV_Y        90
#define TV_W       (SCREEN_WIDTH  - TV_X * 2)
#define TV_H       (SCREEN_HEIGHT - TV_Y * 2)
#define TV_PAD      24
#define TV_LINE_H   26
#define TV_FONT_PX  20
#define TV_VISIBLE_LINES ((TV_H - TV_PAD * 2 - 60) / TV_LINE_H)

/* Split the buffer into line start offsets. Cheap: only runs when the
 * view is (re)opened or edited, never every frame. */
static int tv_line_offsets[256];
static int tv_line_count = 0;

static void tv_reindex_lines(PGSettingsUIState *st) {
    tv_line_count = 0;
    tv_line_offsets[tv_line_count++] = 0;
    for (int i = 0; st->textview_buf[i] && tv_line_count < 256; i++) {
        if (st->textview_buf[i] == '\n') {
            tv_line_offsets[tv_line_count++] = i + 1;
        }
    }
}

void pgsettings_textview_open(PGSettingsUIState *st, const Schema *schema, const GameSettings *settings) {
    if (!st || !schema || !settings) return;
    int written = pgsettings_generate_commands(settings, schema, st->textview_buf, sizeof(st->textview_buf));
    if (written < 0) st->textview_buf[0] = '\0';
    st->textview_active = 1;
    st->textview_edit_mode = 0;
    st->textview_scroll = 0;
    st->textview_cursor = 0;
    tv_reindex_lines(st);
}

int pgsettings_textview_handle_input(unsigned int pressed, unsigned int held, PGSettingsUIState *st) {
    (void)held;
    if (!st || !st->textview_active) return 0;

    if (!st->textview_edit_mode) {
        if (pressed & ORBIS_PAD_BUTTON_UP)   st->textview_scroll--;
        if (pressed & ORBIS_PAD_BUTTON_DOWN) st->textview_scroll++;
        if (pressed & ORBIS_PAD_BUTTON_L1)   st->textview_scroll -= TV_VISIBLE_LINES;
        if (pressed & ORBIS_PAD_BUTTON_R1)   st->textview_scroll += TV_VISIBLE_LINES;
        if (st->textview_scroll < 0) st->textview_scroll = 0;
        if (st->textview_scroll > tv_line_count - 1) st->textview_scroll = tv_line_count - 1;
        if (st->textview_scroll < 0) st->textview_scroll = 0;

        if (pressed & ORBIS_PAD_BUTTON_CROSS) {
            /* Real text editing intentionally isn't done via sceImeDialog
             * here -- see the reply this shipped with for why (short
             * version: it needs system IME modules this jailbroken build
             * doesn't currently load, and is known to be unreliable for
             * unsigned homebrew, which is why tools like ps4xplorer draw
             * their own keyboard instead of using Sony's). The plan is to
             * reuse the same local upload_qr_ui/local_upload_server
             * mechanism for editing too -- a browser textarea on the
             * phone/PC already IS a better keyboard than anything drawn
             * on-screen. Not wired up yet, so CROSS is a no-op here for
             * now. */
        }
        if (pressed & ORBIS_PAD_BUTTON_CIRCLE) {
            st->textview_active = 0;
        }
        return 1;
    }

    /* Fallback path when PGSETTINGS_HAVE_IME isn't defined yet: just bail
     * back out of edit mode on any confirm/cancel press so the overlay
     * never gets stuck. */
    if (pressed & (ORBIS_PAD_BUTTON_CIRCLE | ORBIS_PAD_BUTTON_CROSS)) {
        st->textview_edit_mode = 0;
    }
    return 1;
}

void draw_pgsettings_textview(PGSettingsUIState *st) {
    if (!st || !st->textview_active) return;

    draw_rect(0, 0, SCREEN_WIDTH, SCREEN_HEIGHT, 0xD0000000);
    draw_rounded_rect(TV_X, TV_Y, TV_W, TV_H, 12, COLOR_BORDER);
    draw_rounded_rect(TV_X + 2, TV_Y + 2, TV_W - 4, TV_H - 4, 10, COLOR_PANEL);

    draw_text_slot(TV_X + TV_PAD, TV_Y + 18, "Result Preview", COLOR_GOLD, 28, FONT_SLOT_BOLD);
    draw_text(TV_X + TV_PAD, TV_Y + 54,
              "This is exactly what will be written for this game's current settings.",
              COLOR_DIM, 18);

    int content_y = TV_Y + 92;
    int content_x = TV_X + TV_PAD;

    if (tv_line_count <= 1 && st->textview_buf[0] == '\0') {
        draw_text(content_x, content_y, "(no non-default settings -- nothing will be generated)", COLOR_DIM, TV_FONT_PX);
    } else {
        int end = st->textview_scroll + TV_VISIBLE_LINES;
        if (end > tv_line_count) end = tv_line_count;
        int y = content_y;
        for (int li = st->textview_scroll; li < end; li++) {
            int start = tv_line_offsets[li];
            int stop = (li + 1 < tv_line_count) ? tv_line_offsets[li + 1] - 1 : (int)strlen(st->textview_buf);
            char line[256];
            int len = stop - start;
            if (len < 0) len = 0;
            if (len > (int)sizeof(line) - 1) len = sizeof(line) - 1;
            memcpy(line, st->textview_buf + start, len);
            line[len] = '\0';
            if (line[0]) draw_text(content_x, y, line, COLOR_TEXT, TV_FONT_PX);
            y += TV_LINE_H;
        }
    }

    char footer[64];
    snprintf(footer, sizeof(footer), "Line %d / %d", st->textview_scroll + 1, tv_line_count);
    draw_text(content_x, TV_Y + TV_H - 40, footer, COLOR_DIM, 20);

    int fx = TV_X + TV_W - TV_PAD - 420;
    int fy = TV_Y + TV_H - 40;
    /* Font only rasterizes ASCII 32-126 (see font.c), so keep hints plain. */
    draw_text(fx, fy, "UP/DOWN Scroll   L1/R1 Page   O Close", COLOR_DIM, 18);
}
