# Granblue Fantasy: Relink — Steam Deck Performance Analysis (Dialogues & Towns)

Target binary analyzed: `granblue_fantasy_relink.exe` v2.0.5 (file `v2.0.5.xxh128`, 2026-08-28), index `data.i` with codename `relink`, 12 `data.N` archive files.
Dates: 2026-11-02 (reverse engineering) and 2026-11-03 (RenderDoc profiling, FSR1, benchmarks).

Confidence tags:
- **[VERIFIED]**: observed directly in the executable or game archives via scripts in `tools/`. Reproducible.
- **[SOURCE]**: from third-party documentation or source code cross-checked against the binary.
- **[HYPOTHESIS]**: reasonable inference pending in-game confirmation.

---

## 0. Final Summary (2026-11-03, Measured)

| Question | Data-Backed Answer |
|---|---|
| What causes dialogue FPS drops? | **Near-camera geometry shading**, especially alpha-tested foliage. In the Verbose Man dialogue, a single bush (`bg012c`, 9,366 indices x 19 instances) accounts for 31% of frame time, and foliage totals 49%. In Lyria's dialogue, foliage is 27% and opaque scenery is 28% (RenderDoc, §7.9 & §7.11). Post-processing takes ~1 ms; shadows and distant LOD ~0 ms (§7.8). |
| Does the game raise render resolution in dialogues? | No (§3.2, confirmed in captures: constant 1280x720). |
| What works? | (1) **Foliage LOD1** data mod (`build_foliage.py`): -15% frame time in dialogues, also helps while walking. (2) **Dialogue-only 3D scaling** (`RelinkDeckTuner.asi`, `Fields = 1`): keeps UI sharp with minimal flicker. (3) **FSR 1.0** replacing the engine's temporal upsampler (which caused horizontal banding): same or lower GPU cost with clean edges (§7.12). |
| Results | Test PC (iGPU): Lyria 18 -> 25 FPS, Verbose Man 25 -> 38 FPS with the final package (§7.13-7.15). Steam Deck: Lyria reaches 30 FPS with LOD1 + 0.85 scale. |
| What didn't work? | Unlocking hidden menu options (PC build lacks load/apply handlers for them), disabling post-processing effects (~1 ms gain for broken visuals), distant LOD / shadows alone (don't touch near foliage), and `DialogueLodBias` (small gain; instanced foliage bypasses that loop). |
| Remaining status | Nothing critical. On Steam Deck under Proton: FSR 1.0 works cleanly (confirmed in log), dialogues hold 30 FPS, and Seedhollow holds steady 30 FPS without major stutter (§7.16). Optional: RCAS sharpening (default enabled) for 0.8 scale clarity. |

User guide: [GUIDE.md](GUIDE.md).

---

## 1. Executive Summary

| Question | Answer |
|---|---|
| Does internal render resolution increase during dialogues? | **Most likely no.** The two render resolution pairs in globals are only written during initialization and inside the graphics preset applicator. That function runs every frame in the main loop and always reads the selected Options preset row. Cutscene timeline data contains no resolution parameter `[VERIFIED]`. |
| Then why do dialogues drop FPS? | Dialogue scenes enable per-event features rare in normal gameplay: **point lights with dynamic shadows updated every frame**, **character event lights**, **per-shot animated DoF**, and hundreds of animated light tracks, while the camera moves close so characters sit at LOD0 `[VERIFIED in data; exact cost was HYPOTHESIS until §7.9]`. |
| Why do towns drop FPS? | Both hub towns (`pc00`, `pd00`) contain **5-20x more placed NPCs** than combat zones (298 and 236 vs. <= 57), thousands of ambient area lights, and a 2 MB `foliage` layout. Their zone filters also have different quality bitmasks for simple shadows and downscaled fog `[VERIFIED in data]`. |
| How can costs be reduced? | Two paths: **(A) Data mod** (disable per-frame dynamic shadows on cutscene lights, limit church volumetric light, enable simple shadows / downscaled fog in towns, shift foliage LODs). **(B) Code mod (ASI)**: scale 3D render resolution dynamically in dialogues with FSR 1.0, fix post-DoF point resize, and control vegetation/scenery LOD. |
| Does the game have hidden options? | Yes. `option_item.tbl` contains Shadow Quality, Effects, Character Models, Background Models, Decal, and DoF, disabled on PC (§5.3). "Limit Number of Point Lights" / "Limit Shadow Cache" do **not** exist: those UI strings are orphaned. |
| What works out of the box without custom code? | GBFRelinkFix (Codeberg, Aug 2026) with `Level of Detail Multiplier < 1` and lowered `Shadow Quality Resolution` — its 8 AOB patterns match uniquely in v2.0.5 (§5.1). |

