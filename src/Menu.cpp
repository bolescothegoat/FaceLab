// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Bolescothegoat - FaceLab. See LICENSE.

#include "Input.h"
#include "Loc.h"
#include "Lab.h"
#include "Morph.h"
#include "Presets.h"
#include "Scenes.h"
#include "SKSEMenuFramework.h"

#include <algorithm>
#include <chrono>
#include <cstdarg>

#include <format>

#include <Windows.h>

namespace G = ImGuiMCP;

namespace FaceLab::Menu
{
	namespace
	{
		const G::ImVec4 kGood{ 0.45f, 0.90f, 0.45f, 1.0f };
		const G::ImVec4 kWarn{ 1.00f, 0.75f, 0.30f, 1.0f };

		// ---- translated drawing: every text goes through Loc (English falls through unchanged) ------------------------------------------
		std::string VFmt(const char* fmt, va_list ap)
		{
			va_list c;
			va_copy(c, ap);
			const int n = std::vsnprintf(nullptr, 0, fmt, c);
			va_end(c);
			std::string out(n > 0 ? static_cast<std::size_t>(n) : 0, '\0');
			if (n > 0) {
				std::vsnprintf(out.data(), out.size() + 1, fmt, ap);
			}
			return out;
		}

		void Text(const char* fmt, ...)
		{
			va_list ap;
			va_start(ap, fmt);
			const auto s = VFmt(Loc::T(fmt), ap);
			va_end(ap);
			G::TextUnformatted(s.c_str());
		}

		void TextWrapped(const char* fmt, ...)
		{
			va_list ap;
			va_start(ap, fmt);
			const auto s = VFmt(Loc::T(fmt), ap);
			va_end(ap);
			G::TextWrapped("%s", s.c_str());
		}

		void TextCol(const G::ImVec4 col, const char* fmt, ...)
		{
			va_list ap;
			va_start(ap, fmt);
			const auto s = VFmt(Loc::T(fmt), ap);
			va_end(ap);
			G::TextColored(col, "%s", s.c_str());
		}

		void SeparatorText(const char* label) { G::SeparatorText(Loc::T(label)); }
		bool Button(const char* label) { return G::Button(Loc::L(label)); }
		bool SmallButton(const char* label) { return G::SmallButton(Loc::L(label)); }
		bool Checkbox(const char* label, bool* v) { return G::Checkbox(Loc::L(label), v); }
		bool RadioButton(const char* label, int* v, int value) { return G::RadioButton(Loc::L(label), v, value); }
		bool SliderInt(const char* label, int* v, int lo, int hi, const char* fmt = "%d") { return G::SliderInt(Loc::L(label), v, lo, hi, Loc::T(fmt)); }

		// a text with {} holes, translated, then filled in
		template <class... A>
		std::string TF(const char* key, A&&... a)
		{
			try {
				return std::vformat(Loc::T(key), std::make_format_args(a...));
			} catch (...) {
				return std::vformat(key, std::make_format_args(a...));
			}
		}

