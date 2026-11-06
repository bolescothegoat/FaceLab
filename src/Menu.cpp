// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Bolescothegoat - FaceLab. See LICENSE.

#include "Input.h"
#include "Lab.h"
#include "Morph.h"
#include "Presets.h"
#include "SKSEMenuFramework.h"

#include <algorithm>

#include <format>

namespace G = ImGuiMCP;

namespace FaceLab::Menu
{
	namespace
	{
		const G::ImVec4 kGood{ 0.45f, 0.90f, 0.45f, 1.0f };
		const G::ImVec4 kWarn{ 1.00f, 0.75f, 0.30f, 1.0f };

		void Tip(const char* text)
		{
			if (text && *text && G::IsItemHovered()) {
				G::SetTooltip("%s", text);
			}
		}

		void __stdcall PageFace()
		{
			auto& s = GetSettings();
			bool  save = false;

			G::SeparatorText("Target");
			save |= G::RadioButton("The player", &s.target, 0);
			G::SameLine();
			save |= G::RadioButton("The actor I last looked at", &s.target, 1);
			G::SameLine();
			const std::string tn = TargetName();
			G::TextColored(kGood, "%s", tn.c_str());
			Tip("Close this menu, look at an NPC for a moment, then open it again: the name appears here.");

			G::SeparatorText("Fade");
			G::PushItemWidth(320.0f);
			save |= G::SliderInt("Fade time (ms)", &s.lerpMs, 0, 3000);
			G::PopItemWidth();
			Tip("How long a face takes to arrive and to leave. 0 = instant.");
			G::PushItemWidth(320.0f);
			save |= G::SliderInt("Lab face holds for (seconds, 0 = until Reset)", &s.labHold, 0, 120);
			G::PopItemWidth();
			Tip("A face you apply here outranks every other mod's face while it is on, so it lets go by itself after this long.");
			if (Applied()) {
				G::TextColored(kWarn, "A lab face is ON: it overrides other mods' faces on the target until it expires or you press Reset.");
			}
			save |= G::Checkbox("Apply a slider as soon as I move it", &s.live);

			G::SeparatorText("Do it");
			if (G::Button("Apply all sliders")) {
				ApplyAll();
			}
			G::SameLine();
			if (G::Button("Reset the target's face")) {
				Reset();
			}
			G::SameLine();
			G::TextColored(Applied() ? kGood : kWarn, "%s", Applied() ? "applied" : "not applied");

			save |= G::Checkbox("Debug tools", &s.debug);
			Tip("Shows the Probe / Poke tests and writes measurement lines to FaceLab.log. Off for normal use.");
			if (s.debug) {
				save |= G::Checkbox("Also patch the second base-vertex copy (FMD)", &s.bump);
				Tip("The first copy (FOD) is always patched. Only turn this on if a face does not hold. Takes effect the next time you press Apply.");
				if (G::Button("Probe: find the face's base vertices (writes PROBE lines to the log)")) {
					ProbeModel();
				}
				if (G::Button("Poke test: lift the face mesh 40 units for 8 s")) {
					PokeTest();
				}
			}

			G::Text("Fill the sliders:");
			G::SameLine();
			if (G::SmallButton("Clear")) {
				LoadPreset(0);
			}
			G::SameLine();
			if (G::SmallButton("Just BigAah 3.0")) {
				LoadPreset(3);
			}
			G::TextWrapped("Values are weight x 100: 100 = 1.0, 300 = 3.0 (RaceMenu's top end).");

			const auto& ch = Channels();
			const char* group = "";
			for (std::size_t i = 0; i < ch.size(); ++i) {
				if (std::string_view(ch[i].group) != group) {
					group = ch[i].group;
					G::SeparatorText(group);
				}
				G::PushID(static_cast<int>(i));
				G::PushItemWidth(360.0f);
				if (G::SliderInt(ch[i].name, &s.values[i], 0, 300)) {
					save = true;
					if (s.live) {
						ApplyOne(static_cast<int>(i));
					}
				}
				G::PopItemWidth();
				G::PopID();
			}

			// morphs only this head has (a CoTR head's Smile001 ..., a UBE head's Ahegao ...)
			{
				std::string            family;
				std::vector<ExtraName> names;
				const bool             ok = ExtrasSnapshot(family, names);
				G::SeparatorText(ok && !family.empty() ? ("Extra morphs of this head - " + family).c_str() : "Extra morphs of this head");
				if (!ok) {
					G::TextWrapped("The target has no supported head yet (or its morph files are still being read).");
				} else if (names.empty()) {
					G::TextWrapped("This head has no morphs beyond the standard ones above.");
				} else {
					G::TextWrapped("Names that only some heads have. Values are weight x 100, like the sliders above. Saved by name.");
					for (const bool whole : { true, false }) {
						bool any = false;
						for (const auto& n : names) {
							if ((n.parts > 0) == whole) {
								any = true;
							}
						}
						if (!any) {
							continue;
						}
						G::SeparatorText(whole ? "The whole face follows (the head and its face parts have the morph)" : "Head only (the mouth, lashes and brows have no such morph, so they may not follow)");
						for (const auto& n : names) {
							if ((n.parts > 0) != whole) {
								continue;
							}
							int& v = s.extras[n.name];
							G::PushID(n.name.c_str());
							G::PushItemWidth(360.0f);
							if (G::SliderInt(n.name.c_str(), &v, 0, 300)) {
								save = true;
								if (s.live) {
									ApplyOne(0);
								}
							}
							G::PopItemWidth();
							if (n.partsTotal > 0) {
								Tip(std::format("{} of the head's {} other face parts have this morph.", n.parts, n.partsTotal).c_str());
							}
							G::PopID();
						}
					}
				}
			}

			if (save) {
				SaveConfig();
			}
		}

