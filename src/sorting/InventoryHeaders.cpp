// Inventory visuals reuse the same registered carousel wrappers; their controller identities stay intact.
#include "sorting/PickerInternal.h"
#include "sorting/rules/InventoryHeaderLayout.h"
#include "engine/Reflect.h"
#include "engine/PEHook.h"
#include "engine/Ui.h"
#include "engine/UiText.h"
namespace sorting::detail
{
	bool IsInventoryStatusItem(UE::UObject* item);

	namespace
	{
		UE::UObject* InventoryList(UE::UObject* menu)
		{
			if (auto* list = ui::FindInTree(menu, "inv_cont_listview")) return list;
			return ui::FindByClassPrefix(menu, "WBP_OriginalMenu_InventoryListView");
		}
		UE::UObject* Slot(UE::UObject* widget)
		{
			if (!widget) return nullptr;
			auto** at=reflect::At<UE::UObject*>(widget,reflect::Offset(widget->GetClass(),"Slot"));
			return at?*at:nullptr;
		}
		bool Bounds(UE::UObject* widget,inventoryheaders::Box& box)
		{ return widget && ui::Measure(widget,box.x,box.y,box.w,box.h) && box.w>0 && box.h>0; }
		void StyleInventoryScrollbar(UE::UObject* list)
		{
			static bool reported = false;
			ui::Call get(list, L"GetListView");
			auto* inner = get.Size("ReturnValue") == sizeof(UE::UObject*) && get.RunGuarded()
				? get.Get<UE::UObject*>("ReturnValue") : list;
			if (!reflect::Usable(inner)) return;
			static auto* style = UE::StaticFindObject<UE::UStruct>(nullptr, nullptr, L"/Script/SlateCore.ScrollBarStyle");
			static auto* brush = UE::StaticFindObject<UE::UStruct>(nullptr, nullptr, L"/Script/SlateCore.SlateBrush");
			static auto* dynamicType = ui::Class(L"/Script/Engine.MaterialInstanceDynamic");
			static auto* materialType = ui::Class(L"/Script/Engine.MaterialInterface");
			static auto* materialLibrary = ui::Class(L"/Script/Engine.KismetMaterialLibrary");
			const int styleOffset = reflect::Offset(inner->GetClass(), "ScrollBarStyle");
			const int styleSize = ui::SizeOf(inner->GetClass(), "ScrollBarStyle");
			const int resourceOffset = brush ? reflect::Offset(brush, "ResourceObject") : -1;
			unsigned count = 0;
			const auto paint = [&](const char* field, bool track) {
				if (!style || !dynamicType || styleOffset < 0 || resourceOffset < 0) return;
				const int offset = reflect::Offset(style, field);
				if (offset < 0 || offset + resourceOffset + int(sizeof(UE::UObject*)) > styleSize) return;
				auto* resource = reflect::At<UE::UObject*>(inner, styleOffset + offset + resourceOffset);
				if (!resource || !reflect::Usable(*resource)) return;
				auto* material = *resource;
				if (!material->GetClass()->IsChildOf(dynamicType)) {
					if (track || !materialType || !materialLibrary || !material->GetClass()->IsChildOf(materialType)) return;
					ui::Call create(materialLibrary->GetDefaultObject(false), L"CreateDynamicMaterialInstance");
					if (create.Size("ReturnValue") != sizeof(UE::UObject*) ||
						!create.Set("WorldContextObject", inner) || !create.Set("Parent", material) ||
						!create.RunGuarded()) return;
					auto* instance = create.Get<UE::UObject*>("ReturnValue");
					if (!reflect::Usable(instance) || !instance->GetClass()->IsChildOf(dynamicType)) return;
					material = instance;
				}
				const auto before = count;
				const auto setColor = [&](const wchar_t* parameter, float gray) {
					const std::array value{gray, gray, gray, 1.0f};
					ui::Call set(material, L"SetVectorParameterValue");
					if (set.Set("ParameterName", UE::FName(parameter, UE::EFindName::Add)) &&
						set.Set("Value", value) && set.RunGuarded()) ++count;
				};
				if (track) {
					setColor(L"DarkColor", 0.0052f);
					setColor(L"LightColor", 0.0052f);
				} else {
					setColor(L"BorderColor - Dark", 0.0052f);
					setColor(L"BorderColor - Light", 0.0052f);
					setColor(L"InsideColor - Dark", 0.0222f);
					setColor(L"InsideColor - Light", 0.0222f);
				}
				if (count > before && *resource != material) *resource = material;
			};
			paint("VerticalBackgroundImage", true);
			for (const auto* field : {"NormalThumbImage", "HoveredThumbImage", "DraggedThumbImage"}) paint(field, false);
			if (!reported) {
				reported = true;
				logger::warn("TEMP-DIAG MR native skin: scrollbar style size {} and {} color calls", styleSize, count);
			}
		}

