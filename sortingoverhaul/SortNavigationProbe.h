#pragma once

#ifdef SORTING_NAV_PROBE
// TEMP-DIAG: native controller Y (or F10) arms bounded, read-only sort observation.
namespace sortnavprobe
{
	void ObserveButtons(std::uint16_t a_buttons);  // no controller-state mutation
	void Tick(int a_menu, UE::UObject* a_widget);
}
#endif
