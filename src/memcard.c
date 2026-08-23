#include "memcard.h"
#include "settings.h"
#include "debug.h"
#include "goodnames.h"
#include "mcio.h"
#include <dirent.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>

/* ============================================================
   GLOBALS
   ============================================================ */

EmulatorEntry g_emulators[MAX_EMULATORS];
int g_emulator_count = 0;

MemCardSlot g_slots[2];
int g_active_slot = 0;

static char g_user_home[128] = {0};
static int g_user_home_discovered = 0;

int g_confirm_dialog_open = 0;
int g_confirm_dialog_sel = 0;
char g_confirm_title[64] = {0};
char g_confirm_msg[256] = {0};
void (*g_confirm_yes_callback)(void) = NULL;
void (*g_confirm_no_callback)(void) = NULL;

/* ============================================================
   USER HOME DISCOVERY (dynamic, not hardcoded)
   ============================================================ */

const char *memcard_get_user_home(void) {
    if (!g_user_home_discovered) {
        memcard_discover_user_home();
    }
    return g_user_home[0] ? g_user_home : NULL;
}

void memcard_discover_user_home(void) {
    if (g_user_home_discovered) return;
    g_user_home[0] = '\0';

    DIR *dir = opendir("/user/home");
    if (!dir) {
        log_debug("memcard: cannot open /user/home");
        return;
    }

    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL) {
        if (entry->d_name[0] == '.') continue;

        /* Check if this dir has a savedata/ subdir */
        char test_path[256];
        snprintf(test_path, sizeof(test_path), "/user/home/%s/savedata", entry->d_name);

        struct stat st;
        if (stat(test_path, &st) == 0 && S_ISDIR(st.st_mode)) {
            snprintf(g_user_home, sizeof(g_user_home), "/user/home/%s", entry->d_name);
            log_debug("memcard: discovered user home: %s", g_user_home);
            break;
        }
    }
    closedir(dir);
    g_user_home_discovered = 1;
}

/* ============================================================
   EMULATOR LIST
   ============================================================ */

static const char *emulators_config_path(void) {
    static char path[512];
    snprintf(path, sizeof(path), "%s/config/emulators.txt", g_settings.work_path);
    return path;
}

void memcard_load_emulators(void) {
    g_emulator_count = 0;

    const char *path = emulators_config_path();
    FILE *fp = fopen(path, "r");
    if (!fp) {
        /* Create default */
        snprintf(g_emulators[0].id, sizeof(g_emulators[0].id), "PCSX20042");
        snprintf(g_emulators[0].name, sizeof(g_emulators[0].name), "Default");
        g_emulators[0].is_usb = 0;
        g_emulator_count = 1;
        memcard_save_emulators();
    } else {
        char line[512];
        while (fgets(line, sizeof(line), fp) && g_emulator_count < MAX_EMULATORS - 1) {
            char *nl = strchr(line, '\n');
            if (nl) *nl = '\0';
            if (line[0] == '\0' || line[0] == '#') continue;

            char *comma = strchr(line, ',');
            if (comma) {
                *comma = '\0';
                strncpy(g_emulators[g_emulator_count].id, line, 31);
                g_emulators[g_emulator_count].id[31] = '\0';
                strncpy(g_emulators[g_emulator_count].name, comma + 1, 63);
                g_emulators[g_emulator_count].name[63] = '\0';
            } else {
                strncpy(g_emulators[g_emulator_count].id, line, 31);
                g_emulators[g_emulator_count].id[31] = '\0';
                snprintf(g_emulators[g_emulator_count].name, 64, "Emu %d", g_emulator_count + 1);
            }
            g_emulators[g_emulator_count].is_usb = 0;
            g_emulator_count++;
        }
        fclose(fp);
    }

    /* Always append USB as the last virtual emulator */
    snprintf(g_emulators[g_emulator_count].id, sizeof(g_emulators[g_emulator_count].id), "USB");
    snprintf(g_emulators[g_emulator_count].name, sizeof(g_emulators[g_emulator_count].name), "USB Drive");
    g_emulators[g_emulator_count].is_usb = 1;
    g_emulator_count++;
}