		UE::UObject* FirstRow(UE::UObject* list,const inventoryheaders::Box& viewport,
			double& rowsBottom,std::vector<UE::UObject*>& displayed)
		{
			for (const wchar_t* getter:{L"GetListView",L"GetItemListView"}) {
				ui::Call get(list,getter);
				if (get && get.Size("ReturnValue")==8 && get.RunGuarded())
					if (auto* inner=get.Get<UE::UObject*>("ReturnValue")) { list=inner; break; }
			}
			ui::Call entries(list,L"GetDisplayedEntryWidgets");
			if (!entries || entries.Size("ReturnValue")!=16 || !entries.RunGuarded()) return nullptr;
			struct Array { UE::UObject** data; std::int32_t count,capacity; };
			const auto array=entries.Get<Array>("ReturnValue");
			UE::UObject* row=nullptr;
			double firstY=viewport.y+viewport.h;
			if (array.data && array.count>0 && array.count<=4096) {
				const int count=std::min<int>(array.count,64);
				displayed.assign(array.data,array.data+count);
				// The entry array can be in pool order. Find the lowest laid-out row.
				for (auto* entry : displayed) {
					inventoryheaders::Box bounds;
					if (!reflect::Usable(entry) || !Bounds(entry,bounds) ||
						bounds.y>=viewport.y+viewport.h || bounds.y+bounds.h<=viewport.y) continue;
					rowsBottom=std::max(rowsBottom,bounds.y+bounds.h);
					if (bounds.y<firstY) {
						firstY=bounds.y;
						row=entry;
					}
				}
			}
			if (array.data) UE::FMemory::Free(array.data);
			return row;
		}
		bool ReserveList(Picker& p,UE::UObject* list,double headerY)
		{
			auto* slot=Slot(list); if (!slot) return false;
			if (p.inventoryListReserved && reflect::Get(p.inventoryListSlot)==slot) return true;
			p.HideInventoryHeaderLayout();
			std::array<float,4> offsets{}; bool padding=false,stretched=false;
			ui::Call get(slot,L"GetOffsets");
			if (get && get.Size("ReturnValue")==16 && get.RunGuarded()) {
				offsets=get.Get<std::array<float,4>>("ReturnValue");
				ui::Call anchors(slot,L"GetAnchors"); if (!anchors || !anchors.RunGuarded()) return false;
				if (anchors.Size("ReturnValue")==32) { const auto a=anchors.Get<std::array<double,4>>("ReturnValue"); stretched=a[3]>a[1]; }
				else if (anchors.Size("ReturnValue")==16) { const auto a=anchors.Get<std::array<float,4>>("ReturnValue"); stretched=a[3]>a[1]; }
				else return false;
			} else {
				const auto off=reflect::Offset(slot->GetClass(),"Padding");
				if (off<0 || ui::SizeOf(slot->GetClass(),"Padding")!=16) return false;
				std::memcpy(offsets.data(),reinterpret_cast<const std::uint8_t*>(slot)+off,16); padding=true;
			}
			auto changed=offsets; changed[1]+=float(inventoryheaders::kInset);
			if (!padding && !stretched) changed[3]=std::max(0.0f,changed[3]-float(inventoryheaders::kInset));
			if (!ui::CallFirstGuarded(slot,padding?L"SetPadding":L"SetOffsets",changed.data(),16)) return false;
			p.inventoryListSlot=reflect::Hold(slot); p.inventoryListBase=offsets;
			p.inventoryListPadding=padding; p.inventoryListReserved=true; p.inventoryHeaderY=headerY; return true;
		}
		UE::UObject* Column(UE::UObject* row, std::string_view name)
		{
			auto* horizontal = ui::FindInTree(row, "inv_entry_horizontal");
			ui::Call count(horizontal, L"GetChildrenCount");
			if (!count || !count.RunGuarded()) return nullptr;
			const int n = count.Get<int>("ReturnValue");
			if (n < 0 || n > 32) return nullptr;
			for (int i = 0; i < n; ++i) {
				ui::Call get(horizontal, L"GetChildAt");
				if (!get.Set("Index", i) || !get.RunGuarded()) return nullptr;
				auto* child = get.Get<UE::UObject*>("ReturnValue");
				if (child && pe::Utf8(child->GetFName().ToString()) == name) return child;
			}
			return nullptr;
		}

