// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Bolescothegoat - FaceLab. See LICENSE.

#include "Scenes.h"

#include "Api.h"
#include "Lab.h"
#include "Presets.h"

#include <toml++/toml.hpp>

#include <Windows.h>

#include <algorithm>
#include <atomic>
#include <deque>
#include <filesystem>
#include <functional>
#include <map>
#include <mutex>
#include <thread>

namespace FaceLab::Scenes
{
	namespace
	{
		namespace fs = std::filesystem;
		using VarFn = std::function<void(const RE::BSScript::Variable&)>;

		constexpr const char* kSceneOwner = "FaceLab.Scene";           // the face a scene rule gives an actor while the scene runs
		constexpr const char* kOrgasmOwner = "FaceLab.Scene.Orgasm";   // the face of an orgasm rule: shown for a few seconds, above the scene face

		struct Rule
		{
			std::string              name;
			bool                     ostim{ true };
			bool                     sexlab{ true };
			bool                     orgasm{ false };    // false: while the scene runs (start / change), true: at an orgasm
			std::vector<std::string> tags;               // scene tags, lower case
			bool                     tagsAll{ false };   // all of them (false: any one)
			std::vector<std::string> actions;            // OStim action types an actor takes part in, lower case
			bool                     roleTarget{ false };  // actions: the actor is the target of the action (false: the one who does it)
			int                      sex{ -1 };          // -1 any, 0 male, 1 female
			std::string              preset;
			int                      priority{ 5 };
			float                    fade{ 0.5f };
			float                    hold{ 8.0f };       // orgasm rules: seconds the face stays
		};

		std::mutex               g_mx;
		std::vector<Rule>        g_rules;
		std::vector<std::string> g_problems;
		std::deque<std::string>  g_log;

		std::mutex                                      g_stMx;
		std::map<RE::FormID, std::string>               g_sceneFace;  // actor -> preset the scene layer shows now
		std::map<std::string, std::vector<RE::FormID>>  g_threads;    // "o:<id>" / "s:<id>" -> actors a rule gave a face
		std::map<RE::FormID, int>                       g_orgToken;   // actor -> number of the latest orgasm face (a timer only clears its own)

