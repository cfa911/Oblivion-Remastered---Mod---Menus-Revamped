#include "engine/Ui.h"

#include "engine/PEHook.h"
#include "engine/Reflect.h"

// Walking widget trees: on-screen checks, searches by name, class collection.
namespace ui
{
	// on screen for real: the widget and every widget above it visible (not Collapsed / Hidden) and not faded out - the
	// magic menu hides its wheel panel through a parent, so the wheel's own IsVisible stayed true after it was hidden
	// (the owner, 2026-09-30: "after you hide the wheel menu, the title of it still stays")
	bool ShownOnScreen(UE::UObject* a_widget)
	{
		static auto* widgetClass = Class(L"/Script/UMG.Widget");
		static auto* treeClass = Class(L"/Script/UMG.WidgetTree");
		static auto* slotClass = Class(L"/Script/UMG.PanelSlot");
		static auto* switcherClass = Class(L"/Script/UMG.WidgetSwitcher");
		const auto slotOff = widgetClass && reflect::Ok() ? reflect::Offset(reinterpret_cast<UE::UStruct*>(widgetClass), "Slot") : -1;
		const auto parentOff = slotClass && reflect::Ok() ? reflect::Offset(reinterpret_cast<UE::UStruct*>(slotClass), "Parent") : -1;
		UE::UObject* w = a_widget;
		for (int depth = 0; w && depth < 40; ++depth) {
			Call vis(w, L"IsVisible");
			if (!vis) {
				return depth > 0;   // above the widgets: the game instance or a viewport owner - shown so far
			}
			if (!vis.Run() || !vis.Get<bool>("ReturnValue")) return false;
			if (RenderOpacity(w) < 0.05f) return false;
			UE::UObject* next = nullptr;
			auto* slot = slotOff >= 0 ? *reinterpret_cast<UE::UObject* const*>(reinterpret_cast<const std::uint8_t*>(w) + slotOff) : nullptr;
			if (slot && parentOff >= 0) {
				next = *reinterpret_cast<UE::UObject* const*>(reinterpret_cast<const std::uint8_t*>(slot) + parentOff);
			}
			// a WidgetSwitcher draws only its active child, though every child reads visible: the magic menu's wheel panel
			// (Magic_QuickKeys) shares RightPanel_Switcher with the description, and read as shown under it (probe A,
			// 2026-10-01: every parent identical with the panel hidden and shown)
			if (next && switcherClass && next->GetClass()->IsChildOf(switcherClass)) {
				Call active(next, L"GetActiveWidget");
				if (active && active.Run() && active.Get<UE::UObject*>("ReturnValue") != w) return false;
			}
			if (!next) {
				// a tree's root widget: the user widget that owns the tree (outer of its WidgetTree)
				auto* outer = w->GetOuter();
				next = outer && treeClass && outer->GetClass() == treeClass ? outer->GetOuter() : nullptr;
			}
			w = next;
		}
		return true;
	}

	bool IsWithin(UE::UObject* a_obj, UE::UObject* a_ancestor)
	{
		for (auto* o = a_obj; o; o = o->GetOuter()) {
			if (o == a_ancestor) return true;
		}
		return false;
	}
}

namespace ui
{
	namespace
	{
		UE::UObject* PropObj(UE::UObject* a_o, UE::UClass* a_declaring, std::string_view a_name)
		{
			const auto off = a_o && a_declaring && reflect::Ok() ? reflect::Offset(reinterpret_cast<UE::UStruct*>(a_declaring), a_name) : -1;
			return off >= 0 ? *reinterpret_cast<UE::UObject* const*>(reinterpret_cast<const std::uint8_t*>(a_o) + off) : nullptr;
		}

		bool IsA(UE::UObject* a_o, UE::UClass* a_class)
		{
			for (auto* c = a_o ? static_cast<UE::UStruct*>(a_o->GetClass()) : nullptr; c; c = c->superStruct) {
				if (c == reinterpret_cast<UE::UStruct*>(a_class)) return true;
			}
			return false;
		}

