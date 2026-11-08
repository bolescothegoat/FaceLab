// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Bolescothegoat - FaceLab. See LICENSE.

#include "Lab.h"
#include "Api.h"
#include "Morph.h"

#include <toml++/toml.hpp>

#include <Windows.h>
#include <psapi.h>
#include <atomic>
#include <fstream>
#include <set>

namespace FaceLab
{
	namespace
	{
		constexpr const char* kPath = "Data/SKSE/Plugins/FaceLab.toml";

		std::atomic<bool> g_applied{ false };
		std::atomic<long long> g_labUntil{ 0 };  // steady-clock ms when the lab face lets go; 0 = never
		RE::FormID        g_labActor{ 0 };
		constexpr const char*     kTongueOwner = "FaceLab.Tongue";  // the Tongue assist layer
		std::mutex                g_shMx;
		std::set<std::string>     g_shHide;       // face shapes the debug list hides
		std::vector<std::string>  g_shNames;      // the target's face shapes (refreshed about once a second)
		long long                 g_shNext{ 0 };
		std::atomic<RE::FormID>   g_crossId{ 0 };
		std::atomic<bool>         g_tongueOn{ false };
		std::atomic<RE::FormID>   g_tongueActor{ 0 };
		std::atomic<long long>    g_tongueUntil{ 0 };  // like the lab face: lets go by itself unless 'keep until reset' is on

		long long NowMs()
		{
			return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
		}
		std::mutex        g_mx;
		RE::ActorHandle   g_cross;
		std::string       g_crossName = "(nobody yet: look at an NPC)";

		RE::Actor* Target()
		{
			if (GetSettings().target == 0) {
				return RE::PlayerCharacter::GetSingleton();
			}
			std::lock_guard lk(g_mx);
			return g_cross.get().get();
		}

		// the lab is a layer like any other mod's; priority 1000 so what you are testing always wins
		void SetOwnMorphs(RE::Actor* a)
		{
			const auto& s = GetSettings();
			Api::Weights w;
			for (std::size_t i = 0; i < s.values.size(); ++i) {
				if (s.values[i] != 0) {
					w.push_back({ Channels()[i].name, static_cast<float>(s.values[i]) / 100.0f });
				}
			}
			for (const auto& [name, v] : s.extras) {
				if (v != 0) {
					w.push_back({ name, static_cast<float>(v) / 100.0f });
				}
			}
			for (const auto& [wn, wv] : w) {
				if (wn == "TongueOut" && wv > 0.0f) {
					logger::info("Lab face asks for a tongue: TongueOut {:.2f} on {} ({:08X})", wv, a->GetDisplayFullName(), a->GetFormID());
				}
			}
			Morph::SetBump(s.bump);
			Api::ApplyWeights(a->GetFormID(), kLabOwner, std::move(w), static_cast<float>(s.lerpMs) / 1000.0f, 1000);
			g_labActor = a->GetFormID();
			g_labUntil = s.labHold > 0 && !s.holdUntilReset ? NowMs() + static_cast<long long>(s.labHold) * 1000 : 0;
		}

		bool Readable(const void* p, std::size_t n)
		{
			if (!p) {
				return false;
			}
			MEMORY_BASIC_INFORMATION mbi{};
			if (!::VirtualQuery(p, &mbi, sizeof(mbi)) || mbi.State != MEM_COMMIT || (mbi.Protect & (PAGE_NOACCESS | PAGE_GUARD))) {
				return false;
			}
			return static_cast<const char*>(mbi.BaseAddress) + mbi.RegionSize >= static_cast<const char*>(p) + n;
		}

