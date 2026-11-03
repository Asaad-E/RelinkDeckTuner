# Relink Deck Tuner {VERSION}

Performance package for **Granblue Fantasy: Relink v2.0.5** on Steam Deck (graphics preset Standard, 1280x720). Goal: a steady 30 FPS in dialogues and towns, with the image almost identical to the original.

> **v2.0.5 only, personal use.** `game/` contains modified game files (`data.i` and `data/…`). If the game updates, this `data.i` no longer matches; both `.asi` plugins check the game version and switch themselves off, so the game will not break.

## Contents

| Path | What it is |
|---|---|
| `game/data.i`, `game/data/model/…` | Vegetation (606 models) uses its second level of detail even up close. The leaves keep their wind animation. |
| `game/data/layout/timeline/…` | Cutscene lights without per-frame dynamic shadows. |
| `game/data/filter/pc00|pd00/…` | Folca and Seedhollow: simple shadows and reduced fog; no volumetric light in the church. |
| `game/data/system/NPC/…` | NPC density at 80 % in Folca and Seedhollow. |
| `game/scripts/RelinkDeckTuner.asi` + `.ini` | Lowers only the 3D resolution during dialogues (UI and text stay sharp) and upscales it with **AMD FSR 1.0**. F9/F10 hotkeys. Logs FPS and stutter. |
| `game/winmm.dll` + `game/scripts/GBFRelinkFix.*` | GBFRelinkFix 1.1.5 by Lyall (ASI loader, intro skip, shadows 128, distant LOD 0.8). |
| `install.sh` / `uninstall.sh` | Steam Deck / Linux: install with backup and SHA-256 check; restore the factory game. |
| `install.bat` / `uninstall.bat` | The same for Windows. |
| `SHA256SUMS.txt` | Checksums of every file in `game/`. |
| `licenses/` | MIT licenses of GBFRelinkFix and AMD FidelityFX FSR 1.0. |

## Install

Close the game first. The installer keeps a copy of everything it replaces in `<game>/relinkdecktuner_backup_<date>/`, saves the factory `data.i` as `data.i.orig`, copies `game/` and verifies every file.

**Steam Deck (desktop mode, Konsole):**
```
bash install.sh "/home/deck/path/to/Granblue Fantasy Relink"
```
First time only: Proton must load the game's own `winmm.dll`. In Steam, open the game's Properties and set the launch options to:
```
WINEDLLOVERRIDES="winmm=n,b" %command%
```

**Windows:** double-click `install.bat` and enter the game folder when asked, or run:
```
install.bat "C:\Games\Granblue Fantasy Relink"
```

Check: the company logos are skipped at startup, and `scripts/RelinkDeckTuner.log` contains `temporal upsampler shader replaced by FSR1 EASU`.

## Settings (`scripts/RelinkDeckTuner.ini`)

The file is re-read every second, so it can be edited while playing. Only `Upscaler` is read once at startup.

| Key | Value | Meaning |
|---|---|---|
| `Mode` | `1` | 1 = `Scale` during dialogues and `BaseScale` elsewhere; 2 = `Scale` always; 0 = off (logging only). |
| `Scale` | `0.8` | 3D resolution scale per axis during dialogues (0.8 = 1024x576 upscaled to 1280x720). |
| `BaseScale` | `1.0` | 3D resolution scale outside dialogues (1.0 = native). |
| `HotkeyBaseScale` / `HotkeyDialogueScale` | `120` (F9) / `121` (F10) | Each press moves to the next step (1.0 → 0.95 → 0.9 → 0.85 → 0.8 → 0.75 → 1.0) and saves it to the `.ini`. On the Deck, map them to back buttons in Steam's controller settings. |
| `Fields` | `1` | 1 = scale the 3D scene only (sharp UI). 3 scales the UI too (blurry); not recommended. |
| `Upscaler` | `1` | 1 = FSR 1.0; 2 = bicubic (softer); 0 = the game's own upsampler (horizontal bands). |
| `DialogueLodBias` | `1.0` | Scenery LOD factor during dialogues. 0.5 gains a few FPS but distant objects get visibly simpler. |
| `ExitDelayMs` | `700` | How long without cutscene-camera updates before a dialogue is considered over. |
| `LogPeriodMs` | `2000` | Interval of the status line in `RelinkDeckTuner.log`. |

GBFRelinkFix settings are in `scripts/GBFRelinkFix.ini`.

## Log

`scripts/RelinkDeckTuner.log` writes a line every `LogPeriodMs` with:
- `fps`: whole game;
- `maxFrameMs`: longest frame in the interval;
- `slow40`: frames longer than 40 ms (visible stutter at 30 FPS);
- `dialogueFps`, `dialogue`: dialogue state;
- the 3D render size in use.

## Uninstall

```
bash uninstall.sh "/home/deck/path/to/Granblue Fantasy Relink"
```
or `uninstall.bat` on Windows. Both restore `data.i.orig` and remove the mod files. `data/sound` and `data/ui` belong to the game and are kept.

## Measured results

| | Factory game | With this package |
|---|---|---|
| Dialogues with vegetation behind, Deck | 22-27 FPS | 30 FPS |
| Verbose Man dialogue (Seedhollow), test PC | 25 FPS | 38 FPS |
| Towns, Deck | Zinkenstill: 30 with 28-38 ms frame times | Seedhollow: steady 30 (33.5 ms; almost no frames over 40 ms) |

## Credits

- GBFRelinkFix: Lyall (codeberg.org/Lyall/GBFRelinkFix), MIT license.
- FSR 1.0: AMD FidelityFX, MIT license.
- Data formats: Nenkai's GBFRDataTools (FlatBuffers schemas).
