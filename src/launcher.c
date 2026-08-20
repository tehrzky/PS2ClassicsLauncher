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
#define SCE_LNC_UTIL_ERROR_ALREADY_RUNNING              0x80D00504
#define SCE_LNC_UTIL_ERROR_ALREADY_RUNNING_SUSPEND_NEEDED  0x80D0050B
#define SCE_LNC_UTIL_ERROR_ALREADY_RUNNING_KILL_NEEDED     0x80D0050C

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

    // Try every known location for the LNC library.
    // libSceSystemService.sprx is the correct file on real firmware —
    // sceLncUtilLaunchApp and sceLncUtilInitialize live inside it.
    // The emulator's own sce_module folder and our app0 are also tried
    // since they're always accessible without privilege escalation.
    const char *sprx_paths[] = {
        "/app0/sce_module/libSceLncUtil.prx",             // bundled in our PKG
        "/user/app/PCSX20042/sce_module/libSceLncUtil.prx", // emulator's copy
        "/system/common/lib/libSceSystemService.sprx",    // real firmware (needs jb)
        "/system/common/lib/libSceLncUtil.sprx",          // alternate name
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
        log_debug("LAUNCH FAILED: could not load LNC library from any path");
        return;
    }

    // sceLncUtilInitialize MUST be called before sceLncUtilLaunchApp.
    // ItemzFlow daemon calls this before entering its launch loop.
    void *init_func = NULL;
    ret = ps4_dlsym(mod, "sceLncUtilInitialize", &init_func);
    log_debug("ps4_dlsym(sceLncUtilInitialize) = 0x%08X, ptr = %p", ret, init_func);
    if (ret == 0 && init_func) {
        typedef int (*Init_t)(void);
        int init_ret = ((Init_t)init_func)();
        log_debug("sceLncUtilInitialize returned: 0x%08X", init_ret);
    } else {
        log_debug("sceLncUtilInitialize not found — continuing anyway");
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

    if (ret == 0 ||
        (unsigned int)ret == SCE_LNC_UTIL_ERROR_ALREADY_RUNNING ||
        (unsigned int)ret == SCE_LNC_UTIL_ERROR_ALREADY_RUNNING_SUSPEND_NEEDED ||
        (unsigned int)ret == SCE_LNC_UTIL_ERROR_ALREADY_RUNNING_KILL_NEEDED) {
        log_debug("Launch OK (or already running)");
        return;
    }

    log_debug("sceLncUtilLaunchApp failed with 0x%08X", ret);
}
