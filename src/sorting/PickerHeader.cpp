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

namespace sorting
{
	namespace detail
	{
		std::vector<reflect::Handle> g_ownedHeaders;
		std::uint64_t g_nativePeerRevision = 0;
		bool IsOwnedHeader(UE::UObject* widget)
		{
			for (const auto& handle : g_ownedHeaders) if (reflect::Get(handle) == widget) return true;
			return false;
		}

		bool OwnHeaderEvent(UE::UObject* object, UE::UFunction* function, void*)
		{
			// Never cancel an event on a native widget or a class-default object.
			if (!IsOwnedHeader(object)) return false;
			const auto name = pe::FunctionName(function);
			return name == "OnFocus" || name == "OnUnfocus";
		}
	}

	namespace detail
	{
	// RMI-004: the native Transfer action is about to run. Take our header out of the container side's sort row
	// so the native A-Z button is placed as without the mod; the layout pass puts it back once the hold ends.
	void Picker::DetachForTransfer()
	{
		if (kind != 4 || !active.load() || transfer.Held()) return;
		// RMI-048: with own sorts the native A-Z is always collapsed (ours replaces it), so the RMI-004 problem (an unplaced
		// native A-Z) cannot occur and its test below would detach the bar on every Transfer action (F, held F, R3).
		if (OwnSorts()) return;
		auto* iconBox = container::FindWidget(reflect::Get(menu), "FilterIconBox");
		if (!iconBox) iconBox = container::FindWidget(reflect::Get(menu), "inv_cont_filter");
		auto* panel = iconBox ? container::FindWidget(iconBox, "ScrollBox") : nullptr;
		if (!panel || !transferhold::ShouldDetach(NativeNameShown(panel))) return;
		ClearOwnFromBar();   // header, toggles and sorts leave together and return together
		ClearHitAreas();     // no clicks on where they were
		transfer.Start(GetTickCount64());
	}

	void Picker::ClearOwnFromBar()
	{
		const auto remove = [](const reflect::Handle& a_handle) {
			auto* widget = reflect::Get(a_handle);
			if (!widget || !reflect::Usable(widget)) return;
			ui::Call call(widget, L"RemoveFromParent");
			call.RunGuarded();
		};
		remove(enchantedHeader); remove(stolenHeader); remove(header);
		for (const auto& sort : sortHeaders) remove(sort);
	}

	void Picker::ClearHitAreas()
	{
		std::scoped_lock lock(inputLock);
		headerPx = {}; stolenPx = {}; enchantedPx = {};
		for (auto& r : sortPx) r = {};
	}

	// Whether the native A-Z (Name) button in this sort row has been given a size.
	bool Picker::NativeNameShown(UE::UObject* a_panel)
	{
		ui::Call count(a_panel, L"GetChildrenCount");
		if (!count || !count.Run()) return false;
		const int n = count.Get<int>("ReturnValue");
		for (int i = 0; i < n && i < 12; ++i) {
			ui::Call get(a_panel, L"GetChildAt"); get.Set("Index", i);
			if (!get.Run()) return false;
			auto* widget = get.Get<UE::UObject*>("ReturnValue");
			if (!widget || pe::Utf8(widget->GetFName().ToString()) != "Name") continue;
			ui::Call desired(widget, L"GetDesiredSize");
			const auto bytes = desired.Size("ReturnValue");
			if (!desired || (bytes != 8 && bytes != 16) || !desired.Run()) return false;
			struct Vec2f { float x, y; };
			struct Vec2d { double x, y; };
			return bytes == 8 ? desired.Get<Vec2f>("ReturnValue").x > 0 : desired.Get<Vec2d>("ReturnValue").x > 0;
		}
		return false;
	}

