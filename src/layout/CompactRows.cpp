#include "layout/CompactRows.h"

#include "core/Config.h"
#include "engine/PEHook.h"
#include "engine/Reflect.h"
#include "engine/Ui.h"
#include "layout/rules/CompactRowsTable.h"

// No ProcessEvent watcher here: pe's watcher table holds 32 entries and one per blueprint filled it (owner round 1,
// 2026-11-11: "pe: watcher table full", the Dark Magic row watch refused). Instead, once a blueprint class has loaded:
// 1. its template widget tree (UWidgetBlueprintGeneratedClass::WidgetTree, which new instances are copied from) gets the
//    compact values, so every later row, icon and panel is built compact (FuzzUI's sizes are baked the same way);
// 2. the instances already alive (the menu opening now) get them once, nested table widgets included (the icon entry inside
//    an Inventory row).
namespace compactrows
{
	namespace
	{
		struct State
		{
			UE::UClass* cls = nullptr;
			bool        reportedLive = false;
			bool        reportedTemplate = false;
		};
		std::array<State, kBlueprintCount> g_state{};

		int IndexOf(UE::UClass* a_class)
		{
			for (int i = 0; i < kBlueprintCount; ++i)
				if (a_class && g_state[i].cls == a_class) return i;
			return -1;
		}

		std::string NameOf(UE::UObject* a_obj) { return pe::Utf8(a_obj->GetFName().ToString()); }

		UE::UStruct* ScriptStruct(const wchar_t* a_path)
		{
			return reinterpret_cast<UE::UStruct*>(UE::StaticFindObject<UE::UObject>(nullptr, nullptr, a_path));
		}

		// ---- one widget tree, walked raw (fault-guarded): every widget with its slot, nested user widgets not entered ----
		struct Layout
		{
			UE::UClass*  userWidget = nullptr;
			UE::UClass*  panelWidget = nullptr;
			std::int32_t userTree = -1, treeRoot = -1, panelSlots = -1, slotContent = -1;
			bool Ok() const { return userWidget && panelWidget && userTree >= 0 && treeRoot >= 0 && panelSlots >= 0 && slotContent >= 0; }
		};
		const Layout& Offsets()
		{
			static Layout s;
			if (!s.Ok()) {
				s.userWidget = ui::Class(L"/Script/UMG.UserWidget");
				s.panelWidget = ui::Class(L"/Script/UMG.PanelWidget");
				auto* treeClass = ui::Class(L"/Script/UMG.WidgetTree");
				auto* slotClass = ui::Class(L"/Script/UMG.PanelSlot");
				const auto off = [](UE::UClass* c, const char* n) { return c ? reflect::Offset(reinterpret_cast<UE::UStruct*>(c), n) : -1; };
				s.userTree = off(s.userWidget, "WidgetTree");
				s.treeRoot = off(treeClass, "RootWidget");
				s.panelSlots = off(s.panelWidget, "Slots");
				s.slotContent = off(slotClass, "Content");
			}
			return s;
		}

		struct Node { UE::UObject* widget; UE::UObject* slot; int depth; };
		constexpr int kMaxNodes = 400, kMaxNested = 32;
		struct Walk
		{
			Node         nodes[kMaxNodes];
			int          count = 0;
			UE::UObject* nested[kMaxNested];
			int          nestedCount = 0;
		};

		UE::UObject* Ptr(UE::UObject* a_obj, std::int32_t a_offset)
		{
			return *reinterpret_cast<UE::UObject* const*>(reinterpret_cast<const std::uint8_t*>(a_obj) + a_offset);
		}

