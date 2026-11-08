// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Bolescothegoat - FaceLab. See LICENSE.

#include "Api.h"
#include "Lab.h"
#include "Morph.h"
#include "Presets.h"

#include "../api/FaceLabAPI.h"

namespace FaceLab::Api
{
	namespace
	{
		struct Layer
		{
			std::string   owner;
			int           priority{ 0 };
			std::uint64_t seq{ 0 };
			Weights       weights;
		};

		std::mutex                               g_mx;
		std::map<RE::FormID, std::vector<Layer>> g_layers;  // game thread, but the lock is cheap and keeps ClearAll safe
		std::uint64_t                            g_seq = 0;

		std::map<RE::FormID, std::map<std::string, float>> g_yield;       // who asked for the mouth to be given up, and how much (0..1)
		std::map<RE::FormID, std::string>                  g_lastWinner;  // for the log: which layer an actor showed last

		bool IsMouthChannel(const std::string& name)
		{
			for (const auto& c : Channels()) {
				if (name == c.name) {
					return std::string_view(c.group) == "Mouth" || name == "MouthAssist";  // the assist is a mouth shape too: a spoken line takes it
				}
			}
			return false;
		}

		// show the winning layer of this actor (or take the face out when none is left); mouth channels are scaled down while someone speaks
		void Show(RE::Actor* a, float fade)
		{
			Weights     best;
			bool        any = false;
			std::string winner = "(none)";
			std::string others;
			float       yield = 0.0f;
			{
				std::lock_guard lk(g_mx);
				const auto      it = g_layers.find(a->GetFormID());
				if (it != g_layers.end() && !it->second.empty()) {
					const Layer* win = &it->second.front();
					for (const auto& l : it->second) {
						if (l.priority > win->priority || (l.priority == win->priority && l.seq > win->seq)) {
							win = &l;
						}
					}
					for (const auto& l : it->second) {
						if (&l != win) {
							others += std::format(" {}({})", l.owner, l.priority);
						}
					}
					winner = std::format("{} ({})", win->owner, win->priority);
					best = win->weights;
					any = true;
				}
				if (const auto y = g_yield.find(a->GetFormID()); y != g_yield.end()) {
					for (const auto& [o, v] : y->second) {
						yield = std::max(yield, v);
					}
				}
				auto& last = g_lastWinner[a->GetFormID()];
				if (last != winner + others) {
					last = winner + others;
					logger::info("{} now shows: {}{}", a->GetDisplayFullName(), winner, others.empty() ? "" : "   (also layered:" + others + ")");
				}
			}
			if (any) {
				if (yield > 0.0f) {
					for (auto& [name, w] : best) {
						if (IsMouthChannel(name)) {
							w *= (1.0f - yield);
						}
					}
				}
				Morph::Set(a, best, fade);
			} else {
				Morph::Clear(a, fade);
			}
		}

		void Put(RE::FormID id, const std::string& owner, Weights w, float fade, int priority)
		{
			auto* a = RE::TESForm::LookupByID<RE::Actor>(id);
			if (!a) {
				return;
			}
			if (Morph::SupportState(a) == 0) {
				return;  // a head we have no morphs for: no face, and so no tongue or mouth hold either (the caller keeps its own expressions)
			}
			{
				std::lock_guard lk(g_mx);
				auto&           stack = g_layers[id];
				Layer*          mine = nullptr;
				for (auto& l : stack) {
					if (l.owner == owner) {
						mine = &l;
					}
				}
				if (!mine) {
					stack.emplace_back();
					mine = &stack.back();
					mine->owner = owner;
				}
				mine->priority = priority;
				mine->seq = ++g_seq;
				mine->weights = std::move(w);
			}
			Show(a, fade);
		}

		// ---- Papyrus ----
		bool P_ApplyFace(RE::StaticFunctionTag*, RE::Actor* a, RE::BSFixedString owner, RE::BSFixedString preset, float fade, std::int32_t priority)
		{
			return a && ApplyPreset(a->GetFormID(), owner.c_str(), preset.c_str(), fade, priority);
		}

		void P_ClearFace(RE::StaticFunctionTag*, RE::Actor* a, RE::BSFixedString owner, float fade)
		{
			if (a) {
				Clear(a->GetFormID(), owner.c_str(), fade);
			}
		}

