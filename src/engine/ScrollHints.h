#pragma once
namespace scrollhints {
	void Discover();   // lazy class hook, no periodic object-table scans
	void Tick();       // game thread; cached widget handles only between player-menu events
}
