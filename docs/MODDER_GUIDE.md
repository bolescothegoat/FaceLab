# FaceLab - guide for modders

FaceLab lets your mod put a **real expression** on an actor (RaceMenu-style head morphs, weights up to 3.0, live, faded, layered with other mods)
without writing any face code. You ship a small text file of named faces, then ask for one by name from Papyrus or from an SKSE plugin.

* The plugin (`FaceLab.dll`) is GPL-3.0-or-later.
* The two files you copy, `FaceLabAPI.h` and `FaceLab.psc`, are **MIT**: using them does **not** put your mod under the GPL.
* Your mod needs **no link library and no hard dependency**: if FaceLab is not installed your calls do nothing and you keep your old behaviour.

## 1. The idea

You ask for a face; FaceLab decides what shows. A face is a **layer owned by your mod** on an actor.

* Several mods can have a layer on the same actor. The **highest priority wins** (ties: the newest). 10 = normal, 50+ = must win.
* When the winning layer is cleared, the next one shows. You never need to know about the other mods.
* `Clear` removes only **your** layer.
* Faces are **never saved**. A new game or a load drops every layer, so nothing can get stuck.
* Fades are in seconds (0 = instant).
* Every call is safe from any thread and is applied on the game thread.

## 2. Ship named faces (presets)

Put a file in `Data/SKSE/Plugins/FaceLab/Presets/<YourMod>.toml`:

```toml
[[preset]]
name = "MyMod.Kissing.Pleased"
weights = { MoodHappy = 120, BigAah = 40 }       # weight x 100: 100 = 1.0, up to 300

[[preset]]
name = "MyMod.Kissing.Displeased"
weights = { MoodDisgusted = 150, BrowDownLeft = 80, BrowDownRight = 80 }
```

* Names are `Mod.Context.Disposition`. Disposition is `Displeased`, `Neutral` or `Pleased`.
* **Fallback:** if `MyMod.Kissing.Displeased` does not exist FaceLab uses `MyMod.Kissing.Neutral`, then `MyMod.Displeased`, then `MyMod.Neutral`. You only author the faces you care about.
* A name can carry a variant suffix (for example `MyMod.Facial.Pleased.1`, `.2`). FaceLab does not pick variants for you: the name you ask for is the name it looks up. Choose at random in your own code.
* Names are matched without case.
* Presets are read at game start. Players can edit and re-save any preset in the in-game menu (Presets page), and the menu has a reload button.
* **Own your presets:** use your mod's name as the first part so nobody else overwrites it.

### Channel names

The channels are the standard RaceMenu expression morphs plus a few FaceLab signals:

| Group | Channels |
|---|---|
| Mouth | `Aah` `BigAah` `BMP` `ChJSh` `DST` `Eee` `Eh` `FV` `I` `K` `N` `Oh` `OohQ` `R` `Th` `W` |
| Eyes and brows | `BlinkLeft` `BlinkRight` `BrowDownLeft` `BrowDownRight` `BrowInLeft` `BrowInRight` `BrowUpLeft` `BrowUpRight` `LookDown` `LookLeft` `LookRight` `LookUp` `SquintLeft` `SquintRight` |
| Emotions | `DialogueAnger` `DialogueFear` `DialogueHappy` `DialogueSad` `DialogueSurprise` `DialoguePuzzled` `DialogueDisgusted` `MoodAnger` `MoodFear` `MoodHappy` `MoodSad` `MoodSurprise` `MoodPuzzled` `MoodDisgusted` `CombatAnger` `CombatShout` |
| Each eye separately | `EyesWide` `EyeLeftWide` `EyeRightWide` `EyesCross` `EyesApart` `EyeLeftLook{Left,Right,Up,Down}` `EyeRightLook{Left,Right,Up,Down}` |
| True eye rotation (10 degrees per 1.0) | `EyeLeftTurn{Left,Right,Up,Down}` `EyeRightTurn{Left,Right,Up,Down}` |
| Signals (no morph) | `TongueOut` `OralMode` `MouthAssist` |

The **ground truth is the Face page of the in-game menu** (and `ChannelName()` in the C++ API): use the exact names there. Unknown names are skipped.

Signal channels:

* `TongueOut` - the face asks a tongue mod for the tongue (through `GetTongue`): 1.0 = fully out, above 1 = further. Such a mod can have OStim equip a tongue if the actor has none.
* `OralMode` - a marker: 50 or more = oral scene. The mouth belongs to OStim then (emotion channels leave the lips alone, the mouth channels give way as OStim's own mouth opens).
* `MouthAssist` - a BigAah opening that fades out as the natural mouth opens (full effect shut, none from 0.45 open).
* `EyesCross` - cross-eyed. **The Fourth Wall** lets go of the eyes in proportion.

### Which heads

FaceLab drives the High Poly Head, the vanilla head (with Expressive Facial Animation), the original low poly CoTR head and UBE (female), male and female where the head exists.
Call `IsSupported` before showing something that only makes sense on a real face.

## 3. Ask for a face

### Papyrus

Copy `FaceLab.psc` (MIT) from the `Developer` folder or the `api` folder.

```papyrus
Scriptname MyModScript extends Quest

Function Show(Actor akActor)
    If FaceLab.ApplyFace(akActor, "MyMod", "MyMod.Kissing.Pleased", 0.5, 10)
        ; a face is showing
    EndIf
EndFunction

Function Hide(Actor akActor)
    FaceLab.ClearFace(akActor, "MyMod", 0.5)
EndFunction
```

| Function | Notes |
|---|---|
| `Bool ApplyFace(Actor, String owner, String preset, Float fade = 0.5, Int priority = 10)` | false if no such preset (after the fallback) |
| `Function ClearFace(Actor, String owner, Float fade = 0.5)` | removes your layer only |
| `Bool HasPreset(String preset)` | true if it exists after the fallback |
| `Bool IsSupported(Actor)` | the actor must be loaded |
| `Function SetMouthYield(Actor, String owner, Float amount, Float fade = 0.1)` | see "Lip sync" |
| `Int ApplyWeightsText(Actor, String owner, String "Channel=1.5;Other=0.5", Float fade, Int priority)` | test helper: a face from text, values 0 - 3.0 |
| `Int SetGazeText` / `Function ClearGaze` | test helpers for the gaze layer |

### SKSE plugin (C++)

Copy `FaceLabAPI.h` (MIT). No link library: it finds `FaceLab.dll` at run time.

```cpp
if (const auto* fl = FaceLabAPI::Get()) {          // nullptr: FaceLab not installed -> do whatever you did before
    fl->ApplyPreset(actor->GetFormID(), "MyMod", "MyMod.Kissing.Pleased", 0.5f, 10);
    // ... later
    fl->Clear(actor->GetFormID(), "MyMod", 0.5f);
}
```

* A miss from `Get()` is not cached: plugins load in any order, so ask again after `kDataLoaded`.
* The interface has a `version` (6 today). **New functions are only ever added at the end**, so an old caller keeps working. Check `version >= N` before using a function added in version N.
* Every function returns 1 when the request was accepted.

| API version | Adds |
|---|---|
| 1 | `ApplyPreset` `ApplyWeights` (your own face from `{channel, value}` pairs, 0 - 3.0) `Clear` `HasPreset` `IsSupported` `ChannelCount` `ChannelName` |
| 2 | `SetMouthYield` |
| 3 | `GetMouthOpen` |
| 4 | `GetEyeControl` |
| 5 | `GetTongue` |
| 6 | `SetGaze` `ClearGaze` |

## 4. Lip sync and speech: SetMouthYield

If your mod plays speech or a lip-synced sound, FaceLab's held open mouth would fight it. Call
`SetMouthYield(actor, "MyMod", 1.0, 0.1)` when the line starts: FaceLab fades its **mouth** channels out by that amount. Call it again with `0` when the line ends.
Several askers are fine, the biggest amount counts. (lip-sync mods do this.)

## 5. Reading state back

* `GetMouthOpen(formID)` - how open the mouth is right now **as FaceLab draws it** (the biggest of Aah / BigAah / Eh / Oh, smoothed, scaled down by any yield). 1.0 = a full RaceMenu weight. -1 = FaceLab has no face on this actor. Cheap enough to call every frame.
* `GetTongue(formID)` - the `TongueOut` weight of the face on the actor. -1 = no face.
* `GetEyeControl(formID)` - 0 to 1: how cross-eyed the shown face is (1 from a weight of 0.1 up). Mods that steer the eyes themselves should let go in proportion. -1 = no face.

## 6. The gaze layer (eyes on top of a face)

`SetGaze` adds eye channels **on top of** whatever face is showing, or shows them alone. It is not a face: it never competes with other layers and priorities do not apply.

* Only eye channels are accepted (`Look*`, `EyeLeft*` / `EyeRight*` Look and Turn, `EyesCross`, `EyesWide`, `Blink`, `Squint`, `Brow` ...). Anything else in the list is ignored.
* Several owners add up (0 - 3 per channel).
* **Each call replaces your previous gaze**; channels you leave out go back to zero. Call it every frame if you steer the eyes live, it is cheap.
* `ClearGaze` removes yours.
* It does not count as a face for `GetEyeControl`.

## 7. Good manners

* Use **your mod's name** as `owner` on every call, always the same string.
* Use priority **10** unless something truly must win. 50+ outranks other mods; the player's lab face is above everything.
* **Always `Clear`** when your scene ends.
* Do not rely on a face surviving a save/load: ask again after the game loads.
* If FaceLab is missing, fall back to what you did before (or do nothing). Never make FaceLab a hard master.
* Do not depend on a particular preset of another mod existing. Check `HasPreset`.

## 8. Testing your presets without writing code

1. Open the menu: **SKSE Menu Framework > FaceLab > Presets**, pick your preset and press **Load and apply to the target**.
2. On the **Face** page, change the sliders and press **Save the sliders over this preset** to write them back to your file. This is the fastest way to author a face.
3. In game, `FaceLab.log` (Documents/My Games/Skyrim Special Edition/SKSE) names the head family each actor was matched to. Tick **Debug tools** on the Face page for more detail.

## 9. Licence

`FaceLabAPI.h` and `FaceLab.psc` are MIT (see `LICENSE-MIT.txt`): copy them into your own project freely.
The FaceLab plugin and everything else in the repository is GPL-3.0-or-later. A mod that only calls FaceLab through these two files is a separate program and is not covered by the GPL.
