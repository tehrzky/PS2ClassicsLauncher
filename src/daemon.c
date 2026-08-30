#include "daemon.h"
#include "debug.h"

#include <string.h>
#include <stdint.h>
#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <sys/param.h>
#include <sys/mount.h>

/* This toolchain's <sys/mount.h> only forward-declares struct iovec and
 * doesn't expose nmount()/MNT_UPDATE at all. Define them ourselves —
 * this matches the real FreeBSD/PS4 kernel ABI (same layout Itemzflow's
 * own mountfs() relies on). */
#ifndef MNT_UPDATE
#define MNT_UPDATE 0x0000000000010000ULL
#endif

struct iovec {
    void   *iov_base;
    size_t  iov_len;
};

/* This toolchain's musl-based libc exposes umount() but not nmount() as
 * a linkable symbol, even though the PS4 kernel (a BSD fork) implements
 * the syscall itself. Call it directly via syscall() instead of relying
 * on a wrapper that doesn't exist in this libc. 378 is nmount's syscall
 * number on FreeBSD 11/12, which the PS4's BSD-compat layer mirrors —
 * this is the same number scene tools (mira-project etc.) use. */
#define PS4_SYS_NMOUNT 378
extern long syscall(long number, ...);

static int nmount(struct iovec *iov, unsigned int niov, int flags) {
    return (int)syscall(PS4_SYS_NMOUNT, iov, niov, flags);
}
#include <orbis/libkernel.h>
#include <orbis/SystemService.h>

#define LAUNCH_APP_SKIP_SYSTEM_UPDATE  2u
#define SCE_LNC_UTIL_ERROR_ALREADY_RUNNING  0x80D00504u

typedef struct {
    uint32_t size;
    uint32_t user_id;
    uint32_t app_opt;
    uint64_t crash_report;
    uint32_t check_flag;
} LncAppParam;

extern int32_t sceLncUtilInitialize(void);
extern int32_t sceLncUtilLaunchApp(const char *title_id, const char *argv[], LncAppParam *param);

/* Real system-app param.sfo files to borrow metadata from. These vary by
 * firmware — we try each in order and use whichever exists. Log output
 * tells you which one was picked (or if none were found). */
static const char *SYSTEM_SFO_CANDIDATES[] = {
    "/system/vsh/app/NPXS21007/sce_sys/param.sfo",
    "/system/vsh/app/NPXS20001/sce_sys/param.sfo",
    "/system/vsh/app/NPXS21000/sce_sys/param.sfo",
    "/system/vsh/app/NPXS20113/sce_sys/param.sfo",
    NULL
};

/* Candidate paths for our OWN currently-running eboot.bin, so we can
 * copy ourselves into DAEMON_PATH. The exact sandbox mount pattern can
 * vary between jailbreak payloads, so we try a few known conventions.
 * If none match, daemon_install() logs that clearly — check the debug
 * log and tell me which (if any) exists on your firmware. */
static const char *SELF_EBOOT_CANDIDATES[] = {
    "/mnt/sandbox/pfsmnt/" SELF_TITLE_ID "-app0/eboot.bin",
    "/mnt/sandbox/" SELF_TITLE_ID "-app0/eboot.bin",
    "/mnt/sandbox/pfsmnt/" SELF_TITLE_ID "_000-app0/eboot.bin",
    NULL
};

static int file_exists(const char *path) {
    int fd = open(path, O_RDONLY);
    if (fd < 0) return 0;
    close(fd);
    return 1;
}

static long file_size(const char *path) {
    int fd = open(path, O_RDONLY);
    if (fd < 0) return -1;
    off_t sz = lseek(fd, 0, SEEK_END);
    close(fd);
    return (long)sz;
}

static int copy_file_raw(const char *src, const char *dst) {
    int sfd = open(src, O_RDONLY);
    if (sfd < 0) {
        log_debug("daemon: cannot open src for copy: %s", src);
        return 0;
    }
    int dfd = open(dst, O_WRONLY | O_CREAT | O_TRUNC, 0777);
    if (dfd < 0) {
        log_debug("daemon: cannot open dst for copy: %s", dst);
        close(sfd);
        return 0;
    }
    char buf[65536];
    ssize_t n;
    int ok = 1;
    while ((n = read(sfd, buf, sizeof(buf))) > 0) {
        if (write(dfd, buf, n) != n) { ok = 0; break; }
    }
    if (n < 0) ok = 0;
    close(sfd);
    close(dfd);
    if (!ok) log_debug("daemon: copy failed mid-write: %s -> %s", src, dst);
    return ok;
}

/* Recursive mkdir for a plain "/a/b/c" absolute path. */
static void mkdirs(const char *path) {
    char tmp[512];
    strncpy(tmp, path, sizeof(tmp) - 1);
    tmp[sizeof(tmp) - 1] = '\0';
    size_t len = strlen(tmp);
    if (len > 0 && tmp[len - 1] == '/') tmp[len - 1] = '\0';

    for (char *p = tmp + 1; *p; p++) {
        if (*p == '/') {
            *p = '\0';
            mkdir(tmp, 0777);
            *p = '/';
        }
    }
    mkdir(tmp, 0777);
}

