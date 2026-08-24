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
#include <dlfcn.h>          // for dlsym, dlopen (if needed)

// ============ LAUNCH STRUCTURES & DEFINES ============
typedef struct {
    uint32_t sz;
    uint32_t user_id;
    uint32_t app_opt;
    uint64_t crash_report;
    uint32_t check_flag;
} LncAppParam;

#define SkipSystemUpdateCheck 0x20000

// Error codes (from Sony SDK)
#define SCE_LNC_ERROR_APP_NOT_FOUND                     0x80D00501
#define SCE_LNC_UTIL_ERROR_ALREADY_RUNNING              0x80D00504
#define SCE_LNC_UTIL_ERROR_ALREADY_RUNNING_SUSPEND_NEEDED  0x80D0050B
#define SCE_LNC_UTIL_ERROR_ALREADY_RUNNING_KILL_NEEDED     0x80D0050C
#define SCE_LNC_UTIL_ERROR_APPHOME_EBOOTBIN_NOT_FOUND   0x80D0050D
#define SCE_LNC_UTIL_ERROR_APPHOME_PARAMSFO_NOT_FOUND   0x80D0050E
#define SCE_LNC_UTIL_ERROR_NO_SFOKEY_IN_APP_INFO        0x80D00510
#define SCE_LNC_UTIL_ERROR_SETUP_FS_SANDBOX             0x80D00509
#define SCE_LNC_UTIL_ERROR_INVALID_TITLE_ID             0x80D0050A

#define IS_ERROR(ret) ((unsigned int)ret & 0x80000000)

// ============ FUNCTION POINTER TYPE ============
typedef uint32_t (*sceLncUtilLaunchApp_t)(const char *titleId, const char *argv[], LncAppParam *param);

// ============ MAIN LAUNCHER FUNCTION ============
/**
 * Launch a PS4 application by Title ID
 * This version loads libSceSystemService.sprx explicitly, initializes it,
 * resolves sceLncUtilLaunchApp dynamically, and uses proven flags.
 * 
 * @param tid        The Title ID to launch (e.g., "CUSA12345")
 * @param override_tid Fallback Title ID if primary is empty
 * @return 0 on success, negative on error
 */
int launch_app(const char *tid, const char *override_tid) {
    const char *title_id = (tid && tid[0]) ? tid : override_tid;
    if (!title_id || !title_id[0]) {
        log_debug("LAUNCH FAILED: No valid Title ID provided");
        return -1;
    }

    log_debug("=== LAUNCHING APP ===");
    log_debug("Title ID: %s", title_id);

    // 1. Get the foreground user (fallback to initial user)
    int userId = 0;
    int ret = sceUserServiceGetForegroundUser(&userId);
    if (ret < 0) {
        sceUserServiceGetInitialUser(&userId);
    }
    log_debug("User ID: %d", userId);

    // 2. Load the system service module (if not already loaded)
    int sys_mod = sceKernelLoadStartModule(
        "/system/common/lib/libSceSystemService.sprx",
        0, NULL, 0, 0, NULL
    );
    if (sys_mod < 0) {
        log_debug("LAUNCH FAILED: Could not load libSceSystemService.sprx (0x%08X)", sys_mod);
        return sys_mod;
    }
    log_debug("Module handle: %d", sys_mod);

    // 3. Call sceLncUtilInitialize to ensure internal state is ready
    void *init_fn = dlsym((void*)(uintptr_t)sys_mod, "sceLncUtilInitialize");
    if (init_fn) {
        int r = ((int(*)(void))init_fn)();
        log_debug("sceLncUtilInitialize returned: 0x%08X", r);
    } else {
        log_debug("Warning: sceLncUtilInitialize not found, continuing anyway");
    }

    // 4. Resolve sceLncUtilLaunchApp dynamically
    void *launch_fn = dlsym((void*)(uintptr_t)sys_mod, "sceLncUtilLaunchApp");
    if (!launch_fn) {
        log_debug("LAUNCH FAILED: sceLncUtilLaunchApp not found in module");
        return -1;
    }
    sceLncUtilLaunchApp_t launch = (sceLncUtilLaunchApp_t)launch_fn;

    // 5. Prepare parameters (using the more robust flags from known launchers)
    LncAppParam param;
    memset(&param, 0, sizeof(param));
    param.sz = sizeof(LncAppParam);
    param.user_id = (uint32_t)userId;
    param.app_opt = 0x00000001;                // bring to foreground, avoid suspend
    param.crash_report = 0;
    param.check_flag = SkipSystemUpdateCheck | 0x00040000; // additional mask

    // 6. Launch the app
    log_debug("Calling sceLncUtilLaunchApp with TID: %s", title_id);
    uint32_t sys_res = launch(title_id, NULL, &param);
    log_debug("sceLncUtilLaunchApp returned: 0x%08X", sys_res);

    // 7. Handle success / already‑running states
    if (sys_res == 0 ||
        sys_res == SCE_LNC_UTIL_ERROR_ALREADY_RUNNING ||
        sys_res == SCE_LNC_UTIL_ERROR_ALREADY_RUNNING_SUSPEND_NEEDED ||
        sys_res == SCE_LNC_UTIL_ERROR_ALREADY_RUNNING_KILL_NEEDED) {
        log_debug("Launch successful (or app already running)");
        return 0;
    }

    // 8. Detailed error mapping for common failures
    if (IS_ERROR(sys_res)) {
        switch (sys_res) {
            case SCE_LNC_ERROR_APP_NOT_FOUND:
                log_debug("Launch error: App not found (0x%08X)", sys_res);
                return -2;
            case SCE_LNC_UTIL_ERROR_APPHOME_EBOOTBIN_NOT_FOUND:
                log_debug("Launch error: Missing eboot.bin (0x%08X)", sys_res);
                return -3;
            case SCE_LNC_UTIL_ERROR_APPHOME_PARAMSFO_NOT_FOUND:
                log_debug("Launch error: Missing param.sfo (0x%08X)", sys_res);
                return -4;
            case SCE_LNC_UTIL_ERROR_NO_SFOKEY_IN_APP_INFO:
                log_debug("Launch error: Corrupted SFO configuration (0x%08X)", sys_res);
                return -5;
            case SCE_LNC_UTIL_ERROR_SETUP_FS_SANDBOX:
                log_debug("Launch error: Sandbox setup failure (0x%08X)", sys_res);
                return -6;
            case SCE_LNC_UTIL_ERROR_INVALID_TITLE_ID:
                log_debug("Launch error: Invalid Title ID format (0x%08X)", sys_res);
                return -7;
            default:
                log_debug("Launch error: Unhandled system return (0x%08X)", sys_res);
                return (int)sys_res;
        }
    }

    return (int)sys_res;
}

// ============ CONVENIENCE WRAPPERS ============
/**
 * Launch emulator (using EMULATOR_TID from config.h)
 * @param override_tid Optional custom Title ID, falls back to EMULATOR_TID
 * @return 0 on success, negative on error
 */
int launch_emulator(const char *override_tid) {
    log_debug("Launching emulator...");
    return launch_app(override_tid, EMULATOR_TID);
}

/**
 * Launch by URI (for system apps like settings, store, etc.)
 * This is a placeholder – you may need to use a different API for URIs.
 * @param uri The URI to launch
 * @return 0 on success, negative on error
 */
int launch_by_uri(const char *uri) {
    if (!uri || !uri[0]) {
        log_debug("LAUNCH_URI FAILED: No URI provided");
        return -1;
    }
    log_debug("Launching URI: %s (not yet implemented)", uri);
    // TODO: implement using sceShellUIUtilLaunchUri or equivalent
    return 0;
}
