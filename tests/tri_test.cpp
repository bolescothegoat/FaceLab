// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Bolescothegoat - FaceLab. See LICENSE.

// Offline test of the .tri reader: parses the files given on the command line and prints vertex count, morph count and a few names.
#include "TriFile.h"
#include <chrono>
#include <cstdio>
#include <set>

int main(int argc, char** argv)
{
	int bad = 0;
	for (int i = 1; i < argc; ++i) {
		FaceLab::Tri::File f;
		std::string        err;
		const auto         t0 = std::chrono::steady_clock::now();
		const bool         ok = FaceLab::Tri::Read(argv[i], f, err);
		const double       ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
		int                hv = 0;
		const bool         hdr = FaceLab::Tri::ReadVertexCount(argv[i], hv);
		std::set<std::string> uniq;
		for (auto& m : f.morphs) {
			uniq.insert(m.first);
		}
		std::printf("%s | nv %d (header %d) | morphs %zu unique %zu | base %s | %.0f ms%s%s\n", ok ? "OK  " : "FAIL", f.nv, hdr ? hv : -1, f.morphs.size(), uniq.size(),
			f.base.size() == static_cast<std::size_t>(f.nv) * 3 ? "yes" : "NO", ms, err.empty() ? "" : " | ", err.c_str());
		std::printf("     %s\n     first:", argv[i]);
		for (std::size_t k = 0; k < f.morphs.size() && k < 5; ++k) {
			std::printf(" %s", f.morphs[k].first.c_str());
		}
		std::printf("\n");
		if (!ok) {
			++bad;
		}
	}
	return bad ? 1 : 0;
}