		void WalkNode(UE::UObject* a_w, UE::UObject* a_slot, int a_depth, Walk& a_out, const Layout& a_l)
		{
			if (!a_w || a_depth > 24 || a_out.count >= kMaxNodes) return;
			a_out.nodes[a_out.count++] = { a_w, a_slot, a_depth };
			auto* cls = a_w->GetClass();
			if (cls->IsChildOf(a_l.userWidget)) {   // a nested user widget: its own tree belongs to its own blueprint
				if (a_out.nestedCount < kMaxNested) a_out.nested[a_out.nestedCount++] = a_w;
				return;
			}
			if (!cls->IsChildOf(a_l.panelWidget)) return;   // SizeBox, Border and other content widgets are panels too
			struct RawArray { UE::UObject** data; std::int32_t num; std::int32_t max; };
			const auto* arr = reinterpret_cast<const RawArray*>(reinterpret_cast<const std::uint8_t*>(a_w) + a_l.panelSlots);
			for (std::int32_t i = 0; arr->data && i < arr->num && i < 256; ++i)
				if (auto* slot = arr->data[i]) WalkNode(Ptr(slot, a_l.slotContent), slot, a_depth + 1, a_out, a_l);
		}

		bool WalkTree(UE::UObject* a_tree, Walk& a_out, const Layout& a_l)
		{
			__try {
				WalkNode(Ptr(a_tree, a_l.treeRoot), nullptr, 0, a_out, a_l);
				return true;
			} __except (GetExceptionCode() == EXCEPTION_ACCESS_VIOLATION ? EXCEPTION_EXECUTE_HANDLER : EXCEPTION_CONTINUE_SEARCH) {
				return false;
			}
		}

		// ---- the fields one change edits; live widgets go through their setters, templates are written in place ----
		float* FloatField(UE::UObject* a_obj, const char* a_name)
		{
			auto* cls = reinterpret_cast<UE::UStruct*>(a_obj->GetClass());
			const auto off = reflect::Offset(cls, a_name);
			return off >= 0 && ui::SizeOf(cls, a_name) == 4 ? reflect::At<float>(a_obj, off) : nullptr;
		}

		// An FMargin (Left, Top, Right, Bottom floats): a CanvasPanelSlot's LayoutData.Offsets, any other slot's Padding.
		float* MarginField(UE::UObject* a_obj, bool* a_canvas)
		{
			static auto* canvasSlot = ui::Class(L"/Script/UMG.CanvasPanelSlot");
			auto* cls = reinterpret_cast<UE::UStruct*>(a_obj->GetClass());
			*a_canvas = canvasSlot && a_obj->GetClass()->IsChildOf(canvasSlot);
			if (*a_canvas) {
				static auto* anchorData = ScriptStruct(L"/Script/UMG.AnchorData");
				const auto layout = reflect::Offset(cls, "LayoutData");
				const auto offsets = anchorData ? reflect::Offset(anchorData, "Offsets") : -1;
				if (layout < 0 || offsets < 0 || ui::SizeOf(anchorData, "Offsets") != 16) return nullptr;
				return reflect::At<float>(a_obj, layout + offsets);
			}
			const auto off = reflect::Offset(cls, "Padding");
			return off >= 0 && ui::SizeOf(cls, "Padding") == 16 ? reflect::At<float>(a_obj, off) : nullptr;
		}

		const char* FitName(Fit a_fit)
		{
			switch (a_fit) {
			case Fit::Applied: return "applied";
			case Fit::AlreadyCompact: return "already";
			case Fit::Ambiguous: return "ambiguous";
			default: return "missing";
			}
		}

		// A SizeBox's three size pairs (RetargetPairs picks the one that still holds the stock value). FuzzUI's numbers sit in the
		// overrides or in the desired sizes (Inventory icon SizeBox_60: overrides 0,0; Stats SizeBox_1: 54 override, 64 elsewhere).
		Fit ApplyBox(UE::UObject* a_box, const Change& a_change, bool a_live, std::string& a_seen)
		{
			static constexpr const char*    kFields[3][2]{ { "WidthOverride", "HeightOverride" }, { "MinDesiredWidth", "MinDesiredHeight" },
				{ "MaxDesiredWidth", "MaxDesiredHeight" } };
			static constexpr const wchar_t* kSetters[3][2]{ { L"SetWidthOverride", L"SetHeightOverride" },
				{ L"SetMinDesiredWidth", L"SetMinDesiredHeight" }, { L"SetMaxDesiredWidth", L"SetMaxDesiredHeight" } };
			float* fields[3][2]{};
			float  values[3][2]{};
			a_seen.clear();
			for (int p = 0; p < 3; ++p)
				for (int k = 0; k < 2; ++k) {
					fields[p][k] = FloatField(a_box, kFields[p][k]);
					if (!fields[p][k]) return Fit::Missing;
					values[p][k] = *fields[p][k];
				}
			a_seen = std::format("{:g},{:g}/{:g},{:g}/{:g},{:g}", values[0][0], values[0][1], values[1][0], values[1][1], values[2][0], values[2][1]);
			const Fit fit = RetargetPairs(values, 3, a_change);
			if (fit != Fit::Applied) return fit;
			for (int p = 0; p < 3; ++p)
				for (int k = 0; k < 2; ++k) {
					if (values[p][k] == *fields[p][k]) continue;
					if (a_live) ui::Float(a_box, kSetters[p][k], values[p][k]);
					else *fields[p][k] = values[p][k];
				}
			return fit;
		}

