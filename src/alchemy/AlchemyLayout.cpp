#include "alchemy/AlchemyInternal.h"
#include "sorting/Icons.h"
#include "core/DarkPack.h"

namespace alchemy
{
	namespace detail
	{

		// RMI-024: the count gets its own label in the row (next to the effect name), created once per row with the row text's
		// font and colour; the game's own effect text is never rewritten (its selected look on a recycled row depends on it)
		UE::UObject* CountLabel(UE::UObject* a_row, UE::UObject* a_text)
		{
			if (auto* existing = ui::FindInTree(a_row, "MrAlchemyCount")) return existing;
			auto* box = ui::FindInTree(a_row, "HorizontalBox");
			auto* cls = ui::Class(L"/Script/UMG.TextBlock");
			if (!box || !cls || !box->GetOuter()) return nullptr;
			auto* label = UE::NewObject<UE::UObject>(box->GetOuter(), cls, UE::FName(L"MrAlchemyCount"));
			if (!label) return nullptr;
			ui::Call add(box, L"AddChildToHorizontalBox");
			if (!add) return nullptr;
			add.Set("Content", label);
			add.Run();
			if (auto* slot = add.Get<UE::UObject*>("ReturnValue")) {
				const std::uint8_t centre = 2;   // EVerticalAlignment::VAlign_Center
				ui::CallFirst(slot, L"SetVerticalAlignment", &centre, sizeof(centre));
			}
			for (const char* prop : { "Font", "ColorAndOpacity" }) {   // the row text's own look
				const auto off = reflect::Offset(a_text->GetClass(), prop);
				const auto size = ui::SizeOf(a_text->GetClass(), prop);
				if (off >= 0 && size > 0 && size <= 512)
					ui::CallFirst(label, prop == std::string_view("Font") ? L"SetFont" : L"SetColorAndOpacity", reinterpret_cast<const std::uint8_t*>(a_text) + off, static_cast<std::size_t>(size));
			}
			return label;
		}

		void DecorateRow(UE::UObject* a_row)
		{
			if (!g_countsReady || !a_row) return;
			auto* label = Field(a_row, "EffectText");
			auto* text = label ? ui::FindInTree(label, "text_block") : nullptr;
			if (!text) return;
			const auto shown = uix::GetText(text);
			if (MatchKey(shown).empty()) return;
			const int n = CountOf(shown);
			auto*     count = CountLabel(a_row, text);
			if (count) {
				const auto want = " (" + std::to_string(n) + ")";
				if (uix::GetText(count) != want) uix::SetText(count, want);
				ui::Float(count, L"SetRenderOpacity", n > 0 ? 1.0f : UiPalette().alchemy.zeroOpacity);
			}
			ui::Float(label, L"SetRenderOpacity", n > 0 ? 1.0f : UiPalette().alchemy.zeroOpacity);
		}

		void OnEntryEvent(UE::UObject* a_row, UE::UFunction* a_fn, void* a_params)
		{
			if (!g_open || !Enabled() || !a_fn) return;
			static UE::UFunction* s_set = nullptr;
			if (!s_set) {
				if (pe::FunctionName(a_fn) != "OnListItemObjectSet") return;
				s_set = a_fn;
			}
			if (a_fn != s_set) return;
			DecorateRow(a_row);
			if (g_itemNames == 0 && a_params) {   // learn the item's name field once from what the row shows
				auto* item = *static_cast<UE::UObject**>(a_params);
				auto* label = Field(a_row, "EffectText");
				auto* text = label ? ui::FindInTree(label, "text_block") : nullptr;
				if (text && item && reflect::IsLive(item) && item->GetClass()) {
					const auto texts = ItemTexts(item);
					const auto size = item->GetClass()->propertiesSize;
					const auto shown = uix::GetText(text);
					g_nameOffset = LearnTextOffset(texts, shown);
					g_itemNames = g_nameOffset >= 0 ? 1 : -1;
				if (g_itemNames == 1) g_effectSearchDirty = true;
					if (g_itemNames < 0) logger::warn("alchemy: no effect name found in the filter item (size {:X}, {} texts, row \"{}\"); one-click removal is off", size, texts.size(), shown);
				}
			}
		}

		// RMI-024 (game bug, vanilla too): a row reused on scroll comes back unselected even when its effect is an active
		// filter. Set the row's own flag again and let its highlight event redraw it (the event draws the selected look
		// from bIsSelected). Done from the tick, never inside the game's own row call.
		void RestoreSelected(UE::UObject* a_row, const std::vector<std::string>& a_active)
		{
			if (a_active.empty() || !a_row || !a_row->GetClass()) return;
			const auto off = reflect::Offset(a_row->GetClass(), "bIsSelected");
			auto*      label = Field(a_row, "EffectText");
			auto*      text = label ? ui::FindInTree(label, "text_block") : nullptr;
			auto*      check = Field(a_row, "Checkmark");
			auto*      back = Field(a_row, "Selected_State");
			if (off < 0 || !text || !check || !back || !check->GetClass()) return;
			// the look a click gives (probe 6): bIsSelected, the diamond (Checkmark) and the dark row (Selected_State) Visible
			const auto vis = reflect::Offset(check->GetClass(), "Visibility");
			auto*      flag = reinterpret_cast<std::uint8_t*>(a_row) + off;
			const bool shown = *flag != 0 && vis >= 0 && *(reinterpret_cast<const std::uint8_t*>(check) + vis) == 0;
			if (!ShouldRestoreSelected(uix::GetText(text), shown, a_active)) return;
			*flag = 1;
			const std::uint8_t visible = 0;   // ESlateVisibility::Visible
			ui::CallFirst(check, L"SetVisibility", &visible, sizeof(visible));
			ui::CallFirst(back, L"SetVisibility", &visible, sizeof(visible));
		}

		void ClearStaleFilterHover(const std::vector<UE::UObject*>& a_rows)
		{
			if (!PadInput()) return;
			UE::UObject* focused = nullptr;
			for (auto* row : a_rows) {
				ui::Call own(row, L"HasAnyUserFocus");
				ui::Call child(row, L"HasFocusedDescendants");
				if ((own && own.Run() && own.Get<bool>("ReturnValue")) ||
				    (child && child.Run() && child.Get<bool>("ReturnValue"))) {
					if (focused) return;
					focused = row;
				}
			}
			if (!focused) return;
			for (auto* row : a_rows) {
				if (row == focused) continue;
				auto* hover = Field(row, "Hovered_State");
				if (!hover || !hover->GetClass()) continue;
				const auto vis = reflect::Offset(hover->GetClass(), "Visibility");
				if (vis >= 0 && *reflect::At<std::uint8_t>(hover, vis) == 0) {
					const std::uint8_t collapsed = 1;
					ui::CallFirst(hover, L"SetVisibility", &collapsed, sizeof(collapsed));
				}
			}
		}

		void DecorateRows(UE::UObject* a_filters, const std::vector<std::string>& a_active)
		{
			auto* list = InnerList(Field(a_filters, "ListView_Filters"));
			if (!list) return;
			const auto rows = DisplayedRows(list);
			ClearStaleFilterHover(rows);
			std::vector<std::string> rowTexts;
			for (auto* row : rows) {
				if (!g_entryClass && row->GetClass()) {
					g_entryClass = row->GetClass();
					pe::WatchPost(g_entryClass, &OnEntryEvent);
				}
				if (g_itemNames == 0) {
					auto* label = Field(row, "EffectText");
					if (auto* text = label ? ui::FindInTree(label, "text_block") : nullptr) rowTexts.push_back(uix::GetText(text));
				}
				DecorateRow(row);
				RestoreSelected(row, a_active);
			}
			if (g_itemNames == 0 && rowTexts.size() >= 3) {   // learn the item's name field from the rows on screen
				std::vector<std::vector<std::pair<int, std::string>>> perItem;
				for (auto* item : ListItems(list)) {
					if (perItem.size() >= 24) break;
					perItem.push_back(ItemTexts(item));
				}
				g_nameOffset = LearnTextOffsetFromRows(perItem, rowTexts);
				g_itemNames = g_nameOffset >= 0 ? 1 : -1;
				if (g_itemNames == 1) g_effectSearchDirty = true;
				if (g_itemNames < 0) logger::warn("alchemy: no effect name found in {} filter items against {} rows; one-click removal is off", perItem.size(), rowTexts.size());
			}
		}

		// ---- our widgets
		UE::UObject* MakeLabel(UE::UObject* a_canvas, const wchar_t* a_name, const float* a_ink, double a_size)
		{
			UE::UObject* slot = nullptr;
			auto*        label = ui::AddToCanvas(a_canvas, L"/Script/UMG.TextBlock", a_name, &slot);
			if (!label || !slot) return nullptr;
			const bool yes = true;
			ui::CallFirst(slot, L"SetAutoSize", &yes, sizeof(yes));
			const int z = 50;
			ui::CallFirst(slot, L"SetZOrder", &z, sizeof(z));
			uix::Ink(label, a_ink);
			uix::ApplyFont(label, a_size);
			ui::Visible(label, false);
			return label;
		}

