// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Bolescothegoat - FaceLab. See LICENSE.

#include "Presets.h"
#include "Lab.h"

#include <toml++/toml.hpp>

#include <filesystem>
#include <fstream>

namespace FaceLab::Presets
{
	namespace
	{
		constexpr const char* kFolder = "Data/SKSE/Plugins/FaceLab/Presets";

		std::mutex           g_mx;
		std::vector<Preset>  g_all;

		std::string Lower(std::string_view s)
		{
			std::string o(s);
			std::ranges::transform(o, o.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
			return o;
		}

		int ChannelIndex(std::string_view name)
		{
			const auto& ch = Channels();
			for (std::size_t i = 0; i < ch.size(); ++i) {
				if (Lower(ch[i].name) == Lower(name)) {
					return static_cast<int>(i);
				}
			}
			return -1;
		}

		const Preset* FindLocked(std::string_view name)
		{
			const auto key = Lower(name);
			for (const auto& p : g_all) {
				if (Lower(p.name) == key) {
					return &p;
				}
			}
			return nullptr;
		}

		bool IsDisposition(std::string_view s)
		{
			const auto l = Lower(s);
			return l == "displeased" || l == "neutral" || l == "pleased";
		}

		bool WriteFile(const std::string& file)
		{
			std::string o = "# FaceLab presets. Edited in the game (SKSE Menu Framework > FaceLab > Presets); weights are x 100 (100 = 1.0, up to 300).\n";
			for (const auto& p : g_all) {
				if (p.file != file) {
					continue;
				}
				o += std::format("\n[[preset]]\nname = \"{}\"\nweights = {{ ", p.name);
				bool first = true;
				for (const auto& [ch, w] : p.weights) {
					o += std::format("{}{} = {}", first ? "" : ", ", ch, w);
					first = false;
				}
				o += first ? "}\n" : " }\n";
			}
			std::error_code ec;
			std::filesystem::create_directories(kFolder, ec);
			std::ofstream f(std::filesystem::path(kFolder) / file, std::ios::binary | std::ios::trunc);
			if (!f) {
				return false;
			}
			f << o;
			return true;
		}
	}

	std::string Folder()
	{
		return kFolder;
	}

	void Load()
	{
		std::vector<Preset> found;
		std::error_code     ec;
		for (const auto& e : std::filesystem::directory_iterator(kFolder, ec)) {
			if (!e.is_regular_file() || Lower(e.path().extension().string()) != ".toml") {
				continue;
			}
			const auto file = e.path().filename().string();
			try {
				const auto tbl = toml::parse_file(e.path().string());
				if (const auto* arr = tbl["preset"].as_array()) {
					for (const auto& node : *arr) {
						const auto* t = node.as_table();
						const auto  name = t ? (*t)["name"].value_or(std::string()) : std::string();
						if (name.empty()) {
							continue;
						}
						Preset p;
						p.name = name;
						p.file = file;
						if (const auto* w = (*t)["weights"].as_table()) {
							for (const auto& [k, v] : *w) {
								const int idx = ChannelIndex(k.str());
								if (idx < 0) {
									logger::info("preset '{}' ({}): unknown channel '{}' ignored", name, file, std::string(k.str()));
									continue;
								}
								p.weights.push_back({ Channels()[static_cast<std::size_t>(idx)].name, std::clamp(static_cast<int>(v.value_or(0)), 0, 300) });
							}
						}
						if (std::ranges::any_of(found, [&](const Preset& o) { return Lower(o.name) == Lower(name); })) {
							logger::info("preset '{}' in {} is a duplicate name: ignored", name, file);
							continue;
						}
						found.push_back(std::move(p));
					}
				}
			} catch (const std::exception& ex) {
				logger::info("preset file {} could not be read: {}", file, ex.what());
			}
		}
		std::ranges::sort(found, {}, [](const Preset& p) { return Lower(p.name); });
		{
			std::lock_guard lk(g_mx);
			g_all = std::move(found);
		}
		logger::info("presets: {} loaded from {}", All().size(), kFolder);
	}

	std::vector<Preset> All()
	{
		std::lock_guard lk(g_mx);
		return g_all;
	}

	std::optional<Preset> Find(std::string_view name)
	{
		std::lock_guard lk(g_mx);
		const auto*     p = FindLocked(name);
		return p ? std::optional<Preset>(*p) : std::nullopt;
	}

	std::optional<Preset> Resolve(std::string_view name)
	{
		if (auto p = Find(name)) {
			return p;
		}
		const auto dot = name.rfind('.');
		if (dot == std::string_view::npos || !IsDisposition(name.substr(dot + 1))) {
			return std::nullopt;
		}
		const auto head = name.substr(0, dot);  // Mod.Context
		const auto last = name.substr(dot + 1);
		const auto mod = head.substr(0, head.find('.'));
		for (const auto& c : { std::format("{}.Neutral", head), std::format("{}.{}", mod, last), std::format("{}.Neutral", mod) }) {
			if (auto p = Find(c)) {
				return p;
			}
		}
		return std::nullopt;
	}

	bool Save(const Preset& in)
	{
		Preset p = in;
		if (p.file.empty()) {
			p.file = "User.toml";
		}
		std::lock_guard lk(g_mx);
		bool            replaced = false;
		for (auto& e : g_all) {
			if (Lower(e.name) == Lower(p.name)) {
				p.file = e.file;  // an existing preset stays in its own file
				e = p;
				replaced = true;
				break;
			}
		}
		if (!replaced) {
			g_all.push_back(p);
			std::ranges::sort(g_all, {}, [](const Preset& x) { return Lower(x.name); });
		}
		return WriteFile(p.file);
	}
}
