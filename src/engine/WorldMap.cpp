#include "engine/WorldMap.h"
#include "engine/Menus.h"
#include "engine/PEHook.h"
#include "engine/Reflect.h"
#include "engine/Ui.h"
#include <UE/F/FLegacyMapMenuIconProperties.h>
#include <cmath>

namespace worldmap
{
	namespace
	{
		UE::UClass* g_class = nullptr;
		reflect::Handle g_widget, g_model;
		std::uint64_t g_generation = 1;
		bool g_scanned = false, g_watched = false;
		struct NativeFilter { reflect::Handle button, image; int activeOffset = -1; };
		std::array<NativeFilter, 13> g_filters;
		reflect::Handle g_filterRoot;
		ULONGLONG g_nextFilters = 0;
		void ClearFilters() { g_filters = {}; g_filterRoot = {}; g_nextFilters = 0; }
		void FindFilters()
		{
			if (GetTickCount64() < g_nextFilters) return;
			g_nextFilters = GetTickCount64() + 1000;
			auto* root = ui::FindInTree(Widget(), "MapIconFilter");
			if (root != reflect::Get(g_filterRoot)) { g_filters = {}; g_filterRoot = reflect::Hold(root); }
			if (!root) return;
			static constexpr const char* names[]{ "", "Camp", "Cave", "City", "ElvenRuin", "FortRuin", "Mine", "MountainPeak", "Tavern", "Settlement", "DaedricShrine", "OblivionGate", "Door" };
			for (int type = 1; type <= 12; ++type) {
				auto& f = g_filters[type];
				if (reflect::Get(f.button) && reflect::Get(f.image)) continue;
				auto* button = ui::FindInTree(root, names[type]);
				if (!button) continue;
				auto* cls = button->GetClass();
				const int active = reflect::Offset(cls, "IsFilterActivated");
				const int image = reflect::Offset(cls, "FilterImage");
				if (active < 0 || image < 0 || ui::SizeOf(cls, "IsFilterActivated") != 1 || ui::SizeOf(cls, "FilterImage") != sizeof(void*)) continue;
				auto** at = reflect::At<UE::UObject*>(button, image);
				f = { reflect::Hold(button), reflect::Hold(at ? *at : nullptr), active };
			}
		}
		std::uint32_t ReadFilters()
		{
			if (!Open()) return 0;
			FindFilters();
			std::uint32_t mask = 0;
			// The native All control can stay active with individual types disabled.
			for (int type = 1; type <= 12; ++type) {
				const auto& f = g_filters[type];
				auto* button = reflect::Get(f.button);
				const auto* active = button ? reflect::At<bool>(button, f.activeOffset) : nullptr;
				if (active && *active) mask |= 1u << type;
			}
			return mask;
		}

		void Reset()
		{
			if (g_widget.ptr || g_model.ptr) ++g_generation;
			g_widget = {}; g_model = {}; ClearFilters();
		}
		void Remember(UE::UObject* obj)
		{
			if (!obj || pe::Utf8(obj->GetName()) != "WorldMapPage" || !ui::ShownOnScreen(obj)) return;
			auto* cls = obj->GetClass();
			const auto off = reflect::Offset(cls, "VMapPageViewModel");
			auto** at = reflect::At<UE::UObject*>(obj, off);
			auto* model = at && ui::SizeOf(cls, "VMapPageViewModel") == sizeof(void*) ? *at : nullptr;
			if (!model || !reflect::IsLive(model)) return;
			if (reflect::Get(g_widget) != obj || reflect::Get(g_model) != model) { ++g_generation; ClearFilters(); }
			g_widget = reflect::Hold(obj); g_model = reflect::Hold(model);
		}
		void Observed(UE::UObject* obj, UE::UFunction*, void*) { Remember(obj); }
		UE::UObject* Model()
		{
			if (!Open()) return nullptr;
			Remember(reflect::Get(g_widget));
			return reflect::Get(g_model);
		}
		bool Layout(UE::UObject* model)
		{
			if (!model || !reflect::Ok()) return false;
			auto* cls = model->GetClass();
			auto* icon = UE::StaticFindObject<UE::UStruct>(nullptr, nullptr, L"/Script/Altar.LegacyMapMenuIconProperties");
			return icon && icon->propertiesSize == sizeof(UE::FLegacyMapMenuIconProperties) &&
				reflect::Offset(icon, "position") == 0 && reflect::Offset(icon, "Key") == 16 &&
				reflect::Offset(icon, "Name") == 24 && reflect::Offset(icon, "Type") == 48 &&
				reflect::Offset(icon, "FastTravelEnabled") == 49 && reflect::Offset(icon, "IconAddress") == 64 &&
				reflect::Offset(cls, "MapIcons") >= 0 && ui::SizeOf(cls, "MapIcons") == 16 &&
				reflect::Offset(cls, "MapPosition") >= 0 && ui::SizeOf(cls, "MapPosition") == 16 &&
				reflect::Offset(cls, "Zoom") >= 0 && ui::SizeOf(cls, "Zoom") == 4;
		}
		void Read(std::vector<maprules::Location>& out)
		{
			auto* model = Model();
			if (!Layout(model)) return;
			auto* array = reflect::At<UE::TArray<UE::FLegacyMapMenuIconProperties>>(model, reflect::Offset(model->GetClass(), "MapIcons"));
			if (!array || array->Num() < 0 || array->Num() > 4096) return;
			const auto filters = ActiveFilterMask();
			out.reserve(array->Num());
			for (const auto& icon : *array) {
				if (!std::isfinite(icon.position.x) || !std::isfinite(icon.position.y)) continue;
				maprules::Location l;
				l.key = pe::Utf8(icon.key.ToString()); l.address = pe::Utf8(icon.iconAddress);
				l.name = reflect::Text(icon.name); l.x = icon.position.x; l.y = icon.position.y;
				l.type = static_cast<int>(icon.type);
				// Per-marker native discovery/travel-unlocked state; never the player's
				// global fast-travel permission (combat/interior/quest restrictions).
				l.discovered = icon.fastTravelEnabled;
				l.passesNativeFilter = l.type >= 1 && l.type <= 12 && (filters & (1u << l.type)) != 0;
				l.id = std::format("{}|{}|{}|{}|{}|{}", g_generation, l.key, l.address, l.x, l.y, l.type);
				out.push_back(std::move(l));
			}
		}
		bool GuardedRead(std::vector<maprules::Location>& out)
		{
			__try { Read(out); return true; }
			__except (EXCEPTION_EXECUTE_HANDLER) { return false; }
		}
	}

