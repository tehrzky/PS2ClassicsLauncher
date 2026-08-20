#include "launcher.h"
#include "debug.h"
#include "config.h"
#include "syscalls.h"
#include <orbis/libkernel.h>
#include <orbis/SystemService.h>
#include <orbis/UserService.h>
#include <orbis/Lnc.h>
#include <string.h>

// Use the toolchain's own LncAppParam — it has crash_report as uint64_t
// which matches the real PS4 struct layout. Do NOT redefine it.
// LaunchApp_SkipSystemUpdate = 2 per the enum in sys_service.h

int launch_emulator(const char *override_tid) {
    const char *tid = (override_tid && override_tid[0]) ? override_tid : EMULATOR_TID;

    log_debug("=== LAUNCHING EMULATOR ===");
    log_debug("EMULATOR_TID: %s", tid);

    uint32_t userId = 0;
    int ret = sceUserServiceGetForegroundUser(&userId);
    if (ret < 0) {
        sceUserServiceGetInitialUser(&userId);
    }
    log_debug("User ID: %u", userId);

    LncAppParam param;
    memset(&param, 0, sizeof(LncAppParam));
    param.size = sizeof(LncAppParam);
    param.user_id = userId;
    param.app_opt = 0;
    param.crash_report = 0;
    param.LaunchAppCheck_flag = LaunchApp_SkipSystemUpdate;

    log_debug("sizeof(LncAppParam) = %zu", sizeof(LncAppParam));
    log_debug("Calling sceLncUtilLaunchApp...");
    ret = sceLncUtilLaunchApp(tid, NULL, &param);
    log_debug("sceLncUtilLaunchApp returned: 0x%08X", ret);
    
    return ret;
}
