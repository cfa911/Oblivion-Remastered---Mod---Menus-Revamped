#pragma once
namespace core {
	void Tick();
	bool ReturnToListAfterFilter();
	bool ControllerKeyboardOpen();
	void PublishInputState();
}
namespace input { bool Install(); }
