// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Bolescothegoat - FaceLab. See LICENSE.

#pragma once

#include "PCH.h"

namespace FaceLab::Input
{
	// which binding the next pressed key / button should fill (the menu's "press a key" buttons)
	enum class Capture
	{
		None,
		KbKey,
		KbMod,
		PadKey,
		PadMod,
		TongueKbKey,
		TongueKbMod,
		TonguePadKey,
		TonguePadMod
	};

	void    Install();
	void    BeginCapture(Capture which);
	Capture CurrentCapture();
	// Call from the menu every frame while a binding may be waiting. The game's input events are not reliable while a menu window is open (the window takes the keyboard), so this
	// also looks at the keyboard and the gamepad itself and finishes the capture the moment a key / button goes down.
	void PollCapture();

	std::string KeyName(int dik);
	std::string PadName(int mask);
}
