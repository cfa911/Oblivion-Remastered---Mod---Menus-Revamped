#include "engine/ScrollHints.h"
#include "engine/ScrollHintRules.h"
#include "engine/Ui.h"
#include "engine/Reflect.h"
#include "engine/PEHook.h"
#include "engine/Menus.h"
#include "core/Config.h"

namespace scrollhints
{
	namespace
	{
		struct Entry {
			reflect::Handle owner, scope, material, glyph, image, slot;
		};
		std::vector<Entry> g_entries;
		std::vector<UE::UClass*> g_scopeHooks;
		reflect::Handle g_player, g_root, g_canvas, g_blank;
		bool g_dirty = true, g_hooked = false, g_open = false, g_lookup = false;
		ULONGLONG g_next = 0;
		ULONGLONG g_settle = 0;
		bool g_blankFailed = false;
		int g_serial = 0;

		std::string Name(UE::UObject* object) { return object ? pe::Utf8(object->GetFName().ToString()) : std::string{}; }
		void OnScope(UE::UObject*, UE::UFunction* function, void*)
		{
			const auto name=pe::FunctionName(function);
			if (name=="Tick" || name=="OnMouseMove" || name=="OnMouseEnter" || name=="OnMouseLeave") return;
			g_dirty=true; g_settle=GetTickCount64()+350;
		}
		void OnPlayer(UE::UObject* object, UE::UFunction* function, void*)
		{
			if (pe::FunctionName(function)=="Destruct") return;
			if (reflect::Get(g_player) != object) { g_player = reflect::Hold(object); g_dirty = true; }
			OnScope(object,function,nullptr);
		}
		bool Canvas()
		{
			if (auto* root = reflect::Get(g_root); root && reflect::Get(g_canvas)) {
				ui::Call shown(root, L"IsInViewport");
				if (shown.Run() && !shown.Get<bool>("ReturnValue")) {
					ui::Call add(root, L"AddToViewport"); add.Set<std::int32_t>("ZOrder", 64); add.Run();
				}
				return true;
			}
			auto* root = ui::CreateWidget(L"/Script/UMG.UserWidget");
			auto* canvas = root ? ui::RootCanvas(root, L"MRScrollHintCanvas") : nullptr;
			if (!canvas) return false;
			ui::Call add(root, L"AddToViewport"); add.Set<std::int32_t>("ZOrder", 64);
			if (!add.Run()) return false;
			ui::Visible(root, true);
			g_root = reflect::Hold(root); g_canvas = reflect::Hold(canvas);
			return true;
		}
		bool Vertical(UE::UObject* widget)
		{
			const auto offset = reflect::Offset(widget->GetClass(), "Orientation");
			return offset < 0 || ui::SizeOf(widget->GetClass(), "Orientation") != 1 ||
				*reflect::At<std::uint8_t>(widget, offset) == 1;
		}
		UE::UObject* Track(UE::UObject* widget)
		{
			static auto* style = UE::StaticFindObject<UE::UStruct>(nullptr, nullptr, L"/Script/SlateCore.ScrollBarStyle");
			static auto* brush = UE::StaticFindObject<UE::UStruct>(nullptr, nullptr, L"/Script/SlateCore.SlateBrush");
			const auto track = style ? reflect::Offset(style, "VerticalBackgroundImage") : -1;
			const auto bytes = style ? ui::SizeOf(style, "VerticalBackgroundImage") : -1;
			if (track < 0 || bytes <= 0 || !brush) return nullptr;
			std::uint8_t* actual = nullptr;
			for (const char* property : { "ScrollBarStyle", "WidgetBarStyle" }) {
				const auto offset = reflect::Offset(widget->GetClass(), property);
				const auto size = ui::SizeOf(widget->GetClass(), property);
				if (offset >= 0 && track + bytes <= size) { actual = reinterpret_cast<std::uint8_t*>(widget) + offset + track; break; }
			}
			if (!actual) return nullptr;
			static auto* library = ui::Class(L"/Script/UMG.WidgetBlueprintLibrary");
			ui::Call dynamic(library ? library->GetDefaultObject(false) : nullptr, L"GetDynamicMaterial");
			if (dynamic.Size("Brush") != bytes || !dynamic.At("Brush")) return nullptr;
			std::memcpy(dynamic.At("Brush"), actual, bytes);
			if (!dynamic.Run()) return nullptr;
			auto* material = dynamic.Get<UE::UObject*>("ReturnValue");
			if (!material) return nullptr;
			const auto resource = reflect::Offset(brush, "ResourceObject");
			if (resource < 0 || resource + sizeof(material) > static_cast<std::size_t>(bytes)) return nullptr;
			if (*reinterpret_cast<UE::UObject**>(actual + resource) != material) {
				std::memcpy(actual, dynamic.At("Brush"), bytes);
				ui::ResetBrushHandle(actual, bytes);
			}
			return material;
		}
		void Restore(Entry& entry)
		{
			if (auto* material = reflect::Get(entry.material))
				if (auto* glyph = reflect::Get(entry.glyph); glyph && ui::MidTexture(material, L"NavHint") == reflect::Get(g_blank))
					ui::SetMidTexture(material, L"NavHint", glyph);
			ui::Visible(reflect::Get(entry.image), false);
		}
		UE::UObject* Object(UE::UObject* owner, const char* property)
		{
			const auto offset=reflect::Offset(owner->GetClass(),property);
			return offset>=0 && ui::SizeOf(owner->GetClass(),property)==sizeof(UE::UObject*) ?
				*reflect::At<UE::UObject*>(owner,offset) : nullptr;
		}
		bool LocalVisible(UE::UObject* widget)
		{
			const auto offset=reflect::Offset(widget->GetClass(),"Visibility");
			if (offset>=0 && ui::SizeOf(widget->GetClass(),"Visibility")==1) {
				const auto visibility=*reflect::At<std::uint8_t>(widget,offset);
				if (visibility==1 || visibility==2) return false;
			}
			return ui::RenderOpacity(widget)>=0.05f;
		}
		std::vector<UE::UObject*> Children(UE::UObject* widget)
		{
			static auto* user=ui::Class(L"/Script/UMG.UserWidget");
			static auto* switcher=ui::Class(L"/Script/UMG.WidgetSwitcher");
			static auto* panel=ui::Class(L"/Script/UMG.PanelWidget");
			if (user && widget->GetClass()->IsChildOf(user)) {
				auto* tree=Object(widget,"WidgetTree");
				auto* root=tree ? Object(tree,"RootWidget") : nullptr;
				return root ? std::vector<UE::UObject*>{root} : std::vector<UE::UObject*>{};
			}
			if (switcher && widget->GetClass()->IsChildOf(switcher)) {
				ui::Call active(widget,L"GetActiveWidget");
				auto* child=active.Run() ? active.Get<UE::UObject*>("ReturnValue") : nullptr;
				return child ? std::vector<UE::UObject*>{child} : std::vector<UE::UObject*>{};
			}
			if (auto* displayed=Object(widget,"DisplayedWidget")) return {displayed};
			std::vector<UE::UObject*> out;
			if (!panel || !widget->GetClass()->IsChildOf(panel)) return out;
			const auto offset=reflect::Offset(panel,"Slots");
			struct Array { UE::UObject** data; std::int32_t num,max; };
			const auto* slots=reflect::At<Array>(widget,offset);
			if (!slots || !slots->data || slots->num<0 || slots->num>512 || slots->num>slots->max) return out;
			for (int i=0;i<slots->num;++i)
				if (auto* slot=slots->data[i]) if (auto* child=Object(slot,"Content")) out.push_back(child);
			return out;
		}
		void Collect(UE::UObject* root, bool scopes, std::vector<UE::UObject*>* output)
		{
			static auto* list=ui::Class(L"/Script/UMG.ListViewBase");
			static auto* scroll=ui::Class(L"/Script/UMG.ScrollBox");
			int budget=scopes ? 3000 : 1000;
			rules::WalkVisible(root,LocalVisible,Children,[&](UE::UObject* widget) {
				const bool target=scopes ? rules::Target(Name(widget->GetClass())) :
					((list && widget->GetClass()->IsChildOf(list)) || (scroll && widget->GetClass()->IsChildOf(scroll)));
				if (target) output->push_back(widget);
				// DetailsScrollbox contains the independently scrollable skill-description widget measured in the capture.
				return !target || (!scopes && Name(widget)=="DetailsScrollbox");
			},budget);
		}
		bool CollectGuarded(UE::UObject* root, bool scopes, std::vector<UE::UObject*>* output)
		{
			__try { Collect(root,scopes,output); return true; }
			__except (GetExceptionCode()==EXCEPTION_ACCESS_VIOLATION ? EXCEPTION_EXECUTE_HANDLER : EXCEPTION_CONTINUE_SEARCH) { return false; }
		}
		void Gather()
		{
			auto* player = reflect::Get(g_player);
			if (!player) return;
			static auto* list = ui::Class(L"/Script/UMG.ListViewBase");
			static auto* scroll = ui::Class(L"/Script/UMG.ScrollBox");
			std::vector<UE::UObject*> scopes;
			if (!CollectGuarded(player,true,&scopes)) return;
			for (auto* scope : scopes) {
				if (std::find(g_scopeHooks.begin(),g_scopeHooks.end(),scope->GetClass())==g_scopeHooks.end()) {
					g_scopeHooks.push_back(scope->GetClass()); pe::WatchPost(scope->GetClass(),&OnScope);
				}
				if (!ui::ShownOnScreen(scope)) continue;
				std::vector<UE::UObject*> widgets;
				if (!CollectGuarded(scope,false,&widgets)) continue;
				for (auto* widget : widgets) {
					if (!((list && widget->GetClass()->IsChildOf(list)) || (scroll && widget->GetClass()->IsChildOf(scroll))) || !Vertical(widget)) continue;
					if (std::any_of(g_entries.begin(), g_entries.end(), [widget](const auto& e) { return reflect::Get(e.owner) == widget; })) continue;
					if (g_entries.size() >= 64) return;
					double x=0,y=0,w=0,h=0;
					if (!ui::ShownOnScreen(widget) || !ui::Measure(widget,x,y,w,h) || w < 120 || h < 160) continue;
					auto* material = Track(widget);
					auto* glyph = material ? ui::MidTexture(material, L"NavHint") : nullptr;
					if (!glyph || !Canvas()) continue;
					UE::UObject* slot = nullptr;
					auto* image = ui::AddToCanvas(reflect::Get(g_canvas), L"/Script/UMG.Image", (L"MRScrollHint" + std::to_wstring(++g_serial)).c_str(), &slot);
					if (!image || !slot) continue;
					ui::Anchors(slot,0,0,0,0); ui::Vec2(slot,L"SetAlignment",0,0);
					ui::Visible(image,false);
					g_entries.push_back({reflect::Hold(widget),reflect::Hold(scope),reflect::Hold(material),reflect::Hold(glyph),reflect::Hold(image),reflect::Hold(slot)});
				}
			}
		}
		bool Scrollable(UE::UObject* widget, double bottom)
		{
			ui::Call end(widget, L"GetScrollOffsetOfEnd");
			if (end && end.Run()) return end.Get<float>("ReturnValue") > 1.0f;
			ui::Call count(widget, L"GetNumItems");
			ui::Call shown(widget, L"GetDisplayedEntryWidgets");
			struct Array { UE::UObject** data; std::int32_t num, max; };
			if (!count.Run() || !shown.Run() || shown.Size("ReturnValue") != sizeof(Array)) return false;
			const auto entries = shown.Get<Array>("ReturnValue");
			if (entries.num < 0 || entries.num > entries.max || entries.max > 4096 || (!entries.data && entries.num)) return false;
			bool overflow = count.Get<std::int32_t>("ReturnValue") > entries.num;
			ui::Call offset(widget,L"GetScrollOffset");
			if (offset && offset.Run()) overflow |= offset.Get<float>("ReturnValue") > 0.01f;
			for (int i=0; !overflow && entries.data && i<entries.num && i<64; ++i) {
				double x=0,y=0,w=0,h=0;
				if (ui::Measure(entries.data[i],x,y,w,h) && y+h > bottom+1.0) overflow=true;
			}
			if (entries.data) UE::FMemory::Free(entries.data);
			return overflow;
		}
	}