		constexpr float kProbeInk[]{ 0.86f, 0.86f, 0.86f, 1.0f };
		std::array<reflect::Handle, 3> g_probeFooter;

		void ProbeFooterInk(UE::UObject* footer)
		{
			auto* label = ui::FindInTree(footer, "text");
			if (!label) label = ui::FindByClassPrefix(footer, "WBP_AltarTextBlock");
			if (label) uix::Ink(label, kProbeInk);
		}

		void OnProbeFooterUpdated(UE::UObject* footer, UE::UFunction* function, void*)
		{
			const auto name = pe::FunctionName(function);
			if (name != "UpdateText" && name != "UpdateTextApperance") return;
			if (std::any_of(g_probeFooter.begin(), g_probeFooter.end(),
				[footer](const auto& handle) { return reflect::Get(handle) == footer; })) {
				ProbeFooterInk(footer);
			}
		}

		void ProbeInventoryRowInk(UE::UObject* row)
		{
			auto* name = Column(row, "inv_entry_name");
			if (!name) return;
			uix::Ink(name, kProbeInk);
			for (const auto* field : { "inv_entry_health", "inv_entry_weapon_damage",
				"inv_entry_armor_rating", "inv_entry_weight", "inv_entry_value" }) {
				auto* cell = Column(row, field);
				if (auto* label = ui::FindInTree(cell, "inv_entry_value")) uix::Ink(label, kProbeInk);
			}
			static bool reported = false;
			if (!reported) {
				reported = true;
				logger::warn("TEMP-DIAG MR native skin: Inventory row and footer contrast test");
			}
		}

		void PlaceRowStatus(Picker& picker,UE::UObject* row,
			const std::array<inventoryheaders::Box,3>* initialTargets=nullptr)
		{
			if (!reflect::Usable(row)) return;
			auto* status=Column(row,"inv_entry_statusIcons");
			const std::array icons{ui::FindInTree(status,"inv_status_enchanted"),
				ui::FindInTree(status,"inv_status_stolen")};
			unsigned mask=0;
			for (unsigned i=0;i<icons.size();++i) {
				ui::Call visible(icons[i],L"IsVisible");
				if (visible && visible.RunGuarded() && visible.Get<bool>("ReturnValue")) mask|=1u<<i;
			}
			const std::array filters{picker.enchantedHeader,picker.stolenHeader};
			for (unsigned i=0;i<icons.size();++i) {
				if (!(mask&(1u<<i)) || !reflect::Get(filters[i])) continue;
				auto* icon=icons[i];
				auto& translation=picker.inventoryStatusOffsets[mask][i];
				auto& moved=picker.inventoryStatusImages;
				const bool known=std::any_of(moved.begin(),moved.end(),
					[&](const auto& handle){return reflect::Get(handle)==icon;});
				if (initialTargets && !known) {
					inventoryheaders::Box bounds;
					const auto& target=(*initialTargets)[i];
					if (Bounds(icon,bounds)) {
						const double delta=target.x+target.w/2-bounds.x-bounds.w/2;
						if (std::abs(delta)<80) translation=delta;
					}
				}
				ui::Vec2(icon,L"SetRenderTranslation",translation,0);
				if (!known) {
					// The enchanted marker now comes from the native texture override.
					if (i == 1) {
						const auto* color = stolenfilter::kStolenFill;
						Brush(icon, L"Stolen-Item.png", true);
						ui::Colour(icon, color[0], color[1], color[2], 1.0f);
					}
					std::erase_if(moved,[](const auto& handle){return !reflect::Get(handle);});
					moved.push_back(reflect::Hold(icon));
				}
			}
		}

