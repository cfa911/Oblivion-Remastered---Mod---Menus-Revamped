#include "sorting/rules/FilterTogglePaint.h"
#include "sorting/PickerInternal.h"

#include "core/Config.h"
#include "core/Runtime.h"
#include "engine/ContainerMenu.h"
#include "engine/PEHook.h"
#include "engine/Reflect.h"
#include "engine/TextUtil.h"
#include "engine/Ui.h"
#include "engine/UiText.h"
#include "sorting/GearData.h"
#include "sorting/Icons.h"

namespace sorting::detail
{
	void Picker::OpenPopup()
	{
		returnToList.Cancel();
		for (auto& other : g_pickers) {
			if (&other == this) continue;
			other.returnToList.Cancel();
			if (!other.open && !other.paintedOpen) continue;
			other.open = other.paintedOpen = other.controllerPopup = false;
			{
				std::scoped_lock lock(other.inputLock);
				other.inputOpen = false;
				other.command = -1; other.commandFromController = false;
			}
			// A peer may already have ticked this frame: hide it now, rather than next tick.
			const std::uint8_t hidden = 1;
			ui::CallFirst(reflect::Get(other.popup), L"SetVisibility", &hidden, sizeof(hidden));
			other.PaintController();
		}
		open = true; nextHitMeasure = 0;
	}

	bool Picker::BuildPopup(UE::UObject* a_canvas)
	{
		static unsigned serial = 0;
		const std::wstring suffix = std::to_wstring(++serial);
		UE::UObject* slot = nullptr;
		auto* popupWidget = ui::AddToCanvas(a_canvas, L"/Script/UMG.CanvasPanel", (L"SlPiecePopup" + suffix).c_str(), &slot);
		if (!popupWidget || !slot) return false;
		ui::Visible(popupWidget, false);
		const auto count = itemtags::Info(view).count;
		const auto width = pickergrid::Width(count), height = pickergrid::Height(count);
		ui::Vec2(slot, L"SetSize", width, height);
		const int z = 4000;
		ui::CallFirst(slot, L"SetZOrder", &z, sizeof(z));
		UE::UObject* backgroundSlot = nullptr;
		auto* background = CanvasChild(popupWidget, L"/Script/UMG.Image", (L"PiecePaper" + suffix).c_str(), 0, 0, width, height, 0, &backgroundSlot);
		if (!background || !backgroundSlot) return false;
		paperSlot = reflect::Hold(backgroundSlot);
		const bool skinned = kind == 0 && uistyle::SkinFor(mrconfig::Get().stylePreset).recolor;
		const auto& style = skinned ? uistyle::kInventorySkinPicker : uistyle::Get(mrconfig::Get().stylePreset).picker;
		ui::RoundedBox(background, false, style.paper.data(), style.border.data(), 1.5f);
		const std::uint8_t visible = 0;
		ui::CallFirst(background, L"SetVisibility", &visible, sizeof(visible));
		auto* titleWidget = CanvasChild(popupWidget, L"/Script/UMG.TextBlock", (L"PieceTitle" + suffix).c_str(), 12, 6, width - 24, 24, 1);
		uix::SetText(titleWidget, itemtags::Info(view).title);
		BoundLabel(titleWidget);
		uix::Ink(titleWidget, style.titleInk.data());
		const std::uint8_t titleCenter = 1;
		ui::CallFirst(titleWidget, L"SetJustification", &titleCenter, sizeof(titleCenter));
		title = reflect::Hold(titleWidget);
		for (unsigned i = 0; i < itemtags::kMaxChoices; ++i) {
			const double x = pickergrid::CellX(i, count), y = pickergrid::CellY(i, count);
			const auto name = std::to_wstring(i) + L"_" + suffix;
			auto* cell = CanvasChild(popupWidget, L"/Script/UMG.Image", (L"PieceCell" + name).c_str(), x, y, pickergrid::CellWidth, pickergrid::CellHeight, 1);
			ui::RoundedBox(cell, false, kClear, kClear, 0);
			cells[i] = reflect::Hold(cell);
			auto* image = CanvasChild(popupWidget, L"/Script/UMG.Image", (L"PieceIcon" + name).c_str(), x + (pickergrid::CellWidth - 42) / 2.0, y + 1, 42, 40, 2);
			icons[i] = reflect::Hold(image);
			auto* label = CanvasChild(popupWidget, L"/Script/UMG.TextBlock", (L"PieceLabel" + name).c_str(), x, y + 41, pickergrid::CellWidth, 21, 2);
			labels[i] = reflect::Hold(label);
			uix::SetText(label, "");
			BoundLabel(label);
			uix::Ink(label, style.ink.data());
			const std::uint8_t center = 1;
			ui::CallFirst(label, L"SetJustification", &center, sizeof(center));
		}
		// A collapsed ListView owns strong UE references to the full source list while its visible list is filtered.
		// It generates no visible rows; handles alone would not keep removed list items alive through GC.
		popup = reflect::Hold(popupWidget);
		popupSlot = reflect::Hold(slot);
		canvas = reflect::Hold(a_canvas);
		paintedOpen = false;
		fontsReady = false; fontWarning = false; nextFontTry = 0;
		styled = false;
		ui::Visible(popupWidget, false);
		return true;
	}