	void Tick()
	{
		if (!reflect::Ok()) return;
		if (!menus::AnyOpen()) { Reset(); g_scanned = false; return; }
		if (!g_class) g_class = ui::Class(L"/Game/UI/Original/GameMenuLayer/Map/WBP_Modern_MapPage.WBP_Modern_MapPage_C");
		if (!g_class) return;
		if (!g_watched) g_watched = pe::WatchPost(g_class, Observed);
		if (!g_scanned) {
			g_scanned = true;
			// One fallback discovery per menu session; subsequent routing is event-based.
			for (auto* obj : reflect::Instances(g_class)) Remember(obj);
		}
	}
	bool Open()
	{
		if (!menus::AnyOpen()) { Reset(); g_scanned = false; return false; }
		auto* widget = reflect::Get(g_widget);
		return widget && ui::ShownOnScreen(widget) && reflect::Get(g_model);
	}
	UE::UObject* Widget() { return Open() ? reflect::Get(g_widget) : nullptr; }
	UE::UObject* View()
	{
		auto* page = Widget();
		if (!page) return nullptr;
		auto** map = reflect::At<UE::UObject*>(page, reflect::Offset(page->GetClass(), "MapWidget"));
		auto* widget = map ? *map : nullptr;
		auto** view = widget ? reflect::At<UE::UObject*>(widget, reflect::Offset(widget->GetClass(), "View")) : nullptr;
		return view ? *view : nullptr;
	}
	std::uint64_t Generation() { return g_generation; }
	std::uint32_t ActiveFilterMask()
	{
		__try { return ReadFilters(); }
		__except (EXCEPTION_EXECUTE_HANDLER) { ClearFilters(); return 0; }
	}
	bool CopyFilterIcon(int type, UE::UObject* image)
	{
		if (!image || type < 1 || type > 12) return false;
		auto* source = reflect::Get(g_filters[type].image);
		if (!source) return false;
		const int offset = reflect::Offset(source->GetClass(), "Brush");
		const int size = ui::SizeOf(source->GetClass(), "Brush");
		if (offset < 0 || size <= 0 || size > 4096 || ui::SizeOf(image->GetClass(), "Brush") != size) return false;
		return ui::CallFirst(image, L"SetBrush", reinterpret_cast<const std::uint8_t*>(source) + offset, size);
	}

	std::vector<maprules::Location> Snapshot()
	{
		std::vector<maprules::Location> out;
		if (!GuardedRead(out)) out.clear();
		return out;
	}
	bool Focus(const std::string& id)
	{
		const auto current = Snapshot();
		const auto* target = maprules::Resolve(current, id);
		auto* model = Model();
		if (!target || !model) return false;
		const UE::FVector2d position{ target->x, target->y };
		ui::Call focus(model, L"ForceSetMapPosition");
		return focus && focus.Size("NewMapPosition") == sizeof(position) && focus.Set("NewMapPosition", position) && focus.RunGuarded();
	}
	bool DisplayedCenter(double& x, double& y)
	{
		auto* page = Widget();
		if (!page || ui::SizeOf(page->GetClass(), "MapWidget") != sizeof(void*)) return false;
		auto** map = reflect::At<UE::UObject*>(page, reflect::Offset(page->GetClass(), "MapWidget"));
		auto* widget = map ? *map : nullptr;
		if (!widget || ui::SizeOf(widget->GetClass(), "MapImage") != sizeof(void*)) return false;
		auto** at = reflect::At<UE::UObject*>(widget, reflect::Offset(widget->GetClass(), "MapImage"));
		auto* image = at ? *at : nullptr;
		auto* view = View();
		double ix=0,iy=0,iw=0,ih=0,vx=0,vy=0,vw=0,vh=0;
		if (!image || !view || !ui::Measure(image,ix,iy,iw,ih) || !ui::Measure(view,vx,vy,vw,vh) ||
			iw<=1 || ih<=1 || vw<=1 || vh<=1) return false;
		// Native pan moves MapImage, while the page model can retain its last
		// centering request. Read the displayed map point at the viewport center.
		x = (vx+vw*0.5-ix)/iw;
		y = (vy+vh*0.5-iy)/ih;
		return std::isfinite(x) && std::isfinite(y);
	}
	bool Position(double& x, double& y, float& zoom)
	{
		auto* model = Model();
		if (!Layout(model)) return false;
		auto* pos = reflect::At<UE::FVector2d>(model, reflect::Offset(model->GetClass(), "MapPosition"));
		auto* z = reflect::At<float>(model, reflect::Offset(model->GetClass(), "Zoom"));
		if (!pos || !z) return false;
		x = pos->x; y = pos->y; zoom = *z;
		return true;
	}
}
