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
		PadMod
	};

	void    Install();
	void    BeginCapture(Capture which);
	Capture CurrentCapture();

	std::string KeyName(int dik);
	std::string PadName(int mask);
}
