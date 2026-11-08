// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Bolescothegoat - FaceLab. See LICENSE.

#pragma once

// Loads the face parts of one head family (see Families.h) from the morph files that are installed. No game code in it (tested on its own, see tests/).

#include "Families.h"

#include <functional>
#include <map>
#include <string>
#include <vector>

namespace FaceLab::Fam
{
	// One face part: the head itself, the eyes, the mouth ... with every morph the installed files have for it.
	struct Part
	{
		int                                       nv{ 0 };
		std::vector<float>                        base;    // 3 floats per vertex
		std::vector<std::string>                  files;   // full paths of the files merged into this part, first one first
		std::vector<std::string>                  rels;    // the same files as written in Families.h
		std::map<std::string, std::vector<float>> morphs;  // plain name -> offsets (3 floats per vertex)
		// Add-on eye packs (Advanced Eyes Addon) draw only a front cap of the eyeball: turn it too far and its open edge swings into the lid opening (a dark wedge). The largest
		// weight each per-eye look channel may have, so that gain x weight stays inside a safe angle. 0 = no limit. [0] sideways, [1] up, [2] down.
		float eyeWMax[3]{ 0.0f, 0.0f, 0.0f };
		bool  ube{ false };  // UBE's own eye files: the per-eye look channels are unit strength here; FaceLab multiplies them at run time (the UBE eye sliders)
		float lookDeg[2]{ 0.0f, 0.0f };  // degrees one unit of the per-eye LookUp / LookDown turns the eyeball (the vertical limit adds these to the TurnUp / TurnDown ones, 10 degrees a unit)
	};

	// rel (as in Families.h, relative to the Meshes folder) -> full path of the file that exists, or "" when it is not installed
	using Resolver = std::function<std::string(const std::string& rel)>;

	// What is installed, without reading morph data: the vertex count of every file that exists (the head's is first). For finding out which family an actor has.
	struct Header
	{
		int         nv{ 0 };
		std::string path;
	};
	std::vector<Header> Headers(const Def& def, const Resolver& resolve);

	// Reads everything. Files with the same vertex count whose base vertices agree are one part (a name present in several files comes from the first);
	// two different meshes with the same count stay two parts. Eye morphs per side (EyeLeftLook*, EyeRightLook*) and the true-rotation ones (EyeLeftTurn* ...) are added.
	// `log` receives one line per problem or notable decision.
	std::vector<Part> Load(const Def& def, const Resolver& resolve, std::vector<std::string>* log);

	// One file as a part of its own (face parts that are in no family list: beards ...). "eyes" in the path marks an eye file.
	bool LoadFile(const std::string& fullPath, Part& out, std::string& err);
}
