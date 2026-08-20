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

/* OpenOrbis does not ship orbis/Lnc.h, so we define LncAppParam here.
   Layout: size(4) + user_id(4) + app_opt(4) + pad(4) + crash_report(8) + flag(4) + pad(4) = 32 */
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
    log_debug("sceUserServiceGetForegroundUser ret=%d userId=%d", ret, userId);

    if (ret < 0 || userId <= 0) {
        ret = sceUserServiceGetInitialUser(&userId);
        log_debug("sceUserServiceGetInitialUser ret=%d userId=%d", ret, userId);
    }

    /* Sanity fallback: most PS4s have user 1 as the primary account */
    if (userId <= 0) {
        userId = 1;
        log_debug("Falling back to default User ID: %d", userId);
    }

    LncAppParam param;
    memset(&param, 0, sizeof(LncAppParam));
    param.size = sizeof(LncAppParam);
    param.user_id = (uint32_t)userId;
    param.app_opt = 0;
    param.crash_report = 0;
    param.LaunchAppCheck_flag = LaunchApp_SkipSystemUpdate;

    log_debug("sizeof(LncAppParam) = %zu", sizeof(LncAppParam));
    log_debug("Calling sceSystemServiceLaunchApp...");
    sceSystemServiceLaunchApp(tid, NULL, &param);
    /* If we get here, the launch did NOT happen (PS4 would suspend us first).
       Wait a bit so the log flushes before we return. */
    sceKernelSleep(2);
    log_debug("sceSystemServiceLaunchApp returned — launch likely failed");

    return -1;
}
