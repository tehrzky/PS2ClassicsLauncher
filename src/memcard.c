#include "memcard.h"
#include "sd.h"
#include <sys/stat.h>
#include "settings.h"
#include "debug.h"
#include "goodnames.h"
#include "mcio.h"
#include "mcio_compat.h"
#include "ps2icon.h"
#include "sjis.h"
#include "psu.h"
#include <dirent.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>
#include "sd.h"
#include <orbis/UserService.h>


/* ============================================================
   GLOBALS
   ============================================================ */

EmulatorEntry g_emulators[MAX_EMULATORS];
int g_emulator_count = 0;

MemCardSlot g_slots[2];
int g_active_slot = 0;
int g_memcard_action_menu_open = 0;
int g_memcard_action_sel = 0;

static char g_user_home[128] = {0};
static int g_user_home_discovered = 0;

int g_confirm_dialog_open = 0;
int g_confirm_dialog_sel = 0;
char g_confirm_title[64] = {0};
char g_confirm_msg[256] = {0};
void (*g_confirm_yes_callback)(void) = NULL;
void (*g_confirm_no_callback)(void) = NULL;

#define MAX_MOUNTS 32
static struct {
    char mount_path[512];
    int active;
} g_mounts[MAX_MOUNTS];
static int g_mount_count = 0;

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
    g_user_home_discovered = 1;

    OrbisUserServiceInitializeParams param;
    param.priority = ORBIS_KERNEL_PRIO_FIFO_LOWEST;
    sceUserServiceInitialize(&param);

    int32_t user_id = 0;
    int ret = sceUserServiceGetInitialUser(&user_id);
    if (ret < 0) {
        log_debug("memcard: sceUserServiceGetInitialUser failed: 0x%08X", ret);
        return;
    }

    snprintf(g_user_home, sizeof(g_user_home), "/user/home/%08x", user_id);

    struct stat st;
    char test_path[160];
    snprintf(test_path, sizeof(test_path), "%s/savedata", g_user_home);
    if (stat(test_path, &st) != 0 || !S_ISDIR(st.st_mode)) {
        log_debug("memcard: constructed home not reachable: %s", g_user_home);
        g_user_home[0] = '\0';
    } else {
        log_debug("memcard: discovered user home: %s", g_user_home);
    }
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

