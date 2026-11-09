#pragma once
// FSlateBrush (UE 5.3) caches the Slate resource handle of its ResourceObject after the first paint; only
// SetResourceObject clears it. A resource written in place keeps drawing the old object until the handle is cleared
// (RMI-068: the Inventory scrollbar thumb stayed pale). The handle is unreflected: it follows ImageType, 8-byte aligned,
// a 16-byte shared pointer, then a flag byte.
namespace brushhandle
{
	constexpr int HandleOffset(int a_imageTypeOffset) { return a_imageTypeOffset < 0 ? -1 : (a_imageTypeOffset + 1 + 7) & ~7; }
	constexpr bool Fits(int a_handleOffset, int a_brushSize) { return a_handleOffset > 0 && a_handleOffset + 17 <= a_brushSize; }
	// a shared pointer is empty or holds both its object and its reference controller
	inline bool Plausible(const void* a_object, const void* a_controller) { return (a_object == nullptr) == (a_controller == nullptr); }
}