		UE::UObject* SlotOf(UE::UObject* a_widget) { return Field(a_widget, "Slot"); }

		void Place(UE::UObject* a_label, double a_x, double a_y, double a_alignX)
		{
			auto* slot = SlotOf(a_label);
			if (!slot) return;
			ui::Vec2(slot, L"SetAlignment", a_alignX, 0.5);
			ui::Vec2(slot, L"SetPosition", a_x, a_y);
		}

		std::array<double, 2> DesiredSize(UE::UObject* a_label)
		{
			ui::Call size(a_label, L"GetDesiredSize");
			if (!size || !size.Run()) return { 0, 0 };
			return size.Get<std::array<double, 2>>("ReturnValue");
		}

		double DesiredWidth(UE::UObject* a_label) { return DesiredSize(a_label)[0]; }

		UE::UObject* MakeBack(UE::UObject* a_canvas, std::size_t a_index)
		{
			UE::UObject* slot = nullptr;
			auto*        image = ui::AddToCanvas(a_canvas, L"/Script/UMG.Image", (L"MrAlchemyChipBack" + std::to_wstring(a_index)).c_str(), &slot);
			if (!image || !slot) return nullptr;
			const int z = 49;   // under the label
			ui::CallFirst(slot, L"SetZOrder", &z, sizeof(z));
			ui::Visible(image, false);
			return image;
		}

		void StyleBack(std::size_t a_index, bool a_hovered)
		{
			if (a_index >= g_chipBacks.size() || a_index >= g_chipStyle.size()) return;
			auto*     back = reflect::Get(g_chipBacks[a_index]);
			const int want = a_hovered ? 1 : 0;
			if (!back || g_chipStyle[a_index] == want) return;
			// Clear all keeps the red rim; a filter button gets a black one (owner 2026-11-04)
			const auto& style = UiPalette().alchemy;
			const float* line = a_index == 0 ? style.buttonLine.data() : style.buttonLineDark.data();
			if (ui::RoundedBox(back, false, a_hovered ? style.buttonHover.data() : style.buttonFill.data(), line, 0.5f, 6.0f)) g_chipStyle[a_index] = want;
		}

		double MarkWidth()
		{
			static double s_width = 0;
			if (s_width <= 0) {
				for (auto& h : g_chipMarks) {
					auto* mark = reflect::Get(h);
					const double w = mark ? DesiredWidth(mark) : 0;
					if (w > 0) { s_width = w; break; }
				}
			}
			return s_width > 0 ? s_width : 8.0;
		}

		Rect PixelRect(UE::UObject* a_widget, double a_scale)
		{
			Rect r;
			if (a_widget && ui::Measure(a_widget, r.x, r.y, r.w, r.h)) {
				r = { r.x * a_scale, r.y * a_scale, r.w * a_scale, r.h * a_scale, r.w > 0 && r.h > 0 };
			}
			return r;
		}

		struct IngredientMarkerStyle
		{
			reflect::Handle image;
			reflect::Handle slot;
			int colour = -1;
			float line = 0;
			double x = 0, y = 0, diameter = 0;
		};
		std::unordered_map<UE::UObject*, IngredientMarkerStyle> g_ingredientMarkerStyles;

		void HideOwn()
		{
			g_ingredientMarkerStyles.clear();
			for (auto& h : g_chipCircles) if (auto* w = reflect::Get(h)) ui::Visible(w, false);
			for (auto& h : g_icons) if (auto* l = reflect::Get(h)) ui::Visible(l, false);
			for (auto& h : g_iconBacks) if (auto* l = reflect::Get(h)) ui::Visible(l, false);
			if (auto* l = reflect::Get(g_arrow)) ui::Visible(l, false);
			for (auto& h : g_seps) if (auto* l = reflect::Get(h)) ui::Visible(l, false);
			if (auto* l = reflect::Get(g_rule)) ui::Visible(l, false);
			for (auto& h : g_chips) {
				if (auto* l = reflect::Get(h)) ui::Visible(l, false);
			}
			for (auto& h : g_chipBacks) {
				if (auto* l = reflect::Get(h)) ui::Visible(l, false);
			}
			if (auto* l = reflect::Get(g_more)) ui::Visible(l, false);
			if (auto* l = reflect::Get(g_chipTitle)) ui::Visible(l, false);
			for (auto& h : g_chipMarks) {
				if (auto* l = reflect::Get(h)) ui::Visible(l, false);
			}
			for (const auto& h : g_tabLabels) if (auto* w = reflect::Get(h)) ui::Visible(w, false);
			for (const auto& h : g_tabLeft) if (auto* w = reflect::Get(h)) ui::Visible(w, false);
			for (const auto& h : g_tabRight) if (auto* w = reflect::Get(h)) ui::Visible(w, false);
			for (const auto& h : g_tabDashLeft) if (auto* w = reflect::Get(h)) ui::Visible(w, false);
			for (const auto& h : g_tabDashRight) if (auto* w = reflect::Get(h)) ui::Visible(w, false);
			for (const auto& group : {g_tabUpperLeft, g_tabUpperRight, g_tabLowerLeft, g_tabLowerRight})
				for (const auto& h : group) if (auto* w = reflect::Get(h)) ui::Visible(w, false);
			g_hoverTab = -1;
			g_hoverChip = -1;
			std::scoped_lock lock(g_rectLock);
			g_iconRects = {};
			g_chipRects.clear();
			g_tabRects = {};
		}

		UE::UObject* MakeTabOrnament(UE::UObject* a_canvas, UE::UObject* a_source, const wchar_t* a_name)
		{
			UE::UObject* slot = nullptr;
			auto* image = ui::AddToCanvas(a_canvas, L"/Script/UMG.Image", a_name, &slot);
			auto* source = a_source ? ui::FindInTree(a_source, "Image") : nullptr;
			if (!image || !slot || !source) return nullptr;
			const auto off = reflect::Offset(source->GetClass(), "Brush");
			const auto size = ui::SizeOf(source->GetClass(), "Brush");
			if (off < 0 || size <= 0 || size > 1024) return nullptr;
			ui::CallFirst(image, L"SetBrush", reinterpret_cast<const std::uint8_t*>(source) + off, size);
			const int z = 50;
			ui::CallFirst(slot, L"SetZOrder", &z, sizeof(z));
			return image;
		}

		void CopyIngredientsFont(UE::UObject* a_label, UE::UObject* a_title)
		{
			auto* text = a_title;
			if (text && reflect::Offset(text->GetClass(), "Font") < 0) text = ui::FindInTree(text, "text_block");
			if (!text) return;
			const auto off = reflect::Offset(text->GetClass(), "Font");
			const auto size = ui::SizeOf(text->GetClass(), "Font");
			if (off < 0 || size <= 0 || size > 512) return;
			auto* fontInfo = reinterpret_cast<UE::UStruct*>(UE::StaticFindObject<UE::UObject>(nullptr, nullptr, L"/Script/SlateCore.SlateFontInfo"));
			const auto sizeOff = fontInfo ? reflect::Offset(fontInfo, "Size") : -1;
			const auto sizeBytes = fontInfo ? ui::SizeOf(fontInfo, "Size") : -1;
			if (sizeOff < 0 || sizeOff + sizeBytes > size || (sizeBytes != 4 && sizeBytes != 8)) return;
			const auto* data = reinterpret_cast<const std::uint8_t*>(text) + off;
			std::vector<std::uint8_t> font(data, data + size);
			if (sizeBytes == 4) { const float value = 23.1f; std::memcpy(font.data() + sizeOff, &value, 4); }
			else { const double value = 23.1; std::memcpy(font.data() + sizeOff, &value, 8); }
			ui::CallFirst(a_label, L"SetFont", font.data(), font.size());
		}

		void CopyTabFont(UE::UObject* a_label, UE::UObject* a_original)
		{
			auto* prefab = Field(a_original, "focus_text");
			auto* text = prefab ? ui::FindInTree(prefab, "text_block") : nullptr;
			if (!text) return;
			const auto off = reflect::Offset(text->GetClass(), "Font");
			const auto size = ui::SizeOf(text->GetClass(), "Font");
			if (off >= 0 && size > 0 && size <= 512)
				ui::CallFirst(a_label, L"SetFont", reinterpret_cast<const std::uint8_t*>(text) + off, size);
		}

