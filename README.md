# PS2 ISO Launcher for PS4

Scans your PS2 ISO collection, reads each disc ID (e.g. `SCUS-97101`), and generates a per-game config on-the-fly from a default template.

## How It Works

1. **Scans** `/data/PS4ROMS/PS2ISO/` for `.iso` / `.bin` files.
2. **Extracts** the disc ID from each ISO header (BOOT2 / cdrom0: pattern).
3. **Shows** an alphabetical list: `Game Name` + `Disc ID`.
4. **On `[X]`:**
   - Reads `/data/PS4ROMS/PS2ISO/default.txt` (or falls back to embedded minimal default).
   - Writes a temp config with the default settings + `--image="..."` + `--ps2-title-id=DISC-ID`.
   - Updates your master `config-emu-ps4.txt` to point to the temp config.
   - Launches your emulator PKG.

## File Structure

```
.
├── main.c
├── Makefile
├── pkg/
│   └── pkg.gp4
├── sce_sys/
│   ├── param.sfo
│   └── icon0.png
└── .github/
    └── workflows/
        └── build.yml
```

## Setup

1. **Change `EMULATOR_TID`** in `main.c` to your installed PS2 emulator PKG Title ID.
2. **Add an icon**: Put a 512×512 PNG named `icon0.png` in `sce_sys/`. (Or delete the line from `pkg.gp4`.)
3. **Push to GitHub** — Actions builds the `.pkg` automatically.
4. **On your PS4**, create `/data/PS4ROMS/PS2ISO/default.txt` with your preferred default settings. If missing, the launcher uses a safe embedded default.

## Default Config

Place this at `/data/PS4ROMS/PS2ISO/default.txt` (customize to your taste):

```
--max-disc-num=1
--ps2-lang=system
--host-osd=0
--host-audio=1
--host-display-mode=normal
--gs-uprender=2x2
--gs-upscale=EdgeSmooth
--path-patches="/data/PS4ROMS/PS2ISO/patches/"
--path-featuredata="/data/PS4ROMS/PS2ISO/feature_data/"
--load-feature-lua=0
--trophy-support=0
```

The launcher appends `--image=` and `--ps2-title-id=` automatically.

## Cover Art (Future)

Cover support can be added later by loading `/data/PS4ROMS/PS2ISO/covers/<DISC-ID>.png`.

## Requirements

- PS4 with GoldHEN / homebrew enabler (for file system access).
- Your PS2 emulator PKG already installed.
- ISO files in `/data/PS4ROMS/PS2ISO/`.