---

## 2. Methodology & Tools (`tools/`)

| Tool | Purpose |
|---|---|
| `gbfr_archive.py` | Custom `data.i`/`data.N` reader: parses `IndexFile` FlatBuffer, hashes lowercase paths with XXH64, decompresses LZ4 blocks. Verified against official XXH64 test vectors. Resolves **404,180 of 408,165** files using GBFRDataTools' path list. |
| `patscan.py` | Scans `.text` for IDA-style byte patterns. |
| `disasm.py` | Disassembles executable address ranges using `capstone`. |
| `xref.py` | Finds RIP-relative reads/writes to global variables. |
| `callers.py` | Finds `call`/`jmp rel32` callers targeting a function. |
| `evtb_filters.py`, `evtb_bools.py` | Parses cutscene `master.evtb` FlatBuffers (GBFRDataTools schema). |
| `scene_stats.py`, `layout_aggregate.py` | Counts scene objects (lights, NPCs, etc.) in `layout/**/*.scene.msg` (MessagePack). |
| `option_table.py` | Decodes `option_item.tbl` and resolves localized strings via the game's XXHash32. |
| `ccomp_dump.py` | Flattens all 173 `filter/**/*.ccomp.msg` zone post-processing & lighting files. |

Intermediate dumps are written to `dumps/` (gitignored, regenerated by scripts).

Manual filtering over ~3,000 performance-related strings in the executable identified candidates confirmed in game archives and runtime behavior (such as `useSimpleShadowQualityFlag_`, `useFogDownScaleQualityFlag_`, and `isDistanceCulling_`).

Note on GBFRDataTools: its source code was used purely as a format/schema specification; all archive tools here are self-contained Python scripts.

---

## 3. Dialogues & Cutscenes

### 3.1 Event Types `[VERIFIED]`

The executable constructs event identifiers with `cw%04x`, `ct%04x`, `ci%04x`, `cm%04x`, `cn%04x`, plus `ft_pl%04x`, `vn_pl%04x`, and `p%03x_ev%04x`. In `system/event/<type>/<id>/master.evtb`:

| Type | Events | Shots (cuts) | Notes |
|---|---|---|---|
| `ci` | 119 | 445 | Major cutscenes (some `master.evtb` exceed 10 MB). |
| `ct` | 62 | 1,349 | In-engine voiced cutscenes with heavy animation (17,228 `.mot`). |
| `cw` | 98 | 2,189 | Dialogue scenes with many short camera cuts. |
| `cm` / `cn` | 7 / 4 | 1 each | Trivial. |

Script lines (`system/table/scenario/*.msg`) break down as: VT 8,913, FT 6,632, WD 2,258, CT 2,046, SB 1,395, AC 769, IN 760, CN 121, CM 98. Most dialogue (VT/FT) does **not** rely on an `evtb`, instead running through field conversation logic.

### 3.2 Internal Resolution Does Not Change in Cutscenes `[VERIFIED]`

1. `.data` holds a **6-row graphics preset table** (v2.0.5 RVA `0x6b82460`, 6 rows x `0x4C` bytes) and a global preset index (RVA `0x7034940`, values 0..5 where 5 is "Custom"). Each row stores 4 shadow resolutions (`+0x4..+0x10`, default 2048) and two resolution pairs (`+0x24..+0x30`, default 1920x1080 and 1920x1080).
2. Function `0x6bbaa0` copies `row[+0x24..+0x30]` into 4 globals (`0x6b822e0`, `...e4`, `...e8`, `...ec`) using `xchg`, recreating render targets when changed.
3. `xref.py` shows **those 4 globals are only written in two places**: `0x1c2034` (init) and `0x6bbadf` (inside `0x6bbaa0`).
4. `callers.py`: `0x6bbaa0` is only called from `0x1b0048` (the **Windows main message loop**, `PeekMessage` -> `Translate/Dispatch`, comparing against `0x12` = `WM_QUIT`) and `0x1e7c1c` (startup).
5. The preset index is only written at `0x1c1f42` (init, value 5) and `0x6bbbb2` (when applying settings).
6. None of the 31 cutscene filter track types (`TimelineFilterData`) control render resolution. `MultiFrameSuperResolutionParameter` only exists in zone filters (`filter/`), never in `evtb`.

