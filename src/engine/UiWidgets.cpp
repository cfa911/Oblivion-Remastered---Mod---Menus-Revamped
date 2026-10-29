#include "engine/Ui.h"

#include "engine/PEHook.h"
#include "engine/Reflect.h"

// Creating, styling and measuring widgets; material parameters.
namespace ui
{
	UE::UObject* CreateWidget(const wchar_t* a_classPath)
	{
		static auto* lib = Class(L"/Script/UMG.WidgetBlueprintLibrary");
		auto* cls = Class(a_classPath);
		auto* pc = PlayerController();
		auto* cdo = lib ? lib->GetDefaultObject(false) : nullptr;
		if (!cdo || !cls || !pc) return nullptr;
		Call c(cdo, L"Create");
		c.Set("WorldContextObject", pc);
		c.Set("WidgetType", cls);
		c.Set("OwningPlayer", pc);
		if (!c.RunGuarded()) return nullptr;   // a world-context call: fault-guarded (a quit, a load)
		return c.Get<UE::UObject*>("ReturnValue");
	}

	UE::UObject* RootCanvas(UE::UObject* a_userWidget, const wchar_t* a_name)
	{
		auto* treeClass = Class(L"/Script/UMG.WidgetTree");
		auto* canvasClass = Class(L"/Script/UMG.CanvasPanel");
		if (!a_userWidget || !treeClass || !canvasClass || !a_userWidget->GetClass()) return nullptr;
		auto** tree = reflect::At<UE::UObject*>(a_userWidget, reflect::Offset(a_userWidget->GetClass(), "WidgetTree"));
		if (!tree) return nullptr;
		if (!*tree) *tree = UE::NewObject<UE::UObject>(a_userWidget, treeClass, UE::FName((std::wstring(a_name) + L"Tree").c_str()));
		if (!*tree) return nullptr;
		auto* canvas = UE::NewObject<UE::UObject>(*tree, canvasClass, UE::FName(a_name));
		auto** root = reflect::At<UE::UObject*>(*tree, reflect::Offset(treeClass, "RootWidget"));
		if (!canvas || !root) return nullptr;
		*root = canvas;
		return canvas;
	}

	UE::UObject* AddToCanvas(UE::UObject* a_canvas, const wchar_t* a_class, const wchar_t* a_name, UE::UObject** a_slot)
	{
		*a_slot = nullptr;
		auto* cls = Class(a_class);
		auto* outer = a_canvas ? a_canvas->GetOuter() : nullptr;   // the widget tree
		if (!cls || !outer) return nullptr;
		auto* w = UE::NewObject<UE::UObject>(outer, cls, UE::FName(a_name));
		if (!w) return nullptr;
		Call add(a_canvas, L"AddChildToCanvas");
		add.Set("Content", w);
		add.Run();
		*a_slot = add.Get<UE::UObject*>("ReturnValue");
		if (!*a_slot) return nullptr;
		const bool no = false;
		CallFirst(*a_slot, L"SetAutoSize", &no, sizeof(no));
		return w;
	}

	void Anchors(UE::UObject* a_slot, double a_minX, double a_minY, double a_maxX, double a_maxY)
	{
		const double a[4] = { a_minX, a_minY, a_maxX, a_maxY };
		CallFirst(a_slot, L"SetAnchors", a, sizeof(a));
	}

