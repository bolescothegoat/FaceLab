# FaceLab - player guide

## What FaceLab is (in one minute)

RaceMenu has expression sliders (Smile, Sad, Angry, BigAah ...). They are real changes to the head mesh, and RaceMenu lets you go up to **3.0**,
far beyond what the game's own face animation ever uses. FaceLab does the same thing **live, in the game world, on any NPC or on you**, and lets
other mods ask for a named face ("pleased", "shocked", "tongue out") that stays on while a scene runs.

On its own FaceLab does nothing to your game. It is a tool for two kinds of people:

* **Players:** a lab to try expressions on yourself or an NPC, and to edit the faces that other mods use.
* **Modders:** a way to put a real expression on an actor without writing face code (see MODDER_GUIDE.md).

## What you need

| Needed | Why |
|---|---|
| Skyrim SE 1.5.97, or AE 1.6.x / 1.7.x | one DLL for all of them. **Only AE 1.6.1170 has been run by the author**; the others are supported through the Address Library but untested |
| SKSE and the matching Address Library | standard |
| SKSE Menu Framework | FaceLab's menu lives in it |
| **A head with morph files** (see below) | FaceLab reads them; it ships none |

### Which heads work

FaceLab finds the head an actor wears and reads the expression files (`.tri`) of the morph mod you have installed. If none of these is installed, FaceLab simply says "not supported" and does nothing.

| Head | Morph files come from | Notes |
|---|---|---|
| High Poly Head (male / female) | High Poly Head | the original target of FaceLab |
| Vanilla head (male / female) | Expressive Facial Animation (it ships the vanilla morphs loose) | |
| CoTR **original low poly** head (male / female) | CoTR's expression files | the CoTR *High Poly* head is **not** supported |
| UBE (female) | UBE 2.0 | the mouth, brows and lashes follow the head |

Some heads have extra morphs the others do not (CoTR's `Smile001` ..., UBE's `Ahegao`, `Kiss`, `Yawning` ...). They appear as extra sliders on the Face page automatically.

## Open the menu

Open the SKSE Menu Framework menu (its own hotkey) and pick **FaceLab**. There are three pages.

### Face page - the lab

1. **Target.** "The player", or "The actor I last looked at". To pick an NPC: close the menu, look at them for a moment, open the menu again; their name appears next to the radio buttons.
2. **Sliders.** Every channel is a number from 0 to 300. That is the RaceMenu weight x 100 (100 = 1.0, 300 = 3.0). They are grouped: Mouth, Eyes and brows, Emotions, each eye separately, true eye rotation, tongue, oral.
3. **Apply all sliders** puts that face on the target. **Reset the target's face** takes it off. With "Apply a slider as soon as I move it" ticked, every slider acts at once.
4. **Fade time** is how long a face takes to arrive and leave (0 = instant).
5. **Lab face holds for (seconds)** - a face you apply here outranks every other mod's face, so it lets go by itself after this long (0 = until you press Reset). While it is on, the page shows an orange warning.
6. **Extra morphs of this head** - at the bottom, only the names your target's head has.

Tip: start with **Just BigAah 3.0** to see that FaceLab works on your target at all.

### Presets page - faces that mods ask for

Other mods ask FaceLab for a face **by name** (for example `MyMod.Facial.Pleased.1`). Those names are stored in small text files, `Data/SKSE/Plugins/FaceLab/Presets/<Mod>.toml`.

* Pick one in the list. **Load into the sliders** copies it to the Face page so you can look at it. **Load and apply to the target** shows it on the target.
* Change the sliders, then **Save the sliders over this preset** to keep your version, or **Save the sliders as this name** to make a new one.
* A preset that you save stays in its own file. Reinstalling the mod that owns it will overwrite your changes, so copy the file if you want to keep your version.
* Press **Reload preset files** after editing a file by hand.

### Controls page - hotkey

A single hotkey applies the sliders to the target; the next press resets the face. Keyboard and gamepad are separate. The gamepad default is **X while holding D-pad Down**. Pick a key or gamepad button and a modifier, and press Esc to cancel.

## Special channels

Most channels are plain morphs. A few are signals for other mods:

| Channel | What it does |
|---|---|
| `TongueOut` | not a morph: the face asks a tongue mod for the tongue (1.0 = fully out, higher = further) |
| `OralMode` | a marker: 50 or more says "oral scene". The mouth then belongs to OStim and FaceLab's mouth channels give way as OStim's own mouth opens |
| `MouthAssist` | an opening that fades out as the natural mouth opens, so the mouth is not shut for the second after a scene change |
| `EyesCross` | cross-eyed. **The Fourth Wall** lets go of the eyes in proportion while this is on |
| `Eye...Turn...` | true eye rotation, 10 degrees per 1.0 |

## Things that surprise people

* **A face is not saved.** A new game or a load drops every face. Nothing can get stuck in a save.
* **The highest priority wins.** Several mods can ask for faces on the same actor. The lab face has the highest priority, so it hides the others until it expires or you press Reset. When it goes, the next layer shows.
* **Weights above 1.0 stretch the head.** That is the point (RaceMenu does the same up to 3.0), but extreme combinations look strange.

## When it does not work

Open `Documents/My Games/Skyrim Special Edition/SKSE/FaceLab.log` (it is quiet unless the Debug tools box is ticked on the Face page).

| You see | Meaning |
|---|---|
| `Morph: <name> uses head family '...'` | working: FaceLab recognised the head |
| `Morph: <name> is not supported: none of the head families fits` | the head is not one of the supported ones, or its morph mod is not installed |
| a family listed with `head file 0 vertices, 0 morph file(s)` | that head's morph mod is not installed (normal for heads you do not use) |
| `the head file has ... vertices (CoTR high poly): not supported` | you have the CoTR **high poly** head; use the original low poly one |

Scattered lines or a deformed head **in RaceMenu itself** (before FaceLab does anything) usually mean two morph mods overwrite each other, so the head mesh and its `.tri` file have different vertex counts. That is a load-order problem between the morph mods; FaceLab is not involved.

If you report a problem, send `FaceLab.log` **and** tick the Debug tools box first, then reproduce it.

## Compatibility

* **OStim:** scene mods can ask FaceLab for faces during scenes. FaceLab is the face, OStim keeps the mouth in oral scenes.
* **Tongue mods:** can obey `TongueOut`.
* **The Fourth Wall:** obeys `EyesCross` and adds gaze on top of the face.
* **RaceMenu:** FaceLab reads the same morph targets but never writes your saved face. Nothing is stored in the save.
* **SMP physics:** works, FaceLab only moves the face vertices.

FaceLab is free software under the GPL-3.0-or-later; its two API files (the C++ header and the Papyrus script) are MIT so that closed mods can use it.