void memcard_save_emulators(void) {
    const char *path = emulators_config_path();
    FILE *fp = fopen(path, "w");
    if (!fp) return;

    /* Don't write the virtual USB entry to file */
    int count = g_emulator_count;
    if (count > 0 && g_emulators[count - 1].is_usb) {
        count--;
    }

    for (int i = 0; i < count; i++) {
        fprintf(fp, "%s,%s\n", g_emulators[i].id, g_emulators[i].name);
    }
    fclose(fp);
}

/* ============================================================
   VMC FILE SCANNING
   ============================================================ */

void memcard_extract_disc_id(const char *filename, char *out, size_t out_len) {
    memset(out, 0, out_len);
    const char *dot = strrchr(filename, '.');
    size_t len = dot ? (size_t)(dot - filename) : strlen(filename);
    if (len >= out_len) len = out_len - 1;
    strncpy(out, filename, len);
    out[len] = '\0';
}

int memcard_is_vmc_file_size_valid(size_t sz) {
    return (sz == 0x800000 || sz == 0x840000 ||
            sz == 0x1000000 || sz == 0x1080000 ||
            sz == 0x2000000 || sz == 0x2100000 ||
            sz == 0x4000000 || sz == 0x4200000);
}

static int is_vmc_extension(const char *name) {
    int len = strlen(name);
    if (len < 5) return 0;
    const char *ext = name + len - 4;
    return (strcasecmp(ext, ".bin") == 0 ||
            strcasecmp(ext, ".vm2") == 0 ||
            strcasecmp(ext, ".vmc") == 0);
}

static void build_vmc_display_name(const char *disc_id, char *out, size_t out_len) {
    /* Use existing goodnames database */
    char game_name[256];
    build_display_name(disc_id, disc_id, game_name, sizeof(game_name));

    if (game_name[0] && strcasecmp(game_name, disc_id) != 0) {
        snprintf(out, out_len, "%s : %s", disc_id, game_name);
    } else {
        snprintf(out, out_len, "%s", disc_id);
    }
}

