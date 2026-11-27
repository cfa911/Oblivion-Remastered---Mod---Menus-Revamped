// In-game Pack Builder Phase 4b: widget colour literals and our own run-time tints, live for the session (see LiveWidgets.h).
// Game thread only, except Post/Skin. Bounded: one object-array pass every 30 game ticks once a literal was edited (and at
// once after an edit); widgets found by their blueprint property, else a bounded tree walk (ui::FindInTree).
// SPDX-License-Identifier: GPL-3.0-or-later
#include "builder/LiveWidgets.h"

#include "core/DarkPack.h"
#include "core/DarkStyle.h"
#include "core/rules/CustomColours.h"
#include "engine/PEHook.h"
#include "engine/Reflect.h"
#include "engine/Ui.h"

#include <map>
#include <mutex>
#include <optional>
#include <set>
#include <unordered_map>

namespace builder::livepreview::widgets
{
	namespace
	{
		namespace live = pack::live;
		using Colour = std::array<float, 4>;

		constexpr const char* kMarker = "MR-LIVE-WIDGETS";
		constexpr unsigned    kCadence = 30;   // game ticks between passes over the live widgets

		std::mutex                 g_lock;   // g_posted, g_skin
		std::vector<Paint>         g_posted;
		std::optional<std::string> g_skin;

		struct Entry
		{
			Paint               paint;
			std::vector<Colour> known;   // what the slot may hold for this literal: stock, the colour at load, earlier previews
			UE::UClass*         widgetClass = nullptr;   // the widget's class (from the template or the first live widget)
			std::int32_t        slot = -1;               // the colour's offset in that widget
			std::string         property;                // the reflected property holding it
			bool                warned = false;
		};
		std::map<std::string, Entry> g_entries;   // asset|owner|offset
		unsigned                     g_ticks = 0;
		std::string                  g_skinApplied;

		std::wstring Wide(const std::string& a_ascii) { return std::wstring(a_ascii.begin(), a_ascii.end()); }   // asset and widget names are ASCII

		std::vector<Colour> Candidates(const Entry& a_e)
		{
			auto out = a_e.known;
			out.push_back(a_e.paint.colour);
			return out;
		}

		bool Holds(const UE::UObject* a_o, std::int32_t a_slot, const std::vector<Colour>& a_colours)
		{
			const auto* f = reinterpret_cast<const float*>(reinterpret_cast<const std::uint8_t*>(a_o) + a_slot);
			for (const auto& c : a_colours)
				if (live::SameColour(f, c)) return true;
			return false;
		}

		// the reflected property (of the class or a parent) whose bytes hold a_offset
		std::string PropertyAt(UE::UStruct* a_struct, std::int32_t a_offset)
		{
			for (auto* s = a_struct; s; s = s->GetSuperStruct())
				for (const auto& [name, offset] : reflect::Fields(s)) {
					const auto size = ui::SizeOf(s, name);
					if (size > 0 && a_offset >= offset && a_offset < offset + size) return name;
				}
			return {};
		}

		// where a widget keeps this literal: a colour slot inside a colour property, else the only slot that holds it
		bool Resolve(Entry& a_e, UE::UObject* a_widget)
		{
			auto*      cls = a_widget->GetClass();
			const auto size = cls ? reinterpret_cast<UE::UStruct*>(cls)->propertiesSize : 0;
			if (size <= 0x30) return false;
			const auto slots = live::ColourSlots(reinterpret_cast<const std::uint8_t*>(a_widget), static_cast<size_t>(size), Candidates(a_e));
			static const std::set<std::string> kColourProperties{ "ColorAndOpacity", "BrushColor", "ContentColorAndOpacity", "Brush" };
			for (const auto s : slots) {
				if (s < 0x28) continue;   // the UObject header
				auto name = PropertyAt(reinterpret_cast<UE::UStruct*>(cls), static_cast<std::int32_t>(s));
				if (kColourProperties.count(name)) {
					a_e.widgetClass = cls, a_e.slot = static_cast<std::int32_t>(s), a_e.property = std::move(name);
					return true;
				}
			}
			if (slots.size() == 1 && slots[0] >= 0x28) {   // a blueprint variable (a prefab's colour) or another property
				a_e.widgetClass = cls, a_e.slot = static_cast<std::int32_t>(slots[0]);
				a_e.property = PropertyAt(reinterpret_cast<UE::UStruct*>(cls), a_e.slot);
				return true;
			}
			return false;
		}