		void LayoutFilterTabs(UE::UObject* a_menu)
		{
			auto* filters = Field(a_menu, "Alchemy_Filters");
			const bool shown = filters && Flag(filters, "bIsActive");
			if (!shown) {
				for (const auto& group : {g_tabLabels, g_tabLeft, g_tabRight, g_tabDashLeft, g_tabDashRight, g_tabUpperLeft, g_tabUpperRight, g_tabLowerLeft, g_tabLowerRight})
					for (const auto& h : group) if (auto* w = reflect::Get(h)) ui::Visible(w, false);
				g_hoverTab = -1;
				std::scoped_lock lock(g_rectLock); g_tabRects = {}; return;
			}
			auto* positive = Field(filters, "PositiveEffectsButton");
			auto* negative = Field(filters, "NegativeEffectsButton");
			Box p, n;
			if (!ui::Measure(positive, p.x, p.y, p.w, p.h) || !ui::Measure(negative, n.x, n.y, n.w, n.h) || p.w <= 0 || n.w <= 0) return;
			auto* canvas = ui::TreeRootCanvas(a_menu);
			if (!canvas) return;
			if (reflect::Get(g_tabOwner) != a_menu) {
				g_tabLabels = {}; g_tabLeft = {}; g_tabRight = {}; g_tabDashLeft = {}; g_tabDashRight = {};
				g_tabUpperLeft = {}; g_tabUpperRight = {}; g_tabLowerLeft = {}; g_tabLowerRight = {};
				g_tabOwner = reflect::Hold(a_menu);
				g_hoverTab = -1;
			}
			// Read original brush assets only; no live Blueprint button instances or style calls.
			ui::Call negParent(negative, L"GetParent");
			auto* dashRow = negParent && negParent.Run() ? negParent.Get<UE::UObject*>("ReturnValue") : nullptr;
			auto* dashLeft = dashRow ? ui::FindInTree(dashRow, "PauseMenuDecoLeft_1") : nullptr;
			Box dashSize{0, 0, 24, 20};
			ui::Measure(dashLeft, dashSize.x, dashSize.y, dashSize.w, dashSize.h);
			dashSize.w *= 0.8;
			dashSize.h *= 0.8;
			Box leftHint, rightHint;
			double startX = p.x, endX = n.x + n.w;
			if (ui::Measure(ui::FindInTree(filters, "NavigateLeft_InputHint"), leftHint.x, leftHint.y, leftHint.w, leftHint.h) &&
			    ui::Measure(ui::FindInTree(filters, "NavigateRight_InputHint"), rightHint.x, rightHint.y, rightHint.w, rightHint.h) &&
			    rightHint.x > leftHint.x + leftHint.w) {
				startX = leftHint.x + leftHint.w + 12;
				endX = rightHint.x - 12;
			}
			const double totalWidth = endX - startX;
			const std::array<double, 3> widths{ totalWidth * 0.18, totalWidth * 0.41, totalWidth * 0.41 };
			double cellX = startX;
			const double scale = ui::ViewportScale();
			std::array<Rect, 3> rects{};
			constexpr const char* names[] = { "All", "Positive Effects", "Negative Effects" };
			for (int i = 0; i < 3; ++i) {
				auto* label = reflect::Get(g_tabLabels[i]);
				const auto suffix = std::to_wstring(i);
				if (!label) {
					const auto& style = UiPalette().alchemy;
					label = MakeLabel(canvas, (L"MrAlchemyTabText" + suffix).c_str(), style.titleInk.data(), style.titleFontSize);
					if (!label) continue;
					g_tabLabels[i] = reflect::Hold(label);
					uix::SetText(label, names[i]);
					CopyTabFont(label, positive);
				}
				for (int stroke = 0; stroke < 3; ++stroke) for (int side = 0; side < 2; ++side) {
					auto& handles = stroke == 0 ? (side == 0 ? g_tabLeft : g_tabRight) :
					                stroke == 1 ? (side == 0 ? g_tabUpperLeft : g_tabUpperRight) :
					                              (side == 0 ? g_tabLowerLeft : g_tabLowerRight);
					if (!reflect::Get(handles[i])) {
						const auto name = L"MrAlchemyArrow" + suffix + L"_" + std::to_wstring(stroke) + L"_" + std::to_wstring(side);
						auto* image = MakeTabOrnament(canvas, dashLeft, name.c_str());
						if (image) handles[i] = reflect::Hold(image);
					}
				}
				if (!reflect::Get(g_tabDashLeft[i])) {
					auto* image = MakeTabOrnament(canvas, dashLeft, (L"MrAlchemyTabDashLeft" + suffix).c_str());
					if (image) g_tabDashLeft[i] = reflect::Hold(image);
				}
				if (!reflect::Get(g_tabDashRight[i])) {
					auto* image = MakeTabOrnament(canvas, dashLeft, (L"MrAlchemyTabDashRight" + suffix).c_str());
					if (image) g_tabDashRight[i] = reflect::Hold(image);
				}
				const bool selected = static_cast<int>(g_filterTab) == i;
				const bool hovered = g_hoverTab.load() == i;
				const double width = widths[i];
				const double center = cellX + width / 2;
				Place(label, center, p.y + p.h / 2, 0.5);
				ui::Float(label, L"SetRenderOpacity", selected || hovered ? 1.0f : 0.5f);
				ui::Visible(label, true);
				const double halfText = DesiredWidth(label) / 2;
				for (int stroke = 0; stroke < 3; ++stroke) for (int side = 0; side < 2; ++side) {
					const auto& handles = stroke == 0 ? (side == 0 ? g_tabLeft : g_tabRight) :
					                      stroke == 1 ? (side == 0 ? g_tabUpperLeft : g_tabUpperRight) :
					                                    (side == 0 ? g_tabLowerLeft : g_tabLowerRight);
					auto* image = reflect::Get(handles[i]);
					if (!image) continue;
					const double tip = side == 0 ? center - halfText - 8 : center + halfText + 8;
					auto* slot = SlotOf(image);
					ui::Vec2(slot, L"SetPosition", tip - dashSize.w, p.y + (p.h - dashSize.h) / 2);
					ui::Vec2(slot, L"SetSize", dashSize.w, dashSize.h);
					ui::Vec2(image, L"SetRenderTransformPivot", 1.0, 0.5);
					ui::Vec2(image, L"SetRenderScale", side == 0 ? 1.0 : -1.0, 1.0);
					const float angle = (stroke == 0 ? 0.0f : stroke == 1 ? 24.0f : -24.0f) * (side == 0 ? 1.0f : -1.0f);
					ui::Float(image, L"SetRenderTransformAngle", angle);
					ui::Visible(image, selected);
				}
				for (int side = 0; side < 2; ++side) {
					auto* image = reflect::Get(side == 0 ? g_tabDashLeft[i] : g_tabDashRight[i]);
					if (!image) continue;
					auto* slot = SlotOf(image);
					const double x = side == 0 ? center - halfText - dashSize.w - 8 : center + halfText + 8;
					ui::Vec2(slot, L"SetPosition", x, p.y + (p.h - dashSize.h) / 2);
					ui::Vec2(slot, L"SetSize", dashSize.w, dashSize.h);
					ui::Vec2(image, L"SetRenderTransformPivot", 0.5, 0.5);
					ui::Vec2(image, L"SetRenderScale", side == 0 ? 1.0 : -1.0, 1.0);
					ui::Float(image, L"SetRenderOpacity", hovered ? 1.0f : 0.5f);
					ui::Visible(image, !selected);
				}
				rects[i] = { cellX * scale, p.y * scale, width * scale, p.h * scale, true };
				cellX += width;
			}
			// Original visuals stay transparent but keep layout for native list/input behavior.
			for (auto* original : {positive, negative}) {
				ui::Float(original, L"SetRenderOpacity", 0.0f);
				ui::Call getParent(original, L"GetParent");
				auto* row = getParent && getParent.Run() ? getParent.Get<UE::UObject*>("ReturnValue") : nullptr;
				for (const char* name : {"PauseMenuDecoLeft", "PauseMenuDecoRight"}) {
					const auto actual = std::string(name) + (original == negative ? "_1" : "");
					if (auto* deco = row ? ui::FindInTree(row, actual) : nullptr) ui::Float(deco, L"SetRenderOpacity", 0.0f);
				}
			}
			std::scoped_lock lock(g_rectLock);
			g_tabRects = rects;
		}

		// ---- IMP-MR-005: native-style sort header and the Weight / Value columns (owner 2026-11-04)
		UE::UObject* NativeTexture(int a_index)
		{
			if (auto* t = reflect::Get(g_tex[static_cast<std::size_t>(a_index)])) return t;
			static std::array<bool, kTexCount> s_missing{};
			if (s_missing[static_cast<std::size_t>(a_index)]) return nullptr;
			auto* t = ui::Load(kTexPaths[a_index]);
			if (!t) {
				s_missing[static_cast<std::size_t>(a_index)] = true;
				logger::warn("alchemy: game texture {} not found; that sort icon is blank", pe::Utf8(UE::FString(kTexPaths[a_index])));
				return nullptr;
			}
			g_tex[static_cast<std::size_t>(a_index)] = reflect::Hold(t);
			return t;
		}