void memcard_scan_vmc_files(int slot_idx) {
    if (slot_idx < 0 || slot_idx >= 2) return;
    MemCardSlot *slot = &g_slots[slot_idx];

    slot->vmc_count = 0;
    slot->save_count = 0;
    slot->save_idx = -1;
    slot->vmc_loaded = 0;
    slot->loaded_vmc_path[0] = '\0';

    if (slot->emulator_idx < 0 || slot->emulator_idx >= g_emulator_count) return;

    EmulatorEntry *emu = &g_emulators[slot->emulator_idx];
    char scan_path[512];

    if (emu->is_usb) {
        snprintf(scan_path, sizeof(scan_path), "/mnt/usb0/PS2VMC");
    } else {
        const char *home = memcard_get_user_home();
        if (!home) return;
        snprintf(scan_path, sizeof(scan_path), "%s/savedata/%s", home, emu->id);
    }

    DIR *dir = opendir(scan_path);
    if (!dir) {
        log_debug("memcard: cannot open VMC dir: %s", scan_path);
        return;
    }

    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL && slot->vmc_count < MAX_VMC_FILES) {
        if (entry->d_name[0] == '.') continue;
        if (strncmp(entry->d_name, "sce_bu_", 7) == 0) continue; /* Skip Sony backups */
        if (!is_vmc_extension(entry->d_name)) continue;

        char full_path[768];
        snprintf(full_path, sizeof(full_path), "%s/%s", scan_path, entry->d_name);

        struct stat st;
        if (stat(full_path, &st) != 0) continue;
        if (!S_ISREG(st.st_mode)) continue;

        /* For USB, parse emulator prefix if present: PCSX20042_SLUS-21214.VM2 */
        char disc_id[32];
        if (emu->is_usb) {
            const char *underscore = strchr(entry->d_name, '_');
            if (underscore) {
                size_t id_len = underscore - entry->d_name;
                if (id_len < sizeof(disc_id)) {
                    strncpy(disc_id, underscore + 1, sizeof(disc_id) - 1);
                    char *dot = strrchr(disc_id, '.');
                    if (dot) *dot = '\0';
                } else {
                    memcard_extract_disc_id(entry->d_name, disc_id, sizeof(disc_id));
                }
            } else {
                memcard_extract_disc_id(entry->d_name, disc_id, sizeof(disc_id));
            }
        } else {
            memcard_extract_disc_id(entry->d_name, disc_id, sizeof(disc_id));
        }

        VmcFile *vf = &slot->vmc_files[slot->vmc_count];
        memset(vf, 0, sizeof(VmcFile));
        strncpy(vf->filename, entry->d_name, sizeof(vf->filename) - 1);
        strncpy(vf->disc_id, disc_id, sizeof(vf->disc_id) - 1);
        strncpy(vf->full_path, full_path, sizeof(vf->full_path) - 1);
        vf->file_size = st.st_size;
        build_vmc_display_name(disc_id, vf->display_name, sizeof(vf->display_name));

        slot->vmc_count++;
    }
    closedir(dir);

    if (slot->vmc_idx >= slot->vmc_count) {
        slot->vmc_idx = (slot->vmc_count > 0) ? 0 : -1;
    }

    log_debug("memcard: slot %d scanned %d VMC files in %s", slot_idx, slot->vmc_count, scan_path);
}

/* ============================================================
   VMC LOADING via Apollo mcio
   ============================================================ */

static void read_icon_sys_title(const uint8_t *icon_sys_data, char *title, size_t title_len) {
    memset(title, 0, title_len);
    if (!icon_sys_data) return;
    /* icon.sys title at offset 0xC0, max 68 bytes */
    int copy_len = 68;
    if (copy_len >= (int)title_len) copy_len = (int)title_len - 1;
    memcpy(title, icon_sys_data + 0xC0, copy_len);
    title[copy_len] = '\0';
    /* Trim trailing spaces/nulls */
    int len = (int)strlen(title);
    while (len > 0 && (title[len - 1] == ' ' || title[len - 1] == '\0')) {
        title[len - 1] = '\0';
        len--;
    }
}

void memcard_unload_current_vmc(void) {
    mcio_vmcFinish();
}