		Fit ApplySlot(UE::UObject* a_slot, const Change& a_change, bool a_live, std::string& a_seen)
		{
			bool canvas = false;
			float* margin = MarginField(a_slot, &canvas);
			if (!margin) return Fit::Missing;
			float values[4]{ margin[0], margin[1], margin[2], margin[3] };
			a_seen = std::format("{:g},{:g},{:g},{:g}", values[0], values[1], values[2], values[3]);
			const Fit fit = Retarget(values, 4, a_change);
			if (fit != Fit::Applied) return fit;
			if (!a_live) { std::memcpy(margin, values, sizeof(values)); return fit; }
			return ui::CallFirst(a_slot, canvas ? L"SetOffsets" : L"SetPadding", values, sizeof(values)) ? fit : Fit::Missing;
		}

		// A Spacer's Size: FVector2D of doubles (UE 5.3), compared as floats.
		Fit ApplySpacer(UE::UObject* a_spacer, const Change& a_change, bool a_live, std::string& a_seen)
		{
			auto* cls = reinterpret_cast<UE::UStruct*>(a_spacer->GetClass());
			const auto off = reflect::Offset(cls, "Size");
			if (off < 0 || ui::SizeOf(cls, "Size") != 16) return Fit::Missing;
			auto* size = reflect::At<double>(a_spacer, off);
			float values[2]{ static_cast<float>(size[0]), static_cast<float>(size[1]) };
			a_seen = std::format("{:g},{:g}", values[0], values[1]);
			const Fit fit = Retarget(values, 2, a_change);
			if (fit != Fit::Applied) return fit;
			const double next[2]{ values[0] == static_cast<float>(size[0]) ? size[0] : values[0],
				values[1] == static_cast<float>(size[1]) ? size[1] : values[1] };
			if (!a_live) { size[0] = next[0]; size[1] = next[1]; return fit; }
			return ui::CallFirst(a_spacer, L"SetSize", next, sizeof(next)) ? fit : Fit::Missing;
		}

		// The font size a prefab text widget (WBP_AltarTextBlock and kin) keeps on itself and hands its inner text_block when it is
		// built: FuzzUI changed it there (inv_entry_icon_count 12 -> 11). Found by reflection: a property of the prefab's blueprint
		// classes whose name has "Font" and the size of an FSlateFontInfo (its Size inside), or a 4-byte "Font...Size" float.
		float* PrefabFont(UE::UObject* a_widget)
		{
			static auto* userWidget = ui::Class(L"/Script/UMG.UserWidget");
			static auto* textBlock = ui::Class(L"/Script/UMG.TextBlock");
			static auto* fontInfo = ScriptStruct(L"/Script/SlateCore.SlateFontInfo");
			const auto infoSize = textBlock ? ui::SizeOf(reinterpret_cast<UE::UStruct*>(textBlock), "Font") : -1;
			const auto sizeOff = fontInfo ? reflect::Offset(fontInfo, "Size") : -1;
			if (!fontInfo || ui::SizeOf(fontInfo, "Size") != 4) return nullptr;
			for (auto* s = reinterpret_cast<UE::UStruct*>(a_widget->GetClass()); s && s != reinterpret_cast<UE::UStruct*>(userWidget); s = s->superStruct) {
				for (const auto& [name, off] : reflect::Fields(s)) {
					if (name.find("Font") == std::string::npos && name.find("font") == std::string::npos) continue;
					const auto size = ui::SizeOf(s, name);
					if (size == infoSize && sizeOff >= 0) return reflect::At<float>(a_widget, off + sizeOff);
					if (size == 4 && name.find("Size") != std::string::npos) return reflect::At<float>(a_widget, off);
				}
			}
			return nullptr;
		}

