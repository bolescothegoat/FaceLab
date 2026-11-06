// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Bolescothegoat - FaceLab. See LICENSE.

#pragma once

// FaceGen .tri (FRTRI003) reader, no game code in it so it can be tested on its own (see tests/).
// A tri holds the head's base vertices and a list of morph targets: int32 nameLength (with the zero), the name, float scale, int16 x 3 per vertex.
// The shipped heads name their morphs two ways: "EXPR_BigAah" (High Poly Head, CoTR, RaceMenu's expression sliders) or plain "BigAah" (vanilla, EFA, UBE).
// Both come out as the plain name.

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace FaceLab::Tri
{
	struct File
	{
		int                                                    nv{ 0 };
		std::string                                            path;
		std::vector<float>                                     base;    // 3 floats per vertex (empty when the file is too short to hold them)
		std::vector<std::pair<std::string, std::vector<float>>> morphs;  // plain name -> offsets, 3 floats per vertex (int16 x scale), in file order
	};

	// Only the vertex count of the file (12 bytes read). false = not a FRTRI003 file / cannot be opened.
	bool ReadVertexCount(const std::string& path, int& nv);

	// The whole file. `err` says why it failed.
	bool Read(const std::string& path, File& out, std::string& err);

	// "EXPR_BigAah" / "EXPR1_BigAah" -> "BigAah" (prefix matched without regard to case), anything else unchanged.
	std::string PlainName(const std::string& morphName);
}
