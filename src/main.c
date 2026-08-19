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

// ============ SCROLLING ============
static int scroll_hold_counter = 0;
static int scroll_direction = 0;
static const int SCROLL_DELAY = 12;      // Frames between scrolls (slow)
static const int FAST_SCROLL_DELAY = 3;  // Frames between scrolls (fast with L2)

// ============ CONFIG ============
#define EMULATOR_TID "PCSX20042"

// ============ UI COLORS (for main.c) ============
#define COLOR_GOLD         0xFFFFD700
#define COLOR_SUCCESS      0xFF00FF00
#define COLOR_ERROR        0xFFFF0000
#define COLOR_TEXT_PRIMARY 0xFFFFFFFF
#define COLOR_TEXT_SECONDARY 0xFFAAAAAA

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

    load_good_names();
    log_debug("GOOD NAMES loaded");

    scan_games();
    log_debug("GAMES: %d", game_count);

    if (game_count == 0) {
        draw_launcher_ui(game_count, selected);
        flip();
        sceKernelSleep(5);
        return 0;
    }

    OrbisPadData pad_data;
    unsigned int old_buttons = 0;

    while (1) {
        if (pad >= 0) {
            scePadReadState(pad, &pad_data);
            unsigned int buttons = pad_data.buttons;
            unsigned int pressed = buttons & ~old_buttons;
            old_buttons = buttons;

            // L2 held for fast scroll
            int fast = (buttons & ORBIS_PAD_BUTTON_L2) != 0;

            // --- Single step (press) ---
            if (pressed & ORBIS_PAD_BUTTON_UP) {
                selected = (selected - 1 + game_count) % game_count;
                scroll_hold_counter = 0;
                scroll_direction = -1;
            } else if (pressed & ORBIS_PAD_BUTTON_DOWN) {
                selected = (selected + 1) % game_count;
                scroll_hold_counter = 0;
                scroll_direction = 1;
            }

            // --- Holding D-pad (auto-scroll) ---
            // Only auto-scroll if we're not in the first frame after a press
            if ((buttons & ORBIS_PAD_BUTTON_UP) && !(pressed & ORBIS_PAD_BUTTON_UP)) {
                scroll_hold_counter++;
                int delay = fast ? FAST_SCROLL_DELAY : SCROLL_DELAY;
                if (scroll_hold_counter >= delay) {
                    selected = (selected - 1 + game_count) % game_count;
                    scroll_hold_counter = 0;
                    scroll_direction = -1;
                }
            } else if ((buttons & ORBIS_PAD_BUTTON_DOWN) && !(pressed & ORBIS_PAD_BUTTON_DOWN)) {
                scroll_hold_counter++;
                int delay = fast ? FAST_SCROLL_DELAY : SCROLL_DELAY;
                if (scroll_hold_counter >= delay) {
                    selected = (selected + 1) % game_count;
                    scroll_hold_counter = 0;
                    scroll_direction = 1;
                }
            } else {
                // Reset counter when no D-pad is held
                if (!(buttons & ORBIS_PAD_BUTTON_UP) && !(buttons & ORBIS_PAD_BUTTON_DOWN)) {
                    scroll_hold_counter = 0;
                    scroll_direction = 0;
                }
            }

            // --- X button: Launch ---
            if (pressed & ORBIS_PAD_BUTTON_CROSS) {
                log_debug("LAUNCH: %s", games[selected].display_name);
                char emu_tid[32] = {0};
                if (set_active_game(games[selected].path, games[selected].id,
                                    games[selected].name, emu_tid, sizeof(emu_tid))) {
                    // Draw launch screen
                    draw_launcher_ui(game_count, selected);
                    draw_text_scaled(80, 500, "LAUNCHING...", COLOR_GOLD, 3);
                    flip();
                    sceKernelSleep(1);
                    launch_emulator(emu_tid);
                } else {
                    log_debug("set_active_game FAILED for %s", games[selected].name);
                    draw_launcher_ui(game_count, selected);
                    draw_text_scaled(80, 500, "CONFIG WRITE FAILED!", COLOR_ERROR, 3);
                    flip();
                    sceKernelSleep(2);
                }
            }

            // --- Circle button: Exit ---
            if (pressed & ORBIS_PAD_BUTTON_CIRCLE) {
                log_debug("EXIT requested");
                break;
            }
        }

        draw_launcher_ui(game_count, selected);
        flip();
        sceKernelUsleep(16666);
    }

    return 0;
}
