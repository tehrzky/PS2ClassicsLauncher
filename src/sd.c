#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include <fcntl.h>
#include <unistd.h>
#include <orbis/libkernel.h>

#include "sd.h"

#ifndef UNUSED
#define UNUSED(x) ((void)(x))
#endif

int (*sceFsInitMountSaveDataOpt)(MountSaveDataOpt *opt);
int (*sceFsMountSaveData)(MountSaveDataOpt *opt, const char *volumePath, const char *mountPath, uint8_t decryptedSealedKey[DEC_SEALEDKEY_LEN]);
int (*sceFsInitUmountSaveDataOpt)(UmountSaveDataOpt *opt);
int (*sceFsUmountSaveData)(UmountSaveDataOpt *opt, const char *mountPath, int handle, bool ignoreErrors);

int loadPrivLibs(void) {
    int sys = sceKernelLoadStartModule("/system/priv/libSceFsInternalForVsh.sprx", 0, NULL, 0, NULL, NULL);
    if (sys >= 0) {
        sceKernelDlsym(sys, "sceFsInitMountSaveDataOpt", (void **)&sceFsInitMountSaveDataOpt);
        sceKernelDlsym(sys, "sceFsMountSaveData",        (void **)&sceFsMountSaveData);
        sceKernelDlsym(sys, "sceFsInitUmountSaveDataOpt",(void **)&sceFsInitUmountSaveDataOpt);
        sceKernelDlsym(sys, "sceFsUmountSaveData",       (void **)&sceFsUmountSaveData);
    } else {
        return -1;
    }
    return 0;
}

static int decryptSealedKeyAtPath(const char *keyPath, uint8_t decryptedSealedKey[DEC_SEALEDKEY_LEN]) {
    uint8_t sealedKey[ENC_SEALEDKEY_LEN];
    int fd = open(keyPath, O_RDONLY);
    if (fd == -1) return -1;
    if (read(fd, sealedKey, ENC_SEALEDKEY_LEN) != ENC_SEALEDKEY_LEN) {
        close(fd);
        return -2;
    }
    close(fd);

    uint8_t data[ENC_SEALEDKEY_LEN + DEC_SEALEDKEY_LEN];
    memset(data, 0, sizeof(data));

    fd = open("/dev/sbl_srv", O_RDWR);
    if (fd == -1) return -3;
    memcpy(data, sealedKey, ENC_SEALEDKEY_LEN);
    if (ioctl(fd, 0xc0845302, data) == -1) {
        close(fd);
        return -4;
    }
    memcpy(decryptedSealedKey, &data[ENC_SEALEDKEY_LEN], DEC_SEALEDKEY_LEN);
    close(fd);
    return 0;
}

int mountSave(const char *volumePath, const char *volumeKeyPath, const char *mountPath) {
    int ret;
    uint8_t decryptedSealedKey[DEC_SEALEDKEY_LEN];
    MountSaveDataOpt opt;

    memset(&opt, 0, sizeof(MountSaveDataOpt));

    ret = decryptSealedKeyAtPath(volumeKeyPath, decryptedSealedKey);
    if (ret < 0) {
        return ret;
    }

    sceFsInitMountSaveDataOpt(&opt);
    opt.budgetid = "system";

    ret = sceFsMountSaveData(&opt, volumePath, mountPath, decryptedSealedKey);
    if (ret < 0) {
        return ret;
    }

    return 0;
}

int umountSave(const char *mountPath, int handle, bool ignoreErrors) {
    UmountSaveDataOpt opt;
    memset(&opt, 0, sizeof(UmountSaveDataOpt));
    sceFsInitUmountSaveDataOpt(&opt);
    return sceFsUmountSaveData(&opt, mountPath, handle, ignoreErrors);
}