static int is_vmc_file(const char *name) {
    /* PS4 emulator stores real VMCs as sdimg_<DISC_ID> with no extension */
    if (strncmp(name, "sdimg_", 6) == 0)
        return 1;
    
    /* USB/imported VMCs use standard extensions */
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

void memcard_unmount_all(void)
{
    for (int i = 0; i < g_mount_count; i++) {
        if (g_mounts[i].active) {
            umountSave(g_mounts[i].mount_path, 0, 0);
            rmdir(g_mounts[i].mount_path);
            g_mounts[i].active = 0;
        }
    }
    g_mount_count = 0;
}

/* Recursive mkdir helper */
static int memcard_mkdirs(const char *path)
{
    char tmp[512];
    char *p = NULL;
    size_t len;
    snprintf(tmp, sizeof(tmp), "%s", path);
    len = strlen(tmp);
    if (tmp[len - 1] == '/')
        tmp[len - 1] = 0;
    for (p = tmp + 1; *p; p++) {
        if (*p == '/') {
            *p = 0;
            mkdir(tmp, 0777);
            *p = '/';
        }
    }
    mkdir(tmp, 0777);
    return 0;
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
        if (!home) {
            log_debug("memcard: no user home, aborting scan");
            return;
        }
        snprintf(scan_path, sizeof(scan_path), "%s/savedata/%s", home, emu->id);
        log_debug("memcard: scanning internal savedata: %s", scan_path);

        memcard_unmount_all();

        DIR *sdimg_dir = opendir(scan_path);
        if (!sdimg_dir) {
            log_debug("memcard: cannot open savedata dir: %s", scan_path);
            return;
        }

        struct dirent *sdimg_entry;
        while ((sdimg_entry = readdir(sdimg_dir)) != NULL) {
            if (strncmp(sdimg_entry->d_name, "sdimg_", 6) != 0)
                continue;

            log_debug("memcard: found sdimg: %s", sdimg_entry->d_name);

            char disc_id[32] = {0};
            char volume_path[512] = {0};
            char key_path[512] = {0};
            char mount_path[256] = {0};

            size_t name_len = strlen(sdimg_entry->d_name);
            if (name_len <= 6) continue;

            size_t id_len = name_len - 6;
            if (id_len >= sizeof(disc_id)) continue;
            memcpy(disc_id, sdimg_entry->d_name + 6, id_len);
            disc_id[id_len] = '\0';

            int n;
            n = snprintf(volume_path, sizeof(volume_path),
                         "%s/savedata/%s/%s", home, emu->id, sdimg_entry->d_name);
            if (n < 0 || (size_t)n >= sizeof(volume_path)) continue;

            n = snprintf(key_path, sizeof(key_path),
                         "%s/savedata/%s/%s.bin", home, emu->id, disc_id);
            if (n < 0 || (size_t)n >= sizeof(key_path)) continue;

            n = snprintf(mount_path, sizeof(mount_path),
                         "/tmp/ps2mc_%s", disc_id);
            if (n < 0 || (size_t)n >= sizeof(mount_path)) continue;

            struct stat st_vol;
            if (stat(volume_path, &st_vol) != 0 || !S_ISREG(st_vol.st_mode)) continue;

            struct stat st_key;
            if (stat(key_path, &st_key) != 0 || !S_ISREG(st_key.st_mode)) continue;

            memcard_mkdirs(mount_path);

            int mount_err = mountSave(volume_path, key_path, mount_path);
            if (mount_err == -99) {
                log_debug("memcard: priv libs not available, skip");
                rmdir(mount_path);
                continue;
            }
            if (mount_err < 0) {
                log_debug("memcard: mountSave failed (%d)", mount_err);
                rmdir(mount_path);
                continue;
            }

            if (g_mount_count < MAX_MOUNTS) {
                size_t mp_len = strlen(mount_path);
                if (mp_len < sizeof(g_mounts[0].mount_path)) {
                    memcpy(g_mounts[g_mount_count].mount_path, mount_path, mp_len + 1);
                    g_mounts[g_mount_count].active = 1;
                    g_mount_count++;
                }
            }

            DIR *mnt_dir = opendir(mount_path);
            if (!mnt_dir) continue;

            struct dirent *mnt_entry;
            while ((mnt_entry = readdir(mnt_dir)) != NULL) {
                if (strcmp(mnt_entry->d_name, ".") == 0 ||
                    strcmp(mnt_entry->d_name, "..") == 0)
                    continue;
                if (!is_vmc_file(mnt_entry->d_name))
                    continue;

                char vmc_path[512];
                n = snprintf(vmc_path, sizeof(vmc_path), "%s/%s",
                             mount_path, mnt_entry->d_name);
                if (n < 0 || (size_t)n >= sizeof(vmc_path)) continue;

                struct stat vmc_st;
                if (stat(vmc_path, &vmc_st) != 0) continue;
                if (!S_ISREG(vmc_st.st_mode)) continue;
                if (vmc_st.st_size < 0x100000) continue;

                MemCardSlot *s = &g_slots[slot];
                if (s->vmc_count >= MAX_VMC_FILES) break;

                VmcFile *vf = &s->vmc_files[s->vmc_count];
                memset(vf, 0, sizeof(VmcFile));

                size_t path_len = strlen(vmc_path);
                if (path_len >= sizeof(vf->full_path)) path_len = sizeof(vf->full_path) - 1;
                memcpy(vf->full_path, vmc_path, path_len);
                vf->full_path[path_len] = '\0';

                size_t did_len = strlen(disc_id);
                if (did_len >= sizeof(vf->disc_id)) did_len = sizeof(vf->disc_id) - 1;
                memcpy(vf->disc_id, disc_id, did_len);
                vf->disc_id[did_len] = '\0';

                size_t fn_len = strlen(mnt_entry->d_name);
                if (fn_len >= sizeof(vf->filename)) fn_len = sizeof(vf->filename) - 1;
                memcpy(vf->filename, mnt_entry->d_name, fn_len);
                vf->filename[fn_len] = '\0';

                vf->file_size = vmc_st.st_size;

                s->vmc_count++;
            }
            closedir(mnt_dir);
        }
        closedir(sdimg_dir);

        log_debug("memcard: slot %d scanned %d VMC files from mounted saves",
                  slot, g_slots[slot].vmc_count);
        return;
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
        if (!is_vmc_file(entry->d_name)) continue;

        char full_path[768];
        snprintf(full_path, sizeof(full_path), "%s/%s", scan_path, entry->d_name);

        struct stat st;
        if (stat(full_path, &st) != 0) continue;
        if (!S_ISREG(st.st_mode)) continue;

         if (st.st_size < 0x100000) {  /* Less than 1MB = not a real VMC */
            log_debug("memcard: skipping %s (size %zu too small)", entry->d_name, st.st_size);
            continue;
        }

        /* Extract disc ID from filename */
        char disc_id[32];
        if (strncmp(entry->d_name, "sdimg_", 6) == 0) {
            /* PS4 savedata format: sdimg_SCUS-97402 */
            strncpy(disc_id, entry->d_name + 6, sizeof(disc_id) - 1);
            disc_id[sizeof(disc_id) - 1] = '\0';
        } else if (emu->is_usb) {
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

/* Free all decoded icons for a slot */
static void free_slot_icons(MemCardSlot *slot)
{
    for (int i = 0; i < slot->save_count; i++) {
        if (slot->saves[i].icon_rgba) {
            free(slot->saves[i].icon_rgba);
            slot->saves[i].icon_rgba = NULL;
            slot->saves[i].icon_w = 0;
            slot->saves[i].icon_h = 0;
        }
    }
}

void memcard_unload_current_vmc(void) {
    mcio_vmcFinish();
}

void memcard_load_vmc(int slot_idx) {
    if (slot_idx < 0 || slot_idx >= 2) return;
    MemCardSlot *slot = &g_slots[slot_idx];

    free_slot_icons(slot);
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

        /* Read title from icon.sys (with SJIS->UTF8) */
        char icon_sys_path[128];
        snprintf(icon_sys_path, sizeof(icon_sys_path), "/%s/icon.sys", dirent.name);
        int fd = mcio_mcOpen(icon_sys_path, sceMcFileAttrReadable | sceMcFileAttrFile);
        if (fd >= 0) {
            uint8_t icon_data[1024];
            int n = mcio_mcRead(fd, (void *)icon_data, sizeof(icon_data));
            mcio_mcClose(fd);
            if (n >= 964) {
                ps2icon_parse_title(icon_data, n, se->title, sizeof(se->title));
            }
        }

        /* Load icon0.ico */
        char icon0_path[128];
        snprintf(icon0_path, sizeof(icon0_path), "/%s/icon0.ico", dirent.name);
        int fd_icon = mcio_mcOpen(icon0_path, sceMcFileAttrReadable | sceMcFileAttrFile);
        if (fd_icon >= 0) {
            uint8_t ico_buf[512];
            int n_icon = mcio_mcRead(fd_icon, ico_buf, sizeof(ico_buf));
            mcio_mcClose(fd_icon);
            if (n_icon >= 160) {
                ps2icon_decode(ico_buf, n_icon,
                               &se->icon_rgba, &se->icon_w, &se->icon_h);
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
    free_slot_icons(slot);
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

    free_slot_icons(slot);
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
        /* Refresh both slots so UI matches physical state */
        memcard_scan_vmc_files(src_slot);
        memcard_load_vmc(src_slot);
        memcard_scan_vmc_files(dst_slot);
        memcard_load_vmc(dst_slot);
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

/* ============================================================
   TOAST NOTIFICATIONS
   ============================================================ */

char g_toast_msg[128] = {0};
int g_toast_timer = 0;

void memcard_show_toast(const char *msg)
{
    strncpy(g_toast_msg, msg, sizeof(g_toast_msg) - 1);
    g_toast_msg[sizeof(g_toast_msg) - 1] = '\0';
    g_toast_timer = TOAST_DURATION;
}

void memcard_update_toast(void)
{
    if (g_toast_timer > 0) g_toast_timer--;
}

/* ============================================================
   PSU FILE PICKER
   ============================================================ */

char g_psu_files[MAX_PSU_FILES][256];
int g_psu_file_count = 0;
int g_psu_picker_open = 0;
int g_psu_picker_sel = 0;

void memcard_scan_psu_files(void)
{
    g_psu_file_count = 0;
    DIR *dir = opendir("/mnt/usb0/PS2SAVES");
    if (!dir) return;

    struct dirent *ent;
    while ((ent = readdir(dir)) != NULL && g_psu_file_count < MAX_PSU_FILES) {
        int len = strlen(ent->d_name);
        if (len < 5) continue;
        const char *ext = ent->d_name + len - 4;
        if (strcasecmp(ext, ".psu") == 0) {
            snprintf(g_psu_files[g_psu_file_count], 256,
                     "/mnt/usb0/PS2SAVES/%s", ent->d_name);
            g_psu_file_count++;
        }
    }
    closedir(dir);
    g_psu_picker_sel = 0;
}

/* --- PSU export/import --- */

int memcard_export_save_psu(int slot_idx, const char *usb_base) {
    if (slot_idx < 0 || slot_idx >= 2) return 0;
    MemCardSlot *slot = &g_slots[slot_idx];
    if (!slot->vmc_loaded || slot->save_idx < 0 || slot->save_idx >= slot->save_count)
        return 0;

    char psu_path[512];
    snprintf(psu_path, sizeof(psu_path), "%s/PS2SAVES/%s.psu",
             usb_base, slot->saves[slot->save_idx].dir_name);

    char dir_path[512];
    snprintf(dir_path, sizeof(dir_path), "%s/PS2SAVES", usb_base);
    mkdir(dir_path, 0777);

    int r = psu_export_save(slot->saves[slot->save_idx].dir_name, psu_path);
    return (r == 0) ? 1 : 0;
}

int memcard_import_save_psu(int slot_idx, const char *psu_path) {
    if (slot_idx < 0 || slot_idx >= 2) return 0;
    MemCardSlot *slot = &g_slots[slot_idx];
    if (!slot->vmc_loaded) return 0;

    char new_dir[32];
    int r = psu_import_save(psu_path, new_dir, sizeof(new_dir));
    if (r != 0) return 0;

    memcard_load_vmc(slot_idx);
    return 1;
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
