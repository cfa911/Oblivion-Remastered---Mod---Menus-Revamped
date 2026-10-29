#include "sorting/PickerInternal.h"

#include "core/Runtime.h"
#include "engine/ContainerMenu.h"
#include "engine/PEHook.h"
#include "engine/Reflect.h"
#include "engine/TextUtil.h"
#include "engine/Ui.h"
#include "engine/UiText.h"
#include "sorting/GearData.h"
#include "sorting/Icons.h"

namespace gearfilter::detail
{
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
		ui::RoundedBox(background, false, kPaper, kInk, 1.5f);
		const std::uint8_t visible = 0;
		ui::CallFirst(background, L"SetVisibility", &visible, sizeof(visible));
		auto* titleWidget = CanvasChild(popupWidget, L"/Script/UMG.TextBlock", (L"PieceTitle" + suffix).c_str(), 12, 6, width - 24, 24, 1);
		uix::SetText(titleWidget, itemtags::Info(view).title);
		BoundLabel(titleWidget);
		uix::Ink(titleWidget, kTitleInk);
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
			uix::Ink(label, kInk);
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
				Brush(reflect::Get(icons[i]), info.icons[i]);
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
		if (auto* image = reflect::Get(headerImage)) Brush(image, HeaderIcon());
		PaintController();
	}

	// On: the icon at full strength on the owner's red background. Off: dimmed. Controller focus uses the shared picker fill.
	void Picker::PaintStolen()
	{
		ui::RoundedBox(reflect::Get(stolenFrame), false, stolenFocus ? pickerhighlight::kFill.data() : stolenOnly ? stolenfilter::kStolenFill : kClear,
			kClear, pickerhighlight::kOutlineWidth, pickerhighlight::kCornerRadius);
		ui::Call fade(reflect::Get(stolenImage), L"SetRenderOpacity");
		if (fade && fade.Size("InOpacity") == 4 && fade.Set("InOpacity", stolenOnly ? 1.0f : 0.45f)) fade.RunGuarded();
	}

	void Picker::PaintController()
	{
		// Applied membership is not focus. Match the owner's native square, borderless #E6E4DF reference.
		ui::RoundedBox(reflect::Get(headerFrame), false, controllerFocus ? pickerhighlight::kFill.data() : kClear,
			kClear, pickerhighlight::kOutlineWidth, pickerhighlight::kCornerRadius);
		PaintStolen();
		for (unsigned i = 0; i < itemtags::Info(view).count; ++i) {
			const bool focused = open && controllerPopup && i == cursor;
			ui::RoundedBox(reflect::Get(cells[i]), false, focused ? pickerhighlight::kFill.data() : kClear,
				kClear, pickerhighlight::kOutlineWidth, pickerhighlight::kCornerRadius);
		}
	}
}
