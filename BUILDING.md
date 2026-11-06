# FaceLab - source code and build instructions (version 0.2.0)

Free software under the GNU GPL v3 or later (see `LICENSE`). Copyright (C) 2026 Bolescothegoat.
Only the two interface files in `api/` (`FaceLabAPI.h`, `FaceLab.psc`) are under the MIT licence (`api/LICENSE-MIT.txt`), so other mods can use them whatever their own licence.
Third-party licences: `THIRD_PARTY_NOTICES.txt`.

An SKSE plugin (C++23) for Skyrim SE 1.5.97 and AE 1.6.x / 1.7.x (one DLL; the engine addresses come from Address Library). It plays RaceMenu-style face morphs
(uncapped, up to weight 3.0) live on any loaded actor, for the High Poly Head, the vanilla head, CoTR and UBE.

Built with AI assistance (Claude, by Anthropic) from the author's designs and in-game testing.

## Layout

| File | What it is |
|---|---|
| `src/main.cpp` | SKSE plugin entry (`SKSEPlugin_Version` for AE, `SKSEPlugin_Query` for SE 1.5.97), logging |
| `src/Morph.h`, `src/Morph.cpp` | The morph engine: head-family detection, the face update hook, base-vertex patching, fades, gaze layer, mouth / oral handling |
| `src/TriFile.h`, `src/TriFile.cpp` | Reader for FaceGen `.tri` (FRTRI003) files; plain and `EXPR_` morph names |
| `src/Families.h`, `src/Families.cpp` | The table of head families (High Poly Head, vanilla, CoTR, UBE; male / female) and the morph files each one reads |
| `src/FamilyLoad.h`, `src/FamilyLoad.cpp` | Loads a family's face parts, merges files, adds per-eye morphs, makes the UBE mouth / brows / lashes follow the head |
| `src/Lab.h`, `src/Lab.cpp`, `src/Menu.cpp` | The in-game lab (SKSE Menu Framework pages), settings |
| `src/Presets.h`, `src/Presets.cpp` | Preset files (`Data/SKSE/Plugins/FaceLab/Presets/*.toml`) |
| `src/Api.h`, `src/Api.cpp`, `src/Input.*` | Papyrus natives, the C interface other plugins use (`FaceLabAPI.h`), hotkeys |
| `api/` | What other mods copy: `FaceLabAPI.h`, `FaceLab.psc`, a developer README |
| `tests/` | Small console programs: the `.tri` reader, the family loader against a real load order, the UBE mouth check |

## Building

Prerequisites: Visual Studio 2022+ (MSVC, C++23), CMake 3.21+, Ninja, [vcpkg](https://github.com/microsoft/vcpkg), and:

1. **CommonLibSSE-NG 15.5.0** - a checkout of <https://github.com/alandtse/CommonLibSSE-NG> at tag `v15.5.0` (commit `8eadded`). Not the older
   CharmedBaryon repository: it cannot read the AE 1.7 Address Library files. Set the environment variable `COMMONLIB_SSE_FOLDER` to the checkout folder.
2. **SKSEMenuFramework.h** - not included here. Download `resources/SKSEMenuFramework.h` from <https://github.com/QTR-Modding/SKSE-Menu-Framework-3>
   and put it in `src/`.
3. vcpkg packages (`spdlog`, `fmt`, `tomlplusplus`, `directxtk`, ...) are installed automatically from `vcpkg.json`; set `VCPKG_ROOT` to your vcpkg folder.

```
cmake --preset release
cmake --build --preset release
```

The DLL is written to `dist/SKSE/Plugins/FaceLab.dll`. At run time FaceLab needs the SKSE Menu Framework DLL (for the menu) and the Address Library for your game version.

### Tests

The programs in `tests/` need only the files in `src/` they name, and a C++20 compiler:

```
cl /std:c++latest /EHsc /O2 /I src tests/tri_test.cpp src/TriFile.cpp
cl /std:c++latest /EHsc /O2 /I src tests/family_test.cpp src/FamilyLoad.cpp src/Families.cpp src/TriFile.cpp
cl /std:c++latest /EHsc /O2 /I src tests/mouth_cmp.cpp src/FamilyLoad.cpp src/Families.cpp src/TriFile.cpp
```

`family_test <modlist.txt> <mods folder>` resolves the morph files the way Mod Organizer 2 would (the enabled mods of a `modlist.txt`, highest priority first; add
`--enable "Mod name"` to include a disabled mod) and reports, per head family, the face parts it finds, the morph counts and how many of FaceLab's channel names it covers.

## Notes for hackers

* **The hook:** the vtable of `BSFaceGenNiNode` (`REL::VariantID` 252410 on SE, 200333 on AE), virtual function `0x2C` (`UpdateDownwardPass`), through CommonLibSSE-NG's
  `write_vfunc`. After the engine has rebuilt the face for the frame, FaceLab adds its offsets to the head shape's live vertex data **and** to the shape's `FOD` extra data
  (base vertices at `+0x18`, count at `+0x20`; found by probing a real head), so the engine's own rewrite already contains them.
* **Runtimes:** the DLL exports both `SKSEPlugin_Version` (AE) and `SKSEPlugin_Query` (SE 1.5.97) and has no `CompatibleVersions` list. The offsets it relies on are the same on SE and
  AE in CommonLibSSE-NG 15.5.0 (`BSDynamicTriShape` runtime data at `0x160`; the face node's vtable IDs). It has been run on AE 1.6.1170 only; SE 1.5.97 and AE 1.7 are untested.
* **Head families** are found from the vertex counts of the actor's face shapes: the head's count is the strong sign (High Poly Head 3832 / 3598, vanilla 996 / 898, CoTR 3127 / 12206, UBE 11498),
  otherwise at least two different face parts must match. The morph files are **not bundled**: they are read from whichever morph mod is installed (`src/Families.cpp` lists the paths).
  Only loose files are read; the game's own `.tri` files are inside its BSA.
* **UBE:** the head's expression file is far stronger than the face parts' own morphs and has names they lack. So the mouth, brows, lashes and eye shadows are driven *from the head's morphs*:
  brows, lashes and shadows take the weighted offsets of their nearest head vertices; the mouth part (teeth, tongue) takes one rigid jaw motion fitted per morph (its own `BigAah` says which vertices are
  lower jaw), with half of the jaw's turn, because morphs blend in a straight line and a full turn stretches the teeth at weights above 1.
* Morph weights are not clamped at 1.0 (FaceLab goes to 3.0, like RaceMenu's sliders). Faces are never saved in the save game.