		Fit ApplyTextFont(UE::UObject* a_text, const Change& a_change, bool a_live, std::string& a_seen);

		Fit Better(Fit a, Fit b)   // the more useful of two outcomes for one change
		{
			for (Fit f : { Fit::Applied, Fit::AlreadyCompact, Fit::Ambiguous })
				if (a == f || b == f) return f;
			return Fit::Missing;
		}

		Fit ApplyFont(UE::UObject* a_widget, const Change& a_change, bool a_live, std::string& a_seen)
		{
			if (reflect::Offset(a_widget->GetClass(), "Font") >= 0) return ApplyTextFont(a_widget, a_change, a_live, a_seen);
			Fit fit = Fit::Missing;
			if (float* size = PrefabFont(a_widget)) {   // templates and live: the prefab's own value, built into its text later
				float value = *size;
				a_seen = std::format("prefab {:g}", value);
				fit = Retarget(&value, 1, a_change);
				if (fit == Fit::Applied) *size = value;
			}
			if (a_live)   // a built prefab: its inner text too (the prefab only copies its font at construction)
				if (auto* text = ui::FindInTree(a_widget, "text_block")) {
					std::string seen;
					fit = Better(fit, ApplyTextFont(text, a_change, true, seen));
					a_seen += " text " + seen;
				}
			return fit;
		}

		Fit ApplyTextFont(UE::UObject* a_widget, const Change& a_change, bool a_live, std::string& a_seen)
		{
			auto* text = a_widget;
			static auto* fontInfo = ScriptStruct(L"/Script/SlateCore.SlateFontInfo");
			const auto off = reflect::Offset(text->GetClass(), "Font");
			const auto size = ui::SizeOf(text->GetClass(), "Font");
			const auto sizeOff = fontInfo ? reflect::Offset(fontInfo, "Size") : -1;
			const auto sizeBytes = fontInfo ? ui::SizeOf(fontInfo, "Size") : -1;
			if (off < 0 || size <= 0 || size > 512 || sizeOff < 0 || sizeOff + sizeBytes > size || (sizeBytes != 4 && sizeBytes != 8))
				return Fit::Missing;
			auto* data = reinterpret_cast<std::uint8_t*>(text) + off;
			std::vector<std::uint8_t> font(data, data + size);
			float value = 0.0f;
			if (sizeBytes == 4) std::memcpy(&value, font.data() + sizeOff, 4);
			else { double d = 0.0; std::memcpy(&d, font.data() + sizeOff, 8); value = static_cast<float>(d); }
			a_seen = std::format("{:g}", value);
			const Fit fit = Retarget(&value, 1, a_change);
			if (fit != Fit::Applied) return fit;
			if (sizeBytes == 4) std::memcpy(font.data() + sizeOff, &value, 4);
			else { const double d = value; std::memcpy(font.data() + sizeOff, &d, 8); }
			if (!a_live) { std::memcpy(data + sizeOff, font.data() + sizeOff, static_cast<std::size_t>(sizeBytes)); return fit; }
			return ui::CallFirst(text, L"SetFont", font.data(), font.size()) ? fit : Fit::Missing;
		}

