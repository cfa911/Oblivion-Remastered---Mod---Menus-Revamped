#include "GearFilter.h"

#include "GearData.h"
#include "ControllerPolicy.h"
#include "ItemTags.h"
#include "PickerNavigation.h"
#include "MagicRows.h"
#include "PickerHighlight.h"
#include "Icons.h"
#include "PEHook.h"
#include "Reflect.h"
#include "Ui.h"
#include "UiText.h"
#include "TextUtil.h"



#include <unordered_map>
#include <cmath>

namespace gearfilter
{
	namespace
	{
		constexpr float kInk[4] = { 0.20f, 0.14f, 0.09f, 1 };
		// sRGB #D6D0C8 converted to linear Slate colour.
		constexpr float kPaper[4] = { 0.672443f, 0.630757f, 0.577580f, 1.0f }, kClear[4] = { 0, 0, 0, 0 };
		// Focus colour/flat geometry are defined in PickerHighlight.h; applied-filter state is not a highlight.
		// Item masks: low 32 bits = the first picker's tags, high 32 bits = the second picker's (Magic effect). A row that is
		// not a spell (for example an active effect) carries kPassAll and is never hidden.
		constexpr std::uint64_t kPassAll = magicrows::kPassAll;
		struct Rect
		{
			double x = 0, y = 0, w = 0, h = 0;
			bool Hit(double px, double py) const { return w > 0 && h > 0 && px >= x && py >= y && px < x + w && py < y + h; }
		};

		// One filtered list shared by every picker of the open menu.
		bool g_inside = false, g_dirty = true, g_haveApplied = false, g_dataReady = false;
		std::string g_query;
		std::vector<reflect::Handle> g_source;
		std::vector<UE::UObject*> g_applied;
		std::vector<int> g_indices;
		std::unordered_map<int, std::uint64_t> g_masks;
		std::uint64_t g_ticket = 0, g_dataDeadline = 0;

		void ResetShared()
		{
			geardata::Cancel();
			g_source.clear(); g_applied.clear(); g_indices.clear(); g_masks.clear();
			g_ticket = 0; g_dataReady = false; g_haveApplied = false; g_dirty = true; g_query.clear();
		}

		void Brush(UE::UObject* a_image, const wchar_t* a_icon)
		{
			if (auto* texture = icons::Texture(a_icon)) {
				ui::Call set(a_image, L"SetBrushFromTexture");
				set.Set("Texture", texture);
				set.Set("bMatchSize", false);
				set.Run();
			}
		}

		UE::UObject* CanvasChild(UE::UObject* a_canvas, const wchar_t* a_class, const wchar_t* a_name, double x, double y, double w, double h, int z = 0)
		{
			UE::UObject* slot = nullptr;
			auto* child = ui::AddToCanvas(a_canvas, a_class, a_name, &slot);
			if (!child || !slot) return nullptr;
			ui::Vec2(slot, L"SetPosition", x, y);
			ui::Vec2(slot, L"SetSize", w, h);
			ui::CallFirst(slot, L"SetZOrder", &z, sizeof(z));
			return child;
		}

		// The shipped Inventory and Magic item-name widgets use this asset and face.
		// Build an independent FSlateFontInfo for SetFont; do not mutate the live Font property
		// before the setter (it may treat an unchanged property as no visual update).
		bool PickerFont(UE::UObject* label, double size)
		{
			static reflect::Handle nativeFont;
			auto* fontObject = reflect::Get(nativeFont);
			if (!fontObject) {
				fontObject = ui::Load(L"/Game/UI/CommonStyle/Fonts/Font_Kingthings_Localized.Font_Kingthings_Localized");
				if (fontObject) nativeFont = reflect::Hold(fontObject);
			}
			if (!label || !fontObject || !reflect::Ok()) return false;
			auto* info = reinterpret_cast<UE::UStruct*>(UE::StaticFindObject<UE::UObject>(nullptr, nullptr, L"/Script/SlateCore.SlateFontInfo"));
			const auto fontOff = reflect::Offset(label->GetClass(), "Font");
			const auto fontSize = ui::SizeOf(label->GetClass(), "Font");
			const auto objOff = info ? reflect::Offset(info, "FontObject") : -1;
			const auto faceOff = info ? reflect::Offset(info, "TypefaceFontName") : -1;
			const auto sizeOff = info ? reflect::Offset(info, "Size") : -1;
			const auto sizeBytes = info ? ui::SizeOf(info, "Size") : 0;
			if (fontOff < 0 || fontSize <= 0 || fontSize > 512 || objOff < 0 || faceOff < 0 || sizeOff < 0 ||
				objOff + sizeof(fontObject) > static_cast<std::size_t>(fontSize) ||
				faceOff + sizeof(UE::FName) > static_cast<std::size_t>(fontSize) ||
				(sizeBytes != 4 && sizeBytes != 8) || sizeOff + sizeBytes > fontSize) return false;
			const auto* current = reinterpret_cast<const std::uint8_t*>(label) + fontOff;
			std::vector<std::uint8_t> copy(current, current + fontSize);
			std::memcpy(copy.data() + objOff, &fontObject, sizeof(fontObject));
			const UE::FName face(L"Regular-Regular");
			std::memcpy(copy.data() + faceOff, &face, sizeof(face));
			if (sizeBytes == 4) { const float value = static_cast<float>(size); std::memcpy(copy.data() + sizeOff, &value, 4); }
			else std::memcpy(copy.data() + sizeOff, &size, 8);
			return ui::CallFirst(label, L"SetFont", copy.data(), copy.size());
		}

		std::string ShortLabel(std::string_view full)
		{
			// Category names are fixed English strings. Keep the beginning and an explicit ellipsis.
			return full.size() > 7 ? std::string(full.substr(0, 4)) + "..." : std::string(full);
		}

