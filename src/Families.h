// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Bolescothegoat - FaceLab. See LICENSE.

#pragma once

// Head families FaceLab can drive. A family = one kind of head and sex: the head itself plus its face parts (eyes, mouth, brows, lashes ...).
// Every part is a list of morph files (.tri) relative to Data/Meshes/, first file first. Files that are not installed are skipped.
// Files with the same vertex count are the same face part: their morphs are merged (the first file wins a name), when their base vertices agree.
// Nothing here is bundled: the morph files come from whatever morph mod the player has installed (High Poly Head, EFA, CoTR, UBE ...).
//
// Which family an actor has is decided from the vertex counts of its face shapes (the head's count is the strong sign), see Morph.cpp.

#include <string>
#include <vector>

namespace FaceLab::Fam
{
	struct Def
	{
		const char*              name;    // for the log and the menu
		bool                     female;
		std::vector<const char*> files;   // first = the head itself
		bool                     followHead{ false };  // the mouth, brows, lashes and eye shadows take their motion from the head's nearest vertices instead of from their own morph
		                                                //  files (UBE: the head's expression file is much stronger than the face parts' own, and has names they lack)
	};

	const std::vector<Def>& All();

	// Names a preset may use that a head does not have under that exact name: the nearest morph it does have, in order of preference.
	// ("Oh" on a vanilla head has no morph of its own: OohQ is the closest). Empty when there is none.
	const std::vector<const char*>& Aliases(const std::string& name);
}
