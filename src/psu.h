#ifndef PSU_H
#define PSU_H

#include <stdint.h>

/* Export a single save directory from the currently mounted VMC to a .PSU file.
 * vmc_dir   : directory name inside VMC (e.g. "BASLUS-12345")
 * psu_path  : full host path to write (e.g. "/mnt/usb0/save.psu")
 * Returns 0 on success, negative on error.
 */
int psu_export_save(const char *vmc_dir, const char *psu_path);

/* Import a .PSU file into the currently mounted VMC.
 * psu_path  : full host path to read
 * out_dir   : buffer to receive the created directory name (min 32 bytes)
 * Returns 0 on success, negative on error.
 */
int psu_import_save(const char *psu_path, char *out_dir, size_t out_dir_len);

#endif
