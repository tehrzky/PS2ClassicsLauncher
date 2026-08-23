#include "psu.h"
#include "mcio.h"
#include "mcio_compat.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

/* PSU file format (uLaunchELF / PCSX2 / AetherSX2 compatible):
 *   - Sequence of 512-byte headers (same layout as PS2 mcio dirent)
 *   - Each header immediately followed by the file's raw data
 *   - Last entry has mode==0 to terminate
 *
 * Header layout (512 bytes, little-endian):
 *   0x000: mode       (2 bytes)
 *   0x002: reserved   (2 bytes)
 *   0x004: length     (4 bytes)
 *   0x008: created    (8 bytes)
 *   0x010: cluster    (4 bytes)
 *   0x014: dir_entry  (4 bytes)
 *   0x018: modified   (8 bytes)
 *   0x020: attr       (4 bytes)
 *   0x024: reserved2  (4 bytes)
 *   0x028: name       (32 bytes)
 *   0x048: padding    (472 bytes)
 */

static int write_all(FILE *fp, const void *buf, size_t len)
{
    return (fwrite(buf, 1, len, fp) == len) ? 0 : -1;
}

static int read_all(FILE *fp, void *buf, size_t len)
{
    return (fread(buf, 1, len, fp) == len) ? 0 : -1;
}

int psu_export_save(const char *vmc_dir, const char *psu_path)
{
    FILE *fp = fopen(psu_path, "wb");
    if (!fp) return -1;

    char dir_path[128];
    snprintf(dir_path, sizeof(dir_path), "/%s", vmc_dir);
    int dfd = mcio_mcDopen(dir_path);
    if (dfd < 0) {
        fclose(fp);
        return -2;
    }

    int ret = 0;
    struct io_dirent dirent;

    while (ret == 0) {
        int n = mcio_mcDread(dfd, &dirent);
        if (n <= 0) break;
        if (dirent.name[0] == '\0') break;

        if (strcmp(dirent.name, ".") == 0 || strcmp(dirent.name, "..") == 0)
            continue;

        /* Read file data from VMC */
        char file_path[256];
        snprintf(file_path, sizeof(file_path), "/%s/%s", vmc_dir, dirent.name);
        int fd = mcio_mcOpen(file_path, sceMcFileAttrReadable | sceMcFileAttrFile);
        if (fd < 0) {
            ret = -3;
            break;
        }

        uint8_t *fdata = NULL;
        int fsize = 0;
        if (dirent.stat.size > 0) {
            fdata = (uint8_t *)malloc(dirent.stat.size);
            if (!fdata) {
                mcio_mcClose(fd);
                ret = -4;
                break;
            }
            fsize = mcio_mcRead(fd, fdata, dirent.stat.size);
        }
        mcio_mcClose(fd);

        /* Build 512-byte PSU header from dirent.stat */
        uint8_t hdr[512];
        memset(hdr, 0, sizeof(hdr));

        hdr[0x00] = dirent.stat.mode & 0xFF;
        hdr[0x01] = (dirent.stat.mode >> 8) & 0xFF;
        /* 0x02-0x03 reserved */
        hdr[0x04] = dirent.stat.size & 0xFF;
        hdr[0x05] = (dirent.stat.size >> 8) & 0xFF;
        hdr[0x06] = (dirent.stat.size >> 16) & 0xFF;
        hdr[0x07] = (dirent
