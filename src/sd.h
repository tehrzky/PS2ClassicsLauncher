#ifndef SD_H
#define SD_H

#include <stdint.h>
#include <stdbool.h>

#define ENC_SEALEDKEY_LEN 0x60
#define DEC_SEALEDKEY_LEN 0x20

typedef struct {
    bool readOnly;
    char *budgetid;
} MountSaveDataOpt;

typedef struct {
    int blockSize;
    uint8_t idk[2];
} CreatePfsSaveDataOpt;

typedef struct {
    bool dummy;
} UmountSaveDataOpt;

int loadPrivLibs(void);
int mountSave(const char *volumePath, const char *volumeKeyPath, const char *mountPath);
int umountSave(const char *mountPath, int handle, bool ignoreErrors);

#endif