	// Owner 2026-11-07: on the spell tabs the school (scroll) and type (star) pickers show the game's original sort icons
	// in their "All" state, loaded from the game at run time (no game art shipped); other choices keep their PNGs.
	void Picker::BrushHeader(const wchar_t* a_icon)
	{
		auto* image = reflect::Get(headerImage);
		if (!image || !a_icon) return;
		const bool all = std::wcscmp(a_icon, itemtags::Info(tab).icons[0]) == 0;
		const wchar_t* game = all ? AllTexture(kind) : nullptr;
		if (!game || !TextureBrush(image, game)) Brush(image, a_icon, kind == 0 && OwnSorts());
	}

	const wchar_t* Picker::HeaderIcon() const
	{
		if (tab != itemtags::Tab::AllItems) return itemtags::SelectionIcon(tab, choice);
		if (category == 0 || category > 4) return itemtags::Info(tab).icons[0];
		if (choice == 0) return itemtags::Info(tab).icons[category];  // the whole category
		return itemtags::SelectionIcon(static_cast<itemtags::Tab>(category - 1), choice);
	}

	// Which cell of the popup's current view is the active selection (99 = none).
	unsigned Picker::Highlight() const
	{
		return pickernav::Highlight(tab, view, category, choice);
	}

	// Labels, icons and visible cells follow the popup's view; the header icon follows the current selection.
	void Picker::StyleCells()
	{
		const auto& info = itemtags::Info(view);
		// Resize both the hit-test panel and its paper whenever a parent/submenu changes.
		const auto width = pickergrid::Width(info.count), height = pickergrid::Height(info.count);
		ui::Vec2(reflect::Get(popupSlot), L"SetSize", width, height);
		ui::Vec2(reflect::Get(paperSlot), L"SetSize", width, height);
		LayoutChild(reflect::Get(title), 12, 6, width - 24, 24);
		nextLayout = nextHitMeasure = 0;
		uix::SetText(reflect::Get(title), info.title);
		for (unsigned i = 0; i < itemtags::kMaxChoices; ++i) {
			const bool on = i < info.count;
			ui::Visible(reflect::Get(cells[i]), on);
			ui::Visible(reflect::Get(icons[i]), on);
			ui::Visible(reflect::Get(labels[i]), on);
			if (on) {
				const double x = pickergrid::CellX(i, info.count), y = pickergrid::CellY(i, info.count);
				LayoutChild(reflect::Get(cells[i]), x, y, pickergrid::CellWidth, pickergrid::CellHeight);
				LayoutChild(reflect::Get(icons[i]), x + (pickergrid::CellWidth - 42) / 2.0, y + 1, 42, 40);
				LayoutChild(reflect::Get(labels[i]), x, y + 41, pickergrid::CellWidth, 21);
				// The popup's "All" of the Magic school/type pickers: the game's icon too (owner, R14 test 2026-11-07).
				const wchar_t* game = i == 0 && view == tab ? AllTexture(kind) : nullptr;
				if (!game || !TextureBrush(reflect::Get(icons[i]), game)) Brush(reflect::Get(icons[i]), info.icons[i], kind == 0);
				const auto display = std::string(itemtags::PopupLabel(view, i));
				uix::SetText(reflect::Get(labels[i]), display);
				ui::Call tooltip(reflect::Get(labels[i]), L"SetToolTipText");
				if (auto* at = tooltip.At("InToolTipText"); at && tooltip.Size("InToolTipText") == sizeof(UE::FText)) {
					const auto wide = textutil::Wide(info.names[i]);
					auto* text = new (at) UE::FText(UE::FText::AsCultureInvariant(UE::FString(wide.c_str())));
					tooltip.Run(); text->~FText();
				}
			}
		}
		BrushHeader(HeaderIcon());
		PaintController();
	}