static const char *find_self_eboot(void) {
    for (int i = 0; SELF_EBOOT_CANDIDATES[i]; i++) {
        if (file_exists(SELF_EBOOT_CANDIDATES[i])) {
            return SELF_EBOOT_CANDIDATES[i];
        }
    }
    return NULL;
}

static int remount_system_rw(void) {
    struct iovec iov[16];
    int iovlen = 0;

#define ADDIOV(name_lit, val_lit) do { \
        iov[iovlen].iov_base = (void*)(name_lit); iov[iovlen].iov_len = strlen(name_lit) + 1; iovlen++; \
        iov[iovlen].iov_base = (void*)(val_lit);  iov[iovlen].iov_len = strlen(val_lit) + 1;  iovlen++; \
    } while (0)

    ADDIOV("fstype", "exfatfs");
    ADDIOV("fspath", "/system");
    ADDIOV("from", "/dev/da0x4.crypt");
    ADDIOV("large", "yes");
    ADDIOV("timezone", "static");
    ADDIOV("async", "");
    ADDIOV("ignoreacl", "");
    ADDIOV("dirmask", "511");
    ADDIOV("mask", "511");

#undef ADDIOV

    int ret = nmount(iov, iovlen, MNT_UPDATE);
    if (ret < 0) {
        log_debug("daemon: /system remount rw failed: %d (errno %d)", ret, errno);
        return 0;
    }
    log_debug("daemon: /system remounted read-write");
    return 1;
}

int daemon_is_elevated(int argc, char *argv[]) {
    for (int i = 0; i < argc; i++) {
        if (argv[i] && strcmp(argv[i], "--elevated") == 0) {
            return 1;
        }
    }
    return 0;
}

int daemon_needs_install(void) {
    if (!file_exists(DAEMON_PATH "/eboot.bin")) {
        log_debug("daemon: elevated copy not installed yet");
        return 1;
    }

    const char *self = find_self_eboot();
    if (!self) {
        /* Can't verify staleness without knowing our own path — assume
         * whatever's installed is fine rather than reinstalling forever. */
        log_debug("daemon: could not locate self eboot to check staleness, skipping reinstall check");
        return 0;
    }

    long installed_sz = file_size(DAEMON_PATH "/eboot.bin");
    long self_sz = file_size(self);
    if (installed_sz != self_sz) {
        log_debug("daemon: installed eboot size %ld != current %ld, reinstalling", installed_sz, self_sz);
        return 1;
    }
    return 0;
}

int daemon_install(void) {
    log_debug("daemon: installing elevated copy at %s", DAEMON_PATH);

    if (!remount_system_rw()) {
        return 0;
    }

    mkdirs(DAEMON_PATH);
    mkdirs(DAEMON_PATH "/sce_sys");

    const char *sfo_src = NULL;
    for (int i = 0; SYSTEM_SFO_CANDIDATES[i]; i++) {
        if (file_exists(SYSTEM_SFO_CANDIDATES[i])) {
            sfo_src = SYSTEM_SFO_CANDIDATES[i];
            break;
        }
    }
    if (!sfo_src) {
        log_debug("daemon: no candidate system param.sfo found on this firmware, install aborted");
        return 0;
    }
    log_debug("daemon: using param.sfo from %s", sfo_src);

    if (!copy_file_raw(sfo_src, DAEMON_PATH "/sce_sys/param.sfo")) {
        return 0;
    }

    const char *self_eboot = find_self_eboot();
    if (!self_eboot) {
        log_debug("daemon: could not locate our own running eboot.bin under any known path — "
                   "check the log above for candidates tried and tell Claude which path is correct");
        return 0;
    }
    log_debug("daemon: copying self from %s", self_eboot);

    if (!copy_file_raw(self_eboot, DAEMON_PATH "/eboot.bin")) {
        return 0;
    }

    log_debug("daemon: install complete");
    return 1;
}

int daemon_launch_elevated(void) {
    int32_t ir = sceLncUtilInitialize();
    log_debug("daemon: sceLncUtilInitialize: 0x%08X", ir);

    LncAppParam param;
    memset(&param, 0, sizeof(param));
    param.size        = sizeof(param);
    param.user_id      = (uint32_t)-1;
    param.app_opt       = 0;
    param.crash_report  = 0;
    param.check_flag   = LAUNCH_APP_SKIP_SYSTEM_UPDATE;

    const char *argv[] = { "--elevated", NULL };

    log_debug("daemon: launching elevated copy (%s)...", DAEMON_TITLE_ID);
    int32_t res = sceLncUtilLaunchApp(DAEMON_TITLE_ID, argv, &param);
    log_debug("daemon: sceLncUtilLaunchApp(%s) returned: 0x%08X", DAEMON_TITLE_ID, res);

    if (res == 0 || (uint32_t)res == SCE_LNC_UTIL_ERROR_ALREADY_RUNNING) {
        sceSystemServiceLoadExec("exit", NULL);
        return 0; /* not reached */
    }

    return (int)res;
}
