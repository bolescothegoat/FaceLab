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
		std::string language = "auto";  // menu language: "auto" = the game's language, or a file name in Data/SKSE/Plugins/FaceLab/Languages
		int  kbKey = -1;
		int  kbMod = -1;
		int  padKey = 0x4000;  // X
		int  padMod = 0x0002;  // D-pad Down
		int  lerpMs = 500;     // fade time of a face
		int  labHold = 20;     // seconds a face you applied from the lab stays before it lets go (0 = until Reset). It outranks every mod while it is on.
		int  target = 0;       // 0 = the player, 1 = the actor last looked at
		bool holdUntilReset = false;  // a lab face never lets go by itself (labHold ignored)
		int  tKbKey = -1;     // tongue hotkey: same layout as the face hotkey
		int  tKbMod = -1;
		int  tPadKey = 0;
		int  tPadMod = 0;
		bool scenes = true;    // scene rules (Data/SKSE/Plugins/FaceLab/Scenes/*.toml) may apply faces
		int  tongueOut = 100;  // Tongue assist: how far out (100 = fully, up to 300)
		int  shadowStrength = 25;  // Eye assist: opacity (%) of the add-on eyes' black shadow shell
		int  vertLimit = 26;   // Eye assist: the most an add-on eyeball turns up or down, in degrees
		int  ubeSide = 0;      // UBE eyes: % change of the per-eye sideways look from the calibrated look (0 = as shipped)
		int  ubeDown = 0;      // UBE eyes: % change of the per-eye look down
		int  ubeUp = 0;        // UBE eyes: % change of the per-eye look up
		int  lidFollow = 150;   // Eyelid assist: percent of a vertical look the head's lids follow (heads with an add-on eye pack only)
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
	void        ToggleTongue();  // the Tongue assist on <-> off (the Assists button and the tongue hotkey)
	void        SetTongue(bool on);  // the Tongue assist: TongueOut on the target ON TOP of whatever face it shows (a preset, another mod's face, none)
	bool        TongueOn();
	RE::FormID  TargetFormID();  // the target's form id (the player's, or the actor last looked at; 0 = nobody yet). Any thread.
	// Debug: hide a face shape of the target live (to find which one draws something wrong)
	bool        ShapeSnapshot(std::vector<std::string>& names);
	bool        ShapeHidden(const std::string& name);
	void        SetShapeHidden(const std::string& name, bool hidden);
	bool        TongueModInstalled(); // a tongue mod that answers FaceLab is loaded (found by an export name, never by its file name)
	int         TongueModStatus(unsigned int formID);  // what that mod makes of the tongue request for this character: -1 off, -2 no tongue armor, 0 not seen, 1 skipped, 2 putting one on, 3 has one; -99 = no answer
	void        ApplyTongueAmount();  // the 'how far out' slider moved while the tongue is out
	bool        Applied();
	void        StartCrosshairWatch();
	void        ProbeModel();  // debug: find where the engine keeps the face's base vertices
	void        PokeTest();    // debug: lift the face mesh 40 units for 8 s
	void        LoadPreset(int which);  // 0 clear, 3 BigAah 300
}