		UE::UObject* Search(UE::UObject* a_w, std::string_view a_name, int a_depth)
		{
			static auto* userWidget = Class(L"/Script/UMG.UserWidget");
			static auto* panelWidget = Class(L"/Script/UMG.PanelWidget");
			static auto* treeClass = Class(L"/Script/UMG.WidgetTree");
			static auto* panelSlot = Class(L"/Script/UMG.PanelSlot");
			if (!a_w || a_depth > 24) return nullptr;
			if (a_depth > 0 && pe::Utf8(a_w->GetFName().ToString()) == a_name) return a_w;
			if (IsA(a_w, userWidget)) {   // a user widget: into its own tree
				auto* tree = PropObj(a_w, userWidget, "WidgetTree");
				auto* root = tree ? PropObj(tree, treeClass, "RootWidget") : nullptr;
				if (auto* found = root ? (pe::Utf8(root->GetFName().ToString()) == a_name ? root : Search(root, a_name, a_depth + 1)) : nullptr) return found;
			}
			if (IsA(a_w, panelWidget)) {   // a panel: into each slot's content
				const auto slotsOff = reflect::Ok() ? reflect::Offset(reinterpret_cast<UE::UStruct*>(panelWidget), "Slots") : -1;
				if (slotsOff < 0) return nullptr;
				struct RawArray { UE::UObject** data; std::int32_t num; std::int32_t max; };
				const auto* arr = reinterpret_cast<const RawArray*>(reinterpret_cast<const std::uint8_t*>(a_w) + slotsOff);
				for (std::int32_t i = 0; arr->data && i < arr->num && i < 256; ++i) {
					auto* content = PropObj(arr->data[i], panelSlot, "Content");
					if (!content) continue;
					if (pe::Utf8(content->GetFName().ToString()) == a_name) return content;
					if (auto* found = Search(content, a_name, a_depth + 1)) return found;
				}
			}
			// content widgets with one child (SizeBox, Border...) are panels too; nothing else holds children
			return nullptr;
		}

		void Dump(UE::UObject* a_w, int a_depth, int& a_budget)
		{
			static auto* userWidget = Class(L"/Script/UMG.UserWidget");
			static auto* panelWidget = Class(L"/Script/UMG.PanelWidget");
			static auto* treeClass = Class(L"/Script/UMG.WidgetTree");
			static auto* panelSlot = Class(L"/Script/UMG.PanelSlot");
			if (!a_w || a_depth > 24 || a_budget <= 0) return;
			--a_budget;
			logger::info("ui tree: {}{} <{}>", std::string(static_cast<std::size_t>(a_depth) * 2, ' '), pe::Utf8(a_w->GetFName().ToString()),
				pe::Utf8(a_w->GetClass()->GetFName().ToString()));
			if (IsA(a_w, userWidget)) {
				auto* tree = PropObj(a_w, userWidget, "WidgetTree");
				if (auto* root = tree ? PropObj(tree, treeClass, "RootWidget") : nullptr) Dump(root, a_depth + 1, a_budget);
			}
			if (IsA(a_w, panelWidget)) {
				const auto slotsOff = reflect::Ok() ? reflect::Offset(reinterpret_cast<UE::UStruct*>(panelWidget), "Slots") : -1;
				if (slotsOff < 0) return;
				struct RawArray { UE::UObject** data; std::int32_t num; std::int32_t max; };
				const auto* arr = reinterpret_cast<const RawArray*>(reinterpret_cast<const std::uint8_t*>(a_w) + slotsOff);
				for (std::int32_t i = 0; arr->data && i < arr->num && i < 256; ++i) {
					if (auto* content = PropObj(arr->data[i], panelSlot, "Content")) Dump(content, a_depth + 1, a_budget);
				}
			}
		}
	}