		// the sliders as a preset, and the other way round
		Presets::Preset SlidersAs(const std::string& name, const std::string& file)
		{
			Presets::Preset p;
			p.name = name;
			p.file = file;
			const auto& s = GetSettings();
			for (std::size_t i = 0; i < s.values.size(); ++i) {
				if (s.values[i] != 0) {
					p.weights.push_back({ Channels()[i].name, s.values[i] });
				}
			}
			for (const auto& [morph, v] : s.extras) {
				if (v != 0) {
					p.weights.push_back({ morph, v });
				}
			}
			return p;
		}

		void SlidersFrom(const Presets::Preset& p)
		{
			auto& v = GetSettings().values;
			std::ranges::fill(v, 0);
			GetSettings().extras.clear();
			for (const auto& [ch, w] : p.weights) {
				bool standard = false;
				for (std::size_t i = 0; i < Channels().size(); ++i) {
					if (std::string_view(Channels()[i].name) == ch) {
						v[i] = w;
						standard = true;
					}
				}
				if (!standard && w != 0) {
					GetSettings().extras[ch] = std::clamp(w, 0, 300);  // a name only some heads have
				}
			}
			SaveConfig();
		}

		void __stdcall PagePresets()
		{
			static int         sel = -1;
			static char        newName[96] = "MyMod.Context.Pleased";
			static std::string status;
			auto               all = Presets::All();

			G::TextWrapped("Faces that mods ask for by name. Any mod can ship Data/SKSE/Plugins/FaceLab/Presets/<Mod>.toml. Pick one, load it into the sliders on the Face page, change it, and save it back.");
			if (G::Button("Reload preset files")) {
				Presets::Load();
				sel = -1;
			}
			G::SameLine();
			G::Text("%d presets", static_cast<int>(all.size()));
			if (Applied()) {
				G::TextColored(kWarn, "A lab face is ON (it overrides other mods' faces on the target). Press Reset on the Face page, or wait for it to expire.");
			}

			G::SeparatorText("Presets");
			if (G::BeginChild("##presetlist", G::ImVec2(0.0f, 260.0f), 0, 0)) {
				for (int i = 0; i < static_cast<int>(all.size()); ++i) {
					const std::string label = std::format("{}   ({})##{}", all[static_cast<std::size_t>(i)].name, all[static_cast<std::size_t>(i)].file, i);
					if (G::Selectable(label.c_str(), sel == i)) {
						sel = i;
					}
				}
			}
			G::EndChild();

			const bool have = sel >= 0 && sel < static_cast<int>(all.size());
			if (have) {
				const auto& p = all[static_cast<std::size_t>(sel)];
				G::Text("Selected: %s", p.name.c_str());
				if (G::Button("Load into the sliders")) {
					SlidersFrom(p);
					status = "Loaded " + p.name + " into the sliders (Face page).";
				}
				G::SameLine();
				if (G::Button("Load and apply to the target")) {
					SlidersFrom(p);
					ApplyAll();
					status = "Applied " + p.name + ".";
				}
				G::SameLine();
				if (G::Button("Save the sliders over this preset")) {
					status = Presets::Save(SlidersAs(p.name, p.file)) ? "Saved " + p.name + " to " + p.file : "Could not write " + p.file;
				}
			}

			G::SeparatorText("New preset");
			G::PushItemWidth(380.0f);
			G::InputText("Name", newName, sizeof(newName));
			G::PopItemWidth();
			G::SameLine();
			if (G::Button("Save the sliders as this name")) {
				status = Presets::Save(SlidersAs(newName, "")) ? std::string("Saved ") + newName + " to User.toml" : "Could not write User.toml";
			}
			G::TextWrapped("Naming: Mod.Context.Disposition, e.g. MyMod.Masturbation.Pleased. A missing Displeased / Pleased falls back to the same name with Neutral, then Mod.Displeased / Mod.Pleased, then Mod.Neutral.");
			if (!status.empty()) {
				G::TextColored(kGood, "%s", status.c_str());
			}
		}