		// is q an array of nv positions (stride 12 or 16 bytes) close to the shape's current positions?
		int LooksLikeBase(const void* q, std::size_t nv, const float* dyn)
		{
			for (const int stride : { 12, 16 }) {
				if (!Readable(q, nv * static_cast<std::size_t>(stride))) {
					continue;
				}
				const auto* f = static_cast<const float*>(q);
				std::size_t close = 0;
				for (std::size_t v = 0; v < nv; ++v) {
					const auto* e = f + v * static_cast<std::size_t>(stride / 4);
					if (std::fabs(e[0] - dyn[v * 4]) < 3.0f && std::fabs(e[1] - dyn[v * 4 + 1]) < 3.0f && std::fabs(e[2] - dyn[v * 4 + 2]) < 3.0f) {
						++close;
					}
				}
				if (close * 100 >= nv * 90) {
					return stride;
				}
			}
			return 0;
		}

		void ScanObject(const std::string& label, const void* obj, std::size_t nv, const float* dyn, int depth)
		{
			if (!Readable(obj, 0x100)) {
				return;
			}
			const auto* w = static_cast<const std::uintptr_t*>(obj);
			for (std::size_t i = 0; i < 0x100 / 8; ++i) {
				const void* q = reinterpret_cast<const void*>(w[i]);
				if (!q || q == dyn) {
					continue;
				}
				if (const int stride = LooksLikeBase(q, nv, dyn)) {
					logger::info("PROBE base vertices? {} + {:#x} -> {:p}, {} verts, stride {}", label, i * 8, q, nv, stride);
				} else if (depth > 0 && Readable(q, 0x100)) {
					ScanObject(label + "+" + std::format("{:#x}", i * 8), q, nv, dyn, depth - 1);
				}
			}
		}
	}

	const std::vector<Channel>& Channels()
	{
		static const std::vector<Channel> c = {
			{ "Aah", "Mouth" }, { "BigAah", "Mouth" }, { "BMP", "Mouth" }, { "ChJSh", "Mouth" }, { "DST", "Mouth" }, { "Eee", "Mouth" },
			{ "Eh", "Mouth" }, { "FV", "Mouth" }, { "I", "Mouth" }, { "K", "Mouth" }, { "N", "Mouth" }, { "Oh", "Mouth" },
			{ "OohQ", "Mouth" }, { "R", "Mouth" }, { "Th", "Mouth" }, { "W", "Mouth" },
			{ "BlinkLeft", "Eyes and brows" }, { "BlinkRight", "Eyes and brows" }, { "BrowDownLeft", "Eyes and brows" }, { "BrowDownRight", "Eyes and brows" },
			{ "BrowInLeft", "Eyes and brows" }, { "BrowInRight", "Eyes and brows" }, { "BrowUpLeft", "Eyes and brows" }, { "BrowUpRight", "Eyes and brows" },
			{ "LookDown", "Eyes and brows" }, { "LookLeft", "Eyes and brows" }, { "LookRight", "Eyes and brows" }, { "LookUp", "Eyes and brows" },
			{ "SquintLeft", "Eyes and brows" }, { "SquintRight", "Eyes and brows" },
			{ "DialogueAnger", "Emotions" }, { "DialogueFear", "Emotions" }, { "DialogueHappy", "Emotions" }, { "DialogueSad", "Emotions" },
			{ "DialogueSurprise", "Emotions" }, { "DialoguePuzzled", "Emotions" }, { "DialogueDisgusted", "Emotions" },
			{ "MoodAnger", "Emotions" }, { "MoodFear", "Emotions" }, { "MoodHappy", "Emotions" }, { "MoodSad", "Emotions" },
			{ "MoodSurprise", "Emotions" }, { "MoodPuzzled", "Emotions" }, { "MoodDisgusted", "Emotions" },
			{ "CombatAnger", "Emotions" }, { "CombatShout", "Emotions" },
			// appended last on purpose: the lab's saved sliders are stored by position
			{ "EyesWide", "Eyes - each eye separately" }, { "EyeLeftWide", "Eyes - each eye separately" }, { "EyeRightWide", "Eyes - each eye separately" },
			{ "EyesCross", "Eyes - each eye separately" }, { "EyesApart", "Eyes - each eye separately" },
			{ "EyeLeftLookLeft", "Eyes - each eye separately" }, { "EyeLeftLookRight", "Eyes - each eye separately" },
			{ "EyeLeftLookUp", "Eyes - each eye separately" }, { "EyeLeftLookDown", "Eyes - each eye separately" },
			{ "EyeRightLookLeft", "Eyes - each eye separately" }, { "EyeRightLookRight", "Eyes - each eye separately" },
			{ "EyeRightLookUp", "Eyes - each eye separately" }, { "EyeRightLookDown", "Eyes - each eye separately" },
			{ "MouthAssist", "Mouth assist" },  // a mouth opening (BigAah) that fades out as the natural mouth (OStim's open mouth, MFG) opens: covers the seconds a scene change leaves it shut
{ "TongueOut", "Tongue" },  // no mesh morph: the face asks a tongue mod for the tongue (1.0 = fully out, more = further)
			{ "OralMode", "Oral" },  // a marker, no morph: 50 and more = oral scene. The mouth belongs to OStim (emotion channels leave the lips alone, the mouth channels give way as OStim's own mouth opens, and a face rebuild is bridged)
			{ "EyeLeftTurnLeft", "Eyes - true rotation (10 degrees per 1.0)" }, { "EyeLeftTurnRight", "Eyes - true rotation (10 degrees per 1.0)" },
			{ "EyeLeftTurnUp", "Eyes - true rotation (10 degrees per 1.0)" }, { "EyeLeftTurnDown", "Eyes - true rotation (10 degrees per 1.0)" },
			{ "EyeRightTurnLeft", "Eyes - true rotation (10 degrees per 1.0)" }, { "EyeRightTurnRight", "Eyes - true rotation (10 degrees per 1.0)" },
			{ "EyeRightTurnUp", "Eyes - true rotation (10 degrees per 1.0)" }, { "EyeRightTurnDown", "Eyes - true rotation (10 degrees per 1.0)" },
		};
		return c;
	}