		std::string Lower(std::string s)
		{
			std::ranges::transform(s, s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
			return s;
		}

		template <class... A>
		void Say(std::format_string<A...> f, A&&... a)
		{
			const std::string line = std::format(f, std::forward<A>(a)...);
			logger::info("Scenes: {}", line);
			std::lock_guard lk(g_mx);
			g_log.push_back(line);
			while (g_log.size() > 14) {
				g_log.pop_front();
			}
		}

		// ---- reading the rule files ------------------------------------------------------------------------------------------
		std::vector<std::string> Strings(const toml::table& t, const char* key)
		{
			std::vector<std::string> out;
			if (const auto* a = t[key].as_array()) {
				for (const auto& n : *a) {
					if (const auto s = n.value<std::string>()) {
						out.push_back(Lower(*s));
					}
				}
			} else if (const auto s = t[key].value<std::string>()) {  // a single word is fine too
				out.push_back(Lower(*s));
			}
			return out;
		}

		void ReadFile(const fs::path& path, std::vector<Rule>& rules, std::vector<std::string>& problems)
		{
			const std::string fname = path.filename().string();
			toml::table       tbl;
			try {
				tbl = toml::parse_file(path.string());
			} catch (const std::exception& e) {
				problems.push_back(std::format("{}: cannot be read ({})", fname, e.what()));
				return;
			}
			const auto* arr = tbl["rule"].as_array();
			if (!arr) {
				problems.push_back(std::format("{}: no [[rule]] in it", fname));
				return;
			}
			int idx = 0;
			for (const auto& node : *arr) {
				++idx;
				const auto* t = node.as_table();
				if (!t) {
					continue;
				}
				Rule r;
				r.name = (*t)["name"].value_or(std::format("{} #{}", fname, idx));
				r.preset = (*t)["preset"].value_or(std::string{});
				if (r.preset.empty()) {
					problems.push_back(std::format("{}: rule '{}' has no preset", fname, r.name));
					continue;
				}
				const auto fw = Strings(*t, "frameworks");
				if (!fw.empty()) {
					r.ostim = std::ranges::find(fw, "ostim") != fw.end();
					r.sexlab = std::ranges::find(fw, "sexlab") != fw.end();
				}
				const std::string on = Lower((*t)["on"].value_or(std::string{ "scene" }));
				r.orgasm = on == "orgasm";
				if (on != "scene" && on != "orgasm") {
					problems.push_back(std::format("{}: rule '{}': on must be \"scene\" or \"orgasm\" (not '{}'), using scene", fname, r.name, on));
				}
				r.tags = Strings(*t, "tags");
				r.tagsAll = (*t)["tags_all"].value_or(false);
				r.actions = Strings(*t, "actions");
				r.roleTarget = Lower((*t)["role"].value_or(std::string{ "actor" })) == "target";
				const std::string sx = Lower((*t)["sex"].value_or(std::string{ "any" }));
				r.sex = sx == "female" ? 1 : (sx == "male" ? 0 : -1);
				r.priority = std::clamp((*t)["priority"].value_or(r.orgasm ? 9 : 5), 1, 999);
				r.fade = std::clamp(static_cast<float>((*t)["fade"].value_or(0.5)), 0.0f, 10.0f);
				r.hold = std::clamp(static_cast<float>((*t)["hold"].value_or(8.0)), 0.5f, 120.0f);
				if (r.tags.empty() && r.actions.empty()) {
					problems.push_back(std::format("{}: rule '{}' has no tags and no actions, so it would match every scene: skipped", fname, r.name));
					continue;
				}
				if (!Presets::Resolve(r.preset)) {
					problems.push_back(std::format("{}: rule '{}': no preset called '{}' (yet)", fname, r.name, r.preset));
				}
				rules.push_back(std::move(r));
			}
		}

		// ---- calling OStim / SexLab through Papyrus -------------------------------------------------------------------------
		class VarFunctor : public RE::BSScript::IStackCallbackFunctor
		{
		public:
			explicit VarFunctor(VarFn a_fn) :
				fn(std::move(a_fn)) {}
			void operator()(RE::BSScript::Variable a_result) override { fn(a_result); }
			void SetObject(const RE::BSTSmartPointer<RE::BSScript::Object>&) override {}

		private:
			VarFn fn;
		};

		bool CallStatic(const char* cls, const char* fn, RE::BSScript::IFunctionArguments* args, VarFn cb)
		{
			auto* vm = RE::BSScript::Internal::VirtualMachine::GetSingleton();
			if (!vm) {
				return false;
			}
			RE::BSTSmartPointer<RE::BSScript::IStackCallbackFunctor> f(new VarFunctor(std::move(cb)));
			return vm->DispatchStaticCall(cls, fn, args, f);
		}

		bool CallOnForm(RE::TESForm* form, const char* cls, const char* fn, RE::BSScript::IFunctionArguments* args, VarFn cb)
		{
			auto* vm = RE::BSScript::Internal::VirtualMachine::GetSingleton();
			if (!vm || !form) {
				return false;
			}
			auto* policy = vm->GetObjectHandlePolicy();
			if (!policy) {
				return false;
			}
			const auto handle = policy->GetHandleForObject(static_cast<RE::VMTypeID>(form->GetFormType()), form);
			if (handle == policy->EmptyHandle()) {
				return false;
			}
			RE::BSTSmartPointer<RE::BSScript::IStackCallbackFunctor> f(new VarFunctor(std::move(cb)));
			return vm->DispatchMethodCall(handle, RE::BSFixedString(cls), RE::BSFixedString(fn), args, f);
		}

		std::string AsString(const RE::BSScript::Variable& v)
		{
			return v.IsString() ? std::string(v.GetString()) : std::string{};
		}

		std::vector<std::string> AsStrings(const RE::BSScript::Variable& v)
		{
			std::vector<std::string> out;
			if (v.IsArray()) {
				if (const auto arr = v.GetArray()) {
					for (std::uint32_t i = 0; i < arr->size(); ++i) {
						const auto& e = (*arr)[i];
						if (e.IsString()) {
							out.push_back(Lower(std::string(e.GetString())));
						}
					}
				}
			}
			return out;
		}

		std::vector<RE::ActorHandle> AsActors(const RE::BSScript::Variable& v)
		{
			std::vector<RE::ActorHandle> out;
			if (v.IsArray()) {
				if (const auto arr = v.GetArray()) {
					for (std::uint32_t i = 0; i < arr->size(); ++i) {
						auto* a = (*arr)[i].Unpack<RE::Actor*>();
						out.push_back(a ? a->GetHandle() : RE::ActorHandle{});
					}
				}
			}
			return out;
		}

		// a counter that runs `then` when the last of a set of asynchronous calls has answered
		struct Countdown
		{
			std::atomic<int>      n;
			std::function<void()> then;
			Countdown(int count, std::function<void()> f) :
				n(count), then(std::move(f)) {}
			void Done()
			{
				if (--n == 0) {
					then();
				}
			}
		};

		// ---- what a scene is, once asked ------------------------------------------------------------------------------------
		struct Scene
		{
			std::string                  fw;        // "ostim" / "sexlab"
			std::string                  key;       // "o:<thread>" / "s:<thread>"
			std::string                  id;        // OStim scene id (empty for SexLab)
			int                          thread{ 0 };
			std::vector<std::string>     tags;
			std::vector<RE::ActorHandle> actors;
		};

		int SexOf(RE::Actor* a)
		{
			auto* base = a ? a->GetActorBase() : nullptr;
			return base ? (base->GetSex() == RE::SEX::kFemale ? 1 : 0) : -1;
		}

		bool TagsOk(const Rule& r, const std::vector<std::string>& tags)
		{
			if (r.tags.empty()) {
				return true;  // the rule is about actions only
			}
			if (std::ranges::find(r.tags, std::string{ "*" }) != r.tags.end()) {
				return true;  // tags = ["*"]: any scene
			}
			const auto has = [&](const std::string& t) { return std::ranges::find(tags, t) != tags.end(); };
			return r.tagsAll ? std::ranges::all_of(r.tags, has) : std::ranges::any_of(r.tags, has);
		}

		bool FwOk(const Rule& r, const Scene& s)
		{
			return s.fw == "ostim" ? r.ostim : r.sexlab;
		}

		std::string Join(const std::vector<std::string>& v)
		{
			std::string o;
			for (const auto& s : v) {
				o += (o.empty() ? "" : ",") + s;
			}
			return o;
		}

		std::vector<Rule> Rules()
		{
			std::lock_guard lk(g_mx);
			return g_rules;
		}

		void ClearActor(RE::FormID id, float fade)
		{
			Api::Clear(id, kSceneOwner, fade);
			Api::Clear(id, kOrgasmOwner, fade);
			std::lock_guard lk(g_stMx);
			g_sceneFace.erase(id);
		}

		// the scene layer: every actor gets the first rule that fits it (rule files are read in name order, rules top to bottom)
		void DecideScene(const std::shared_ptr<Scene>& sc, const std::vector<std::vector<bool>>& roleOk, const std::vector<Rule>& rules)
		{
			std::vector<RE::FormID> shown;
			for (std::size_t i = 0; i < sc->actors.size(); ++i) {
				auto actor = sc->actors[i].get();
				if (!actor) {
					continue;
				}
				const RE::FormID id = actor->GetFormID();
				const int        sex = SexOf(actor.get());
				const Rule*      chosen = nullptr;
				for (std::size_t r = 0; r < rules.size() && !chosen; ++r) {
					const auto& rule = rules[r];
					if (rule.orgasm || !FwOk(rule, *sc) || !TagsOk(rule, sc->tags)) {
						continue;
					}
					if (rule.sex != -1 && rule.sex != sex) {
						continue;
					}
					if (!rule.actions.empty() && !(roleOk.size() > r && roleOk[r].size() > i && roleOk[r][i])) {
						continue;
					}
					chosen = &rule;
				}
				if (chosen) {
					bool same = false;
					{
						std::lock_guard lk(g_stMx);
						const auto      it = g_sceneFace.find(id);
						same = it != g_sceneFace.end() && it->second == chosen->preset;
						if (!same) {
							g_sceneFace[id] = chosen->preset;
						}
					}
					shown.push_back(id);
					if (!same) {
						Api::ApplyPreset(id, kSceneOwner, chosen->preset, chosen->fade, chosen->priority);
						Say("{} scene '{}' [{}]: rule '{}' -> {} on {}", sc->fw, sc->id.empty() ? std::to_string(sc->thread) : sc->id, Join(sc->tags), chosen->name, chosen->preset, actor->GetDisplayFullName());
					}
				} else {
					bool had = false;
					{
						std::lock_guard lk(g_stMx);
						had = g_sceneFace.contains(id);
					}
					if (had) {
						Api::Clear(id, kSceneOwner, 0.5f);
						std::lock_guard lk(g_stMx);
						g_sceneFace.erase(id);
						Say("{} scene changed: no rule fits {} any more, face released", sc->fw, actor->GetDisplayFullName());
					}
				}
			}
			std::lock_guard lk(g_stMx);
			g_threads[sc->key] = shown;
		}

		// OStim rules that name actions need one question per actor: does this actor take part in one of those actions?
		void EvaluateScene(const std::shared_ptr<Scene>& sc)
		{
			// say what the scene is, even when no rule fits it: that is how a rule author finds the tag / action names to use
			{
				static std::mutex                        mx;
				static std::map<std::string, std::string> last;
				const std::string desc = std::format("{} scene '{}' tags [{}], {} actor(s)", sc->fw, sc->id.empty() ? std::to_string(sc->thread) : sc->id, Join(sc->tags), sc->actors.size());
				bool               fresh = false;
				{
					std::lock_guard lk(mx);
					fresh = last[sc->key] != desc;
					last[sc->key] = desc;
				}
				if (fresh) {
					Say("{}", desc);
				}
			}
			const auto rules = Rules();
			auto       roleOk = std::make_shared<std::vector<std::vector<bool>>>(rules.size(), std::vector<bool>(sc->actors.size(), false));
			int        asks = 0;
			if (sc->fw == "ostim") {
				for (std::size_t r = 0; r < rules.size(); ++r) {
					const auto& rule = rules[r];
					if (!rule.orgasm && !rule.actions.empty() && FwOk(rule, *sc) && TagsOk(rule, sc->tags)) {
						asks += static_cast<int>(sc->actors.size());
					}
				}
			}
			auto finish = [sc, roleOk, rules]() { DecideScene(sc, *roleOk, rules); };
			if (asks == 0) {
				finish();
				return;
			}
			auto cd = std::make_shared<Countdown>(asks, finish);
			for (std::size_t r = 0; r < rules.size(); ++r) {
				const auto& rule = rules[r];
				if (rule.orgasm || rule.actions.empty() || !FwOk(rule, *sc) || !TagsOk(rule, sc->tags)) {
					continue;
				}
				for (std::size_t i = 0; i < sc->actors.size(); ++i) {
					const bool sent = CallStatic("OMetadata", rule.roleTarget ? "FindAnyActionForTargetCSV" : "FindAnyActionForActorCSV",
						RE::MakeFunctionArguments(RE::BSFixedString(sc->id.c_str()), static_cast<std::int32_t>(i), RE::BSFixedString(Join(rule.actions).c_str())),
						[roleOk, r, i, cd](const RE::BSScript::Variable& v) {
							(*roleOk)[r][i] = v.IsInt() && v.GetSInt() >= 0;
							cd->Done();
						});
					if (!sent) {
						cd->Done();
					}
				}
			}
		}

		// ---- OStim ---------------------------------------------------------------------------------------------------------
		void OStimScene(int thread)
		{
			auto sc = std::make_shared<Scene>();
			sc->fw = "ostim";
			sc->thread = thread;
			sc->key = std::format("o:{}", thread);
			const bool sent = CallStatic("OThread", "GetScene", RE::MakeFunctionArguments(static_cast<std::int32_t>(thread)), [sc](const RE::BSScript::Variable& v) {
				sc->id = AsString(v);
				if (sc->id.empty()) {
					return;  // the thread is still starting up: the scene-changed event follows
				}
				auto cd = std::make_shared<Countdown>(2, [sc]() { EvaluateScene(sc); });
				if (!CallStatic("OThread", "GetActors", RE::MakeFunctionArguments(static_cast<std::int32_t>(sc->thread)), [sc, cd](const RE::BSScript::Variable& a) {
						sc->actors = AsActors(a);
						cd->Done();
					})) {
					cd->Done();
				}
				if (!CallStatic("OMetadata", "GetSceneTags", RE::MakeFunctionArguments(RE::BSFixedString(sc->id.c_str())), [sc, cd](const RE::BSScript::Variable& t) {
						sc->tags = AsStrings(t);
						cd->Done();
					})) {
					cd->Done();
				}
			});
			if (!sent) {
				Say("OStim: the Papyrus VM refused the call (thread {})", thread);
			}
		}

		void EndThread(const std::string& key, float fade)
		{
			std::vector<RE::FormID> ids;
			{
				std::lock_guard lk(g_stMx);
				if (const auto it = g_threads.find(key); it != g_threads.end()) {
					ids = it->second;
					g_threads.erase(it);
				}
			}
			for (const auto id : ids) {
				ClearActor(id, fade);
			}
			if (!ids.empty()) {
				Say("scene {} ended: {} face(s) released", key, ids.size());
			}
		}

		// one orgasm face: shown for rule.hold seconds above the scene face
		void ShowOrgasm(const Rule& rule, RE::Actor* actor, const std::string& scene)
		{
			if (!actor) {
				return;
			}
			const RE::FormID id = actor->GetFormID();
			int              token = 0;
			{
				std::lock_guard lk(g_stMx);
				token = ++g_orgToken[id];
			}
			Api::ApplyPreset(id, kOrgasmOwner, rule.preset, rule.fade, rule.priority);
			Say("orgasm in '{}': rule '{}' -> {} on {} for {:.0f} s", scene, rule.name, rule.preset, actor->GetDisplayFullName(), rule.hold);
			const float hold = rule.hold, fade = rule.fade;
			std::thread([id, token, hold, fade]() {
				std::this_thread::sleep_for(std::chrono::milliseconds(static_cast<int>(hold * 1000.0f)));
				bool mine = false;
				{
					std::lock_guard lk(g_stMx);
					mine = g_orgToken[id] == token;
				}
				if (mine) {
					Api::Clear(id, kOrgasmOwner, fade);
				}
			}).detach();
		}

		void OStimOrgasm(RE::ActorHandle h, std::string scene, int thread)
		{
			if (scene.empty()) {
				return;
			}
			CallStatic("OMetadata", "GetSceneTags", RE::MakeFunctionArguments(RE::BSFixedString(scene.c_str())), [h, scene, thread](const RE::BSScript::Variable& t) {
				const auto tags = AsStrings(t);
				const auto rules = Rules();
				auto       actor = h.get();
				if (!actor) {
					return;
				}
				const int sex = SexOf(actor.get());
				for (const auto& rule : rules) {
					if (!rule.orgasm || !rule.ostim || !TagsOk(rule, tags) || (rule.sex != -1 && rule.sex != sex)) {
						continue;
					}
					ShowOrgasm(rule, actor.get(), scene);
					return;  // the first rule that fits
				}
			});
			(void)thread;
		}

		// ---- SexLab ---------------------------------------------------------------------------------------------------------
		RE::TESForm* SexLabQuest()
		{
			// SexLabFramework.GetAPI() does the same: Game.GetFormFromFile(0xD62, "SexLab.esm")
			auto* dh = RE::TESDataHandler::GetSingleton();
			return dh ? dh->LookupForm(0xD62, "SexLab.esm") : nullptr;
		}

		void SexLabScene(const std::string& tid, RE::TESForm* controller, bool orgasm)
		{
			auto* quest = SexLabQuest();
			if (!quest) {
				return;
			}
			auto sc = std::make_shared<Scene>();
			sc->fw = "sexlab";
			sc->thread = std::atoi(tid.c_str());
			sc->key = std::format("s:{}", sc->thread);
			auto cd = std::make_shared<Countdown>(2, [sc, orgasm]() {
				if (!orgasm) {
					EvaluateScene(sc);
					return;
				}
				const auto rules = Rules();
				for (const auto& wp : sc->actors) {
					auto actor = wp.get();
					if (!actor) {
						continue;
					}
					const int sex = SexOf(actor.get());
					for (const auto& rule : rules) {
						if (rule.orgasm && rule.sexlab && TagsOk(rule, sc->tags) && (rule.sex == -1 || rule.sex == sex)) {
							ShowOrgasm(rule, actor.get(), std::format("SexLab thread {}", sc->thread));
							break;
						}
					}
				}
			});
			if (!CallOnForm(quest, "SexLabFramework", "HookActors", RE::MakeFunctionArguments(RE::BSFixedString(tid.c_str())), [sc, cd](const RE::BSScript::Variable& v) {
					sc->actors = AsActors(v);
					cd->Done();
				})) {
				cd->Done();
			}
			// the tags of what is playing, asked of the thread controller that sent the event
			// The thread's own GetTags keeps the tags of the animation the thread STARTED with (SexLab P+), so it goes stale as soon as the
			// scene is switched. The scene that is playing now is asked of the registry; GetTags is only the fallback (older SexLab).
			auto legacy = [sc, cd, controller]() {
				if (!CallOnForm(controller, "sslThreadController", "GetTags", RE::MakeFunctionArguments(), [sc, cd](const RE::BSScript::Variable& v) {
						sc->tags = AsStrings(v);
						cd->Done();
					})) {
					cd->Done();
				}
			};
			if (!CallOnForm(controller, "sslThreadController", "GetActiveScene", RE::MakeFunctionArguments(), [sc, cd, legacy](const RE::BSScript::Variable& v) {
					const std::string sid = AsString(v);
					if (sid.empty() || !CallStatic("SexLabRegistry", "GetSceneTags", RE::MakeFunctionArguments(RE::BSFixedString(sid.c_str())), [sc, cd, legacy](const RE::BSScript::Variable& t) {
							sc->tags = AsStrings(t);
							if (sc->tags.empty()) {
								legacy();
							} else {
								cd->Done();
							}
						})) {
						legacy();
					}
				})) {
				legacy();
			}
		}

		void SexLabEnd(const std::string& tid)
		{
			EndThread(std::format("s:{}", std::atoi(tid.c_str())), 0.5f);
		}

		// ---- the events ---------------------------------------------------------------------------------------------------------
		class Sink final : public RE::BSTEventSink<SKSE::ModCallbackEvent>
		{
		public:
			RE::BSEventNotifyControl ProcessEvent(const SKSE::ModCallbackEvent* e, RE::BSTEventSource<SKSE::ModCallbackEvent>*) override
			{
				if (!e || !GetSettings().scenes) {
					return RE::BSEventNotifyControl::kContinue;
				}
				const std::string_view name = e->eventName.c_str();
				if (name.starts_with("ostim_thread_")) {
					const int thread = static_cast<int>(e->numArg);
					if (name == "ostim_thread_start" || name == "ostim_thread_scenechanged") {
						OStimScene(thread);
					} else if (name == "ostim_thread_end") {
						EndThread(std::format("o:{}", thread), 0.5f);
					}
				} else if (name == "ostim_actor_orgasm" && e->sender) {
					if (auto* a = e->sender->As<RE::Actor>()) {
						OStimOrgasm(a->GetHandle(), e->strArg.c_str(), static_cast<int>(e->numArg));
					}
				} else if (name == "AnimationStart" || name == "AnimationChange" || name == "StageStart") {
					SexLabScene(e->strArg.c_str(), e->sender, false);
				} else if (name == "OrgasmStart") {
					SexLabScene(e->strArg.c_str(), e->sender, true);
				} else if (name == "AnimationEnd") {
					SexLabEnd(e->strArg.c_str());
				}
				return RE::BSEventNotifyControl::kContinue;
			}
		};
		Sink g_sink;
	}