		void SetTexture(UE::UObject* a_image, UE::UObject* a_texture)
		{
			if (!a_image || !a_texture) return;
			ui::Call set(a_image, L"SetBrushFromTexture");
			set.Set("Texture", a_texture);
			set.Set("bMatchSize", false);
			set.Run();
		}

		UE::UObject* MakeImage(UE::UObject* a_canvas, const wchar_t* a_name, UE::UObject* a_texture, int a_z, double a_size)
		{
			UE::UObject* slot = nullptr;
			auto*        image = ui::AddToCanvas(a_canvas, L"/Script/UMG.Image", a_name, &slot);
			if (!image || !slot) return nullptr;
			ui::CallFirst(slot, L"SetZOrder", &a_z, sizeof(a_z));
			ui::Vec2(slot, L"SetSize", a_size, a_size);
			ui::Vec2(slot, L"SetAlignment", 0.5, 0.5);
			SetTexture(image, a_texture);
			ui::Visible(image, false);
			return image;
		}

		void ResetHeader()
		{
			g_icons = {};
			g_iconBacks = {};
			g_arrow = {};
			g_arrowDir = -1;
			g_seps = {};
			g_rule = {};
		}

		// the list moves down by kHeaderGap (its slot's top padding, added once per list widget) to make room for the
		// icon line between the Ingredients title and the rows (owner 2026-11-04)
		void PadList(UE::UObject* a_wrapper, double a_gap, reflect::Handle& a_padded)
		{
			if (!a_wrapper || reflect::Get(a_padded) == a_wrapper) return;
			auto* slot = SlotOf(a_wrapper);
			const auto off = slot ? reflect::Offset(slot->GetClass(), "Padding") : -1;
			if (off < 0 || ui::SizeOf(slot->GetClass(), "Padding") != 16) {
				static bool s_said = false;
				if (!s_said) { s_said = true; logger::warn("alchemy: the ingredient list's slot has no padding; no room made for the sort icons"); }
				a_padded = reflect::Hold(a_wrapper);
				return;
			}
			float margin[4];   // FMargin: left, top, right, bottom
			std::memcpy(margin, reinterpret_cast<const std::uint8_t*>(slot) + off, sizeof(margin));
			margin[1] += static_cast<float>(a_gap);
			ui::CallFirst(slot, L"SetPadding", margin, sizeof(margin));
			a_padded = reflect::Hold(a_wrapper);
		}

		// the icons on their own line in that gap: lightning (Count) and A-Z at the list's left, weight and coin centred
		// over their columns (owner 2026-11-04 screenshot)
		std::array<Rect, kIcons> LayoutHeader(UE::UObject* a_canvas, UE::UObject* a_menu, double a_scale)
		{
			std::array<Rect, kIcons> rects{};
			auto*        wrapper = Field(a_menu, "IngredientsListView");
			const double px = a_scale > 0 ? 1.0 / a_scale : 1.0;   // one screen pixel in viewport units
			const double gap = kHeaderGap - kGapTrimPx * px;
			const double ruleDrop = kHeaderRuleDropPx * px;
			PadList(wrapper, gap + ruleDrop + (searchcfg::Get().alchemy ? kSearchSpace : 0));
			Box        lb, rb;
			const bool haveList = wrapper && ui::Measure(wrapper, lb.x, lb.y, lb.w, lb.h) && lb.w > 0;
			auto*      list = InnerList(wrapper);
			const auto rows = list ? DisplayedRows(list) : std::vector<UE::UObject*>{};
			const bool haveRow = !rows.empty() && ui::Measure(rows.front(), rb.x, rb.y, rb.w, rb.h) && rb.w > 0;
			const bool room = haveList;
			for (int i = 0; i < kIcons; ++i) {
				auto& ih = g_icons[static_cast<std::size_t>(i)];
				auto& bh = g_iconBacks[static_cast<std::size_t>(i)];
				if (!reflect::Get(bh)) {
					if (auto* b = MakeImage(a_canvas, (L"MrAlchemySortBack" + std::to_wstring(i)).c_str(), NativeTexture(kTexBack), 49, kIconBack)) bh = reflect::Hold(b);
				}
				if (!reflect::Get(ih)) {
					const int count = static_cast<int>(SortKey::Count);
					auto* texture = NativeTexture(i == count ? kTexCategory : i == static_cast<int>(SortKey::Colors) ? count : i);
					if (auto* m = MakeImage(a_canvas, (L"MrAlchemySortIcon" + std::to_wstring(i)).c_str(), texture, 50, kIconSize)) ih = reflect::Hold(m);
				}
			}
			if (!reflect::Get(g_arrow)) {
				if (auto* a = MakeImage(a_canvas, L"MrAlchemySortArrow", nullptr, 51, kArrowSize)) { g_arrow = reflect::Hold(a); g_arrowDir = -1; }
			}
			for (std::size_t i = 0; i < g_seps.size(); ++i) {
				if (reflect::Get(g_seps[i])) continue;
				if (auto* sep = MakeImage(a_canvas, (L"MrAlchemySortSep" + std::to_wstring(i)).c_str(), NativeTexture(kTexLine), 50, kIconSize)) {
					if (auto* slot = SlotOf(sep)) ui::Vec2(slot, L"SetSize", kSepWidth, kIconSize);
					g_seps[i] = reflect::Hold(sep);
				}
			}
			if (!reflect::Get(g_rule)) {   // the column line turned 90 degrees, so it looks like the separators
				if (auto* rule = MakeImage(a_canvas, L"MrAlchemySortRule", NativeTexture(kTexLine), 50, kIconSize)) {
					ui::Float(rule, L"SetRenderTransformAngle", 90.0f);
					g_rule = reflect::Hold(rule);
				}
			}
			const double rowRight = haveRow ? rb.x + rb.w : lb.x + lb.w;
			const auto xs = HeaderX(lb.x + kLeftShiftPx * px, rowRight, kFirstIcon, kIconStep, kWeightCx, kValueCx);
			const double originalListTop = lb.y - ruleDrop;
			const double cy = originalListTop - gap * 0.5 - kIconLiftPx * px;
			const int    active = static_cast<int>(g_sortState.key);
			const int    focus = g_padFocus.load(), hover = g_hoverIcon.load();
			for (int i = 0; i < kIcons; ++i) {
				auto*        icon = reflect::Get(g_icons[static_cast<std::size_t>(i)]);
				auto*        back = reflect::Get(g_iconBacks[static_cast<std::size_t>(i)]);
				const double cx = xs[static_cast<std::size_t>(i)] +
					(i == static_cast<int>(SortKey::Weight) ? kWeightCx - WeightColumnInset(kWeightCx, kValueCx, kIconStep) : 0.0);
				if (icon) {
					if (auto* slot = SlotOf(icon)) ui::Vec2(slot, L"SetPosition", cx, cy);
					ui::Visible(icon, room);
					if (room) rects[static_cast<std::size_t>(i)] = PixelRect(icon, a_scale);
				}
				if (back) {
					if (auto* slot = SlotOf(back)) ui::Vec2(slot, L"SetPosition", cx, cy);
					const bool   lit = i == active || i == focus || i == hover;
					const auto&  style = UiPalette().alchemy;
					const float* tint = i == active ? style.activeSort.data() : style.focusTint.data();
					if (lit) ui::Colour(back, tint[0], tint[1], tint[2], tint[3]);
					ui::Visible(back, room && lit);
				}
			}
			// Match each divider to the midpoint between its two icons.
			const double sepX[] = { (xs[0] + xs[1]) * 0.5, (xs[2] + xs[3]) * 0.5, (xs[4] + xs[2]) * 0.5 };
			for (std::size_t i = 0; i < g_seps.size(); ++i) {
				auto* sep = reflect::Get(g_seps[i]);
				if (!sep) continue;
				if (auto* slot = SlotOf(sep)) ui::Vec2(slot, L"SetPosition", sepX[i], cy);
				ui::Visible(sep, room);
			}
			if (auto* rule = reflect::Get(g_rule)) {   // between the icons' bottom and the first row, list left to row right
				const double length = (std::max)(0.0, rowRight - lb.x);
				if (auto* slot = SlotOf(rule)) {
					ui::Vec2(slot, L"SetSize", kSepWidth, length);   // before the turn: width x length
					ui::Vec2(slot, L"SetPosition", (lb.x + rowRight) * 0.5, (cy + kIconSize * 0.5 + originalListTop) * 0.5 + ruleDrop);
				}
				ui::Visible(rule, room && length > 0);
			}
			if (auto* arrow = reflect::Get(g_arrow)) {
				const int dir = g_sortState.descending ? 1 : 0;   // up = A-Z / lowest first, down = Z-A / highest first
				if (dir != g_arrowDir) {
					SetTexture(arrow, NativeTexture(dir ? kTexDown : kTexUp));
					g_arrowDir = dir;
				}
				const double ax = xs[static_cast<std::size_t>(active)] +
					(active == static_cast<int>(SortKey::Weight) ? kWeightCx - WeightColumnInset(kWeightCx, kValueCx, kIconStep) : 0.0);
				if (auto* slot = SlotOf(arrow)) ui::Vec2(slot, L"SetPosition", ax + (kIconBack - kArrowSize) * 0.5, cy + (kIconBack - kArrowSize) * 0.5);
				ui::Visible(arrow, room);
			}
			return rects;
		}

