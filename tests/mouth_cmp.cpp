// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Bolescothegoat - FaceLab. See LICENSE.

// How far do the head and the mouth part move for the same morph name, and does the mouth part's lower jaw move as a rigid body?
//   mouth_cmp <modlist.txt> <mods folder> [family text]
#include "FamilyLoad.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>

namespace fs = std::filesystem;

int main(int argc, char** argv)
{
	std::vector<std::string> mods;
	{
		std::ifstream f(argv[1]);
		std::string   line;
		while (std::getline(f, line)) {
			if (!line.empty() && line.back() == '\r') {
				line.pop_back();
			}
			if (line.size() > 1 && line[0] == '+') {
				mods.push_back(line.substr(1));
			}
		}
	}
	const std::string root = argv[2];
	const auto        resolve = [&](const std::string& rel) -> std::string {
        for (const auto& m : mods) {
            const fs::path p = fs::path(root) / m / "meshes" / rel;
            std::error_code ec;
            if (fs::exists(p, ec)) {
                return p.string();
            }
        }
        return {};
	};
	const std::string want = argc > 3 ? argv[3] : "UBE";
	for (const auto& def : FaceLab::Fam::All()) {
		if (std::string(def.name).find(want) == std::string::npos) {
			continue;
		}
		std::vector<std::string> log;
		auto                     parts = FaceLab::Fam::Load(def, resolve, &log);
		for (const auto& l : log) {
			if (l.find("follow") != std::string::npos || l.find("rigid") != std::string::npos) {
				std::printf("LOG: %s\n", l.c_str());
			}
		}
		const FaceLab::Fam::Part* head = nullptr;
		const FaceLab::Fam::Part* mouth = nullptr;
		for (const auto& p : parts) {
			if (!head) {
				head = &p;
			}
			if (p.rels.front().find("outh") != std::string::npos && !mouth) {
				mouth = &p;
			}
		}
		if (!head || !mouth) {
			continue;
		}
		float lo[3] = { 1e9f, 1e9f, 1e9f }, hi[3] = { -1e9f, -1e9f, -1e9f };
		for (int v = 0; v < mouth->nv; ++v) {
			for (int k = 0; k < 3; ++k) {
				lo[k] = std::min(lo[k], mouth->base[v * 3 + k]);
				hi[k] = std::max(hi[k], mouth->base[v * 3 + k]);
			}
		}
		std::vector<int> nearv;
		for (int v = 0; v < head->nv; ++v) {
			bool in = true;
			for (int k = 0; k < 3; ++k) {
				if (head->base[v * 3 + k] < lo[k] - 1.0f || head->base[v * 3 + k] > hi[k] + 1.0f) {
					in = false;
				}
			}
			if (in) {
				nearv.push_back(v);
			}
		}
		std::printf("%s: head %d verts, mouth %d verts\n", def.name, head->nv, mouth->nv);
		const auto maxMove = [](const std::vector<float>& d, const std::vector<int>* idx, int nv) {
			float m = 0;
			if (idx) {
				for (int v : *idx) {
					m = std::max(m, std::sqrt(d[v * 3] * d[v * 3] + d[v * 3 + 1] * d[v * 3 + 1] + d[v * 3 + 2] * d[v * 3 + 2]));
				}
			} else {
				for (int v = 0; v < nv; ++v) {
					m = std::max(m, std::sqrt(d[v * 3] * d[v * 3] + d[v * 3 + 1] * d[v * 3 + 1] + d[v * 3 + 2] * d[v * 3 + 2]));
				}
			}
			return m;
		};
		for (const char* n : { "BigAah", "CombatShout", "MoodFear", "DialogueFear", "MoodHappy", "Ahegao", "Kiss", "Yawning", "Smile" }) {
			const auto h = head->morphs.find(n);
			const auto m = mouth->morphs.find(n);
			std::printf("   %-14s head(around mouth) max move %5.2f | mouth part %5.2f\n", n, h == head->morphs.end() ? -1.0f : maxMove(h->second, &nearv, head->nv), m == mouth->morphs.end() ? -1.0f : maxMove(m->second, nullptr, mouth->nv));
		}
		// rigidity: vertices that move with the jaw must keep their mutual distances at weight 1; at 2 and 3 a linear morph drifts (the head's chin does the same)
		const auto& big = mouth->morphs.at("BigAah");
		float       mx = 0;
		for (int v = 0; v < mouth->nv; ++v) {
			mx = std::max(mx, std::sqrt(big[v * 3] * big[v * 3] + big[v * 3 + 1] * big[v * 3 + 1] + big[v * 3 + 2] * big[v * 3 + 2]));
		}
		std::vector<int> jawv;
		for (int v = 0; v < mouth->nv; ++v) {
			if (std::sqrt(big[v * 3] * big[v * 3] + big[v * 3 + 1] * big[v * 3 + 1] + big[v * 3 + 2] * big[v * 3 + 2]) > 0.85f * mx) {
				jawv.push_back(v);
			}
		}
		std::printf("   lower-jaw vertices (full jaw weight): %zu of %d\n", jawv.size(), mouth->nv);
		for (const char* n : { "BigAah", "DialogueFear", "Ahegao", "CombatShout", "Yawning" }) {
			const auto m = mouth->morphs.find(n);
			if (m == mouth->morphs.end()) {
				continue;
			}
			for (const float w : { 1.0f, 2.0f, 3.0f }) {
				double worst = 0;
				int    cnt = 0;
				for (size_t i = 0; i + 7 < jawv.size() && cnt < 4000; i += 3, ++cnt) {
					const int  a = jawv[i], b = jawv[(i * 7 + 13) % jawv.size()];
					const auto P = [&](int v, float k) {
						return std::array<double, 3>{ mouth->base[v * 3] + k * m->second[v * 3], mouth->base[v * 3 + 1] + k * m->second[v * 3 + 1], mouth->base[v * 3 + 2] + k * m->second[v * 3 + 2] };
					};
					const auto d = [](const std::array<double, 3>& x, const std::array<double, 3>& y) {
						return std::sqrt((x[0] - y[0]) * (x[0] - y[0]) + (x[1] - y[1]) * (x[1] - y[1]) + (x[2] - y[2]) * (x[2] - y[2]));
					};
					const double d0 = d(P(a, 0), P(b, 0));
					if (d0 < 0.3) {
						continue;
					}
					worst = std::max(worst, std::fabs(d(P(a, w), P(b, w)) / d0 - 1.0));
				}
				std::printf("      %-13s weight %.0f: worst change of a distance between two lower-jaw vertices %.1f%%\n", n, w, worst * 100);
			}
		}
	}
}
