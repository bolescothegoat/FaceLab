# Making your own faces, and putting them in scenes

This guide takes you from "I want her to look like this in this scene" to a working face, step by step. It also shows how to give a face a tongue.

You need: **FaceLab** and the **SKSE Menu Framework**. For tongues you also need a **tongue mod that works with FaceLab** (see "The tongue" below). For scenes: **OStim** and/or **SexLab**.

---

## 1. Make a face

1. Open the SKSE Menu Framework menu, then **FaceLab > Face**.
2. Pick who to test on under **Target**: yourself, or "The actor I last looked at" (close the menu, look at an NPC for a moment, open it again and the name appears).
3. Move the sliders. Each one is a morph of the head, from 0 to 300 (100 = the full RaceMenu slider; above 100 goes further than RaceMenu does). Turn on **Apply a slider as soon as I move it** to see changes live.
4. **Apply all sliders** puts the face on the target. **Reset the target's face** takes it off.
5. Happy? Go to **FaceLab > Presets**, type a name in "New preset" and press **Save the sliders as this name**.

Your preset is saved in `Data/SKSE/Plugins/FaceLab/Presets/` (in your mod manager, under the mod or Overwrite). It is a plain text file; you can also edit or copy it by hand:

```toml
[[preset]]
name = "MyPack.Orgasm.Female"
weights = { BigAah = 100, BlinkLeft = 100, BlinkRight = 100, MoodHappy = 80 }
```

**Naming.** Use `Pack.Context.Name`, for example `MyPack.Blowjob`. A preset name is just a name; scene rules (below) point at it. A name may also end in `.Displeased`, `.Neutral` or `.Pleased`: a mod that knows how a character feels can ask for the matching one, and if it is missing FaceLab falls back to `.Neutral`, then to the name without the ending.

**Tip: a face that is only mouth and brows is more believable than every slider moved.** Use the Presets page to load one of the shipped faces into the sliders, change two or three things, and save it under your own name.

### Channels worth knowing

| Channel | What it does |
|---|---|
| `BigAah`, `Eee`, `Oh` ... | Mouth shapes. `BigAah` is the open mouth. |
| `BlinkLeft`/`BlinkRight` | Closes the eyes. 100 = shut. |
| `EyesCross` | Both eyes turn towards the nose (ahegao). |
| `EyeLeftLookUp` ... | Each eye on its own. |
| `BrowUpLeft`, `BrowInLeft` ... | Brows. |
| `MoodHappy`, `DialogueSad` ... | Whole-face emotions. |
| `OralMode` | Not a morph. 50 or more says "this is an oral scene" (see below). |
| `MouthAssist` | Not a morph. Holds the mouth open while the scene has not opened it yet. |
| `TongueOut` | Not a morph. Asks the tongue mod to put the tongue out (see below). |

---

## 2. The tongue

`TongueOut` is a request, not a morph. FaceLab does not draw a tongue; a **tongue mod** does. To give a face a tongue, put `TongueOut` in it:

```toml
weights = { BigAah = 70, EyesCross = 101, OralMode = 100, TongueOut = 123 }
```

`TongueOut = 100` is fully out; more goes further out, less is partway.

**What has to be installed for a tongue to appear in a scene:**

1. FaceLab and a tongue mod that supports it, **both updated together** (an old pair can silently not talk to each other).
2. OStim with Halo's tongue models and their OStim equip objects (the tongue mod asks OStim to equip the tongue). On a UBE race you also need the small UBE patch that adds the UBE races to the tongue armors.
3. HDT-SMP if you want the tongue to have physics.

**Testing a tongue without a scene.** On the Face page press **Show the tongue**. It is added on top of whatever face is showing (or on no face at all). On a character other than yourself the tongue can take a second or two to appear; it then stays on her. The line under the button says what is happening: "The tongue mod has a tongue on her." means it works; any other message tells you what is missing. Which tongue is put on when no scene has supplied one, and the tongue's size and position, are the tongue mod's job: set them in its own menu.

**Oral scenes.** In a blowjob the scene (OStim) controls the mouth. Put `OralMode = 100` in the face, so FaceLab does not fight the scene: the mouth channels give way as the scene's mouth opens, and the emotions leave the lips alone.

---

## 3. Put a face in a scene: rule files

A **rule** says: *in a scene like this, for an actor like this, use that preset.* Rules are plain text files in:

```
Data/SKSE/Plugins/FaceLab/Scenes/*.toml
```

FaceLab reads every `.toml` there (in name order). After you edit one, press **FaceLab > Scenes > Reload the rule files**; no restart needed.

