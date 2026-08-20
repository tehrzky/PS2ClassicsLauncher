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

/* OpenOrbis doesn't ship LncAppParam. Layout must match PS4 kernel exactly.
   size=32 bytes with implicit padding after app_opt to align crash_report. */
typedef struct LncAppParam {
    uint32_t size;
    uint32_t user_id;
    uint32_t app_opt;
    uint32_t pad1;           /* implicit alignment padding */
    uint64_t crash_report;
    uint32_t LaunchAppCheck_flag;
    uint32_t pad2;
} LncAppParam;

#define LaunchApp_SkipSystemUpdate 2

typedef int (*sceLncUtilLaunchApp_t)(const char* title_id, const char* argv[], LncAppParam* param);

int launch_emulator(const char *override_tid) {
    const char *tid = (override_tid && override_tid[0]) ? override_tid : EMULATOR_TID;

    log_debug("=== LAUNCHING EMULATOR ===");
    log_debug("EMULATOR_TID: %s", tid);

    /* UserService calls are returning garbage on your setup; hardcode to 1.
       User 1 is the primary account on virtually every PS4. */
    int userId = 1;
    log_debug("User ID: %d", userId);

    LncAppParam param;
    memset(&param, 0, sizeof(LncAppParam));
    param.size = sizeof(LncAppParam);
    param.user_id = (uint32_t)userId;
    param.app_opt = 0;
    param.crash_report = 0;
    param.LaunchAppCheck_flag = LaunchApp_SkipSystemUpdate;

    log_debug("sizeof(LncAppParam) = %zu", sizeof(LncAppParam));

    /* Dynamically resolve sceLncUtilLaunchApp from the real PS4 system library.
       The OpenOrbis stub doesn't export it, but the function exists in retail firmware. */
    sceLncUtilLaunchApp_t lncLaunch = NULL;
    int sysmod = sceKernelLoadStartModule("/system/common/lib/libSceSystemService.sprx", 0, NULL, 0, 0, 0);
    if (sysmod >= 0) {
        sceKernelDlsym(sysmod, "sceLncUtilLaunchApp", (void**)&lncLaunch);
    }
    if (!lncLaunch) {
        sysmod = sceKernelLoadStartModule("/system/common/lib/libSceLncUtil.sprx", 0, NULL, 0, 0, 0);
        if (sysmod >= 0) {
            sceKernelDlsym(sysmod, "sceLncUtilLaunchApp", (void**)&lncLaunch);
        }
    }

    int ret;
    if (lncLaunch) {
        log_debug("Calling sceLncUtilLaunchApp (resolved at %p)...", lncLaunch);
        ret = lncLaunch(tid, NULL, &param);
        log_debug("sceLncUtilLaunchApp returned: 0x%08X", ret);
    } else {
        log_debug("sceLncUtilLaunchApp not found, falling back to sceSystemServiceLaunchApp...");
        sceSystemServiceLaunchApp(tid, NULL, &param);
        log_debug("sceSystemServiceLaunchApp returned");
        ret = 0;
    }

    return ret;
}
