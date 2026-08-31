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
#include <orbis/libkernel.h>
#include <orbis/SystemService.h>

/* -----------------------------------------------------------------------
 * Raw syscall wrappers.
 * This toolchain's musl-derived libc is missing several BSD syscall
 * wrappers (nmount, mkdir). We implement them the same way ps4-libjbc
 * implements open/close — naked inline asm that issues the syscall
 * directly, bypassing libc entirely.  Syscall numbers match the PS4's
 * BSD-fork kernel (same as standard FreeBSD 9-12 on x86-64).
 * SysV AMD64 ABI: args in rdi, rsi, rdx, [rcx→r10 for syscall], r8, r9.
 * ----------------------------------------------------------------------- */

#ifndef MNT_UPDATE
#define MNT_UPDATE 0x0000000000010000ULL
#endif

struct ps4_iovec {
    void   *iov_base;
    size_t  iov_len;
};

/* syscall 378 — nmount(iov, niov, flags) */
__attribute__((naked)) static int
_ps4_nmount(struct ps4_iovec *iov, unsigned int niov, int flags) {
    __asm__ volatile(
        "mov $378, %rax\n\t"
        "mov %rcx, %r10\n\t"
        "syscall\n\t"
        "ret"
    );
}

/* syscall 136 — mkdir(path, mode) */
__attribute__((naked)) static int
_ps4_mkdir(const char *path, int mode) {
    __asm__ volatile(
        "mov $136, %rax\n\t"
        "mov %rcx, %r10\n\t"
        "syscall\n\t"
        "ret"
    );
}

/* -----------------------------------------------------------------------
 * LncUtil types/externs (matching Itemzflow's Header.h layout exactly)
 * ----------------------------------------------------------------------- */
#define LAUNCH_APP_SKIP_SYSTEM_UPDATE  2u
#define SCE_LNC_UTIL_ERROR_ALREADY_RUNNING  0x80D00504u

typedef struct {
    uint32_t size;
    uint32_t user_id;
    uint32_t app_opt;
    uint64_t crash_report;
    uint32_t check_flag;
} LncAppParam;

extern int32_t sceLncUtilLaunchApp(const char *title_id,
                                    const char *argv[],
                                    LncAppParam *param);

/* -----------------------------------------------------------------------
 * Candidate paths
 * ----------------------------------------------------------------------- */
static const char *SYSTEM_SFO_CANDIDATES[] = {
    "/system/vsh/app/NPXS21007/sce_sys/param.sfo",
    "/system/vsh/app/NPXS20001/sce_sys/param.sfo",
    "/system/vsh/app/NPXS21000/sce_sys/param.sfo",
    "/system/vsh/app/NPXS20113/sce_sys/param.sfo",
    "/system/vsh/app/NPXS21006/sce_sys/param.sfo",
    "/system/vsh/app/NPXS20108/sce_sys/param.sfo",
    NULL
};

static const char *SELF_EBOOT_CANDIDATES[] = {
    "/mnt/sandbox/pfsmnt/" SELF_TITLE_ID "-app0/eboot.bin",
    "/mnt/sandbox/" SELF_TITLE_ID "-app0/eboot.bin",
    "/mnt/sandbox/pfsmnt/" SELF_TITLE_ID "_000-app0/eboot.bin",
    "/mnt/sandbox/pfsmnt/" SELF_TITLE_ID "0-app0/eboot.bin",
    NULL
};

/* -----------------------------------------------------------------------
 * Low-level helpers
 * ----------------------------------------------------------------------- */
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
    log_debug("daemon:   open src: %s", src);
    int sfd = open(src, O_RDONLY);
    if (sfd < 0) {
        log_debug("daemon:   open src FAILED fd=%d", sfd);
        return 0;
    }
    log_debug("daemon:   open dst: %s", dst);
    int dfd = open(dst, O_WRONLY | O_CREAT | O_TRUNC, 0777);
    if (dfd < 0) {
        log_debug("daemon:   open dst FAILED fd=%d", dfd);
        close(sfd);
        return 0;
    }
    log_debug("daemon:   copying bytes...");
    char buf[32768];
    ssize_t n;
    long total = 0;
    int ok = 1;
    while ((n = read(sfd, buf, sizeof(buf))) > 0) {
        if (write(dfd, buf, (size_t)n) != n) {
            log_debug("daemon:   write failed after %ld bytes", total);
            ok = 0;
            break;
        }
        total += n;
    }
    if (n < 0) {
        log_debug("daemon:   read failed after %ld bytes", total);
        ok = 0;
    }
    close(sfd);
    close(dfd);
    if (ok) log_debug("daemon:   copied %ld bytes OK", total);
    return ok;
}

/* mkdir -p using our raw syscall wrapper. Ignores EEXIST. */
static void mkdirs_raw(const char *path) {
    char tmp[512];
    strncpy(tmp, path, sizeof(tmp) - 1);
    tmp[sizeof(tmp) - 1] = '\0';
    size_t len = strlen(tmp);
    if (len > 0 && tmp[len - 1] == '/') tmp[len - 1] = '\0';

    for (char *p = tmp + 1; *p; p++) {
        if (*p == '/') {
            *p = '\0';
            int r = _ps4_mkdir(tmp, 0777);
            log_debug("daemon:   mkdir(%s) = %d", tmp, r);
            *p = '/';
        }
    }
    int r = _ps4_mkdir(tmp, 0777);
    log_debug("daemon:   mkdir(%s) = %d", tmp, r);
}