		void OnInventoryRowBound(UE::UObject* row,UE::UFunction* function,void* params)
		{
			auto& picker=g_pickers[0];
			if (picker.kind!=0 || !picker.active.load() || !picker.inventoryStatusReady ||
				!params || pe::FunctionName(function)!="OnListItemObjectSet") return;
			ProbeInventoryRowInk(row);
			const int offset=reflect::Offset(function,"ListItemObject");
			if (offset<0 || offset+int(sizeof(UE::UObject*))>function->parmsSize ||
				ui::SizeOf(function,"ListItemObject")!=sizeof(UE::UObject*)) return;
			UE::UObject* item=nullptr;
			std::memcpy(&item,static_cast<const std::uint8_t*>(params)+offset,sizeof(item));
			if (reflect::Usable(item) && IsInventoryStatusItem(item)) PlaceRowStatus(picker,row);
		}
		bool Place(UE::UObject*,UE::UObject* widget,const inventoryheaders::Box& box,double& tx,double& ty,bool* settled=nullptr)
		{
			if (!widget || box.w<=0) return false;
			inventoryheaders::Box current;
			if (!Bounds(widget, current)) return false;
			if (settled && (std::abs(box.x-current.x)>0.75 || std::abs(box.y-current.y)>0.75))
				*settled=false;
			// Keep the native ScrollBox slot and carousel membership. Render-only movement
			// preserves the bar's desired size and Blueprint/controller focus traversal.
			tx += box.x - current.x; ty += box.y - current.y;
			ui::Vec2(widget, L"SetRenderTranslation", tx, ty);
			return true;
		}
	}
	void Picker::SetInventoryHeaderOpacity(float opacity)
	{
		for (const auto& handle : sortHeaders)
			if (auto* widget=reflect::Get(handle)) ui::Float(widget,L"SetRenderOpacity",opacity);
		for (const auto& handle : {enchantedHeader,stolenHeader,header})
			if (auto* widget=reflect::Get(handle)) ui::Float(widget,L"SetRenderOpacity",opacity);
	}

	void Picker::HideInventoryHeaderLayout()
	{
		if (kind==0 || inventoryListReserved || inventoryHeaderReady) SetInventoryHeaderOpacity(0.0f);
		inventoryHeaderReady=false;
		inventoryHeaderFrame=-1;
		if (inventoryListReserved) {
			if (auto* slot=reflect::Get(inventoryListSlot)) ui::CallFirstGuarded(slot,inventoryListPadding?L"SetPadding":L"SetOffsets",inventoryListBase.data(),16);
			inventoryListReserved=false;
		}
		for (const auto& image : inventoryStatusImages)
			if (auto* widget=reflect::Get(image)) ui::Vec2(widget,L"SetRenderTranslation",0,0);
		inventoryStatusImages.clear();
		inventoryStatusReady=false;
		for (const auto& guide : inventoryHeaderGuides)
			if (auto* widget=reflect::Get(guide)) ui::Visible(widget,false);
		for (const auto& clip : inventoryClips) if (auto* widget=reflect::Get(clip.widget)) ui::CallFirstGuarded(widget,L"SetClipping",&clip.clipping,1);
		inventoryClips.clear();
	}

	void Picker::ClearInventoryHeaderLayout()
	{
		HideInventoryHeaderLayout();
		for (const auto& h : sortHeaders) if (auto* widget=reflect::Get(h)) ui::Vec2(widget,L"SetRenderTranslation",0,0);
		for (const auto& h : {enchantedHeader,stolenHeader,header}) if (auto* widget=reflect::Get(h)) ui::Vec2(widget,L"SetRenderTranslation",0,0);
		inventoryTranslations={};
		inventoryListSlot={};
		inventoryHeaderMeasured=false;
		inventoryHeaderFrame=-1;
		for (auto& guide : inventoryHeaderGuides) {
			if (auto* widget=reflect::Get(guide)) {
				ui::Call remove(widget,L"RemoveFromParent");
				remove.RunGuarded();
			}
			guide={};
		}
	}