	bool RoundedBox(UE::UObject* a_image, bool a_circle, const float a_fill[4], const float a_outline[4], float a_width, float a_radius)
	{
		static auto* brushStruct = reinterpret_cast<UE::UStruct*>(UE::StaticFindObject<UE::UObject>(nullptr, nullptr, L"/Script/SlateCore.SlateBrush"));
		static auto* outlineStruct = reinterpret_cast<UE::UStruct*>(UE::StaticFindObject<UE::UObject>(nullptr, nullptr, L"/Script/SlateCore.SlateBrushOutlineSettings"));
		static auto* colorStruct = reinterpret_cast<UE::UStruct*>(UE::StaticFindObject<UE::UObject>(nullptr, nullptr, L"/Script/SlateCore.SlateColor"));
		if (!a_image || !brushStruct || !outlineStruct || !colorStruct || !a_image->GetClass()) return false;
		const auto brushOff = reflect::Offset(a_image->GetClass(), "Brush");
		const auto brushSize = SizeOf(a_image->GetClass(), "Brush");
		const auto drawAs = reflect::Offset(brushStruct, "DrawAs");
		const auto tint = reflect::Offset(brushStruct, "TintColor");
		const auto outline = reflect::Offset(brushStruct, "OutlineSettings");
		const auto radii = reflect::Offset(outlineStruct, "CornerRadii");
		const auto radiiSize = SizeOf(outlineStruct, "CornerRadii");
		const auto oColor = reflect::Offset(outlineStruct, "Color");
		const auto rounding = reflect::Offset(outlineStruct, "RoundingType");
		const auto width = reflect::Offset(outlineStruct, "Width");
		const auto spec = reflect::Offset(colorStruct, "SpecifiedColor");
		const auto rule = reflect::Offset(colorStruct, "ColorUseRule");
		if (brushOff < 0 || brushSize <= 0 || drawAs < 0 || tint < 0 || outline < 0 || radii < 0 || oColor < 0 || rounding < 0 || width < 0 ||
			spec < 0 || rule < 0) {
			return false;
		}
		auto* b = reinterpret_cast<std::uint8_t*>(a_image) + brushOff;
		b[drawAs] = 4;   // RoundedBox
		std::memcpy(b + tint + spec, a_fill, sizeof(float) * 4);
		b[tint + rule] = 0;
		std::memcpy(b + outline + oColor + spec, a_outline, sizeof(float) * 4);
		b[outline + oColor + rule] = 0;
		const double r = a_radius;
		if (radiiSize == 32) {
			const double v[4] = { r, r, r, r };
			std::memcpy(b + outline + radii, v, sizeof(v));
		} else if (radiiSize == 16) {
			const float v[4] = { a_radius, a_radius, a_radius, a_radius };
			std::memcpy(b + outline + radii, v, sizeof(v));
		}
		b[outline + rounding] = a_circle ? 1 : 0;
		std::memcpy(b + outline + width, &a_width, sizeof(a_width));
		std::vector<std::uint8_t> copy(b, b + brushSize);
		return CallFirst(a_image, L"SetBrush", copy.data(), copy.size());
	}
}

namespace ui
{
	UE::UObject* ChildNamed(UE::UObject* a_userWidget, const wchar_t* a_name)
	{
		if (!a_userWidget || !a_name) return nullptr;
		Call c(a_userWidget, L"GetWidgetFromName");
		if (!c) return nullptr;
		c.Set("Name", UE::FName(a_name, UE::EFindName::Find));
		return c.Run() ? c.Get<UE::UObject*>("ReturnValue") : nullptr;
	}

	UE::UObject* BrushResource(UE::UObject* a_image)
	{
		static auto* brushStruct = reinterpret_cast<UE::UStruct*>(UE::StaticFindObject<UE::UObject>(nullptr, nullptr, L"/Script/SlateCore.SlateBrush"));
		if (!a_image || !brushStruct || !reflect::Ok()) return nullptr;
		const auto brushOff = reflect::Offset(reinterpret_cast<UE::UStruct*>(a_image->GetClass()), "Brush");
		const auto resOff = reflect::Offset(brushStruct, "ResourceObject");
		if (brushOff < 0 || resOff < 0) return nullptr;
		return *reinterpret_cast<UE::UObject* const*>(reinterpret_cast<const std::uint8_t*>(a_image) + brushOff + resOff);
	}