	void LoadRules()
	{
		std::vector<Rule>        rules;
		std::vector<std::string> problems;
		std::error_code          ec;
		const fs::path           dir = "Data/SKSE/Plugins/FaceLab/Scenes";
		std::vector<fs::path>    files;
		if (fs::exists(dir, ec)) {
			for (const auto& e : fs::directory_iterator(dir, ec)) {
				if (e.is_regular_file() && Lower(e.path().extension().string()) == ".toml") {
					files.push_back(e.path());
				}
			}
		}
		std::ranges::sort(files);
		for (const auto& f : files) {
			ReadFile(f, rules, problems);
		}
		logger::info("Scenes: {} rule(s) from {} file(s) in {}{}", rules.size(), files.size(), dir.string(), problems.empty() ? "" : std::format(" ({} problem(s))", problems.size()));
		for (const auto& p : problems) {
			logger::warn("Scenes: {}", p);
		}
		std::lock_guard lk(g_mx);
		g_rules = std::move(rules);
		g_problems = std::move(problems);
	}

	void Reset()
	{
	std::lock_guard lk(g_stMx);
	g_sceneFace.clear();
	g_threads.clear();
	g_orgToken.clear();
	}

	void Init()
	{
		LoadRules();
		if (auto* src = SKSE::GetModCallbackEventSource()) {
			src->AddEventSink(&g_sink);
		}
		logger::info("Scenes: listening for OStim ({}) and SexLab ({}) events", ::GetModuleHandleA("OStim.dll") ? "found" : "not found", SexLabQuest() ? "found" : "not found");
	}

	Info GetInfo()
	{
		Info i;
		i.ostim = ::GetModuleHandleA("OStim.dll") != nullptr;
		{
			std::lock_guard lk(g_mx);
			i.rules = static_cast<int>(g_rules.size());
			i.problems = g_problems;
			i.log.assign(g_log.begin(), g_log.end());
		}
		auto* dh = RE::TESDataHandler::GetSingleton();
		i.sexlab = dh && dh->LookupModByName("SexLab.esm") != nullptr;
		return i;
	}
}