		// Every change of blueprint a_index on one widget tree (a live instance's, or the class template's). Widgets are found by
		// name in this tree only (a nested user widget can have its own SizeBox_0); slots by the slot's own name. A nested
		// widget of another table blueprint (the icon entry inside an Inventory row) gets its own changes from its own tree.
		// Idempotent: a done widget reads AlreadyCompact and is not written again.
		void Apply(UE::UObject* a_tree, int a_index, bool a_live, int a_depth)
		{
			const auto& l = Offsets();
			if (!a_tree || !l.Ok() || a_depth > 3) return;
			auto walk = std::make_unique<Walk>();
			if (!WalkTree(a_tree, *walk, l)) return;
			const auto& bp = kBlueprints[a_index];
			// every widget (or slot) of the change's name: names repeat inside one tree (Magic has two VerticalBoxSlot_0, the
			// header row's and the effectiveness line's; layout dump 2026-11-11)
			std::vector<std::vector<UE::UObject*>> candidates(static_cast<std::size_t>(bp.changeCount));
			bool anyFound = false;
			for (int n = 0; n < walk->count; ++n) {
				const auto& node = walk->nodes[n];
				const auto name = NameOf(node.widget);
				const auto slotName = node.slot ? NameOf(node.slot) : std::string();
				for (int i = 0; i < bp.changeCount; ++i) {
					const auto& c = bp.changes[i];
					if (c.kind == Kind::Slot ? (node.slot && slotName == c.widget) : name == c.widget) {
						candidates[i].push_back(c.kind == Kind::Slot ? node.slot : node.widget);
						anyFound = true;
					}
				}
			}
			std::string report;
			bool trouble = false;
			for (int i = 0; i < bp.changeCount; ++i) {
				const auto& c = bp.changes[i];
				std::string seen = "-";
				Fit fit = Fit::Missing;
				for (auto* target : candidates[i]) {   // the first that still holds the stock value takes the change
					std::string s = "-";
					const Fit f = c.kind == Kind::Box ? ApplyBox(target, c, a_live, s) :
					              c.kind == Kind::Slot ? ApplySlot(target, c, a_live, s) :
					              c.kind == Kind::Spacer ? ApplySpacer(target, c, a_live, s) : ApplyFont(target, c, a_live, s);
					if (Better(fit, f) != fit || seen == "-") seen = s;
					fit = Better(fit, f);
					if (fit == Fit::Applied) break;
				}
				trouble = trouble || fit == Fit::Missing || fit == Fit::Ambiguous;
				report += std::format(" {} {}({})", c.widget, FitName(fit), seen);
			}
			// nested table widgets: their own tree, their own blueprint's changes
			for (int n = 0; n < walk->nestedCount; ++n) {
				auto* nested = walk->nested[n];
				const int index = IndexOf(nested->GetClass());
				if (index >= 0 && index != a_index) Apply(Ptr(nested, l.userTree), index, a_live, a_depth + 1);
			}
			// once per blueprint and kind, and only after a pass that met its widgets (a tree not built yet says nothing)
			auto& state = g_state[a_index];
			bool& reported = a_live ? state.reportedLive : state.reportedTemplate;
			if (!anyFound || reported) return;
			reported = true;
			if (trouble) logger::warn("compact rows: {} {} left partly stock:{}", a_live ? "live" : "template", bp.name, report);
		}

		// The class template: what UUserWidget::Initialize copies each new instance's tree from.
		void PatchTemplate(int a_index)
		{
			auto* cls = g_state[a_index].cls;
			auto* meta = cls ? reinterpret_cast<UE::UStruct*>(cls->GetClass()) : nullptr;
			const auto off = meta ? reflect::Offset(meta, "WidgetTree") : -1;
			auto* tree = off >= 0 ? Ptr(cls, off) : nullptr;
			if (!tree) {
				logger::warn("compact rows: {} has no template WidgetTree (offset {}): only open instances change", kBlueprints[a_index].name, off);
				return;
			}
			Apply(tree, a_index, false, 0);
		}

		// The class has just loaded: the instances built before now (the menu opening) get theirs in this one scan per class
		// and session; every later instance is copied from the patched template.
		void Watched(int a_index)
		{
			const auto& l = Offsets();
			for (auto* obj : reflect::Instances(g_state[a_index].cls))
				if (l.Ok() && reflect::Usable(obj)) Apply(Ptr(obj, l.userTree), a_index, true, 0);
		}
	}

	void Tick()
	{
		if (!mrconfig::Get().compactRows) return;
		for (int i = 0; i < kBlueprintCount; ++i) {
			if (g_state[i].cls) continue;
			auto* cls = UE::StaticFindObject<UE::UClass>(nullptr, nullptr, kBlueprints[i].classPath);
			if (!cls) continue;   // not opened yet this session
			g_state[i].cls = cls;
			PatchTemplate(i);
			Watched(i);
		}
	}
}
