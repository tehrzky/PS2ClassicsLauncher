#ifndef SD_H
#define SD_H

#include <stdint.h>
#include <stdbool.h>

#define ENC_SEALEDKEY_LEN 0x30
#define DEC_SEALEDKEY_LEN 0x20

typedef struct {
    uint32_t size;
    uint32_t userId;
    const char* budgetid;
    uint32_t mountMode;
    uint32_t blocks;
    uint8_t reserved[0x28];
} MountSaveDataOpt;

typedef struct {
    uint8_t data[0x40];
} CreatePfsSaveDataOpt;

typedef struct {
    uint8_t data[0x10];
} UmountSaveDataOpt;

int loadPrivLibs(void);
int mountSave(const char *volumePath, const char *volumeKeyPath, const char *mountPath);
int umountSave(const char *mountPath, int handle, bool ignoreErrors);

#endif
