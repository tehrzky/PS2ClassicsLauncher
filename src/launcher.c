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

    /* 4) Launch directly through the stub
