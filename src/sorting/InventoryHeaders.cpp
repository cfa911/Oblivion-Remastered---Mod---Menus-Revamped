// Inventory visuals reuse the same registered carousel wrappers; their controller identities stay intact.
#include "sorting/PickerInternal.h"
#include "sorting/rules/InventoryHeaderLayout.h"
#include "sorting/MenuSkin.h"
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
		UE::UObject* FirstRow(UE::UObject* list,const inventoryheaders::Box& viewport,
			double& rowsBottom,std::vector<UE::UObject*>& displayed,int& items)
		{
			for (const wchar_t* getter:{L"GetListView",L"GetItemListView"}) {
				ui::Call get(list,getter);
				if (get && get.Size("ReturnValue")==8 && get.RunGuarded())
					if (auto* inner=get.Get<UE::UObject*>("ReturnValue")) { list=inner; break; }
			}
			ui::Call number(list,L"GetNumItems");
			if (number && number.Size("ReturnValue")==4 && number.RunGuarded()) items=number.Get<int>("ReturnValue");
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
					if (i == 1 && menuskin::Current().recolor) {
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
			menuskin::StyleRow(row, menuskin::kInventory);
			const int offset=reflect::Offset(function,"ListItemObject");
			if (offset<0 || offset+int(sizeof(UE::UObject*))>function->parmsSize ||
				ui::SizeOf(function,"ListItemObject")!=sizeof(UE::UObject*)) return;
			UE::UObject* item=nullptr;
			std::memcpy(&item,static_cast<const std::uint8_t*>(params)+offset,sizeof(item));
			if (reflect::Usable(item) && IsInventoryStatusItem(item)) PlaceRowStatus(picker,row);
		}
		// RMI-073: the native tab title cuts "Miscellaneous" at 156-158 units whatever width is set on it. Our own text sits on
		// it with the native font and text; the native text is hidden by render opacity (its layout and animations stay).
		void PlaceTitleLabel(Picker& picker,UE::UObject* menu,UE::UObject* canvas,const inventoryheaders::Box& canvasBounds)
		{
			auto* title=ui::FindInTree(menu,"inv_cont_title");
			auto* text=title?ui::FindInTree(title,"text_block"):nullptr;
			inventoryheaders::Box native;
			if (!text || !Bounds(text,native)) return;
			auto* label=reflect::Get(picker.titleLabel);
			const bool created=!label;
			if (!label) {
				label=CanvasChild(canvas,L"/Script/UMG.TextBlock",L"MrInventoryTitleLabel",0,0,1,1,3);
				if (!label) return;
				picker.titleLabel=reflect::Hold(label);
				menuskin::SetTitleLabel(label);
			}
			picker.titleNative=reflect::Hold(title);
			const auto box=inventoryheaders::TitleLabel(native);
			LayoutChild(label,box.x-canvasBounds.x,box.y-canvasBounds.y,box.w,box.h);
			if (created) {
				for (const char* prop : {"Font","ColorAndOpacity"}) {   // the native text's own look
					const auto off=reflect::Offset(text->GetClass(),prop);
					const auto size=ui::SizeOf(text->GetClass(),prop);
					if (off>=0 && size>0 && size<=512)
						ui::CallFirst(label,prop==std::string_view("Font")?L"SetFont":L"SetColorAndOpacity",
							reinterpret_cast<const std::uint8_t*>(text)+off,static_cast<std::size_t>(size));
				}
				menuskin::InkText(label);
				const auto want=uix::GetText(text);
				if (uix::GetText(label)!=want) uix::SetText(label,want);
			}
			if (ui::RenderOpacity(title)>0) ui::Float(title,L"SetRenderOpacity",0.0f);
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
	// RMI-077: the game resizes the A-Z wrapper per tab in a post-event, and the next paint moves every later FilterScrollbox
	// slot by the difference (probe 2026-11-09). Move those controls' render translations back by the same amount first.
	void Picker::CompensateInventorySlots()
	{
		if (kind!=0 || !inventoryHeaderReady) return;
		ui::Call parent(reflect::Get(header),L"GetParent");
		auto* panel=parent && parent.RunGuarded()?parent.Get<UE::UObject*>("ReturnValue"):nullptr;
		ui::Call count(panel,L"GetChildrenCount");
		if (!count || !count.RunGuarded()) return;
		const int n=std::min(count.Get<int>("ReturnValue"),inventoryheaders::kPanelChildren);
		std::array<UE::UObject*,inventoryheaders::kPanelChildren> children{};
		std::array<double,inventoryheaders::kPanelChildren> painted{},desired{};
		painted.fill(-1); desired.fill(-1);
		bool resized=false;
		for (int i=0;i<n;++i) {
			ui::Call get(panel,L"GetChildAt");
			if (!get.Set("Index",i) || !get.RunGuarded()) return;
			children[i]=get.Get<UE::UObject*>("ReturnValue");
			ui::Call size(children[i],L"GetDesiredSize");
			if (size && size.Size("ReturnValue")==16 && size.RunGuarded()) desired[i]=size.Get<std::array<double,2>>("ReturnValue")[0];
			resized=resized || std::abs(desired[i]-inventorySlotWidths[i])>=0.5;
		}
		if (!resized) return;   // the common case: one desired-width read per child, nothing else
		for (int i=0;i<n;++i) {
			double x=0,y=0,w=0,h=0;
			if (children[i] && ui::Measure(children[i],x,y,w,h)) painted[i]=w;
		}
		const auto shift=inventoryheaders::SlotShifts(inventorySlotWidths,painted,desired,n);
		const std::array filters{enchantedHeader,stolenHeader,header};
		for (int i=0;i<n;++i) {
			if (!children[i] || std::abs(shift[i])<0.5) continue;
			int slot=-1;
			for (int k=0;k<kSorts;++k) if (reflect::Get(sortHeaders[k])==children[i]) slot=k;
			for (int f=0;f<3;++f) if (reflect::Get(filters[f])==children[i]) slot=kSorts+f;
			if (slot<0) continue;
			auto& translation=inventoryTranslations[slot];
			translation[0]-=shift[i];
			ui::Vec2(children[i],L"SetRenderTranslation",translation[0],translation[1]);
		}
	}

	void Picker::SetInventoryHeaderOpacity(float opacity)
	{
		for (const auto& handle : sortHeaders)
			if (auto* widget=reflect::Get(handle)) ui::Float(widget,L"SetRenderOpacity",opacity);
		for (const auto& handle : {enchantedHeader,stolenHeader,header})
			if (auto* widget=reflect::Get(handle)) ui::Float(widget,L"SetRenderOpacity",opacity);
	}

	void Picker::SetInventoryFillOpacity(float opacity)
	{
		const auto apply = [&](const reflect::Handle& handle) {
			if (auto* widget = reflect::Get(handle)) ui::Float(widget, L"SetRenderOpacity", opacity);
		};
		for (const auto& frame : sortFrames) apply(frame);
		for (const auto& back : sortBacks) apply(back);
		for (const auto& fill : {headerFrame, groupBack, stolenFrame, enchantedFrame}) apply(fill);
	}

	void Picker::HideInventoryHeaderLayout()
	{
		if (kind==0 || inventoryListReserved || inventoryHeaderReady) SetInventoryHeaderOpacity(0.0f);
		inventoryHeaderReady=false;
		inventoryFillPending=true;
		inventoryHeaderFrame=-1;
		if (inventoryListReserved) {
			if (auto* slot=reflect::Get(inventoryListSlot)) ui::CallFirstGuarded(slot,inventoryListPadding?L"SetPadding":L"SetOffsets",inventoryListBase.data(),16);
			inventoryListReserved=false;
		}
		for (const auto& image : inventoryStatusImages)
			if (auto* widget=reflect::Get(image)) ui::Vec2(widget,L"SetRenderTranslation",0,0);
		inventoryStatusImages.clear();
		inventoryStatusReady=false;
		inventoryRowsBottom=0;
		inventorySlotWidths.fill(-1);
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
		inventoryTabMeasures.Clear();
		if (auto* hint=reflect::Get(inventoryHint)) ui::Vec2(hint,L"SetRenderTranslation",0,0);   // the glyph's native place (RMI-078)
		inventoryHint={};
		inventoryHintTranslation={};
		inventoryListSlot={};
		inventoryHeaderMeasured=false;
		inventoryHeaderFrame=-1;
		if (auto* label=reflect::Get(titleLabel)) {   // our title label goes; the native title shows again
			ui::Call remove(label,L"RemoveFromParent");
			remove.RunGuarded();
		}
		titleLabel={};
		menuskin::SetTitleLabel(nullptr);
		if (auto* text=reflect::Get(titleNative)) ui::Float(text,L"SetRenderOpacity",1.0f);
		titleNative={};
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
			menuskin::StyleTitle(ui::FindInTree(a_menu, "inv_cont_title"));
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
		int items=-1;   // unreadable: treated as rows still coming
		auto* row=FirstRow(list,viewport,rowsBottom,displayed,items);
		// RMI-077: the list guides keep their height while the new tab's rows are not laid out yet.
		rowsBottom=inventoryRowsBottom=inventoryheaders::RowsBottom(rowsBottom,inventoryRowsBottom,items);
		// RMI-077: this tab's own earlier measurement is final at once; another tab's is the starting estimate.
		const auto tabIndex=static_cast<std::size_t>(tab);
		bool exact=false;
		const auto stored=inventoryTabMeasures.For(tabIndex,bounds,&exact);
		if (!inventoryHeaderMeasured && stored && exact) {
			inventoryMeasuredList=stored->list;
			inventoryMeasuredNameX=stored->nameX;
			inventoryMeasuredStatus=stored->status;
			inventoryMeasuredColumns=stored->columns;
			inventoryHeaderMeasured=true;
		}
		const bool measured=inventoryHeaderMeasured &&
			std::abs(inventoryMeasuredList.x-bounds.x)<0.5 &&
			std::abs(inventoryMeasuredList.w-bounds.w)<0.5;
		if (measured) {
			nameX=inventoryMeasuredNameX;
			status=inventoryMeasuredStatus;
			columns=inventoryMeasuredColumns;
		} else {
			if (stored) { nameX=stored->nameX; status=stored->status; columns=stored->columns; }
			if (row && measureColumns) {
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
				inventoryTabMeasures.Store(tabIndex,{bounds,status,columns,nameX});
			}
			}
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
		const auto placeFill = [&](const reflect::Handle& frame, inventoryheaders::Box control) {
			const auto fill = inventoryheaders::ControlFill(plan, control);
			LayoutChild(reflect::Get(frame), fill.x, fill.y, fill.w, fill.h);
		};
		for (int k=0;k<kSorts;++k) {
			if (!offered[k]) continue;
			auto* widget=reflect::Get(sortHeaders[k]);
			allowOverflow(widget);
			auto& translation=inventoryTranslations[k];
			if (!Place(nullptr,widget,plan.sorts[k],translation[0],translation[1],&settled)) return false;
			{ const auto& i=inventoryheaders::kSortIcon; LayoutChild(reflect::Get(sortImages[k]),i.x,i.y,i.w,i.h); } placeFill(sortFrames[k],plan.sorts[k]);
			placeFill(sortBacks[k],plan.sorts[k]);
			LayoutChild(reflect::Get(sortArrows[k]),34,25,9,9);
		}
		for (std::size_t i=0;i<filters.size();++i) {
			auto* widget=reflect::Get(filters[i]);
			if (!widget) continue;
			allowOverflow(widget);
			auto& translation=inventoryTranslations[kSorts+i];
			if (!Place(nullptr,widget,plan.filters[i],translation[0],translation[1],&settled)) return false;
		}
		{ const auto& i=inventoryheaders::kCategoryIcon; LayoutChild(reflect::Get(headerImage),i.x,i.y,i.w,i.h); } placeFill(headerFrame,plan.filters[2]);
		placeFill(groupBack,plan.filters[2]); LayoutChild(reflect::Get(groupArrow),32,27,10,10);
		placeFill(enchantedFrame,plan.filters[0]);
		placeFill(stolenFrame,plan.filters[1]);
		// RMI-078: the native controller Y glyph stays in the native filter row, behind the tab title; it goes to the row's end.
		auto* hint=reflect::Get(inventoryHint);
		if (!hint) {
			auto* filter=ui::FindInTree(a_menu,"inv_cont_filter");
			hint=filter?ui::FindByClassPrefix(filter,"WBP_ModernPrefab_ControllerInputHint"):nullptr;
			if (hint) inventoryHint=reflect::Hold(hint);
			else {
				static bool s_warned=false;
				if (!s_warned) { s_warned=true; logger::warn("Menus Revamped: Inventory filter Y glyph (WBP_ModernPrefab_ControllerInputHint) not found"); }
			}
		}
		inventoryheaders::Box hintBounds;
		if (hint && Bounds(hint,hintBounds)) {   // hidden without a pad: nothing to place
			allowOverflow(hint);
			Place(nullptr,hint,inventoryheaders::HintBox(plan,hintBounds),inventoryHintTranslation[0],inventoryHintTranslation[1]);
		}

		if (!inventoryStatusReady && measureColumns && row &&
			pe::WatchPost(row->GetClass(),&OnInventoryRowBound)) {
			for (auto* entry : displayed) {
				menuskin::StyleRow(entry, menuskin::kInventory);
				PlaceRowStatus(*this,entry,&plan.filters);
			}
			auto* mainPart = a_menu;
			for (auto* owner = list; owner && reflect::Usable(owner); owner = owner->GetOuter()) {
				if (pe::Utf8(owner->GetClass()->GetFName().ToString()).starts_with("WBP_OriginalMenu_InventoryMainPart")) {
					mainPart = owner;
					break;
				}
			}
			menuskin::WatchFooters(mainPart, a_menu, menuskin::kInventory);
			inventoryStatusReady=true;
			menuskin::StyleListScrollbar(list);
			menuskin::WatchDetails(a_menu, list, menuskin::kInventory);
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
					const bool rule=plan.rule[i];
					const auto& skin=menuskin::Current();
					ui::RoundedBox(guide,false,rule?skin.rule.data():skin.guide.data(),kClear,0);
				}
				LayoutChild(guide,rect.x-canvasBounds.x,rect.y-canvasBounds.y,rect.w,rect.h);
				ui::Visible(guide,settled || inventoryHeaderReady);
			}
			PlaceTitleLabel(*this,a_menu,a_canvas,canvasBounds);
		}
		if (inventoryFillPending) {
			const bool reveal = inventoryheaders::CanRevealControlFills(measureColumns, settled);
			SetInventoryFillOpacity(reveal ? 1.0f : 0.0f);
			inventoryFillPending = !reveal;
		}
		if (settled && !inventoryHeaderReady) {
			SetInventoryHeaderOpacity(1.0f);
			inventoryHeaderReady=true;
		}
		return true;
	}
}