**Conclusion:** Render resolution comes strictly from the active Options preset, and no event code increases it during dialogues. Any perceived extra sharpness in dialogues is due to close-up camera framing and DoF.

### 3.3 What Actually Raises Dialogue Cost `[VERIFIED in data]`

**a) Dedicated per-event point lights with dynamic shadows.**
`layout/timeline/` contains 222 cutscene scene layers (`ci`/`ct`/`cw` `####_light|cloud|wind|localibl.scene.msg`), containing **1,412 point lights with `bCastDynamicShadow_ = 1`** (recalculated every frame via `castDynamicShadowFrequency_ = 1`) across 58 layers. 64 are active at scene load across 33 events, and the rest toggle on via timeline keys (e.g., `ct0020` (4), `ct1050` (4), `ct1110` (4), `ct8040` (4), `ct9040` (5), `cid326` (5)). By contrast, `pd00` has 0 and `pc00` has 5. Point light shadows render up to 6 cubemap faces (`PointLightShadowCastDualParaboloid` / `PointLightShadowCastPerspective`).
There are also **38 lights with `useEventCharaLight_ = true`**.

**b) Per-shot animated lights.** `ct` `evtb` files contain **1,129 `DeferredPointLight` tracks** and 798 `AmbientAreaLight` tracks across 1,349 shots; `cw` has 205 and 218. In `ct`, 761 of 1,167 keys set `DeferredPointLight.field_5 = true`.

**c) Per-shot DoF.** `cw` events have **2,161 `Dof` tracks across 2,189 shots** (nearly one per cut), with `Dof.field_12 = true` in 1,748 of 2,165 keys. Zone filters also enable `DofParameter` in 168 of 173 zones (`blurSizeMax_ = 16`, `apertureShape_ = 1`).

**d) Event volumetric lighting and clouds.** `*_cloud.scene.msg` layers reach up to 6.8 MB (`ct0030_cloud`), and `VolumetricLight` tracks appear across `ct`/`cw`.

**e) Close-up characters.** Close camera forces LOD0 characters, hair, and cloth physics (`isUseClothPysics_`).

---

## 4. Towns

### 4.1 Zone Identification `[VERIFIED]`

- `pc00` = **"redtown"** (with church; Folca). Has `system/NPC/c00_npcConfig.msg`.
- `pd00` = **"yellowtown"**: backstreet, bar, frontcastle, port, square, street1/2 (Seedhollow). Has `system/NPC/d00_npcConfig.msg`.
- `pd01`/`pd02`/`pd04`: annex areas (castle, "7st square/backstreet").

### 4.2 NPC Density `[VERIFIED]`

In `layout/<zone>/placement*.scene.msg`, `PlacementInfo` entries with `memberType_ = 7` represent NPCs. `values_[0]` holds the NPC ID (33/34 IDs in `pc00` and 34/36 in `pd00` match `np/<id>` folders).

| Zone | NPC Placements | Unique IDs |
|---|---|---|
| **pc00** | **298** | 34 |
| **pd00** | **236** | 36 |
| pd02 | 73 | |
| p100 (field/combat) | 57 | |
| p600 (combat) | 54 | |
| others | <= 48 | |

Not all placements are active simultaneously (gated by story flags or `placement_fate`), but towns carry an order of magnitude more NPCs.

`npcConfig` defines a **crowd budget**: `playerCost_ = 15`, `npcCost_ = 2`, `objCost_ = 1`, `npcLength_ = 5`, and area `density_` ~ `5.1`.

### 4.3 Lights and Other Scene Objects `[VERIFIED]`

| Zone | Active Point Lights | Dynamic Shadows | Ambient Area Lights | LocalIBL |
|---|---|---|---|---|
| pc00 | 92 | 5 | **2,422** | 65 |
| pd00 | 371 | 0 | 1,175 | **205** |
| p600 (combat) | 4,222 | 0 | 3,310 | 194 |

Point light count alone does not explain town cost (combat zones have more). Towns stand out by NPC count + foliage (`redtown_foliage_00` is 2.1 MB) + LocalIBL/area lights + 65 effect emitters in `pc00`.

### 4.4 Per-Zone Quality Bitmasks `[VERIFIED; bit semantics: HYPOTHESIS]`

