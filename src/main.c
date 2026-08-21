void _init(void) {}
void _fini(void) {}

#include <string.h>
#include <unistd.h>
#include <orbis/libkernel.h>
#include <orbis/Sysmodule.h>
#include <orbis/SystemService.h>
#include <orbis/UserService.h>
#include <orbis/Pad.h>
#include <orbis/VideoOut.h>

#include "debug.h"
#include "video.h"
#include "font.h"
#include "game.h"
#include "scraper.h"
#include "config.h"
#include "launcher.h"
#include "goodnames.h"
#include "ui.h"
#include "settings.h"

#define SCREEN_WIDTH 1920
#define SCREEN_HEIGHT 1080

#define EMULATOR_TID "PCSX20042"

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

#define REPEAT_DELAY_BASE 2
#define REPEAT_DELAY_FAST 1

int main(void) {
    log_debug("=== START ===");

    sceSysmoduleLoadModuleInternal(ORBIS_SYSMODULE_INTERNAL_USER_SERVICE);
    sceSysmoduleLoadModuleInternal(ORBIS_SYSMODULE_INTERNAL_VIDEO_OUT);
    sceSysmoduleLoadModuleInternal(ORBIS_SYSMODULE_INTERNAL_PAD);

    if (init_video() < 0) {
        log_debug("VIDEO FAIL");
        return -1;
    }

    sceUserServiceInitialize(NULL);
    scePadInit();

    int userId = 1;
    sceUserServiceGetInitialUser(&userId);
    int pad = scePadOpen(userId, ORBIS_PAD_PORT_TYPE_STANDARD, 0, NULL);

    settings_load();
    font_init();

    scraper_download_gameindex();
    load_good_names();

    scan_games();

    if (game_count == 0) {
        draw_launcher_ui(0, 0, 0);
        flip();
        sceKernelSleep(5);
        return 0;
    }

    OrbisPadData pad_data;
    unsigned int old_buttons = 0;
    unsigned int repeat_counter = 0;
    unsigned int repeat_delay = REPEAT_DELAY_BASE;

    int ui_mode = 0;        // 0=launcher, 1=settings
    int settings_sel = 0;

    while (1) {
        if (pad >= 0) {
            scePadReadState(pad, &pad_data);
            unsigned int buttons = pad_data.buttons;
            unsigned int pressed = buttons & ~old_buttons;
            old_buttons = buttons;

            if (ui_mode == 1) {
                // ===== SETTINGS MODE =====
                if (pressed & ORBIS_PAD_BUTTON_CIRCLE) {
                    settings_save();
                    ui_mode = 0;
                }
                if (pressed & ORBIS_PAD_BUTTON_UP) {
                    settings_sel = (settings_sel - 1 + SETTINGS_ITEMS) % SETTINGS_ITEMS;
                }
                if (pressed & ORBIS_PAD_BUTTON_DOWN) {
                    settings_sel = (settings_sel + 1) % SETTINGS_ITEMS;
                }
                                if (pressed & ORBIS_PAD_BUTTON_LEFT || pressed & ORBIS_PAD_BUTTON_RIGHT) {
                    if (settings_sel == 0) g_settings.auto_download_covers ^= 1;
                    if (settings_sel == 1) g_settings.auto_download_gameindex ^= 1;
                    if (settings_sel == 2) g_settings.cover_type ^= 1;
                    if (settings_sel == 3) {
                        // Cycle through preset scraper URLs
                        const char *urls[] = {
                            "https://raw.githubusercontent.com/xlenore/ps2-covers/main",
                            "https://raw.githubusercontent.com/tehrzky/ps2-covers/main",
                            "https://gitlab.com/xlenore/ps2-covers/-/raw/main"
                        };
                        int num_urls = sizeof(urls) / sizeof(urls[0]);
                        int current = 0;
                        for (int i = 0; i < num_urls; i++) {
                            if (strcmp(g_settings.scraper_base_url, urls[i]) == 0) {
                                current = i; break;
                            }
                        }
                        if (pressed & ORBIS_PAD_BUTTON_RIGHT) {
                            current = (current + 1) % num_urls;
                        } else {
                            current = (current - 1 + num_urls) % num_urls;
                        }
                        strncpy(g_settings.scraper_base_url, urls[current],
                                sizeof(g_settings.scraper_base_url) - 1);
                        g_settings.scraper_base_url[sizeof(g_settings.scraper_base_url) - 1] = '\0';
                    }
                }
                if (pressed & ORBIS_PAD_BUTTON_CROSS) {
                    if (settings_sel == 3) {
                        // X also cycles URL forward
                        const char *urls[] = {
                            "https://raw.githubusercontent.com/xlenore/ps2-covers/main",
                            "https://raw.githubusercontent.com/tehrzky/ps2-covers/main",
                            "https://gitlab.com/xlenore/ps2-covers/-/raw/main"
                        };
                        int num_urls = sizeof(urls) / sizeof(urls[0]);
                        int current = 0;
                        for (int i = 0; i < num_urls; i++) {
                            if (strcmp(g_settings.scraper_base_url, urls[i]) == 0) {
                                current = i; break;
                            }
                        }
                        current = (current + 1) % num_urls;
                        strncpy(g_settings.scraper_base_url, urls[current],
                                sizeof(g_settings.scraper_base_url) - 1);
                        g_settings.scraper_base_url[sizeof(g_settings.scraper_base_url) - 1] = '\0';
                    }
                    if (settings_sel == 4) scraper_force_download_gameindex();
                    if (settings_sel == 5) {
                        for (int i = 0; i < game_count; i++) {
                            scraper_force_download_cover(games[i].id);
                        }
                    }
                }
                if (pressed & ORBIS_PAD_BUTTON_CROSS) {
                    if (settings_sel == 4) scraper_force_download_gameindex();
                    if (settings_sel == 5) {
                        for (int i = 0; i < game_count; i++) {
                            scraper_force_download_cover(games[i].id);
                        }
                    }
                }
            } else {
                // ===== LAUNCHER MODE =====
                if (pressed & ORBIS_PAD_BUTTON_CROSS) {
                    char emu_tid[32] = {0};
                    if (set_active_game(games[selected].path, games[selected].id,
                                        games[selected].name, emu_tid, sizeof(emu_tid))) {
                        draw_launcher_ui(game_count, selected, game_count);
                        draw_text_scaled(SCREEN_WIDTH/2 - 80, SCREEN_HEIGHT/2, "LAUNCHING...", 0xFFFFD700, 3);
                        flip();
                        sceKernelSleep(1);
                        launch_emulator(emu_tid);
                    }
                }
                if (pressed & ORBIS_PAD_BUTTON_CIRCLE) {
                    return 0;
                }
                if (pressed & ORBIS_PAD_BUTTON_TRIANGLE) {
                    ui_mode = 1;
                    settings_sel = 0;
                }

                // Single press movement
                if (pressed & ORBIS_PAD_BUTTON_UP) {
                    selected = (selected - 1 + game_count) % game_count;
                    repeat_counter = 0;
                }
                if (pressed & ORBIS_PAD_BUTTON_DOWN) {
                    selected = (selected + 1) % game_count;
                    repeat_counter = 0;
                }

                // Held repeat scrolling
                int move = 0;
                if (buttons & ORBIS_PAD_BUTTON_UP) move = -1;
                else if (buttons & ORBIS_PAD_BUTTON_DOWN) move = 1;

                if (move != 0) {
                    int step = (buttons & ORBIS_PAD_BUTTON_L2) ? 5 : 1;
                    repeat_delay = (buttons & ORBIS_PAD_BUTTON_L2) ? REPEAT_DELAY_FAST : REPEAT_DELAY_BASE;
                    repeat_counter++;
                    if (repeat_counter >= repeat_delay) {
                        int new_sel = selected + move * step;
                        if (new_sel < 0) new_sel = 0;
                        if (new_sel >= game_count) new_sel = game_count - 1;
                        selected = new_sel;
                        repeat_counter = 0;
                    }
                } else {
                    repeat_counter = 0;
                }
            }
        }

        if (ui_mode == 1) {
            draw_launcher_ui(game_count, selected, game_count);
            draw_settings_ui(settings_sel, 0);
        } else {
            draw_launcher_ui(game_count, selected, game_count);
        }
        flip();
        sceKernelUsleep(16666);
    }

    return 0;
}