		void BoundLabel(UE::UObject* label)
		{
			const std::uint8_t clipToBounds = 1, ellipsis = 1;
			const bool wrap = false;
			ui::CallFirst(label, L"SetClipping", &clipToBounds, sizeof(clipToBounds));
			ui::CallFirst(label, L"SetTextOverflowPolicy", &ellipsis, sizeof(ellipsis));
			ui::CallFirst(label, L"SetAutoWrapText", &wrap, sizeof(wrap));
		}

		void SetItems(UE::UObject* a_list, const std::vector<UE::UObject*>& a_items, bool a_refresh)
		{
			ui::Call clear(a_list, L"ClearListItems"); clear.Run();
			for (auto* item : a_items) ui::CallFirst(a_list, L"AddItem", &item, sizeof(item));
			if (a_refresh) {
				ui::Call regenerate(a_list, L"RegenerateAllEntries"); regenerate.Run();
				ui::Call refresh(a_list, L"RequestRefresh"); refresh.Run();
			}
		}

		int Index(UE::UObject* a_item)
		{
			ui::Call get(a_item, L"GetInventoryIndex");
			return get && get.Size("ReturnValue") == 4 && get.Run() ? get.Get<int>("ReturnValue") : -1;
		}

		// ---- Magic rows: school/range from the row, native spell type/effects from the player's spell list ----
		// Row layout (VLegacyMagicMenuItem, 136 bytes, proven 2026-10-24): Category +0x50, Type +0x51, InventoryIndex +0x54, School +0x58.
		// InventoryIndex - 1 is the spell's position in the player's list.
		using SpellRow = magicrows::Spell;
		static_assert(static_cast<unsigned>(RE::MagicSystem::SpellType::kSpell) == 0);
		static_assert(static_cast<unsigned>(RE::MagicSystem::SpellType::kPower) == 2);
		static_assert(static_cast<unsigned>(RE::MagicSystem::SpellType::kLesserPower) == 3);

		int ReadSpellsRaw(SpellRow* a_out, int a_max)
		{
			int n = 0;
			__try {
				auto* player = RE::PlayerCharacter::GetSingleton();
				auto* base = player ? player->data.objectReference : nullptr;
				if (!base || !base->Is(RE::FormType::NPC)) return 0;
				auto* list = static_cast<RE::TESSpellList*>(static_cast<RE::TESNPC*>(base));
				using Node = RE::BSSimpleList<RE::SpellItem*>::Node;
				for (auto* node = reinterpret_cast<Node*>(&list->spells); node && n < a_max; node = node->next) {
					auto* spell = node->item;
					SpellRow row{};
					if (spell && spell->Is(RE::FormType::Spell)) {
						row.type = static_cast<unsigned>(spell->data.spellType);
						auto** effect = reinterpret_cast<void**>(&spell->effectList);
						for (int k = 0; effect && k < 8; ++k) {
							auto* item = reinterpret_cast<std::uint8_t*>(effect[0]);
							if (item) row.codes[row.count++] = *reinterpret_cast<std::uint32_t*>(item + 0x08);
							effect = reinterpret_cast<void**>(effect[1]);
						}
						row.valid = true;
					}
					a_out[n++] = row;
				}
			} __except (EXCEPTION_EXECUTE_HANDLER) {
			}
			return n;
		}

		bool ReadByte(const void* a_object, std::size_t a_offset, std::uint8_t& a_value)
		{
			__try { a_value = *(reinterpret_cast<const std::uint8_t*>(a_object) + a_offset); return true; }
			__except (EXCEPTION_EXECUTE_HANDLER) { return false; }
		}

		void ComputeMagicMasks(const std::vector<UE::UObject*>& a_items)
		{
			std::vector<SpellRow> spells(600);
			spells.resize(static_cast<std::size_t>(ReadSpellsRaw(spells.data(), 600)));
			for (auto* item : a_items) {
				const int index = Index(item);
				std::uint8_t range = 0, school = 0;
				const bool read = item && ReadByte(item, 0x51, range) && ReadByte(item, 0x58, school);
				const auto mask = read ? magicrows::Mask(range, school, index, spells.data(), spells.size()) : kPassAll;
				// A genuine unresolved row still fails open; report once, never every row/menu tick.
				static bool warned = false;
				if (!warned && read && magicrows::CastableRange(range) && mask == kPassAll) {
					warned = true;
					logger::warn("Sorting Overhaul: native spell-type power filtering could not resolve a Magic row (index={}, range={}, captured={}); leaving it unfiltered", index, range, spells.size());
				}
				g_masks[index] = mask;
			}
		}

		std::vector<reflect::Handle> g_ownedHeaders;
		std::uint64_t g_nativePeerRevision = 0;
		bool IsOwnedHeader(UE::UObject* widget)
		{
			for (const auto& handle : g_ownedHeaders) if (reflect::Get(handle) == widget) return true;
			return false;
		}
		bool OwnHeaderEvent(UE::UObject*, UE::UFunction*, void*);

		// One header icon plus its popup. The Inventory has one; the Magic menu has two (school, effect).
		struct Picker
		{
			int kind = -1;  // -1 inactive, 0 inventory tabs, 1 magic school, 2 magic effect
			std::mutex inputLock;
			Rect headerPx, popupPx, cellsPx[itemtags::kMaxChoices];
			bool inputActive = false, inputOpen = false, swallowUp = false, swallowEscape = false;
			std::uint64_t doubleClickUntil = 0;
			int command = -1;
			std::atomic<bool> active{ false };
			bool open = false, paintedOpen = false, controllerFocus = false, controllerPopup = false;
			unsigned cursor = 0;
			reflect::Handle carousel;
			std::vector<reflect::Handle> nativePeers;
			bool nativeFocusWarning = false;
			unsigned choice = 0, appliedChoice = 0;  // flat membership ID, independent of popup depth
			unsigned category = 0, appliedCategory = 0;  // All Items page only: 0 = none, else 1 weapons, 2 gear, 3 potions, 4 misc
			itemtags::Tab tab = itemtags::Tab::Gear, view = itemtags::Tab::Gear;  // view: whose choices the popup shows
			bool tabSet = false, styled = false, widgetWarning = false, fontsReady = false, fontWarning = false;
			reflect::Handle menu, canvas, header, headerImage, headerFrame, popup, popupSlot, cache, nativeSchool;
			reflect::Handle cells[itemtags::kMaxChoices], icons[itemtags::kMaxChoices], labels[itemtags::kMaxChoices], title;
			std::uint64_t nextLayout = 0, nextHitMeasure = 0, nextFontTry = 0;

