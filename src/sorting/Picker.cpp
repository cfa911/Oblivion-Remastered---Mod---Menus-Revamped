#include "sorting/PickerInternal.h"

#include "core/Runtime.h"
#include "engine/ContainerMenu.h"
#include "engine/PEHook.h"
#include "engine/Reflect.h"
#include "engine/TextUtil.h"
#include "engine/Ui.h"
#include "engine/UiText.h"
#include "sorting/GearData.h"
#include "sorting/Icons.h"

namespace sorting
{
	namespace detail
	{
		Picker g_pickers[4];  // Inventory/Magic school, Magic effect, player side, other side

		// RMI-004: the native container Transfer action and the end of its sequence, before their native bodies.
		void OnTransferStarted() { g_pickers[3].DetachForTransfer(); }
		void OnTransferSettled() { g_pickers[3].transfer.Settle(GetTickCount64()); }
	}

	using namespace detail;

	bool Active(int side) { return side >= 3 && side <= 4 ? g_pickers[side - 1].active.load() :
		g_pickers[0].active.load() || g_pickers[1].active.load(); }

	void Tick(Mode a_mode, UE::UObject* a_menu, UE::UObject* a_canvas, float a_viewportScale, UE::UObject* a_other)
	{
		static const bool s_transferHooked = (container::SetTransferHandlers(&OnTransferStarted, &OnTransferSettled), true);
		(void)s_transferHooked;
		if (a_mode != Mode::Container) {
			g_pickers[2].Tick(-1, nullptr, nullptr, 1);
			g_pickers[3].Tick(-1, nullptr, nullptr, 1);
		}
		if (a_mode == Mode::Magic) {
			g_pickers[1].Tick(2, a_menu, a_canvas, a_viewportScale);  // attached first so the school icon ends up before it
			g_pickers[0].Tick(1, a_menu, a_canvas, a_viewportScale);
		} else if (a_mode == Mode::Inventory) {
			g_pickers[1].Tick(-1, nullptr, nullptr, 1.0f);
			g_pickers[0].Tick(0, a_menu, a_canvas, a_viewportScale);
		} else if (a_mode == Mode::Container) {
			g_pickers[1].Tick(-1, nullptr, nullptr, 1);
			g_pickers[0].Tick(-1, nullptr, nullptr, 1);
			g_pickers[2].Tick(3, a_menu, a_canvas, a_viewportScale);
			g_pickers[3].Tick(4, a_other, a_canvas, a_viewportScale);
		} else {
			g_pickers[1].Tick(-1, nullptr, nullptr, 1.0f);
			g_pickers[0].Tick(-1, nullptr, nullptr, 1.0f);
		}
		UpdateNativeBackgroundMask();
	}
}