	Settings& GetSettings()
	{
		static Settings s;
		if (s.values.size() != Channels().size()) {
			s.values.assign(Channels().size(), 0);
		}
		return s;
	}

	void LoadConfig()
	{
		auto& s = GetSettings();
		try {
			auto tbl = toml::parse_file(kPath);
			s.enabled = tbl["enabled"].value_or(s.enabled);
			s.lerpMs = std::clamp(tbl["lerpMs"].value_or(s.lerpMs), 0, 5000);
			s.labHold = std::clamp(tbl["labHold"].value_or(s.labHold), 0, 600);
			s.holdUntilReset = tbl["holdUntilReset"].value_or(s.holdUntilReset);
			s.target = std::clamp(tbl["target"].value_or(s.target), 0, 1);
			s.live = tbl["live"].value_or(s.live);
			s.scenes = tbl["scenes"].value_or(s.scenes);
			s.tongueOut = std::clamp(tbl["tongueOut"].value_or(s.tongueOut), 0, 100);
			s.lidFollow = std::clamp(tbl["lidFollow"].value_or(s.lidFollow), 0, 250);
			Morph::SetLidFollow(static_cast<float>(s.lidFollow) / 100.0f);
			s.ubeSide = std::clamp(tbl["ubeSideAdj"].value_or(s.ubeSide), -100, 100);
			s.ubeDown = std::clamp(tbl["ubeDownAdj"].value_or(s.ubeDown), -100, 100);
			s.ubeUp = std::clamp(tbl["ubeUpAdj"].value_or(s.ubeUp), -100, 100);
			Morph::SetUbeAdjust(s.ubeSide, s.ubeDown, s.ubeUp);
			s.vertLimit = std::clamp(tbl["vertLimit"].value_or(s.vertLimit), 4, 60);
			Morph::SetVertLimit(static_cast<float>(s.vertLimit));
			s.shadowStrength = std::clamp(tbl["shadowStrength"].value_or(s.shadowStrength), 0, 100);
			Morph::SetShadowStrength(static_cast<float>(s.shadowStrength) / 100.0f);
			s.bump = tbl["patchFmd"].value_or(s.bump);
			s.debug = tbl["debug"].value_or(s.debug);
			if (auto t = tbl["keys"].as_table()) {
				s.kbKey = (*t)["keyboard"].value_or(s.kbKey);
				s.kbMod = (*t)["keyboardModifier"].value_or(s.kbMod);
				s.padKey = (*t)["gamepad"].value_or(s.padKey);
				s.padMod = (*t)["gamepadModifier"].value_or(s.padMod);
			}
			if (auto t = tbl["tongueKeys"].as_table()) {
				s.tKbKey = (*t)["keyboard"].value_or(s.tKbKey);
				s.tKbMod = (*t)["keyboardModifier"].value_or(s.tKbMod);
				s.tPadKey = (*t)["gamepad"].value_or(s.tPadKey);
				s.tPadMod = (*t)["gamepadModifier"].value_or(s.tPadMod);
			}
			auto* arr = tbl["values"].as_array();
			if (!arr) {
				if (auto t = tbl["keys"].as_table()) {
					arr = (*t)["values"].as_array();  // files written by older versions put the list under [keys]
				}
			}
			if (arr) {
				for (std::size_t i = 0; i < arr->size() && i < s.values.size(); ++i) {
					s.values[i] = std::clamp((*arr)[i].value_or(0), 0, 300);
				}
			}
			s.extras.clear();
			if (auto t = tbl["extras"].as_table()) {
				for (auto&& [k, v] : *t) {
					const int val = std::clamp(v.value_or(0), 0, 300);
					if (val != 0) {
						s.extras[std::string(k.str())] = val;
					}
				}
			}
		} catch (const std::exception& e) {
			logger::info("no usable FaceLab.toml ({}); using defaults", e.what());
			SaveConfig();
		}
	}