### A rule, field by field

```toml
[[rule]]
name       = "Blowjob (OStim)"          # shown in the log; yours to choose
frameworks = ["ostim"]                   # "ostim", "sexlab", or both (the default is both)
on         = "scene"                     # "scene" = while the scene runs, "orgasm" = at an orgasm
tags       = ["blowjob"]                 # scene tags (any one of them); ["*"] = any scene
tags_all   = false                       # true = ALL the tags must be there
actions    = ["blowjob", "deepthroat"]   # OStim: the actor takes part in one of these actions
role       = "actor"                     # actions: "actor" = the one doing it, "target" = the one it is done to
sex        = "any"                       # "female", "male" or "any": which actors get the face
preset     = "MyPack.Blowjob"           # the preset to apply (required)
priority   = 5                           # higher wins over other mods' faces (orgasm rules default to 9)
fade       = 0.5                         # seconds to blend in and out
hold       = 8                           # orgasm rules: seconds the face stays
```

A rule needs at least `tags` or `actions`; one with neither would fit every scene, so FaceLab skips it and says so on the Scenes page.

**First rule wins.** For each actor in the scene FaceLab goes down the rules from the top of the first file and uses the first one that fits. Put specific rules above general ones.

**Orgasm rules** show their face for `hold` seconds on top of the scene face, then it fades back to the scene face. They apply to the actor that orgasmed in OStim, and to every actor of the thread in SexLab (SexLab announces orgasms per scene, not per person; `sex` picks who gets which).

### OStim or SexLab: what to match on

- **OStim** scenes carry few tags. Match on **`actions`** (for example `blowjob`, `deepthroat`, `femalemasturbation`, `malemasturbation`, `vaginalsex`, `handjob`, `cunnilingus`, `boobjob`). `role` decides whether the face goes to the one doing the action or the one receiving it. The action names are in the scene files under `Data/SKSE/Plugins/OStim/scenes/`.
- **SexLab** animations carry **tags** (`Blowjob`, `Oral`, `Masturbation`, `Solo`, `Doggy` ...). Match on `tags`. SexLab does not say who does what, so use `sex` to choose (for example `sex = "female"` for the woman in an oral scene).

Names are not case sensitive.

### Not sure what a scene is called?

Start the scene and open **FaceLab > Scenes**. Under "What the rules did lately" FaceLab prints every scene it sees, with its tags and the number of actors, even when no rule fits, and then which rule it used. The same lines are in `FaceLab.log` (`Scenes: ...`). Copy the tag you want into a rule.

### Three complete examples

A face for someone giving oral, OStim:

```toml
[[rule]]
name    = "Giving a blowjob"
frameworks = ["ostim"]
actions = ["blowjob", "deepthroat"]
preset  = "MyPack.Blowjob"
```

A face for anyone in a SexLab masturbation scene:

```toml
[[rule]]
name    = "Solo"
frameworks = ["sexlab"]
tags    = ["masturbation", "solo"]
preset  = "MyPack.Solo"
```

A shared moaning face for every male orgasm, in any scene:

```toml
[[rule]]
name   = "Male orgasm"
on     = "orgasm"
tags   = ["*"]
sex    = "male"
preset = "MyPack.Orgasm.Male"
hold   = 6
```

---

## 4. If it does not work

Open **FaceLab > Scenes** first. It shows whether OStim and SexLab were found, how many rules loaded, any problem in your files (a typo in a preset name, a rule with nothing to match) and what the rules did recently.

| Symptom | Check |
|---|---|
| "0 rule(s) loaded" | The files must be in `Data/SKSE/Plugins/FaceLab/Scenes/` and end in `.toml`. Press Reload. |
| A rule shows a problem | Read the line: it names the file and the rule. |
| Scene lines appear but no rule is used | The tag or action in your rule does not match the line printed for that scene. Copy it from the log. |
| The face is used but nothing changes | The preset name is wrong (Scenes page: "no preset called ..."), or another mod's face has a higher `priority`. |
| The face is right but there is no tongue | The tongue line under Show the tongue on the Face page says why (no tongue mod, too old, switched off, no tongue models). |
| It works for OStim but not SexLab | SexLab support follows SexLab's own events; check the Scenes log for a "sexlab scene" line. If there is none, SexLab is not sending the scene events (an old or modified SexLab). |

When you ask for help, send `Documents/My Games/Skyrim Special Edition/SKSE/FaceLab.log` and, for tongues, the tongue mod's own log.
