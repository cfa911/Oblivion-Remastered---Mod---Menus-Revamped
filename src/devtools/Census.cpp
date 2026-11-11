// Dev-only widget census (owner 2026-11-11). See Census.h. Replaces the per-feature census probes of the Alchemy recolour.
#include "devtools/Census.h"
#include "engine/PEHook.h"
#include "engine/Reflect.h"
#include "engine/Ui.h"
#include "engine/UiText.h"

#include <ShlObj.h>
#include <atomic>
#include <chrono>
#include <filesystem>
#include <format>
#include <fstream>

namespace devtools
{
	namespace
	{
		std::atomic<bool> g_request{ false };

		struct Node
		{
			UE::UObject* widget;
			int          depth;
		};

		UE::UObject* Prop(UE::UObject* a_object, const char* a_name)
		{
			const int offset = reflect::Offset(a_object->GetClass(), a_name);
			auto**    at = offset >= 0 ? reflect::At<UE::UObject*>(a_object, offset) : nullptr;
			return at ? *at : nullptr;
		}

		int Visibility(UE::UObject* a_widget)   // ESlateVisibility: 0 visible, 1 collapsed, 2 hidden, 3/4 hit-test invisible
		{
			const int offset = reflect::Offset(a_widget->GetClass(), "Visibility");
			const auto* at = offset >= 0 ? reflect::At<std::uint8_t>(a_widget, offset) : nullptr;
			return at ? *at : -1;
		}

		// every widget under a_widget; collapsed and hidden ones are listed but not entered (what is on screen)
		void Walk(UE::UObject* a_widget, int a_depth, int& a_budget, std::vector<Node>& a_out)
		{
			static auto* userWidget = ui::Class(L"/Script/UMG.UserWidget");
			static auto* panelWidget = ui::Class(L"/Script/UMG.PanelWidget");
			if (!a_widget || a_depth > 48 || a_budget <= 0) return;
			--a_budget;
			a_out.push_back({ a_widget, a_depth });
			const int visibility = Visibility(a_widget);
			if (visibility == 1 || visibility == 2) return;
			if (userWidget && a_widget->GetClass()->IsChildOf(userWidget)) {
				auto* tree = Prop(a_widget, "WidgetTree");
				if (auto* root = tree ? Prop(tree, "RootWidget") : nullptr) Walk(root, a_depth + 1, a_budget, a_out);
			}
			if (panelWidget && a_widget->GetClass()->IsChildOf(panelWidget)) {
				struct RawArray { UE::UObject** data; std::int32_t num; std::int32_t max; };
				const int offset = reflect::Offset(reinterpret_cast<UE::UStruct*>(panelWidget), "Slots");
				const auto* slots = offset >= 0 ? reflect::At<RawArray>(a_widget, offset) : nullptr;
				for (std::int32_t i = 0; slots && slots->data && i < slots->num && i < 512; ++i)
					if (auto* slot = slots->data[i]) Walk(Prop(slot, "Content"), a_depth + 1, a_budget, a_out);
			}
			// a CommonUI activatable stack (the layout's GameMenuStack etc.) is not a panel: its menus are the shown
			// DisplayedWidget, or every entry of WidgetList when none is marked shown
			if (const int listOffset = reflect::Offset(a_widget->GetClass(), "WidgetList"); listOffset >= 0) {
				if (auto* shown = Prop(a_widget, "DisplayedWidget")) {
					Walk(shown, a_depth + 1, a_budget, a_out);
				} else {
					struct RawArray { UE::UObject** data; std::int32_t num; std::int32_t max; };
					const auto* list = reflect::At<RawArray>(a_widget, listOffset);
					for (std::int32_t i = 0; list && list->data && i < list->num && i < 32; ++i) Walk(list->data[i], a_depth + 1, a_budget, a_out);
				}
			}
		}

		bool WalkGuarded(UE::UObject* a_root, int a_depth, int a_budget, std::vector<Node>* a_out)
		{
			__try {
				Walk(a_root, a_depth, a_budget, *a_out);
				return true;
			} __except (GetExceptionCode() == EXCEPTION_ACCESS_VIOLATION ? EXCEPTION_EXECUTE_HANDLER : EXCEPTION_CONTINUE_SEARCH) {
				return false;
			}
		}

		std::string Name(UE::UObject* a_object) { return a_object ? pe::Utf8(a_object->GetFName().ToString()) : std::string("-"); }

		std::string Colour(const float* a_rgba)
		{
			return a_rgba ? std::format("{:.3f} {:.3f} {:.3f} {:.3f}", a_rgba[0], a_rgba[1], a_rgba[2], a_rgba[3]) : std::string("-");
		}

