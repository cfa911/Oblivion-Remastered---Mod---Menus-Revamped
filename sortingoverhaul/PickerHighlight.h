#pragma once
#include <array>

namespace pickerhighlight
{
	// Owner reference #E6E4DF in sRGB, converted to linear Slate colour. Opaque, no outline.
	inline constexpr std::array<float, 4> kFill{ 0.7912979403f, 0.7758222183f, 0.7379104088f, 1.0f };
	inline constexpr float kOutlineWidth = 0.0f, kCornerRadius = 0.0f;
	constexpr bool Owns(bool headerFocused, bool popupOpen, bool popupController)
	{
		return headerFocused || (popupOpen && popupController);
	}
	// Shared per-widget snapshot: a second custom owner must not capture the masked zero.
	class OpacitySnapshot
	{
	public:
		bool Capture(float value)
		{
			if (saved) return false;
			original = value; saved = true; return true;
		}
		bool HasValue() const { return saved; }
		float Original() const { return original; }
		void Reset() { saved = false; original = 1.0f; }
	private:
		bool saved = false;
		float original = 1.0f;
	};
	enum class Transition { None, Enter, Leave };
	class NativeHandoff
	{
	public:
		Transition Update(bool headerFocused, bool popupOpen, bool popupController)
		{
			const bool next = Owns(headerFocused, popupOpen, popupController);
			const auto change = next == owned ? Transition::None : (next ? Transition::Enter : Transition::Leave);
			owned = next;
			return change;
		}
		void Reset() { owned = false; }
	private:
		bool owned = false;
	};
}