			// ---- gaze layer ----
			// Eye channels added on top of the face an actor shows (see Morph::SetGaze). Several mods can each hold a gaze on the same actor: they are summed (0..3).
			std::mutex                                           g_gzMx;
			std::map<RE::FormID, std::map<std::string, Weights>> g_gaze;  // actor -> owner -> eye weights

			bool IsEyeChannel(const std::string& name)
			{
				for (const auto& c : Channels()) {
					if (name == c.name) {
						return std::string_view(c.group).starts_with("Eyes");  // "Eyes and brows", "Eyes - each eye separately", "Eyes - true rotation ..."
					}
				}
				return false;
			}

			// g_gzMx held
			void PushGaze(RE::FormID id, float fade)
			{
				std::map<std::string, float> acc;
				if (const auto it = g_gaze.find(id); it != g_gaze.end()) {
					for (const auto& [owner, w] : it->second) {
						for (const auto& [name, v] : w) {
							acc[name] += v;
						}
					}
				}
				if (acc.empty()) {
					Morph::ClearGaze(id, fade);
					return;
				}
				Weights sum;
				for (const auto& [name, v] : acc) {
					sum.push_back({ name, std::clamp(v, 0.0f, 3.0f) });
				}
				Morph::SetGaze(id, sum, fade);
			}

			// "Channel=1.5;OtherChannel=0.5" (RaceMenu units, 0 - 3). Unknown channel names are skipped and logged.
			Weights ParseText(const char* a_text)
			{
				Weights          w;
				std::string_view s(a_text ? a_text : "");
				std::size_t      pos = 0;
				while (pos < s.size()) {
					auto end = s.find(';', pos);
					if (end == std::string_view::npos) {
						end = s.size();
					}
					const auto item = s.substr(pos, end - pos);
					pos = end + 1;
					const auto eq = item.find('=');
					if (eq == std::string_view::npos) {
						continue;
					}
					std::string name(item.substr(0, eq));
					while (!name.empty() && name.front() == ' ') name.erase(name.begin());
					while (!name.empty() && name.back() == ' ') name.pop_back();
					float val = 0.0f;
					try {
						val = std::stof(std::string(item.substr(eq + 1)));
					} catch (...) {
						continue;
					}
					bool known = false;
					for (const auto& c : Channels()) {
						if (name == c.name) {
							known = true;
							break;
						}
					}
					if (!known) {
						logger::info("ApplyWeightsText: no channel '{}'", name);
						continue;
					}
					w.push_back({ name, std::clamp(val, 0.0f, 3.0f) });
				}
				return w;
			}

			// Developer / test helper: show any face from text, no preset file needed. Returns how many channels were accepted.
			std::int32_t P_ApplyWeightsText(RE::StaticFunctionTag*, RE::Actor* a, RE::BSFixedString owner, RE::BSFixedString text, float fade, std::int32_t priority)
			{
				if (!a) {
					return 0;
				}
				auto               w = ParseText(text.c_str());
				const std::int32_t n = static_cast<std::int32_t>(w.size());
				ApplyWeights(a->GetFormID(), owner.c_str(), std::move(w), fade, priority);
				return n;
			}

			// Developer / test helper for the gaze layer: eye channels as text, added on top of the face. Returns how many channels were accepted.
			std::int32_t P_SetGazeText(RE::StaticFunctionTag*, RE::Actor* a, RE::BSFixedString owner, RE::BSFixedString text, float fade)
			{
				if (!a || Morph::SupportState(a) == 0) {
					return 0;
				}
				Weights w;
				for (auto& [name, v] : ParseText(text.c_str())) {
					if (IsEyeChannel(name)) {
						w.push_back({ name, v });
					}
				}
				const auto      n = static_cast<std::int32_t>(w.size());
				std::lock_guard lk(g_gzMx);
				g_gaze[a->GetFormID()][owner.c_str()] = std::move(w);
				PushGaze(a->GetFormID(), fade);
				return n;
			}

