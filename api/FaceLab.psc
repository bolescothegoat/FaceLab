; SPDX-License-Identifier: MIT
; Copyright (c) 2026 Bolescothegoat - FaceLab API. See LICENSE-MIT.txt (this file only; the FaceLab plugin is GPL-3.0-or-later).

Scriptname FaceLab Native Hidden
{FaceLab - RaceMenu-style face morphs (uncapped), as named presets. Native functions implemented by FaceLab.dll.}

; Show a named face on an actor. asOwner is your mod's name: the face is a layer owned by you, and ClearFace only removes your layer.
; asPreset comes from any mod's Data/SKSE/Plugins/FaceLab/Presets/*.toml, e.g. "MyMod.Kissing.Pleased". A missing Displeased/Pleased
; falls back to the same name with Neutral, then MyMod.Pleased, then MyMod.Neutral.
; aiPriority: the highest priority layer on an actor is what it shows (10 = normal, 50+ = must win). Returns false if no such preset.
Bool Function ApplyFace(Actor akActor, String asOwner, String asPreset, Float afFadeSeconds = 0.5, Int aiPriority = 10) global native

; Developer / test helper: show any face from text, "Channel=1.5;OtherChannel=0.5" (RaceMenu units 0 - 3, channel names as in the FaceLab menu). Unknown names are skipped.
; Returns how many channels were accepted. Same layer rules as ApplyFace (owner, priority, fade). ClearFace removes it.
Int Function ApplyWeightsText(Actor akActor, String asOwner, String asWeights, Float afFadeSeconds = 0.0, Int aiPriority = 10) global native

; Developer / test helper for the GAZE layer: eye channels as text, "Channel=1.5;OtherChannel=0.5", added ON TOP of the face the actor shows (never replaces it). Returns how many were accepted.
Int Function SetGazeText(Actor akActor, String asOwner, String asWeights, Float afFadeSeconds = 0.05) global native
Function ClearGaze(Actor akActor, String asOwner, Float afFadeSeconds = 0.1) global native

; Remove your layer. The next layer down (another mod's) shows, or the plain face if there is none. Faces are never saved in the save game.
Function ClearFace(Actor akActor, String asOwner, Float afFadeSeconds = 0.5) global native

; True if the preset exists (after the fallback above).
Bool Function HasPreset(String asPreset) global native

; Speech is driving the mouth (lip sync): fades the face's mouth morphs out by afAmount (0..1) so they do not fight it. Call again with 0 when done.
Function SetMouthYield(Actor akActor, String asOwner, Float afAmount, Float afFadeSeconds = 0.1) global native

; True if this actor's head has morph data FaceLab can drive (High Poly Head, vanilla, original CoTR low poly or UBE, when its morph files are installed). Actor must be loaded.
Bool Function IsSupported(Actor akActor) global native
