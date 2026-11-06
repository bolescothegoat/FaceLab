// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Bolescothegoat - FaceLab. See LICENSE.

#pragma once

#include "PCH.h"

// The shared face service: mods ask for a face on an actor as a named layer; the highest-priority layer is what the actor shows.
namespace FaceLab::Api
{
	using Weights = std::vector<std::pair<std::string, float>>;  // channel -> 1.0 = full

	// all of these queue their work to the game thread
	void ApplyWeights(RE::FormID id, std::string owner, Weights w, float fade, int priority);
	bool ApplyPreset(RE::FormID id, std::string owner, std::string_view preset, float fade, int priority);  // false: no such preset
	void Clear(RE::FormID id, std::string owner, float fade);
	// Someone is speaking with the mouth (lip sync): the face's Mouth-channel morphs fade down by `amount` (1 = fully out) so the speech drives the mouth.
	// Every asker is remembered by owner; the biggest amount counts. amount 0 gives the mouth back.
	void SetMouthYield(RE::FormID id, std::string owner, float amount, float fade);
	void ClearAll();  // new game / load

	bool RegisterPapyrus(RE::BSScript::IVirtualMachine* vm);
}

namespace FaceLab
{
	inline constexpr const char* kLabOwner = "FaceLab.Lab";  // the layer the menu / hotkey sliders use
}