			void P_ClearGaze(RE::StaticFunctionTag*, RE::Actor* a, RE::BSFixedString owner, float fade)
			{
				if (!a) {
					return;
				}
				std::lock_guard lk(g_gzMx);
				if (const auto it = g_gaze.find(a->GetFormID()); it != g_gaze.end()) {
					it->second.erase(owner.c_str());
					if (it->second.empty()) {
						g_gaze.erase(it);
					}
				}
				PushGaze(a->GetFormID(), fade);
			}

		bool P_HasPreset(RE::StaticFunctionTag*, RE::BSFixedString preset)
		{
			return Presets::Resolve(preset.c_str()).has_value();
		}

		bool P_IsSupported(RE::StaticFunctionTag*, RE::Actor* a)
		{
			return a && Morph::Supported(a);
		}

		// ---- C interface ----
		const char* Safe(const char* s)
		{
			return s ? s : "";
		}

		int C_ApplyPreset(std::uint32_t id, const char* owner, const char* preset, float fade, int priority)
		{
			return ApplyPreset(id, Safe(owner), Safe(preset), fade, priority) ? 1 : 0;
		}

		int C_ApplyWeights(std::uint32_t id, const char* owner, const FaceLabAPI::Weight* w, std::uint32_t n, float fade, int priority)
		{
			Weights out;
			for (std::uint32_t i = 0; w && i < n; ++i) {
				if (w[i].channel) {
					out.push_back({ w[i].channel, std::clamp(w[i].value, 0.0f, 3.0f) });
				}
			}
			ApplyWeights(id, Safe(owner), std::move(out), fade, priority);
			return 1;
		}

		int C_Clear(std::uint32_t id, const char* owner, float fade)
		{
			Clear(id, Safe(owner), fade);
			return 1;
		}

		int C_HasPreset(const char* preset)
		{
			return Presets::Resolve(Safe(preset)).has_value() ? 1 : 0;
		}

		int C_IsSupported(std::uint32_t id)
		{
			auto* a = RE::TESForm::LookupByID<RE::Actor>(id);
			return a && a->Get3D() ? Morph::SupportState(a) : -1;
		}

		std::uint32_t C_ChannelCount()
		{
			return static_cast<std::uint32_t>(Channels().size());
		}

		const char* C_ChannelName(std::uint32_t i)
		{
			return i < Channels().size() ? Channels()[i].name : nullptr;
		}

		int C_SetMouthYield(std::uint32_t id, const char* owner, float amount, float fade)
		{
			SetMouthYield(id, Safe(owner), amount, fade);
			return 1;
		}

		void P_SetMouthYield(RE::StaticFunctionTag*, RE::Actor* a, RE::BSFixedString owner, float amount, float fade)
		{
			if (a) {
				SetMouthYield(a->GetFormID(), owner.c_str(), amount, fade);
			}
		}

		float C_GetMouthOpen(std::uint32_t id)
		{
			return Morph::MouthOpen(id);
		}

		float C_GetEyeControl(std::uint32_t id)
		{
			return Morph::EyeControl(id);
		}

		float C_GetTongue(std::uint32_t id)
		{
			return Morph::Tongue(id);
		}

		int C_SetGaze(std::uint32_t id, const char* owner, const FaceLabAPI::Weight* w, std::uint32_t n, float fade)
			{
				Weights out;
				for (std::uint32_t i = 0; w && i < n; ++i) {
					if (w[i].channel && IsEyeChannel(w[i].channel)) {
						out.push_back({ w[i].channel, std::clamp(w[i].value, 0.0f, 3.0f) });
					}
				}
				std::lock_guard lk(g_gzMx);
				g_gaze[id][Safe(owner)] = std::move(out);
				PushGaze(id, fade);
				return 1;
			}

			int C_ClearGaze(std::uint32_t id, const char* owner, float fade)
			{
				std::lock_guard lk(g_gzMx);
				if (const auto it = g_gaze.find(id); it != g_gaze.end()) {
					it->second.erase(Safe(owner));
					if (it->second.empty()) {
						g_gaze.erase(it);
					}
				}
				PushGaze(id, fade);
				return 1;
			}

			const FaceLabAPI::Interface g_iface{ 6, C_ApplyPreset, C_ApplyWeights, C_Clear, C_HasPreset, C_IsSupported, C_ChannelCount, C_ChannelName, C_SetMouthYield, C_GetMouthOpen, C_GetEyeControl, C_GetTongue, C_SetGaze, C_ClearGaze };
	}

