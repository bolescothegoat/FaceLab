// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Bolescothegoat - FaceLab. See LICENSE.

#include "Input.h"
#include "Lab.h"
#include "Morph.h"
#include "Presets.h"
#include "Scenes.h"
#include "SKSEMenuFramework.h"

#include <algorithm>

#include <format>

#include <Windows.h>

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

		// index of a channel in the lab's list (-1 = not there)
		int ChannelIndex(const char* name)
		{
			const auto& c = Channels();
			for (std::size_t i = 0; i < c.size(); ++i) {
				if (std::string_view(c[i].name) == name) {
					return static_cast<int>(i);
				}
			}
			return -1;
		}

		// the three assist channels are not part of a face: they have their own section at the end of the Face page
		bool IsAssistGroup(const char* group)
		{
			const std::string_view g = group;
			return g == "Mouth assist" || g == "Tongue" || g == "Oral";
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
			save |= G::Checkbox("Keep a face until I reset it (ignores the time above)", &s.holdUntilReset);
			Tip("The face stays on the target until you press Reset (or the hotkey). Applies the next time you apply a face.");
			if (Applied()) {
				G::TextColored(kWarn, "A lab face is ON: it overrides other mods' faces on the target until it expires or you press Reset.");
			}
			save |= G::Checkbox("Apply a slider as soon as I move it", &s.live);

			G::SeparatorText("Apply");
			if (G::Button("Apply all sliders")) {
				ApplyAll();
			}
			G::SameLine();
			if (G::Button("Reset the target's face")) {
				Reset();
			}
			G::SameLine();
			G::TextColored(Applied() ? kGood : kWarn, "%s", Applied() ? "applied" : "not applied");
			G::SeparatorText("Tongue");
			{
				const bool out = TongueOn();
				if (G::Button(out ? "Put the tongue away" : "Show the tongue")) {
					ToggleTongue();
				}
				Tip("The Tongue assist: puts the tongue out on the target on top of whatever face is showing, a preset or none. How far out it goes is on the Assists page (it is set to 100 for you if it was 0). The tongue itself is drawn by a tongue mod, which also chooses which tongue is used.");
				G::SameLine();
				G::TextColored(out ? kGood : kWarn, "%s", out ? "tongue out" : "tongue away");
			}
			{
				// is a tongue mod listening, and what does it make of the request? (so a missing tongue says why)
				const long long ago = Morph::TonguePolledMsAgo();
				const bool      installed = TongueModInstalled() || (ago >= 0 && ago <= 3000);
				const char*     msg = nullptr;
				bool            good = false;
				if (!installed) {
					msg = "No tongue will be drawn: no tongue mod that works with FaceLab was found.";
				} else if (ago < 0 || ago > 3000) {
					msg = "A tongue mod is installed but is not asking FaceLab for tongues (is it switched on in its menu, and is it a version with FaceLab support?).";
				} else if (TongueOn()) {
					switch (TongueModStatus(TargetFormID())) {
					case -1: msg = "The tongue mod is switched off in its own menu."; break;
					case -2: msg = "The tongue mod found no tongue armor (it needs the OStim / Halo tongue equip objects)."; break;
					case 0: msg = "The tongue mod has not seen the request for this character yet."; break;
					case 1: msg = "The tongue mod skips this character: see its Who settings (player / others / females / males)."; break;
					case 2: msg = "The tongue mod is putting a tongue on her."; break;
					case 3: msg = "The tongue mod has a tongue on her."; good = true; break;
					default: break;
					}
				}
				if (msg) {
					G::TextColored(good ? kGood : kWarn, "%s", msg);
				}
			}

			G::SeparatorText("Face sliders");
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
				if (IsAssistGroup(ch[i].group)) {
					continue;
				}
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

		void __stdcall PageAssists()
		{
			auto& s = GetSettings();
			bool  save = false;

			G::SeparatorText("Assists");
			G::TextWrapped("Helpers that sit on top of whatever face is showing. They are not part of a face and do not change your presets. Hover a title to see what it is for.");

			G::TextColored(kGood, "Mouth assist");
			Tip("Keeps the mouth open in oral scenes during the moments the scene has not opened it yet, for example the seconds after a position change or when a tongue is put in. It fades away as the scene's own open mouth takes over, so it never fights it. 0 = off.");
			{
				const int mi = ChannelIndex("MouthAssist");
				if (mi >= 0) {
					G::PushID("mouthassist");
					G::PushItemWidth(360.0f);
					if (G::SliderInt("How much (100 = 1.0)", &s.values[static_cast<std::size_t>(mi)], 0, 300)) {
						save = true;
						if (s.live) {
							ApplyOne(mi);
						}
					}
					G::PopItemWidth();
					G::PopID();
				}
			}

			G::TextColored(kGood, "Tongue assist");
			{
				G::PushID("tongueamount");
				G::PushItemWidth(360.0f);
				if (G::SliderInt("How far out (100 = fully)", &s.tongueOut, 0, 100)) {
					save = true;
					ApplyTongueAmount();
				}
				G::PopItemWidth();
				Tip("The Tongue assist: puts the tongue out on the target whatever face is showing, a preset you applied, another mod's face, or none. It is added on top, so it never replaces your face. The tongue itself is drawn by a tongue mod (FaceLab draws none). The same button is on the tongue hotkey on the Controls page.");
				G::PopID();
				const bool tl = TongueModInstalled();
				G::TextColored(tl ? kGood : kWarn, "%s", tl ? "A tongue mod is installed: it draws the tongue." : "No tongue mod was found: nothing will be drawn without one.");
				const int ti = ChannelIndex("TongueOut");
				if (ti >= 0 && s.values[static_cast<std::size_t>(ti)] > 0) {
					G::TextColored(kWarn, "The face in the sliders also asks for a tongue (%d) as part of itself.", s.values[static_cast<std::size_t>(ti)]);
					G::SameLine();
					if (G::SmallButton("Take it out of the face")) {
						s.values[static_cast<std::size_t>(ti)] = 0;
						save = true;
					}
				}
			}

			G::TextColored(kGood, "Oral assist");
			Tip("Tells FaceLab this is an oral scene, so the face you apply does not fight the scene's own mouth: emotion channels leave the lips alone, mouth channels give way as the scene's mouth opens, and the mouth is held through the short face rebuild when a tongue appears. Leave it off outside oral scenes.");
			{
				const int oi = ChannelIndex("OralMode");
				if (oi >= 0) {
					bool oral = s.values[static_cast<std::size_t>(oi)] >= 50;
					if (G::Checkbox("Treat this face as an oral scene", &oral)) {
						s.values[static_cast<std::size_t>(oi)] = oral ? 100 : 0;
						save = true;
						if (s.live) {
							ApplyOne(oi);
						}
					}
				}
			}

			G::TextColored(kGood, "Eye assist");
			Tip("On a head with add-on eyes (Advanced Eyes Addon) the eyeball is only a front cap. Turned far up or down, the dark rim of the cap comes into the lid opening as a thick dark line. This limits how far the eyes turn up or down (degrees, both eyes together with every gaze mod). Lower = a thinner line but the eyes follow you less vertically. Sideways looks are not limited by it, and heads with stock eyes are left alone.");
			G::PushID("vertlimit");
			G::PushItemWidth(360.0f);
			if (G::SliderInt("Most the eyes turn up / down (degrees)", &s.vertLimit, 4, 60)) {
				save = true;
				Morph::SetVertLimit(static_cast<float>(s.vertLimit));
			}
			G::PopItemWidth();
			G::PopID();
			G::PushID("lidfollow");
			G::PushItemWidth(360.0f);
			if (G::SliderInt("How much the lids follow a vertical look (%)", &s.lidFollow, 0, 250)) {
				save = true;
				Morph::SetLidFollow(static_cast<float>(s.lidFollow) / 100.0f);
			}
			G::PopItemWidth();
			Tip("The add-on eye textures have a dark strip baked into the top edge of each eye. The upper lid hides it at rest; when the eye looks down it can show as a dark lash-like line. Raising this makes the head's lids follow a look up or down more, so the upper lid comes down and covers it. 100 = the stock amount. Only heads with add-on eyes are affected; sideways looks are not.");
			G::PopID();
			G::PushID("shadowstrength");
			G::PushItemWidth(360.0f);
			if (G::SliderInt("Eye shadow strength (%)", &s.shadowStrength, 0, 100)) {
				save = true;
				Morph::SetShadowStrength(static_cast<float>(s.shadowStrength) / 100.0f);
			}
			G::PopItemWidth();
			Tip("Add-on eyes (Advanced Eyes Addon) have a black shadow shell over the upper eye whose darkness rises steeply towards the top, so a small lid movement shows a much darker band. This sets how opaque that shell is. 100 = the add-on's own look, lower = a lighter shadow, 0 = none. Only that shell is changed.");
			G::PopID();

			G::PushID("ubeeyes");
			G::SeparatorText("UBE eyes");
			G::PushItemWidth(360.0f);
			bool ubeChanged = false;
			ubeChanged |= G::SliderInt("Sideways (% change)", &s.ubeSide, -100, 100, "%+d %%");
			ubeChanged |= G::SliderInt("Down (% change)", &s.ubeDown, -100, 100, "%+d %%");
			ubeChanged |= G::SliderInt("Up (% change)", &s.ubeUp, -100, 100, "%+d %%");
			G::PopItemWidth();
			if (ubeChanged) {
				save = true;
				Morph::SetUbeAdjust(s.ubeSide, s.ubeDown, s.ubeUp);
			}
			Tip("UBE heads ship eye morphs that move the eye much less than a vanilla-style head's, so FaceLab already turns UBE eyes further to match: 0 is that calibrated look and is where it should normally stay. Move a slider right to turn the eyes further in that direction (+100 = twice as much), left to turn them less (-100 = not at all). If she falls short of the camera, go right; if she overshoots or the eye comes out over the lid, go left. Only UBE heads are affected.");
			G::PopID();


			if (save) {
				SaveConfig();
			}
		}

		void __stdcall PagePresets()
		{
			static int         sel = -1;
			static char        newName[96] = "MyPack.Context.Neutral";
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
			G::TextWrapped("Naming: Pack.Context.Name, e.g. MyPack.Blowjob. A name may end in .Displeased, .Neutral or .Pleased; a missing Displeased / Pleased falls back to the same name with Neutral, then the name without the ending.");
			if (!status.empty()) {
				G::TextColored(kGood, "%s", status.c_str());
			}
		}

		void BindRow(const char* label, const char* tip, Input::Capture which, int value, bool pad, bool optional)
		{
			G::PushID(label);
			Input::PollCapture();
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
					case Input::Capture::TongueKbMod: s.tKbMod = -1; break;
					case Input::Capture::TonguePadMod: s.tPadMod = 0; break;
					case Input::Capture::TongueKbKey: s.tKbKey = -1; break;
					case Input::Capture::TonguePadKey: s.tPadKey = 0; break;
					default: break;
					}
					SaveConfig();
				}
			}
			Tip(tip);
			G::PopID();
		}

		void __stdcall PageScenes()
		{
			auto& s = GetSettings();
			bool  save = false;
			G::SeparatorText("Scene rules");
			G::TextWrapped("When an OStim or SexLab scene starts, changes, or reaches an orgasm, FaceLab can put a preset on the actors it fits. Which preset goes with which kind of scene is decided by rule files in Data/SKSE/Plugins/FaceLab/Scenes/. FaceLab ships none; a rule pack (or your own files) supplies them.");
			save |= G::Checkbox("Apply faces from scene rules", &s.scenes);
			Tip("Off = the rules are ignored. Faces other mods ask for and the ones you apply by hand are not affected.");
			const auto info = Scenes::GetInfo();
			G::TextColored(info.ostim ? kGood : kWarn, "OStim: %s", info.ostim ? "found" : "not found");
			G::SameLine();
			G::TextColored(info.sexlab ? kGood : kWarn, "   SexLab: %s", info.sexlab ? "found" : "not found");
			G::Text("%d rule(s) loaded", info.rules);
			G::SameLine();
			if (G::SmallButton("Reload the rule files")) {
				Scenes::LoadRules();
			}
			Tip("Read Data/SKSE/Plugins/FaceLab/Scenes/*.toml again after you edit a rule. Presets are re-read on the Presets page.");
			for (const auto& p : info.problems) {
				G::TextColored(kWarn, "%s", p.c_str());
			}
			G::SeparatorText("What the rules did lately");
			if (info.log.empty()) {
				G::TextWrapped("Nothing yet. Start a scene; the rule that fits shows up here, newest last.");
			}
			for (const auto& l : info.log) {
				G::TextWrapped("%s", l.c_str());
			}
			if (save) {
				SaveConfig();
			}
		}

		void __stdcall PageDebug()
		{
			auto& s = GetSettings();
			bool  save = false;
			G::TextWrapped("Tools for finding out what a face or a head is doing. Off for normal use.");
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
				G::SeparatorText("Hide a face shape (finds what draws something wrong)");
				G::TextWrapped("Tick a shape to hide it on the target right now; untick to bring it back. Hiding the one that draws the odd thing makes it vanish. Not saved.");
				{
					std::vector<std::string> shapeNames;
					ShapeSnapshot(shapeNames);
					for (const auto& n : shapeNames) {
						bool hid = ShapeHidden(n);
						if (G::Checkbox(n.c_str(), &hid)) {
							SetShapeHidden(n, hid);
						}
					}
				}
			}

			if (save) {
				SaveConfig();
			}
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

			G::SeparatorText("Tongue hotkey");
			G::TextWrapped("Shows or puts away the Tongue assist on the target, on top of whatever face is showing. Needs a tongue mod that works with FaceLab. Unset by default.");
			BindRow("Tongue: keyboard key", "The key that toggles the tongue.", Input::Capture::TongueKbKey, s.tKbKey, false, true);
			BindRow("Tongue: keyboard modifier", "Optional: this key must be HELD when you press the key above.", Input::Capture::TongueKbMod, s.tKbMod, false, true);
			BindRow("Tongue: gamepad button", "The gamepad button that toggles the tongue.", Input::Capture::TonguePadKey, s.tPadKey, true, true);
			BindRow("Tongue: gamepad modifier", "Optional: this button must be HELD when you press the button above.", Input::Capture::TonguePadMod, s.tPadMod, true, true);
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
		SKSEMenuFramework::AddSectionItem("Assists", PageAssists);
		SKSEMenuFramework::AddSectionItem("Presets", PagePresets);
		SKSEMenuFramework::AddSectionItem("Scenes", PageScenes);
		SKSEMenuFramework::AddSectionItem("Controls", PageControls);
		SKSEMenuFramework::AddSectionItem("Debug", PageDebug);
		logger::info("SKSE Menu Framework pages registered (section: FaceLab)");
	}
}
