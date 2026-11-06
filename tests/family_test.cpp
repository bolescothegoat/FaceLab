// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Bolescothegoat - FaceLab. See LICENSE.

// Offline test of the head families: resolves the morph files the way MO2 would (the enabled mods of a modlist.txt, highest priority first, plus any mods
// named with --enable), loads every family and reports what each one has: parts, morph counts, coverage of the names FaceLab presets use, extra names.
//   family_test <modlist.txt> <mods folder> [--enable "Mod name"]... [--family "text in family name"]
#include "FamilyLoad.h"
#include "TriFile.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <set>

namespace fs = std::filesystem;

int main(int argc, char** argv)
{
	if (argc < 3) {
		std::puts("usage: family_test <modlist.txt> <mods folder> [--enable \"Mod\"]... [--family text]");
		return 2;
	}
	std::set<std::string> enable;
	std::string           only;
	for (int i = 3; i < argc; ++i) {
		const std::string a = argv[i];
		if (a == "--enable" && i + 1 < argc) {
			enable.insert(argv[++i]);
		} else if (a == "--family" && i + 1 < argc) {
			only = argv[++i];
		}
	}
	std::vector<std::string> mods;  // highest priority first
	{
		std::ifstream f(argv[1]);
		std::string   line;
		while (std::getline(f, line)) {
			if (!line.empty() && line.back() == '\r') {
				line.pop_back();
			}
			if (line.size() > 1 && (line[0] == '+' || (line[0] == '-' && enable.contains(line.substr(1))))) {
				mods.push_back(line.substr(1));
			}
		}
	}
	std::printf("%zu enabled mods in the list\n", mods.size());
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
	static const char* kStandard[] = { "Aah", "BigAah", "BMP", "ChJSh", "DST", "Eee", "Eh", "FV", "I", "K", "N", "Oh", "OohQ", "R", "Th", "W", "BlinkLeft", "BlinkRight", "BrowDownLeft",
		"BrowDownRight", "BrowInLeft", "BrowInRight", "BrowUpLeft", "BrowUpRight", "LookDown", "LookLeft", "LookRight", "LookUp", "SquintLeft", "SquintRight", "DialogueAnger",
		"DialogueFear", "DialogueHappy", "DialogueSad", "DialogueSurprise", "DialoguePuzzled", "DialogueDisgusted", "MoodAnger", "MoodFear", "MoodHappy", "MoodSad", "MoodSurprise",
		"MoodPuzzled", "MoodDisgusted", "CombatAnger", "CombatShout", "EyesWide", "EyeLeftWide", "EyeRightWide", "EyesCross", "EyesApart", "EyeLeftLookLeft", "EyeLeftLookRight",
		"EyeLeftLookUp", "EyeLeftLookDown", "EyeRightLookLeft", "EyeRightLookRight", "EyeRightLookUp", "EyeRightLookDown", "EyeLeftTurnLeft", "EyeRightTurnUp" };
	std::set<std::string> standard(std::begin(kStandard), std::end(kStandard));
	for (const auto& def : FaceLab::Fam::All()) {
		if (!only.empty() && std::string(def.name).find(only) == std::string::npos) {
			continue;
		}
		std::printf("\n=== %s\n", def.name);
		const auto hdr = FaceLab::Fam::Headers(def, resolve);
		if (hdr.empty()) {
			std::puts("   (no files installed)");
			continue;
		}
		const int headNv = hdr.front().nv;
		std::printf("   head: %d vertices (%s)\n", headNv, hdr.front().path.c_str());
		std::vector<std::string> log;
		const auto               t0 = std::chrono::steady_clock::now();
		const auto               parts = FaceLab::Fam::Load(def, resolve, &log);
		const double             ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
		std::printf("   loaded %zu parts in %.0f ms\n", parts.size(), ms);
		std::size_t floats = 0;
		for (const auto& p : parts) {
			floats += p.morphs.size() * p.nv * 3;
			std::printf("     part %6d verts | %2zu file(s) | %3zu morphs | first: %s\n", p.nv, p.files.size(), p.morphs.size(), p.files.front().substr(p.files.front().find_last_of("\/") + 1).c_str());
		}
		std::printf("   memory for morph data: %.1f MB\n", floats * 4.0 / 1048576.0);
		for (const auto& l : log) {
			if (l.find("separate part") != std::string::npos || l.find("cannot") != std::string::npos || l.find("announces") != std::string::npos) {
				std::printf("   NOTE: %s\n", l.c_str());
			}
		}
		const FaceLab::Fam::Part* head = nullptr;
		for (const auto& p : parts) {
			if (p.nv == headNv) {
				head = &p;
				break;
			}
		}
		if (!head) {
			continue;
		}
		const auto anyPart = [&](const std::string& n) {
			for (const auto& p : parts) {
				if (p.morphs.contains(n)) {
					return true;
				}
			}
			return false;
		};
		std::string missing, viaAlias;
		int         have = 0, total = 0;
		for (const char* n : kStandard) {
			++total;
			if (anyPart(n)) {
				++have;
				continue;
			}
			bool ok = false;
			for (const char* al : FaceLab::Fam::Aliases(n)) {
				if (anyPart(al)) {
					ok = true;
					viaAlias += std::string(n) + "->" + al + " ";
					break;
				}
			}
			if (!ok) {
				missing += std::string(n) + " ";
			}
		}
		std::printf("   channels FaceLab presets use found on some part: %d of %d; via alias: %s\n", have, total, viaAlias.empty() ? "-" : viaAlias.c_str());
		std::printf("   not available on this head: %s\n", missing.empty() ? "-" : missing.c_str());
		// per face part: which of the head's names it does NOT have (these move the head but not that part)
		for (const auto& p : parts) {
			if (&p == head) {
				continue;
			}
			std::string lack, lackStd;
			int         nLack = 0, nLackStd = 0;
			for (const auto& [name, dl] : head->morphs) {
				if (p.morphs.contains(name)) {
					continue;
				}
				++nLack;
				if (standard.contains(name)) {
					++nLackStd;
					lackStd += name + " ";
				} else if (lack.size() < 100) {
					lack += name + " ";
				}
			}
			std::printf("   part %6d (%s): lacks %d of the head's %zu names (%d standard: %s) e.g. %s\n", p.nv, p.rels.front().substr(p.rels.front().find_last_of('/') + 1).c_str(), nLack, head->morphs.size(), nLackStd,
				lackStd.substr(0, 150).c_str(), lack.c_str());
		}
		int extras = 0, safe = 0;
		std::string sample;
		for (const auto& [name, dl] : head->morphs) {
			if (standard.contains(name)) {
				continue;
			}
			++extras;
			int followers = 0;
			for (const auto& p : parts) {
				if (&p != head && p.morphs.contains(name)) {
					++followers;
				}
			}
			if (followers > 0) {
				++safe;
			}
			if (sample.size() < 160) {
				sample += name + "(" + std::to_string(followers) + ") ";
			}
		}
		std::printf("   extra names on the head: %d, of which %d also exist on at least one other part\n   e.g. %s\n", extras, safe, sample.c_str());
	}
	return 0;
}
