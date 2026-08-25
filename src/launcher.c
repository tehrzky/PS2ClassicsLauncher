#include "launcher.h"
#include "debug.h"
#include "config.h"
#include <orbis/libkernel.h>
#include <orbis/SystemService.h>
#include <orbis/UserService.h>
#include <string.h>

/* ---------- LAUNCH STRUCTURES ---------- */
typedef struct {
    uint32_t sz;
    uint32_t user_id;
    uint32_t app_opt;
    uint64_t crash_report;
    uint32_t check_flag;
} LncAppParam;

#define SkipSystemUpdateCheck 0x20000

#define SCE_LNC_ERROR_APP_NOT_FOUND                     0x80D00501
#define SCE_LNC_UTIL_ERROR_ALREADY_RUNNING              0x80D00504
#define SCE_LNC_UTIL_ERROR_ALREADY_RUNNING_SUSPEND_NEEDED  0x80D0050B
#define SCE_LNC_UTIL_ERROR_ALREADY_RUNNING_KILL_NEEDED     0x80D0050C
#define SCE_LNC_UTIL_ERROR_APPHOME_EBOOTBIN_NOT_FOUND   0x80D0050D
#define SCE_LNC_UTIL_ERROR_APPHOME_PARAMSFO_NOT_FOUND   0x80D0050E
#define SCE_LNC_UTIL_ERROR_NO_SFOKEY_IN_APP_INFO        0x80D00510
#define SCE_LNC_UTIL_ERROR_SETUP_FS_SANDBOX             0x80D00509
#define SCE_LNC_UTIL_ERROR_INVALID_TITLE_ID             0x80D0050A

#define IS_ERROR(ret) ((unsigned int)(ret) & 0x80000000)

/* ---------- MAIN LAUNCHER FUNCTION ---------- */
int launch_app(const char *tid, const char *override_tid)
{
    uint32_t sys_res = (uint32_t)-1;
    int userId = 0;
    const char *title_id = (tid && tid[0]) ? tid : override_tid;

    if (!title_id || !title_id[0]) {
        log_debug("LAUNCH FAILED: No valid Title ID provided");
        return -1;
    }

    log_debug("=== LAUNCHING APP ===");
    log_debug("Title ID: %s", title_id);

    /* 1) Get foreground user */
    int ret = sceUserServiceGetForegroundUser(&userId);
    if (ret < 0) {
        log_debug("sceUserServiceGetForegroundUser failed: 0x%08X, using 0", ret);
        userId = 0;
    }
    log_debug("User ID: %d", userId);

    /* 2) Load the real system service PRX into memory
       (stubs are already linked via -lSceSystemService) */
    int sys_mod = sceKernelLoadStartModule(
        "/system/common/lib/libSceSystemService.sprx",
        0, NULL, 0, 0, NULL);

    log_debug("sceKernelLoadStartModule (SystemService) returned: %d", sys_mod);
    if (sys_mod < 0) {
        log_debug("LAUNCH FAILED: Could not load libSceSystemService.sprx (0x%08X)", sys_mod);
        return sys_mod;
    }

    /* 3) Prepare launch param (Itemzflow-style minimal flags) */
    LncAppParam param;
    memset(&param, 0, sizeof(LncAppParam));
    param.sz         = sizeof(LncAppParam);
    param.user_id    = (uint32_t)userId;
    param.app_opt    = 0;
    param.crash_report = 0;
    param.check_flag = SkipSystemUpdateCheck;

    /* 4) Launch directly through the stub — no dlsym needed */
    log_debug("Calling sceLncUtilLaunchApp with TID: %s", title_id);
    sys_res = sceLncUtilLaunchApp(title_id, NULL, &param);
    log_debug("sceLncUtilLaunchApp returned: 0x%08X", sys_res);

    /* 5) Handle results */
    if (sys_res == 0) {
        log_debug("Launch successful!");
        return 0;
    }

    if (sys_res == SCE_LNC_UTIL_ERROR_ALREADY_RUNNING ||
        sys_res == SCE_LNC_UTIL_ERROR_ALREADY_RUNNING_SUSPEND_NEEDED ||
        sys_res == SCE_LNC_UTIL_ERROR_ALREADY_RUNNING_KILL_NEEDED) {
        log_debug("App already running, treating as success");
        return 0;
    }

    if (IS_ERROR(sys_res)) {
        switch (sys_res) {
        case SCE_LNC_ERROR_APP_NOT_FOUND:
            log_debug("Launch error: App not found (0x%08X)", sys_res);
            return -2;
        case SCE_LNC_UTIL_ERROR_APPHOME_EBOOTBIN_NOT_FOUND:
            log_debug("Launch error: Missing eboot.bin (0x%08X)", sys_res);
            return -3;
        case SCE_LNC_UTIL_ERROR_APPHOME_PARAMSFO_NOT_FOUND:
            log_debug("Launch error: Missing param.sfo (0x%08X)", sys_res);
            return -4;
        case SCE_LNC_UTIL_ERROR_NO_SFOKEY_IN_APP_INFO:
            log_debug("Launch error: Corrupted SFO (0x%08X)", sys_res);
            return -5;
        case SCE_LNC_UTIL_ERROR_SETUP_FS_SANDBOX:
            log_debug("Launch error: Sandbox setup failure (0x%08X)", sys_res);
            return -6;
        case SCE_LNC_UTIL_ERROR_INVALID_TITLE_ID:
            log_debug("Launch error: Invalid Title ID (0x%08X)", sys_res);
            return -7;
        default:
            log_debug("Launch error: Unhandled code (0x%08X)", sys_res);
            return (int)sys_res;
        }
    }

    return (int)sys_res;
}

int launch_emulator(const char *override_tid)
{
    log_debug("Launching emulator...");
    return launch_app(override_tid, EMULATOR_TID);
}