			unsigned Shift() const { return kind == 2 ? 32u : 0u; }

			// does an item with this mask pass this picker's choice?
			bool Passes(std::uint64_t a_mask) const
			{
				if (a_mask & kPassAll) return true;
				const auto tags = static_cast<std::uint32_t>(a_mask >> Shift());
				return tab == itemtags::Tab::AllItems ? itemtags::IncludesAll(category, choice, tags) : itemtags::Includes(choice, tags);
			}

			bool BuildPopup(UE::UObject* a_canvas)
			{
				static unsigned serial = 0;
				const std::wstring suffix = std::to_wstring(++serial);
				UE::UObject* slot = nullptr;
				auto* popupWidget = ui::AddToCanvas(a_canvas, L"/Script/UMG.CanvasPanel", (L"SlPiecePopup" + suffix).c_str(), &slot);
				if (!popupWidget || !slot) return false;
				ui::Visible(popupWidget, false);
				ui::Vec2(slot, L"SetSize", pickergrid::Width, pickergrid::Height(itemtags::Info(view).count));
				const int z = 4000;
				ui::CallFirst(slot, L"SetZOrder", &z, sizeof(z));
				auto* background = CanvasChild(popupWidget, L"/Script/UMG.Image", (L"PiecePaper" + suffix).c_str(), 0, 0, pickergrid::Width, pickergrid::Height(itemtags::Info(view).count));
				ui::RoundedBox(background, false, kPaper, kInk, 1.5f);
				const std::uint8_t visible = 0;
				ui::CallFirst(background, L"SetVisibility", &visible, sizeof(visible));
				auto* titleWidget = CanvasChild(popupWidget, L"/Script/UMG.TextBlock", (L"PieceTitle" + suffix).c_str(), 12, 6, 224, 24, 1);
				uix::SetText(titleWidget, itemtags::Info(view).title);
				BoundLabel(titleWidget);
				uix::Ink(titleWidget, kInk);
				title = reflect::Hold(titleWidget);
				for (unsigned i = 0; i < itemtags::kMaxChoices; ++i) {
					const double x = pickergrid::CellX(i), y = pickergrid::CellY(i);
					const auto name = std::to_wstring(i) + L"_" + suffix;
					auto* cell = CanvasChild(popupWidget, L"/Script/UMG.Image", (L"PieceCell" + name).c_str(), x, y, 72, 62, 1);
					ui::RoundedBox(cell, false, kClear, kClear, 0);
					cells[i] = reflect::Hold(cell);
					auto* image = CanvasChild(popupWidget, L"/Script/UMG.Image", (L"PieceIcon" + name).c_str(), x + 15, y + 1, 42, 40, 2);
					icons[i] = reflect::Hold(image);
					auto* label = CanvasChild(popupWidget, L"/Script/UMG.TextBlock", (L"PieceLabel" + name).c_str(), x, y + 41, 72, 21, 2);
					labels[i] = reflect::Hold(label);
					uix::SetText(label, "");
					BoundLabel(label);
					uix::Ink(label, kInk);
					const std::uint8_t center = 1;
					ui::CallFirst(label, L"SetJustification", &center, sizeof(center));
				}
				// A collapsed ListView owns strong UE references to the full source list while its visible list is filtered.
				// It generates no visible rows; handles alone would not keep removed list items alive through GC.
				auto* cacheWidget = CanvasChild(a_canvas, L"/Script/UMG.ListView", (L"SlPieceSource" + suffix).c_str(), 0, 0, 0, 0);
				if (!cacheWidget) return false;
				ui::Visible(cacheWidget, false);
				cache = reflect::Hold(cacheWidget);
				popup = reflect::Hold(popupWidget);
				popupSlot = reflect::Hold(slot);
				canvas = reflect::Hold(a_canvas);
				paintedOpen = false;
				fontsReady = false; fontWarning = false; nextFontTry = 0;
				styled = false;
				ui::Visible(popupWidget, false);
				return true;
			}

			const wchar_t* HeaderIcon() const
			{
				if (tab != itemtags::Tab::AllItems) return itemtags::SelectionIcon(tab, choice);
				if (category == 0 || category > 4) return itemtags::Info(tab).icons[0];
				if (choice == 0) return itemtags::Info(tab).icons[category];  // the whole category
				return itemtags::SelectionIcon(static_cast<itemtags::Tab>(category - 1), choice);
			}

			// Which cell of the popup's current view is the active selection (99 = none).
			unsigned Highlight() const
			{
				return pickernav::Highlight(tab, view, category, choice);
			}