void memcard_load_vmc(int slot_idx) {
    if (slot_idx < 0 || slot_idx >= 2) return;
    MemCardSlot *slot = &g_slots[slot_idx];

    slot->save_count = 0;
    slot->save_idx = -1;
    slot->vmc_loaded = 0;
    slot->loaded_vmc_path[0] = '\0';

    if (slot->vmc_idx < 0 || slot->vmc_idx >= slot->vmc_count) return;

    VmcFile *vf = &slot->vmc_files[slot->vmc_idx];
    strncpy(slot->loaded_vmc_path, vf->full_path, sizeof(slot->loaded_vmc_path) - 1);

    /* Unload any previously loaded VMC first */
    memcard_unload_current_vmc();

    if (mcio_vmcInit(vf->full_path) != sceMcResSucceed) {
        log_debug("memcard: failed to load VMC: %s", vf->full_path);
        return;
    }

    slot->vmc_loaded = 1;
    log_debug("memcard: loaded VMC: %s", vf->full_path);

    /* Read root directory to find save games */
    int dd = mcio_mcDopen("/");
    if (dd < 0) {
        log_debug("memcard: mcDopen failed on VMC root");
        mcio_vmcFinish();
        slot->vmc_loaded = 0;
        return;
    }

    struct io_dirent dirent;
    int slot_num = 1;
    while (mcio_mcDread(dd, &dirent) > 0 && slot->save_count < MAX_VMC_SAVES) {
        if (!(dirent.stat.mode & sceMcFileAttrSubdir)) continue;
        if (strcmp(dirent.name, ".") == 0 || strcmp(dirent.name, "..") == 0) continue;

        VmcSaveEntry *se = &slot->saves[slot->save_count];
        memset(se, 0, sizeof(VmcSaveEntry));
        strncpy(se->dir_name, dirent.name, sizeof(se->dir_name) - 1);
        strncpy(se->title, dirent.name, sizeof(se->title) - 1); /* fallback */
        se->slot_num = slot_num++;
        se->blocks = (dirent.stat.size + 8191) / 8192;
        if (se->blocks < 1) se->blocks = 1;

        /* Try to read icon.sys for title */
        char icon_path[128];
        snprintf(icon_path, sizeof(icon_path), "/%s/icon.sys", dirent.name);
        int fd = mcio_mcOpen(icon_path, sceMcFileAttrReadable | sceMcFileAttrFile);
        if (fd >= 0) {
            uint8_t icon_data[1024];
            int n = mcio_mcRead(fd, (void *)icon_data, sizeof(icon_data));
            mcio_mcClose(fd);
            if (n >= 964) {
                read_icon_sys_title(icon_data, se->title, sizeof(se->title));
            }
        }

        slot->save_count++;
    }
    mcio_mcDclose(dd);

    if (slot->save_count > 0 && slot->save_idx < 0) {
        slot->save_idx = 0;
    }

    /* Keep VMC loaded in memory for browsing */
    /* Don't call mcio_vmcFinish() until we need to write or switch VMCs */
}

void memcard_set_slot_off(int slot_idx) {
    if (slot_idx < 0 || slot_idx >= 2) return;
    MemCardSlot *slot = &g_slots[slot_idx];
    slot->state = SLOT_STATE_OFF;
    slot->emulator_idx = -1;
    slot->vmc_idx = -1;
    slot->save_idx = -1;
    slot->save_count = 0;
    slot->vmc_count = 0;
    slot->vmc_loaded = 0;
    slot->in_dropdown = 0;
    slot->focus_element = 0;
    slot->loaded_vmc_path[0] = '\0';
}

void memcard_refresh_slot(int slot_idx) {
    if (slot_idx < 0 || slot_idx >= 2) return;
    MemCardSlot *slot = &g_slots[slot_idx];

    if (slot->emulator_idx < 0) {
        memcard_set_slot_off(slot_idx);
        return;
    }

    slot->state = SLOT_STATE_EMU_SEL;
    memcard_scan_vmc_files(slot_idx);

    if (slot->vmc_idx >= 0 && slot->vmc_count > 0) {
        slot->state = SLOT_STATE_VMC_SEL;
        memcard_load_vmc(slot_idx);
    }
}

/* ============================================================
   COPY / DELETE / BACKUP OPERATIONS
   ============================================================ */

static int read_save_directory(const char *dir_name, SaveDirectory *out) {
    memset(out, 0, sizeof(SaveDirectory));

    int dd = mcio_mcDopen(dir_name);
    if (dd < 0) return 0;

    struct io_dirent dirent;
    while (mcio_mcDread(dd, &dirent) > 0 && out->count < MAX_SAVE_FILES) {
        if (strcmp(dirent.name, ".") == 0 || strcmp(dirent.name, "..") == 0) continue;
        if (dirent.stat.mode & sceMcFileAttrSubdir) continue; /* Skip nested dirs for now */

        char path[128];
        snprintf(path, sizeof(path), "%s/%s", dir_name, dirent.name);

        struct io_dirent st;
        if (mcio_mcStat(path, &st) < 0) continue;

        size_t size = st.stat.size;
        if (size > MAX_FILE_SIZE) {
            log_debug("memcard: file too large, skipping: %s (%zu bytes)", path, size);
            continue;
        }

        uint8_t *data = (uint8_t *)malloc(size);
        if (!data) continue;

        int fd = mcio_mcOpen(path, sceMcFileAttrReadable | sceMcFileAttrFile);
        if (fd < 0) {
            free(data);
            continue;
        }

        int n = mcio_mcRead(fd, data, size);
        mcio_mcClose(fd);

        if (n != (int)size) {
            free(data);
            continue;
        }

        strncpy(out->files[out->count].name, dirent.name, 63);
        out->files[out->count].name[63] = '\0';
        out->files[out->count].data = data;
        out->files[out->count].size = size;
        out->count++;
    }

    mcio_mcDclose(dd);
    return out->count > 0;
}

