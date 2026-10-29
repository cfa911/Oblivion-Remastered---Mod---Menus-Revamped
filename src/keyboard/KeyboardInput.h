#pragma once
#include <Xinput.h>
namespace keyboardinput {
	// Internal stage of the single Menus Revamped input hook.
	void Process(XINPUT_STATE& state, bool allowOpen, bool connected);
}