	bool SaveConfig()
	{
		const auto& s = GetSettings();
		auto        b = [](bool v) { return v ? "true" : "false"; };
		std::string o = "# FaceLab settings. Written by the in-game menu (SKSE Menu Framework > FaceLab).\n";
		o += std::format("holdUntilReset = {}\nscenes = {}\ntongueOut = {}\nlidFollow = {}\nvertLimit = {}\nshadowStrength = {}\n", b(s.holdUntilReset), b(s.scenes), s.tongueOut, s.lidFollow, s.vertLimit, s.shadowStrength);
		o += std::format("ubeSideAdj = {}\nubeDownAdj = {}\nubeUpAdj = {}\n", s.ubeSide, s.ubeDown, s.ubeUp);
		o += std::format("enabled = {}\nlerpMs = {}\nlabHold = {}\ntarget = {}\nlive = {}\npatchFmd = {}\ndebug = {}\n", b(s.enabled), s.lerpMs, s.labHold, s.target, b(s.live), b(s.bump), b(s.debug));
		o += "\n# one value per channel in the menu's order (weight x 100, 0-300)\nvalues = [";
		for (std::size_t i = 0; i < s.values.size(); ++i) {
			o += std::format("{}{}", i ? ", " : "", s.values[i]);
		}
		o += "]\n";
		o += std::format("\n[keys]\nkeyboard = {}\nkeyboardModifier = {}\ngamepad = {}\ngamepadModifier = {}\n", s.kbKey, s.kbMod, s.padKey, s.padMod);
		o += std::format("\n[tongueKeys]\nkeyboard = {}\nkeyboardModifier = {}\ngamepad = {}\ngamepadModifier = {}\n", s.tKbKey, s.tKbMod, s.tPadKey, s.tPadMod);
		o += "\n# morphs only some heads have (CoTR, UBE ...), by name: weight x 100, 0-300\n[extras]\n";
		for (const auto& [name, v] : s.extras) {
			if (v != 0) {
				o += std::format("{} = {}\n", name, v);
			}
		}
		std::ofstream f(kPath, std::ios::binary | std::ios::trunc);
		if (!f) {
			return false;
		}
		f << o;
		return true;
	}

