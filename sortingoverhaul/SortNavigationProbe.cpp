#include "SortNavigationProbe.h"

#ifdef SORTING_NAV_PROBE
#include "PEHook.h"
#include "Reflect.h"
#include "Ui.h"

#include <unordered_map>
#include <unordered_set>

namespace sortnavprobe
{
	namespace
	{
		constexpr unsigned kLineLimit = 1600, kEventLimit = 160, kClassLimit = 5;
		constexpr ULONGLONG kCaptureMs = 30000;
		struct Target { reflect::Handle object; std::string label; };
		std::vector<Target> g_targets;
		std::unordered_set<UE::UClass*> g_described, g_watched;
		std::unordered_map<UE::UFunction*, unsigned> g_calls;
		reflect::Handle g_filter, g_menu, g_scroll, g_model;
		ULONGLONG g_deadline = 0, g_nextSample = 0;
		unsigned g_lines = 0, g_events = 0, g_captures = 0;
		bool g_busy = false, g_keyDown = false, g_padYDown = false;
				ULONGLONG g_padYEdge = 0;
		std::string g_previousState;

		bool Read(const void* source, void* dest, std::size_t bytes)
		{
			__try { std::memcpy(dest, source, bytes); return true; }
			__except (EXCEPTION_EXECUTE_HANDLER) { return false; }
		}

		std::string Hex(const void* source, std::size_t bytes)
		{
			if (!source || bytes > 64) return {};
			std::uint8_t copy[64]{};
			if (!Read(source, copy, bytes)) return "unreadable";
			constexpr char digits[] = "0123456789abcdef";
			std::string out;
			for (std::size_t i = 0; i < bytes; ++i) {
				out += digits[copy[i] >> 4]; out += digits[copy[i] & 15];
			}
			return out;
		}

		template <class... Args>
		void Log(spdlog::format_string_t<Args...> format, Args&&... args)
		{
			if (g_lines++ < kLineLimit) logger::warn(format, std::forward<Args>(args)...);
		}

		bool Capturing()
		{
			return g_deadline && GetTickCount64() < g_deadline && g_lines < kLineLimit;
		}

		std::string State(UE::UObject* object)
		{
			if (!object) return "no-filter";
			std::string result;
			for (const char* field : { "CurrentActiveFilter", "EnabledFilterWidget", "CurrentEnabled Filters" }) {
				const auto offset = reflect::Offset(object->GetClass(), field);
				const auto size = ui::SizeOf(object->GetClass(), field);
				if (offset < 0 || size <= 0 || size > 16 || offset + size > object->GetClass()->propertiesSize) continue;
				result += std::string(field) + "=" + Hex(reinterpret_cast<std::uint8_t*>(object) + offset, size) + " ";
			}
			return result;
		}

		std::string Label(UE::UObject* object)
		{
			for (const auto& target : g_targets) if (reflect::Get(target.object) == object) return target.label;
			return "untracked";
		}

		bool Interesting(const std::string& name)
		{
			// Metadata is still unfiltered. Only the event trace uses these explicit action/focus categories.
			if (name.starts_with("Get") || name.starts_with("Is") || name == "OnAnalogValueChanged") return false;
			for (const char* word : { "Clicked", "Tirggering", "Trigger", "Navigate", "Sorting", "Reversed", "SortType", "ActiveFilter", "FocusedElement" }) {
				if (name.find(word) != std::string::npos) return true;
			}
			return name == "OnFocus" || name == "OnUnfocus" || name == "BP_OnActivated" || name == "BP_OnDeactivated";
		}

		std::string Parameters(UE::UFunction* function, void* params)
		{
			std::string out;
			unsigned count = 0;
			for (const auto& [name, offset] : reflect::Fields(function)) {
				if (++count > 8) { out += "TRUNCATED"; break; }
				const auto bytes = ui::SizeOf(function, name);
				if (offset < 0 || bytes < 1 || offset + bytes > function->parmsSize) continue;
				out += name + "@" + std::to_string(offset) + ":" + std::to_string(bytes) + "=";
				out += bytes <= 64 && params ? Hex(static_cast<std::uint8_t*>(params) + offset, bytes) : "large";
				out += " ";
			}
			return out;
		}

