#include "launcher.h"
#include "debug.h"
#include "video.h"
#include "font.h"
#include "config.h"
#include "syscalls.h"
#include <orbis/libkernel.h>
#include <orbis/SystemService.h>
#include <orbis/UserService.h>
#include <string.h>

// ============ LAUNCH ============
typedef struct {
    uint32_t sz;
    uint32_t user_id;
    uint32_t app_opt;
    uint32_t crash_report;
    uint32_t check_flag;
    uint32_t unk[2];
} LncAppParam;

#define SkipSystemUpdateCheck 0x20000
#define SCE_LNC_UTIL_ERROR_ALREADY_RUNNING 0x80D00504

void launch_emulator(const char *override_tid) {
    int userId = 0;
    int ret;
    int mod = -1;
    const char *tid = (override_tid && override_tid[0]) ? override_tid : EMULATOR_TID;

    log_debug("=== LAUNCHING EMULATOR ===");
    log_debug("EMULATOR_TID: %s", tid);

    ret = sceUserServiceGetForegroundUser(&userId);
    if (ret < 0) {
        ret = sceUserServiceGetInitialUser(&userId);
        if (ret < 0) userId = 0;
    }
    log_debug("User ID: %d", userId);

    const char *sprx_paths[] = {
        "/system/common/lib/libSceLncUtil.sprx",
        "/system/priv/lib/libSceLncUtil.sprx",
        "/system/lib/libSceLncUtil.sprx",
        NULL
    };

    for (int i = 0; sprx_paths[i] != NULL; i++) {
        mod = ps4_load_prx(sprx_paths[i], &mod);
        log_debug("ps4_load_prx(%s) = %d", sprx_paths[i], mod);
        if (mod >= 0) break;
    }

    if (mod < 0) {
        log_debug("LAUNCH FAILED: could not load libSceLncUtil.sprx");
        return;
    }

    void *launch_func = NULL;
    ret = ps4_dlsym(mod, "sceLncUtilLaunchApp", &launch_func);
    log_debug("ps4_dlsym(sceLncUtilLaunchApp) = 0x%08X, ptr = %p", ret, launch_func);

    if (ret != 0 || launch_func == NULL) {
        log_debug("LAUNCH FAILED: sceLncUtilLaunchApp symbol not found");
        return;
    }

    LncAppParam param;
    memset(&param, 0, sizeof(param));
    param.sz = sizeof(LncAppParam);
    param.user_id = userId;
    param.app_opt = 0;
    param.crash_report = 0;
    param.check_flag = SkipSystemUpdateCheck;

    typedef int (*LaunchApp_t)(const char *titleId, const char *args, void *param);
    LaunchApp_t sceLncUtilLaunchApp = (LaunchApp_t)launch_func;

    log_debug("Calling sceLncUtilLaunchApp with TID: %s", tid);
    ret = sceLncUtilLaunchApp(tid, NULL, &param);
    log_debug("sceLncUtilLaunchApp returned: 0x%08X", ret);

    if (ret == 0 || (unsigned int)ret == SCE_LNC_UTIL_ERROR_ALREADY_RUNNING) {
        log_debug("Launch OK");
        return;
    }

    log_debug("Falling back to sceSystemServiceLaunchApp");
    sceSystemServiceLaunchApp(tid, NULL, NULL);
    return;
}