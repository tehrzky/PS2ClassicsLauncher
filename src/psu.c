#include "psu.h"
#include "mcio.h"
#include "mcio_compat.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

/* PSU file format (used by uLaunchELF, PCSX2, AetherSX2):
 *   - Sequence of 512-byte file headers (same layout as PS2 mcio dirent)
 *   - Each header is immediately followed by the file's raw data
 *   - Last entry has mode==0 to terminate the list
 *
 * Header layout (512 bytes, little-endian):
 *   0x000: mode      (2 bytes)
 *   0x002: unknown   (2 bytes)
 *   0x004: length    (4 bytes)
 *   0x008: created   (8 bytes)
 *   0x010: cluster   (4 bytes)
 *   0x014: dir_entry (4 bytes)
 *   0x018: modified  (8 bytes)
 *   0x020: attr      (4 bytes)
 *   0x024: unknown2  (4 bytes)
 *   0x028: name      (32 bytes)
 *   0x048: padding   (472 bytes)
 */

static int write_all(FILE *fp, const void *buf, size_t len)
{
    size_t written = fwrite(buf, 1, len, fp);
    return (written == len) ? 0 : -1;
}

static int read_all(FILE *fp, void *buf, size_t len)
{
    size_t n = fread(buf, 1, len, fp);
    return (n == len) ? 0 : -1;
}

int psu_export_save(const char *vmc_dir, const char *psu_path)
{
    FILE *fp = fopen(psu_path, "wb");
    if (!fp) return -1;

    /* Open the save directory on VMC */
    char dir_path[128];
    snprintf(dir_path, sizeof(dir_path), "/%s", vmc_dir);
    int dfd = mcio_mcOpen(dir_path, sceMcFileAttrReadable | sceMcFileAttrSubdir);
    if (dfd < 0) {
        fclose(fp);
        return -2;
    }

    int ret = 0;
    sceMcTblGetDir dirent;

    while (ret == 0) {
        int n = mcio_mcRead(dfd, &dirent, sizeof(dirent));
        if (n < (int)sizeof(dirent)) break;
        if (dirent.name[0] == '\0') break;

        /* Skip . and .. */
        if (strcmp(dirent.name, ".") == 0 || strcmp(dirent.name, "..") == 0)
            continue;

        /* Read the file data from VMC */
        char file_path[256];
        snprintf(file_path, sizeof(file_path), "/%s/%s", vmc_dir, dirent.name);
        int fd = mcio_mcOpen(file_path, sceMcFileAttrReadable | sceMcFileAttrFile);
        if (fd < 0) {
            ret = -3;
            break;
        }

        uint8_t *fdata = NULL;
        int fsize = 0;
        if (dirent.length > 0) {
            fdata = (uint8_t *)malloc(dirent.length);
            if (!fdata) {
                mcio_mcClose(fd);
                ret = -4;
                break;
            }
            fsize = mcio_mcRead(fd, fdata, dirent.length);
        }
        mcio_mcClose(fd);

        /* Build 512-byte PSU header from dirent */
        uint8_t hdr[512];
        memset(hdr, 0, sizeof(hdr));

        hdr[0x00] = dirent.mode & 0xFF;
        hdr[0x01] = (dirent.mode >> 8) & 0xFF;
        /* unknown at 0x02-0x03 */
        hdr[0x04] = dirent.length & 0xFF;
        hdr[0x05] = (dirent.length >> 8) & 0xFF;
        hdr[0x06] = (dirent.length >> 16) & 0xFF;
        hdr[0x07] = (dirent.length >> 24) & 0xFF;
        /* created 0x08-0x0F */
        hdr[0x08] = dirent.created.year & 0xFF;
        hdr[0x09] = (dirent.created.year >> 8) & 0xFF;
        hdr[0x0A] = dirent.created.month;
        hdr[0x0B] = dirent.created.day;
        hdr[0x0C] = dirent.created.hour;
        hdr[0x0D] = dirent.created.minute;
        hdr[0x0E] = dirent.created.second;
        /* cluster 0x10-0x13 */
        hdr[0x10] = dirent.cluster & 0xFF;
        hdr[0x11] = (dirent.cluster >> 8) & 0xFF;
        hdr[0x12] = (dirent.cluster >> 16) & 0xFF;
        hdr[0x13] = (dirent.cluster >> 24) & 0xFF;
        /* dir_entry 0x14-0x17 */
        hdr[0x14] = dirent.dir_entry & 0xFF;
        hdr[0x15] = (dirent.dir_entry >> 8) & 0xFF;
        hdr[0x16] = (dirent.dir_entry >> 16) & 0xFF;
        hdr[0x17] = (dirent.dir_entry >> 24) & 0xFF;
        /* modified 0x18-0x1F */
        hdr[0x18] = dirent.modified.year & 0xFF;
        hdr[0x19] = (dirent.modified.year >> 8) & 0xFF;
        hdr[0x1A] = dirent.modified.month;
        hdr[0x1B] = dirent.modified.day;
        hdr[0x1C] = dirent.modified.hour;
        hdr[0x1D] = dirent.modified.minute;
        hdr[0x1E] = dirent.modified.second;
        /* attr 0x20-0x23 */
        hdr[0x20] = dirent.attr & 0xFF;
        hdr[0x21] = (dirent.attr >> 8) & 0xFF;
        hdr[0x22] = (dirent.attr >> 16) & 0xFF;
        hdr[0x23] = (dirent.attr >> 24) & 0xFF;
        /* name 0x28-0x47 */
        strncpy((char *)(hdr + 0x28), dirent.name, 32);

        /* Write header + data */
        if (write_all(fp, hdr, 512) != 0 ||
            (fsize > 0 && write_all(fp, fdata, fsize) != 0)) {
            ret = -5;
        }

        free(fdata);
    }

    mcio_mcClose(dfd);

    /* Write terminator entry (mode = 0) */
    if (ret == 0) {
        uint8_t term[512];
        memset(term, 0, sizeof(term));
        if (write_all(fp, term, 512) != 0) ret = -6;
    }

    fclose(fp);
    if (ret != 0) remove(psu_path);
    return ret;
}