static void free_save_directory(SaveDirectory *dir) {
    for (int i = 0; i < dir->count; i++) {
        if (dir->files[i].data) {
            free(dir->files[i].data);
            dir->files[i].data = NULL;
        }
    }
    dir->count = 0;
}

static int write_save_directory(const char *dir_name, SaveDirectory *dir) {
    mcio_mcMkDir(dir_name);

    for (int i = 0; i < dir->count; i++) {
        char path[128];
        snprintf(path, sizeof(path), "%s/%s", dir_name, dir->files[i].name);

        int fd = mcio_mcOpen(path, sceMcFileAttrWriteable | sceMcFileAttrFile);
        if (fd < 0) {
            log_debug("memcard: mcOpen failed for write: %s", path);
            return 0;
        }

        int n = mcio_mcWrite(fd, dir->files[i].data, dir->files[i].size);
        mcio_mcClose(fd);

        if (n != (int)dir->files[i].size) {
            log_debug("memcard: mcWrite incomplete: %s", path);
            return 0;
        }
    }
    return 1;
}

static int vmc_has_directory(const char *dir_name) {
    struct io_dirent st;
    return (mcio_mcStat(dir_name, &st) == 0 && (st.stat.mode & sceMcFileAttrSubdir));
}

static void delete_vmc_directory(const char *dir_name) {
    int dd = mcio_mcDopen(dir_name);
    if (dd >= 0) {
        struct io_dirent dirent;
        while (mcio_mcDread(dd, &dirent) > 0) {
            if (strcmp(dirent.name, ".") == 0 || strcmp(dirent.name, "..") == 0) continue;
            char fpath[128];
            snprintf(fpath, sizeof(fpath), "%s/%s", dir_name, dirent.name);
            mcio_mcRemove(fpath);
        }
        mcio_mcDclose(dd);
    }
    mcio_mcRmDir(dir_name);
}

/* --- Copy save between slots --- */

int memcard_copy_save_between_slots(int src_slot, int dst_slot) {
    if (src_slot < 0 || src_slot >= 2 || dst_slot < 0 || dst_slot >= 2) return 0;
    if (src_slot == dst_slot) return 0;

    MemCardSlot *src = &g_slots[src_slot];
    MemCardSlot *dst = &g_slots[dst_slot];

    if (!src->vmc_loaded || src->save_idx < 0 || src->save_idx >= src->save_count) return 0;
    if (!dst->vmc_loaded) return 0;

    const char *src_dir = src->saves[src->save_idx].dir_name;

    /* Step 1: Load source VMC and read save */
    memcard_unload_current_vmc();
    if (mcio_vmcInit(src->loaded_vmc_path) != sceMcResSucceed) return 0;

    SaveDirectory savedir;
    if (!read_save_directory(src_dir, &savedir)) {
        mcio_vmcFinish();
        return 0;
    }

    /* Step 2: Load destination VMC and write save */
    memcard_unload_current_vmc();
    if (mcio_vmcInit(dst->loaded_vmc_path) != sceMcResSucceed) {
        free_save_directory(&savedir);
        return 0;
    }

    /* Check if destination already has this directory */
    if (vmc_has_directory(src_dir)) {
        /* Don't auto-overwrite; caller should have shown confirm dialog */
        log_debug("memcard: destination already has %s, aborting copy", src_dir);
        free_save_directory(&savedir);
        mcio_vmcFinish();
        return 0;
    }

    int ok = write_save_directory(src_dir, &savedir);
    free_save_directory(&savedir);

    if (ok) {
        mcio_vmcFinish(); /* Commit changes to dest VMC */
        log_debug("memcard: copied %s from slot %d to slot %d", src_dir, src_slot, dst_slot);
        /* Refresh destination slot */
        memcard_load_vmc(dst_slot);
        src->vmc_loaded = 0; /* Source buffer was replaced by dest VMC load; force reload */
        return 1;
    } else {
        mcio_vmcFinish();
        return 0;
    }
}