Each zone has `filter/<zone>/*.ccomp.msg` containing:
- `DeferredLightParameter.useSimpleShadowQualityFlag_` (bitmask)
- `GlobalParameter.useFogDownScaleQualityFlag_` (bitmask)
- `VolumetricLightParameter` (`active_`, `numStepsMin_/Max_`, `qualityMask_`)
- `MultiFrameSuperResolutionParameter` (`active_`, `qualityMask_ = 255`) in 44 zones including `pc00/redtown_00`

Town values:

| File | simpleShadow | fogDownScale | Volumetric |
|---|---|---|---|
| pc00/redtown_00 | 240 | 57 | - |
| pc00/church_00 | 1 | 59 | active |
| pc00/church_01 | 58 | 53 | active |
| pd00/square_00 | 448 | 5 | - |
| pd00/backstreet_00 | 448 | 15 | - |
| pd00/bar_00 | 58 | 58 | - |

Other zones commonly use `53`/`57`/`58` (`simpleShadow`) and `58`/`53` (`fog`). Each bit likely maps to a quality tier or platform, meaning `square`/`backstreet` (`448` = bits 6-8) disable simple shadows on Standard quality. Setting all low bits (`0x1FF` = 511 / 59) enables cheap shadows/fog across presets.

---

## 5. Modding Hooks & Levers

### 5.1 Code Patterns (ASI) Verified in v2.0.5 `[VERIFIED]`

Patterns referenced from GBFRelinkFix (Codeberg, 2026-08-07) and verified against `granblue_fantasy_relink.exe` with `patscan.py`:

| Feature | Pattern | RVA (v2.0.5) | Matches |
|---|---|---|---|
| LOD multiplier (multiplies `xmm1`) | `C5 ?? 5E ?? ?? ?? ?? ?? C5 ?? 5F ?? C5 ?? 29 ... C6 ?? ?? ?? ?? ?? 01` (+0xC) | `0x802236` | 1 |
| Shadow resolution (preset row `+0xC/+0x10`, `+0x4/+0x8`) | `8B ?? ?? 0C 8B ?? ?? 10 8B ?? ?? ?? B9 ?? ?? ?? ?? 48 ?? ?? ??` | `0x21a3f83` | 1 |
| Disable TAA | `0F ?? ?? ?? 88 ?? ?? 48 ... 5E` | `0x21607a0` | 1 |
| FPS limiter / min frame floor | (see `notes/gbfrelinkfix_v2_patterns.txt`) | `0x1affa3` / `0x1b01de` | 1 / 1 |
| Resolution list | `0F ?? ?? 0F ?? ?? 48 8D ...` | 4 sites | 4 |

Custom hooks located for Relink Deck Tuner:
- `0x6bbaa0`: applies render resolution every frame (§3.2). Scaling the first pair changes 3D render size while keeping UI at native resolution.
- Preset table `0x6b82460` + index `0x7034940`.

### 5.2 Static Data Changes (`tools/build_mod.py`)

Candidate data edits built cleanly against `data.i` / `data.N`:

| ID | File(s) | Change | Goal |
|---|---|---|---|
| D1 | 38 `layout/timeline/*_light.scene.msg` | `bCastDynamicShadow_: 0` on 64 lights | Disable dynamic shadows on dialogue/cutscene event lights. |
| D2 | `filter/pc00/*`, `filter/pd00/*` (6 files) | Set all low quality bits in `useSimpleShadowQualityFlag_` and `useFogDownScaleQualityFlag_` | Force simple shadows and downscaled fog in towns. |
| D3 | `filter/pc00/church_00/01` | `VolumetricLightParameter.active_: false` | Disable church volumetric lighting in Folca. |
| D4 | `filter/**/cutscene/*.ccomp.msg` | Lower `DofParameter.blurSizeMax_` | Cheaper DoF (note: `evtb` tracks can override per shot). |
| D5 | `system/NPC/c00/d00_npcConfig.msg` | Scale `density_` (e.g. `0.8`) | Lower town NPC density. |

### 5.3 Menu Options: Visible, Hidden, and Orphaned `[VERIFIED]`

> **Correction:** Initial notes flagged "Limit Number of Point Lights" and "Limit Shadow Cache" as hidden options. They are orphaned UI strings (`TXT_PAU_OPT_TOG_LIMIT_PLIGHT` / `_SCASH` in `text_ui.msg`) not referenced anywhere in `option_item.tbl` or the executable.