		void BindRow(const char* label, const char* tip, Input::Capture which, int value, bool pad, bool optional)
		{
			G::PushID(label);
			const bool capturing = Input::CurrentCapture() == which;
			G::Text("%-22s", label);
			G::SameLine(240.0f);
			if (capturing) {
				G::TextColored(kWarn, pad ? "press a gamepad button..." : "press a key... (Esc cancels)");
			} else {
				const std::string name = pad ? Input::PadName(value) : Input::KeyName(value);
				G::Text("%s", name.c_str());
			}
			G::SameLine(420.0f);
			if (G::SmallButton(capturing ? "Cancel" : "Set")) {
				Input::BeginCapture(capturing ? Input::Capture::None : which);
			}
			if (optional) {
				G::SameLine();
				if (G::SmallButton("Clear")) {
					auto& s = GetSettings();
					switch (which) {
					case Input::Capture::KbMod: s.kbMod = -1; break;
					case Input::Capture::PadMod: s.padMod = 0; break;
					case Input::Capture::KbKey: s.kbKey = -1; break;
					case Input::Capture::PadKey: s.padKey = 0; break;
					default: break;
					}
					SaveConfig();
				}
			}
			Tip(tip);
			G::PopID();
		}

		void __stdcall PageControls()
		{
			auto& s = GetSettings();
			G::SeparatorText("Hotkey");
			if (G::Checkbox("FaceLab hotkey enabled", &s.enabled)) {
				SaveConfig();
			}
			G::TextWrapped("The hotkey toggles: it applies the sliders to the target, and the next press resets the face. Keyboard and gamepad are separate. Defaults for the gamepad: X while holding D-pad Down.");
			BindRow("Keyboard key", "The key that toggles the face.", Input::Capture::KbKey, s.kbKey, false, true);
			BindRow("Keyboard modifier", "Optional: this key must be HELD when you press the key above.", Input::Capture::KbMod, s.kbMod, false, true);
			BindRow("Gamepad button", "The gamepad button that toggles the face.", Input::Capture::PadKey, s.padKey, true, true);
			BindRow("Gamepad modifier", "Optional: this button must be HELD when you press the button above.", Input::Capture::PadMod, s.padMod, true, true);
		}
	}

	void Register()
	{
		if (!SKSEMenuFramework::IsInstalled()) {
			logger::info("SKSE Menu Framework is not installed: no in-game menu");
			return;
		}
		SKSEMenuFramework::SetSection("FaceLab");
		SKSEMenuFramework::AddSectionItem("Face", PageFace);
		SKSEMenuFramework::AddSectionItem("Presets", PagePresets);
		SKSEMenuFramework::AddSectionItem("Controls", PageControls);
		logger::info("SKSE Menu Framework pages registered (section: FaceLab)");
	}
}