/* --- Delete save --- */

int memcard_delete_save(int slot_idx) {
    if (slot_idx < 0 || slot_idx >= 2) return 0;
    MemCardSlot *slot = &g_slots[slot_idx];
    if (!slot->vmc_loaded || slot->save_idx < 0 || slot->save_idx >= slot->save_count) return 0;

    memcard_unload_current_vmc();
    if (mcio_vmcInit(slot->loaded_vmc_path) != sceMcResSucceed) return 0;

    delete_vmc_directory(slot->saves[slot->save_idx].dir_name);
    mcio_vmcFinish();

    memcard_load_vmc(slot_idx);
    return 1;
}

/* --- Backup full VMC to USB --- */

static int copy_file(const char *src, const char *dst) {
    int sfd = open(src, O_RDONLY);
    if (sfd < 0) return 0;
    int dfd = open(dst, O_WRONLY | O_CREAT | O_TRUNC, 0777);
    if (dfd < 0) { close(sfd); return 0; }
    char buf[65536];
    int n;
    while ((n = read(sfd, buf, sizeof(buf))) > 0) {
        write(dfd, buf, n);
    }
    close(sfd);
    close(dfd);
    return 1;
}

int memcard_backup_vmc_to_usb(int slot_idx, const char *usb_base) {
    if (slot_idx < 0 || slot_idx >= 2) return 0;
    MemCardSlot *slot = &g_slots[slot_idx];
    if (slot->loaded_vmc_path[0] == '\0') return 0;

    char dst_dir[512];
    snprintf(dst_dir, sizeof(dst_dir), "%s/PS2VMC", usb_base);
    mkdir(dst_dir, 0777);

    EmulatorEntry *emu = &g_emulators[slot->emulator_idx];
    char dst_name[256];
    if (emu->is_usb) {
        /* USB→USB backup: keep original name */
        const char *base = strrchr(slot->loaded_vmc_path, '/');
        if (!base) base = slot->loaded_vmc_path;
        else base++;
        snprintf(dst_name, sizeof(dst_name), "%s", base);
    } else {
        snprintf(dst_name, sizeof(dst_name), "%s_%s.VM2",
                 emu->id, slot->vmc_files[slot->vmc_idx].disc_id);
    }

    char dst_path[768];
    snprintf(dst_path, sizeof(dst_path), "%s/%s", dst_dir, dst_name);
    return copy_file(slot->loaded_vmc_path, dst_path);
}

/* --- Import full VMC from USB --- */

int memcard_import_vmc_from_usb(int slot_idx, const char *usb_path) {
    if (slot_idx < 0 || slot_idx >= 2) return 0;
    MemCardSlot *slot = &g_slots[slot_idx];
    if (slot->emulator_idx < 0) return 0;

    EmulatorEntry *emu = &g_emulators[slot->emulator_idx];
    if (emu->is_usb) return 0; /* Can't import TO USB */

    const char *home = memcard_get_user_home();
    if (!home) return 0;

    char dst_dir[512];
    snprintf(dst_dir, sizeof(dst_dir), "%s/savedata/%s", home, emu->id);
    mkdir(dst_dir, 0777);

    const char *src_name = strrchr(usb_path, '/');
    if (src_name) src_name++;
    else src_name = usb_path;

    char dst_path[768];
    snprintf(dst_path, sizeof(dst_path), "%s/%s", dst_dir, src_name);

    return copy_file(usb_path, dst_path);
}

