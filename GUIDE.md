# Relink Deck Tuner: User Guide

Performance mod for **Granblue Fantasy: Relink** (v2.0.5+ on Steam Deck and low-power PCs, with graphics preset on Standard). Goal: a steady **30 FPS** in dialogues and towns without degrading image quality. Technical details and all benchmarks are in [report.md](report.md) (§7).

## What Each Component Does

| Component | Files | Effect |
|---|---|---|
| **Foliage LOD1** | `data.i` + `data/model/.../*.minfo` (606 models) | Trees, bushes, and grass use their second LOD level even up close. This is the biggest win in foliage-heavy dialogues (a single bush previously took 31% of the frame). Leaves retain wind animation. Applies game-wide. |
| **RelinkDeckTuner.asi** | `scripts/RelinkDeckTuner.asi` + `.ini` | During dialogues, lowers **only 3D render resolution** (UI and text remain native/sharp) and restores it afterward. Replaces the engine's temporal upsampler (which caused horizontal banding) with **AMD FidelityFX FSR 1.0**, and fixes unfiltered depth-of-field stretching. |
| **Data mods** | `data/layout/timeline/*_light.scene.msg`, `data/filter/pc00|pd00/*`, `data/system/NPC/*` | Disables per-frame dynamic shadows on cutscene point lights, enables simple shadows and reduced-res fog in Folca/Seedhollow, disables church volumetric light, and sets town NPC density to 80%. Modest impact. |
| **GBFRelinkFix 1.1.5** (by Lyall) | `winmm.dll` (ASI loader) + `scripts/GBFRelinkFix.*` | Skips startup logos, lowers shadow resolution to 128 (half of Standard's 256), and sets distant LOD multiplier to 0.8. |

## `scripts/RelinkDeckTuner.ini`

Re-read every second while running, so changes apply live. Only `Upscaler` is read once at game startup.

| Key | Recommended Value | Behavior |
|---|---|---|
| `Mode` | `1` | `0` = off (logging only); `1` = `Scale` during dialogues and `BaseScale` elsewhere; `2` = `Scale` always. |
| `BaseScale` | `1.0` | 3D resolution scale outside dialogues (also upscaled with FSR 1.0). `1.0` = native. |
| `Scale` | `0.8` | 3D resolution scale per axis while active (`0.8` = 1024x576 -> 1280x720). `0.85` is slightly sharper with less headroom. |
| `HotkeyBaseScale` / `HotkeyDialogueScale` | `120` (F9) / `121` (F10) | Cycles through presets (`1.0 -> 0.95 -> 0.9 -> 0.85 -> 0.8 -> 0.75 -> 1.0`) and saves back to `.ini`. On Steam Deck, bind them to back grip buttons via Steam Input. |
| `Fields` | `1` | `1` = scale 3D scene only (sharp UI). Avoid `3`. |
| `Upscaler` | `1` | `1` = FSR 1.0 (EASU + RCAS); `3` = Bicubic + RCAS; `2` = Bicubic; `0` = engine TUP (causes banding). |
| `DofFix` | `1` | `1` = fixes unfiltered point-stretch in the game's post-DoF resize using the active upscaler (`0` disables). |
| `DialogueVegetationLod` | `0.05` | Extra LOD factor for vegetation during dialogues (`0.0` - `1.0`). |
| `DialogueLodBias` | `1.0` | Scenery LOD factor during dialogues. `0.5` gains a few FPS, but distant props visibly simplify. |
| `ExitDelayMs` | `700` | Grace period (ms) without cutscene camera updates before ending dialogue state. |
| `LogPeriodMs` | `2000` | Status interval (ms) written to `RelinkDeckTuner.log` (`fps`, `maxFrameMs`, `slow40`, `dialogueFps`). |

## Building & Installing

### v2.0.5 Prebuilt Package (Steam Deck / PC)

Requires a clean v2.0.5 `data.i` in `backup/data.i` and base loader files in `deck_pkg/`.

```bash
bash tools/make_deck_pkg.sh
bash tools/make_release.sh v0.15
```

This generates `release/RelinkDeckTuner_v0.15/` containing `install.sh`, `install.bat`, `uninstall.sh`, `uninstall.bat`, and `SHA256SUMS.txt`. Proton requires `WINEDLLOVERRIDES="winmm=n,b" %command%` in Steam Launch Options (or configured in `winecfg`).

### Versions >= 2.0.5 (e.g. Ver. 2.0.6+)

Patches the target game directory's own `data.i` / `data.N` archives directly (or outputs a Reloaded-II mod folder):

```bash
python -I tools/patch_any_version.py "<path_to_game>" --mode direct
```

## Restoring Factory Files

Use `uninstall.sh` / `uninstall.bat` from the release package, or with the game closed restore `data.i.orig` over `data.i` and delete `winmm.dll`, `scripts/`, and the modified folders inside `data/` (`filter`, `layout`, `model`, `system`).

## Measuring Performance

- **Steam Deck:** SteamOS performance overlay level 4. Additionally, `scripts/RelinkDeckTuner.log` records `fps` (overall), `maxFrameMs`, `slow40`, and `dialogueFps` (during dialogues) every `LogPeriodMs`.