static int remount_system_rw(void) {
    struct ps4_iovec iov[20];
    int iovlen = 0;

#define ADDIOV(k, v) do { \
    iov[iovlen].iov_base = (void*)(k); \
    iov[iovlen].iov_len  = strlen(k) + 1; iovlen++; \
    iov[iovlen].iov_base = (void*)(v); \
    iov[iovlen].iov_len  = strlen(v) + 1; iovlen++; \
} while(0)

    ADDIOV("fstype",   "exfatfs");
    ADDIOV("fspath",   "/system");
    ADDIOV("from",     "/dev/da0x4.crypt");
    ADDIOV("large",    "yes");
    ADDIOV("timezone", "static");
    ADDIOV("async",    "");
    ADDIOV("ignoreacl","");
    ADDIOV("dirmask",  "511");
    ADDIOV("mask",     "511");

#undef ADDIOV

    int ret = _ps4_nmount(iov, (unsigned int)iovlen, (int)MNT_UPDATE);
    if (ret != 0) {
        log_debug("daemon: /system remount rw failed: %d", ret);
        return 0;
    }
    log_debug("daemon: /system remounted read-write");
    return 1;
}

static const char *find_self_eboot(void) {
    for (int i = 0; SELF_EBOOT_CANDIDATES[i]; i++) {
        log_debug("daemon:   checking eboot candidate: %s", SELF_EBOOT_CANDIDATES[i]);
        if (file_exists(SELF_EBOOT_CANDIDATES[i])) {
            log_debug("daemon:   FOUND: %s", SELF_EBOOT_CANDIDATES[i]);
            return SELF_EBOOT_CANDIDATES[i];
        }
    }
    return NULL;
}

/* -----------------------------------------------------------------------
 * Public API
 * ----------------------------------------------------------------------- */
int daemon_is_elevated(int argc, char *argv[]) {
    for (int i = 0; i < argc; i++) {
        if (argv[i] && strcmp(argv[i], "--elevated") == 0)
            return 1;
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
        log_debug("daemon: can't check staleness (self eboot not found), skip reinstall");
        return 0;
    }
    long installed_sz = file_size(DAEMON_PATH "/eboot.bin");
    long self_sz      = file_size(self);
    if (installed_sz != self_sz) {
        log_debug("daemon: stale install (%ld != %ld), reinstalling", installed_sz, self_sz);
        return 1;
    }
    log_debug("daemon: elevated copy up to date (%ld bytes)", installed_sz);
    return 0;
}

int daemon_install(void) {
    log_debug("daemon: === BEGIN INSTALL ===");

    log_debug("daemon: step 1/6 — remount /system rw");
    if (!remount_system_rw())
        return 0;

    log_debug("daemon: step 2/6 — mkdirs %s", DAEMON_PATH);
    mkdirs_raw(DAEMON_PATH);

    log_debug("daemon: step 3/6 — mkdirs %s/sce_sys", DAEMON_PATH);
    mkdirs_raw(DAEMON_PATH "/sce_sys");

    log_debug("daemon: step 4/6 — find system param.sfo");
    const char *sfo_src = NULL;
    for (int i = 0; SYSTEM_SFO_CANDIDATES[i]; i++) {
        log_debug("daemon:   checking: %s", SYSTEM_SFO_CANDIDATES[i]);
        if (file_exists(SYSTEM_SFO_CANDIDATES[i])) {
            sfo_src = SYSTEM_SFO_CANDIDATES[i];
            log_debug("daemon:   FOUND: %s", sfo_src);
            break;
        }
    }
    if (!sfo_src) {
        log_debug("daemon: ABORT — no param.sfo found on this firmware");
        return 0;
    }

    log_debug("daemon: step 5/6 — copy param.sfo");
    if (!copy_file_raw(sfo_src, DAEMON_PATH "/sce_sys/param.sfo"))
        return 0;

    log_debug("daemon: step 6/6 — find and copy self eboot");
    const char *self_eboot = find_self_eboot();
    if (!self_eboot) {
        log_debug("daemon: ABORT — self eboot not found under any candidate path");
        return 0;
    }
    if (!copy_file_raw(self_eboot, DAEMON_PATH "/eboot.bin"))
        return 0;

    log_debug("daemon: === INSTALL COMPLETE ===");
    return 1;
}

int daemon_launch_elevated(void) {
    log_debug("daemon: loading libSceSystemService.sprx");
    int32_t mod = sceKernelLoadStartModule(
        "/system/common/lib/libSceSystemService.sprx", 0, NULL, 0, 0, 0);
    log_debug("daemon: sceKernelLoadStartModule = %d", mod);
    if (mod <= 0) {
        log_debug("daemon: module load failed, aborting launch");
        return -1;
    }

    LncAppParam param;
    memset(&param, 0, sizeof(param));
    param.size        = sizeof(param);
    param.user_id     = (uint32_t)-1;
    param.app_opt      = 0;
    param.crash_report = 0;
    param.check_flag  = LAUNCH_APP_SKIP_SYSTEM_UPDATE;

    const char *argv[] = { "--elevated", NULL };

    log_debug("daemon: sceLncUtilLaunchApp(%s)", DAEMON_TITLE_ID);
    int32_t res = sceLncUtilLaunchApp(DAEMON_TITLE_ID, argv, &param);
    log_debug("daemon: sceLncUtilLaunchApp returned 0x%08X", res);

    if (res == 0 || (uint32_t)res == SCE_LNC_UTIL_ERROR_ALREADY_RUNNING) {
        sceSystemServiceLoadExec("exit", NULL);
        return 0;
    }
    return (int)res;
}
