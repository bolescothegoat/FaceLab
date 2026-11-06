# FaceLab for modders

FaceLab plays RaceMenu-style face morphs (the head's `EXPR_*` targets, weights up to 3.0) on any loaded actor.
They are the same expressions you see in RaceMenu's expression sliders, applied live, held in normal gameplay,
and compatible with blinking, lip sync and SMP physics.

FaceLab works on the **High Poly Head**, the **vanilla head** (with Expressive Facial Animation, which ships the vanilla head's morph files loose), **CoTR** (original low poly head only; the high poly head is not supported)
and **UBE** (female), male and female where the head exists. It reads the morph files of whichever of these morph mods the player has installed;
it ships none. Heads it has no morph data for report "not supported" instead of failing.
Runs on Skyrim SE 1.5.97 and AE 1.6.x / 1.7.x (one DLL, Address Library).

You ask for a **named face**. FaceLab, the player and other mods decide the rest.

## 1. Ship presets

`Data/SKSE/Plugins/FaceLab/Presets/<YourMod>.toml`

```toml
[[preset]]
name = "MyMod.Kissing.Pleased"
weights = { MoodHappy = 120, BigAah = 40 }      # weight x 100: 100 = 1.0, up to 300

[[preset]]
name = "MyMod.Kissing.Displeased"
weights = { MoodDisgusted = 150, BrowDownLeft = 80, BrowDownRight = 80 }
```

Names are `Mod.Context.Disposition` (disposition = Displeased / Neutral / Pleased). If a requested
`MyMod.Kissing.Displeased` does not exist FaceLab falls back to `MyMod.Kissing.Neutral`, then `MyMod.Displeased`, then
`MyMod.Neutral`, so you only have to author the faces you care about.

Players edit and re-save any preset in-game: **SKSE Menu Framework > FaceLab > Presets** (load into the sliders,
tweak, save back to its own file). Channel names: see the Face page, or `ChannelName()` below.

## 2. Ask for a face

**SKSE plugin (C++):** copy `FaceLabAPI.h`, no link library needed.

```cpp
if (const auto* fl = FaceLabAPI::Get()) {   // nullptr: FaceLab not installed -> do whatever you did before
    fl->ApplyPreset(actor->GetFormID(), "MyMod", "MyMod.Kissing.Pleased", 0.5f, 10);
    // ...later
    fl->Clear(actor->GetFormID(), "MyMod", 0.5f);
}
```

**Papyrus:**

```papyrus
FaceLab.ApplyFace(akActor, "MyMod", "MyMod.Kissing.Pleased", 0.5, 10)
FaceLab.ClearFace(akActor, "MyMod", 0.5)
```

## 3. Rules

* `owner` is your mod's name. Your face is a layer you own; `Clear` removes only your layer.
* Several layers on one actor: the **highest priority** wins (ties: the newest). 10 = normal, 50+ = must win.
  When the winner is cleared the next layer shows. Mods never need to know about each other.
* Calls are safe from any thread and are applied on the game thread.
* Faces are **not saved**. A new game or a load drops every layer, so nothing can get stuck.
* Fade is in seconds. 0 = instant.
* `ApplyWeights` builds a face without a preset file: `{channel, value}` pairs, value 0 - 3.0.
* Preset files are read at game start. The in-game menu has a reload button.