The Options menu is defined in `system/table/option_item.tbl` (235 rows of 52 bytes), linked to localized text by XXHash32 (`tools/option_table.py`). Flag bytes 5 and 9 are `1` on PC-visible rows and `0` on PC-hidden rows:
- **Hidden rows on PC:** Shadow Quality (row 60), Effects (69), Character Models (71), Background Models (72), Decal (73), Depth/DoF (63), IBL (68), Reflection (75), HDR (54), DirectX (52), Benchmark (59), Quality/Performance Graphics Mode (57/58).

**In-game test of un-hiding them (Deck, 2026-11-02) `[VERIFIED]`:** With visibility flags enabled in data alone, all 6 appear in Options -> Graphics, but **changes revert on exit** and `GraphicSetting.vdat` is never updated. Disassembly confirms PC only registers those keys once in the key registry (`0x20e3xx..0x20e46x`) and has no apply/save code for them.

Relink Deck Tuner solves this (v0.13+) by hooking the option getter (`0x20d320`) and setter (`0x2096b0`) in memory, repurposing 5 hidden rows as custom **Relink Deck Tuner** settings persisted directly to `scripts/RelinkDeckTuner.ini`.

---

## 6. Architecture Summary

1. **Base layer (GBFRelinkFix):** Proton `WINEDLLOVERRIDES="winmm=n,b" %command%`, intro skip, distant LOD multiplier, shadow resolution override.
2. **Data mod layer (`build_mod.py`, `build_foliage.py`):** Event dynamic shadow removal (D1), town simple shadows/fog + church volumetrics removal (D2+D3), NPC density `0.8` (D5), **Foliage LOD1** on 606 vegetation `.minfo` files, and Graphics menu string rows.
3. **Native ASI layer (`RelinkDeckTuner.asi`):** Dialogue camera detection, 3D-only render scaling (`Fields = 1`), FSR 1.0 (`EASU + RCAS`) / Bicubic upscaler replacement, post-DoF resize fix, `DialogueVegetationLod` hook, F9/F10 hotkeys, menu getter/setter hooks, and telemetry logging.

---

## 7. Steam Deck & PC Benchmarks

### 7.1 Factory Baseline (Steam Deck, 2026-11-02) `[MEASURED]`

MangoHud level 4, stock game (v2.0.5), Standard preset, 1280x720:

| Shot | Scene | FPS | Frametime | GPU Load / Clock / Power | CPU Load / Clock / Power |
|---|---|---|---|---|---|
| 01 | Ship dialogue, sky background | **30** | 33.3 ms | 73% / 998 MHz / 3.2 W | 37% / 3499 MHz / 6.4 W |
| 02 | Forest dialogue (`p100`), foliage background | **26** | 39.1 ms (38.6-40.1) | **99%** / 1453 MHz / 10.3 W | 41% / **1399 MHz** / 1.7 W |
| 03 | Same, wide shot with flowers & 3 characters | **22** | 46.1 ms (45.4-47.7) | **99%** / ~1400 MHz / 10.2 W | 36% / 1399 MHz / 1.6 W |
| 04 | Same, close-up with flowers behind | **23** | 43.0 ms (42.6-44.6) | **99%** / 1425 MHz / 10.5 W | 36% / 1399 MHz / 1.1 W |
| 05 | Starting village gameplay (`p100`) | **30** | 33.4 ms (27.9-38.1) | 86% / 1065 MHz / 4.6 W | 46% / **3255 MHz** / 6.9 W |

**Takeaway:** Slow dialogues are **GPU-bound** (99% GPU consuming most of the 15 W APU budget; CPU drops to 1.4 GHz). Close-up background foliage causes the bottleneck (~30% GPU time cut needed: 46 -> 33 ms).

### 7.6 Slot-3 A/B in Seedhollow (Steam Deck) `[MEASURED]`

Same save point ("Head to the Family Zathba's base", Seedhollow):

| Scene | Stock | Data Mods (`dialogue` + `towns` + GBFRelinkFix LOD 0.6 / Shadows 512) |
|---|---|---|
| "Verbose Man" dialogue (bushes behind) | 26 FPS, 38.2 ms, GPU 99% / 1443 MHz / 10.2 W | 27 FPS, 37.0 ms, GPU 94% / 1427 MHz / 9.7 W |
| Seedhollow market archway | 27 FPS, 36.5 ms, GPU 94% / 1419 MHz / 9.7 W | **30 FPS**, 33.5 ms, GPU 93% / **1282 MHz / 7.8 W**, CPU 2.4 GHz |

