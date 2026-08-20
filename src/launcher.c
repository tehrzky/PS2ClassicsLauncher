#include "launcher.h"
#include "debug.h"
#include "config.h"
#include "syscalls.h"

#include <orbis/UserService.h>
#include <orbis/SystemService.h>
#include <string.h>
#include <stdint.h>
#include <stdio.h>

/* OpenOrbis does not ship orbis/Lnc.h, so we define LncAppParam here.
   Layout matches the real PS4 struct (crash_report is uint64_t). */
typedef struct LncAppParam {
    uint32_t size;
    uint32_t user_id;
    uint32_t app_opt;
    uint64_t crash_report;
    uint32_t LaunchAppCheck_flag;
} LncAppParam;

#define LaunchApp_SkipSystemUpdate 2

int launch_emulator(const char *override_tid) {
    const char *tid = (override_tid && override_tid[0]) ? override_tid : EMULATOR_TID;

    log_debug("=== LAUNCHING EMULATOR ===");
    log_debug("EMULATOR_TID: %s", tid);

    int userId = 0;
    int ret = sceUserServiceGetForegroundUser(&userId);
    if (ret < 0) {
        sceUserServiceGetInitialUser(&userId);
    }
    log_debug("User ID: %d", userId);

    LncAppParam param;
    memset(&param, 0, sizeof(LncAppParam));
    param.size = sizeof(LncAppParam);
    param.user_id = (uint32_t)userId;
    param.app_opt = 0;
    param.crash_report = 0;
    param.LaunchAppCheck_flag = LaunchApp_SkipSystemUpdate;

    log_debug("sizeof(LncAppParam) = %zu", sizeof(LncAppParam));
    log_debug("Calling sceSystemServiceLaunchApp...");
    ret = sceSystemServiceLaunchApp(tid, NULL, &param);
    log_debug("sceSystemServiceLaunchApp returned: 0x%08X", ret);

    return ret;
}
