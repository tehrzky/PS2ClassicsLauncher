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

// ============ LAUNCH STRUCTURES & DEFINES ============
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
#define SCE_LNC_UTIL_ERROR_NO_SFOKEY_IN_APP_INFO        0x80D00510
#define SCE_LNC_UTIL_ERROR_SETUP_FS_SANDBOX             0x80D00509
#define SCE_LNC_UTIL_ERROR_INVALID_TITLE_ID             0x80D0050A

#define IS_ERROR(ret) ((unsigned int)ret & 0x80000000)

// ============ DYNAMIC SYMBOL RESOLUTION ============
typedef uint32_t (*sceLncUtilLaunchApp_t)(const char *titleId, const char *argv[], LncAppParam *param);

static sceLncUtilLaunchApp_t sceLncUtilLaunchApp = NULL;

extern void *dlsym(void *handle, const char *symbol);

// ============ MAIN LAUNCHER FUNCTION ============
/**
 * Launch a PS4 application by Title ID
 * Loads libSceLncUtil.sprx to resolve launch symbols safely via ps4_dlsym
 * 
 * @param tid The Title ID to launch (e.g., "CUSA12345")
 * @param override_tid Fallback Title ID if primary is empty
 * @return 0 on success, negative on error
 */
int launch_app(const char *tid, const char *override_tid) {
    uint32_t sys_res = -1;
    int userId = 0;
    int lnc_mod = -1;
    const char *title_id = (tid && tid[0]) ? tid : override_tid;

    if (!title_id || !title_id[0]) {
        log_debug("LAUNCH FAILED: No valid Title ID provided");
        return -1;
    }

    log_debug("=== LAUNCHING APP ===");
    log_debug("Title ID: %s", title_id);

    // Step 1: Get the foreground user
    int ret = sceUserServiceGetForegroundUser(&userId);
    if (ret < 0) {
        log_debug("Failed to get foreground user: 0x%08X, using user 0", ret);
        userId = 0;  // Fallback to user 0
    }
    log_debug("User ID: %d", userId);

    // Step 2: Load the CORRECT Launch Utility module containing launch functionality
    log_debug("Attempting to load libSceLncUtil.sprx...");
    lnc_mod = sceKernelLoadStartModule(
        "/system/common/lib/libSceLncUtil.sprx", 
        0, NULL, 0, 0, NULL
    );
    
    // Fallback path check if common directory lacks permissions or module path variations apply
    if (lnc_mod < 0) {
        log_debug("Common path failed, attempting privileged module path...");
        lnc_mod = sceKernelLoadStartModule(
            "/system/priv/lib/libSceLncUtil.sprx", 
            0, NULL, 0, 0, NULL
        );
    }
    
    log_debug("sceKernelLoadStartModule (LncUtil) returned handle: %d", lnc_mod);
    
    if (lnc_mod < 0) {
        log_debug("LAUNCH FAILED: Could not load libSceLncUtil.sprx (0x%08X)", lnc_mod);
        log_debug("Ensure system privileges and sandbox bypass routines executed correctly");
        return lnc_mod;
    }

    // Step 3: Resolve launch symbols dynamically from the correct module handle
    void *init_fn = NULL;
    ps4_dlsym(lnc_mod, "sceLncUtilInitialize", &init_fn);
    log_debug("sceLncUtilInitialize address: %p", init_fn);
    if (init_fn) {
        int r = ((int(*)(void))init_fn)();
        log_debug("sceLncUtilInitialize executed and returned: 0x%08X", r);
    }

    void *launch_fn = NULL;
    ps4_dlsym(lnc_mod, "sceLncUtilLaunchApp", &launch_fn);
    log_debug("sceLncUtilLaunchApp address: %p", launch_fn);

    if (!launch_fn) {
        log_debug("LAUNCH FAILED: sceLncUtilLaunchApp function symbol missing in module %d", lnc_mod);
        return -1;
    }

    sceLncUtilLaunchApp = (sceLncUtilLaunchApp_t)launch_fn;
    log_debug("Successfully resolved sceLncUtilLaunchApp function assignment");

    // Step 4: Prepare launch parameters
    LncAppParam param;
    memset(&param, 0, sizeof(LncAppParam));
    param.sz = sizeof(LncAppParam);
    param.user_id = (uint32_t)userId;
    param.app_opt = 0;
    param.crash_report = 0;
    param.check_flag = SkipSystemUpdateCheck;

    // Step 5: Launch the app
    log_debug("Calling sceLncUtilLaunchApp with TID: %s", title_id);
    sys_res = sceLncUtilLaunchApp(title_id, NULL, &param);
    log_debug("sceLncUtilLaunchApp returned: 0x%08X", sys_res);

    // Step 6: Handle launch result
    if (sys_res == 0) {
        log_debug("Launch successful!");
        return 0;
    }

    // Check for "already running" errors (these are success states)
    if (sys_res == SCE_LNC_UTIL_ERROR_ALREADY_RUNNING ||
        sys_res == SCE_LNC_UTIL_ERROR_ALREADY_RUNNING_SUSPEND_NEEDED ||
        sys_res == SCE_LNC_UTIL_ERROR_ALREADY_RUNNING_KILL_NEEDED) {
        log_debug("App already running (resuming it)");
        return 0;
    }

    // Handle specific errors
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
            log_debug("Launch error: Corrupted SFO (0x%08X)", sys_res);
            return -5;
        case SCE_LNC_UTIL_ERROR_SETUP_FS_SANDBOX:
            log_debug("Launch error: Sandbox setup failed (0x%08X)", sys_res);
            return -6;
        case SCE_LNC_UTIL_ERROR_INVALID_TITLE_ID:
            log_debug("Launch error: Invalid Title ID format (0x%08X)", sys_res);
            return -7;
        default:
            log_debug("Launch error: Unknown error (0x%08X)", sys_res);
            return (int)sys_res;
        }
    }

    return sys_res;
}

/**
 * Launch emulator (convenience wrapper)
 * 
 * @param override_tid Optional custom Title ID, falls back to EMULATOR_TID
 * @return 0 on success, negative on error
 */
int launch_emulator(const char *override_tid) {
    log_debug("Launching emulator...");
    return launch_app(override_tid, EMULATOR_TID);
}

/**
 * Launch by URI (e.g., "pssettings:play?mode=settings")
 * This is for system apps like settings, store, etc.
 * 
 * @param uri The URI to launch
 * @return 0 on success, negative on error
 */
int launch_by_uri(const char *uri) {
    if (!uri || !uri[0]) {
        log_debug("LAUNCH_URI FAILED: No URI provided");
        return -1;
    }

    log_debug("Launching URI: %s", uri);

    int userId = 0;
    int libcmi = sceKernelLoadStartModule(
        "/system/common/lib/libSceShellUIUtil.sprx",
        0, NULL, 0, 0, NULL
    );

    if (libcmi < 0) {
        log_debug("Failed to load libSceShellUIUtil.sprx: 0x%08X", libcmi);
        return libcmi;
    }

    // Get user ID
    int ret = sceUserServiceGetForegroundUser(&userId);
    if (ret < 0) {
        userId = 0;
    }

    // This would require sceShellUIUtilLaunchByUri which you'd need to dlsym
    // For now, just log the attempt
    log_debug("URI launch would go here (requires additional implementation)");
    return 0;
}
