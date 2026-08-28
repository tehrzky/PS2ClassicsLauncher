# PS2 ISO Launcher for PS4

A PS4 homebrew launcher for PS2 Classics that scans your ISO collection, auto-generates per-game emulator configs, and includes a full-featured **Memory Card Manager** with dual-slot VMC browsing, save copying, and `.PSU` import/export.

![License](https://img.shields.io/badge/license-MIT-blue.svg)

---

## Table of Contents

- [Features](#features)
- [Screenshots](#screenshots)
- [Requirements](#requirements)
- [Installation](#installation)
- [File Structure](#file-structure)
- [Usage](#usage)
  - [Game List](#game-list)
  - [Memory Card Manager](#memory-card-manager)
  - [Settings](#settings)
- [Memory Card Manager Details](#memory-card-manager-details)
  - [Supported Sources](#supported-sources)
  - [Save Actions](#save-actions)
  - [Icon & Title Support](#icon--title-support)
- [Building from Source](#building-from-source)
- [Configuration](#configuration)
- [Credits](#credits)

---

## Features

### Game Launcher
- **Auto-scan** `/data/PS4ROMS/PS2ISO/` for `.iso` / `.bin` files
- **Disc ID extraction** directly from ISO headers (BOOT2 / SYSTEM.CNF parsing)
- **Per-game config generation** — reads `default.txt` template and injects `--image=` and `--ps2-title-id=` automatically
- **Game-specific configs** — drop a `.txt` in `gameconfig/` with a matching Disc ID to override defaults per title
- **Alphabetical game list** with Disc ID display
- **Cover art support** — auto-downloads from remote repositories or loads local PNGs
- **Custom wallpaper** support

### Memory Card Manager (L1 / R1)
- **Dual-slot interface** — compare and copy saves side-by-side
- **Three VMC sources:**
  - **Internal (PS4 savedata)** — mounts encrypted `sdimg_*` databases via `sceFsMountSaveData`
  - **USB Drive** — reads raw `.card` / `.vmc` / `.bin` files from `/mnt/usb0/PS2VMC/`
  - **Internal HDD** — reads VMCs from `/data/PS4ROMS/PS2ISO/VMC/`
- **Save operations:**
  - Copy save between slots (with overwrite confirmation)
  - Delete save
  - Export save as `.PSU` to USB or HDD
  - Import save from `.PSU`
  - Backup full VMC to USB or HDD
  - Format VMC
- **PS2 icon rendering** — decodes 3D TIM2 textures and 16×16 paletted icons
- **S-JIS title parsing** — reads `icon.sys` titles (e.g., `"Save 1 - LV 45 - 30:15"`)
- **Shared mount safety** — same `sdimg_*` file is mounted only once even when both slots use it

### Settings Menu (Triangle)
- Auto-download covers toggle
- Auto-download GameIndex toggle
- Cover type selection
- Scraper base URL selection (3 mirrors)
- Font customization (body & title fonts)
- Force re-download GameIndex / all covers

### Technical
- **Sandbox bypass** via `libjbc` for full filesystem access
- **Priv libs loading** for `sceFsMountSaveData` (internal save mounting)
- **GitHub Actions CI** — auto-builds `.pkg` on every push

---

## Screenshots

*(Add your own screenshots here)*

---

## Requirements

- PS4 on firmware **9.00 or lower** (or any firmware with a compatible jailbreak)
- An installed PS2 Classics emulator PKG (e.g., `PCSX20042`)
- PS2 ISO files placed in `/data/PS4ROMS/PS2ISO/`

---

## Installation

1. Download the latest `.pkg` from [GitHub Actions](https://github.com/tehrzky/PS2ClassicsLauncher/actions) or build it yourself.
2. Install the `.pkg` on your PS4 via Debug Settings or a package installer.
3. Create the working directory on your PS4:
   ```
   /data/PS4ROMS/PS2ISO/
   ```
4. Place your PS2 `.iso` or `.bin` files in that folder.
5. *(Optional)* Create `/data/PS4ROMS/PS2ISO/default.txt` with your preferred emulator settings (see [Configuration](#configuration)).
6. Launch **PS2 ISO Launcher** from the PS4 home screen.

---

## File Structure

```
/data/PS4ROMS/PS2ISO/
├── default.txt              # Default emulator config template
├── config/
│   └── config-emu-ex.txt    # Master config (auto-generated)
├── gameconfig/
│   └── SLUS-12345.txt       # Per-game override configs (optional)
├── gameconfig/
│   └── Ace Combat 04 - Shattered Skies (USA).txt
├── patches/                 # Emulator patches
├── feature_data/            # Emulator feature data
├── covers/
│   └── SLUS-12345.png       # Local cover art (optional)
├── assets/
│   └── fonts/               # Custom TTF fonts (optional)
├── VMC/                     # Internal HDD VMC folder (auto-created)
│   ├── VMC0.card
│   └── VMC1.card
└── config/
    └── GameIndex.yaml       # Game title database (auto-downloaded)

/mnt/usb0/
├── PS2VMC/                  # USB VMC folder
│   ├── VMC0.card
│   └── VMC1.card
└── PS2SAVES/                # .PSU import folder
    └── save.psu
```

---

## Usage

### Game List

| Button | Action |
|--------|--------|
| `D-Pad Up/Down` | Navigate game list |
| `L2 + D-Pad` | Fast scroll (5 items) |
| `[X]` | Launch selected game |
| `[Triangle]` | Open Settings |
| `[L1] / [R1]` | Open Memory Card Manager |
| `[Circle]` | Exit app |

### Memory Card Manager

| Button | Action |
|--------|--------|
| `D-Pad` | Navigate emulators / PS2 IDs / save grid |
| `Left/Right` | Switch between Slot 1 and Slot 2 |
| `[X]` | Select / open dropdown |
| `[Triangle]` | Open Action Menu |
| `[Circle]` | Back / close menu |

### Settings

| Button | Action |
|--------|--------|
| `D-Pad Up/Down` | Navigate settings |
| `D-Pad Left/Right` | Change value |
| `[X]` | Activate / cycle font |
| `[Circle]` | Save and exit settings |

---

## Memory Card Manager Details

### Supported Sources

| Source | Path | Description |
|--------|------|-------------|
| **Internal** | `/user/home/<userid>/savedata/<emulator>/` | PS4 encrypted save databases (`sdimg_*`) |
| **USB** | `/mnt/usb0/PS2VMC/` | Raw VMC image files |
| **HDD** | `/data/PS4ROMS/PS2ISO/VMC/` | Internal VMC image files |

### Save Actions

When a save is selected in the grid, press `[Triangle]` to open the Action Menu:

1. **Copy Save to Other Slot** — copies the selected save to the opposite slot's VMC
2. **Delete Save** — permanently deletes the selected save (with confirmation)
3. **Export Save as .PSU to USB** — exports to `/mnt/usb0/PS2SAVES/`
4. **Export Save as .PSU to HDD** — exports to `/data/PS4ROMS/PS2ISO/VMC/`
5. **Import Save from .PSU** — imports a `.psu` file from `/mnt/usb0/PS2SAVES/`
6. **Backup Full VMC to USB** — copies the entire VMC file to USB
7. **Backup Full VMC to HDD** — copies the entire VMC file to HDD
8. **Format VMC** — wipes the VMC clean (with confirmation)

### Icon & Title Support

The launcher parses PS2 save metadata directly:

- **Icons**: Supports both 3D TIM2 textures (128×128) and standard 16×16 paletted icons
- **Titles**: Reads `icon.sys` S-JIS title strings and converts them to ASCII
- **Fallback**: If no `icon.sys` is present, shows the raw directory name (e.g., `BASLUS-21615WA121000`)

Example titles you might see:
```
WildARMs5 No.1-Lv042 029:01:12
VP2-01-000:47-Underground Path
TOA  -No.1 Lv2
```

---

## Building from Source

### Prerequisites

- Linux or WSL
- [OpenOrbis PS4 Toolchain](https://github.com/OpenOrbis/OpenOrbis-PS4-Toolchain)
- LLVM/Clang 12

### Manual Build

```bash
export OO_PS4_TOOLCHAIN=/path/to/OpenOrbis/PS4Toolchain
make
```

### GitHub Actions (Recommended)

Just push to `main` or `master`. The workflow automatically:
1. Downloads the OpenOrbis toolchain
2. Installs `libjbc`
3. Builds the `.pkg`
4. Uploads it as an artifact

---

## Configuration

### `default.txt`

Place at `/data/PS4ROMS/PS2ISO/default.txt`. This is the base template for all games.

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

The launcher automatically appends:
```
--image="/data/PS4ROMS/PS2ISO/YourGame.iso"
--ps2-title-id=SLUS-12345
```

### Per-Game Overrides

Create a `.txt` file in `gameconfig/` with the Disc ID in the header:

```
#  Disc ID:     SLUS-21214
--gs-uprender=1x1
--gs-upscale=None
```

The launcher will use this config instead of `default.txt` for matching games.

### `emulators.txt`

The launcher auto-discovers emulators from your PS4 system. You can also create `/data/PS4ROMS/PS2ISO/config/emulators.txt` to add custom entries:

```
PCSX20042=Default Emulator
PCSX20001=Alternate Emulator
```

Virtual entries `USB` and `HDD` are appended automatically.

---

## Credits

- **tehrzky** — Main developer
- **bucanero** — Apollo-PS4 reference, `libjbc`, `oosdk_libraries`
- **OpenOrbis Team** — PS4 toolchain
- **illusion0001** — OpenOrbis toolchain release
- **xlenore** — PS2 cover art repository
- **root670** — CheatDevicePS2 (`.MAX`/`.CBS`/`.XPS` format reference)

---

## License

MIT License — see [LICENSE](LICENSE) for details.
