#include "launcher.h"
#include "debug.h"
#include "config.h"
#include "syscalls.h"

#include <orbis/libkernel.h>
#include <orbis/SystemService.h>
#include <orbis/UserService.h>
#include <string.h>
#include <stdint.h>
#include <stdio.h>

/* OpenOrbis doesn't ship orbis/Lnc.h, so we define the struct locally.
   Layout must match the real PS4 struct exactly (crash_report is u64). */
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
    log_debug("Calling sceLncUtilLaunchApp...");
    ret = sceLncUtilLaunchApp(tid, NULL, &param);
    log_debug("sceLncUtilLaunchApp returned: 0x%08X", ret);

    return ret;
}
