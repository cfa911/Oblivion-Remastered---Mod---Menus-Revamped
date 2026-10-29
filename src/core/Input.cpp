#include "core/Runtime.h"


#include "engine/Menus.h"
#include "sorting/Picker.h"
#include "sorting/rules/ControllerPolicy.h"
#include "keyboard/KeyboardInput.h"
#include "keyboard/Keyboard.h"

#include <Xinput.h>   // types and constants only - nothing is linked or loaded

namespace sortingdriver
{
	namespace
	{
		using XInputGetState_t = DWORD(WINAPI*)(DWORD, XINPUT_STATE*);

		XInputGetState_t  g_previous = nullptr;
		DWORD             g_gameThread = 0;
		std::atomic<bool> g_installed{ false };
		sortpad::PacketStamp g_packets;

		DWORD WINAPI Chained(DWORD a_user, XINPUT_STATE* a_state)
		{
			const DWORD rc = g_previous ? g_previous(a_user, a_state) : ERROR_DEVICE_NOT_CONNECTED;
			if (a_user == 0 && GetCurrentThreadId() == g_gameThread) {
				sorting::Tick();   // on every read, a connected pad or not
				if (rc == ERROR_SUCCESS && a_state) {
					auto& pad = a_state->Gamepad;
					const bool keyboardHadInput = osk::Taking();
					keyboardinput::Process(*a_state, !gearfilter::PopupOpen(), true);
					if (!keyboardHadInput && !osk::Taking())
						gearfilter::Controller(pad.wButtons, pad.sThumbLX, pad.sThumbLY, true);
					a_state->dwPacketNumber = g_packets.Update(a_state->dwPacketNumber,
						{ pad.wButtons, pad.sThumbLX, pad.sThumbLY, pad.sThumbRX, pad.sThumbRY, pad.bLeftTrigger, pad.bRightTrigger });
					sorting::PublishInputState();
				} else {
					g_packets.Reset();
					XINPUT_STATE empty{};
					keyboardinput::Process(empty, false, false);
					sorting::PublishInputState();
					std::uint16_t buttons = 0; short x = 0, y = 0;
					gearfilter::Controller(buttons, x, y, false);
				}
			}
			return rc;
		}
	}

	bool Install()
	{
		if (g_installed.load()) return true;
		g_gameThread = GetCurrentThreadId();
		auto*       base = reinterpret_cast<std::uint8_t*>(GetModuleHandleW(nullptr));
		const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
		const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS64*>(base + dos->e_lfanew);
		const auto& dir = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
		if (!dir.VirtualAddress) return false;
		for (auto* desc = reinterpret_cast<const IMAGE_IMPORT_DESCRIPTOR*>(base + dir.VirtualAddress); desc->Name; ++desc) {
			const char* dll = reinterpret_cast<const char*>(base + desc->Name);
			if (_strnicmp(dll, "xinput", 6) != 0) continue;
			auto* names = reinterpret_cast<const IMAGE_THUNK_DATA64*>(base + (desc->OriginalFirstThunk ? desc->OriginalFirstThunk : desc->FirstThunk));
			auto* slots = reinterpret_cast<IMAGE_THUNK_DATA64*>(base + desc->FirstThunk);
			for (; names->u1.AddressOfData; ++names, ++slots) {
				const bool match = IMAGE_SNAP_BY_ORDINAL64(names->u1.Ordinal)
				                       ? IMAGE_ORDINAL64(names->u1.Ordinal) == 2
				                       : std::strcmp(reinterpret_cast<const IMAGE_IMPORT_BY_NAME*>(base + names->u1.AddressOfData)->Name, "XInputGetState") == 0;
				if (!match) continue;
				auto* slot = reinterpret_cast<XInputGetState_t*>(&slots->u1.Function);
				DWORD old = 0;
				if (!VirtualProtect(slot, sizeof(void*), PAGE_READWRITE, &old)) return false;
				g_previous = *slot;
				*slot = &Chained;
				VirtualProtect(slot, sizeof(void*), old, &old);
				g_installed.store(true);
				logger::info("driver: the game's {} XInputGetState import now ticks Sorting Overhaul every read (owns input only for the custom carousel header/popup)", dll);
				return true;
			}
		}
		logger::warn("driver: the game imports no XInputGetState - Sorting Overhaul is ticked by the window timer only");
		return false;
	}
}
