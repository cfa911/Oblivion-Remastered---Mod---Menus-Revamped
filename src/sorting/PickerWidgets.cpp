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

namespace gearfilter
{
	namespace detail
	{
		void Brush(UE::UObject* a_image, const wchar_t* a_icon)
		{
			if (auto* texture = icons::Texture(a_icon)) {
				ui::Call set(a_image, L"SetBrushFromTexture");
				set.Set("Texture", texture);
				set.Set("bMatchSize", false);
				set.Run();
			}
		}

		UE::UObject* CanvasChild(UE::UObject* a_canvas, const wchar_t* a_class, const wchar_t* a_name, double x, double y, double w, double h, int z, UE::UObject** a_slot)
		{
			UE::UObject* slot = nullptr;
			auto* child = ui::AddToCanvas(a_canvas, a_class, a_name, &slot);
			if (!child || !slot) return nullptr;
			if (a_slot) *a_slot = slot;
			ui::Vec2(slot, L"SetPosition", x, y);
			ui::Vec2(slot, L"SetSize", w, h);
			ui::CallFirst(slot, L"SetZOrder", &z, sizeof(z));
			return child;
		}

		void LayoutChild(UE::UObject* widget, double x, double y, double width, double height)
		{
			auto** slot = widget ? reflect::At<UE::UObject*>(widget, reflect::Offset(widget->GetClass(), "Slot")) : nullptr;
			if (!slot || !*slot) return;
			ui::Vec2(*slot, L"SetPosition", x, y);
			ui::Vec2(*slot, L"SetSize", width, height);
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

		void BoundLabel(UE::UObject* label)
		{
			const std::uint8_t clipToBounds = 1, clipOverflow = 0;
			const bool wrap = false;
			ui::CallFirst(label, L"SetClipping", &clipToBounds, sizeof(clipToBounds));
			ui::CallFirst(label, L"SetTextOverflowPolicy", &clipOverflow, sizeof(clipOverflow));
			ui::CallFirst(label, L"SetAutoWrapText", &wrap, sizeof(wrap));
		}
	}
}