		// the property's setter, so Slate shows it now; false when the property has none
		bool Setter(UE::UObject* a_widget, const std::string& a_property, const Colour& a_c)
		{
			const wchar_t* fn = a_property == "ColorAndOpacity"         ? L"SetColorAndOpacity" :
			                    a_property == "BrushColor"              ? L"SetBrushColor" :
			                    a_property == "ContentColorAndOpacity"  ? L"SetContentColorAndOpacity" :
			                    a_property == "Brush"                   ? L"SetBrushTintColor" :
			                                                              nullptr;
			auto* f = fn ? a_widget->FindFunction(UE::FName(fn, UE::EFindName::Find)) : nullptr;
			if (!f) return false;
			const auto params = reflect::Fields(reinterpret_cast<UE::UStruct*>(f));
			if (params.empty()) return false;
			if (ui::SizeOf(reinterpret_cast<UE::UStruct*>(f), params[0].first) == 16) return ui::CallFirst(a_widget, fn, a_c.data(), 16);   // FLinearColor
			struct SlateColor   // FSlateColor, a specified colour (UiText.cpp Ink)
			{
				float        rgba[4];
				std::uint8_t rule;
				std::uint8_t pad[7];
			} ink{ { a_c[0], a_c[1], a_c[2], a_c[3] }, 0, {} };
			return ui::CallFirst(a_widget, fn, &ink, sizeof(ink));
		}

		void Write(UE::UObject* a_o, std::int32_t a_slot, const Colour& a_c)
		{
			std::memcpy(reinterpret_cast<std::uint8_t*>(a_o) + a_slot, a_c.data(), 16);
		}

		// a live widget: the new colour through its setter (raw bytes when the setter did not take it)
		void PaintWidget(Entry& a_e, UE::UObject* a_widget)
		{
			if (!a_widget || !reflect::Usable(a_widget)) return;
			if (a_e.slot < 0 && !Resolve(a_e, a_widget)) return;
			if (a_widget->GetClass() != a_e.widgetClass) return;
			const auto* now = reinterpret_cast<const float*>(reinterpret_cast<const std::uint8_t*>(a_widget) + a_e.slot);
			if (live::SameColour(now, a_e.paint.colour) || !Holds(a_widget, a_e.slot, a_e.known)) return;   // done, or a state colour the game set
			Setter(a_widget, a_e.property, a_e.paint.colour);
			if (!live::SameColour(now, a_e.paint.colour)) Write(a_widget, a_e.slot, a_e.paint.colour);
		}

		UE::UObject* ObjectProperty(UE::UObject* a_owner, const std::string& a_name)
		{
			auto*      cls = reinterpret_cast<UE::UStruct*>(a_owner->GetClass());
			const auto offset = reflect::Offset(cls, a_name);
			if (offset < 0 || ui::SizeOf(cls, a_name) != static_cast<std::int32_t>(sizeof(void*))) return nullptr;
			return *reflect::At<UE::UObject*>(a_owner, offset);
		}

		bool SameName(UE::UObject* a_o, const std::string& a_name)
		{
			return a_o && pe::Utf8(a_o->GetName()) == a_name;   // ASCII names
		}

		// the vector parameter of a material instance (K2_GetVectorParameterValue)
		bool VectorParameter(UE::UObject* a_mid, const std::string& a_name, Colour& a_out)
		{
			ui::Call get(a_mid, L"K2_GetVectorParameterValue");
			if (!get || !get.Set("ParameterName", UE::FName(Wide(a_name).c_str(), UE::EFindName::Add)) || !get.Run()) return false;
			a_out = get.Get<Colour>("ReturnValue");
			return true;
		}

		void PaintMid(const Entry& a_e, UE::UObject* a_mid)
		{
			Colour now{};
			if (!VectorParameter(a_mid, a_e.paint.lit.owner, now) || live::SameColour(now.data(), a_e.paint.colour)) return;
			if (!std::any_of(a_e.known.begin(), a_e.known.end(), [&](const Colour& c) { return live::SameColour(now.data(), c); })) return;
			ui::Call set(a_mid, L"SetVectorParameterValue");
			if (!set) return;
			set.Set("ParameterName", UE::FName(Wide(a_e.paint.lit.owner).c_str(), UE::EFindName::Add));
			set.Set("Value", a_e.paint.colour);
			set.Run();
		}

