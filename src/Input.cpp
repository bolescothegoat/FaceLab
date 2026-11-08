// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Bolescothegoat - FaceLab. See LICENSE.

#include "Input.h"

#include "Lab.h"

#include <Windows.h>

namespace FaceLab::Input
{
	namespace
	{
		std::atomic<Capture>    g_capture{ Capture::None };
		std::unordered_set<int> g_heldKb;
		std::unordered_set<int> g_heldPad;

		constexpr int kEscape = 1;  // DirectInput scan code: cancels a keyboard capture

		bool MenuBlocksInput()
		{
			const auto ui = RE::UI::GetSingleton();
			return !ui || ui->GameIsPaused() || ui->IsMenuOpen(RE::Console::MENU_NAME) || ui->IsMenuOpen(RE::DialogueMenu::MENU_NAME);
		}

		void Bind(Capture which, bool pad, int code)
		{
			auto& s = GetSettings();
			switch (which) {
			case Capture::KbKey: s.kbKey = code; break;
			case Capture::KbMod: s.kbMod = code; break;
			case Capture::PadKey: s.padKey = code; break;
			case Capture::PadMod: s.padMod = code; break;
			case Capture::TongueKbKey: s.tKbKey = code; break;
			case Capture::TongueKbMod: s.tKbMod = code; break;
			case Capture::TonguePadKey: s.tPadKey = code; break;
			case Capture::TonguePadMod: s.tPadMod = code; break;
			default: return;
			}
			SaveConfig();
			logger::info("binding set: {} = {}", static_cast<int>(which), pad ? PadName(code) : KeyName(code));
		}

		class Sink final : public RE::BSTEventSink<RE::InputEvent*>
		{
		public:
			RE::BSEventNotifyControl ProcessEvent(RE::InputEvent* const* a_event, RE::BSTEventSource<RE::InputEvent*>*) override
			{
				if (!a_event) {
					return RE::BSEventNotifyControl::kContinue;
				}
				const auto& s = GetSettings();
				for (auto* e = *a_event; e; e = e->next) {
					if (e->GetEventType() != RE::INPUT_EVENT_TYPE::kButton) {
						continue;
					}
					const auto* b = e->AsButtonEvent();
					if (!b) {
						continue;
					}
					const bool pad = b->GetDevice() == RE::INPUT_DEVICE::kGamepad;
					const bool kb = b->GetDevice() == RE::INPUT_DEVICE::kKeyboard;
					if (!pad && !kb) {
						continue;
					}
					const int code = static_cast<int>(b->GetIDCode());
					auto&     held = pad ? g_heldPad : g_heldKb;

					const Capture cap = g_capture.load();
					if (cap != Capture::None && b->IsDown()) {
						const bool wantPad = cap == Capture::PadKey || cap == Capture::PadMod || cap == Capture::TonguePadKey || cap == Capture::TonguePadMod;
						if (wantPad == pad) {
							g_capture = Capture::None;
							if (!(kb && code == kEscape)) {
								Bind(cap, pad, code);
							}
							continue;
						}
					}

					if (b->IsDown()) {
						held.insert(code);
					} else if (b->IsUp()) {
						held.erase(code);
						continue;
					}
					if (!b->IsDown() || cap != Capture::None || !s.enabled || MenuBlocksInput()) {
						continue;
					}
					const auto fires = [&](int key, int mod) {
						const bool haveMod = pad ? mod > 0 : mod >= 0;
						return key > 0 && code == key && (!haveMod || held.contains(mod));
					};
					if (fires(pad ? s.padKey : s.kbKey, pad ? s.padMod : s.kbMod)) {
						OnHotkey();
					} else if (fires(pad ? s.tPadKey : s.tKbKey, pad ? s.tPadMod : s.tKbMod)) {
						ToggleTongue();
						RE::SendHUDMessage::ShowHUDMessage("FaceLab: tongue toggled");
					}
				}
				return RE::BSEventNotifyControl::kContinue;
			}
		};
		Sink g_sink;
	}

	void Install()
	{
		if (const auto mgr = RE::BSInputDeviceManager::GetSingleton()) {
			mgr->AddEventSink(&g_sink);
			logger::info("input sink installed");
		}
	}

	void BeginCapture(Capture which)
	{
		g_capture = which;
	}

	Capture CurrentCapture()
	{
		return g_capture.load();
	}

	namespace
	{
		struct XState
		{
			DWORD dwPacketNumber;
			WORD  wButtons;
			BYTE  bLeftTrigger, bRightTrigger;
			SHORT sThumbLX, sThumbLY, sThumbRX, sThumbRY;
		};
		using XGetState = DWORD(WINAPI*)(DWORD, XState*);