		std::string FocusState()
		{
			// Only proven, side-effect-free getters; g_busy suppresses probe reentry during these calls.
			std::string out;
			ui::Call index(reflect::Get(g_scroll), L"GetFocusElementIndex");
			if (index && index.Size("ReturnValue") == 4 && index.RunGuarded())
				out += "focus-index=" + std::to_string(index.Get<int>("ReturnValue")) + " ";
			ui::Call focused(reflect::Get(g_scroll), L"GetFocusedElement");
			if (focused && focused.Size("ReturnValue") == 8 && focused.RunGuarded()) {
				auto* widget = focused.Get<UE::UObject*>("ReturnValue");
				out += "focused-wrapper=" + Label(widget) + " ";
			}
			ui::Call filterMode(reflect::Get(g_menu), L"IsFilterFocused");
			if (filterMode && filterMode.Size("IsClosed") == 1 && filterMode.RunGuarded())
				out += "IsFilterFocused.IsClosed=" + std::to_string(filterMode.Get<std::uint8_t>("IsClosed")) + " ";
			if (auto* model = reflect::Get(g_model)) {
				for (const char* name : { "SortTypeInventory", "bIsSortReversedInventory", "SortType", "bIsSortReversed" }) {
					const auto offset = reflect::Offset(model->GetClass(), name);
					if (offset < 0 || ui::SizeOf(model->GetClass(), name) != 1) continue;
					out += std::string(name) + "=" + Hex(reinterpret_cast<std::uint8_t*>(model) + offset, 1) + " ";
				}
			}
			return out;
		}

		bool Targeted(UE::UObject* object)
		{
			return std::any_of(g_targets.begin(), g_targets.end(), [object](const auto& target) {
				return reflect::Get(target.object) == object;
			});
		}

		void Before(UE::UObject* object, UE::UFunction* function, void* params)
		{
			if (g_busy || !Capturing() || !Targeted(object) || g_events >= kEventLimit) return;
			const auto name = pe::FunctionName(function);
			if (!Interesting(name)) return;
			if (++g_calls[function] > 24) return;
			++g_events;
			Log("TEMP-DIAG SO-NAV2 event={} pre {}.{} params={} bytes={} state={}",
				g_events, Label(object), name, function->parmsSize, Parameters(function, params),
				State(reflect::Get(g_filter)));
		}

		void After(UE::UObject* object, UE::UFunction* function, void*)
		{
			if (g_busy || !Capturing() || !Targeted(object)) return;
			const auto found = g_calls.find(function);
			if (found == g_calls.end() ||found->second > 24 || g_events >= kEventLimit) return;
			Log("TEMP-DIAG SO-NAV2 post {}.{} state={}",
				Label(object), pe::FunctionName(function), State(reflect::Get(g_filter)));
		}

		void Describe(UE::UObject* object, const std::string& label = {})
		{
			if (!object || g_targets.size() >= 32) return;
			g_targets.push_back({ reflect::Hold(object), label.empty() ? pe::Utf8(object->GetFName().ToString()) : label });
			auto* objectClass = object->GetClass();
			Log("TEMP-DIAG SO-NAV2 object {} class={} address={:p} size={}",
				pe::Utf8(object->GetFName().ToString()), pe::Utf8(objectClass->GetFName().ToString()),
				static_cast<void*>(object), objectClass->propertiesSize);
			const bool describeClass = g_described.insert(objectClass).second;
			auto* functionClass = ui::Class(L"/Script/CoreUObject.Function");
			for (auto* cls = static_cast<UE::UStruct*>(objectClass); cls && g_lines < kLineLimit; cls = cls->GetSuperStruct()) {
				const auto owner = pe::Utf8(cls->GetFName().ToString());
				if (owner == "Object" || owner == "UserWidget" || owner == "Widget" ||
					owner == "CommonActivatableWidget" || owner == "CommonUserWidget") break;
				for (const auto& [name, offset] : reflect::Fields(cls)) {
					const auto size = ui::SizeOf(cls, name);
					if (offset < 0 || size < 0 || offset + size > objectClass->propertiesSize) continue;
					if (describeClass) Log("TEMP-DIAG SO-NAV2 field {}.{} offset=0x{:X} size={}", owner, name, offset, size);
					const bool wantValue = describeClass || name == "EnumKey" || name == "Filter" || name == "isReversed" ||
						name == "IsFilterActivated" || name == "ParentWrapper" || name == "AssociatedFilter" || name == "filterIcon";
					if (wantValue && (size == 1 || size == 4 || size == 8 || name == "CurrentEnabled Filters")) {
						Log("TEMP-DIAG SO-NAV2 value {}.{} raw={}", Label(object),
							name, Hex(reinterpret_cast<std::uint8_t*>(object) + offset, (std::min)(size, 32)));
					}
				}
				if (!describeClass) continue;
				unsigned visited = 0;
				for (auto* field = cls->children.Get(); field && ++visited <= 512 && g_lines < kLineLimit; field = field->next) {
					if (!functionClass || !field->GetClass()->IsChildOf(functionClass)) continue;
					auto* fn = static_cast<UE::UFunction*>(field);
					std::string signature;
					for (const auto& [parameter, offset] : reflect::Fields(fn)) {
						if (signature.size() > 800) { signature += "TRUNCATED"; break; }
						signature += parameter + "@" + std::to_string(offset) + ":" + std::to_string(ui::SizeOf(fn, parameter)) + " ";
					}
					Log("TEMP-DIAG SO-NAV2 method {}.{} size={} native={:p} [{}]", owner, pe::FunctionName(fn),
						fn->parmsSize, reinterpret_cast<void*>(fn->func), signature);
				}
			}
			if (!g_watched.contains(objectClass) && g_watched.size() < kClassLimit) {
				const bool pre = pe::Watch(objectClass, &Before), post = pe::WatchPost(objectClass, &After);
				if (pre && post) g_watched.insert(objectClass);
				Log("TEMP-DIAG SO-NAV2 observers class={} pre={} post={}", pe::Utf8(objectClass->GetFName().ToString()), pre, post);
			}
		}
	}