			// Labels, icons and visible cells follow the popup's view; the header icon follows the current selection.
			void StyleCells()
			{
				const auto& info = itemtags::Info(view);
				uix::SetText(reflect::Get(title), info.title);
				for (unsigned i = 0; i < itemtags::kMaxChoices; ++i) {
					const bool on = i < info.count;
					ui::Visible(reflect::Get(cells[i]), on);
					ui::Visible(reflect::Get(icons[i]), on);
					ui::Visible(reflect::Get(labels[i]), on);
					if (on) {
						Brush(reflect::Get(icons[i]), info.icons[i]);
						const auto display = view == itemtags::Tab::MagicSchool ? std::string(itemtags::SchoolPopupLabel(i)) :
							(view == itemtags::Tab::MagicEffect ? std::string(magiceffects::kChoices[i].label) : ShortLabel(info.names[i]));
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

			bool InsertHeader(UE::UObject* a_panel, UE::UObject* navigation)
			{
				ui::Call count(a_panel, L"GetChildrenCount");
				if (!count || !count.Run()) return false;
				const int n = count.Get<int>("ReturnValue");
				if (n < 1 || n > 12) return false;
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
					if (!get.Run()) return false;
					auto* widget = get.Get<UE::UObject*>("ReturnValue");
					if (!widget || widget == reflect::Get(header)) continue;
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
					children.push_back(child);
				}
				if (children.empty()) return false;
				nativePeers.clear();
				++g_nativePeerRevision;
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
					box = ui::CreateWidget(classPath.c_str());
					if (!box) return false;
					header = reflect::Hold(box); // establish ownership before registering/focusing
					std::erase_if(g_ownedHeaders, [](const auto& h) { return !reflect::Get(h); });
					g_ownedHeaders.push_back(header); // retain identity until GC, including detached/retired widgets
					struct Rollback {
						Picker& picker; bool complete = false;
						~Rollback() {
							if (complete) return;
							if (auto* widget = reflect::Get(picker.header)) { ui::Call remove(widget, L"RemoveFromParent"); remove.Run(); }
							picker.header = {}; picker.carousel = {}; picker.headerImage = {}; picker.headerFrame = {};
						}
					} rollback{ *this };
					auto* inner = ui::RootCanvas(box, (L"SlPieceHeaderCanvas" + suffix).c_str());
					if (!inner) return false;
					auto* sizeBox = CanvasChild(inner, L"/Script/UMG.SizeBox", (L"SlPieceHeaderSize" + suffix).c_str(), 0, 0, 49, 49);
					if (!sizeBox) return false;
					ui::Float(sizeBox, L"SetWidthOverride", 49);
					ui::Float(sizeBox, L"SetHeightOverride", 49);
					auto* frame = CanvasChild(inner, L"/Script/UMG.Image", (L"SlPieceHeaderFrame" + suffix).c_str(), 0, 0, 49, 49);
					ui::RoundedBox(frame, false, kClear, kClear, 0);
					auto* image = CanvasChild(inner, L"/Script/UMG.Image", (L"SlPieceHeaderIcon" + suffix).c_str(), 5, 4, 39, 40, 1);
					Brush(image, itemtags::Info(tab).icons[0]);
					if (!frame || !image) return false;
					headerImage = reflect::Hold(image); headerFrame = reflect::Hold(frame);
					ui::Call registerChild(navigation, L"AddChild");
					if (registerChild.Size("ChildToAdd") != 8 ||
						!registerChild.Set("ChildToAdd", box) || !registerChild.RunGuarded()) return false;
					carousel = reflect::Hold(navigation);
					rollback.complete = true;
				}
				// InsertChildAt/ShiftChild are not reflected: reattach the same native widgets preserving slot layout and existing
				// bindings, with our icon first, before A-Z (and before the other picker's icon when both are attached).
				ui::Call clear(a_panel, L"ClearChildren"); if (!clear.Run()) return false;
				if (schoolAt < 0) { ui::Call own(a_panel, L"AddChild"); own.Set("Content", box); own.Run(); }  // no native school button: first
				for (std::size_t i = 0; i < children.size(); ++i) {
					if (static_cast<int>(i) == schoolAt) { ui::Call own(a_panel, L"AddChild"); own.Set("Content", box); own.Run(); }
					ui::Call add(a_panel, L"AddChild"); add.Set("Content", children[i].widget); add.Run();
					auto* slot = add.Get<UE::UObject*>("ReturnValue");
					ui::CallFirst(slot, L"SetPadding", children[i].padding.data(), 16);
					if (children[i].hasSize) ui::CallFirst(slot, L"SetSize", children[i].size.data(), 8);
					ui::CallFirst(slot, L"SetHorizontalAlignment", &children[i].horizontal, 1);
					ui::CallFirst(slot, L"SetVerticalAlignment", &children[i].vertical, 1);
				}
				return true;
			}

			void Reset()
			{
				ResetShared();
				choice = 0; appliedChoice = 0; category = 0; appliedCategory = 0; view = tab; open = false;
				controllerFocus = controllerPopup = false; cursor = 0;
				nativeFocusWarning = false;
				paintedOpen = false; nextLayout = 0; nextHitMeasure = 0;
				widgetWarning = false;
				if (auto* cacheList = reflect::Get(cache)) SetItems(cacheList, {}, false);
				if (auto* image = reflect::Get(headerImage)) Brush(image, itemtags::Info(tab).icons[0]);
				if (auto* frame = reflect::Get(headerFrame)) ui::RoundedBox(frame, false, kClear, kClear, 0, 0);
				{ std::scoped_lock lock(inputLock); headerPx = {}; popupPx = {}; command = -1; inputOpen = false; swallowUp = false; swallowEscape = false; doubleClickUntil = 0; }
			}

			void Deactivate()
			{
				if (active.exchange(false)) {
					Reset();
					tabSet = false;
					if (auto* h = reflect::Get(header)) ui::Visible(h, false);
					if (auto* p = reflect::Get(popup)) ui::Visible(p, false);
				}
				std::scoped_lock lock(inputLock); inputActive = false; inputOpen = false; command = -1;
			}

			// a_kind: 0 inventory tabs, 1 magic school, 2 magic effect, -1 not shown
			void Tick(int a_kind, UE::UObject* a_menu, UE::UObject* a_canvas, float a_viewportScale)
			{
				bool show = false;
				itemtags::Tab wanted = itemtags::Tab::Gear;
				if (a_kind >= 0 && a_menu && a_canvas) {
					if (a_kind == 0) {
						auto** vm = reflect::At<UE::UObject*>(a_menu, reflect::Offset(a_menu->GetClass(), "VInventoryMenuViewModel"));
						if (vm && *vm) {
							auto* page = reflect::At<std::uint8_t>(*vm, reflect::Offset((*vm)->GetClass(), "CurrentPageInventory"));
							show = page && itemtags::TabFromPage(*page, wanted);
						}
					} else {
						// Visibility follows the native tab, never the filtered rows. Active effects is page 4.
						auto** vm = reflect::At<UE::UObject*>(a_menu, reflect::Offset(a_menu->GetClass(), "VMagicMenuViewModel"));
						ui::Call page(vm ? *vm : nullptr, L"GetCurrentPage");
						show = page && page.Size("ReturnValue") == 1 && page.Run() && page.Get<std::uint8_t>("ReturnValue") < 4;
						wanted = a_kind == 1 ? itemtags::Tab::MagicSchool : itemtags::Tab::MagicEffect;
					}
				}
				if (!show) { Deactivate(); kind = -1; return; }
				if (kind != a_kind) {  // another menu: forget everything and rebuild
					if (kind >= 0) { Reset(); tabSet = false; }
					kind = a_kind;
				}
				active.store(true);  // page identity is independent of whether its new widgets have finished layout
				if (!tabSet || tab != wanted) {  // another tab: its own choices and filter
					if (tabSet) Reset();
					tab = wanted; view = wanted; styled = false;
				}
				tabSet = true;
				// The game re-shows the native School wrapper when its filters update. Collapse it
				// on every tick (not only the 150 ms layout pass) so its glyph never renders beside ours.
				if (auto* native = reflect::Get(nativeSchool)) {
					ui::Call visible(native, L"IsVisible");
					if (!visible || visible.Size("ReturnValue") != 1 || !visible.Run() || visible.Get<bool>("ReturnValue")) {
						ui::Call hide(native, L"SetVisibility");
						const bool hidden = hide && hide.Size("InVisibility") == 1 &&
							hide.Set("InVisibility", static_cast<std::uint8_t>(1)) && hide.Run();
						if (!hidden) logger::warn("Sorting Overhaul: SO-SCHOOL-FLASH-20261025 could not collapse the native School wrapper");
					}
				}
				if (reflect::Get(menu) != a_menu || reflect::Get(canvas) != a_canvas) {
					Reset();
					for (const auto& handle : { popup, cache, header }) if (auto* old = reflect::Get(handle)) { ui::Call remove(old, L"RemoveFromParent"); remove.Run(); }
					menu = reflect::Hold(a_menu); popup = {}; cache = {}; header = {}; carousel = {}; nativeSchool = {}; nativePeers.clear();
					canvas = reflect::Hold(a_canvas); nextLayout = 0;
				}
				if (!reflect::Get(popup) && !BuildPopup(a_canvas)) {
					if (!widgetWarning) { logger::warn("gear piece filter: could not construct picker widgets"); widgetWarning = true; }
					return;
				}
				if (!styled) { StyleCells(); styled = true; }
				const auto now = GetTickCount64();
				if (!fontsReady && now >= nextFontTry) {
					nextFontTry = now + 1000;
					fontsReady = PickerFont(reflect::Get(title), 18);
					for (const auto& label : labels) fontsReady = PickerFont(reflect::Get(label), 14) && fontsReady;
					if (!fontsReady && !fontWarning) {
						logger::warn("Sorting Overhaul: item/spell Kingthings font unavailable; retrying");
						fontWarning = true;
					}
				}
				if (now >= nextLayout) {
					nextLayout = now + 150;
					auto* iconBox = ui::FindInTree(a_menu, "FilterIconBox");
					auto* panel = iconBox ? ui::FindInTree(iconBox, "ScrollBox") : nullptr;  // FilterIconBox is a user widget wrapping the real panel
					auto* headerWidget = reflect::Get(header);
					ui::Call parent(headerWidget, L"GetParent");
					const bool attached = parent && parent.Run() && parent.Get<UE::UObject*>("ReturnValue") == panel;
					if (!panel) return;
					if (!attached && !InsertHeader(panel, iconBox)) {
						if (!widgetWarning) { logger::warn("Sorting Overhaul: native controller focus-only header registration/layout unsupported"); widgetWarning = true; }
						return;
					}
					ui::Visible(reflect::Get(header), true);
					if (auto* native = reflect::Get(nativeSchool)) ui::Visible(native, false);  // the game may show it again when its filters update
					double x = 0, y = 0, w = 0, h = 0;
					if (ui::Measure(reflect::Get(header), x, y, w, h) && w > 0 && h > 0) {
						double cx = 0, cy = 0, cw = 0, ch = 0;
						ui::Measure(a_canvas, cx, cy, cw, ch);
						const double popupHeight = pickergrid::Height(itemtags::Info(view).count);
						ui::Vec2(reflect::Get(popupSlot), L"SetPosition",
							pickergrid::Position(x - cx + w - pickergrid::Width, pickergrid::Width, cw),
							pickergrid::Position(y - cy + h + 5, popupHeight, ch));
						std::scoped_lock lock(inputLock);
						headerPx = { x * a_viewportScale, y * a_viewportScale, w * a_viewportScale, h * a_viewportScale };
					}
				}
				int cmd = -1;
				{ std::scoped_lock lock(inputLock); cmd = command; command = -1; }
				if (cmd == pickergrid::kToggleCommand) {
					open = !open; nextHitMeasure = 0;
					if (open && view != tab) { view = tab; styled = false; }  // always open on the top level
					cursor = Highlight() < itemtags::Info(tab).count ? Highlight() : 0;
				}
				else if (pickergrid::IsSelection(cmd, itemtags::Info(view).count)) {
					const auto next = pickernav::Choose(tab, view, category, choice, static_cast<unsigned>(cmd));
					if (view != next.view) { view = next.view; styled = false; nextHitMeasure = 0; nextLayout = 0; }
					category = next.category; choice = next.choice; cursor = next.cursor;
					open = !next.close;
				}
				else if (cmd == -3) {
					const auto next = pickernav::Back(tab, view, category, choice);
					if (view != next.view) { view = next.view; styled = false; nextHitMeasure = 0; nextLayout = 0; }
					cursor = next.cursor;
					if (next.close) open = false;
				}
				else if (cmd == -2) open = false;
				// A nested view changed after the normal style pass. Update visible cells
				// before measuring input regions, not a tick later with stale parent cells.
				if (!styled) { StyleCells(); styled = true; }
				if (!open) controllerPopup = false;
				const std::uint8_t popupVisibility = open ? 0 : 1;  // visible background blocks hover/click hit tests on rows below it
				if (open != paintedOpen) {
					ui::CallFirst(reflect::Get(popup), L"SetVisibility", &popupVisibility, sizeof(popupVisibility));
					paintedOpen = open;
				}

				if (cmd >= 0) Brush(reflect::Get(headerImage), HeaderIcon());
				if (cmd != -1) PaintController();
				Rect popupRect, cellRects[itemtags::kMaxChoices];
				const bool measure = open && now >= nextHitMeasure;
				if (measure) {
					nextHitMeasure = now + 100;
					ui::Measure(reflect::Get(popup), popupRect.x, popupRect.y, popupRect.w, popupRect.h);
					if (popupRect.w <= 0 || popupRect.h <= 0) nextHitMeasure = 0;  // wait for the first Slate layout
					popupRect.x *= a_viewportScale; popupRect.y *= a_viewportScale; popupRect.w *= a_viewportScale; popupRect.h *= a_viewportScale;
					for (unsigned i = 0; i < itemtags::Info(view).count; ++i) {
						auto& rect = cellRects[i]; ui::Measure(reflect::Get(cells[i]), rect.x, rect.y, rect.w, rect.h);
						rect.x *= a_viewportScale; rect.y *= a_viewportScale; rect.w *= a_viewportScale; rect.h *= a_viewportScale;
					}
				}
				// Never hold the window-message mutex across ProcessEvent / geometry calls.
				{
					std::scoped_lock lock(inputLock);
					inputActive = true; inputOpen = open;
					if (measure) { popupPx = popupRect; std::copy(std::begin(cellRects), std::end(cellRects), cellsPx); }
				}
				active.store(true);
			}

			bool SortCarouselFocused()
			{
				if (!active.load() || !ui::ShownOnScreen(reflect::Get(header))) return false;
				ui::Call mode(reflect::Get(menu), L"IsFilterFocused");
				if (mode && mode.Size("IsClosed") == 1) {
					if (!mode.RunGuarded() || !mode.Get<std::uint8_t>("IsClosed")) return false;
				} else {
					// Magic may not expose Inventory's Blueprint query: use the inherited focus getter.
					ui::Call nativeFocus(reflect::Get(carousel), L"GetIsFocussed");
					if (!nativeFocus || nativeFocus.Size("ReturnValue") != 1 ||
						!nativeFocus.RunGuarded() || !nativeFocus.Get<std::uint8_t>("ReturnValue")) return false;
				}
				return true;
			}

			bool ControllerFocused()
			{
				if (!SortCarouselFocused()) return false;
				ui::Call focused(reflect::Get(carousel), L"GetFocusedElement");
				return focused && focused.Size("ReturnValue") == 8 && focused.RunGuarded() &&
					focused.Get<UE::UObject*>("ReturnValue") == reflect::Get(header);
			}

			UE::UObject* NativeInner(UE::UObject* wrapper)
			{
				if (!wrapper || IsOwnedHeader(wrapper) || ui::SizeOf(wrapper->GetClass(), "filterIcon") != 8) return nullptr;
				auto** inner = reflect::At<UE::UObject*>(wrapper, reflect::Offset(wrapper->GetClass(), "filterIcon"));
				return inner ? *inner : nullptr;
			}

			void WarnNativeFocus()
			{
				if (!nativeFocusWarning) {
					logger::warn("Sorting Overhaul: native activated-sort background mask unavailable");
					nativeFocusWarning = true;
				}
			}

			void PaintController()
			{
				// Applied membership is not focus. Match the owner's native square, borderless #E6E4DF reference.
				ui::RoundedBox(reflect::Get(headerFrame), false, controllerFocus ? pickerhighlight::kFill.data() : kClear,
					kClear, pickerhighlight::kOutlineWidth, pickerhighlight::kCornerRadius);
				for (unsigned i = 0; i < itemtags::Info(view).count; ++i) {
					const bool focused = open && controllerPopup && i == cursor;
					ui::RoundedBox(reflect::Get(cells[i]), false, focused ? pickerhighlight::kFill.data() : kClear,
						kClear, pickerhighlight::kOutlineWidth, pickerhighlight::kCornerRadius);
				}
			}

			void Queue(int cmd)
			{
				std::scoped_lock lock(inputLock); command = cmd;
			}

			bool Contains(double a_x, double a_y)
			{
				std::scoped_lock lock(inputLock);
				return inputActive && (swallowUp || headerPx.Hit(a_x, a_y) || (inputOpen && popupPx.Hit(a_x, a_y)));
			}

			bool Message(UINT a_message, WPARAM a_key, LPARAM a_position)
			{
				std::scoped_lock lock(inputLock);
				if (a_message == WM_LBUTTONUP && swallowUp) { swallowUp = false; return true; }
				if (a_message == WM_KEYUP && a_key == VK_ESCAPE && swallowEscape) { swallowEscape = false; return true; }
				if (!inputActive) return false;
				if (a_message == WM_LBUTTONDBLCLK && GetTickCount64() <= doubleClickUntil) { swallowUp = true; return true; }
				if (a_message == WM_KEYDOWN && a_key == VK_ESCAPE && inputOpen) { command = -2; swallowEscape = true; return true; }
				if (a_message != WM_LBUTTONDOWN) return false;
				const double x = static_cast<short>(LOWORD(a_position)), y = static_cast<short>(HIWORD(a_position));
				if (headerPx.Hit(x, y)) command = pickergrid::kToggleCommand;
				else if (inputOpen) {
					command = -2;
					for (int i = 0; i < itemtags::kMaxChoices; ++i) if (cellsPx[i].Hit(x, y)) { command = i; break; }
				} else return false;
				swallowUp = true;
				doubleClickUntil = GetTickCount64() + GetDoubleClickTime();
				return true;
			}
		};