		UE::UObject* RowNameText(UE::UObject* a_row)
		{
			auto* name = Field(a_row, "EntryName");
			if (!name) return nullptr;
			if (reflect::Offset(name->GetClass(), "Font") >= 0) return name;
			return ui::FindInTree(name, "text_block");
		}

		// a column label in the row's own canvas, anchored to its right edge, with the name text's font and colour
		UE::UObject* ColumnLabel(UE::UObject* a_row, UE::UObject* a_text, const wchar_t* a_name, double a_fromRight)
		{
			auto* canvas = ui::FindInTree(a_row, "CanvasPanel");
			if (!canvas) return nullptr;
			UE::UObject* slot = nullptr;
			auto*        label = ui::AddToCanvas(canvas, L"/Script/UMG.TextBlock", a_name, &slot);
			if (!label || !slot) return nullptr;
			const bool yes = true;
			ui::CallFirst(slot, L"SetAutoSize", &yes, sizeof(yes));
			ui::Anchors(slot, 1.0, 0.5, 1.0, 0.5);
			ui::Vec2(slot, L"SetAlignment", 0.5, 0.5);
			ui::Vec2(slot, L"SetPosition", -a_fromRight, g_columnDy);
			if (g_dyKnown) g_dyDone.insert(label);
			const int z = 5;
			ui::CallFirst(slot, L"SetZOrder", &z, sizeof(z));
			for (const char* prop : { "Font", "ColorAndOpacity" }) {
				const auto off = reflect::Offset(a_text->GetClass(), prop);
				const auto size = ui::SizeOf(a_text->GetClass(), prop);
				if (off >= 0 && size > 0 && size <= 512)
					ui::CallFirst(label, prop == std::string_view("Font") ? L"SetFont" : L"SetColorAndOpacity", reinterpret_cast<const std::uint8_t*>(a_text) + off, static_cast<std::size_t>(size));
			}
			return label;
		}

		// IMP-MR-011: named widgets belong to the row/menu widget tree, not the canvas itself.
		UE::UObject* EffectCircle(UE::UObject* a_canvas, UE::UObject* a_owner, const wchar_t* a_name)
		{
			const std::wstring wide(a_name);
			std::string name;
			for (const wchar_t c : wide) name.push_back(static_cast<char>(c)); // stable names are ASCII
			auto* image = ui::FindInTree(a_owner, name.c_str());
			if (image && !ui::IsWithin(image, a_owner)) return nullptr;
			if (!image) {
				UE::UObject* slot = nullptr;
				image = ui::AddToCanvas(a_canvas, L"/Script/UMG.Image", a_name, &slot);
				if (!image || !slot) return nullptr;
				const int z = 51;
				ui::CallFirst(slot, L"SetZOrder", &z, sizeof(z));
			}
			return image;
		}

		void DecorateIngredientMarkers(UE::UObject* a_row, const std::string& a_name)
		{
			auto* canvas = ui::FindInTree(a_row, "CanvasPanel");
			if (!canvas) return;
			const auto it = g_markerEffects.find(MatchKey(a_name));
			const auto markers = it == g_markerEffects.end() ? std::vector<int>{} : IngredientMarkers(it->second, g_markerNames);
			const auto outline = MarkerOutline();
			const float line = static_cast<float>(1.0 / (std::max)(0.01, static_cast<double>(ui::ViewportScale())));
			for (int i = 0; i < kIngredientMarkerLimit; ++i) {
				const auto wide = L"MrAlchemyEffect" + std::to_wstring(i);
				std::string name;
				for (const wchar_t c : wide) name.push_back(static_cast<char>(c));
				auto* image = ui::FindInTree(a_row, name.c_str());
				if (image && !ui::IsWithin(image, a_row)) continue;
				const bool show = i < static_cast<int>(markers.size());
				if (!image && show) image = EffectCircle(canvas, a_row, wide.c_str());
				if (!image) continue;
				auto* slot = SlotOf(image);
				const int colour = show ? markers[i] : -1;
				const int count = static_cast<int>(markers.size());
				// Keep the first selected effect nearest Weight.
				const double x = show ? IngredientMarkerX(count - 1 - i, count, kWeightCx) : 0;
				const auto cached = g_ingredientMarkerStyles.find(image);
				if (cached != g_ingredientMarkerStyles.end()) {
					const auto& style = cached->second;
					if (reflect::Get(style.image) == image && style.colour == colour &&
					    (!show || (slot && reflect::Get(style.slot) == slot && style.line == line &&
					               style.x == x && style.y == g_columnDy && style.diameter == g_markerDiameter))) continue;
					g_ingredientMarkerStyles.erase(cached);
				}
				ui::Visible(image, show);
				if (!show) {
					g_ingredientMarkerStyles[image] = { reflect::Hold(image), {}, colour };
					continue;
				}
				const auto fill = MarkerColor(colour);
				const bool brushed = ui::RoundedBox(image, true, fill.data(), outline.data(), line);
				if (slot) {
					ui::Anchors(slot, 1.0, 0.5, 1.0, 0.5);
					ui::Vec2(slot, L"SetAlignment", 0, 0.5);
					ui::Vec2(slot, L"SetSize", g_markerDiameter, g_markerDiameter);
					ui::Vec2(slot, L"SetPosition", x, g_columnDy);
					if (brushed) {
						g_ingredientMarkerStyles[image] = {
							reflect::Hold(image), reflect::Hold(slot), colour, line, x, g_columnDy, g_markerDiameter
						};
					}
				}
			}
		}

		void DecorateIngredientRow(UE::UObject* a_row)
		{
			auto* text = a_row ? RowNameText(a_row) : nullptr;
			if (!text) return;
			const auto shown = uix::GetText(text);
			DecorateIngredientMarkers(a_row, shown); // empty/recycled rows must also hide old markers
			if (shown.empty()) return;
			std::pair<float, int> stats{ -1.0f, -1 };
			if (auto it = g_formData.find(shown); it != g_formData.end()) stats = it->second;
			else if (auto m = g_formMatch.find(MatchKey(shown)); m != g_formMatch.end()) stats = m->second;
			for (int c = 0; c < 2; ++c) {
				auto* label = ui::FindInTree(a_row, c ? "MrAlchemyValue" : "MrAlchemyWeight");
				const double inset = c ? kValueCx : WeightColumnInset(kWeightCx, kValueCx, kIconStep);
				if (!label) label = ColumnLabel(a_row, text, c ? L"MrAlchemyValue" : L"MrAlchemyWeight", inset);
				if (!label) continue;
				const auto want = c ? FormatValue(stats.second) : FormatWeight(stats.first);
				if (uix::GetText(label) != want) uix::SetText(label, want);
				if (!g_dyKnown && c == 0) {   // the numbers sat at the row's top (owner 2026-11-04): centre them on the name
					Box tb, lb;
					if (ui::Measure(text, tb.x, tb.y, tb.w, tb.h) && tb.h > 0 && ui::Measure(label, lb.x, lb.y, lb.w, lb.h) && lb.h > 0) {
						g_columnDy = ColumnDy(tb.y, tb.h, lb.y, lb.h);
						g_dyKnown = true;
					}
				}
				if (g_dyKnown && g_dyDone.insert(label).second) {
					if (auto* slot = SlotOf(label)) ui::Vec2(slot, L"SetPosition", -inset, g_columnDy);
				}
			}
		}

		void OnIngredientItemBound(UE::UObject* a_row, UE::UFunction* a_fn, void*)
		{
			if (!g_open || pe::FunctionName(a_fn) != "OnListItemObjectSet" || !reflect::IsLive(a_row)) return;
			DecorateIngredientRow(a_row);
			ui::Call prepass(a_row, L"ForceLayoutPrepass");
			if (prepass) prepass.Run();
		}

		// Periodic refresh for existing rows. Rebinding also applies cosmetics before paint.
		void DecorateIngredientRows(UE::UObject* a_menu)
		{
			auto* list = InnerList(Field(a_menu, "IngredientsListView"));
			if (!list) return;
			for (auto* row : DisplayedRows(list)) {
				if (auto* cls = row->GetClass(); cls && cls != g_ingredientRowClass &&
				    pe::WatchPost(cls, &OnIngredientItemBound)) g_ingredientRowClass = cls;
				DecorateIngredientRow(row);
			}
		}

