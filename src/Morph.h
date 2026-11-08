// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Bolescothegoat - FaceLab. See LICENSE.

#pragma once

#include <set>

#include "PCH.h"

// Our own morph application, the way RaceMenu's skee does it, but live: the head's EXPR_* morph targets (read from the .tri files the High Poly Head ships)
// are added, weight x offset, to the face shapes' vertex data every frame. No cap on the weight (3.0 works), no fade of its own beyond what we ask for.
namespace FaceLab::Morph
{
	// the weights to show on an actor, by morph name without the EXPR_ prefix ("BigAah"), 1.0 = a full weight
	void Set(RE::Actor* actor, const std::vector<std::pair<std::string, float>>& weights, float fadeSeconds);
	// The gaze layer: eye channels (Look*, EyeLeft*/EyeRight*, EyesCross ...) added on top of the face that is showing, or alone. Any thread, cheap enough for every frame.
	// The weights replace the previous gaze; channels not listed go back to zero. It does not count as the face for EyeControl / MouthOpen / Tongue.
	void SetGaze(RE::FormID id, const std::vector<std::pair<std::string, float>>& weights, float fadeSeconds);
	void ClearGaze(RE::FormID id, float fadeSeconds = -1.0f);
	void Clear(RE::Actor* actor, float fadeSeconds = -1.0f);  // takes the offsets back out (fade < 0: keep the fade of the last Set)
	void ClearAll();                                           // every actor, at once (new game / load)
	bool Supported(RE::Actor* actor);
	// The morph names this actor's head has beyond FaceLab's standard channels (a CoTR head's Smile001, a UBE head's Ahegao ...). parts = how many of the head's other face parts
	// (mouth, lashes, brows ...) have the same name, so the whole face follows; 0 = only the head moves. false = no supported head, or the files are still being read (ask again).
	struct NameInfo
	{
		std::string name;
		int         parts{ 0 };
		int         partsTotal{ 0 };
	};
	bool HeadNames(RE::Actor* actor, std::string& family, std::vector<NameInfo>& out);
                          // main thread: does this actor's head have EXPR morph data we can drive?
	bool Active(RE::Actor* actor);
	int  SupportState(RE::Actor* actor);  // 1 = a head we have morphs for, 0 = another head (vanilla ...), -1 = no face built yet
	// How open the mouth morphs (Aah, BigAah, Eh, Oh) are right now, smoothed and including any speech yield (1.0 = a full RaceMenu weight). -1 = no face of ours on this actor. Any thread.
	float MouthOpen(RE::FormID id);
	// How much the face shown on this actor is cross-eyed (the EyesCross channel), 0..1, from the live faded weight. -1 = no face of ours.
	float EyeControl(RE::FormID id);
	// The TongueOut weight of the face shown on this actor (live, faded), 0 or more; -1 = no face of ours. Any thread.
	float Tongue(RE::FormID id);
	// Milliseconds since anything last read Tongue() (a tongue mod does, every tick); -1 = never. Tells the menu whether a tongue mod is listening at all. Any thread.
	long long TonguePolledMsAgo();
	void Poke(RE::Actor* actor, float zUnits, float seconds);  // test: shift the whole face mesh up by zUnits for a few seconds
	// Debug: the names of the actor's face shapes, and hiding some of them (to find which shape draws something wrong). Game thread.
	std::vector<std::string> FaceShapeNames(RE::Actor* actor);
	void CullFaceShapes(RE::Actor* actor, const std::set<std::string>& hide);
	void SetUbeEyes(float side, float down, float up);  // UBE eye files: strength of the per-eye look channels (sideways, down, up); 1 = the unit channel (a 10 degree turn per 1.0 sideways / down, the shipped morph up)
	void SetUbeAdjust(int sidePct, int downPct, int upPct);  // the menu's view of SetUbeEyes: % change from the calibrated UBE look (0 = as shipped)
	void SetShadowStrength(float k);   // 0..1: opacity of the add-on eyes' black shadow shell (Advanced Eyes Addon: its gradient is steep, so a small lid shift shows a much darker band)
	void SetVertLimit(float degrees);  // add-on eyes: the most an eyeball may turn up or down in total (its dark rim shows in the lid opening beyond that)
	void SetLidFollow(float k);     // 0..1: on heads with an add-on eye pack, how much the head's eyelids follow a look up / down (the eye turns further than a stock eye, so full lids droop over it)
	void SetBump(bool on);          // after writing, also increase the mesh's frameCount (the engine's "vertex data changed" counter?)
	void StartFrameLoop();         // once, after the game data is loaded
}