	float RenderOpacity(UE::UObject* a_widget)
	{
		static auto* widget = Class(L"/Script/UMG.Widget");
		const auto off = widget && a_widget && reflect::Ok() ? reflect::Offset(reinterpret_cast<UE::UStruct*>(widget), "RenderOpacity") : -1;
		return off >= 0 ? *reinterpret_cast<const float*>(reinterpret_cast<const std::uint8_t*>(a_widget) + off) : 1.0f;
	}

	bool Measure(UE::UObject* a_w, double& a_x, double& a_y, double& a_width, double& a_height)
	{
		auto* pc = PlayerController();
		static auto* lib = Class(L"/Script/UMG.SlateBlueprintLibrary");
		auto* cdo = lib ? lib->GetDefaultObject(false) : nullptr;
		if (!a_w || !pc || !cdo) return false;
		Call geo(a_w, L"GetCachedGeometry");
		const auto gsize = geo.Size("ReturnValue");
		if (!geo || gsize <= 0 || !geo.Run()) return false;
		Call size(cdo, L"GetLocalSize");
		void* g = size.At("Geometry");
		if (!size || !g || size.Size("Geometry") != gsize) return false;
		std::memcpy(g, geo.At("ReturnValue"), static_cast<std::size_t>(gsize));
		size.Run();
		const auto local = size.Get<std::array<double, 2>>("ReturnValue");
		const auto toViewport = [&](double a_lx, double a_ly, double& a_vx, double& a_vy) {
			Call c(cdo, L"LocalToViewport");
			void* gg = c.At("Geometry");
			if (!c || !gg || c.Size("Geometry") != gsize) return false;
			c.Set("WorldContextObject", pc);
			std::memcpy(gg, geo.At("ReturnValue"), static_cast<std::size_t>(gsize));
			const double lc[2] = { a_lx, a_ly };
			c.Set("LocalCoordinate", lc);
			if (!c.RunGuarded()) return false;   // a world-context call: fault-guarded (a quit, a load)
			const auto vpos = c.Get<std::array<double, 2>>("ViewportPosition");
			a_vx = vpos[0];
			a_vy = vpos[1];
			return true;
		};
		double x0 = 0, y0 = 0, x1 = 0, y1 = 0;
		if (!toViewport(0.0, 0.0, x0, y0) || !toViewport(local[0], local[1], x1, y1)) return false;
		a_x = x0;
		a_y = y0;
		a_width = x1 - x0;
		a_height = y1 - y0;
		return local[0] > 0.0 && a_width > 1.0 && a_height > 1.0;
	}

	float MidScalar(UE::UObject* a_mid, const wchar_t* a_name)
	{
		Call c(a_mid, L"K2_GetScalarParameterValue");
		if (!c) return 0.0f;
		c.Set("ParameterName", UE::FName(a_name, UE::EFindName::Add));
		return c.Run() ? c.Get<float>("ReturnValue") : 0.0f;
	}

	void SetMidScalar(UE::UObject* a_mid, const wchar_t* a_name, float a_v)
	{
		Call c(a_mid, L"SetScalarParameterValue");
		if (!c) return;
		c.Set("ParameterName", UE::FName(a_name, UE::EFindName::Add));
		c.Set("Value", a_v);
		c.Run();
	}

	void SetMidTexture(UE::UObject* a_mid, const wchar_t* a_name, UE::UObject* a_texture)
	{
		Call c(a_mid, L"SetTextureParameterValue");
		if (!c) return;
		c.Set("ParameterName", UE::FName(a_name, UE::EFindName::Add));
		c.Set("Value", a_texture);
		c.Run();
	}
}

namespace ui
{
	UE::UObject* MidTexture(UE::UObject* a_mid, const wchar_t* a_name)
	{
		Call c(a_mid, L"K2_GetTextureParameterValue");
		if (!c) return nullptr;
		c.Set("ParameterName", UE::FName(a_name, UE::EFindName::Add));
		return c.Run() ? c.Get<UE::UObject*>("ReturnValue") : nullptr;
	}
}
