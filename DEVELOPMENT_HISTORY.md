# Development history

Dates before this repository's first commit come from my own project notes and file timestamps on my PC. They are not third-party timestamps.

| Date | Event |
|---|---|
| 2026-10-25 | Found what RaceMenu's expression sliders really are: static head morph targets named `EXPR_<Name>` in the head's expression `.tri` files, not MFG / face-animation values. That is why no MFG, MFEE or key-frame approach could reproduce RaceMenu's look. Built FaceLab's own method: read the `.tri` morph records, add weight x offsets to the head shape's live vertex data and to its base-vertex copy (`FOD`), so the engine's own rewrite keeps them. Confirmed in game ("that was it, it works now"). |
| 2026-10-25 | Split into its own mod. Added the preset files, layers with priorities, the Papyrus natives and the C interface (`FaceLabAPI.h`); other mods ask FaceLab for faces by name. Fixed a crash caused by writing into the engine's second base-vertex copy (`FMD`) on the API path. Added mouth yield (API v2) so lip sync can take the mouth; confirmed in game. |
| 2026-10-26 | Fixed a flash-then-revert twitch on some actors (the offsets were being added twice after a normal blink or lip-sync rewrite). Added the cross-eyed channel that The Fourth Wall yields to (API v4). |
| 2026-11-04 onward | Per-eye calibration against camera angle and distance, and the gaze layer (API v6), developed together with The Fourth Wall. |
| 2026-11-05 | Head families: the High Poly Head, vanilla (EFA), CoTR (low poly; the high poly head was dropped the same day: little support, and it did not work in the first test) and UBE, male and female, found from the vertex counts of the actor's face shapes, reading whichever morph mod is installed. Extra per-head morph names in the menu. UBE: the mouth, brows, lashes and eye shadows follow the head's morphs (rigid jaw for the teeth and tongue). Tested in game on all of them, including original CoTR low poly (male and female, extra expressions). |
| 2026-11-05 | Opened to Skyrim SE 1.5.97 and AE 1.6.x / 1.7.x (Address Library, CommonLibSSE-NG 15.5.0). Audited for licences, released as GPL-3.0-or-later; the API files under MIT. This repository's first commit is the source at that point. |
| 2026-11-07 | A Nexus user reported lower frame rates with the Fourth Wall's FaceLab gaze. FaceLab's own timers (Debug tools) showed about 0.6 ms per character per frame. A section profiler (Debug only) found the cause: the check that a face shape's base array had not been rebuilt called VirtualQuery twice per shape on every update, 85% of the update cost. 0.2.1 checks the extra-data pointer instead and only probes memory when it changes. One update went from about 480 us to about 70 us with one character. |
| 2026-11-08 | 0.2.2: scene rules for OStim and SexLab (new Scenes page), Advanced Eyes Addon support with the Eye assist sliders, UBE eye correction sliders, the Assists page, a tongue on/off button, and a menu clean-up (Apply, a Debug page, hotkey capture that shows at once). FaceLab now finds a tongue mod by an exported function name instead of a file name. |

From the first commit of this repository onward, git history is the record.
