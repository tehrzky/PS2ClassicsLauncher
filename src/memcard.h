#ifndef MEMCARD_H
#define MEMCARD_H

#include <stddef.h>
#include <stdint.h>

#define MAX_EMULATORS       16
#define MAX_VMC_FILES       128
#define MAX_VMC_SAVES       64
#define MAX_SAVE_FILES      32          /* Max files inside one save directory */
#define MAX_FILE_SIZE       (512*1024)  /* Max single file size from VMC */

/* Slot states */
#define SLOT_STATE_OFF      0
#define SLOT_STATE_EMU_SEL  1
#define SLOT_STATE_VMC_SEL  2

/* An emulator entry (from emulators.txt) */
typedef struct {
    char id[32];        /* PS4 Title ID, e.g. "PCSX20042" */
    char name[64];      /* Display name */
    int is_usb;         /* 1 = virtual USB emulator */
} EmulatorEntry;

/* A VMC file discovered in savedata or USB */
typedef struct {
    char filename[256];     /* e.g., "SLUS-21214.bin" */
    char disc_id[32];       /* e.g., "SLUS-21214" */
    char display_name[256]; /* e.g., "SLUS-21214 : Final Fantasy X" */
    char full_path[512];    /* Absolute path */
    size_t file_size;
} VmcFile;

/* A save game directory inside a VMC */
typedef struct {
    char dir_name[32];      /* Directory name inside VMC */
    char title[128];        /* Title from icon.sys (ASCII/SJIS raw) */
    int blocks;
    int slot_num;           /* 1-based position in grid */
    uint32_t *icon_rgba;    /* Decoded icon pixels, ARGB format */
    int icon_w;
    int icon_h;
} VmcSaveEntry;

/* In-memory buffer for one file during copy operations */
typedef struct {
    char name[64];
    uint8_t *data;
    size_t size;
} SaveFileBuffer;

typedef struct {
    SaveFileBuffer files[MAX_SAVE_FILES];
    int count;
} SaveDirectory;

/* One memory card slot (left or right panel) */
typedef struct {
    int state;              /* SLOT_STATE_OFF, etc. */
    int emulator_idx;       /* -1 = OFF */
    int vmc_idx;            /* -1 = none selected */
    int save_idx;           /* Selected save in grid, -1 = none */
    int focus_element;      /* 0=emulator, 1=ps2id, 2=grid */
    int in_dropdown;        /* 0=none, 1=emulator, 2=ps2id */
    int dropdown_sel;

    VmcFile vmc_files[MAX_VMC_FILES];
    int vmc_count;

    VmcSaveEntry saves[MAX_VMC_SAVES];
    int save_count;

    char loaded_vmc_path[512];
    int vmc_loaded;         /* mcio_vmcInit succeeded for this slot */
} MemCardSlot;

/* Global state */
extern EmulatorEntry g_emulators[MAX_EMULATORS];
extern int g_emulator_count;
extern MemCardSlot g_slots[2];
extern int g_active_slot;
extern int g_memcard_action_menu_open;
extern int g_memcard_action_sel;

/* Confirmation dialog state */
extern int g_confirm_dialog_open;
extern int g_confirm_dialog_sel;    /* 0=NO (default), 1=YES */
extern char g_confirm_title[64];
extern char g_confirm_msg[256];
extern void (*g_confirm_yes_callback)(void);
extern void (*g_confirm_no_callback)(void);

/* Core lifecycle */
void memcard_init(void);
void memcard_discover_user_home(void);
void memcard_load_emulators(void);
void memcard_save_emulators(void);

/* Slot operations */
void memcard_set_slot_off(int slot_idx);
void memcard_scan_vmc_files(int slot_idx);
void memcard_load_vmc(int slot_idx);
void memcard_unload_current_vmc(void);
void memcard_refresh_slot(int slot_idx);
void memcard_unmount_all(void);  /* Added: Unmount all VMC files from both slots */

/* Save operations */
int memcard_copy_save_between_slots(int src_slot, int dst_slot);
int memcard_delete_save(int slot_idx);
int memcard_export_save_psu(int slot_idx, const char *usb_base);
int memcard_import_save_psu(int slot_idx, const char *psu_path);

/* VMC operations */
int memcard_backup_vmc_to_usb(int slot_idx, const char *usb_base);
int memcard_import_vmc_from_usb(int slot_idx, const char *usb_path);
int memcard_format_vmc(int slot_idx);

/* Confirmation dialog */
void memcard_show_confirm(const char *title, const char *msg,
                          void (*yes_cb)(void), void (*no_cb)(void));
void memcard_confirm_yes(void);
void memcard_confirm_no(void);

/* Toast notification */
#define TOAST_DURATION 120  /* ~2 seconds at 60fps */
extern char g_toast_msg[128];
extern int g_toast_timer;
void memcard_show_toast(const char *msg);
void memcard_update_toast(void);

/* PSU file picker */
#define MAX_PSU_FILES 32
extern char g_psu_files[MAX_PSU_FILES][256];
extern int g_psu_file_count;
extern int g_psu_picker_open;
extern int g_psu_picker_sel;
void memcard_scan_psu_files(void);

/* Utility */
const char *memcard_get_user_home(void);
int memcard_is_vmc_file_size_valid(size_t sz);
void memcard_extract_disc_id(const char *filename, char *out, size_t out_len);

#endif