		void Pass()
		{
			// the blueprint classes and materials that are loaded now
			std::unordered_map<UE::UClass*, std::vector<Entry*>>  byClass;
			std::unordered_map<UE::UObject*, std::vector<Entry*>> byMaterial;
			for (auto& [key, e] : g_entries) {
				const auto& lit = e.paint.lit;
				if (lit.owner.empty()) continue;
				if (lit.kind == live::LiteralKind::Widget) {
					auto* cls = UE::StaticFindObject<UE::UClass>(nullptr, nullptr, Wide(live::ClassPath(lit.asset)).c_str());
					if (!cls) continue;   // not loaded: its widgets are built from the pak when it loads (painted on a later pass)
					// the template the class builds new widgets from: rows scrolled in and menus opened later take the colour
					if (auto* tmpl = UE::StaticFindObject<UE::UObject>(nullptr, nullptr, Wide(live::ClassPath(lit.asset) + ":WidgetTree." + lit.owner).c_str())) {
						if (e.slot < 0) Resolve(e, tmpl);
						if (e.slot >= 0 && tmpl->GetClass() == e.widgetClass && !live::SameColour(reinterpret_cast<const float*>(reinterpret_cast<std::uint8_t*>(tmpl) + e.slot), e.paint.colour) &&
							Holds(tmpl, e.slot, e.known))
							Write(tmpl, e.slot, e.paint.colour);
					}
					byClass[cls].push_back(&e);
				} else if (lit.kind == live::LiteralKind::Material) {
					if (auto* material = UE::StaticFindObject<UE::UObject>(nullptr, nullptr, Wide(live::ObjectPath(lit.asset)).c_str())) byMaterial[material].push_back(&e);
				}
			}
			if (byClass.empty() && byMaterial.empty()) return;
			static auto* midClass = ui::Class(L"/Script/Engine.MaterialInstanceDynamic");
			std::vector<UE::UObject*> instances, mids;
			if (auto* arr = UE::FUObjectArray::GetSingleton()) {
				arr->LockInternalArray();
				const std::int32_t n = arr->GetObjectArrayNum();
				for (std::int32_t i = 0; i < n; ++i) {
					auto* item = arr->IndexToObject(i);
					auto* o = item ? reinterpret_cast<UE::UObject*>(item->object) : nullptr;
					if (!o || (static_cast<std::int32_t>(o->objectFlags) & 0x30) != 0) continue;   // class default objects and templates
					auto* cls = o->GetClass();
					if (byClass.count(cls)) instances.push_back(o);
					else if (cls == midClass && !byMaterial.empty()) mids.push_back(o);
				}
				arr->UnlockInternalArray();
			}
			for (auto* o : instances) {
				if (!reflect::Usable(o)) continue;
				for (auto* e : byClass[o->GetClass()]) {
					auto* widget = ObjectProperty(o, e->paint.lit.owner);
					if (!SameName(widget, e->paint.lit.owner)) widget = ui::FindInTree(o, e->paint.lit.owner);
					PaintWidget(*e, widget);
				}
			}
			for (auto* mid : mids) {
				if (!reflect::Usable(mid)) continue;
				auto* parent = mid;
				for (int depth = 0; depth < 4 && parent; ++depth) {
					parent = ObjectProperty(parent, "Parent");
					if (auto it = byMaterial.find(parent); parent && it != byMaterial.end()) {
						for (const auto* e : it->second) PaintMid(*e, mid);
						break;
					}
				}
			}
			for (auto& [key, e] : g_entries)
				if (e.paint.lit.kind == live::LiteralKind::Widget && e.slot < 0 && !e.warned && byClass.count(UE::StaticFindObject<UE::UClass>(nullptr, nullptr, Wide(live::ClassPath(e.paint.lit.asset)).c_str()))) {
					e.warned = true;   // once per literal
					logger::warn("{}: {} {} keeps its colour until restart (no colour slot found)", kMarker, pack::AssetName(e.paint.lit.asset), e.paint.lit.owner);
				}
		}
	}

	void Post(std::vector<Paint> a_paints)
	{
		std::scoped_lock guard(g_lock);
		for (auto& p : a_paints) g_posted.push_back(std::move(p));
	}

	void Skin(const std::string& a_colourFile)
	{
		std::scoped_lock guard(g_lock);
		g_skin = a_colourFile;
	}

	void Tick()
	{
		std::vector<Paint>         posted;
		std::optional<std::string> skin;
		{
			std::scoped_lock guard(g_lock);
			posted.swap(g_posted);
			skin.swap(g_skin);
		}
		// our own widgets (keyboard, search bar, header rules) read darkstyle: they follow the Custom colours when Custom is in use
		if (skin && *skin != g_skinApplied && darkpack::UsesCustom()) {
			darkstyle::Set(customcolours::Parse(*skin).style);
			g_skinApplied = std::move(*skin);
		}
		for (auto& p : posted) {
			const auto key = p.lit.asset + "|" + p.lit.owner + "|" + std::to_string(p.lit.offset);
			auto [it, fresh] = g_entries.try_emplace(key);
			auto& e = it->second;
			if (fresh) e.known = { p.stock, p.baseline };
			else {
				e.known.push_back(e.paint.colour);   // the earlier preview is on screen now
				if (e.known.size() > 18) e.known.erase(e.known.begin() + 2);   // stock and load colours stay; the oldest previews go
			}
			e.paint = std::move(p);
		}
		if (g_entries.empty()) return;
		if (posted.empty() && ++g_ticks % kCadence != 0) return;
		Pass();
	}
}
