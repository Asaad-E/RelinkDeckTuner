# Relink Deck Tuner

Performance mod for **Granblue Fantasy: Relink** targeting a locked **30 FPS** on **Steam Deck** (Standard preset, 1280x720) and low-power PCs during heavy dialogues and towns, while keeping UI/text sharp and image quality close to native.

## Compatibility

- **Game v2.0.5 (Steam Deck prebuilt package):** Preserves the verified v2.0.5 fast-path and release builder (`tools/make_deck_pkg.sh` + `tools/make_release.sh`), producing an installable package with `install.sh` / `install.bat`, backup, and SHA-256 verification.
- **Game >= 2.0.5 (e.g., Ver. 2.0.6+):**
  - `RelinkDeckTuner.asi` automatically falls back from v2.0.5 RVAs to unique `.text` AOB pattern scanning and DXBC `RDEF` constant-buffer detection if the executable changes across updates.
  - `tools/patch_any_version.py` builds the data mods directly against your own game's `data.i` / `data.N` archives (using `tools/known_paths.txt`), supporting either direct installation or Reloaded-II mod folder output without redistributing game `data.i` files.

> Built against Windows x64 (`granblue_fantasy_relink.exe`) and tested under both Windows and SteamOS Proton. Works alongside **GBFRelinkFix** (`winmm.dll` ASI loader).

## Features

### Native ASI Plugin (`asi/RelinkDeckTuner.cpp`)
- **Dialogue-only 3D resolution scaling (`Mode = 1`):** Detects dialogue/cutscene cameras and scales only the 3D scene resolution (default `Scale = 0.8`), leaving 2D HUD, subtitles, and menus at native resolution (`BaseScale = 1.0`).
- **AMD FidelityFX FSR 1.0 (EASU + RCAS) & Bicubic upscaling:** Replaces the game's temporal upsampler (`TUP`), which produces horizontal banding at non-integer scales.
- **Post-DoF upscale fix (`DofFix = 1`):** Fixes the unfiltered point-stretch in the game's depth-of-field pass when running below native 3D resolution.
- **Dynamic Dialogue LOD:**
  - `DialogueVegetationLod`: Forces lighter LOD levels on instanced vegetation during dialogues (default `0.05` — `+7` to `+12` FPS in foliage-heavy scenes with near-identical visuals).
  - `DialogueLodBias`: Adjusts scenery LOD bias during dialogues (default `1.0`).
- **In-game Graphics Options & Hotkeys:** Reuses five unused rows at the bottom of **Options > Graphics** (`Resolution Scale`, `Dialogue Resolution`, `Upscaler`, `Dialogue Vegetation`, `Dialogue Scenery Detail`) with Apply persistence to `scripts/RelinkDeckTuner.ini`. Supports live hotkeys (`F9` / `F10` by default) to cycle scales.
- **Live Telemetry:** Logs FPS, max frame time, `>40 ms` stutter frames, and active render size to `scripts/RelinkDeckTuner.log`.

### Data Mods (`tools/build_mod.py`, `tools/build_foliage.py`)
- **Vegetation LOD (`level 1`):** Shifts 606 vegetation models to LOD1 even at close range while preserving wind animation on leaves.
- **Dialogue Lighting:** Disables expensive per-frame dynamic shadows on cutscene lights (`data/layout/timeline/...`).
- **Town Optimizations (Folca & Seedhollow):** Enables simple shadows, reduces fog cost, disables church volumetric lighting, and sets town NPC density to `0.8`.
- **In-game Menu Strings:** Patches `option_item.tbl` and localization tables for the five Relink Deck Tuner settings.

## Measured Results

| Scenario | Factory (v2.0.5) | With Relink Deck Tuner |
|---|---|---|
| Foliage-heavy dialogues (Steam Deck, 720p Standard) | 22-27 FPS | **30 FPS** |
| Verbose Man dialogue in Seedhollow (iGPU test PC) | 25 FPS | **38 FPS** |
| Towns (Steam Deck) | Zinkenstill 30 FPS (28-38 ms frametimes) | **Seedhollow steady 30 FPS** (33.5 ms, near-zero >40 ms frames) |

Detailed profiling, RenderDoc breakdowns, and disassembly notes are documented in [report.md](report.md).

## Installation

### Option A: Prebuilt Steam Deck / v2.0.5 Release Package
1. Place the clean v2.0.5 `data.i` in `backup/data.i` and third-party ASI loader/base files (`winmm.dll`, `scripts/GBFRelinkFix.*`) in `deck_pkg/`.
2. Build the package and release directory:
   ```bash
   bash tools/make_deck_pkg.sh
   bash tools/make_release.sh v0.15
   ```
