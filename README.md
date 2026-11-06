# FaceLab

An SKSE plugin for Skyrim SE / AE that plays **RaceMenu-style face morphs** on any loaded actor: the same expression morphs you see in RaceMenu's sliders, applied live, with weights up to 3.0, held in normal gameplay,
and compatible with blinking, lip sync and SMP physics. It is a **framework**: other mods ask for a named face ("this actor, pleased") and FaceLab shows it.

- Source: https://github.com/bolescothegoat/facelab
- License: GNU GPL v3 or later (see `LICENSE`). The two files other mods copy to talk to FaceLab, `api/FaceLabAPI.h` and `api/FaceLab.psc`, are MIT (`api/LICENSE-MIT.txt`).
- Build instructions: `BUILDING.md`
- Development timeline: `DEVELOPMENT_HISTORY.md`
- For modders: `api/README.md`

## Heads it works on

| Head | Female | Male | Morph files it needs (not included) |
|---|---|---|---|
| High Poly Head | yes | yes | the expression files of High Poly Head |
| Vanilla | yes | yes | Expressive Facial Animation (EFA), which ships the vanilla head's morph files loose |
| CoTR (original low poly head only; the high poly head is not supported) | yes | yes | CoTR's expression files |
| UBE | yes | - | UBE 2.0's morph files |

FaceLab ships **no** morph, mesh or texture files. It reads the morph files of the morph mod you have installed. A head it has no morph data for reports "not supported" and is left alone.

Heads with extra expressions (CoTR's `Smile001`, UBE's `Ahegao`, `Kiss`, `Yawning` ...) show those as extra sliders in the menu.

## What it does

* A lab in the SKSE Menu Framework menu: sliders for every channel (mouth shapes, brows and eyes, emotions, per-eye controls) for the player or the actor you last looked at, plus an editor for named presets.
* **Named faces for other mods:** preset files (`Data/SKSE/Plugins/FaceLab/Presets/<Mod>.toml`), a C++ interface and Papyrus functions. Layers with priorities, so several mods can ask at once and nothing gets stuck.
* A **gaze layer** for the eyes (used by The Fourth Wall), mouth yield for lip sync, and an oral mode that leaves the mouth to OStim.

## Requirements

Skyrim SE 1.5.97 or AE 1.6.x / 1.7.x with the matching Address Library, SKSE, and [SKSE Menu Framework](https://github.com/QTR-Modding/SKSE-Menu-Framework-3) for the menu, plus the morph mod for your head (table above).
FaceLab has been run on AE 1.6.1170; the SE 1.5.97 and AE 1.7 paths use Address Library and are not yet tested on those versions.

## Built with AI assistance

The C++ was written by Claude (Anthropic) through Claude Code, from my designs and my in-game testing and logs. The mod contains no AI-made art, textures, meshes or sounds.

## Credits

- CommonLibSSE-NG (alandtse fork)
- SKSE Menu Framework (Thiago099 / QTR-Modding)
- The authors of the morph mods FaceLab reads: High Poly Head, Expressive Facial Animation, CoTR, UBE (required by the player, not included)
- OStim / OStimNG (VersuchDrei and contributors) for the scenes FaceLab's oral mode works alongside

## Guides

* [Player guide](docs/USER_GUIDE.md) - what FaceLab is, the menu page by page, special channels, troubleshooting
* [Modder guide](docs/MODDER_GUIDE.md) - presets, Papyrus, the C++ API, layers and priorities, lip sync, gaze
