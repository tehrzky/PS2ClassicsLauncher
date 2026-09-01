#include "upload_qr_ui.h"
#include "local_upload_server.h"
#include "video.h"
#include "font.h"
#include "colors.h"
#include "qrcodegen.h"
#include <orbis/Pad.h>
#include <string.h>
#include <stdio.h>

#define SCREEN_WIDTH  1920
#define SCREEN_HEIGHT 1080

static int s_open = 0;
static char s_url[128] = "";

void upload_qr_ui_open(void) {
    local_upload_server_start(); /* no-op if already running */
    local_upload_server_get_url(s_url, sizeof(s_url));
    s_open = 1;
}

int upload_qr_ui_is_open(void) { return s_open; }

int upload_qr_ui_handle_input(unsigned int pressed) {
    if (!s_open) return 0;
    if (pressed & ORBIS_PAD_BUTTON_CIRCLE) s_open = 0;
    return 1;
}

static void draw_qr(const uint8_t *qr, int x, int y, int module_px) {
    int size = qrcodegen_getSize(qr);
    /* Quiet zone (blank border) matters for real-world scanners. */
    int quiet = 4 * module_px;
    int total = size * module_px + quiet * 2;

    draw_rect(x, y, total, total, 0xFFFFFFFF);
    for (int qy = 0; qy < size; qy++) {
        for (int qx = 0; qx < size; qx++) {
            if (qrcodegen_getModule(qr, qx, qy)) {
                draw_rect(x + quiet + qx * module_px, y + quiet + qy * module_px,
                           module_px, module_px, 0xFF000000);
            }
        }
    }
}

void draw_upload_qr_ui(void) {
    if (!s_open) return;

    draw_rect(0, 0, SCREEN_WIDTH, SCREEN_HEIGHT, 0xE0000000);

    int panel_w = 760, panel_h = 760;
    int px = (SCREEN_WIDTH - panel_w) / 2;
    int py = (SCREEN_HEIGHT - panel_h) / 2;
    draw_rounded_rect(px, py, panel_w, panel_h, 14, COLOR_BORDER);
    draw_rounded_rect(px + 2, py + 2, panel_w - 4, panel_h - 4, 12, COLOR_PANEL);

    const char *title = "Upload a Lua Patch";
    int tw = font_text_width_slot(title, 32, FONT_SLOT_BOLD);
    draw_text_slot(px + (panel_w - tw) / 2, py + 24, title, COLOR_GOLD, 32, FONT_SLOT_BOLD);

    static uint8_t qr_temp[qrcodegen_BUFFER_LEN_MAX];
    static uint8_t qr_out[qrcodegen_BUFFER_LEN_MAX];
    int ok = qrcodegen_encodeText(s_url, qr_temp, qr_out,
                                   qrcodegen_Ecc_MEDIUM,
                                   qrcodegen_VERSION_MIN, qrcodegen_VERSION_MAX,
                                   qrcodegen_Mask_AUTO, true);

    if (ok) {
        int size = qrcodegen_getSize(qr_out);
        int module_px = 5;
        int qr_total = size * module_px + 8 * module_px;
        int qx = px + (panel_w - qr_total) / 2;
        int qy = py + 80;
        draw_qr(qr_out, qx, qy, module_px);

        int addr_y = qy + qr_total + 24;
        int aw = font_text_width(s_url, 26);
        draw_text(px + (panel_w - aw) / 2, addr_y, s_url, COLOR_ACCENT, 26);

        const char *hint1 = "Scan with your phone, or type that address into any";
        const char *hint2 = "browser on the same network (phone or PC).";
        int h1w = font_text_width(hint1, 20);
        int h2w = font_text_width(hint2, 20);
        draw_text(px + (panel_w - h1w) / 2, addr_y + 36, hint1, COLOR_DIM, 20);
        draw_text(px + (panel_w - h2w) / 2, addr_y + 58, hint2, COLOR_DIM, 20);
    } else {
        const char *err = "Could not build QR code -- use the address below.";
        int ew = font_text_width(err, 22);
        draw_text(px + (panel_w - ew) / 2, py + 200, err, COLOR_ERROR, 22);
        int aw = font_text_width(s_url, 26);
        draw_text(px + (panel_w - aw) / 2, py + 240, s_url, COLOR_ACCENT, 26);
    }

    const char *close_hint = "O Close";
    int cw = font_text_width(close_hint, 22);
    draw_text(px + (panel_w - cw) / 2, py + panel_h - 40, close_hint, COLOR_DIM, 22);
}