	// A row or widget being built or destroyed while the walk reads it can hold a garbage slot pointer: an access violation
	// here (seen as a crash on opening the inventory) ends the walk with "not found" instead of ending the game.
	static UE::UObject* SearchGuarded(UE::UObject* a_w, std::string_view a_name, bool* a_faulted)
	{
		__try {
			return Search(a_w, a_name, 0);
		} __except (GetExceptionCode() == EXCEPTION_ACCESS_VIOLATION ? EXCEPTION_EXECUTE_HANDLER : EXCEPTION_CONTINUE_SEARCH) {
			*a_faulted = true;
			return nullptr;
		}
	}

	UE::UObject* FindInTree(UE::UObject* a_userWidget, std::string_view a_name)
	{
		bool faulted = false;
		auto* found = SearchGuarded(a_userWidget, a_name, &faulted);
		if (faulted) {
			static bool logged = false;
			if (!logged) {
				logged = true;
				logger::warn("ui: a widget tree could not be read (access violation) while looking for {} - skipped, not crashed", std::string(a_name));
			}
		}
		return found;
	}

	static bool DumpGuarded(UE::UObject* a_w, int a_max)
	{
		__try {
			int budget = a_max;
			Dump(a_w, 0, budget);
			return true;
		} __except (GetExceptionCode() == EXCEPTION_ACCESS_VIOLATION ? EXCEPTION_EXECUTE_HANDLER : EXCEPTION_CONTINUE_SEARCH) {
			return false;
		}
	}

	void DumpTree(UE::UObject* a_userWidget, int a_maxNodes)
	{
		if (!DumpGuarded(a_userWidget, a_maxNodes)) {
			logger::warn("ui tree: the widget tree could not be read to the end (access violation)");
		}
	}
	// the same walk, collecting the distinct classes of the widgets met
	static void CollectClasses(UE::UObject* a_w, int a_depth, int& a_budget, std::vector<UE::UClass*>* a_out)
	{
		static auto* userWidget = Class(L"/Script/UMG.UserWidget");
		static auto* panelWidget = Class(L"/Script/UMG.PanelWidget");
		static auto* treeClass = Class(L"/Script/UMG.WidgetTree");
		static auto* panelSlot = Class(L"/Script/UMG.PanelSlot");
		if (!a_w || a_depth > 24 || a_budget <= 0) return;
		--a_budget;
		auto* c = a_w->GetClass();
		if (c && std::find(a_out->begin(), a_out->end(), c) == a_out->end()) a_out->push_back(c);
		if (IsA(a_w, userWidget)) {
			auto* tree = PropObj(a_w, userWidget, "WidgetTree");
			if (auto* root = tree ? PropObj(tree, treeClass, "RootWidget") : nullptr) CollectClasses(root, a_depth + 1, a_budget, a_out);
		}
		if (IsA(a_w, panelWidget)) {
			const auto slotsOff = reflect::Ok() ? reflect::Offset(reinterpret_cast<UE::UStruct*>(panelWidget), "Slots") : -1;
			if (slotsOff < 0) return;
			struct RawArray { UE::UObject** data; std::int32_t num; std::int32_t max; };
			const auto* arr = reinterpret_cast<const RawArray*>(reinterpret_cast<const std::uint8_t*>(a_w) + slotsOff);
			for (std::int32_t i = 0; arr->data && i < arr->num && i < 256; ++i) {
				if (auto* content = PropObj(arr->data[i], panelSlot, "Content")) CollectClasses(content, a_depth + 1, a_budget, a_out);
			}
		}
	}

	static bool CollectGuarded(UE::UObject* a_w, int a_max, std::vector<UE::UClass*>* a_out)
	{
		__try {
			int budget = a_max;
			CollectClasses(a_w, 0, budget, a_out);
			return true;
		} __except (GetExceptionCode() == EXCEPTION_ACCESS_VIOLATION ? EXCEPTION_EXECUTE_HANDLER : EXCEPTION_CONTINUE_SEARCH) {
			return false;
		}
	}

	std::vector<UE::UClass*> TreeClasses(UE::UObject* a_userWidget, int a_maxNodes)
	{
		std::vector<UE::UClass*> out;
		CollectGuarded(a_userWidget, a_maxNodes, &out);
		return out;
	}
}