int psu_import_save(const char *psu_path, char *out_dir, size_t out_dir_len)
{
    FILE *fp = fopen(psu_path, "rb");
    if (!fp) return -1;

    /* First pass: read first header to get the save directory name */
    uint8_t first_hdr[512];
    if (read_all(fp, first_hdr, 512) != 0) {
        fclose(fp);
        return -2;
    }

    char dir_name[32];
    memset(dir_name, 0, sizeof(dir_name));
    strncpy(dir_name, (char *)(first_hdr + 0x28), 31);

    /* The first entry in a PSU is usually icon.sys; the directory name
     * is the parent directory.  We derive it from the PSU filename
     * or use a sanitized version of the first file's "related" name.
     * For simplicity we create a directory based on the PSU basename. */
    const char *base = strrchr(psu_path, '/');
    if (!base) base = psu_path; else base++;
    char new_dir[32];
    strncpy(new_dir, base, 31);
    char *dot = strrchr(new_dir, '.');
    if (dot) *dot = '\0';
    if (strlen(new_dir) == 0) strcpy(new_dir, "IMPORT");

    /* Create directory on VMC */
    char vmc_path[128];
    snprintf(vmc_path, sizeof(vmc_path), "/%s", new_dir);
    int r = mcio_mcMkDir(vmc_path, sceMcFileAttrSubdir);
    if (r < 0 && r != -4) { /* -4 might mean already exists */
        fclose(fp);
        return -3;
    }

    /* Second pass: write all files */
    fseek(fp, 0, SEEK_SET);

    int ret = 0;
    while (ret == 0) {
        uint8_t hdr[512];
        if (read_all(fp, hdr, 512) != 0) break;

        uint16_t mode = hdr[0x00] | (hdr[0x01] << 8);
        if (mode == 0) break; /* terminator */

        uint32_t length = hdr[0x04] | (hdr[0x05] << 8) |
                          (hdr[0x06] << 16) | (hdr[0x07] << 24);
        char fname[32];
        memset(fname, 0, sizeof(fname));
        strncpy(fname, (char *)(hdr + 0x28), 31);

        if (length > 0) {
            uint8_t *fdata = (uint8_t *)malloc(length);
            if (!fdata) { ret = -4; break; }
            if (read_all(fp, fdata, length) != 0) {
                free(fdata);
                ret = -5;
                break;
            }

            char out_path[256];
            snprintf(out_path, sizeof(out_path), "/%s/%s", new_dir, fname);
            int fd = mcio_mcOpen(out_path, sceMcFileAttrWriteable | sceMcFileAttrFile);
            if (fd >= 0) {
                mcio_mcWrite(fd, fdata, length);
                mcio_mcClose(fd);
            } else {
                ret = -6;
            }
            free(fdata);
        }
    }

    fclose(fp);

    if (out_dir && out_dir_len > 0) {
        strncpy(out_dir, new_dir, out_dir_len - 1);
        out_dir[out_dir_len - 1] = '\0';
    }
    return ret;
}