### 7.7 Dynamic 3D Dialogue Scaling (`RelinkDeckTuner.asi`, 2026-11-02) `[MEASURED]`

- Cutscene camera hook (`0x942698` in v2.0.5) fires once per frame during dialogues (919 calls in 36 s) and **0 times** while walking in town.
- Testing `Fields = 1` vs `2` vs `3` confirmed the first pair controls **3D scene resolution** while the second pair controls **UI/output resolution**.
- At `Fields = 1`, `Scale = 0.8` (1024x576 -> 1280x720) in "Verbose Man": **30 FPS**, 33.4 ms (28.9-36.7), GPU 85% / 1334 MHz / 7.4 W, CPU 3.0 GHz, with sharp UI and negligible black flash.
- Black flash on resize happens because render targets (`0x1da6ce`, `0x26dac6`) are recreated when the globals change (`Fields = 1` reduces visual disruption to near zero).

### 7.8 Post-Processing Ablation (PC Test Rig) `[MEASURED]`

Disabling all major post-process passes (`SNN`, `DoF`, `AmbientAreaLight`, `SkyCloudSdf`, `Bloom`, `MotionBlur`, `ColorShadow`, `DropShadow`, `PlantParameter` via `build_effects.py`) in "Verbose Man" only saved **~1 ms** (27 -> 28 FPS, 37.0 -> 36.0 ms) while ruining visuals. Lowering shadows from 512 to 128 + distant LOD 0.4 also stayed at 27 FPS / 36.5 ms. Confirms heavy dialogues are bound by **near-camera pixel shading cost**.

### 7.9 RenderDoc Capture — "Verbose Man" Dialogue `[MEASURED on PC]`

Direct3D 11 capture on integrated AMD Radeon (same architecture as Steam Deck), Standard, 1280x720 (`tools/rdc_pass_costs.py`, `tools/rdc_inspect.py`, logs in `notes/renderdoc/`):

| Pass | GPU Time | Frame % |
|---|---|---|
| **G-buffer** (4 targets @ 1280x720 + depth) | **45.4 ms** (85 draws) | 64% |
| Shadow map (2048x2048) | 8.0 ms (208 draws) | 11% |
| Lighting / post-processing | ~17 ms | 25% |
| **Total** | **70.5 ms** | |

Inside the G-buffer pass, **one draw (EID 2326, 9,366 indices x 19 instances) takes 21.9 ms (31% of the frame)**: background bushes/trees (`notes/renderdoc/vegetation_draw_2326.png`), covering only 18.5% of the screen. Top 5 draws total **~35 ms (half the frame)** and all run the same **foliage pixel shader**:
- Alpha test (`discard`), double-sided (no backface cull), depth write;
- Granite virtual texturing (page table + albedo/normal/mask cache arrays);
- **Writes streaming feedback to 3 UAVs** (`g_ResolveParam0..2`) *before* `discard`, followed by a dither `discard` (`g_DitherTex`).

Because UAV writes happen before `discard`, **early-Z rejection is disabled**. Overlapping leaf quads pay full page-table lookups and UAV writes per pixel layer: pure **foliage overdraw**.

### 7.10 Foliage LOD Threshold Mod (`build_foliage.py`) `[MEASURED]`

`bg012c` LODs have 14,580 / 10,230 / 5,430 / 2,952 indices. Raising near `lod_screen_size_thresholds` in `.minfo` to `1000.0` forces lighter LODs even up close (`tools/build_foliage.py`):
- **`--level 2`**: Verbose Man jumps from **27 FPS (37 ms, 99% GPU) -> 30 FPS (33.3 ms, 85% GPU / 7.3 W)** at native 720p! However, LOD2 drops vertex wind animation, making trees look static, and Lyria (slot 1) remains 24 FPS because her scene has heavy opaque scenery.
- **`--level 1` (606 vegetation models)**: **Retains leaf wind animation** and reduces foliage frame cost significantly.

### 7.11 RenderDoc Capture — Lyria Dialogue (Slot 1, Zinkenstill) `[MEASURED on PC]`

Capture `gbfr_frame5082` (140 ms iGPU frame, 1,771 draws):

