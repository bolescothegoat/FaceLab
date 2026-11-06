// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Bolescothegoat - FaceLab. See LICENSE.

#pragma once

#include "PCH.h"

#include <map>

// FaceLab: RaceMenu-style face morphs (the head's EXPR_* targets, uncapped) on the player or the actor you last looked at.
namespace FaceLab
{
	struct Channel
	{
		const char* name;   // "BigAah": the morph is "EXPR_BigAah"
		const char* group;  // "Mouth", "Eyes and brows", "Emotions"
	};

	struct Settings
	{
		bool enabled = true;
		int  kbKey = -1;
		int  kbMod = -1;
		int  padKey = 0x4000;  // X
		int  padMod = 0x0002;  // D-pad Down
		int  lerpMs = 500;     // fade time of a face
		int  labHold = 20;     // seconds a face you applied from the lab stays before it lets go (0 = until Reset). It outranks every mod while it is on.
		int  target = 0;       // 0 = the player, 1 = the actor last looked at
		bool live = false;     // apply a slider as soon as it moves
		bool bump = false;     // also patch the second base copy (FMD); the FOD copy is always patched
		bool debug = false;    // show the Probe / Poke tools and write the measurement log lines
		std::vector<int> values;  // one per channel, 0-300 (weight x 100)
		std::map<std::string, int> extras;  // morphs only some heads have (CoTR Smile001, UBE Ahegao ...), by name, 0-300; shown in the menu under the standard channels
	};

	const std::vector<Channel>& Channels();
	Settings&                   GetSettings();
	void                        LoadConfig();
	bool                        SaveConfig();

	std::string TargetName();
	RE::Actor*  TargetActor();  // main thread
	// The extra morph names of the target's head, read on the game thread and handed over here (the menu runs on another thread). Ask often; it refreshes about once a second.
	struct ExtraName
	{
		std::string name;
		int         parts{ 0 };
		int         partsTotal{ 0 };
	};
	bool ExtrasSnapshot(std::string& family, std::vector<ExtraName>& out);
	void        ApplyAll();
	void        ApplyOne(int index);
	void        Reset();
	void        OnHotkey();  // toggles: apply the sliders / reset
	bool        Applied();
	void        StartCrosshairWatch();
	void        ProbeModel();  // debug: find where the engine keeps the face's base vertices
	void        PokeTest();    // debug: lift the face mesh 40 units for 8 s
	void        LoadPreset(int which);  // 0 clear, 3 BigAah 300
}