		Picker g_pickers[2];  // [0] inventory tabs or magic school (owns the filtered list), [1] magic effect

		// One visual mask shared by both Magic pickers. No native focus/selection state changes.
		struct NativeBackground
		{
			reflect::Handle widget;
			pickerhighlight::OpacitySnapshot opacity;
		};
		std::vector<NativeBackground> g_nativeBackgrounds;
		reflect::Handle g_maskCarousel;
		Picker* g_maskOwner = nullptr;
		std::uint64_t g_maskRevision = 0, g_maskRetryAt = 0;

		bool SetNativeBackgroundOpacity(UE::UObject* widget, float value)
		{
			ui::Call fade(widget, L"SetRenderOpacity");
			return fade && fade.Size("InOpacity") == 4 && fade.Set("InOpacity", value) && fade.RunGuarded();
		}

		void RestoreNativeBackgrounds()
		{
			for (auto& entry : g_nativeBackgrounds)
				if (auto* widget = reflect::Get(entry.widget); widget && entry.opacity.HasValue())
					SetNativeBackgroundOpacity(widget, entry.opacity.Original());
			g_nativeBackgrounds.clear();
			g_maskCarousel = {}; g_maskOwner = nullptr; g_maskRevision = 0; g_maskRetryAt = 0;
		}