| Pixel Shader | Time | % | Draws | Notes |
|---|---|---|---|---|
| Foliage (alpha test, 341 lines) | 40.7 ms | 26.8% | 67 | Trees (`bg012c` 9,366 idx x 9 = 12.5 ms, etc.) |
| Static opaque (475 lines, no discard) | 26.5 ms | 17.4% | 208 | General scenery |
| Static opaque (543 lines) | 15.8 ms | 10.4% | 110 | General scenery |
| Alpha test (515 lines) | 7.8 ms | 5.1% | 40 | |
| Remainder | ~61 ms | ~40% | | Shadows (12 ms), prepass, lighting, post |

On Steam Deck, **`foliage_lod1` + `Scale = 0.85`** hits **30 FPS (33.7 ms)** in Lyria's dialogue. However, the stock game upscaler produced visible horizontal banding at `612 -> 720` (`1.18x`).

### 7.12 Upscaler Replacement: Engine TUP vs. FSR 1.0 `[VERIFIED on PC]`

- At non-native 3D resolution (`auto_frame2435`), **EID 5219** (DXBC checksum `0e4625da...`) runs the engine's temporal upsampler (`TUP`, with `ParamBuffer.orgScreen`), using unfiltered `ld` texel fetches (`ftoi(uv * orgScreen)`). At non-integer ratios (`1.18x`), nearest-neighbor row duplication creates **horizontal bands**.
- `RelinkDeckTuner.asi` hooks `D3D11CreateDevice` -> `ID3D11Device::CreatePixelShader` and replaces `TUP` with **AMD FidelityFX FSR 1.0 EASU + RCAS** (`Upscaler = 1`, MIT) or Catmull-Rom **Bicubic** (`Upscaler = 2` / `+ RCAS = 3`).
- Measured upscaler GPU cost on iGPU (1088x612 -> 1280x720): Nearest `0.07 ms`, Bicubic `0.45 ms`, **FSR 1.0 EASU `0.85 ms`**, vs. Stock TUP **`2.16 ms`** in RenderDoc. Visual comparisons saved in `notes/upscaler/`.
- **Post-DoF resize fix (v0.15):** The game's depth-of-field pass also performed an unfiltered stretch back to output dimensions when 3D scale < 1.0; v0.15 redirects the DoF dispatch call to apply the active filter (EASU / Bicubic) cleanly without double-sharpening outlines.

### 7.13 Automated PC Benchmark Matrix (2026-11-03) `[MEASURED]`

Test rig: integrated AMD Radeon (slower than Steam Deck to measure headroom cleanly without hitting 30 FPS cap; 120 FPS limit, 1280x720 Standard):

| Configuration | Lyria (Slot 1) | Verbose Man (Slot 3) | Mean Frame Time Gain |
|---|---|---|---|
| A. Native (stock) | 18-19 | 25 | — |
| B. 3D 0.85, Stock TUP | 22 | 31 | -19% |
| C. 3D 0.85, **FSR 1.0** | 23 | 32 | -22% |
| D. 3D 0.85, Bicubic | 23 | 32 | -22% |
| E. 3D 0.75, FSR 1.0 | 27 | 38 | -33% |
| F. 3D 0.90, FSR 1.0 | 21 | 29 | -13% |
| G. Data mods `dialogue` + `towns` | 19 | 25 | ~0% |
| H. Foliage LOD1 | 21 | 31 | -15% |
| **I. Foliage LOD1 + 3D 0.85 FSR 1.0** | **25** | **39** | **-31%** |
| J. Foliage LOD2 | 22 | 36 | -22% |

Second run (including field walking FPS):

| Configuration | Lyria | Verbose Man | Field Folca | Field Seedhollow |
|---|---|---|---|---|
| Native | 18 | 25 | 45 | 30 |
| Foliage LOD1 | 21 | 31 | 50 | 32 |
| LOD1 + FSR1 0.85 | 25 | 39 | — | — |
| LOD1 + FSR1 0.80 | 26 | 43 | 48 | 33 |
| LOD1 + FSR1 0.75 | 30 | 47 | 51 | 34 |
| Foliage LOD2 | 22 | 36 | 52 | 37 |

Raw results: `notes/bench/results_raw.txt`.

### 7.14 Engine LOD Selection & `DialogueLodBias` `[VERIFIED; small gain]`

