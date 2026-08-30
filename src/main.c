void _init(void) {}
void _fini(void) {}

#include <string.h>
#include <unistd.h>
#include <stdio.h>
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
#include "cover.h"
#include "memcard.h"
#include "sandbox.h"
#include "sd.h"
#include "memcard_ui.h"
#include "schema.h"
#include "pgsettings.h"
#include "pgsettings_ui.h"

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
#define UI_MODE_PGSETTINGS 3

Schema g_schema;
int g_schema_loaded = 0;
GameSettings g_pgsettings;
PGSettingsUIState g_pg_ui_state;

int main(void) {
    log_debug("=== START ===");

    sceSysmoduleLoadModuleInternal(ORBIS_SYSMODULE_INTERNAL_USER_SERVICE);
    sceSysmoduleLoadModuleInternal(ORBIS_SYSMODULE_INTERNAL_VIDEO_OUT);
    sceSysmoduleLoadModuleInternal(ORBIS_SYSMODULE_INTERNAL_PAD);
    /* Required for sceLncUtilLaunchApp / sceSystemServiceLoadExec to actually
     * resolve at runtime. Linking -lSceLncUtil only gives us the stub table;
     * the real code lives in libSceSystemService.sprx and has to be loaded
     * into this process explicitly, or launch_emulator() silently fails. */
    sceSysmoduleLoadModuleInternal(ORBIS_SYSMODULE_INTERNAL_SYSTEM_SERVICE);
    
    if (!sandbox_bypass()) {
        log_debug("SANDBOX BYPASS FAILED");
    } else {
        log_debug("SANDBOX BYPASSED");
    }

    if (loadPrivLibs() < 0) {
        log_debug("PRIV LIBS LOAD FAILED");
    } else {
        log_debug("PRIV LIBS LOADED");
    }

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
    scraper_init();
    load_good_names();

    scan_games();

    if (game_count == 0) {
        draw_launcher_ui(0, 0, 0);
        flip();
        sceKernelSleep(5);
        memcard_unmount_all();
        scraper_cleanup();
        return 0;
    }

    OrbisPadData pad_data;
    unsigned int old_buttons = 0;
    unsigned int repeat_counter = 0;
    unsigned int repeat_delay = REPEAT_DELAY_BASE;

    int ui_mode = 0;
    int settings_sel = 0;

    memcard_init();

    while (1) {
        if (pad >= 0) {
            scePadReadState(pad, &pad_data);
            unsigned int buttons = pad_data.buttons;
            unsigned int pressed = buttons & ~old_buttons;
            old_buttons = buttons;

            if (ui_mode == 1) {
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
                    if (settings_sel == 6) font_cycle_slot(FONT_SLOT_BODY, (pressed & ORBIS_PAD_BUTTON_RIGHT) ? 1 : -1);
                    if (settings_sel == 7) font_cycle_slot(FONT_SLOT_TITLE, (pressed & ORBIS_PAD_BUTTON_RIGHT) ? 1 : -1);
                    if (settings_sel == 10) {
                        int step = (pressed & ORBIS_PAD_BUTTON_RIGHT) ? 5 : -5;
                        g_settings.panel_opacity += step;
                        if (g_settings.panel_opacity < 0) g_settings.panel_opacity = 0;
                        if (g_settings.panel_opacity > 100) g_settings.panel_opacity = 100;
                    }
                    if (settings_sel == 11) {
                        int step = (pressed & ORBIS_PAD_BUTTON_RIGHT) ? 5 : -5;
                        g_settings.wallpaper_brightness += step;
                        if (g_settings.wallpaper_brightness < 0) g_settings.wallpaper_brightness = 0;
                        if (g_settings.wallpaper_brightness > 100) g_settings.wallpaper_brightness = 100;
                    }
                    if (settings_sel == 3) {
                        const char *urls[] = {
                            "https://raw.githubusercontent.com/xlenore/ps2-covers/main",
                            "https://raw.githubusercontent.com/tehrzky/ps2-covers/main",
                            "https://gitlab.com/xlenore/ps2-covers/-/raw/main"
                        };
                        int num = sizeof(urls) / sizeof(urls[0]);
                        int cur = 0;
                        for (int i = 0; i < num; i++) {
                            if (strcmp(g_settings.scraper_base_url, urls[i]) == 0) { cur = i; break; }
                        }
                        if (pressed & ORBIS_PAD_BUTTON_RIGHT) cur = (cur + 1) % num;
                        else cur = (cur - 1 + num) % num;
                        strncpy(g_settings.scraper_base_url, urls[cur], sizeof(g_settings.scraper_base_url) - 1);
                        g_settings.scraper_base_url[sizeof(g_settings.scraper_base_url) - 1] = '\0';
                    }
                }
                if (pressed & ORBIS_PAD_BUTTON_CROSS) {
                    if (settings_sel == 6) font_cycle_slot(FONT_SLOT_BODY, 1);
                    if (settings_sel == 7) font_cycle_slot(FONT_SLOT_TITLE, 1);
                    if (settings_sel == 8) scraper_force_download_gameindex();
                    if (settings_sel == 9) {
                        for (int i = 0; i < game_count; i++) scraper_force_download_cover(games[i].id);
                    }
                }
            } else if (ui_mode == 2) {
                if (pressed & ORBIS_PAD_BUTTON_CIRCLE) {
                    memcard_unmount_all();
                    ui_mode = 0;
                }
                memcard_ui_handle_input(pressed, buttons);
            } else if (ui_mode == UI_MODE_PGSETTINGS) {
                if (pgsettings_ui_handle_input(pressed, buttons, &g_schema,
                                                &g_pgsettings, &g_pg_ui_state)) {
                    if (!g_pg_ui_state.active) {
                        ui_mode = 0;
                    }
                }
            } else {
                if (pressed & ORBIS_PAD_BUTTON_CROSS) {
                    char emu_tid[32] = {0};
                    if (set_active_game(games[selected].path, games[selected].id,
                                        games[selected].name, emu_tid, sizeof(emu_tid))) {
                        draw_launcher_ui(game_count, selected, game_count);
                        int lw = font_text_width("LAUNCHING...", 42);
                        draw_text(SCREEN_WIDTH/2 - lw/2, SCREEN_HEIGHT/2, "LAUNCHING...", 0xFFFFD700, 42);
                        flip();
                        sceKernelSleep(1);
                        memcard_unmount_all();
                        launch_emulator(emu_tid);
                    }
                }
                if (pressed & ORBIS_PAD_BUTTON_CIRCLE) {
                    memcard_unmount_all();
                    scraper_cleanup();
                    return 0;
                }
                if (pressed & ORBIS_PAD_BUTTON_TRIANGLE) {
                    ui_mode = 1;
                    settings_sel = 0;
                }
                if (pressed & ORBIS_PAD_BUTTON_L1 || pressed & ORBIS_PAD_BUTTON_R1) {
                    memcard_unmount_all();
                    memcard_init();
                    ui_mode = 2;
                }
                if (pressed & ORBIS_PAD_BUTTON_SQUARE) {
                    if (!g_schema_loaded) {
                        char schema_path[512];
                        snprintf(schema_path, sizeof(schema_path),
                                 "%s/config/schema.json", g_settings.work_path);
                        g_schema_loaded = (schema_load(schema_path, &g_schema) == 0);
                    }
                    if (g_schema_loaded && game_count > 0 && selected >= 0) {
                        pgsettings_load(games[selected].id, &g_schema, &g_pgsettings);
                        pgsettings_ui_init(&g_pg_ui_state,
                                            games[selected].display_name,
                                            games[selected].id);
                        ui_mode = UI_MODE_PGSETTINGS;
                    }
                }

                if (pressed & ORBIS_PAD_BUTTON_UP) {
                    selected = (selected - 1 + game_count) % game_count;
                    repeat_counter = 0;
                }
                if (pressed & ORBIS_PAD_BUTTON_DOWN) {
                    selected = (selected + 1) % game_count;
                    repeat_counter = 0;
                }

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
        } else if (ui_mode == 2) {
            draw_memcard_ui();
        } else if (ui_mode == UI_MODE_PGSETTINGS) {
            draw_launcher_ui(game_count, selected, game_count);
            draw_pgsettings_ui(&g_schema, &g_pgsettings, &g_pg_ui_state);
        } else {
            draw_launcher_ui(game_count, selected, game_count);
        }
        flip();
        sceKernelUsleep(16666);
    }

    font_cleanup();
    cover_free_wallpaper();
    cover_cleanup();
    scraper_cleanup();
    memcard_unmount_all();
    
    return 0;
}
