// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Bolescothegoat - FaceLab. See LICENSE.

#pragma once

#include "PCH.h"

// Named faces that any mod can ship: Data/SKSE/Plugins/FaceLab/Presets/<anything>.toml
//
//   [[preset]]
//   name = "MyMod.Masturbation.Pleased"
//   weights = { BigAah = 177, Eee = 102, MoodSad = 189 }     # weight x 100, 0 - 300
//
// Names are "Mod.Context.Disposition" by convention; the last part may be Displeased / Neutral / Pleased (see Resolve).
namespace FaceLab::Presets
{
	struct Preset
	{
		std::string                                  name;
		std::string                                  file;     // the file name it lives in, e.g. "MyMod.toml"
		std::vector<std::pair<std::string, int>>     weights;  // channel -> weight x 100
	};

	void                 Load();  // (re)scan the folder
	std::vector<Preset>  All();   // copy, sorted by name
	std::optional<Preset> Find(std::string_view name);     // exact name, case-insensitive
	// Exact name first. When it ends in a disposition and is missing, falls back: same name with Neutral, then Mod.Disposition, then Mod.Neutral.
	std::optional<Preset> Resolve(std::string_view name);
	bool                 Save(const Preset& p);  // add or replace in p.file (empty = "User.toml") and rewrite that file
	std::string          Folder();
}
