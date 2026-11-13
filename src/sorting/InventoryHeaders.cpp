// Inventory visuals reuse the same registered carousel wrappers; their controller identities stay intact.
#include "sorting/PickerInternal.h"
#include "sorting/rules/InventoryHeaderLayout.h"
#include "sorting/MenuSkin.h"
#include "engine/Reflect.h"
#include "engine/PEHook.h"
#include "engine/Ui.h"
#include "engine/UiText.h"
#include "engine/ContainerMenu.h"
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
		// The three filter slots of the header row (inventoryheaders::Plan::filters). Inventory: enchanted, stolen, category.
		// Magic spell pages: the Effect picker (g_pickers[1]), the Type picker (g_pickers[4]) and the School host itself;
		// the peers count only while they belong to the host's menu. Active Effects: none.
		// RMI-081: the strip a picker gave back this frame. The list's painted geometry is still the inset one until the next
		// paint, so a picker taking the strip over (Magic spell pages <-> Active Effects) takes this native top instead.
		struct Released { UE::UObject* slot = nullptr; double headerY = 0, time = -1; } g_released;
		// RMI-079: Magic labels and guides are removed on a clear and made again in the same menu; a removed widget lives on
		// until garbage collection and creating one with its name fails (probe MRMAGIC6). Every Magic widget gets a fresh name.
		unsigned g_widgetSerial = 0;
		struct LayoutFilter { reflect::Handle* widget = nullptr; Picker* owner = nullptr; bool category = false; };
		std::array<LayoutFilter,3> Filters(Picker& p)
		{
			if (p.InventoryStrip()) return {{{&p.enchantedHeader,&p,false},{&p.stolenHeader,&p,false},{&p.header,&p,true}}};
			if (p.kind!=1) return {};
			auto* menu=reflect::Get(p.menu);
			const auto peer=[&](Picker& q) {
				return menu && reflect::Get(q.menu)==menu && reflect::Get(q.header) ? LayoutFilter{&q.header,&q,true} : LayoutFilter{};
			};
			return {peer(g_pickers[1]),peer(g_pickers[4]),LayoutFilter{&p.header,&p,true}};
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
			// Magic: the spell-page host and the Active Effects picker share the list; whoever still holds the strip gives
			// the native padding back first, so the base read below is never an already-inset one.
			for (auto& other : g_pickers)
				if (&other!=&p && other.inventoryListReserved && reflect::Get(other.inventoryListSlot)==slot)
					other.HideInventoryHeaderLayout();
			p.HideInventoryHeaderLayout();
			// RMI-081: given back this frame (or the last): the measured top is still the inset one; use the native top.
			if (g_released.slot==slot && ui::RealTimeSeconds()-g_released.time<0.25) headerY=g_released.headerY;
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
		UE::UObject* Column(UE::UObject* row, std::string_view name, std::string_view box = "inv_entry_horizontal")
		{
			auto* horizontal = ui::FindInTree(row, box);   // Magic rows: Magic_entry_horizontal
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
				// The current translation first: the painted box includes it (probe MRROW2), so the native place is box - current.
				double current=0;
				if (const int off=reflect::Offset(icon->GetClass(),"RenderTransform"); off>=0)
					if (const auto* t=reflect::At<double>(icon,off)) current=t[0];
				// RMI-093: each mask is measured the first time a settled row shows it (probe MRROW: masks first seen after the
				// opening pass kept offset 0).
				if (initialTargets && !picker.inventoryStatusMeasured[mask][i]) {
					inventoryheaders::Box bounds;
					const auto& target=(*initialTargets)[i];
					if (Bounds(icon,bounds)) {
						const double delta=target.x+target.w/2-(bounds.x-current)-bounds.w/2;
						if (std::abs(delta)<80) { translation=delta; picker.inventoryStatusMeasured[mask][i]=true; }
					}
				}
				// Every layout pass re-places the displayed rows: only a changed translation is written.
				if (std::abs(current-translation)>0.01) ui::Vec2(icon,L"SetRenderTranslation",translation,0);
				if (!known) {
					// The enchanted dot and the stolen mark are baked textures (T_Icon_enchanted, T_Icon_stolen; Custom pack).
					std::erase_if(moved,[](const auto& handle){return !reflect::Get(handle);});
					moved.push_back(reflect::Hold(icon));
				}
			}
		}

		void OnInventoryRowBound(UE::UObject* row,UE::UFunction* function,void* params)
		{
			if (!params || pe::FunctionName(function)!="OnListItemObjectSet") return;
			// The Inventory or the container/barter side whose list made the row (entry widgets live in their list's tree).
			Picker* owner=nullptr;
			// Container/barter: both sides share one menu, so a row belongs to the side whose content holds it (RMI-093).
			for (auto& p : g_pickers) {
				if (!p.InventoryStrip() || !p.active.load() || !p.inventoryStatusReady) continue;
				auto* scope=p.kind==3 || p.kind==4?container::SideContent(p.kind):reflect::Get(p.menu);
				if (scope && ui::IsWithin(row,scope)) { owner=&p; break; }
			}
			if (!owner) return;
			auto& picker=*owner;
			const int offset=reflect::Offset(function,"ListItemObject");
			if (offset<0 || offset+int(sizeof(UE::UObject*))>function->parmsSize ||
				ui::SizeOf(function,"ListItemObject")!=sizeof(UE::UObject*)) return;
			UE::UObject* item=nullptr;
			std::memcpy(&item,static_cast<const std::uint8_t*>(params)+offset,sizeof(item));
			// RMI-093: container/barter rows too (the item filter is the Inventory's: bIsInventoryItem && !bIsInContainerMenu)
			if (reflect::Usable(item) && (picker.kind!=0 || IsInventoryStatusItem(item))) PlaceRowStatus(picker,row);
		}
		// Phase 2 (Dark Magic): Magic rows are generated by the list; restyled whenever the game rebinds one.
		void OnMagicRowBound(UE::UObject* row,UE::UFunction* function,void*)
		{
			if (pe::FunctionName(function)=="OnListItemObjectSet") menuskin::StyleMagicRow(row);
		}
		// RMI-079 (owner 2026-11-09): with the Magic header row the native page title is not drawn any more, although its text,
		// colour, opacity and visibility are unchanged and nothing covers it (probes MRMAGIC4/5; cause unknown). As for the
		// Inventory (RMI-073), our own label shows the title in the native font and colour and the native one is hidden.
		void PlaceMagicTitleLabel(Picker& picker,UE::UObject* menu,UE::UObject* canvas,const inventoryheaders::Box& canvasBounds)
		{
			auto* text=reflect::Get(picker.magicTitleText);
			auto* title=reflect::Get(picker.titleNative);
			if (!text || !title) {
				title=ui::FindInTree(menu,"Magic_TabName");
				text=title?ui::FindByClassPrefix(title,"VAltarTextBlock",20):nullptr;
				if (!text) return;
				picker.titleNative=reflect::Hold(title);
				picker.magicTitleText=reflect::Hold(text);
			}
			// The native title box is squeezed to 0 width by the wider filter row in the shared Header_Box (probe MRMAGIC8), so the
			// label is placed from the title row itself: the title text starts 18 units in (164.4 in a row at 146.4).
			inventoryheaders::Box row;
			if (!Bounds(ui::FindInTree(menu,"Header_Box"),row)) return;
			const inventoryheaders::Box native{row.x+18,row.y,inventoryheaders::kTitleLabelWidth,row.h};
			auto* label=reflect::Get(picker.titleLabel);
			if (!label) {
				// One label per Magic header (kind 1 spell pages, 6 Active Effects): NewObject with a name already used in this
				// widget tree replaces that widget in place, so a shared name broke the other header's label (owner 2026-11-09).
				const auto labelName=L"MrMagicTitleLabel"+std::to_wstring(picker.kind)+L"_"+std::to_wstring(g_widgetSerial++);
				label=CanvasChild(canvas,L"/Script/UMG.TextBlock",labelName.c_str(),0,0,1,1,3);
				if (!label) return;
				picker.titleLabel=reflect::Hold(label);
				if (const auto off=reflect::Offset(text->GetClass(),"Font"), size=ui::SizeOf(text->GetClass(),"Font"); off>=0 && size>0 && size<=512)
					ui::CallFirst(label,L"SetFont",reinterpret_cast<const std::uint8_t*>(text)+off,static_cast<std::size_t>(size));
				// The native colour, fully opaque: a label made during a tab switch (Active Effects, or back from it) copied the
				// title while the game faded it, and stayed transparent (owner 2026-11-09; label logged visible, not seen).
				if (const auto off=reflect::Offset(text->GetClass(),"ColorAndOpacity"), size=ui::SizeOf(text->GetClass(),"ColorAndOpacity");
					off>=0 && size>=16 && size<=64) {
					std::array<std::uint8_t,64> bytes{};
					std::memcpy(bytes.data(),reinterpret_cast<const std::uint8_t*>(text)+off,static_cast<std::size_t>(size));
					auto* colour=reinterpret_cast<float*>(bytes.data());   // FSlateColor: FLinearColor SpecifiedColor first
					colour[3]=1.0f;
					ui::CallFirst(label,L"SetColorAndOpacity",bytes.data(),static_cast<std::size_t>(size));
				}
				menuskin::InkText(label);
			}
			picker.SyncMagicTitle();
			// Vertically centred in the title row like the native text; left edge on the native box.
			double h=native.h;
			ui::Call size(label,L"GetDesiredSize");
			if (size && size.Size("ReturnValue")==16 && size.RunGuarded()) {
				const auto desired=size.Get<std::array<double,2>>("ReturnValue")[1];
				if (desired>1 && desired<native.h) h=desired;
			}
			LayoutChild(label,native.x-canvasBounds.x,native.y+(native.h-h)/2-canvasBounds.y,
				std::max(native.w,inventoryheaders::kTitleLabelWidth),h);
			ui::Visible(label,true);
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
		if (!InventoryStrip() || !inventoryHeaderReady) return;
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

	// RMI-079: the page name changes in the Magic post-event; the label copies it on the next tick, not the 150 ms layout timer.
	void Picker::SyncMagicTitle()
	{
		auto* label=reflect::Get(titleLabel);
		auto* text=reflect::Get(magicTitleText);
		if (!label || !text) return;
		const auto want=uix::GetText(text);
		if (!want.empty() && uix::GetText(label)!=want) uix::SetText(label,want);
	}

	void Picker::SetInventoryHeaderOpacity(float opacity)
	{
		for (const auto& handle : sortHeaders)
			if (auto* widget=reflect::Get(handle)) ui::Float(widget,L"SetRenderOpacity",opacity);
		for (const auto& handle : {enchantedHeader,stolenHeader,header})
			if (auto* widget=reflect::Get(handle)) ui::Float(widget,L"SetRenderOpacity",opacity);
		for (const auto& filter : Filters(*this))   // Magic: the Effect and Type pickers fade with their host
			if (filter.owner && filter.owner!=this)
				if (auto* widget=reflect::Get(*filter.widget)) ui::Float(widget,L"SetRenderOpacity",opacity);
	}

	void Picker::SetInventoryFillOpacity(float opacity)
	{
		const auto apply = [&](const reflect::Handle& handle) {
			if (auto* widget = reflect::Get(handle)) ui::Float(widget, L"SetRenderOpacity", opacity);
		};
		for (const auto& frame : sortFrames) apply(frame);
		for (const auto& back : sortBacks) apply(back);
		for (const auto& fill : {headerFrame, groupBack, stolenFrame, enchantedFrame}) apply(fill);
		for (const auto& filter : Filters(*this))
			if (filter.owner && filter.owner!=this) { apply(filter.owner->headerFrame); apply(filter.owner->groupBack); }
	}

	void Picker::HideInventoryHeaderLayout()
	{
		if (InventoryStrip() || inventoryListReserved || inventoryHeaderReady) SetInventoryHeaderOpacity(0.0f);
		inventoryHeaderReady=false;
		inventoryFillPending=true;
		inventoryHeaderFrame=-1;
		if (inventoryListReserved) {
			if (auto* slot=reflect::Get(inventoryListSlot)) {
				ui::CallFirstGuarded(slot,inventoryListPadding?L"SetPadding":L"SetOffsets",inventoryListBase.data(),16);
				g_released={slot,inventoryHeaderY,ui::RealTimeSeconds()};   // RMI-081: for a picker taking the strip over
			}
			inventoryListReserved=false;
		}
		for (const auto& image : inventoryStatusImages)
			if (auto* widget=reflect::Get(image)) ui::Vec2(widget,L"SetRenderTranslation",0,0);
		inventoryStatusImages.clear();
		inventoryStatusMeasured={};
		inventoryStatusReady=false;
		// RMI-079: spell pages and Active Effects each have a Magic title label; only the shown header's label is drawn.
		if (kind==1 || kind==6) if (auto* label=reflect::Get(titleLabel)) {
			ui::Visible(label,false);
		}
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
		for (const auto& filter : Filters(*this))   // Magic: the peers' pickers go back to their native slots too
			if (filter.owner && filter.owner!=this)
				if (auto* widget=reflect::Get(*filter.widget)) ui::Vec2(widget,L"SetRenderTranslation",0,0);
		inventoryTranslations={};
		inventoryTabMeasures.Clear();
		if (auto* hint=reflect::Get(inventoryHint)) ui::Vec2(hint,L"SetRenderTranslation",0,0);   // the glyph's native place (RMI-078)
		inventoryHint={};
		inventoryHintTranslation={};
		inventoryListSlot={};
		inventoryHeaderMeasured=false;
		inventoryHeaderFrame=-1;
		if (auto* label=reflect::Get(titleLabel)) {   // the Magic title label goes; the native title shows again
			ui::Call remove(label,L"RemoveFromParent");
			remove.RunGuarded();
		}
		titleLabel={};
		if (auto* text=reflect::Get(titleNative)) ui::Float(text,L"SetRenderOpacity",1.0f);
		titleNative={};
		magicTitleText={};
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
		if (!HeaderLayout() || !OwnSorts()) return false;
		const bool magic=kind==1 || kind==6;   // Magic spell pages (School host) or Active Effects; else the Inventory strip
		if (measureColumns) {
			// Magic: our own rules replace its separator. The Inventory strip keeps the game's HeaderList_Separator as its upper
			// rule (owner 2026-11-13; probe MRSEP: it sits 2 units above where our upper rule was, full list width).
			if (magic) ui::Visible(ui::FindInTree(a_menu, "HeaderList_Separator"), false);
		}
		static bool announced=false, announcedMagic=false, announcedContainer=false;
		if (kind==0 && !announced) { announced=true; logger::info("Menus Revamped MR-INVENTORY-TITLE-BAKED-T4"); }
		if ((kind==3 || kind==4) && !announcedContainer) { announcedContainer=true; logger::info("Menus Revamped MR-CONTAINER-SINGLE-HEADER-C1K"); }
		if (magic && !announcedMagic) { announcedMagic=true; logger::info("Menus Revamped MR-MAGIC-SINGLE-HEADER-R10"); }
		const double frameTime=ui::RealTimeSeconds();
		if (frameTime>=0 && frameTime==inventoryHeaderFrame) return true;
		if (magic) {   // RMI-079: the title label before anything that can wait a frame (the strip handover on a page change)
			inventoryheaders::Box canvasBox;
			if (Bounds(a_canvas,canvasBox)) PlaceMagicTitleLabel(*this,a_menu,a_canvas,canvasBox);
		}
		auto* list=magic?ui::FindInTree(a_menu,"Magic_Spell_List"):InventoryList(a_menu);
		// Magic: the strip is made on the list's retainer (its VerticalBox slot has the padding); the list view is inside it.
		auto* reserved=magic?ui::FindInTree(a_menu,"SpellListRetainerBox"):list;
		if (!reserved) reserved=list;
		inventoryheaders::Box bounds;
		if (!Bounds(list,bounds)) return false;
		if (!inventoryListReserved) {
			// The initial cached row rectangles describe the list before this inset.
			if (ReserveList(*this,reserved,bounds.y)) inventoryHeaderFrame=frameTime;
			return false;
		}
		const auto viewport=bounds;
		bounds.y=inventoryHeaderY;
		bounds.h+=inventoryheaders::kInset;
		double nameX=inventoryheaders::DefaultNameX(kind,bounds);
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
		auto stored=inventoryTabMeasures.For(tabIndex,bounds,&exact);
		// RMI-092: a container/barter side's other tab gives its name place only (each tab packs its own columns; the Inventory
		// keeps RMI-077's estimate, owner-accepted).
		if (!exact && stored && (kind==3 || kind==4)) stored=inventoryheaders::OtherTabEstimate(*stored);
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
			if (row && measureColumns && magic) {
				// Magic rows: the name, and the cost (spell pages) / magnitude (Active Effects) column at the row end.
				inventoryheaders::Box name;
				const bool measuredName=Bounds(Column(row,"Magic_entry_name","Magic_entry_horizontal"),name) ||
					Bounds(ui::FindInTree(row,"Magic_entry_name"),name);
				if (measuredName) nameX=name.x;
				const auto last=kind==1?itemsort::Key::Cost:itemsort::Key::Magnitude;
				// RMI-080 (probe 2026-11-09): the named row boxes, not Magic_entry_CostMagnitude (the number text inside
				// Cost_SizeBox, about 33 units right of the column's start). Effect_SizeBox is empty (0 wide) on spell pages.
				const bool namedCost=Bounds(Column(row,"Cost_SizeBox","Magic_entry_horizontal"),columns[int(last)]);
				if (namedCost) {
					Bounds(Column(row,"School_SizeBox","Magic_entry_horizontal"),columns[int(itemsort::Key::School)]);
					Bounds(Column(row,"SpellType_SizeBox","Magic_entry_horizontal"),columns[int(itemsort::Key::Category)]);
					if (kind==6) Bounds(Column(row,"Effect_SizeBox","Magic_entry_horizontal"),columns[int(itemsort::Key::EffectValue)]);
				} else if (!Bounds(Column(row,"Magic_entry_CostMagnitude","Magic_entry_horizontal"),columns[int(last)]))
					Bounds(ui::FindInTree(row,"Magic_entry_CostMagnitude"),columns[int(last)]);
				if (measuredName && columns[int(last)].w>0) {
					inventoryMeasuredList=bounds;
					inventoryMeasuredNameX=nameX;
					inventoryMeasuredStatus=status;
					inventoryMeasuredColumns=columns;
					inventoryHeaderMeasured=true;
					inventoryTabMeasures.Store(tabIndex,{bounds,status,columns,nameX});
				}
			}
			if (row && measureColumns && !magic) {
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
		const auto slots=Filters(*this);
		const std::array filters{slots[0].widget?*slots[0].widget:reflect::Handle{},slots[1].widget?*slots[1].widget:reflect::Handle{},
			slots[2].widget?*slots[2].widget:reflect::Handle{}};
		// The Inventory strip without a stolen toggle (the container/barter other side, or the INI off): no stolen cell.
		const int filterCount=kind==6?0:InventoryStrip() && !reflect::Get(stolenHeader)?2:3;
		const auto plan=inventoryheaders::Arrange(kind,bounds,nameX,columns,offered,filterCount,status,rowsBottom);
		if (!plan.enabled || !ReserveList(*this,reserved,bounds.y)) return false;
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
			if (!widget || (slots[i].owner && !slots[i].owner->active.load())) continue;
			allowOverflow(widget);
			auto& translation=inventoryTranslations[kSorts+i];
			if (!Place(nullptr,widget,plan.filters[i],translation[0],translation[1],&settled)) return false;
		}
		if (!magic) {
		{ const auto& i=inventoryheaders::kCategoryIcon; LayoutChild(reflect::Get(headerImage),i.x,i.y,i.w,i.h); } placeFill(headerFrame,plan.filters[2]);
		placeFill(groupBack,plan.filters[2]); LayoutChild(reflect::Get(groupArrow),32,27,10,10);
		placeFill(enchantedFrame,plan.filters[0]);
		placeFill(stolenFrame,plan.filters[1]);
		} else {
			// Magic spell pages: each picker (Effect, Type, School) is drawn like the Inventory category picker.
			for (std::size_t i=0;i<slots.size();++i) {
				auto* owner=slots[i].owner;
				if (!owner || !owner->active.load()) continue;
				{ const auto& c=inventoryheaders::kCategoryIcon; LayoutChild(reflect::Get(owner->headerImage),c.x,c.y,c.w,c.h); }
				placeFill(owner->headerFrame,plan.filters[i]);
				placeFill(owner->groupBack,plan.filters[i]);
				LayoutChild(reflect::Get(owner->groupArrow),32,27,10,10);
				// A peer created after the host settled still has its creation opacity 0: show it with the settled row.
				if (owner!=this && (settled || inventoryHeaderReady))
					if (auto* widget=reflect::Get(*slots[i].widget); widget && ui::RenderOpacity(widget)<1.0f)
						ui::Float(widget,L"SetRenderOpacity",1.0f);
			}
		}
		// RMI-078: the native controller Y glyph stays in the native filter row, behind the tab title; it goes to the row's end.
		// Magic: the same glyph sits in Magic_Filters > Filter_InputHint_Horizontalbox, left of the title-row filters.
		auto* hint=reflect::Get(inventoryHint);
		if (!hint) {
			auto* filter=ui::FindInTree(a_menu,magic?"Magic_Filters":"inv_cont_filter");
			hint=filter?ui::FindByClassPrefix(filter,"WBP_ModernPrefab_ControllerInputHint"):nullptr;
			if (hint) inventoryHint=reflect::Hold(hint);
			else {
				static bool s_warned=false;
				if (!s_warned) { s_warned=true; logger::warn("Menus Revamped: {} filter Y glyph (WBP_ModernPrefab_ControllerInputHint) not found", magic?"Magic":kind==0?"Inventory":"container"); }
			}
		}
		inventoryheaders::Box hintBounds;
		if (hint && Bounds(hint,hintBounds)) {   // hidden without a pad: nothing to place
			allowOverflow(hint);
			// Magic: the spell-page host and the Active Effects picker move the same glyph; start from its real translation.
			if (magic)
				if (const int off=reflect::Offset(hint->GetClass(),"RenderTransform"); off>=0)
					if (const auto* t=reflect::At<double>(hint,off)) inventoryHintTranslation={t[0],t[1]};
			Place(nullptr,hint,inventoryheaders::HintBox(plan,hintBounds),inventoryHintTranslation[0],inventoryHintTranslation[1]);
		}

		if (!magic && !inventoryStatusReady && measureColumns && row &&   // Inventory row status and skin hooks only
			pe::WatchPost(row->GetClass(),&OnInventoryRowBound))
			inventoryStatusReady=true;   // item details and scrollbar colours: baked in the Custom pack (decision 28, Phase 7)
		// RMI-093: the displayed rows every pass (a row's icons can change after the game binds it); a new mask is measured
		// once the row has settled.
		if (!magic && inventoryStatusReady && measureColumns)
			for (auto* entry : displayed) PlaceRowStatus(*this,entry,settled || inventoryHeaderReady?&plan.filters:nullptr);

		if (magic) {   // Dark Magic (phase 2): the parchment page and rows (details and scrollbar: baked, phase 28b / Phase 7); nothing in Classic/Light
			menuskin::StyleMagicPage(a_menu,measureColumns);
			if (menuskin::Current().recolor && !inventoryStatusReady && measureColumns && row &&
				pe::WatchPost(row->GetClass(),&OnMagicRowBound)) {
				static bool s_announced=false;
				if (!s_announced) { s_announced=true; logger::info("Menus Revamped MR-MAGIC-DARK-R11"); }
				for (auto* entry : displayed) menuskin::StyleMagicRow(entry);
				inventoryStatusReady=true;
			}
		}

		inventoryheaders::Box canvasBounds;
		if (Bounds(a_canvas,canvasBounds)) {
			for (std::size_t i=0;i<plan.guides.size();++i) {
				const auto& rect=plan.guides[i];
				auto& handle=inventoryHeaderGuides[i];
				auto* guide=reflect::Get(handle);
				const bool nativeUpper=i==std::size_t(inventoryheaders::kGuides-2) && !inventoryheaders::DrawsUpperRule(kind);
				if (rect.w<=0 || rect.h<=0 || nativeUpper) {   // the game's separator is the Inventory strip's upper rule
					if (guide) ui::Visible(guide,false);
					continue;
				}
				if (!guide) {
					// Magic: per header kind (a shared name in one widget tree replaces the other header's guide in place).
					// Container/barter: both sides share one canvas, so each side's guides get their own names.
					const auto name=magic?L"MrMagicHeaderGuide"+std::to_wstring(kind)+L"_"+std::to_wstring(i)+L"_"+std::to_wstring(g_widgetSerial++)
						:kind!=0?L"MrContainerHeaderGuide"+std::to_wstring(kind)+L"_"+std::to_wstring(i)+L"_"+std::to_wstring(g_widgetSerial++)
						:L"MrInventoryHeaderGuide"+std::to_wstring(i);
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