		void UpdateNativeBackgroundMask()
		{
			Picker* owner = nullptr;
			for (auto& picker : g_pickers)
				if (picker.active.load() && pickerhighlight::Owns(picker.controllerFocus, picker.open, picker.controllerPopup)) {
					owner = &picker; break;
				}
			if (!owner) { if (!g_nativeBackgrounds.empty()) RestoreNativeBackgrounds(); return; }
			auto* carousel = reflect::Get(owner->carousel);
			if (!carousel) { RestoreNativeBackgrounds(); return; }
			if (reflect::Get(g_maskCarousel) != carousel) {
				RestoreNativeBackgrounds(); g_maskCarousel = reflect::Hold(carousel);
			}
			const auto now = GetTickCount64();
			if (g_maskOwner != owner || g_maskRevision != g_nativePeerRevision ||
				(g_nativeBackgrounds.empty() && now >= g_maskRetryAt)) {
				std::erase_if(g_nativeBackgrounds, [](const auto& entry) { return !reflect::Get(entry.widget); });
				for (const auto& handle : owner->nativePeers) {
					auto* wrapper = reflect::Get(handle);
					auto* inner = owner->NativeInner(wrapper);
					if (!inner) continue; // excludes every owned custom header before touching filterIcon
					UE::UObject* background = nullptr;
					if (ui::SizeOf(inner->GetClass(), "filterIcon_background") == 8) {
						auto** property = reflect::At<UE::UObject*>(inner, reflect::Offset(inner->GetClass(), "filterIcon_background"));
						if (property) background = *property;
					}
					if (!background) background = ui::ChildNamed(inner, L"filterIcon_background");
					if (!background) background = ui::FindInTree(inner, "filterIcon_background");
					if (!background) { owner->WarnNativeFocus(); continue; }
					const bool known = std::any_of(g_nativeBackgrounds.begin(), g_nativeBackgrounds.end(),
						[background](const auto& entry) { return reflect::Get(entry.widget) == background; });
					if (known) continue; // preserve original snapshot across School/Effect handoff
					const float original = ui::RenderOpacity(background);
					if (!std::isfinite(original) || g_nativeBackgrounds.size() >= 32) { owner->WarnNativeFocus(); continue; }
					NativeBackground entry{ reflect::Hold(background), {} };
					entry.opacity.Capture(original);
					g_nativeBackgrounds.push_back(entry);
				}
				g_maskOwner = owner; g_maskRevision = g_nativePeerRevision; g_maskRetryAt = now + 150;
				if (g_nativeBackgrounds.empty()) owner->WarnNativeFocus();
			}
			// Cached tiles only. No ProcessEvent when opacity is already zero; no periodic tree/global scans.
			for (const auto& entry : g_nativeBackgrounds)
				if (auto* widget = reflect::Get(entry.widget); widget && ui::RenderOpacity(widget) != 0.0f)
					if (!SetNativeBackgroundOpacity(widget, 0.0f)) owner->WarnNativeFocus();
		}