		void Layout(UE::UObject* a_menu, bool a_readNames)   // a_readNames false: before the list is built (RMI-021)
		{
			auto* canvas = ui::TreeRootCanvas(a_menu);
			auto* applied = Field(a_menu, "AppliedFilters_Text");
			auto* filtersButton = Field(a_menu, "Filters_Button");
			auto* listWidget = Field(a_menu, "IngredientsListView");
			g_framePlaced = false;
			if (!canvas) return;
			auto* title = reflect::Get(g_titleBox);
			if (!title) {
				title = ui::FindInTree(a_menu, "Alchemy_IngredientsListTitle");
				if (title) g_titleBox = reflect::Hold(title);
			}
			Box        t, f, lb;
			const bool haveTitle = title && ui::Measure(title, t.x, t.y, t.w, t.h) && t.w > 0;
			const bool haveFilters = filtersButton && ui::Measure(filtersButton, f.x, f.y, f.w, f.h) && f.w > 0;
			const bool haveList = listWidget && ui::Measure(listWidget, lb.x, lb.y, lb.w, lb.h);
			if (haveTitle) t.w = TextWidth(t.w, DesiredWidth(title));
			g_frameMoving = haveTitle && t.x != g_frameX;
			if (haveTitle) g_frameX = t.x;
			constexpr double kGap = 14;
			const auto  row = PlaceTitleRow(t, f, haveList ? lb.y : 0, kGap);
			static bool s_said = false;
			if (!s_said && (!haveTitle || !haveFilters || row.sortMaxWidth <= 0)) {
				s_said = true;
				logger::warn("alchemy: title row not laid out (title {} {:.0f},{:.0f} {:.0f}x{:.0f}; Filters {} {:.0f},{:.0f}; list {} y {:.0f}; sort room {:.0f})",
					haveTitle, t.x, t.y, t.w, t.h, haveFilters, f.x, f.y, haveList, lb.y, row.sortMaxWidth);
			}
			if (!haveTitle || !haveFilters) {
				HideOwn();
				return;
			}
			const double scale = ui::ViewportScale();

			// sort header: between the "Ingredients" title and the Filters button
			auto* first = reflect::Get(g_icons[0]);
			if (!first || !ui::IsWithin(first, a_menu)) {
				ResetHeader();
				g_chips.clear();   // a new menu widget: its labels are new too
				g_chipBacks.clear();
				g_chipStyle.clear();
				g_chipText.clear();
				g_chipMarks.clear();
				g_more = {};
				g_chipTitle = {};
			}
			const auto iconRects = LayoutHeader(canvas, a_menu, scale);

			// the active-filter buttons: in the space the taller parchment adds at its top (the baked sizes make
			// Alchemy_IngredientsListBG reach the screen top, owner 2026-11-11); without them the parchment is stock and the
			// buttons get their own line under the title row
			double chipsY = row.chipsY;
			if (darkpack::LayoutBaked()) {
				Box pb;
				if (auto* panel = Field(a_menu, "Alchemy_IngredientsListBG"); panel && ui::Measure(panel, pb.x, pb.y, pb.w, pb.h) && pb.w > 0)
					chipsY = ChipsInParchment(pb);
			}

			// active filters
			const auto names = a_readNames ? SelectedNames(a_menu) : std::vector<std::string>{};
			const auto previousMarkers = std::exchange(g_markerNames, {});
			for (auto& h : g_chipCircles) if (auto* w = reflect::Get(h)) ui::Visible(w, false);
			std::vector<std::string> texts, meaning;
			if (!names.empty()) {   // Clear all leftmost (owner 2026-11-04), then one entry per filter
				texts.push_back("Clear all");
				meaning.push_back("");
			}
			for (const auto& n : names) {
				texts.push_back(ChipLabel(n));   // the red x mark (U+00D7) is its own label
				meaning.push_back(n);
			}
			if (applied) ui::Visible(applied, names.empty());
			while (g_chips.size() < texts.size()) {
				auto* back = MakeBack(canvas, g_chips.size());
				const auto& style = UiPalette().alchemy;
				auto* chip = MakeLabel(canvas, (L"MrAlchemyChip" + std::to_wstring(g_chips.size())).c_str(), g_chips.empty() ? style.chipInk.data() : style.sortInk.data(), style.chipFontSize);
				auto* mark = MakeLabel(canvas, (L"MrAlchemyChipX" + std::to_wstring(g_chips.size())).c_str(), style.chipInk.data(), style.chipFontSize);
				if (!chip || !back || !mark) break;
				uix::SetText(mark, "\xC3\x97");
				g_chipMarks.push_back(reflect::Hold(mark));
				g_chips.push_back(reflect::Hold(chip));
				g_chipBacks.push_back(reflect::Hold(back));
				g_chipStyle.push_back(-1);
				g_chipText.emplace_back();
			}
			auto* more = reflect::Get(g_more);
			if (!more) {
				const auto& style = UiPalette().alchemy;
				more = MakeLabel(canvas, L"MrAlchemyChipMore", style.chipInk.data(), style.chipFontSize);
				if (more) g_more = reflect::Hold(more);
			}
			std::vector<Rect> rects(texts.size());   // an empty rect is not clickable
			std::vector<bool> shown(g_chips.size(), false);
			bool              moreShown = false;
			if (!texts.empty() && g_chips.size() >= texts.size()) {
				// each chip keeps its own text; a width is used once the label has drawn that text (else an estimate)
				std::vector<double>                 widths(texts.size());
				std::vector<std::array<double, 2>> sizes(texts.size());
				for (std::size_t i = 0; i < texts.size(); ++i) {
					auto* chip = reflect::Get(g_chips[i]);
					if (!chip) continue;
					if (g_chipText[i] != texts[i]) {
						uix::SetText(chip, texts[i]);
						g_chipText[i] = texts[i];
					} else {
						sizes[i] = DesiredSize(chip);
						if (sizes[i][0] > 0) g_textWidth[texts[i]] = sizes[i][0];
					}
					const auto known = g_textWidth.find(texts[i]);
					const double w = known != g_textWidth.end() ? known->second : 10.0 * static_cast<double>(texts[i].size());
					if (sizes[i][1] <= 0) sizes[i] = { w, 22.0 };
					widths[i] = w + kButtonPadX * 2 + (i > 0 ? MarkWidth() + kMarkGap : 0.0) + (i > 0 && i <= kMarkerLimit ? g_markerDiameter + kMarkerGap : 0);
				}
				const std::string moreText = "+" + std::to_string(texts.size());   // widest marker text it can need
				if (more) {
					const auto it = g_textWidth.find(moreText);
					if (it == g_textWidth.end()) {
						uix::SetText(more, moreText);
						const double mw = DesiredWidth(more);
						if (mw > 0) g_textWidth[moreText] = mw;
					}
				}
				const auto   mw = g_textWidth.count(moreText) ? g_textWidth[moreText] : 30.0;

				double       lineH = 0;
				for (const auto& sz : sizes) lineH = (std::max)(lineH, sz[1] + kButtonPadY * 2);
				const double step = lineH + kRowGap;
				const int rows = chipsY + 2 * step + lineH * 0.5 + kRowGap <= t.y ? kChipRows : 2;
				const auto flow = FlowButtons(widths, row.chipsRight - row.chipsX, kGap, mw, rows);
				const double x0 = row.chipsX + kButtonPadX;
				const int    hovered = g_hoverChip.load(), padFocus = g_chipFocus.load();
				for (std::size_t i = 0; i < flow.row.size(); ++i) {
					auto* chip = reflect::Get(g_chips[i]);
					auto* back = reflect::Get(g_chipBacks[i]);
					if (!chip || !back) break;
					const double x = x0 + flow.x[i];
					const double y = chipsY + step * flow.row[i];
					auto* mark = i < g_chipMarks.size() ? reflect::Get(g_chipMarks[i]) : nullptr;
					if (i > 0 && mark) {
						Place(mark, x, y, 0.0);
						Place(chip, x + MarkWidth() + kMarkGap, y, 0.0);
					} else {
						Place(chip, x, y, 0.0);
					}
					if (mark) ui::Visible(mark, i > 0);
					if (i > 0 && i <= kMarkerLimit) {
						auto* circle = EffectCircle(canvas, a_menu, (L"MrAlchemyChipColor" + std::to_wstring(i - 1)).c_str());
						if (circle) {
							g_chipCircles[i - 1] = reflect::Hold(circle);
							const auto fill = MarkerColor(static_cast<int>(i - 1)), outline = MarkerOutline();
							ui::RoundedBox(circle, true, fill.data(), outline.data(), static_cast<float>(1.0 / (std::max)(0.01, scale)));
							if (auto* slot = SlotOf(circle)) {
								ui::Vec2(slot, L"SetAlignment", 0, 0.5);
								ui::Vec2(slot, L"SetPosition", x + widths[i] - kButtonPadX * 2 - g_markerDiameter, y);
								ui::Vec2(slot, L"SetSize", g_markerDiameter, g_markerDiameter);
							}
							ui::Visible(circle, true);
						}
						g_markerNames.push_back(names[i - 1]);
					}
					const auto box = ButtonBox(x, y, widths[i] - kButtonPadX * 2, sizes[i][1], kButtonPadX, kButtonPadY);
					if (auto* slot = SlotOf(back)) {
						ui::Vec2(slot, L"SetPosition", box.x, box.y);
						ui::Vec2(slot, L"SetSize", box.w, box.h);
					}
					StyleBack(i, static_cast<int>(i) == hovered || static_cast<int>(i) == padFocus);
					ui::Visible(back, true);
					shown[i] = true;
					rects[i] = PixelRect(back, scale);
				}
				if (flow.more && more) {   // "+N": the filters that did not fit (not a button)
					uix::SetText(more, "+" + std::to_string(texts.size() - flow.row.size()));
					Place(more, x0 + flow.moreX - kButtonPadX + kGap * 0.5, chipsY + step * flow.moreRow, 0.0);
					moreShown = true;
				}
			}
			if (g_sortState.key == SortKey::Colors && g_markerNames != previousMarkers) g_dirtySort = true;
			if (more) ui::Visible(more, moreShown);
			auto* chipTitle = reflect::Get(g_chipTitle);
			if (!chipTitle) {
				const auto& titleInk = UiPalette().alchemy.titleInk;
				const std::array<float, 4> ink{ titleInk[0] * 0.8f, titleInk[1] * 0.8f, titleInk[2] * 0.8f, 1.0f };
				chipTitle = MakeLabel(canvas, L"MrAlchemyChipTitle", ink.data(), 23.1);
				if (chipTitle) {
					uix::SetText(chipTitle, "Selected Filters");
					CopyIngredientsFont(chipTitle, title);
					g_chipTitle = reflect::Hold(chipTitle);
				}
			}
			if (chipTitle) {
				Place(chipTitle, row.chipsX, chipsY - kTitleAbove, 0.0);
				ui::Visible(chipTitle, !texts.empty());
			}
			for (std::size_t k = 0; k < g_chips.size(); ++k) {
				if (auto* chip = reflect::Get(g_chips[k])) ui::Visible(chip, shown[k]);
				if (!shown[k] && k < g_chipMarks.size()) {
					if (auto* mark = reflect::Get(g_chipMarks[k])) ui::Visible(mark, false);
				}
				if (!shown[k]) {
					if (auto* back = k < g_chipBacks.size() ? reflect::Get(g_chipBacks[k]) : nullptr) ui::Visible(back, false);
				}
			}
			g_chipNames = meaning;
			int shownCount = 0;
			for (std::size_t k = 0; k < shown.size(); ++k) shownCount += shown[k] ? 1 : 0;
			g_chipCount = shownCount;
			if (const int focus = g_chipFocus.load(); focus >= 0) {   // a removed filter shrank the line
				const int clamped = ClampFocus(focus, shownCount);
				if (clamped != focus) { g_chipFocus = clamped; g_hoverNow = true; }
			}
			std::scoped_lock lock(g_rectLock);
			g_iconRects = iconRects;
			g_chipRects = rects;
			g_framePlaced = true;
		}