	std::string TargetName()
	{
		if (GetSettings().target == 0) {
			return "the player";
		}
		std::lock_guard lk(g_mx);
		return g_crossName;
	}

	RE::Actor* TargetActor()
	{
		return Target();
	}

	namespace
	{
		std::mutex                g_exMx;
		std::string               g_exFamily;
		std::vector<ExtraName>    g_exNames;
		bool                      g_exOk{ false };
		long long                 g_exNext{ 0 };
	}

	bool ExtrasSnapshot(std::string& family, std::vector<ExtraName>& out)
	{
		const long long now = NowMs();
		bool            ask = false;
		{
			std::lock_guard lk(g_exMx);
			if (now >= g_exNext) {
				g_exNext = now + 1000;
				ask = true;
			}
			family = g_exFamily;
			out = g_exNames;
		}
		if (ask) {
			SKSE::GetTaskInterface()->AddTask([]() {
				std::string                family;
				std::vector<Morph::NameInfo> names;
				const bool                 ok = Morph::HeadNames(Target(), family, names);
				std::lock_guard            lk(g_exMx);
				g_exOk = ok;
				g_exFamily = ok ? family : std::string{};
				g_exNames.clear();
				for (const auto& n : names) {
					g_exNames.push_back({ n.name, n.parts, n.partsTotal });
				}
				std::ranges::sort(g_exNames, [](const ExtraName& l, const ExtraName& r) { return l.name < r.name; });
			});
		}
		std::lock_guard lk(g_exMx);
		return g_exOk;
	}

	void ApplyOne(int)
	{
		SKSE::GetTaskInterface()->AddTask([]() {
			if (auto* a = Target()) {
				SetOwnMorphs(a);
			}
		});
	}

	void ApplyAll()
	{
		SKSE::GetTaskInterface()->AddTask([]() {
			auto* a = Target();
			if (!a) {
				RE::SendHUDMessage::ShowHUDMessage("FaceLab: no target (look at an NPC first)");
				return;
			}
			SetOwnMorphs(a);
			g_applied = true;
			if (GetSettings().debug) {
				logger::info("applied to {}", a->GetDisplayFullName());
			}
		});
		SaveConfig();
	}

	void Reset()
	{
		SKSE::GetTaskInterface()->AddTask([]() {
			if (auto* a = Target()) {
				Api::Clear(a->GetFormID(), kLabOwner, static_cast<float>(GetSettings().lerpMs) / 1000.0f);
				g_applied = false;
				g_labUntil = 0;
			}
		});
	}

	bool Applied()
	{
		return g_applied.load();
	}

	void OnHotkey()
	{
		if (!GetSettings().enabled) {
			return;
		}
		if (g_applied) {
			Reset();
			RE::SendHUDMessage::ShowHUDMessage("FaceLab: face reset");
		} else {
			ApplyAll();
			RE::SendHUDMessage::ShowHUDMessage("FaceLab: face applied");
		}
	}

	namespace
	{
	// A tongue mod announces itself by exporting FaceLab_TongueStatus; FaceLab looks through the loaded modules for it, so it never depends on a file name.
	using TongueStatusFn = int (*)(unsigned int);
	std::atomic<TongueStatusFn> g_tongueStatusFn{ nullptr };
	std::atomic<long long>      g_tongueScanAt{ 0 };