	void Discover()
	{
		if (g_hooked) return;
		auto* type = ui::Class(L"/Game/UI/Legacy/GameMenuLayer/WBP_ModernMenu_Player.WBP_ModernMenu_Player_C");
		if (type && (g_hooked = pe::WatchPost(type, &OnPlayer))) logger::info("MR-SCROLL-HINT-BOTTOM-T2: player-menu hook installed");
	}
	void Tick()
	{
		const bool open = menus::AnyOpen();
		if (!open) {
			if (g_open) for (auto& entry : g_entries) Restore(entry);
			g_open=false; g_lookup=false; return;
		}
		if (!g_open) { g_open=true; g_dirty=true; g_next=0; }
		if (!reflect::Get(g_player) && !g_lookup) {
			auto* type = ui::Class(L"/Game/UI/Legacy/GameMenuLayer/WBP_ModernMenu_Player.WBP_ModernMenu_Player_C");
			if (type) {
				g_lookup=true;
				for (auto* object : reflect::Instances(type)) if (reflect::Usable(object)) { g_player=reflect::Hold(object); break; }
			}
		}
		const auto now=GetTickCount64();
		if (now<g_next) return;
		g_next=now+100;
		std::erase_if(g_entries,[](auto& entry) {
			if (reflect::Get(entry.owner)) return false;
			Restore(entry); ui::Call remove(reflect::Get(entry.image),L"RemoveFromParent"); remove.Run(); return true;
		});
		if (g_dirty || now<g_settle) { g_dirty=false; Gather(); }
		if (g_entries.empty() || g_blankFailed) return;
		auto* blank=reflect::Get(g_blank);
		if (!blank) {
			blank=ui::ImportTexture(mrconfig::PluginFolder()/L"MenusRevamped"/L"Skin"/L"ScrollHintBlank.png");
			if (blank) g_blank=reflect::Hold(blank);
			else { g_blankFailed=true; logger::warn("Menus Revamped: scrollbar hint blank texture unavailable; native hints kept"); }
		}
		if (!blank || !Canvas()) return;
		for (auto& entry:g_entries) {
			auto* owner=reflect::Get(entry.owner);
			auto* material=reflect::Get(entry.material);
			if (!owner || !material || !ui::ShownOnScreen(reflect::Get(entry.scope)) || !ui::ShownOnScreen(owner)) { Restore(entry); continue; }
			auto* glyph=ui::MidTexture(material,L"NavHint");
			if (glyph && glyph!=blank) entry.glyph=reflect::Hold(glyph);
			glyph=reflect::Get(entry.glyph);
			double x=0,y=0,w=0,h=0;
			if (!glyph || !ui::Measure(owner,x,y,w,h)) { Restore(entry); continue; }
			const bool show=rules::Show(true,ui::MidScalar(material,L"IsGamepad")>0.5f,Scrollable(owner,y+h),glyph!=nullptr);
			auto* image=reflect::Get(entry.image);
			if (!image || !reflect::Get(entry.slot)) { Restore(entry); continue; }
			ui::Call texture(image,L"SetBrushFromTexture"); texture.Set("Texture",glyph); texture.Set("bMatchSize",false);
			if (!texture.Run()) { Restore(entry); continue; }
			const auto box=rules::Below({x,y,w,h});
			ui::Vec2(reflect::Get(entry.slot),L"SetPosition",box.x,box.y);
			ui::Vec2(reflect::Get(entry.slot),L"SetSize",box.w,box.h);
			ui::SetMidTexture(material,L"NavHint",blank);
			ui::Visible(image,show);
		}
	}
}