3. Run `install.sh` (Steam Deck / Linux) or `install.bat` (Windows) inside `release/RelinkDeckTuner-v0.15/`:
   ```bash
   bash install.sh "/path/to/Granblue Fantasy Relink"
   ```
4. On SteamOS / Linux, set Steam Launch Options for the game:
   ```text
   WINEDLLOVERRIDES="winmm=n,b" %command%
   ```

### Option B: Granblue Fantasy: Relink >= 2.0.5 (e.g. Ver. 2.0.6+)
1. Build `RelinkDeckTuner.asi` (see [Building from Source](#building-from-source)).
2. Run the universal patcher against your installed game directory (Python 3.10+ required):
   ```bash
   # Direct install into the game folder (backs up data.i to data.i.orig)
   python -I tools/patch_any_version.py "C:\Path\To\Granblue Fantasy Relink" --mode direct

   # Or export a Reloaded-II mod folder
   python -I tools/patch_any_version.py "C:\Path\To\Granblue Fantasy Relink" --mode reloaded --out "C:\Path\To\Reloaded-II\Mods\RelinkDeckTuner"
   ```
   *(If targeting an ASI-only setup without menu strings, pass `--skip-options`; `.asi` reads settings from `scripts/RelinkDeckTuner.ini`.)*

## Configuration (`scripts/RelinkDeckTuner.ini`)

`scripts/RelinkDeckTuner.ini` is reloaded every second while running (only `Upscaler` requires a game restart).

| Key | Default | Description |
|---|---|---|
| `Mode` | `1` | `0` = off (logging only), `1` = `Scale` in dialogues & `BaseScale` elsewhere, `2` = `Scale` always |
| `Scale` | `0.8` | 3D resolution scale per axis during dialogues (`0.4` - `1.0`) |
| `BaseScale` | `1.0` | 3D resolution scale outside dialogues when `Mode = 1` |
| `Fields` | `1` | `1` = scale 3D scene only (sharp UI), `3` = scale UI too |
| `Upscaler` | `1` | `1` = FSR 1.0 (EASU + RCAS), `3` = Bicubic + RCAS, `2` = Bicubic, `0` = Game TUP |
| `DofFix` | `1` | Replaces unfiltered point-stretch in the depth-of-field pass |
| `DialogueVegetationLod` | `0.05` | Vegetation LOD factor in dialogues (`0.0` - `1.0`) |
| `DialogueLodBias` | `1.0` | Scenery LOD bias in dialogues (`0.1` - `1.0`) |
| `HotkeyBaseScale` / `HotkeyDialogueScale` | `120` / `121` | Virtual-key codes (`F9` / `F10`) to cycle resolution scales |
| `ExitDelayMs` | `700` | Grace period before ending dialogue state after camera updates stop |
| `LogPeriodMs` | `2000` | Telemetry interval in `scripts/RelinkDeckTuner.log` |

## Building from Source

Requires **Visual Studio 2022+** (MSVC x64 C++ tools), **Windows 10/11 SDK** (`fxc.exe`), and **Python 3**.

```bat
cd asi
build.bat
```

`build.bat` locates Visual Studio via `vswhere.exe`, compiles the HLSL shaders (`fs_vs.hlsl`, `upscale_ps.hlsl`, `fsr_ps.hlsl`, `easu_ps.hlsl`, `rcas_ps.hlsl`), embeds them via `embed_shaders.py`, and links `asi\build\RelinkDeckTuner.asi`.

## Repository Layout

- `asi/` — C++17 D3D11/IAT hook plugin source, HLSL upscalers, default `.ini`, shader embedder, and standalone tests.
- `tools/` — Archive parser/patcher (`gbfr_archive.py`, `patch_index.py`, `patch_any_version.py`), data mod builders (`build_mod.py`, `build_foliage.py`), RenderDoc/disassembly research scripts, and Steam Deck release packagers (`make_deck_pkg.sh`, `make_release.sh`).
- `release_template/` — Cross-platform installer/uninstaller scripts and end-user README for release archives.
- `notes/` — RenderDoc captures summaries, benchmark raw logs, and pattern references.
- `GUIDE.md`, `proposal.md`, and `report.md` — Detailed user guide, design proposal, and profiling report.

## Credits & Third-Party Licenses

- **Relink Deck Tuner** code and scripts are licensed under the [MIT License](LICENSE).
- **AMD FidelityFX FSR 1.0** (`EASU` / `RCAS`) — MIT License (`asi/third_party/fsr1/`).
- **GBFRelinkFix** by Lyall — MIT License (`release_template/GBFRelinkFix_LICENSE.md`).
- **GBFRDataTools** by Nenkai — used as format/schema reference for `.i`, `.msg`, `.tbl`, and `.minfo` files.