/* --- Format VMC --- */

int memcard_format_vmc(int slot_idx) {
    if (slot_idx < 0 || slot_idx >= 2) return 0;
    MemCardSlot *slot = &g_slots[slot_idx];
    if (!slot->vmc_loaded) return 0;

    memcard_unload_current_vmc();
    if (mcio_vmcInit(slot->loaded_vmc_path) != sceMcResSucceed) return 0;

    int r = mcio_mcFormat();
    if (r == sceMcResSucceed) {
        mcio_vmcFinish();
        log_debug("memcard: formatted VMC: %s", slot->loaded_vmc_path);
        memcard_load_vmc(slot_idx);
        return 1;
    }
    mcio_vmcFinish();
    return 0;
}

/* --- PSU export/import (stubs for Phase 2) --- */

int memcard_export_save_psu(int slot_idx, const char *usb_base) {
    /* TODO: Phase 2 - pack save directory into .PSU format */
    log_debug("memcard: export_save_psu not yet implemented");
    (void)slot_idx; (void)usb_base;
    return 0;
}

int memcard_import_save_psu(int slot_idx, const char *psu_path) {
    /* TODO: Phase 2 - unpack .PSU into VMC */
    log_debug("memcard: import_save_psu not yet implemented");
    (void)slot_idx; (void)psu_path;
    return 0;
}

/* ============================================================
   CONFIRMATION DIALOG
   ============================================================ */

void memcard_show_confirm(const char *title, const char *msg,
                          void (*yes_cb)(void), void (*no_cb)(void)) {
    g_confirm_dialog_open = 1;
    g_confirm_dialog_sel = 0; /* Default to NO */
    strncpy(g_confirm_title, title, sizeof(g_confirm_title) - 1);
    g_confirm_title[sizeof(g_confirm_title) - 1] = '\0';
    strncpy(g_confirm_msg, msg, sizeof(g_confirm_msg) - 1);
    g_confirm_msg[sizeof(g_confirm_msg) - 1] = '\0';
    g_confirm_yes_callback = yes_cb;
    g_confirm_no_callback = no_cb;
}

void memcard_confirm_yes(void) {
    g_confirm_dialog_open = 0;
    if (g_confirm_yes_callback) {
        void (*cb)(void) = g_confirm_yes_callback;
        g_confirm_yes_callback = NULL;
        g_confirm_no_callback = NULL;
        cb();
    }
}

void memcard_confirm_no(void) {
    g_confirm_dialog_open = 0;
    if (g_confirm_no_callback) {
        void (*cb)(void) = g_confirm_no_callback;
        g_confirm_yes_callback = NULL;
        g_confirm_no_callback = NULL;
        cb();
    }
}

/* ============================================================
   INIT
   ============================================================ */

void memcard_init(void) {
    static int initialized = 0;
    if (initialized) return;
    initialized = 1;

    memcard_discover_user_home();
    memcard_load_emulators();

    /* Both slots start OFF */
    memset(g_slots, 0, sizeof(g_slots));
    g_slots[0].state = SLOT_STATE_OFF;
    g_slots[0].emulator_idx = -1;
    g_slots[0].vmc_idx = -1;
    g_slots[0].save_idx = -1;
    g_slots[0].focus_element = 0;

    g_slots[1].state = SLOT_STATE_OFF;
    g_slots[1].emulator_idx = -1;
    g_slots[1].vmc_idx = -1;
    g_slots[1].save_idx = -1;
    g_slots[1].focus_element = 0;

    g_active_slot = 0;
}
