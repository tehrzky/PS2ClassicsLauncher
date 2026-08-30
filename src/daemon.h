#ifndef DAEMON_H
#define DAEMON_H

/* -----------------------------------------------------------------------
 * Elevation helper.
 *
 * A normal installed PKG (CATEGORY 'gd') is a sandboxed game/homebrew
 * process. sceLncUtilLaunchApp() crashes hard when called from that
 * context on real hardware — it isn't just a missing-module problem,
 * it's a process-privilege problem.
 *
 * The fix (same pattern real scene tools like Itemzflow use): install a
 * copy of this exact binary into a spare system-app slot under
 * /system/vsh/app/, wearing a real system app's param.sfo. The OS treats
 * that copy as an elevated vsh-class process, and sceLncUtilLaunchApp
 * works fine from inside it.
 *
 * Flow:
 *   1. Sandboxed instance boots normally (argv has no "--elevated").
 *   2. It installs/updates the elevated copy if needed.
 *   3. It calls sceLncUtilLaunchApp("ITEM00002", {"--elevated"}, ...) and
 *      exits.
 *   4. The elevated copy boots, sees "--elevated" in argv, skips the
 *      install/trampoline step, and runs the full UI as normal — except
 *      now launch_emulator() actually works.
 *
 * If elevation fails for any reason, we fall back to running the old
 * sandboxed path so the launcher still works exactly as before (with the
 * old emulator-launch crash), rather than becoming totally unusable.
 * ----------------------------------------------------------------------- */

/* Slot in the PS4's system app registry we install our elevated copy
 * into. "ITEM00002" is an unused/free vsh app id — same slot Itemzflow
 * uses; change it if it collides with something on your firmware. */
#define DAEMON_TITLE_ID   "ITEM00002"
#define DAEMON_PATH       "/system/vsh/app/" DAEMON_TITLE_ID

/* Must match TITLE_ID in the Makefile. Used to locate our own running
 * eboot.bin under the sandbox mount so we can copy ourselves. */
#define SELF_TITLE_ID     "PSTL00001"

/* Returns 1 if this process is the elevated vsh-app copy (launched with
 * the --elevated argv flag), 0 if it's the normal sandboxed install. */
int daemon_is_elevated(int argc, char *argv[]);

/* True if the elevated copy is missing or looks stale (different size
 * than our currently running eboot — i.e. you rebuilt since last
 * install) and needs (re)installing. */
int daemon_needs_install(void);

/* Remounts /system read-write, copies a real system app's param.sfo and
 * our own eboot.bin into DAEMON_PATH. Returns 1 on success, 0 on
 * failure (check the debug log for which step failed). */
int daemon_install(void);

/* Launches the elevated copy and exits this process. Only returns on
 * failure (the launch itself failed) — the caller should fall back to
 * running the normal sandboxed UI. Returns the raw sceLncUtilLaunchApp
 * result (0 means it worked and this line is never actually reached). */
int daemon_launch_elevated(void);

#endif