		XGetState GetXInput()
		{
			static XGetState fn = []() -> XGetState {
				for (const char* dll : { "xinput1_4.dll", "xinput1_3.dll", "xinput9_1_0.dll" }) {
					if (const auto m = ::LoadLibraryA(dll)) {
						if (const auto p = ::GetProcAddress(m, "XInputGetState")) {
							return reinterpret_cast<XGetState>(p);
						}
					}
				}
				return nullptr;
			}();
			return fn;
		}

		bool          g_pollArmed = false;
		bool          g_prevKey[256]{};
		WORD          g_prevPad = 0;

		bool GameHasFocus()
		{
			DWORD pid = 0;
			::GetWindowThreadProcessId(::GetForegroundWindow(), &pid);
			return pid == ::GetCurrentProcessId();
		}

		WORD PadNow()
		{
			XState st{};
			if (const auto fn = GetXInput()) {
				for (DWORD i = 0; i < 4; ++i) {
					if (fn(i, &st) == 0) {
						return st.wButtons;
					}
				}
			}
			return 0;
		}
	}

	void PollCapture()
	{
		const Capture cap = g_capture.load();
		if (cap == Capture::None) {
			g_pollArmed = false;
			return;
		}
		if (!GameHasFocus()) {
			return;
		}
		const bool wantPad = cap == Capture::PadKey || cap == Capture::PadMod || cap == Capture::TonguePadKey || cap == Capture::TonguePadMod;
		if (wantPad) {
			const WORD now = PadNow();
			if (!g_pollArmed) {
				g_pollArmed = true;
				g_prevPad = now;  // what is held when the capture starts does not count
				return;
			}
			const WORD fresh = static_cast<WORD>(now & ~g_prevPad);
			g_prevPad = now;
			if (fresh) {
				WORD bit = 1;
				while (!(fresh & bit)) {
					bit = static_cast<WORD>(bit << 1);
				}
				g_capture = Capture::None;
				Bind(cap, true, bit);
				g_pollArmed = false;
			}
			return;
		}
		if (!g_pollArmed) {
			g_pollArmed = true;
			for (int vk = 8; vk < 255; ++vk) {
				g_prevKey[vk] = (::GetAsyncKeyState(vk) & 0x8000) != 0;
			}
			return;
		}
		for (int vk = 8; vk < 255; ++vk) {
			if (vk == VK_SHIFT || vk == VK_CONTROL || vk == VK_MENU || vk == VK_LBUTTON || vk == VK_RBUTTON || vk == VK_MBUTTON || vk == VK_XBUTTON1 || vk == VK_XBUTTON2) {
				continue;  // the left / right versions of the modifiers are looked at on their own; the mouse buttons are the menu's
			}
			const bool down = (::GetAsyncKeyState(vk) & 0x8000) != 0;
			const bool fresh = down && !g_prevKey[vk];
			g_prevKey[vk] = down;
			if (!fresh) {
				continue;
			}
			// virtual key -> DirectInput scan code (what the game's key events and our saved bindings use): extended keys carry 0x80
			const UINT sc = ::MapVirtualKeyA(static_cast<UINT>(vk), MAPVK_VK_TO_VSC_EX);
			int        dik = static_cast<int>(sc & 0xFF);
			if ((sc & 0xFF00) == 0xE000) {
				dik |= 0x80;
			}
			if (dik <= 0) {
				continue;
			}
			g_capture = Capture::None;
			if (dik != kEscape) {
				Bind(cap, false, dik);
			}
			g_pollArmed = false;
			return;
		}
	}

	std::string KeyName(int dik)
	{
		if (dik < 0) {
			return "none";
		}
		char       buf[64]{};
		const UINT vk = ::MapVirtualKeyA(static_cast<UINT>(dik), MAPVK_VSC_TO_VK);
		LONG       lparam = static_cast<LONG>(dik) << 16;
		if (dik >= 0x47 && dik <= 0x53 && dik != 0x4A && dik != 0x4E) {
			lparam |= 1 << 24;
		}
		if (vk != 0 && ::GetKeyNameTextA(lparam, buf, sizeof(buf)) > 0) {
			return buf;
		}
		return std::format("key {}", dik);
	}

	std::string PadName(int mask)
	{
		switch (mask) {
		case 0x0001: return "D-pad Up";
		case 0x0002: return "D-pad Down";
		case 0x0004: return "D-pad Left";
		case 0x0008: return "D-pad Right";
		case 0x0010: return "Start";
		case 0x0020: return "Back";
		case 0x0040: return "Left stick click";
		case 0x0080: return "Right stick click";
		case 0x0100: return "Left bumper";
		case 0x0200: return "Right bumper";
		case 0x1000: return "A";
		case 0x2000: return "B";
		case 0x4000: return "X";
		case 0x8000: return "Y";
		case 0: return "none";
		default: return std::format("button {:#x}", mask);
		}
	}
}