	void SetMouthYield(RE::FormID id, std::string owner, float amount, float fade)
	{
		SKSE::GetTaskInterface()->AddTask([id, owner = std::move(owner), amount = std::clamp(amount, 0.0f, 1.0f), fade]() {
			{
				std::lock_guard lk(g_mx);
				if (amount <= 0.0f) {
					if (const auto it = g_yield.find(id); it != g_yield.end()) {
						it->second.erase(owner);
						if (it->second.empty()) {
							g_yield.erase(it);
						}
					}
				} else {
					g_yield[id][owner] = amount;
				}
			}
			if (auto* a = RE::TESForm::LookupByID<RE::Actor>(id)) {
				Show(a, fade);
			}
		});
	}

	void ApplyWeights(RE::FormID id, std::string owner, Weights w, float fade, int priority)
	{
		SKSE::GetTaskInterface()->AddTask([id, owner = std::move(owner), w = std::move(w), fade, priority]() mutable { Put(id, owner, std::move(w), fade, priority); });
	}

	void SetAssist(RE::FormID id, std::string owner, Weights w, float fade)
	{
		std::lock_guard lk(g_gzMx);
		g_gaze[id][std::move(owner)] = std::move(w);
		PushGaze(id, fade);
	}

	void ClearAssist(RE::FormID id, std::string owner, float fade)
	{
		std::lock_guard lk(g_gzMx);
		if (const auto it = g_gaze.find(id); it != g_gaze.end()) {
			it->second.erase(owner);
			if (it->second.empty()) {
				g_gaze.erase(it);
			}
		}
		PushGaze(id, fade);
	}

	bool ApplyPreset(RE::FormID id, std::string owner, std::string_view preset, float fade, int priority)
	{
		const auto p = Presets::Resolve(preset);
		if (!p) {
			logger::info("ApplyPreset: no preset '{}' (asked by {})", std::string(preset), owner);
			return false;
		}
		Weights w;
		for (const auto& [ch, v] : p->weights) {
			w.push_back({ ch, static_cast<float>(v) / 100.0f });
		}
		ApplyWeights(id, std::move(owner), std::move(w), fade, priority);
		return true;
	}

	void Clear(RE::FormID id, std::string owner, float fade)
	{
		SKSE::GetTaskInterface()->AddTask([id, owner = std::move(owner), fade]() {
			{
				std::lock_guard lk(g_mx);
				const auto      it = g_layers.find(id);
				if (it == g_layers.end()) {
					return;
				}
				std::erase_if(it->second, [&](const Layer& l) { return l.owner == owner; });
				if (it->second.empty()) {
					g_layers.erase(it);
				}
			}
			if (auto* a = RE::TESForm::LookupByID<RE::Actor>(id)) {
				Show(a, fade);
			}
		});
	}

	void ClearAll()
	{
		{
			std::lock_guard lk(g_gzMx);
			g_gaze.clear();
		}
		{
			std::lock_guard lk(g_mx);
			g_layers.clear();
			g_yield.clear();
			g_lastWinner.clear();
		}
		Morph::ClearAll();
	}

	bool RegisterPapyrus(RE::BSScript::IVirtualMachine* vm)
	{
		vm->RegisterFunction("ApplyFace", "FaceLab", P_ApplyFace);
		vm->RegisterFunction("ClearFace", "FaceLab", P_ClearFace);
		vm->RegisterFunction("ApplyWeightsText", "FaceLab", P_ApplyWeightsText);
		vm->RegisterFunction("SetGazeText", "FaceLab", P_SetGazeText);
		vm->RegisterFunction("ClearGaze", "FaceLab", P_ClearGaze);
		vm->RegisterFunction("HasPreset", "FaceLab", P_HasPreset);
		vm->RegisterFunction("IsSupported", "FaceLab", P_IsSupported);
		vm->RegisterFunction("SetMouthYield", "FaceLab", P_SetMouthYield);
		return true;
	}
}

extern "C" __declspec(dllexport) const FaceLabAPI::Interface* FaceLab_GetInterfaceV1()
{
	return &FaceLab::Api::g_iface;
}