	bool Picker::InsertHeader(UE::UObject* a_panel, UE::UObject* navigation)
	{
		// RMI-043: a closing menu's filter bar and buttons stay in their object slots until GC; clearing and re-adding
		// them called through a freed vtable inside AddChild. Refuse to touch a bar that is being destroyed.
		if (!reflect::Usable(a_panel) || !reflect::Usable(navigation)) return false;
		ui::Call count(a_panel, L"GetChildrenCount");
		if (!count || !count.RunGuarded()) return false;
		const int n = count.Get<int>("ReturnValue");
		if (n < 1 || n > 40) return false;   // ours (up to 9) may still be in the bar: the native count is checked below
		struct Child
		{
			UE::UObject* widget;
			std::array<float, 4> padding{};
			std::array<std::uint8_t, 8> size{};
			std::uint8_t horizontal = 0, vertical = 0;
			bool hasSize = false;
		};
		std::vector<Child> children;
		int schoolAt = -1;  // the school picker takes the place of the native School sort button
		for (int i = 0; i < n; ++i) {
			ui::Call get(a_panel, L"GetChildAt"); get.Set("Index", i);
			if (!get.RunGuarded()) return false;
			auto* widget = get.Get<UE::UObject*>("ReturnValue");
			if (!widget || widget == reflect::Get(header) || widget == reflect::Get(stolenHeader) || widget == reflect::Get(enchantedHeader) ||
				OwnsSortHeader(widget)) continue;
			if (!reflect::Usable(widget)) return false;  // never clear the bar when a native child could not be re-added
			Child child{ widget };
			auto** slot = reflect::At<UE::UObject*>(widget, reflect::Offset(widget->GetClass(), "Slot"));
			if (!slot || !*slot) return false;
			const auto copy = [&](const char* field, void* dest, std::size_t bytes) {
				const int offset = reflect::Offset((*slot)->GetClass(), field);
				if (offset < 0 || ui::SizeOf((*slot)->GetClass(), field) != static_cast<int>(bytes)) return false;
				std::memcpy(dest, reinterpret_cast<std::uint8_t*>(*slot) + offset, bytes);
				return true;
			};
			if (!copy("Padding", child.padding.data(), 16) ||
				!copy("HorizontalAlignment", &child.horizontal, 1) || !copy("VerticalAlignment", &child.vertical, 1)) return false;
			child.hasSize = copy("Size", child.size.data(), 8);  // ScrollBoxSlot has no Size
			if (kind == 1 && pe::Utf8(widget->GetFName().ToString()) == "School") { schoolAt = static_cast<int>(children.size()); nativeSchool = reflect::Hold(widget); }
			if (kind == 5 && pe::Utf8(widget->GetFName().ToString()) == "ItemCategory") { schoolAt = static_cast<int>(children.size()); nativeSchool = reflect::Hold(widget); }
			children.push_back(child);
		}
		// The game's own children only: every picker's widgets (IsOwnedHeader) are left out - the Magic bar is shared by four.
		const auto natives = std::count_if(children.begin(), children.end(), [](const Child& c) { return !IsOwnedHeader(c.widget); });
		if (children.empty() || natives > 12) return false;
		nativePeers.clear();
		nativeSorts.clear();
		++g_nativePeerRevision;
		if (OwnSorts())
			for (const auto& child : children)
				if (const auto name = pe::Utf8(child.widget->GetFName().ToString());
					kind == 1 ? itemsort::MagicNativeKey(name) >= 0 : kind == 6 ? itemsort::ActiveNativeKey(name) >= 0 : itemsort::IsNativeSortWrapper(name))
						nativeSorts.push_back(reflect::Hold(child.widget));
		for (const auto& child : children)
			if (!IsOwnedHeader(child.widget)) nativePeers.push_back(reflect::Hold(child.widget));
		auto* box = reflect::Get(header);
		if (!box) {
			static unsigned serial = 0;
			const auto suffix = std::to_wstring(++serial);
			// Use the loaded native wrapper class, but never initialize or activate a native Filter.
			// The instance-scoped interceptor replaces ONLY our wrapper's focus-forwarding body.
			auto* templateWidget = children.front().widget;
			for (const auto& child : children)
				if (pe::Utf8(child.widget->GetFName().ToString()) == "Name") templateWidget = child.widget;
			auto* cls = templateWidget->GetClass();
			if (ui::SizeOf(cls, "filterIcon") != 8 || !pe::Intercept(cls, &OwnHeaderEvent)) return false;
			const auto fullName = pe::Utf8(cls->GetFullName());
			const auto classPath = textutil::Wide(fullName.substr(fullName.find(' ') + 1));
			struct Rollback {
				Picker& picker; bool complete = false;
				~Rollback() {
					if (complete) return;
					for (const auto& handle : { picker.header, picker.stolenHeader, picker.enchantedHeader })
						if (auto* widget = reflect::Get(handle)) { ui::Call remove(widget, L"RemoveFromParent"); remove.RunGuarded(); }
					for (int k = 0; k < kSorts; ++k) {
						if (auto* widget = reflect::Get(picker.sortHeaders[k])) { ui::Call remove(widget, L"RemoveFromParent"); remove.RunGuarded(); }
						picker.sortHeaders[k] = {}; picker.sortImages[k] = {}; picker.sortFrames[k] = {}; picker.sortArrows[k] = {}; picker.sortBacks[k] = {};
					}
					picker.header = {}; picker.carousel = {}; picker.headerImage = {}; picker.headerFrame = {};
					picker.stolenHeader = {}; picker.stolenImage = {}; picker.stolenFrame = {};
					picker.enchantedHeader = {}; picker.enchantedImage = {}; picker.enchantedFrame = {};
				}
			} rollback{ *this };
			// Native-height buttons; extra toggles use less horizontal space than category replacements.
			const auto make = [&](const wchar_t* a_prefix, const wchar_t* a_icon, reflect::Handle& a_header,
				reflect::Handle& a_image, reflect::Handle& a_frame, float a_width = 49.0f, bool a_tight = false,
				UE::UObject** a_inner = nullptr, float a_extra = 0.0f) -> UE::UObject* {
				auto* widget = ui::CreateWidget(classPath.c_str());
				if (!widget) return nullptr;
				a_header = reflect::Hold(widget); // establish ownership before registering/focusing
				std::erase_if(g_ownedHeaders, [](const auto& h) { return !reflect::Get(h); });
				g_ownedHeaders.push_back(a_header); // retain identity until GC, including detached/retired widgets
				const std::wstring name = a_prefix;
				auto* inner = ui::RootCanvas(widget, (name + L"Canvas" + suffix).c_str());
				if (!inner) return nullptr;
				const float scale = a_width / (a_tight ? 39.0f : 49.0f);
				const float contentWidth = a_width + a_extra;   // extra width right of the icon; the icon keeps its size
				auto* sizeBox = CanvasChild(inner, L"/Script/UMG.SizeBox", (name + L"Size" + suffix).c_str(), 0, 0, contentWidth, 49);
				if (!sizeBox) return nullptr;
				ui::Float(sizeBox, L"SetWidthOverride", contentWidth);
				ui::Float(sizeBox, L"SetHeightOverride", 49);
				auto* frame = CanvasChild(inner, L"/Script/UMG.Image", (name + L"Frame" + suffix).c_str(), 0, 0, contentWidth, 49);
				ui::RoundedBox(frame, false, kClear, kClear, 0);
				auto* image = CanvasChild(inner, L"/Script/UMG.Image", (name + L"Icon" + suffix).c_str(),
					a_tight ? 0 : 5 * scale + a_extra * 0.5f, 4 + (40 - 40 * scale) * 0.5f, 39 * scale, 40 * scale, 1);   // centred in a wider button
				if (a_icon) Brush(image, a_icon);
				if (!frame || !image) return nullptr;
				if (a_inner) *a_inner = inner;
				a_image = reflect::Hold(image); a_frame = reflect::Hold(frame);
				ui::Call registerChild(navigation, L"AddChild");
				if (registerChild.Size("ChildToAdd") != 8 ||
					!registerChild.Set("ChildToAdd", widget) || !registerChild.RunGuarded()) return nullptr;
				return widget;
			};
			// Register custom headers in their on-screen order: enchanted, stolen, category picker.
			if (enchantedfilter::Supports(kind) && mrconfig::Get().picker.enchantedFilter) {
				if (!make(L"SlEnchantedHeader", L"Enchanted.png", enchantedHeader, enchantedImage, enchantedFrame, 30, true)) return false;
				PaintEnchanted();
			}
			if (stolenfilter::Supports(kind) && mrconfig::Get().picker.stolenFilter) {
				if (!make(L"SlStolenHeader", L"Stolen-Item.png", stolenHeader, stolenImage, stolenFrame, 30, true)) return false;
				PaintStolen();
			}
			box = make(L"SlPieceHeader", itemtags::Info(tab).icons[0], header, headerImage, headerFrame);
			if (!box) return false;
			BrushHeader(itemtags::Info(tab).icons[0]);   // Magic school/type "All": the game's own icon
			// Own sort buttons after the category picker in the game's order (A-Z first); navigation order = screen order.
			if (OwnSorts()) {
				const auto used = kind == 1 ? itemsort::MagicKeys() : kind == 6 ? itemsort::ActiveKeys() :
					std::vector<itemsort::Key>{ itemsort::Key::Name, itemsort::Key::Health, itemsort::Key::Damage, itemsort::Key::Armor, itemsort::Key::Weight, itemsort::Key::Value };
				const auto widest = kind == 1 ? itemsort::Key::Cost : kind == 6 ? itemsort::Key::Magnitude : itemsort::Key::Value;   // the bar's last native button
				for (int k = 0; k < kSorts; ++k) {
					const auto key = static_cast<itemsort::Key>(k);
					if (std::find(used.begin(), used.end(), key) == used.end()) continue;
					UE::UObject* inner = nullptr;
					const std::wstring prefix = L"SlSort" + std::to_wstring(k);
					// The game's last sort button (Price) is 87 wide, not 54 (KB internals "Container sort bar children"): our Value
					// button is as wide (82 + margins), so every icon before it sits over its row column and its background and
					// highlight fill the whole button (owner 2026-11-07).
					const float extra = key == widest ? 33.0f : 0.0f;
					if (!make(prefix.c_str(), nullptr, sortHeaders[k], sortImages[k], sortFrames[k], 49.0f, false, &inner, extra)) return false;
					TextureBrush(reflect::Get(sortImages[k]), itemsort::TexturePath(key));
					// The game's active-sort parchment under the icon (shown by PaintSorts on the active sort only).
					auto* back = CanvasChild(inner, L"/Script/UMG.Image", (prefix + L"Back" + suffix).c_str(), 0, 0, 49 + extra, 49, 0);
					if (!back) return false;
					TextureBrush(back, itemsort::kActiveBackground);   // a missing texture only leaves the active sort unmarked
					sortBacks[k] = reflect::Hold(back);
					auto* arrow = CanvasChild(inner, L"/Script/UMG.Image", (prefix + L"Arrow" + suffix).c_str(), 35 + extra * 0.5f, 35, 12, 12, 2);
					if (!arrow) return false;
					sortArrows[k] = reflect::Hold(arrow);
				}
			}
			carousel = reflect::Hold(navigation);
			rollback.complete = true;
			PaintController();  // initial All opacity must be applied after the images exist
		}
		const auto addHeader = [&](UE::UObject* widget) {
			ui::Call add(a_panel, L"AddChild");
			if (!add.Set("Content", widget) || !add.RunGuarded()) return false;
			// Category replacements keep the native 54-unit footprint; extra toggles have no side padding.
			const bool toggle = widget == reflect::Get(stolenHeader) || widget == reflect::Get(enchantedHeader);
			const float side = toggle ? 0.0f : 2.5f;
			const std::array<float, 4> padding{ side, 0, side, 0 };
			return ui::CallFirstGuarded(add.Get<UE::UObject*>("ReturnValue"), L"SetPadding", padding.data(), sizeof(padding));
		};
		// InsertChildAt/ShiftChild are not reflected: reattach the same native widgets preserving slot layout and existing
		// bindings, with our icon first, before A-Z (and before the other picker's icon when both are attached).
		ui::Call clear(a_panel, L"ClearChildren"); if (!clear.RunGuarded()) return false;
		if (schoolAt < 0) {  // no native school button: the stolen-only icon leftmost (owner), then our picker
			if (auto* enchanted = reflect::Get(enchantedHeader); enchanted && !addHeader(enchanted)) return false;
			if (auto* stolen = reflect::Get(stolenHeader); stolen && !addHeader(stolen)) return false;
			if (!addHeader(box)) return false;
			for (const auto& sort : sortHeaders)
				if (auto* widget = reflect::Get(sort); widget && !addHeader(widget)) return false;
		}
		for (std::size_t i = 0; i < children.size(); ++i) {
			if (static_cast<int>(i) == schoolAt) { if (!addHeader(box)) return false; }
			// Magic: our A-Z / Cost take the places of the game's (collapsed) Name / Cost buttons.
			if (kind == 1)
				if (const int key = itemsort::MagicNativeKey(pe::Utf8(children[i].widget->GetFName().ToString())); key >= 0)
					if (auto* own = reflect::Get(sortHeaders[key]); own && !addHeader(own)) return false;
			ui::Call add(a_panel, L"AddChild");
			if (!add.Set("Content", children[i].widget) || !add.RunGuarded()) return false;
			auto* slot = add.Get<UE::UObject*>("ReturnValue");
			if (!slot) return false;
			ui::CallFirstGuarded(slot, L"SetPadding", children[i].padding.data(), 16);
			if (children[i].hasSize) ui::CallFirstGuarded(slot, L"SetSize", children[i].size.data(), 8);
			ui::CallFirstGuarded(slot, L"SetHorizontalAlignment", &children[i].horizontal, 1);
			ui::CallFirstGuarded(slot, L"SetVerticalAlignment", &children[i].vertical, 1);
		}
		return true;
	}
	}
}