	// Other menus: on = the icon at full strength on the owner's red/blue background, off = dimmed. The Inventory dark skin
	// inverts it: on = the icon in the row marker's colour without a fill (filtertoggle::Choose). Controller focus uses the
	// shared picker fill in both.
	static void PaintToggle(UE::UObject* frame, UE::UObject* image, bool inventory, bool on, bool focus, const float* onFill, const float* mark)
	{
		const auto& picker = uistyle::Get(mrconfig::Get().stylePreset).picker;
		const auto paint = filtertoggle::Choose(inventory, on, focus, picker.focusFill.data(), onFill, mark, picker.inactiveIconOpacity);
		ui::RoundedBox(frame, false, paint.fill, kClear, pickerhighlight::kOutlineWidth, pickerhighlight::kCornerRadius);
		if (image) ui::Colour(image, paint.icon[0], paint.icon[1], paint.icon[2], 1.0f);
		ui::Call fade(image, L"SetRenderOpacity");
		if (fade && fade.Size("InOpacity") == 4 && fade.Set("InOpacity", paint.opacity)) fade.RunGuarded();
	}

	void Picker::PaintStolen()
	{
		constexpr float mark[4]{ stolenfilter::kStolenFill[0], stolenfilter::kStolenFill[1], stolenfilter::kStolenFill[2], 1.0f };
		PaintToggle(reflect::Get(stolenFrame), reflect::Get(stolenImage), kind == 0 && uistyle::SkinFor(mrconfig::Get().stylePreset).invertToggles, stolenOnly, stolenFocus, stolenfilter::kStolenFill, mark);
	}

	void Picker::PaintEnchanted()
	{
		PaintToggle(reflect::Get(enchantedFrame), reflect::Get(enchantedImage), kind == 0 && uistyle::SkinFor(mrconfig::Get().stylePreset).invertToggles, enchantedOnly, enchantedFocus,
			enchantedfilter::kEnchantedFill, filtertoggle::kEnchantedMark);
	}

	void Picker::PaintController()
	{
		PaintGroup();   // header opacity, and the category sort's parchment/arrow (hidden under the controller focus fill)
		// Applied membership is not focus. Match the owner's native square, borderless #E6E4DF reference.
		const auto& focusFill = uistyle::Get(mrconfig::Get().stylePreset).picker.focusFill;
		ui::RoundedBox(reflect::Get(headerFrame), false, controllerFocus ? focusFill.data() : kClear,
			kClear, pickerhighlight::kOutlineWidth, pickerhighlight::kCornerRadius);
		PaintStolen();
		PaintEnchanted();
		const int hoveredCell = mouseHoverCell.load();
		const auto& cellFill = kind == 0 && uistyle::SkinFor(mrconfig::Get().stylePreset).recolor
			? uistyle::kInventorySkinPicker.focusFill : focusFill;
		const bool controllerInput = controllerPopup && !mouseInputActive.load();
		for (unsigned i = 0; i < itemtags::Info(view).count; ++i) {
			const bool highlighted = pickerhighlight::CellHighlighted(open, controllerInput, cursor, hoveredCell, i);
			ui::RoundedBox(reflect::Get(cells[i]), false, highlighted ? cellFill.data() : kClear,
				kClear, pickerhighlight::kOutlineWidth, pickerhighlight::kCornerRadius);
		}
	}
}
