# Proposal: "Relink Deck Tuner"

A unified mod with three goals:

1. **Unlock** hidden PC graphics options present in game data tables.
2. **Add** custom performance controls missing from the stock menu.
3. Provide **automatic profiles** for the two main Steam Deck bottlenecks: **dialogues** and **towns**.

This proposal builds on findings in [report.md](report.md). Items marked `[H]` were initial hypotheses pending on-device verification.

---

## 1. Player-Facing Controls

### 1.1 In-Game Menu (Options -> Graphics)

Options already present in `option_item.tbl` but hidden on PC:

| Option | Values | Steam Deck Benefit |
|---|---|---|
| Shadow Quality | Table presets | Cheaper shadows game-wide. |
| Character Models | Standard / Medium / High | Fewer polygons on town NPCs and characters. |
| Background Models | Standard / Medium / High | Lighter town geometry. |
| Effects | Standard / Medium / High | Lower particle cost. |
| Decal | Fast / Standard / High | Cheaper ground decals. |
| Depth of Field | On / Off | Disable background blur (heavy during dialogues). |

`[H]` Need to verify whether the PC engine honors these flags. If ignored, hook them in Layer B (§2).

### 1.2 Mod Configuration (Outside the Game)

| Group | Setting | Default (Deck Profile) |
|---|---|---|
| **Global** | LOD multiplier | `0.75` |
| | Shadow resolution (allows going below menu minimum 256) | `256` |
| | Internal render scale `[H]` | `100%` |
| | Disable TAA | `No` |
| **Dialogue mode** (active only during cutscenes and conversations) | Disable dynamic shadows on event lights | `Yes` |
| | Dialogue DoF | `Off` |
| | Extra dialogue LOD | `1.0` (keep untouched up close) |
| | Dialogue render scale `[H]` | `100%` |
| **Town mode** (active only in `pc00` / `pd00` and annexes) | Town LOD multiplier | `0.6` |
| | Simple shadows & reduced fog (zone flags) | `Yes` |
| | Volumetric light (`pc00` church) | `Off` |
| | NPC density `[H]` | `100%` (options: `75%` / `50%`) |
| | Town shadow resolution | `128` |

Preset profiles:
- **"Deck Stable 30"**: defaults above, targeting a 30 FPS floor.
- **"Deck 40"**: adds `0.5` LOD, `75%` NPCs, and `85%` render scale if validated.
- **"Quality"**: stock settings with unlocked menu options only.

---

## 2. Architecture

Originally designed as a two-layer **Reloaded-II** package (later implemented cleanly as a data patcher + standalone native `.asi` plugin):

```text
RelinkDeckTuner/
├─ GBFR/data/...            <- Layer A: data files (.json / packed assets)
└─ RelinkDeckTuner.dll      <- Layer B: runtime hooks + config + profiles
```

### Layer A: Static Data Changes

| ID | File(s) | Change | Status |
|---|---|---|---|
| A1 | `system/table/option_item.tbl` | Enable visibility flags on hidden options | Format verified, effect `[H]` |
| A2 | `38 x layout/timeline/*_light.scene.msg` | `bCastDynamicShadow_ = 0` across 64 event lights | Format verified |
| A3 | `filter/pc00/*`, `filter/pd00/*` | Set all bits in `useSimpleShadowQualityFlag_` and `useFogDownScaleQualityFlag_` | Format verified, bitmask `[H]` |
| A4 | `filter/pc00/church_00/01` | `VolumetricLightParameter.active_ = false` | Format verified |
| A5 | `system/NPC/c00/d00_npcConfig.msg` | Adjust `npcCost_` / `density_` | Format verified, effect `[H]` |

Layer A is **generated deterministically by script** (`tools/build_mod.py`) from the user's installed game archives so game files never need to be redistributed.

Limitation: data files cannot toggle dynamically. A2 disables event light shadows permanently, and A3–A5 apply statically to towns.

### Layer B: Runtime Hooks

| Hook | Location (v2.0.5) | Status |
|---|---|---|
| LOD multiplier | pattern -> `0x802236` | Verified (unique) |
| Shadow resolution | pattern -> `0x21a3f83` (preset row) | Verified (unique) |
| Disable TAA | pattern -> `0x21607a0` | Verified (unique) |
| Render scale | `0x6bbaa0` (applies resolution every frame) | Hook point located |
| Event detection | Hook `Timeline::WindowTalkModule` / `CutsceneEvent` or cutscene camera | Implemented via camera hook (`0x942698`) |
| Town detection | Stage loading (`pc00` / `pd00`) | To locate if dynamic town overrides needed |
| DoF on/off | `DofParameter.active_` or timeline `Dof` track | Superseded by post-DoF resize fix |

Every hook uses **byte patterns** rather than hardcoded RVAs when crossing versions so updates degrade gracefully rather than breaking the game.

---

## 3. Risks & Mitigations

| Risk | Mitigation |
|---|---|
| Reloaded-II setup on Steam Deck (Proton) is more involved than Windows. | Provide standalone **ASI + `.ini`** loaded via `WINEDLLOVERRIDES="winmm=n,b" %command%`, plus external files patched via `data.i` with automatic backup. |
| Table mods (`.tbl`) can break across game updates. | Generate from the installed table and validate row size (`52` bytes) before patching. |
| Hidden menu options do nothing on PC. | Hook the options getter/setter directly in the `.asi` (done in RelinkDeckTuner 0.13). |
| Online play / updates. | Graphics-only changes; gameplay tables and save files remain untouched. |

---

## 4. Implementation Phases

| Phase | Deliverable | Validation (~5 min each) |
|---|---|---|
| 0 | GBFRelinkFix baseline (`LOD 0.6`, `Shadows 128`) | Check SteamOS overlay in town and dialogue: GPU vs CPU bound? |
| 1 | Layer A data generator (`A1–A5`) | Verify options visibility and FPS delta in town/dialogue. |
| 2 | Layer B runtime hooks + `.ini` | Compare against Phase 0. |
| 3 | Dialogue/event detection + automatic mode switching | Verify clean transitions without stutter. |
| 4 | Render scale + upscaler + DoF fix | Compare visual quality vs FPS. |
