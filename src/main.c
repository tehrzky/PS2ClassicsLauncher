void _init(void) {}
void _fini(void) {}

#include <orbis/libkernel.h>
#include <orbis/SystemService.h>
#include <orbis/UserService.h>
#include <orbis/Pad.h>
#include <orbis/Sysmodule.h>
#include <stdlib.h>
#include <string.h>

#include "debug.h"
#include "video.h"
#include "font.h"
#include "game.h"
#include "config.h"
#include "launcher.h"
#include "goodnames.h"
#include "ui.h"

// ============ CONFIG ============
#define EMULATOR_TID "PCSX20042"

// ============ SCREEN CONSTANTS ============
#define SCREEN_WIDTH    1920
#define SCREEN_HEIGHT   1080

// ============ EMBEDDED DEFAULT CONFIG ============
const char *embedded_default =
"--max-disc-num=1\n"
"--ps2-lang=system\n"
"--host-osd=0\n"
"--host-audio=1\n"
"--host-display-mode=normal\n"
"--gs-uprender=2x2\n"
"--gs-upscale=EdgeSmooth\n"
"--path-patches=\"/data/PS4ROMS/PS2ISO/patches/\"\n"
"--path-featuredata=\"/data/PS4ROMS/PS2ISO/feature_data/\"\n"
"--load-feature-lua=0\n"
"--trophy-support=0\n";

// ============ BUTTON REPEAT CONSTANTS ============
#define REPEAT_DELAY_FAST    5
#define REPEAT_DELAY_L2_FAST 2

// ============ MAIN ============
int main(void) {
    log_debug("=== START ===");

    sceSysmoduleLoadModuleInternal(ORBIS_SYSMODULE_INTERNAL_USER_SERVICE);
    sceSysmoduleLoadModuleInternal(ORBIS_SYSMODULE_INTERNAL_VIDEO_OUT);
    sceSysmoduleLoadModuleInternal(ORBIS_SYSMODULE_INTERNAL_PAD);
    log_debug("MODULES LOADED");

    if (init_video() < 0) {
        log_debug("VIDEO FAIL");
        return -1;
    }
    log_debug("VIDEO OK");

    int userInitRc = sceUserServiceInitialize(NULL);
    log_debug("USER SERVICE INIT: %d", userInitRc);

    scePadInit();
    log_debug("PAD INIT DONE");

    int userId = 1;
    int ret = sceUserServiceGetInitialUser(&userId);
    log_debug("GetInitialUser: %d (uid=%d)", ret, userId);

    int pad = scePadOpen(userId, ORBIS_PAD_PORT_TYPE_STANDARD, 0, NULL);
    log_debug("PAD OPEN: %d (uid=%d)", pad, userId);

    scraper_download_gameindex();
    load_good_names();
    log_debug("GOOD NAMES loaded");

    scan_games();
    log_debug("GAMES: %d", game_count);

    if (game_count == 0) {
        draw_launcher_ui(game_count, 0, 0);
        flip();
        sceKernelSleep(5);
        return 0;
    }

    OrbisPadData pad_data;
    unsigned int old_buttons = 0;
    unsigned int repeat_counter = 0;
    unsigned int repeat_delay = 20;

    while (1) {
        if (pad >= 0) {
            scePadReadState(pad, &pad_data);
            unsigned int buttons = pad_data.buttons;
            unsigned int pressed = buttons & ~old_buttons;
            old_buttons = buttons;

            // --- Single press actions ---
            if (pressed & ORBIS_PAD_BUTTON_CROSS) {
                log_debug("LAUNCH: %s", games[selected].display_name);
                char emu_tid[32] = {0};
                if (set_active_game(games[selected].path, games[selected].id,
                                    games[selected].name, emu_tid, sizeof(emu_tid))) {
                    draw_launcher_ui(game_count, selected, game_count);
                    draw_text_scaled(SCREEN_WIDTH/2 - 80, SCREEN_HEIGHT/2, "LAUNCHING...", 0xFFFFD700, 3);
                    flip();
                    sceKernelSleep(1);
                    launch_emulator(emu_tid);
                } else {
                    log_debug("set_active_game FAILED for %s", games[selected].name);
                    draw_launcher_ui(game_count, selected, game_count);
                    draw_text_scaled(SCREEN_WIDTH/2 - 120, SCREEN_HEIGHT/2, "CONFIG WRITE FAILED!", 0xFFFF0000, 3);
                    flip();
                    sceKernelSleep(2);
                }
            }
            if (pressed & ORBIS_PAD_BUTTON_CIRCLE) {
                log_debug("EXIT requested");
                return 0;
            }

            // --- Immediate movement on press ---
            if (pressed & ORBIS_PAD_BUTTON_UP) {
                selected = (selected - 1 + game_count) % game_count;
                repeat_counter = 0;
                repeat_delay = 20;
            }
            if (pressed & ORBIS_PAD_BUTTON_DOWN) {
                selected = (selected + 1) % game_count;
                repeat_counter = 0;
                repeat_delay = 20;
            }

            // --- L2+UP/DOWN fast jump on press ---
            if (pressed & ORBIS_PAD_BUTTON_L2) {
                if (pressed & ORBIS_PAD_BUTTON_UP) {
                    selected = (selected - 5 + game_count) % game_count;
                    if (selected < 0) selected = 0;
                }
                if (pressed & ORBIS_PAD_BUTTON_DOWN) {
                    selected = (selected + 5) % game_count;
                    if (selected >= game_count) selected = game_count - 1;
                }
            }

            // --- Held repeat for continuous scrolling ---
            int move = 0;
            if (buttons & ORBIS_PAD_BUTTON_UP) move = -1;
            else if (buttons & ORBIS_PAD_BUTTON_DOWN) move = 1;

            if (move != 0) {
                if (buttons & ORBIS_PAD_BUTTON_L2) {
                    repeat_delay = REPEAT_DELAY_L2_FAST;
                } else {
                    repeat_delay = REPEAT_DELAY_FAST;
                }
                repeat_counter++;
                if (repeat_counter >= repeat_delay) {
                    int new_sel = selected + move;
                    if (new_sel < 0) new_sel = 0;
                    if (new_sel >= game_count) new_sel = game_count - 1;
                    selected = new_sel;
                    repeat_counter = 0;
                }
            } else {
                repeat_counter = 0;
                repeat_delay = 20;
            }
        }

        draw_launcher_ui(game_count, selected, game_count);
        flip();
        sceKernelUsleep(16666);
    }

    return 0;
}
