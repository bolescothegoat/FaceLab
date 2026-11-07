// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Bolescothegoat - FaceLab. See LICENSE.

#include "Api.h"
#include "Input.h"
#include "Lab.h"
#include "Morph.h"
#include "Presets.h"

namespace FaceLab::Menu
{
	void Register();
}

namespace
{
	void InitLogging()
	{
		auto path = logger::log_directory();
		if (!path) {
			return;
		}
		*path /= "FaceLab.log"sv;
		auto sink = std::make_shared<spdlog::sinks::basic_file_sink_mt>(path->string(), true);
		auto log = std::make_shared<spdlog::logger>("global log"s, std::move(sink));
		log->set_level(spdlog::level::info);
		log->flush_on(spdlog::level::info);
		spdlog::set_default_logger(std::move(log));
		spdlog::set_pattern("[%Y-%m-%d %T.%e] [%l] %v"s);
	}

	void OnMessage(SKSE::MessagingInterface::Message* msg)
	{
		if (msg->type == SKSE::MessagingInterface::kDataLoaded) {
			logger::info("msg: DataLoaded");
			FaceLab::LoadConfig();
			FaceLab::Morph::SetBump(FaceLab::GetSettings().bump);
			FaceLab::Presets::Load();
			FaceLab::Input::Install();
			FaceLab::Menu::Register();
			FaceLab::StartCrosshairWatch();
			FaceLab::Morph::StartFrameLoop();
		} else if (msg->type == SKSE::MessagingInterface::kNewGame || msg->type == SKSE::MessagingInterface::kPreLoadGame) {
			FaceLab::Api::ClearAll();  // faces are never saved: nothing carries over a load
		}
	}
}

extern "C" __declspec(dllexport) constinit SKSE::PluginVersionData SKSEPlugin_Version = []() {
	SKSE::PluginVersionData v{};
	v.PluginVersion({ 0, 2, 1, 0 });
	v.PluginName("FaceLab");
	v.AuthorName("Bolescothegoat");
	v.UsesAddressLibrary();
	v.UsesNoStructs();
	// no CompatibleVersions list: an Address Library plugin loads on every AE runtime (1.6.x, 1.7.x). The engine addresses FaceLab needs (the face node's vtable) come from
	// Address Library through CommonLibSSE-NG, the rest is the game's own class layout.
	return v;
}();

// Skyrim SE 1.5.97 uses the old SKSE entry point instead of SKSEPlugin_Version
extern "C" __declspec(dllexport) bool SKSEPlugin_Query(const SKSE::QueryInterface*, SKSE::PluginInfo* a_info)
{
	a_info->infoVersion = SKSE::PluginInfo::kVersion;
	a_info->name = "FaceLab";
	a_info->version = REL::Version(0, 2, 1, 0).pack();
	return true;
}

SKSEPluginLoad(const SKSE::LoadInterface* a_skse)
{
	InitLogging();
	SKSE::Init(a_skse);
	logger::info("FaceLab 0.2.1 loaded, runtime {}", REL::Module::get().version().string());
	SKSE::GetMessagingInterface()->RegisterListener(OnMessage);
	SKSE::GetPapyrusInterface()->Register(FaceLab::Api::RegisterPapyrus);
	return true;
}
