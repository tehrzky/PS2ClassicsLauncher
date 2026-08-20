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

/* OpenOrbis does not ship orbis/Lnc.h.
   CRITICAL: the 'size' field MUST be 64-bit. The PS4 kernel reads 8 bytes.
   Layout: size(8) + user_id(4) + app_opt(4) + crash_report(8) + flag(4) + pad(4) = 32 */
typedef struct LncAppParam {
    uint64_t size;
    uint32_t user_id;
    uint32_t app_opt;
    uint64_t crash_report;
    uint32_t LaunchAppCheck_flag;
    uint32_t pad;
} LncAppParam;

#define LaunchApp_SkipSystemUpdate 2

int launch_emulator(const char *override_tid) {
    const char *tid = (override_tid && override_tid[0]) ? override_tid : EMULATOR_TID;

    log_debug("=== LAUNCHING EMULATOR ===");
    log_debug("EMULATOR_TID: %s", tid);

    /* sceUserServiceGetForegroundUser was returning garbage (349522419).
       User 1 is the default primary account on almost every PS4. */
    uint32_t userId = 1;

    LncAppParam param;
    memset(&param, 0, sizeof(LncAppParam));
    param.size = sizeof(LncAppParam);   /* 32 stored as 64-bit */
    param.user_id = userId;
    param.app_opt = 0;
    param.crash_report = 0;
    param.LaunchAppCheck_flag = LaunchApp_SkipSystemUpdate;

    log_debug("sizeof(LncAppParam) = %zu", sizeof(LncAppParam));
    log_debug("Calling sceSystemServiceLaunchApp...");
    sceSystemServiceLaunchApp(tid, NULL, &param);
    log_debug("sceSystemServiceLaunchApp returned");

    return 0;
}
