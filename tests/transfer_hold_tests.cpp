#include "../src/sorting/rules/TransferHold.h"
#include <cstdio>
#include <cstdlib>

// RMI-004: event names captured in game 1.512.105.0 (keyboard F, 2026-10-27); timing of the reinsertion hold.
int main()
{
	unsigned checks = 0, failures = 0;
	const auto check = [&](bool ok, const char* name) {
		++checks;
		if (!ok) { ++failures; std::fprintf(stderr, "FAIL %s\n", name); }
	};
	check(transferhold::IsTransferAction("InpActEvt_IA_UI_Specific_ContainerMenu_Search_K2Node_EnhancedInputActionEvent_5"), "native Transfer action is recognised");
	check(transferhold::IsTransferAction("InpActEvt_IA_UI_Specific_ContainerMenu_Search_K2Node_EnhancedInputActionEvent_9"), "node number is not part of the match");
	check(!transferhold::IsTransferAction("InpActEvt_IA_UI_Specific_InventoryContainerMenu_ToggleSorting_K2Node_EnhancedInputActionEvent_17"), "sorting toggle is not Transfer");
	check(!transferhold::IsTransferAction("OnKeyDown"), "a key event is not Transfer");
	check(transferhold::IsSettled("SequenceEvent__ENTRYPOINTWBP_ModernMenu_Container"), "final Transfer sequence settles");
	check(!transferhold::IsSettled("SequenceEvent__ENTRYPOINTWBP_ModernMenu_Container_0"), "the earlier sequence step does not settle");

	transferhold::Hold idle;
	check(!idle.Held() && idle.MayReinsert(100, false), "no Transfer: reinsertion is never held");

	transferhold::Hold h;
	h.Start(1000);
	check(h.Held(), "Transfer holds our header out");
	check(!h.MayReinsert(1100, false), "held before the sequence settles");
	check(!h.MayReinsert(1200, true), "Name shown before settling still waits for the sequence");
	h.Settle(1450);
	check(!h.MayReinsert(1500, false), "settled but Name not yet placed: wait");
	check(h.MayReinsert(1550, true), "settled and Name placed: reinsert");
	check(!h.Held() && h.MayReinsert(1600, false), "hold released after reinsertion");

	transferhold::Hold late;
	late.Start(1000); late.Settle(1400);
	check(!late.MayReinsert(2300, false), "settled, Name never placed: wait under one second");
	check(late.MayReinsert(2400, false), "settled, Name never placed: reinsert after one second");

	transferhold::Hold lost;
	lost.Start(1000);
	check(!lost.MayReinsert(3900, false), "no settle event: still held before three seconds");
	check(lost.MayReinsert(4000, false), "no settle event: reinsert after three seconds");

	// Holding or tapping F fired the action ~40 times in 37 s (2026-10-27 log); repeats must not extend the hold.
	transferhold::Hold repeated;
	repeated.Start(1000);
	for (std::uint64_t t = 1100; t < 4000; t += 100) repeated.Start(t);
	check(repeated.MayReinsert(4000, false), "repeated actions do not restart the three-second limit");
	check(transferhold::ShouldDetach(false), "quick loot (native A-Z not placed): the action takes the header out");
	check(!transferhold::ShouldDetach(true), "native A-Z already placed (Transfer, details view, R3): the action is ignored");

	transferhold::Hold again;
	again.Start(1000); again.Settle(1200); again.Settle(5000);
	check(again.MayReinsert(1200, true), "a repeated settle does not move the first one");

	std::printf("%u checks, %u failures\n", checks, failures);
	return failures ? 1 : 0;
}