	TongueStatusFn FindTongueMod()
	{
		if (const auto f = g_tongueStatusFn.load()) {
			return f;
		}
		const long long now = NowMs();
		if (now < g_tongueScanAt.load()) {
			return nullptr;
		}
		g_tongueScanAt = now + 2000;  // modules can load late: look again every two seconds until one answers
		HMODULE mods[1024]{};
		DWORD   need = 0;
		if (!::EnumProcessModules(::GetCurrentProcess(), mods, sizeof(mods), &need)) {
			return nullptr;
		}
		const auto n = std::min<std::size_t>(need / sizeof(HMODULE), 1024);
		for (std::size_t i = 0; i < n; ++i) {
			if (const auto p = ::GetProcAddress(mods[i], "FaceLab_TongueStatus")) {
				g_tongueStatusFn = reinterpret_cast<TongueStatusFn>(p);
				return g_tongueStatusFn.load();
			}
		}
		return nullptr;
	}
	}

	bool TongueModInstalled()
	{
		return FindTongueMod() != nullptr;
	}

	int TongueModStatus(unsigned int formID)
	{
		const auto f = FindTongueMod();
		return f ? f(formID) : -99;
	}

	bool ShapeSnapshot(std::vector<std::string>& names)
	{
	const long long now = NowMs();
	bool ask = false;
	{
		std::lock_guard lk(g_shMx);
		if (now >= g_shNext) {
			g_shNext = now + 1000;
			ask = true;
		}
		names = g_shNames;
	}
	if (ask) {
		SKSE::GetTaskInterface()->AddTask([]() {
			auto n = Morph::FaceShapeNames(Target());
			std::lock_guard lk(g_shMx);
			g_shNames = std::move(n);
		});
	}
	return !names.empty();
	}

	bool ShapeHidden(const std::string& name)
	{
	std::lock_guard lk(g_shMx);
	return g_shHide.contains(name);
	}

	void SetShapeHidden(const std::string& name, bool hidden)
	{
	{
		std::lock_guard lk(g_shMx);
		if (hidden) {
			g_shHide.insert(name);
		} else {
			g_shHide.erase(name);
		}
	}
	SKSE::GetTaskInterface()->AddTask([]() {
		std::set<std::string> h;
		{
			std::lock_guard lk(g_shMx);
			h = g_shHide;
		}
		Morph::CullFaceShapes(Target(), h);
	});
	}

	void SetTongue(bool on)
	{
		SKSE::GetTaskInterface()->AddTask([on]() {
			const auto& s = GetSettings();
			const float fade = static_cast<float>(s.lerpMs) / 1000.0f;
			if (on) {
				auto* a = Target();
				if (!a) {
					RE::SendHUDMessage::ShowHUDMessage("FaceLab: no target (look at an NPC first)");
					return;
				}
				if (const auto old = g_tongueActor.load(); old != 0 && old != a->GetFormID()) {
					Api::ClearAssist(old, kTongueOwner, fade);
				}
				// which tongue is shown is the tongue mod's own setting; FaceLab only asks for TongueOut
				Api::SetAssist(a->GetFormID(), kTongueOwner, { { "TongueOut", static_cast<float>(s.tongueOut) / 100.0f } }, fade);
				logger::info("Tongue assist ON: {} ({:08X}), TongueOut {:.2f}, tongue mod {}", a->GetDisplayFullName(), a->GetFormID(), static_cast<float>(s.tongueOut) / 100.0f,
					TongueModInstalled() ? "found" : "NOT found");
				g_tongueActor = a->GetFormID();
				g_tongueOn = true;
				g_tongueUntil = s.labHold > 0 && !s.holdUntilReset ? NowMs() + static_cast<long long>(s.labHold) * 1000 : 0;
			} else {
				if (const auto id = g_tongueActor.load(); id != 0) {
					Api::ClearAssist(id, kTongueOwner, fade);
					logger::info("Tongue assist OFF ({:08X})", id);
				}
				g_tongueOn = false;
				g_tongueActor = 0;
				g_tongueUntil = 0;
			}
		});
	}

	RE::FormID TargetFormID()
	{
	return GetSettings().target == 0 ? 0x14 : g_crossId.load();
	}

	bool TongueOn()
	{
		return g_tongueOn.load();
	}