	bool Picker::LayoutInventoryHeaders(UE::UObject* a_menu,UE::UObject* a_canvas,bool measureColumns)
	{
		if (kind!=0 || !OwnSorts()) return false;
		if (measureColumns) {
			if (auto* title = ui::FindInTree(a_menu, "inv_cont_title")) uix::Ink(title, kProbeInk);
			ui::Visible(ui::FindInTree(a_menu, "HeaderList_Separator"), false);
		}
		static bool announced=false;
		if (!announced) { announced=true; logger::info("Menus Revamped MR-INVENTORY-SINGLE-HEADER-R9"); }
		const double frameTime=ui::RealTimeSeconds();
		if (frameTime>=0 && frameTime==inventoryHeaderFrame) return true;
		auto* list=InventoryList(a_menu);
		inventoryheaders::Box bounds;
		if (!Bounds(list,bounds)) return false;
		if (!inventoryListReserved) {
			// The initial cached row rectangles describe the list before this inset.
			if (ReserveList(*this,list,bounds.y)) inventoryHeaderFrame=frameTime;
			return false;
		}
		const auto viewport=bounds;
		bounds.y=inventoryHeaderY;
		bounds.h+=inventoryheaders::kInset;
		double nameX=bounds.x+60;
		double rowsBottom=0;
		inventoryheaders::Box status;
		std::array<inventoryheaders::Box,inventoryheaders::kCount> columns{};
		std::vector<UE::UObject*> displayed;
		auto* row=FirstRow(list,viewport,rowsBottom,displayed);
		const bool measured=inventoryHeaderMeasured &&
			std::abs(inventoryMeasuredList.x-bounds.x)<0.5 &&
			std::abs(inventoryMeasuredList.w-bounds.w)<0.5;
		if (measured) {
			nameX=inventoryMeasuredNameX;
			status=inventoryMeasuredStatus;
			columns=inventoryMeasuredColumns;
		} else if (row && measureColumns) {
			inventoryheaders::Box name;
			const bool measuredName=Bounds(Column(row,"inv_entry_name"),name) ||
				Bounds(ui::FindInTree(row,"inv_entry_name"),name);
			if (measuredName) nameX=name.x;
			Bounds(Column(row,"inv_entry_statusIcons"),status);
			for (const auto& [key,field]:std::initializer_list<std::pair<itemsort::Key,const char*>>{
				{itemsort::Key::Health,"inv_entry_health"},{itemsort::Key::Damage,"inv_entry_weapon_damage"},
				{itemsort::Key::Armor,"inv_entry_armor_rating"},{itemsort::Key::Weight,"inv_entry_weight"},{itemsort::Key::Value,"inv_entry_value"}})
				Bounds(Column(row,field),columns[int(key)]);
			if (measuredName && columns[int(itemsort::Key::Value)].w>0) {
				inventoryMeasuredList=bounds;
				inventoryMeasuredNameX=nameX;
				inventoryMeasuredStatus=status;
				inventoryMeasuredColumns=columns;
				inventoryHeaderMeasured=true;
			}
		}
		if (!measureColumns && std::abs(inventoryMeasuredList.x-bounds.x)<0.5 &&
			std::abs(inventoryMeasuredList.w-bounds.w)<0.5) {
			nameX=inventoryMeasuredNameX;
			columns[int(itemsort::Key::Value)]=inventoryMeasuredColumns[int(itemsort::Key::Value)];
		}
		std::array<bool,inventoryheaders::kCount> offered{};
		for (auto key:SortKeys()) offered[int(key)]=true;
		const std::array filters{enchantedHeader,stolenHeader,header};
		const auto plan=inventoryheaders::Arrange(kind,bounds,nameX,columns,offered,3,status,rowsBottom);
		if (!plan.enabled || !ReserveList(*this,list,bounds.y)) return false;
		inventoryHeaderFrame=frameTime;
		bool settled=inventoryHeaderReady || inventoryHeaderMeasured || displayed.empty();
		// Allow the moved controls to extend beyond their native ScrollBox strip.
		const auto allowOverflow=[&](UE::UObject* widget) {
			for (int depth=0; widget && depth<12; ++depth) {
				if (const int off=reflect::Offset(widget->GetClass(),"Clipping"); off>=0 && ui::SizeOf(widget->GetClass(),"Clipping")==1) {
					const auto clipping=*reinterpret_cast<const std::uint8_t*>(reinterpret_cast<const std::uint8_t*>(widget)+off);
					if (clipping && std::none_of(inventoryClips.begin(),inventoryClips.end(),[&](const auto& c){return reflect::Get(c.widget)==widget;})) {
						inventoryClips.push_back({reflect::Hold(widget),clipping});
						const std::uint8_t inherit=0; ui::CallFirstGuarded(widget,L"SetClipping",&inherit,1);
					}
				}
				if (widget==a_menu) break;
				ui::Call parent(widget,L"GetParent");
				widget=parent && parent.RunGuarded()?parent.Get<UE::UObject*>("ReturnValue"):nullptr;
			}
		};
		for (int k=0;k<kSorts;++k) {
			if (!offered[k]) continue;
			auto* widget=reflect::Get(sortHeaders[k]);
			allowOverflow(widget);
			auto& translation=inventoryTranslations[k];
			if (!Place(nullptr,widget,plan.sorts[k],translation[0],translation[1],&settled)) return false;
			LayoutChild(reflect::Get(sortImages[k]),6,3,32,32); LayoutChild(reflect::Get(sortFrames[k]),0,0,44,40);
			LayoutChild(reflect::Get(sortBacks[k]),0,0,44,40);
			LayoutChild(reflect::Get(sortArrows[k]),34,25,9,9);
		}
		for (std::size_t i=0;i<filters.size();++i) {
			auto* widget=reflect::Get(filters[i]);
			if (!widget) continue;
			allowOverflow(widget);
			auto& translation=inventoryTranslations[kSorts+i];
			if (!Place(nullptr,widget,plan.filters[i],translation[0],translation[1],&settled)) return false;
		}
		LayoutChild(reflect::Get(headerImage),4,1,36,36); LayoutChild(reflect::Get(headerFrame),0,0,44,40);
		LayoutChild(reflect::Get(groupBack),0,0,44,40); LayoutChild(reflect::Get(groupArrow),32,27,10,10);
		for (const auto& frame : {stolenFrame,enchantedFrame})
			LayoutChild(reflect::Get(frame),0,0,30,40);

		if (!inventoryStatusReady && measureColumns && row &&
			pe::WatchPost(row->GetClass(),&OnInventoryRowBound)) {
			for (auto* entry : displayed) {
				ProbeInventoryRowInk(entry);
				PlaceRowStatus(*this,entry,&plan.filters);
			}
			auto* mainPart = a_menu;
			for (auto* owner = list; owner && reflect::Usable(owner); owner = owner->GetOuter()) {
				if (pe::Utf8(owner->GetClass()->GetFName().ToString()).starts_with("WBP_OriginalMenu_InventoryMainPart")) {
					mainPart = owner;
					break;
				}
			}
			constexpr std::array fields{ "inv_armor", "inv_money", "inv_weight" };
			for (std::size_t i = 0; i < fields.size(); ++i) {
				auto* footer = ui::FindInTree(mainPart, fields[i]);
				if (!footer && mainPart != a_menu) footer = ui::FindInTree(a_menu, fields[i]);
				g_probeFooter[i] = reflect::Hold(footer);
				if (!footer) continue;
				pe::WatchPost(footer->GetClass(), &OnProbeFooterUpdated);
				ProbeFooterInk(footer);
			}
			static bool reported = false;
			if (!reported) {
				reported = true;
				logger::warn("TEMP-DIAG MR native skin: bulk Inventory contrast");
			}
			inventoryStatusReady=true;
			StyleInventoryScrollbar(list);
		}

		inventoryheaders::Box canvasBounds;
		if (Bounds(a_canvas,canvasBounds)) {
			for (std::size_t i=0;i<plan.guides.size();++i) {
				const auto& rect=plan.guides[i];
				auto& handle=inventoryHeaderGuides[i];
				auto* guide=reflect::Get(handle);
				if (rect.w<=0 || rect.h<=0) {
					if (guide) ui::Visible(guide,false);
					continue;
				}
				if (!guide) {
					const auto name=L"MrInventoryHeaderGuide"+std::to_wstring(i);
					guide=CanvasChild(a_canvas,L"/Script/UMG.Image",name.c_str(),0,0,1,1);
					if (!guide) continue;
					handle=reflect::Hold(guide);
					const bool rule=i>=plan.guides.size()-2;
			constexpr float ink[]{0.55f,0.55f,0.55f,0.13f};
			constexpr float ruleInk[]{0.65f,0.65f,0.65f,0.55f};
					ui::RoundedBox(guide,false,rule?ruleInk:ink,kClear,0);
				}
				LayoutChild(guide,rect.x-canvasBounds.x,rect.y-canvasBounds.y,rect.w,rect.h);
				ui::Visible(guide,settled || inventoryHeaderReady);
			}
		}
		if (settled && !inventoryHeaderReady) {
			SetInventoryHeaderOpacity(1.0f);
			inventoryHeaderReady=true;
		}
		return true;
	}
}
