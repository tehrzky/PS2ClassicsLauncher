#include "launcher.h"
#include "debug.h"
#include "config.h"
#include <string.h>
#include <stdint.h>
#include <orbis/libkernel.h>
#include <orbis/SystemService.h>
#include <orbis/UserService.h>
#include <orbis/Sysmodule.h>

/* -----------------------------------------------------------------------
 * LncAppParam and sceLncUtilLaunchApp are in libSceLncUtil, not
 * libSceSystemService. This toolchain's headers don't declare them,
 * so we define the struct and forward-declare the function ourselves.
 * The symbol is stubbed in libSceLncUtil.so — just needs -lSceLncUtil.
 * ----------------------------------------------------------------------- */

#define LAUNCH_APP_SKIP_SYSTEM_UPDATE  2u

typedef struct {
    uint32_t size;
    uint32_t user_id;
    uint32_t app_opt;
    uint64_t crash_report;
    uint32_t check_flag;
} LncAppParam;

int32_t sceLncUtilInitialize(void);
int32_t sceLncUtilLaunchApp(const char *title_id, const char *argv[], LncAppParam *param);

/* LNC error codes */
#define SCE_LNC_ERROR_APP_NOT_FOUND                    0x80D00501u
#define SCE_LNC_UTIL_ERROR_ALREADY_RUNNING             0x80D00504u
#define SCE_LNC_UTIL_ERROR_ALREADY_RUNNING_SUSPEND     0x80D0050Bu
#define SCE_LNC_UTIL_ERROR_ALREADY_RUNNING_KILL        0x80D0050Cu
#define SCE_LNC_UTIL_ERROR_EBOOT_NOT_FOUND             0x80D0050Du
#define SCE_LNC_UTIL_ERROR_PARAMSFO_NOT_FOUND          0x80D0050Eu
#define SCE_LNC_UTIL_ERROR_NO_SFOKEY                   0x80D00510u
#define SCE_LNC_UTIL_ERROR_SANDBOX                     0x80D00509u
#define SCE_LNC_UTIL_ERROR_INVALID_TITLE_ID            0x80D0050Au

/* -----------------------------------------------------------------------
 * launch_emulator
 *
 * Correct flow for "my launcher opens emulator":
 *   1. Call sceLncUtilLaunchApp  — tells the OS to queue the target app
 *   2. Call sceSystemServiceLoadExec("exit")  — tears down THIS process
 *   The OS then brings the queued app to foreground.
 *
 * If we skip step 2 the launcher stays alive, holds the GPU/display,
 * and the emulator never gets focus — which is what was happening before.
 * ----------------------------------------------------------------------- */
int launch_emulator(const char *override_tid)
{
    const char *title_id = (override_tid && override_tid[0]) ? override_tid : EMULATOR_TID;

    log_debug("=== LAUNCH EMULATOR ===");
    log_debug("Title ID: %s", title_id);

    /* Defensive reload — sandbox/jailbreak calls earlier in the process
     * lifetime can leave this unloaded even if main() loaded it at boot.
     * Safe to call repeatedly (checks if already loaded internally). */
    int32_t sysmod_ret = sceSysmoduleLoadModuleInternal(ORBIS_SYSMODULE_INTERNAL_SYSTEM_SERVICE);
    log_debug("sceSysmoduleLoadModuleInternal(SYSTEM_SERVICE): 0x%08X", sysmod_ret);

    /* Initialize LncUtil (safe to call multiple times) */
    int32_t ir = sceLncUtilInitialize();
    log_debug("sceLncUtilInitialize: 0x%08X", ir);

    int userId = 1;
    int ret = sceUserServiceGetForegroundUser(&userId);
    if (ret < 0) {
        log_debug("sceUserServiceGetForegroundUser failed: 0x%08X, using 1", ret);
        userId = 1;
    }
    log_debug("User ID: %d", userId);

    LncAppParam param;
    memset(&param, 0, sizeof(LncAppParam));
    param.size       = sizeof(LncAppParam);
    param.user_id    = (uint32_t)userId;
    param.app_opt    = 0;
    param.crash_report = 0;
    param.check_flag = LAUNCH_APP_SKIP_SYSTEM_UPDATE;

    log_debug("Calling sceLncUtilLaunchApp(%s)...", title_id);
    int32_t res = sceLncUtilLaunchApp(title_id, NULL, &param);
    log_debug("sceLncUtilLaunchApp returned: 0x%08X", res);

    if (res == 0 ||
        (uint32_t)res == SCE_LNC_UTIL_ERROR_ALREADY_RUNNING ||
        (uint32_t)res == SCE_LNC_UTIL_ERROR_ALREADY_RUNNING_SUSPEND ||
        (uint32_t)res == SCE_LNC_UTIL_ERROR_ALREADY_RUNNING_KILL) {
        /* Emulator launched (or already running) — exit the launcher so
         * the OS can hand display focus to it. */
        log_debug("Launch OK — calling sceSystemServiceLoadExec(exit)");
        sceSystemServiceLoadExec("exit", NULL);
        return 0; /* not reached */
    }

    /* Real failure — log and return so the UI can react */
    switch ((uint32_t)res) {
    case SCE_LNC_ERROR_APP_NOT_FOUND:
        log_debug("Launch failed: App not found (%s)", title_id); return -2;
    case SCE_LNC_UTIL_ERROR_EBOOT_NOT_FOUND:
        log_debug("Launch failed: Missing eboot.bin");             return -3;
    case SCE_LNC_UTIL_ERROR_PARAMSFO_NOT_FOUND:
        log_debug("Launch failed: Missing param.sfo");             return -4;
    case SCE_LNC_UTIL_ERROR_NO_SFOKEY:
        log_debug("Launch failed: Corrupted SFO");                 return -5;
    case SCE_LNC_UTIL_ERROR_SANDBOX:
        log_debug("Launch failed: Sandbox error");                 return -6;
    case SCE_LNC_UTIL_ERROR_INVALID_TITLE_ID:
        log_debug("Launch failed: Invalid title ID (%s)", title_id); return -7;
    default:
        log_debug("Launch failed: 0x%08X", res);                  return (int)res;
    }
}
