#include "launcher.h"
#include "debug.h"
#include "config.h"
#include <string.h>
#include <orbis/libkernel.h>
#include <orbis/Sysmodule.h>
#include <orbis/SystemService.h>
#include <orbis/UserService.h>

/* Error codes returned by sceLncUtilLaunchApp */
#define SCE_LNC_ERROR_APP_NOT_FOUND                    0x80D00501
#define SCE_LNC_UTIL_ERROR_ALREADY_RUNNING             0x80D00504
#define SCE_LNC_UTIL_ERROR_ALREADY_RUNNING_SUSPEND_NEEDED 0x80D0050B
#define SCE_LNC_UTIL_ERROR_ALREADY_RUNNING_KILL_NEEDED    0x80D0050C
#define SCE_LNC_UTIL_ERROR_APPHOME_EBOOTBIN_NOT_FOUND  0x80D0050D
#define SCE_LNC_UTIL_ERROR_APPHOME_PARAMSFO_NOT_FOUND  0x80D0050E
#define SCE_LNC_UTIL_ERROR_NO_SFOKEY_IN_APP_INFO       0x80D00510
#define SCE_LNC_UTIL_ERROR_SETUP_FS_SANDBOX            0x80D00509
#define SCE_LNC_UTIL_ERROR_INVALID_TITLE_ID            0x80D0050A

int launch_emulator(const char *override_tid)
{
    const char *title_id = (override_tid && override_tid[0]) ? override_tid : EMULATOR_TID;

    log_debug("=== LAUNCH EMULATOR ===");
    log_debug("Title ID: %s", title_id);

    int userId = 1;
    int ret = sceUserServiceGetForegroundUser(&userId);
    if (ret < 0) {
        log_debug("sceUserServiceGetForegroundUser failed: 0x%08X, defaulting to 1", ret);
        userId = 1;
    }
    log_debug("User ID: %d", userId);

    /* Use the SDK struct directly — field names and flag values from
     * <orbis/_types/sys_service.h>. The old custom struct had wrong field
     * names (sz/check_flag) and a wrong flag value (0x20000 vs 2). */
    LncAppParam param;
    memset(&param, 0, sizeof(LncAppParam));
    param.size               = sizeof(LncAppParam);
    param.user_id            = (uint32_t)userId;
    param.app_opt            = 0;
    param.crash_report       = 0;
    param.LaunchAppCheck_flag = LaunchApp_SkipSystemUpdate;

    /* Call sceLncUtilLaunchApp directly — it is already declared in
     * <orbis/SystemService.h> and stubbed in -lSceSystemService.
     * The previous approach (sceKernelLoadStartModule + sceKernelDlsym)
     * was fragile: it would fail if the sandbox blocked the .sprx load,
     * and different FW versions expose the symbol differently.
     * Direct call is stable and requires no runtime resolution. */
    log_debug("Calling sceLncUtilLaunchApp(%s)...", title_id);
    int32_t res = sceLncUtilLaunchApp(title_id, NULL, &param);
    log_debug("sceLncUtilLaunchApp returned: 0x%08X", res);

    if (res == 0) {
        log_debug("Launch queued. Exiting launcher to hand focus to emulator.");
        /* Exit the launcher so the OS can bring the emulator to foreground.
         * If we stay alive, the launcher holds the GPU/display and the
         * emulator never gets focus. sceSystemServiceLoadExec("exit") is
         * the correct teardown — bare return/exit() is not enough. */
        sceSystemServiceLoadExec("exit", NULL);
        /* Should not be reached, but satisfy the compiler. */
        return 0;
    }

    /* App is already running — just exit the launcher and the OS will
     * resume it in the foreground. Treat all ALREADY_RUNNING variants
     * as success. */
    if (res == (int32_t)SCE_LNC_UTIL_ERROR_ALREADY_RUNNING ||
        res == (int32_t)SCE_LNC_UTIL_ERROR_ALREADY_RUNNING_SUSPEND_NEEDED ||
        res == (int32_t)SCE_LNC_UTIL_ERROR_ALREADY_RUNNING_KILL_NEEDED) {
        log_debug("Emulator already running (0x%08X) — exiting to resume it.", res);
        sceSystemServiceLoadExec("exit", NULL);
        return 0;
    }

    /* Actual failure — log and return so the launcher can show an error. */
    switch ((uint32_t)res) {
    case SCE_LNC_ERROR_APP_NOT_FOUND:
        log_debug("Launch failed: App not found (%s)", title_id);
        return -2;
    case SCE_LNC_UTIL_ERROR_APPHOME_EBOOTBIN_NOT_FOUND:
        log_debug("Launch failed: Missing eboot.bin");
        return -3;
    case SCE_LNC_UTIL_ERROR_APPHOME_PARAMSFO_NOT_FOUND:
        log_debug("Launch failed: Missing param.sfo");
        return -4;
    case SCE_LNC_UTIL_ERROR_NO_SFOKEY_IN_APP_INFO:
        log_debug("Launch failed: Corrupted SFO");
        return -5;
    case SCE_LNC_UTIL_ERROR_SETUP_FS_SANDBOX:
        log_debug("Launch failed: Sandbox setup error");
        return -6;
    case SCE_LNC_UTIL_ERROR_INVALID_TITLE_ID:
        log_debug("Launch failed: Invalid title ID (%s)", title_id);
        return -7;
    default:
        log_debug("Launch failed: Unhandled error 0x%08X", res);
        return (int)res;
    }
}
