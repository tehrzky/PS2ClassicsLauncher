#include "launcher.h"
#include "debug.h"
#include <orbis/libkernel.h>
#include <orbis/SystemService.h>
#include <orbis/UserService.h>
#include <string.h>

// ============ LAUNCH STRUCTURES ============
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

#define IS_ERROR(ret) ((unsigned int)ret & 0x80000000)

// ============ FORWARD DECLARATIONS ============
// Use the correct signatures from orbis/UserService.h and orbis/libkernel.h
extern uint32_t sceLncUtilLaunchApp(const char *titleId, const char *argv[], LncAppParam *param);

// ============ MAIN LAUNCHER ============
/**
 * Launch a PS4 app by Title ID
 * Exactly like ItemzFlow does it
 * 
 * @param tid Title ID (e.g., "CUSA12345")
 * @return 0 on success, error code on failure
 */
int launch_app(const char *tid) {
    uint32_t sys_res = -1;
    int userId = 0;  // Match orbis/UserService.h signature (int*, not uint32_t*)
    uint32_t libcmi = 0;

    if (!tid || !tid[0]) {
        log_debug("ERROR: No Title ID provided");
        return -1;
    }

    log_debug("=== LAUNCHING: %s ===", tid);

    // Step 1: Get the foreground user
    int ret = sceUserServiceGetForegroundUser(&userId);
    if (ret < 0) {
        log_debug("WARNING: sceUserServiceGetForegroundUser failed (0x%08X)", ret);
        userId = 0;  // Fallback to user 0
    }
    log_debug("User ID: %d", userId);

    // Step 2: Load libSceSystemService.sprx
    // Match orbis/libkernel.h signature: uint32_t sceKernelLoadStartModule(const char *, size_t, const void *, uint32_t, void *, void *)
    libcmi = sceKernelLoadStartModule(
        "/system/common/lib/libSceSystemService.sprx",
        0,              // args
        NULL,           // argp
        0,              // flags
        NULL,           // opts (use NULL, not 0)
        NULL            // res (use NULL, not 0)
    );

    log_debug("sceKernelLoadStartModule returned: 0x%08X (module=%u)", libcmi, libcmi);

    if (libcmi == 0) {
        log_debug("FATAL: Could not load libSceSystemService.sprx");
        return -1;
    }

    // Step 3: Prepare launch parameters
    LncAppParam param;
    memset(&param, 0, sizeof(LncAppParam));
    param.sz = sizeof(LncAppParam);
    param.user_id = (uint32_t)userId;
    param.app_opt = 0;
    param.crash_report = 0;
    param.check_flag = SkipSystemUpdateCheck;

    // Step 4: LAUNCH THE APP
    log_debug("Calling sceLncUtilLaunchApp(%s)", tid);
    sys_res = sceLncUtilLaunchApp(tid, NULL, &param);
    log_debug("sceLncUtilLaunchApp returned: 0x%08X", sys_res);

    // Check success
    if (!IS_ERROR(sys_res)) {
        log_debug("SUCCESS: App launched");
        return 0;
    }

    // Check "already running" (this is also success)
    if (sys_res == SCE_LNC_UTIL_ERROR_ALREADY_RUNNING ||
        sys_res == SCE_LNC_UTIL_ERROR_ALREADY_RUNNING_SUSPEND_NEEDED ||
        sys_res == SCE_LNC_UTIL_ERROR_ALREADY_RUNNING_KILL_NEEDED) {
        log_debug("SUCCESS: App already running (resuming)");
        return 0;
    }

    // Handle errors
    switch (sys_res) {
    case SCE_LNC_ERROR_APP_NOT_FOUND:
        log_debug("ERROR: App not installed");
        break;
    case SCE_LNC_UTIL_ERROR_APPHOME_EBOOTBIN_NOT_FOUND:
        log_debug("ERROR: Missing eboot.bin");
        break;
    case SCE_LNC_UTIL_ERROR_APPHOME_PARAMSFO_NOT_FOUND:
        log_debug("ERROR: Missing param.sfo");
        break;
    default:
        log_debug("ERROR: Launch failed with code 0x%08X", sys_res);
    }

    return (int)sys_res;
}
