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

void upload_qr_ui_open(const char *display_name, const char *disc_id,
                       const char *config_name, const char *iso_path) {
    local_upload_server_start(); /* no-op if already running */
    local_upload_server_set_game_context(display_name, disc_id, config_name, iso_path);
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

    int panel_w = 1120, panel_h = 980;
    int px = (SCREEN_WIDTH - panel_w) / 2;
    int py = (SCREEN_HEIGHT - panel_h) / 2;
    draw_rounded_rect(px, py, panel_w, panel_h, 16, COLOR_BORDER);
    draw_rounded_rect(px + 3, py + 3, panel_w - 6, panel_h - 6, 14, COLOR_PANEL);

    const char *title = "Upload / Edit Game Files";
    int tw = font_text_width_slot(title, 44, FONT_SLOT_BOLD);
    draw_text_slot(px + (panel_w - tw) / 2, py + 34, title, COLOR_GOLD, 44, FONT_SLOT_BOLD);

    static uint8_t qr_temp[qrcodegen_BUFFER_LEN_MAX];
    static uint8_t qr_out[qrcodegen_BUFFER_LEN_MAX];
    int ok = qrcodegen_encodeText(s_url, qr_temp, qr_out,
                                   qrcodegen_Ecc_MEDIUM,
                                   qrcodegen_VERSION_MIN, qrcodegen_VERSION_MAX,
                                   qrcodegen_Mask_AUTO, true);

    if (ok) {
        int size = qrcodegen_getSize(qr_out);
        int module_px = 8;
        int qr_total = size * module_px + 8 * module_px;
        int qx = px + (panel_w - qr_total) / 2;
        int qy = py + 110;
        draw_qr(qr_out, qx, qy, module_px);

        int addr_y = qy + qr_total + 34;
        int aw = font_text_width(s_url, 34);
        draw_text(px + (panel_w - aw) / 2, addr_y, s_url, COLOR_ACCENT, 34);

        const char *hint1 = "Scan with your phone, or type that address into any";
        const char *hint2 = "browser on the same network (phone or PC).";
        int h1w = font_text_width(hint1, 24);
        int h2w = font_text_width(hint2, 24);
        draw_text(px + (panel_w - h1w) / 2, addr_y + 48, hint1, COLOR_DIM, 24);
        draw_text(px + (panel_w - h2w) / 2, addr_y + 76, hint2, COLOR_DIM, 24);
    } else {
        const char *err = "Could not build QR code -- use the address below.";
        int ew = font_text_width(err, 26);
        draw_text(px + (panel_w - ew) / 2, py + 300, err, COLOR_ERROR, 26);
        int aw = font_text_width(s_url, 34);
        draw_text(px + (panel_w - aw) / 2, py + 350, s_url, COLOR_ACCENT, 34);
    }

    const char *close_hint = "O Close";
    int cw = font_text_width(close_hint, 26);
    draw_text(px + (panel_w - cw) / 2, py + panel_h - 50, close_hint, COLOR_DIM, 26);
}