	void ToggleTongue()
	{
		const bool turningOn = !g_tongueOn.load();
		if (turningOn && GetSettings().tongueOut <= 0) {
			GetSettings().tongueOut = 100;  // a tongue asked for at 0 never shows: switching it on means it should come out (the button and the hotkey both land here)
			SaveConfig();
		}
		SetTongue(turningOn);
	}

	void ApplyTongueAmount()
	{
		if (g_tongueOn.load()) {
			SetTongue(true);
		}
	}

	void LoadPreset(int which)
	{
		auto& v = GetSettings().values;
		std::ranges::fill(v, 0);
		GetSettings().extras.clear();
		auto set = [&](const char* name, int value) {
			const auto& ch = Channels();
			for (std::size_t i = 0; i < ch.size(); ++i) {
				if (std::string_view(ch[i].name) == name) {
					v[i] = value;
				}
			}
		};
		if (which == 3) {
			set("BigAah", 300);
		}
		SaveConfig();
	}

	void StartCrosshairWatch()
	{
		std::thread([]() {
			for (;;) {
				std::this_thread::sleep_for(std::chrono::milliseconds(200));
				if (const auto until = g_labUntil.load(); until != 0 && NowMs() >= until) {  // the lab face lets go on its own, so it cannot hide a mod's face
					g_labUntil = 0;
					const auto id = g_labActor;
					SKSE::GetTaskInterface()->AddTask([id]() {
						Api::Clear(id, kLabOwner, static_cast<float>(GetSettings().lerpMs) / 1000.0f);
						g_applied = false;
					});
				}
				{
					bool any = false;
					{
						std::lock_guard lk(g_shMx);
						any = !g_shHide.empty();
					}
					if (any) {
						SKSE::GetTaskInterface()->AddTask([]() {
							std::set<std::string> h;
							{
								std::lock_guard lk(g_shMx);
								h = g_shHide;
							}
							Morph::CullFaceShapes(Target(), h);
						});
					}
				}
				if (const auto until = g_tongueUntil.load(); until != 0 && NowMs() >= until) {  // the tongue lets go on its own too
					g_tongueUntil = 0;
					SetTongue(false);
				}
				SKSE::GetTaskInterface()->AddTask([]() {
					if (auto* pick = RE::CrosshairPickData::GetSingleton()) {
						if (auto ref = pick->target.get()) {
							if (auto* a = ref->As<RE::Actor>(); a && !a->IsPlayerRef()) {
								std::lock_guard lk(g_mx);
								g_cross = a->GetHandle();
								g_crossId = a->GetFormID();
								g_crossName = a->GetDisplayFullName();
							}
						}
					}
				});
			}
		}).detach();
	}

	void PokeTest()
	{
		SKSE::GetTaskInterface()->AddTask([]() {
			if (auto* a = Target()) {
				Morph::Poke(a, 40.0f, 8.0f);
			}
		});
	}

	void ProbeModel()
	{
		SKSE::GetTaskInterface()->AddTask([]() {
			auto* a = Target();
			auto* fn = a ? a->GetFaceNode() : nullptr;
			if (!fn) {
				logger::info("PROBE: no face node");
				return;
			}
			for (auto& c : fn->GetChildren()) {
				auto* g = c ? c->AsGeometry() : nullptr;
				auto* ds = g ? g->AsDynamicTriShape() : nullptr;
				if (!ds) {
					continue;
				}
				auto&       rd = ds->GetDynamicTrishapeRuntimeData();
				const auto* dyn = static_cast<const float*>(rd.dynamicData);
				const auto  nv = static_cast<std::size_t>(rd.dataSize / 16);
				logger::info("PROBE shape '{}' {} verts", ds->name.c_str(), nv);
				for (const char* nm : { "FOD", "FMD" }) {
					if (auto* e = ds->GetExtraData(nm)) {
						ScanObject(nm, e, nv, dyn, 2);
					}
				}
			}
			logger::info("PROBE done");
		});
	}
}
