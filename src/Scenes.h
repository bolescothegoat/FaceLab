// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Bolescothegoat - FaceLab. See LICENSE.

#pragma once

#include "PCH.h"

// Scene rules: when an OStim or SexLab scene starts, changes or reaches an orgasm, FaceLab applies the preset a rule names to the actors it fits.
// The rules live in Data/SKSE/Plugins/FaceLab/Scenes/*.toml (see docs/SCENE_RULES.md); FaceLab ships none, a pack (or you) supplies them.
namespace FaceLab::Scenes
{
	void Init();       // kDataLoaded: read the rules and listen for OStim / SexLab events
	void Reset();      // new game / load: forget which actors a scene gave a face (the faces themselves are dropped by the API)
	void LoadRules();  // read Data/SKSE/Plugins/FaceLab/Scenes/*.toml again (the menu's Reload button)

	struct Info
	{
		int                      rules{ 0 };
		bool                     ostim{ false };    // OStim.dll is loaded
		bool                     sexlab{ false };   // SexLab.esm is installed
		std::vector<std::string> problems;          // what could not be read from the rule files
		std::vector<std::string> log;               // the latest things the rules did, newest last (for the menu)
	};
	Info GetInfo();  // any thread
}