		std::string Line(UE::UObject* a_widget, int a_depth)
		{
			static auto* imageClass = ui::Class(L"/Script/UMG.Image");
			static auto* brushStruct = UE::StaticFindObject<UE::UStruct>(nullptr, nullptr, L"/Script/SlateCore.SlateBrush");
			static auto* textClass = ui::Class(L"/Script/UMG.TextLayoutWidget");
			static auto* editableClass = ui::Class(L"/Script/UMG.EditableText");
			auto*        type = a_widget->GetClass();
			const int    colourOffset = reflect::Offset(type, "ColorAndOpacity");   // FLinearColor, or FSlateColor (starts with one)
			const auto*  colour = colourOffset >= 0 ? reflect::At<float>(a_widget, colourOffset) : nullptr;
			std::string  line = std::format("{}{} [{}] vis={} color={}", std::string(static_cast<std::size_t>(a_depth) * 2, ' '),
				Name(a_widget), Name(type), Visibility(a_widget), Colour(colour));
			if (imageClass && type->IsChildOf(imageClass)) {
				const int brush = reflect::Offset(type, "Brush");
				const int tint = brushStruct ? reflect::Offset(brushStruct, "TintColor") : -1;
				line += std::format(" art={} brush={}", Name(ui::BrushResource(a_widget)),
					Colour(brush >= 0 && tint >= 0 ? reflect::At<float>(a_widget, brush + tint) : nullptr));
			}
			if ((textClass && type->IsChildOf(textClass)) || (editableClass && type->IsChildOf(editableClass))) {
				auto text = uix::GetText(a_widget);
				if (text.size() > 80) text = text.substr(0, 80) + "...";
				line += std::format(" text=\"{}\"", text);
			}
			return line;
		}

		std::filesystem::path OutputFile()
		{
			PWSTR documents = nullptr;
			std::filesystem::path folder;
			if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_Documents, 0, nullptr, &documents))) folder = documents;
			CoTaskMemFree(documents);
			folder /= L"My Games/Oblivion Remastered/OBSE/Logs";
			const auto now = std::chrono::floor<std::chrono::milliseconds>(std::chrono::system_clock::now());   // two in one second: two files
			return folder / std::format("MenusRevamped-census-{:%Y%m%d-%H%M%S}.txt", now);
		}

		void Write()
		{
			static auto* layoutClass = ui::Class(L"/Game/UI/WBP_PrimaryGameLayout.WBP_PrimaryGameLayout_C");
			static auto* listClass = ui::Class(L"/Script/UMG.ListViewBase");
			std::vector<Node> nodes;
			for (auto* layout : layoutClass ? reflect::Instances(layoutClass) : std::vector<UE::UObject*>{})
				if (reflect::Usable(layout) && Prop(layout, "WidgetTree") && !WalkGuarded(layout, 0, 20000, &nodes)) nodes.clear();
			const auto path = OutputFile();
			std::ofstream out(path);
			if (!out) return;
			out << "Menus Revamped widget census (dev build, Ctrl+Shift+F12). Widgets of WBP_PrimaryGameLayout on screen; "
			       "collapsed (vis=1) and hidden (vis=2) ones are listed but not entered. Colours are linear RGBA.\n";
			std::size_t rows = 0;
			for (const auto& node : nodes) {
				if (!reflect::Usable(node.widget)) continue;
				out << Line(node.widget, node.depth) << '\n';
				if (!listClass || !node.widget->GetClass()->IsChildOf(listClass)) continue;
				// list rows live outside the widget tree: the displayed entry widgets, each walked like a tree
				ui::Call shown(node.widget, L"GetDisplayedEntryWidgets");
				struct RawArray { UE::UObject** data; std::int32_t num; std::int32_t max; };
				const auto* entries = shown && shown.Run() ? static_cast<const RawArray*>(shown.At("ReturnValue")) : nullptr;
				for (std::int32_t i = 0; entries && entries->data && i < entries->num && i < 64; ++i) {
					std::vector<Node> row;
					if (!WalkGuarded(entries->data[i], node.depth + 1, 400, &row)) continue;
					out << std::string(static_cast<std::size_t>(node.depth + 1) * 2, ' ') << "-- row " << i << '\n';
					for (const auto& item : row)
						if (reflect::Usable(item.widget)) out << Line(item.widget, item.depth) << '\n';
					++rows;
				}
			}
			logger::warn("census: {} widgets and {} list rows written to {}", nodes.size(), rows, path.string());
		}
	}

	bool Message(UINT a_message, WPARAM a_key, LPARAM a_position)
	{
		if (a_message != WM_KEYDOWN || a_key != VK_F12 || (a_position & (1 << 30)) ||   // bit 30: a held key's repeat
		    !(GetKeyState(VK_CONTROL) & 0x8000) || !(GetKeyState(VK_SHIFT) & 0x8000))
			return false;
		g_request = true;
		return true;
	}

	void Tick()
	{
		if (g_request.exchange(false) && reflect::Ok()) Write();
	}
}
