#pragma once

#include <cstdint>
#include <string_view>

// RMI-004: the container's quick-loot -> Transfer switch. Our container-side sort header stays attached through
// the switch and the native A-Z (Name) button is not placed. Our header is taken out when the native Transfer
// action fires and put back only after the menu's Transfer sequence has settled.
// Event names captured in game 1.512.105.0: the action runs for any input bound to Transfer, not one key.
namespace transferhold
{
	inline bool IsTransferAction(std::string_view a_function)
	{
		return a_function.starts_with("InpActEvt_IA_UI_Specific_ContainerMenu_Search_");
	}
	// The menu's last Transfer step; "..._Container_0" fires about 400 ms earlier, before the give side is shown.
	inline bool IsSettled(std::string_view a_function) { return a_function == "SequenceEvent__ENTRYPOINTWBP_ModernMenu_Container"; }

	// Only the quick-loot -> Transfer switch moves our header: once the native A-Z has been placed, held or repeated F,
	// and R3 closing the controller's details view, fire the same action and must leave the header alone.
	// (The give side is hidden while the controller's details view is open, so its visibility is not the test.)
	inline bool ShouldDetach(bool a_nativeNamePlaced) { return !a_nativeNamePlaced; }

	struct Hold
	{
		static constexpr std::uint64_t kAfterSettleMs = 1000;  // the native Name may never be placed (other mods, other views)
		static constexpr std::uint64_t kTimeoutMs = 3000;      // the settle event may not come

		void Start(std::uint64_t a_now)
		{
			if (held) return;  // a repeated action keeps the first start, so the limits still hold
			held = true; startedAt = a_now; settledAt = 0;
		}
		void Settle(std::uint64_t a_now) { if (held && !settledAt) settledAt = a_now; }
		bool Held() const { return held; }
		bool MayReinsert(std::uint64_t a_now, bool a_nameShown)
		{
			if (!held) return true;
			const bool settled = settledAt && (a_nameShown || a_now - settledAt >= kAfterSettleMs);
			if (!settled && a_now - startedAt < kTimeoutMs) return false;
			held = false;
			return true;
		}

	private:
		bool held = false;
		std::uint64_t startedAt = 0, settledAt = 0;
	};
}