		// ---- controller hints (owner 2026-11-04; KB game-internals "Alchemy footer (bottom hint bar)")
		bool PadInput()   // the game's own idea of the input device: ECommonInputType 1 = Gamepad
		{
			auto* sub = reflect::Get(g_inputSubsystem);
			if (!sub) {
				static ULONGLONG s_next = 0;
				if (GetTickCount64() < s_next) return false;
				s_next = GetTickCount64() + 2000;   // an object scan: at most every 2 s until found
				auto* cls = ui::Class(L"/Script/CommonInput.CommonInputSubsystem");
				for (auto* o : cls ? reflect::Instances(cls) : std::vector<UE::UObject*>{}) {
					if (!reflect::IsLive(o) || pe::Utf8(o->GetFName().ToString()).rfind("Default__", 0) == 0) continue;
					sub = o;
					g_inputSubsystem = reflect::Hold(o);
					break;
				}
				if (!sub) return false;
			}
			ui::Call type(sub, L"GetCurrentInputType");
			return type && type.Run() && type.Get<std::uint8_t>("ReturnValue") == 1;
		}

		std::vector<UE::UObject*> PanelChildren(UE::UObject* a_panel)
		{
			std::vector<UE::UObject*> out;
			ui::Call count(a_panel, L"GetChildrenCount");
			const int n = count && count.Run() ? count.Get<std::int32_t>("ReturnValue") : 0;
			for (int i = 0; i < n && i < 32; ++i) {
				ui::Call at(a_panel, L"GetChildAt");
				if (!at) break;
				at.Set("Index", i);
				if (at.Run()) if (auto* w = at.Get<UE::UObject*>("ReturnValue")) out.push_back(w);
			}
			return out;
		}

		std::string ObjName(UE::UObject* a_o) { return a_o ? pe::Utf8(a_o->GetFName().ToString()) : std::string(); }

		void CopyLook(UE::UObject* a_to, UE::UObject* a_from)   // a text's font and colour
		{
			for (const char* prop : { "Font", "ColorAndOpacity" }) {
				const auto off = reflect::Offset(a_from->GetClass(), prop);
				const auto size = ui::SizeOf(a_from->GetClass(), prop);
				if (off >= 0 && size > 0 && size <= 512)
					ui::CallFirst(a_to, prop == std::string_view("Font") ? L"SetFont" : L"SetColorAndOpacity", reinterpret_cast<const std::uint8_t*>(a_from) + off, static_cast<std::size_t>(size));
			}
		}

		UE::UObject* NewWidget(UE::UObject* a_outer, const wchar_t* a_class, const std::wstring& a_name)
		{
			auto* cls = ui::Class(a_class);
			return cls && a_outer ? UE::NewObject<UE::UObject>(a_outer, cls, UE::FName(a_name.c_str())) : nullptr;
		}

		UE::UObject* AddTo(UE::UObject* a_panel, const wchar_t* a_fn, UE::UObject* a_child)
		{
			ui::Call add(a_panel, a_fn);
			if (!add) return nullptr;
			add.Set("Content", a_child);
			add.Run();
			return add.Get<UE::UObject*>("ReturnValue");
		}

		void FillSlot(UE::UObject* a_slot)   // overlay slots start top-left at the brush's own size
		{
			if (!a_slot) return;
			const std::uint8_t fill = 0;   // HAlign_Fill / VAlign_Fill
			ui::CallFirst(a_slot, L"SetHorizontalAlignment", &fill, sizeof(fill));
			ui::CallFirst(a_slot, L"SetVerticalAlignment", &fill, sizeof(fill));
		}


		double HoldTime(UE::UObject* a_entry)   // VAltarBoundActionButton.HoldTreshold (sic); 0 for a press entry
		{
			const auto off = a_entry ? reflect::Offset(a_entry->GetClass(), "HoldTreshold") : -1;
			const auto size = a_entry ? ui::SizeOf(a_entry->GetClass(), "HoldTreshold") : -1;
			if (off < 0) return 0;
			const auto* p = reinterpret_cast<const std::uint8_t*>(a_entry) + off;
			return size == 8 ? *reinterpret_cast<const double*>(p) : size == 4 ? *reinterpret_cast<const float*>(p) : 0.0;
		}

		double GlyphSide(UE::UObject* a_entry)   // the native glyph's height, else kHintGlyph
		{
			auto* glyph = Field(a_entry, "InputActionWidget");
			ui::Call size(glyph, L"GetDesiredSize");
			if (glyph && size && size.Run()) {
				const auto d = size.Get<std::array<double, 2>>("ReturnValue");
				if (d[1] > 4 && d[1] < 120) return d[1];
			}
			return kHintGlyph;
		}