		void Tip(const char* text)
		{
			if (text && *text && G::IsItemHovered()) {
				G::SetTooltip("%s", Loc::T(text));
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

			SeparatorText("Target");
			save |= RadioButton("The player", &s.target, 0);
			G::SameLine();
			save |= RadioButton("The actor I last looked at", &s.target, 1);
			G::SameLine();
			const std::string tn = TargetName();
			TextCol(kGood, "%s", Loc::T(tn.c_str()));
			Tip("Close this menu, look at an NPC for a moment, then open it again: the name appears here.");

			SeparatorText("Fade");
			G::PushItemWidth(320.0f);
			save |= SliderInt("Fade time (ms)", &s.lerpMs, 0, 3000);
			G::PopItemWidth();
			Tip("How long a face takes to arrive and to leave. 0 = instant.");
			G::PushItemWidth(320.0f);
			save |= SliderInt("Lab face holds for (seconds, 0 = until Reset)", &s.labHold, 0, 120);
			G::PopItemWidth();
			Tip("A face you apply here outranks every other mod's face while it is on, so it lets go by itself after this long.");
			save |= Checkbox("Keep a face until I reset it (ignores the time above)", &s.holdUntilReset);
			Tip("The face stays on the target until you press Reset (or the hotkey). Applies the next time you apply a face.");
			if (Applied()) {
				TextCol(kWarn, "A lab face is ON: it overrides other mods' faces on the target until it expires or you press Reset.");
			}
			save |= Checkbox("Apply a slider as soon as I move it", &s.live);

			SeparatorText("Apply");
			if (Button("Apply all sliders")) {
				ApplyAll();
			}
			G::SameLine();
			if (Button("Reset the target's face")) {
				Reset();
			}
			G::SameLine();
			TextCol(Applied() ? kGood : kWarn, "%s", Loc::T(Applied() ? "applied" : "not applied"));
			SeparatorText("Tongue");
			{
				const bool out = TongueOn();
				if (Button(out ? "Put the tongue away" : "Show the tongue")) {
					ToggleTongue();
				}
				Tip("The Tongue assist: puts the tongue out on the target on top of whatever face is showing, a preset or none. How far out it goes is on the Assists page (it is set to 100 for you if it was 0). The tongue itself is drawn by a tongue mod, which also chooses which tongue is used.");
				G::SameLine();
				TextCol(out ? kGood : kWarn, "%s", Loc::T(out ? "tongue out" : "tongue away"));
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
					// a tongue mod that has nothing to do (switched off, or no tongue found) never asks, but it can still say why
					const int st = TongueModStatus(TargetFormID());
					if (st == -1) {
						msg = "The tongue mod is switched off in its own menu.";
					} else if (st == -2) {
						msg = "The tongue mod found no tongue to put on (it needs a tongue mod such as Halo's HDT Tongues).";
					} else {
						msg = "A tongue mod is installed but is not asking FaceLab for tongues (is it switched on in its menu, and is it a version with FaceLab support?).";
					}
				} else if (TongueOn()) {
					switch (TongueModStatus(TargetFormID())) {
					case -1: msg = "The tongue mod is switched off in its own menu."; break;
					case -2: msg = "The tongue mod found no tongue to put on (it needs a tongue mod such as Halo's HDT Tongues)."; break;
					case 0: msg = "The tongue mod has not seen the request for this character yet."; break;
					case 1: msg = "The tongue mod skips this character: see its Who settings (player / others / females / males)."; break;
					case 2: msg = "The tongue mod is putting a tongue on her."; break;
					case 3: msg = "The tongue mod has a tongue on her."; good = true; break;
					default: break;
					}
				}
				if (msg) {
					TextCol(good ? kGood : kWarn, "%s", Loc::T(msg));
				}
			}

			SeparatorText("Face sliders");
			Text("Fill the sliders:");
			G::SameLine();
			if (SmallButton("Clear")) {
				LoadPreset(0);
			}
			G::SameLine();
			if (SmallButton("Just BigAah 3.0")) {
				LoadPreset(3);
			}
			TextWrapped("Values are weight x 100: 100 = 1.0, 300 = 3.0 (RaceMenu's top end).");

			const auto& ch = Channels();
			const char* group = "";
			for (std::size_t i = 0; i < ch.size(); ++i) {
				if (IsAssistGroup(ch[i].group)) {
					continue;
				}
				if (std::string_view(ch[i].group) != group) {
					group = ch[i].group;
					SeparatorText(group);
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
				SeparatorText(ok && !family.empty() ? TF("Extra morphs of this head - {}", family).c_str() : "Extra morphs of this head");
				if (!ok) {
					TextWrapped("The target has no supported head yet (or its morph files are still being read).");
				} else if (names.empty()) {
					TextWrapped("This head has no morphs beyond the standard ones above.");
				} else {
					TextWrapped("Names that only some heads have. Values are weight x 100, like the sliders above. Saved by name.");
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
						SeparatorText(whole ? "The whole face follows (the head and its face parts have the morph)" : "Head only (the mouth, lashes and brows have no such morph, so they may not follow)");
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
								if (G::IsItemHovered()) {
										G::SetTooltip("%s", TF("{} of the head's {} other face parts have this morph.", n.parts, n.partsTotal).c_str());
									}
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

			SeparatorText("Assists");
			TextWrapped("Helpers that sit on top of whatever face is showing. They are not part of a face and do not change your presets. Hover a title to see what it is for.");

			TextCol(kGood, "Mouth assist");
			Tip("Keeps the mouth open in oral scenes during the moments the scene has not opened it yet, for example the seconds after a position change or when a tongue is put in. It fades away as the scene's own open mouth takes over, so it never fights it. 0 = off.");
			{
				const int mi = ChannelIndex("MouthAssist");
				if (mi >= 0) {
					G::PushID("mouthassist");
					G::PushItemWidth(360.0f);
					if (SliderInt("How much (100 = 1.0)", &s.values[static_cast<std::size_t>(mi)], 0, 300)) {
						save = true;
						if (s.live) {
							ApplyOne(mi);
						}
					}
					G::PopItemWidth();
					G::PopID();
				}
			}

			TextCol(kGood, "Tongue assist");
			{
				G::PushID("tongueamount");
				G::PushItemWidth(360.0f);
				if (SliderInt("How far out (100 = fully)", &s.tongueOut, 0, 100)) {
					save = true;
					ApplyTongueAmount();
				}
				G::PopItemWidth();
				Tip("The Tongue assist: puts the tongue out on the target whatever face is showing, a preset you applied, another mod's face, or none. It is added on top, so it never replaces your face. The tongue itself is drawn by a tongue mod (FaceLab draws none). The same button is on the tongue hotkey on the Controls page.");
				G::PopID();
				const bool tl = TongueModInstalled();
				TextCol(tl ? kGood : kWarn, "%s", Loc::T(tl ? "A tongue mod is installed: it draws the tongue." : "No tongue mod was found: nothing will be drawn without one."));
				const int ti = ChannelIndex("TongueOut");
				if (ti >= 0 && s.values[static_cast<std::size_t>(ti)] > 0) {
					TextCol(kWarn, "The face in the sliders also asks for a tongue (%d) as part of itself.", s.values[static_cast<std::size_t>(ti)]);
					G::SameLine();
					if (SmallButton("Take it out of the face")) {
						s.values[static_cast<std::size_t>(ti)] = 0;
						save = true;
					}
				}
			}

			TextCol(kGood, "Oral assist");
			Tip("Tells FaceLab this is an oral scene, so the face you apply does not fight the scene's own mouth: emotion channels leave the lips alone, mouth channels give way as the scene's mouth opens, and the mouth is held through the short face rebuild when a tongue appears. Leave it off outside oral scenes.");
			{
				const int oi = ChannelIndex("OralMode");
				if (oi >= 0) {
					bool oral = s.values[static_cast<std::size_t>(oi)] >= 50;
					if (Checkbox("Treat this face as an oral scene", &oral)) {
						s.values[static_cast<std::size_t>(oi)] = oral ? 100 : 0;
						save = true;
						if (s.live) {
							ApplyOne(oi);
						}
					}
				}
			}

			TextCol(kGood, "Eye assist");
			Tip("On a head with add-on eyes (Advanced Eyes Addon) the eyeball is only a front cap. Turned far up or down, the dark rim of the cap comes into the lid opening as a thick dark line. This limits how far the eyes turn up or down (degrees, both eyes together with every gaze mod). Lower = a thinner line but the eyes follow you less vertically. Sideways looks are not limited by it, and heads with stock eyes are left alone.");
			G::PushID("vertlimit");
			G::PushItemWidth(360.0f);
			if (SliderInt("Most the eyes turn up / down (degrees)", &s.vertLimit, 4, 60)) {
				save = true;
				Morph::SetVertLimit(static_cast<float>(s.vertLimit));
			}
			G::PopItemWidth();
			G::PopID();
			G::PushID("lidfollow");
			G::PushItemWidth(360.0f);
			if (SliderInt("How much the lids follow a vertical look (%)", &s.lidFollow, 0, 250)) {
				save = true;
				Morph::SetLidFollow(static_cast<float>(s.lidFollow) / 100.0f);
			}
			G::PopItemWidth();
			Tip("The add-on eye textures have a dark strip baked into the top edge of each eye. The upper lid hides it at rest; when the eye looks down it can show as a dark lash-like line. Raising this makes the head's lids follow a look up or down more, so the upper lid comes down and covers it. 100 = the stock amount. Only heads with add-on eyes are affected; sideways looks are not.");
			G::PopID();
			G::PushID("shadowstrength");
			G::PushItemWidth(360.0f);
			if (SliderInt("Eye shadow strength (%)", &s.shadowStrength, 0, 100)) {
				save = true;
				Morph::SetShadowStrength(static_cast<float>(s.shadowStrength) / 100.0f);
			}
			G::PopItemWidth();
			Tip("Add-on eyes (Advanced Eyes Addon) have a black shadow shell over the upper eye whose darkness rises steeply towards the top, so a small lid movement shows a much darker band. This sets how opaque that shell is. 100 = the add-on's own look, lower = a lighter shadow, 0 = none. Only that shell is changed.");
			G::PopID();

			G::PushID("ubeeyes");
			SeparatorText("UBE eyes");
			G::PushItemWidth(360.0f);
			bool ubeChanged = false;
			ubeChanged |= SliderInt("Sideways (% change)", &s.ubeSide, -100, 100, "%+d %%");
			ubeChanged |= SliderInt("Down (% change)", &s.ubeDown, -100, 100, "%+d %%");
			ubeChanged |= SliderInt("Up (% change)", &s.ubeUp, -100, 100, "%+d %%");
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

			TextWrapped("Faces that mods ask for by name. Any mod can ship Data/SKSE/Plugins/FaceLab/Presets/<Mod>.toml. Pick one, load it into the sliders on the Face page, change it, and save it back.");
			if (Button("Reload preset files")) {
				Presets::Load();
				sel = -1;
			}
			G::SameLine();
			Text("%d presets", static_cast<int>(all.size()));
			if (Applied()) {
				TextCol(kWarn, "A lab face is ON (it overrides other mods' faces on the target). Press Reset on the Face page, or wait for it to expire.");
			}

			SeparatorText("Presets");
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
				Text("Selected: %s", p.name.c_str());
				if (Button("Load into the sliders")) {
					SlidersFrom(p);
					status = TF("Loaded {} into the sliders (Face page).", p.name);
				}
				G::SameLine();
				if (Button("Load and apply to the target")) {
					SlidersFrom(p);
					ApplyAll();
					status = TF("Applied {}.", p.name);
				}
				G::SameLine();
				if (Button("Save the sliders over this preset")) {
					status = Presets::Save(SlidersAs(p.name, p.file)) ? TF("Saved {} to {}", p.name, p.file) : TF("Could not write {}", p.file);
				}
			}

			SeparatorText("New preset");
			G::PushItemWidth(380.0f);
			G::InputText(Loc::L("Name"), newName, sizeof(newName));
			G::PopItemWidth();
			G::SameLine();
			if (Button("Save the sliders as this name")) {
				status = Presets::Save(SlidersAs(newName, "")) ? TF("Saved {} to User.toml", std::string(newName)) : std::string(Loc::T("Could not write User.toml"));
			}
			TextWrapped("Naming: Pack.Context.Name, e.g. MyPack.Blowjob. A name may end in .Displeased, .Neutral or .Pleased; a missing Displeased / Pleased falls back to the same name with Neutral, then the name without the ending.");
			if (!status.empty()) {
				TextCol(kGood, "%s", status.c_str());
			}
		}

		void BindRow(const char* label, const char* tip, Input::Capture which, int value, bool pad, bool optional)
		{
			G::PushID(label);
			Input::PollCapture();
			const bool capturing = Input::CurrentCapture() == which;
			Text("%-22s", Loc::T(label));
			G::SameLine(240.0f);
			if (capturing) {
				TextCol(kWarn, pad ? "press a gamepad button..." : "press a key... (Esc cancels)");
			} else {
				const std::string name = pad ? Input::PadName(value) : Input::KeyName(value);
				Text("%s", name.c_str());
			}
			G::SameLine(420.0f);
			if (SmallButton(capturing ? "Cancel" : "Set")) {
				Input::BeginCapture(capturing ? Input::Capture::None : which);
			}
			if (optional) {
				G::SameLine();
				if (SmallButton("Clear")) {
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
			SeparatorText("Scene rules");
			TextWrapped("When an OStim or SexLab scene starts, changes, or reaches an orgasm, FaceLab can put a preset on the actors it fits. Which preset goes with which kind of scene is decided by rule files in Data/SKSE/Plugins/FaceLab/Scenes/. FaceLab ships none; a rule pack (or your own files) supplies them.");
			save |= Checkbox("Apply faces from scene rules", &s.scenes);
			Tip("Off = the rules are ignored. Faces other mods ask for and the ones you apply by hand are not affected.");
			const auto info = Scenes::GetInfo();
			TextCol(info.ostim ? kGood : kWarn, "OStim: %s", Loc::T(info.ostim ? "found" : "not found"));
			G::SameLine();
			TextCol(info.sexlab ? kGood : kWarn, "   SexLab: %s", Loc::T(info.sexlab ? "found" : "not found"));
			Text("%d rule(s) loaded", info.rules);
			G::SameLine();
			if (SmallButton("Reload the rule files")) {
				Scenes::LoadRules();
			}
			Tip("Read Data/SKSE/Plugins/FaceLab/Scenes/*.toml again after you edit a rule. Presets are re-read on the Presets page.");
			for (const auto& p : info.problems) {
				TextCol(kWarn, "%s", p.c_str());
			}
			SeparatorText("What the rules did lately");
			if (info.log.empty()) {
				TextWrapped("Nothing yet. Start a scene; the rule that fits shows up here, newest last.");
			}
			for (const auto& l : info.log) {
				TextWrapped("%s", l.c_str());
			}
			if (save) {
				SaveConfig();
			}
		}

		void __stdcall PageDebug()
		{
			auto& s = GetSettings();
			bool  save = false;
			TextWrapped("Tools for finding out what a face or a head is doing. Off for normal use.");
			save |= Checkbox("Debug tools", &s.debug);
			Tip("Shows the Probe / Poke tests and writes measurement lines to FaceLab.log. Off for normal use.");
			if (s.debug) {
				save |= Checkbox("Also patch the second base-vertex copy (FMD)", &s.bump);
				Tip("The first copy (FOD) is always patched. Only turn this on if a face does not hold. Takes effect the next time you press Apply.");
				if (Button("Probe: find the face's base vertices (writes PROBE lines to the log)")) {
					ProbeModel();
				}
				if (Button("Poke test: lift the face mesh 40 units for 8 s")) {
					PokeTest();
				}
				SeparatorText("Hide a face shape (finds what draws something wrong)");
				TextWrapped("Tick a shape to hide it on the target right now; untick to bring it back. Hiding the one that draws the odd thing makes it vanish. Not saved.");
				{
					std::vector<std::string> shapeNames;
					ShapeSnapshot(shapeNames);
					for (const auto& n : shapeNames) {
						bool hid = ShapeHidden(n);
						if (Checkbox(n.c_str(), &hid)) {
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
			SeparatorText("Language");
			{
				// "Automatic" follows the game's language; the rest are the files in FaceLab/Languages
				static std::vector<std::string>              langs;
				static std::chrono::steady_clock::time_point lastScan{};
				const auto                                   nowT = std::chrono::steady_clock::now();
				if (langs.empty() || nowT - lastScan > std::chrono::seconds(3)) {
					langs = Loc::Available();
					lastScan = nowT;
				}
				std::vector<std::string> shown{ Loc::T("Automatic (the game's language)") };
				shown.insert(shown.end(), langs.begin(), langs.end());
				int pick = 0;
				if (s.language != "auto") {
					for (std::size_t i = 0; i < langs.size(); ++i) {
						if (langs[i] == s.language) {
							pick = static_cast<int>(i) + 1;
						}
					}
				}
				std::vector<const char*> ptrs;
				for (auto& n : shown) {
					ptrs.push_back(n.c_str());
				}
				G::PushItemWidth(320.0f);
				if (G::Combo(Loc::L("Language"), &pick, ptrs.data(), static_cast<int>(ptrs.size()))) {
					s.language = pick == 0 ? std::string("auto") : langs[static_cast<std::size_t>(pick) - 1];
					Loc::Set(s.language);
					SaveConfig();
				}
				G::PopItemWidth();
				Tip("Languages are the .toml files in Data/SKSE/Plugins/FaceLab/Languages (English.toml is the template to translate). Automatic uses the language the game is set to. The names in the left-hand list change after the next start of the game.");
				if (const auto need = Loc::FontAdvice(); !need.empty()) {
					TextCol(kWarn, "This language needs the font '%s' in SKSE Menu Framework (its settings page, Font), otherwise its letters show as ????. Restart the game after changing it.", need.c_str());
				}
			}

			SeparatorText("Hotkey");
			if (Checkbox("FaceLab hotkey enabled", &s.enabled)) {
				SaveConfig();
			}
			TextWrapped("The hotkey toggles: it applies the sliders to the target, and the next press resets the face. Keyboard and gamepad are separate. Defaults for the gamepad: X while holding D-pad Down.");
			BindRow("Keyboard key", "The key that toggles the face.", Input::Capture::KbKey, s.kbKey, false, true);
			BindRow("Keyboard modifier", "Optional: this key must be HELD when you press the key above.", Input::Capture::KbMod, s.kbMod, false, true);
			BindRow("Gamepad button", "The gamepad button that toggles the face.", Input::Capture::PadKey, s.padKey, true, true);
			BindRow("Gamepad modifier", "Optional: this button must be HELD when you press the button above.", Input::Capture::PadMod, s.padMod, true, true);

			SeparatorText("Tongue hotkey");
			TextWrapped("Shows or puts away the Tongue assist on the target, on top of whatever face is showing. Needs a tongue mod that works with FaceLab. Unset by default.");
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
		SKSEMenuFramework::AddSectionItem(Loc::T("Face"), PageFace);
		SKSEMenuFramework::AddSectionItem(Loc::T("Assists"), PageAssists);
		SKSEMenuFramework::AddSectionItem(Loc::T("Presets"), PagePresets);
		SKSEMenuFramework::AddSectionItem(Loc::T("Scenes"), PageScenes);
		SKSEMenuFramework::AddSectionItem(Loc::T("Controls"), PageControls);
		SKSEMenuFramework::AddSectionItem(Loc::T("Debug"), PageDebug);
		logger::info("SKSE Menu Framework pages registered (section: FaceLab)");
	}
}