	void ObserveButtons(std::uint16_t buttons)
	{
		const bool down = (buttons & 0x8000u) != 0;  // XINPUT_GAMEPAD_Y
		g_padYEdge = down && !g_padYDown ? GetTickCount64() : 0;
		g_padYDown = down;
	}

	void Tick(int menu, UE::UObject* widget)
	{
		const bool down = (GetAsyncKeyState(VK_F10) & 0x8000) != 0;
		const bool edge = (down && !g_keyDown) || (g_padYEdge && GetTickCount64() - g_padYEdge <= 100);
		g_padYEdge = 0;
		g_keyDown = down;
		if (g_busy || !widget || (menu != 1 && menu != 2)) return;
		if (edge && !g_deadline && g_captures < 2) {
			g_busy = true;
			g_lines = g_events = 0; g_calls.clear(); g_described.clear(); g_targets.clear(); g_previousState.clear();
			++g_captures; g_deadline = GetTickCount64() + kCaptureMs;
			Log("TEMP-DIAG SO-NAV2 BEGIN capture={} menu={} limit-ms={} line-limit={} event-limit={} exe={:p}; observes only, no input/list/state writes",
				g_captures, menu, kCaptureMs, kLineLimit, kEventLimit, static_cast<void*>(GetModuleHandleW(nullptr)));
			g_menu = reflect::Hold(widget);
			auto* filter = ui::FindInTree(widget, menu == 1 ? "inv_cont_filter" : "Magic_Filters");
			g_filter = filter ? reflect::Hold(filter) : reflect::Handle{};
			auto** model = reflect::At<UE::UObject*>(widget, reflect::Offset(widget->GetClass(),
				menu == 1 ? "VInventoryMenuViewModel" : "VMagicMenuViewModel"));
			g_model = model && *model ? reflect::Hold(*model) : reflect::Handle{};
			Describe(widget); Describe(filter);
			auto* iconBox = filter ? ui::FindInTree(filter, "FilterIconBox") : nullptr;
			Describe(iconBox);
			g_scroll = iconBox ? reflect::Hold(iconBox) : reflect::Handle{};
			auto* panel = iconBox ? ui::FindInTree(iconBox, "ScrollBox") : nullptr;
			ui::Call count(panel, L"GetChildrenCount");
			const int total = count && count.Run() ? count.Get<int>("ReturnValue") : -1;
			Log("TEMP-DIAG SO-NAV2 native header children={} (includes existing custom headers)", total);
			for (int i = 0; i < total && i < 12; ++i) {
				ui::Call child(panel, L"GetChildAt"); child.Set("Index", i);
				if (!child.Run()) continue;
				auto* wrapper = child.Get<UE::UObject*>("ReturnValue");
				if (!wrapper) continue;
				const auto offset = reflect::Offset(wrapper->GetClass(), "filterIcon");
				if (offset < 0 || ui::SizeOf(wrapper->GetClass(), "filterIcon") != 8) continue; // skip non-navigable SizeBox
				Describe(wrapper);
				UE::UObject* button = nullptr;
				if (Read(reinterpret_cast<std::uint8_t*>(wrapper) + offset, &button, sizeof(button)) && button)
					Describe(button, pe::Utf8(wrapper->GetFName().ToString()) + "/filterIcon");
			}
			Log("TEMP-DIAG SO-NAV2 schema end lines={} class-cap={} described-classes={}; now use Y and native sort controls",
				g_lines, kClassLimit, g_described.size());
			g_busy = false;
		}
		if (!g_deadline) return;
		const auto now = GetTickCount64();
		if (now >= g_deadline || g_lines >= kLineLimit || g_events >= kEventLimit) {
			logger::warn("TEMP-DIAG SO-NAV2 END capture={} lines={} events={} deadline={} bounded-stop=true",
				g_captures, g_lines, g_events, now >= g_deadline);
			logger::flush(); g_deadline = 0; return;
		}
		if (now >= g_nextSample) {
			g_nextSample = now + 100;
			g_busy = true;
			const auto state = State(reflect::Get(g_filter)) + FocusState();
			g_busy = false;
			if (state != g_previousState) { Log("TEMP-DIAG SO-NAV2 state {}", state); g_previousState = state; }
		}
	}
}
#endif