		UE::UObject* MakeHint(UE::UObject* a_box, int a_index, UE::UObject* a_native, UE::UObject* a_holdEntry, UE::UObject* a_pressEntry)
		{
			auto* outer = a_box->GetOuter();
			const auto id = std::to_wstring(a_index);
			auto* row = NewWidget(outer, L"/Script/UMG.HorizontalBox", L"MrAlchemyHint" + id);
			auto* sizer = NewWidget(outer, L"/Script/UMG.SizeBox", L"MrAlchemyHintSize" + id);
			auto* stack = NewWidget(outer, L"/Script/UMG.Overlay", L"MrAlchemyHintStack" + id);
			auto* glyph = NewWidget(outer, L"/Script/UMG.Image", L"MrAlchemyHintGlyph" + id);
			auto* label = NewWidget(outer, L"/Script/UMG.TextBlock", L"MrAlchemyHintText" + id);
			if (!row || !sizer || !stack || !glyph || !label) return nullptr;
			const auto&  hint = kHints[a_index];
			const double press = GlyphSide(a_pressEntry ? a_pressEntry : a_native);   // the glyph itself
			const double side = hint.hold && a_holdEntry ? (std::max)(press, GlyphSide(a_holdEntry)) : press;   // with the ring
			ui::Float(sizer, L"SetWidthOverride", static_cast<float>(side));
			ui::Float(sizer, L"SetHeightOverride", static_cast<float>(side));
			if (hint.hold) {   // the native hold ring (progress brush) behind the glyph - only when it has a picture
				auto* ringSource = Field(a_holdEntry, "InputActionWidget");
				const auto off = ringSource ? reflect::Offset(ringSource->GetClass(), "ProgressMaterialBrush") : -1;
				const auto size = ringSource ? ui::SizeOf(ringSource->GetClass(), "ProgressMaterialBrush") : -1;
				if (off >= 0 && size > 0 && size <= 1024) {
					if (auto* ring = NewWidget(outer, L"/Script/UMG.Image", L"MrAlchemyHintRing" + id)) {
						ui::CallFirst(ring, L"SetBrush", reinterpret_cast<const std::uint8_t*>(ringSource) + off, static_cast<std::size_t>(size));
						if (ui::BrushResource(ring)) FillSlot(AddTo(stack, L"AddChildToOverlay", ring));   // the rim brush drew a white box
					}
				}
			}
			SetTexture(glyph, NativeTexture(hint.glyph));
			auto* inner = NewWidget(outer, L"/Script/UMG.SizeBox", L"MrAlchemyHintInner" + id);   // the glyph at the press size, centred
			if (inner) {
				ui::Float(inner, L"SetWidthOverride", static_cast<float>(press));
				ui::Float(inner, L"SetHeightOverride", static_cast<float>(press));
				ui::Call innerContent(inner, L"SetContent");
				if (innerContent) { innerContent.Set("Content", glyph); innerContent.Run(); }
				if (auto* slot = AddTo(stack, L"AddChildToOverlay", inner)) {
					const std::uint8_t centre = 2;   // HAlign_Center / VAlign_Center
					ui::CallFirst(slot, L"SetHorizontalAlignment", &centre, sizeof(centre));
					ui::CallFirst(slot, L"SetVerticalAlignment", &centre, sizeof(centre));
				}
			} else {
				FillSlot(AddTo(stack, L"AddChildToOverlay", glyph));
			}
			ui::Call content(sizer, L"SetContent");
			if (content) { content.Set("Content", stack); content.Run(); }
			const std::uint8_t centre = 2;   // EVerticalAlignment::VAlign_Center
			if (auto* slot = AddTo(row, L"AddChildToHorizontalBox", sizer)) ui::CallFirst(slot, L"SetVerticalAlignment", &centre, sizeof(centre));
			if (hint.label) uix::SetText(label, hint.label);   // mirrored labels are set by LayoutFooter
			if (auto* nativeText = Field(a_native, "Text_ActionName")) CopyLook(label, nativeText);
			if (auto* slot = AddTo(row, L"AddChildToHorizontalBox", label)) {
				ui::CallFirst(slot, L"SetVerticalAlignment", &centre, sizeof(centre));
				const float pad[4] = { static_cast<float>(kHintGap), 0, 0, 0 };
				ui::CallFirst(slot, L"SetPadding", pad, sizeof(pad));
			}
			if (auto* slot = AddTo(a_box, L"AddChildToHorizontalBox", row)) {   // the native slots have no padding (screenshot)
				ui::CallFirst(slot, L"SetVerticalAlignment", &centre, sizeof(centre));
				const float pad[4] = { a_index == 0 ? 0.0f : static_cast<float>(kHintSpacing), 0, 0, 0 };
				ui::CallFirst(slot, L"SetPadding", pad, sizeof(pad));
			}
			return row;
		}

		// On a pad the own bar mirrors native Y Filters / hold Y Clear all and adds RB Sort / LB Selected Filters.
		// The footer rebuilds its entries on every device or panel change (KB): this runs from the tick and re-applies.
		// our own bar beside BA_Box in its parent, placed like it (same slot alignment and padding); created once per footer
		UE::UObject* OwnBar(UE::UObject* a_footer, UE::UObject* a_box)
		{
			if (auto* bar = ui::FindInTree(a_footer, "MrAlchemyBar")) return bar;
			ui::Call parentOf(a_box, L"GetParent");
			auto* parent = parentOf && parentOf.Run() ? parentOf.Get<UE::UObject*>("ReturnValue") : nullptr;
			auto* bar = parent ? NewWidget(a_box->GetOuter(), L"/Script/UMG.HorizontalBox", L"MrAlchemyBar") : nullptr;
			if (!bar) return nullptr;
			auto* slot = AddTo(parent, L"AddChildToOverlay", bar);
			if (!slot) slot = AddTo(parent, L"AddChildToHorizontalBox", bar);
			if (!slot) slot = AddTo(parent, L"AddChildToVerticalBox", bar);
			auto* boxSlot = SlotOf(a_box);
			if (slot && boxSlot) {
				for (const char* prop : { "HorizontalAlignment", "VerticalAlignment" }) {
					const auto off = reflect::Offset(boxSlot->GetClass(), prop);
					if (off >= 0) {
						const std::uint8_t v = *(reinterpret_cast<const std::uint8_t*>(boxSlot) + off);
						ui::CallFirst(slot, prop == std::string_view("HorizontalAlignment") ? L"SetHorizontalAlignment" : L"SetVerticalAlignment", &v, sizeof(v));
					}
				}
				const auto off = reflect::Offset(boxSlot->GetClass(), "Padding");
				if (off >= 0 && ui::SizeOf(boxSlot->GetClass(), "Padding") == 16)
					ui::CallFirst(slot, L"SetPadding", reinterpret_cast<const std::uint8_t*>(boxSlot) + off, 16);
			}
			return bar;
		}

		// On a pad with the Filters panel closed our bar (owner's order) replaces the game's: BA_Box is faded out, never
		// changed - the game keeps deciding which of its entries show on each device (forcing them visible showed the
		// keyboard-hidden ones, owner screenshot on fa3e9a32). Runs from the tick; the footer rebuilds its entries.
		void LayoutFooter(UE::UObject* a_menu, bool a_filtersOpen, bool a_pad)
		{
			auto* footer = Field(a_menu, "modern_footer");
			auto* box = footer ? ui::FindInTree(footer, "BA_Box") : nullptr;
			if (!box) return;
			const bool                show = a_pad;
			UE::UObject*              native = nullptr;
			UE::UObject*              holdEntry = nullptr;
			std::vector<UE::UObject*> press;   // the game's press entries in its order (A Add to Potion, B Back)
			for (auto* child : PanelChildren(box)) {
				if (!Field(child, "Text_ActionName")) continue;
				if (!native) native = child;
				// the hold-Y entry (game's Clear All Filters) by its hold time; the action name as a fallback
				auto*      action = Field(child, "BoundInputAction");
				const bool hold = HoldTime(child) > 0 || (action && ObjName(action).find("ClearAll") != std::string::npos);
				if (hold && !holdEntry) holdEntry = child;
				else if (!hold) press.push_back(child);
			}
			ui::Float(box, L"SetRenderOpacity", show ? 0.0f : 1.0f);
			auto* bar = show && native ? OwnBar(footer, box) : ui::FindInTree(footer, "MrAlchemyBar");
			if (!bar) return;
			ui::Visible(bar, show);
			if (!show) return;
			std::array<UE::UObject*, std::size(kHints)> own{};
			for (auto* child : PanelChildren(bar)) {
				const auto name = ObjName(child);
				if (name.rfind("MrAlchemyHint", 0) != 0) continue;
				const auto i = static_cast<std::size_t>(name.back() - '0');
				if (i < own.size()) own[i] = child;
			}
			UE::UObject* pressEntry = press.empty() ? nullptr : press.front();
			for (std::size_t i = 0; i < own.size(); ++i) {
				if (!own[i]) own[i] = MakeHint(bar, static_cast<int>(i), native, holdEntry, pressEntry);
				if (!own[i]) continue;
				const bool hintShown = (!a_filtersOpen || i == 0 || i == 2 || i == 5 || i == 6) &&
					(i != 6 || (searchcfg::Get().alchemy && kbcfg::Get().enabled));
				ui::Visible(own[i], hintShown);
				const int mirror = kHints[i].mirror;
				if (mirror < 0) continue;
				auto* source = mirror < static_cast<int>(press.size()) ? Field(press[static_cast<std::size_t>(mirror)], "Text_ActionName") : nullptr;
				auto* label = ui::FindInTree(footer, "MrAlchemyHintText" + std::to_string(i));
				if (source && label) {
					const auto want = uix::GetText(source);
					if (!want.empty() && uix::GetText(label) != want) uix::SetText(label, want);
				}
			}
		}

		bool Active(UE::UObject* a_menu) { return a_menu && Flag(a_menu, "bIsActive"); }
	}
}