- Using `asi/research/lodtrace.cpp` (hardware watchpoints DR0-DR3 on `bg012c` thresholds `{1.0, 0.6, 0.24, 0.2}`), runtime LOD selection reads derived thresholds ~930x/s at `exe+0x23a4bc6` (`v2.0.5`), multiplying screen size by a scalar in `.rdata` (`exe+0x54abf98 = {0.25, 1.0}`).
- `DialogueLodBias` overrides that `1.0` during dialogues. However, instanced foliage uses `DrawIndexedInstancedIndirect` and ignores this loop; only static scenery responds, and lowering below `0.3` turns Folca hay bales into flat boxes (`notes/bench/lodbias_030_vs_nativo.png`). Left at `1.0` (Normal) by default.
- **`DialogueVegetationLod` (v0.12+):** Hooks the threshold compare instruction directly during dialogues, identifying modified vegetation models by their `>= 1000.0` threshold tag, yielding **+7 to +12 FPS** in vegetation-heavy dialogues at `0.05` (`Reduced`) with near-native visuals.

### 7.15 Final Recommended Configuration

| Component | Setting | Why |
|---|---|---|
| Foliage (`foliage_lod1`) | 606 models start at LOD1 | -15% frame time in dialogues while keeping leaf wind animation; also boosts field FPS. |
| RelinkDeckTuner | `Mode = 1`, `Scale = 0.8`, `Fields = 1`, `Upscaler = 1`, `DofFix = 1`, `DialogueVegetationLod = 0.05` | 3D-only dialogue scaling with sharp HUD, FSR 1.0 + clean DoF stretch, and massive foliage boost. |
| `DialogueLodBias` | `1.0` | Minimal gain vs visible geometry pop-in. |
| `dialogue` + `towns` data mods | Included | Covers cutscene dynamic lights, `pc00`/`pd00` simple shadows/fog/volumetrics, and 80% NPC density. |
| GBFRelinkFix | Shadows `128`, LOD `0.8` | Keeps town frametimes stable without visible quality loss. |

### 7.16 Towns: Stutter & NPC Density (Deck, 2026-11-03) `[MEASURED]`

~1.5 minute walk through Seedhollow (slot 3) with per-frame `RDTSC` timing in `RelinkDeckTuner` 0.9:

| Configuration | Median FPS | Interval `maxFrameMs` (median / p90) | Spikes > 40 ms (excluding loads) |
|---|---|---|---|
| NPCs 100% (`density_` 5.10) | 29.9 | 33.5 / 34.2 ms | 152 ms, 42 ms, 41 ms |
| NPCs 80% (`density_` 4.08) | 29.9 | 33.5 / 34.3 ms | 41 ms |

Seedhollow holds a locked **30 FPS** with almost zero >40 ms spikes outside initial asset streaming. NPC density at 80% shows minimal visual difference and removes two minor hitches, included in the final `towns` mod.

### 7.17 Menu Settings Singleton Notes `[VERIFIED]`

- `0x20d320(settings*, out*, key)` reads option values (e.g. `Rendering`, `TXT_PAU_OPT_GRA_SIMULATION`, `0x9a9f05f8` at `settings + 0x42`).
- Settings object singleton resides at `exe + 0x7c25030` (v2.0.5), resolved via RIP-relative pattern scan on newer versions.

---

## 8. Discarded or Unverified Hypotheses

- "Game raises render resolution in dialogues": **disproved in code** (§3.2).
- `MultiFrameSuperResolution` as FSR-like temporal scaling: component is active in 44 zones, **not verified** whether it scales resolution or is purely TAA sample accumulation.
- `bDebug_DisableShadow_` on `DeferredPointLightParameter` (`false` in all zones): likely stripped debug flag; untested.
- Granite texture streaming (`granite/2k|4k`): executable contains `"GPU cache too small"` warnings, potentially related to initial town load hitches; untested.

---

## 9. References

- **GBFRelinkFix** (Codeberg): https://codeberg.org/Lyall/GBFRelinkFix
- **GBFRDataTools** (`data.i`, `evtb`, schemas, file lists): https://github.com/Nenkai/GBFRDataTools
- **GBFR Modding Docs**: https://nenkai.github.io/relink-modding/
- **GBFR Mod Manager**: https://github.com/WistfulHopes/gbfrelink.utility.manager
- **Community Context (Steam Deck town drops)**: https://steamcommunity.com/app/881020/discussions/0/4208120159992148673 & https://steamcommunity.com/app/881020/discussions/0/4208119288695646903