		bool OwnHeaderEvent(UE::UObject* object, UE::UFunction* function, void*)
		{
			// Never cancel an event on a native widget or a class-default object.
			if (!IsOwnedHeader(object)) return false;
			const auto name = pe::FunctionName(function);
			return name == "OnFocus" || name == "OnUnfocus";
		}
		sortpad::State g_pad;
	}


	bool Active() { return g_pickers[0].active.load() || g_pickers[1].active.load(); }
	void Invalidate() { if (!g_inside) g_dirty = true; }

	void Tick(Mode a_mode, UE::UObject* a_menu, UE::UObject* a_canvas, float a_viewportScale)
	{
		if (a_mode == Mode::Magic) {
			g_pickers[1].Tick(2, a_menu, a_canvas, a_viewportScale);  // attached first so the school icon ends up before it
			g_pickers[0].Tick(1, a_menu, a_canvas, a_viewportScale);
		} else if (a_mode == Mode::Inventory) {
			g_pickers[1].Tick(-1, nullptr, nullptr, 1.0f);
			g_pickers[0].Tick(0, a_menu, a_canvas, a_viewportScale);
		} else {
			g_pickers[1].Tick(-1, nullptr, nullptr, 1.0f);
			g_pickers[0].Tick(-1, nullptr, nullptr, 1.0f);
		}
		UpdateNativeBackgroundMask();
	}

