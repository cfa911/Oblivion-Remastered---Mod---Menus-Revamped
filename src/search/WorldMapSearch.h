#pragma once
namespace mapsearch
{
	void Tick();
	bool Message(HWND, UINT, WPARAM, LPARAM);
	bool Controller(std::uint16_t&, short&, short&, short&, short&, bool keyboardOpen);
}
