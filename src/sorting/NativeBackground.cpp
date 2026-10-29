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

namespace sorting
{
	namespace detail
	{
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
				if (picker.active.load() && pickerhighlight::Owns(picker.controllerFocus || picker.stolenFocus, picker.open, picker.controllerPopup)) {
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

		UE::UObject* Picker::NativeInner(UE::UObject* wrapper)
		{
			if (!wrapper || IsOwnedHeader(wrapper) || ui::SizeOf(wrapper->GetClass(), "filterIcon") != 8) return nullptr;
			auto** inner = reflect::At<UE::UObject*>(wrapper, reflect::Offset(wrapper->GetClass(), "filterIcon"));
			return inner ? *inner : nullptr;
		}

		void Picker::WarnNativeFocus()
		{
			if (!nativeFocusWarning) {
				logger::warn("Menus Revamped: native activated-sort background mask unavailable");
				nativeFocusWarning = true;
			}
		}
	}
}