	void Filter(UE::UObject* a_list, const std::vector<UE::UObject*>& a_live, const std::string& a_query, NameMatches a_nameMatches)
	{
		auto& primary = g_pickers[0];
		auto& second = g_pickers[1];
		if (g_inside || !Active() || primary.kind < 0) return;
		g_inside = true;
		struct Exit { ~Exit() { g_inside = false; } } exit;
		const bool magic = primary.kind != 0;
		bool changed = false;
		if (!g_haveApplied || a_live != g_applied) {
			g_source.clear();
			std::vector<int> indices;
			for (auto* item : a_live) { g_source.push_back(reflect::Hold(item)); indices.push_back(Index(item)); }
			SetItems(reflect::Get(primary.cache), a_live, false);
			if (magic) {
				g_indices = std::move(indices); g_masks.clear();
				ComputeMagicMasks(a_live);
				g_dataReady = true; g_ticket = 0;
			} else if (indices != g_indices || !g_ticket) {
				g_indices = std::move(indices); g_masks.clear(); g_dataReady = g_indices.empty();
				g_ticket = geardata::Request(g_indices); g_dataDeadline = GetTickCount64() + 2500;
			}
			changed = true;
		}
		std::vector<geardata::Entry> entries;
		if (!magic && g_ticket && geardata::Take(g_ticket, entries)) {
			g_dataReady = entries.size() == g_indices.size() && std::all_of(entries.begin(), entries.end(), [](const auto& e) { return e.valid; });
			if (g_dataReady) for (const auto& e : entries) g_masks[e.index] = e.tags;
			else logger::warn("gear piece filter: some equipment identities could not be resolved; leaving piece membership unfiltered");
			changed = true;
		}
		if (!magic && !g_dataReady && g_dataDeadline && GetTickCount64() > g_dataDeadline) {
			g_dataDeadline = 0;
			logger::warn("gear piece filter: equipment identities are unavailable; leaving piece membership unfiltered");
		}
		const bool useSecond = second.kind == 2;
		if (!changed && !g_dirty && a_query == g_query && primary.choice == primary.appliedChoice && primary.category == primary.appliedCategory &&
			(!useSecond || second.choice == second.appliedChoice)) return;
		std::vector<UE::UObject*> keep;
		for (const auto& handle : g_source) {
			auto* item = reflect::Get(handle);
			if (!item) { g_haveApplied = false; return; }
			const auto mask = g_masks.find(Index(item));
			const bool pieces = !g_dataReady || mask == g_masks.end() || (primary.Passes(mask->second) && (!useSecond || second.Passes(mask->second)));
			if (pieces && (a_query.empty() || a_nameMatches(item, a_query))) keep.push_back(item);
		}
		if (keep != a_live) SetItems(a_list, keep, true);
		g_applied = std::move(keep); g_haveApplied = true; g_dirty = false; g_query = a_query;
		primary.appliedChoice = primary.choice; primary.appliedCategory = primary.category;
		second.appliedChoice = second.choice; second.appliedCategory = second.category;
	}

	void Controller(std::uint16_t& buttons, short& x, short& y, bool connected)
	{
		if (!connected) {
			g_pad.Disconnect();
			for (auto& picker : g_pickers) {
				if (picker.controllerFocus || picker.controllerPopup) {
					picker.controllerFocus = picker.controllerPopup = false; picker.PaintController();
				}
			}
			UpdateNativeBackgroundMask(); return;
		}
		const sortpad::Input raw{ buttons, x, y };
		g_pad.Sample(raw, GetTickCount64());
		Picker* owner = nullptr;
		for (auto& picker : g_pickers) {
			const bool focused = picker.ControllerFocused();
			if (picker.controllerFocus != focused) {
				picker.controllerFocus = focused;
				if (!focused && picker.controllerPopup) {
					picker.Queue(-2); picker.controllerPopup = false; picker.PaintController();
					continue; // cancellation cannot be replaced by a confirm in the same sample
				}
				picker.PaintController();
			}
			// A mouse-opened picker is modal for the pad too, but only while its menu is active.
			if (!owner && picker.active.load() && ui::ShownOnScreen(reflect::Get(picker.menu)) && (focused || picker.open)) owner = &picker;
		}
		if (owner) {
			auto& picker = *owner;
			if (picker.open) {
				const bool newlyOwned = !picker.controllerPopup;
				const auto previousCursor = picker.cursor;
				picker.controllerPopup = true;
				g_pad.Consume(raw, 0xFFFF, true);
				if (g_pad.pressed & (sortpad::B | sortpad::Y)) picker.Queue(-3);
				else if (g_pad.pressed & sortpad::A) picker.Queue(static_cast<int>(picker.cursor));
				else if (g_pad.pulse != sortpad::Direction::None)
					picker.cursor = sortpad::Move(picker.cursor, itemtags::Info(picker.view).count, g_pad.pulse);
				if (newlyOwned || picker.cursor != previousCursor) picker.PaintController();
			} else {
				// Native methods retain their original A/up/down reversal. Only our focused header owns these.
				g_pad.Consume(raw, sortpad::A | sortpad::Up | sortpad::Down | sortpad::Left | sortpad::Right, true);
				if ((g_pad.pressed & sortpad::A) || g_pad.pulse == sortpad::Direction::Up || g_pad.pulse == sortpad::Direction::Down) {
					picker.controllerPopup = true; picker.Queue(pickergrid::kToggleCommand);
				}
				else if (g_pad.pulse == sortpad::Direction::Left || g_pad.pulse == sortpad::Direction::Right) {
					ui::Call navigate(reflect::Get(picker.carousel),
						g_pad.pulse == sortpad::Direction::Left ? L"NavigateToPrevElement" : L"NavigateToNextElement");
					navigate.RunGuarded();
				}
			}
		}
		UpdateNativeBackgroundMask();
		const auto filtered = g_pad.Filter(raw);
		buttons = filtered.buttons; x = filtered.x; y = filtered.y;
	}

	bool Contains(double a_x, double a_y)
	{
		return g_pickers[0].Contains(a_x, a_y) || g_pickers[1].Contains(a_x, a_y);
	}

	bool Message(UINT a_message, WPARAM a_key, LPARAM a_position)
	{
		return g_pickers[1].Message(a_message, a_key, a_position) || g_pickers[0].Message(a_message, a_key, a_position);
	}
}
