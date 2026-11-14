#pragma once
#include "sorting/rules/ItemSort.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <optional>
#include <string_view>
namespace inventoryheaders
{
	struct Box { double x=0, y=0, w=0, h=0; };
	inline constexpr int kCount = static_cast<int>(itemsort::Key::kCount);
	inline constexpr double kInset=44, kHeight=40, kButton=44;
	inline constexpr double kRule=2;
	// Icons centred between the upper and lower rules (kRule+(kHeight-kRule)/2 = 21 below the strip top; owner screenshot
	// 2026-11-09: sort icons sat 2 units high, the stolen and enchanted toggles 2.5 to 3.5 units low).
#define INVENTORYHEADERS_ICON_CENTER 1
	inline constexpr Box kSortIcon{6,5,32,32}, kCategoryIcon{4,3,36,36};
	inline constexpr double kFilterLift=3;
	// Slots: header-strip separators first, then the two list guides (kListGuide, +1), then the upper and lower rules.
	inline constexpr int kGuides = kCount + 7;
	inline constexpr int kListGuide = kGuides - 4;
	struct Plan {
		bool enabled=false;
		double inset=0;
		std::array<Box,kCount> sorts{};
		std::array<Box,3> filters{};
		std::array<Box,kGuides> guides{};
		std::array<bool,kGuides> rule{};
	};
	// Magic revamp (owner 2026-11-09): the Magic spell pages (picker kind 1, the School host) and Active Effects (kind 6)
	// get the same strip. Left to right in the controller carousel order (probe 2026-11-09): spell pages Effect (left of
	// the names, like the Inventory category), A-Z, Type and School cells, Cost over its column; Active Effects A-Z,
	// Effect value / Category / School cells, Magnitude over its column. filters: 0 Effect, 1 Type, 2 School.
#define INVENTORYHEADERS_MAGIC 1
	inline Plan ArrangeMagic(int kind, Box list, double nameLeft, const std::array<Box,kCount>& columns,
		const std::array<bool,kCount>& offered, int count, double rowsBottom)
	{
		Plan out;
		out.enabled=true; out.inset=kInset;
		const auto last=kind==1 ? itemsort::Key::Cost : itemsort::Key::Magnitude;
		const auto& column=columns[int(last)];
		const bool measured=column.w>0 && std::isfinite(column.x+column.w);
		const double right=measured ? column.x+column.w : list.x+list.w-22;
		const double lastLeft=measured ? column.x : right-87;
		const double tableRight=right;
		const double fullHeight=std::clamp(rowsBottom-list.y,kHeight,list.h);
		std::size_t guide=0;
		const auto divider=[&](double x) { out.rule[guide]=true; out.guides[guide++]={x,list.y,kRule,kHeight+kRule}; };
		const auto cellButton=[&](double cellLeft) { return Box{cellLeft+27-kButton/2,list.y,kButton,kHeight}; };
		// RMI-080: a measured row column (spell type, school, effect value...) places its control over it, divider on its
		// left edge; an unmeasured one takes the next 54-unit cell to the left.
		const auto place=[&](const Box& col, double& cellLeft) {
			if (col.w>0 && std::isfinite(col.x+col.w) && col.x+col.w<=cellLeft+0.5) {
				cellLeft=col.x; divider(col.x);
				return Box{col.x+col.w/2-kButton/2,list.y,kButton,kHeight};
			}
			cellLeft-=54; divider(cellLeft);
			return cellButton(cellLeft);
		};
		if (offered[int(last)]) out.sorts[int(last)]={lastLeft+(right-lastLeft)/2-kButton/2,list.y,kButton,kHeight};
		const double nameX=std::isfinite(nameLeft)&&nameLeft>=list.x?nameLeft:list.x+60;
		if (offered[int(itemsort::Key::Name)]) out.sorts[int(itemsort::Key::Name)]={nameX,list.y,kButton,kHeight};
		divider(nameX-3);
		divider(lastLeft);
		double cellLeft=lastLeft;
		if (kind==1) {
			// Spell pages: the row's spell-type column is measured into columns[Category], its school column into columns[School].
			const std::array<itemsort::Key,3> rowColumn{itemsort::Key::kCount,itemsort::Key::Category,itemsort::Key::School};
			for (int f=2; f>=1 && f<count; --f) out.filters[f]=place(columns[int(rowColumn[f])],cellLeft);
			if (count>=1) out.filters[0]={std::max(list.x,(list.x+nameX-kButton)/2),list.y,kButton,kHeight};
		} else {
			for (auto key : { itemsort::Key::School, itemsort::Key::Category, itemsort::Key::EffectValue }) {
				if (!offered[int(key)]) continue;
				out.sorts[int(key)]=place(columns[int(key)],cellLeft);
			}
		}
		const double rowsTop=list.y+kHeight+kRule;
		if (fullHeight>kHeight+kRule) {
			out.guides[kListGuide]={nameX-3,rowsTop,1,fullHeight-kHeight-kRule};
			out.guides[kListGuide+1]={lastLeft,rowsTop,1,fullHeight-kHeight-kRule};
		}
		out.rule[kGuides-2]=out.rule[kGuides-1]=true;
		out.guides.back()={list.x,list.y+kHeight,tableRight-list.x,kRule};
		out.guides[kGuides-2]={list.x,list.y,tableRight-list.x,kRule};
		return out;
	}

	inline constexpr double kValueRightInset=42;   // the Value column ends 42 inside the list (probe C1G, every tab)
	inline Plan ArrangeSpellPurchase(Box list, double nameLeft, const std::array<Box,kCount>& columns,
		const std::array<bool,kCount>& offered, double rowsBottom)
	{
		auto plan = ArrangeMagic(1, list, nameLeft, columns, offered, 0, rowsBottom);
		const double priceCenter = columns[int(itemsort::Key::Value)].w > 0
			? columns[int(itemsort::Key::Value)].x + columns[int(itemsort::Key::Value)].w / 2
			: list.x + list.w - 65;
		const double costLeft = columns[int(itemsort::Key::Cost)].w > 0
			? columns[int(itemsort::Key::Cost)].x : priceCenter - 95;
		const double right = columns[int(itemsort::Key::Value)].w > 0
			? columns[int(itemsort::Key::Value)].x + columns[int(itemsort::Key::Value)].w : list.x + list.w - 22;
		const auto& name = plan.sorts[int(itemsort::Key::Name)];
		plan.filters[2] = { std::max(list.x, (list.x + name.x - kButton) / 2), list.y, kButton, kHeight };
		plan.sorts[int(itemsort::Key::Value)] = { priceCenter - kButton / 2, list.y, kButton, kHeight };
		if (columns[int(itemsort::Key::Cost)].w <= 0)
			plan.sorts[int(itemsort::Key::Cost)].x = costLeft + 5;
		plan.guides[1].x = costLeft;
		plan.guides[2] = { columns[int(itemsort::Key::Value)].w > 0
			? columns[int(itemsort::Key::Value)].x : priceCenter - 43, list.y, kRule, kHeight + kRule };
		plan.rule[2] = true;
		if (plan.guides[kListGuide + 1].w > 0) plan.guides[kListGuide + 1].x = costLeft;
		plan.guides[kGuides - 2].w = plan.guides.back().w = right - list.x;
		return plan;
	}

	inline Plan Arrange(int kind, Box list, double nameLeft, const std::array<Box,kCount>& columns,
		const std::array<bool,kCount>& offered, int count, Box status = {}, double rowsBottom = 0)
	{
		Plan out;
		if (kind==7 && std::isfinite(list.x+list.y+list.w+list.h) && list.w>=350 && list.h>=80 && count==1)
			return ArrangeSpellPurchase(list,nameLeft,columns,offered,rowsBottom);
		if ((kind==1 || kind==6) && std::isfinite(list.x+list.y+list.w+list.h) && list.w>=200 && list.h>=80 && count>=0 && count<=3)
			return ArrangeMagic(kind,list,nameLeft,columns,offered,count,rowsBottom);
		// Inventory (0) and both container/barter sides (3/4): the same rows and columns (owner, C.1 2026-11-12).
		if ((kind!=0 && kind!=3 && kind!=4) || !std::isfinite(list.x+list.y+list.w+list.h) || list.w<200 || list.h<80 || count<1 || count>3) return out;
		out.enabled=true; out.inset=kInset;
		const auto& value=columns[int(itemsort::Key::Value)];
		double right=value.w>0 && std::isfinite(value.x+value.w)
			? value.x+value.w : list.x+list.w-kValueRightInset;   // RMI-092: measured 42 (was a 22 guess)
		double statsLeft=right;
		const double tableRight=right;
		const double fullHeight=std::clamp(rowsBottom-list.y,kHeight,list.h);
		std::size_t guide=0;
		const auto divider=[&](double x) {
			out.rule[guide]=true;
			out.guides[guide++]={x,list.y,kRule,kHeight+kRule};
		};
		for (int k=int(itemsort::Key::Value); k>int(itemsort::Key::Name); --k) {
			if (!offered[k]) continue;
			const double width=k==int(itemsort::Key::Value)?87:54;
			const auto& col=columns[k];
			const bool measured=col.w>0 && std::isfinite(col.x+col.w);
			const double center=measured?col.x+col.w/2:right-width/2;
			const double left=measured?col.x:right-width;
			if (statsLeft!=tableRight) divider(statsLeft);
			statsLeft=left;
			out.sorts[k]={center-kButton/2,list.y,kButton,kHeight}; right-=width;
		}
		const double nameX=std::isfinite(nameLeft)&&nameLeft>=list.x?nameLeft:list.x+60;
		if (offered[int(itemsort::Key::Name)]) out.sorts[int(itemsort::Key::Name)]={nameX,list.y,kButton,kHeight};
		const double statusRight=status.w>=60 && status.x+status.w<=statsLeft+2
			? std::min(status.x+status.w,statsLeft) : statsLeft;
		// count 3: enchanted, stolen, category; fewer = no stolen toggle (the container/barter other side, or the INI off):
		// its cell and divider are left out (owner, C.1 2026-11-12).
		const bool stolen=count>=3;
		out.filters[0]={statusRight-32,list.y-kFilterLift,30,kHeight};
		if (stolen) out.filters[1]={statusRight-68,list.y-kFilterLift,30,kHeight};
		const double categoryX=std::max(list.x,(list.x+nameX-kButton)/2);
		out.filters[2]={categoryX,list.y,kButton,kHeight};
		divider(nameX-3);
		divider(statsLeft);
		if (stolen) divider(statusRight-70);
		divider(statusRight-35);
		// The extended list guides stay subtle and begin below the lower rule.
		const double rowsTop=list.y+kHeight+kRule;
		if (fullHeight>kHeight+kRule) {
			out.guides[kListGuide]={nameX-3,rowsTop,1,fullHeight-kHeight-kRule};
			out.guides[kListGuide+1]={statsLeft,rowsTop,1,fullHeight-kHeight-kRule};
		}
		out.rule[kGuides-2]=out.rule[kGuides-1]=true;
		out.guides.back()={list.x,list.y+kHeight,tableRight-list.x,kRule};
		out.guides[kGuides-2]={list.x,list.y,tableRight-list.x,kRule};
		return out;
	}
#define INVENTORYHEADERS_FILL_REVEAL 1
	inline constexpr bool CanRevealControlFills(bool measureColumns, bool settled)
	{
		return measureColumns && settled;
	}

	// RMI-077: what one tab's rows measured. A tab change places the header with that tab's own earlier measurement
	// (exact: its first frame is the final layout); a tab not measured yet starts from the last measured tab's.
#define INVENTORYHEADERS_TAB_MEASURES 1
	struct Measure { Box list, status; std::array<Box,kCount> columns{}; double nameX=0; };
	inline bool SameList(const Box& a, const Box& b) { return std::abs(a.x-b.x)<0.5 && std::abs(a.w-b.w)<0.5; }
	template<std::size_t N>
	struct TabMeasures {
		std::array<Measure,N> tabs{};
		std::array<bool,N> known{};
		int last=-1;
		void Clear() { known={}; last=-1; }
		void Store(std::size_t tab, const Measure& measure)
		{
			if (tab>=N) return;
			if (last>=0 && !SameList(tabs[last].list,measure.list)) known={};   // another list geometry: all stale
			tabs[tab]=measure; known[tab]=true; last=int(tab);
		}
		bool Exact(std::size_t tab) const { return tab<N && known[tab]; }
		std::optional<Measure> For(std::size_t tab, Box list, bool* exact=nullptr) const
		{
			const bool own=Exact(tab) && SameList(tabs[tab].list,list);
			if (exact) *exact=own;
			if (own) return tabs[tab];
			if (last>=0 && known[last] && SameList(tabs[last].list,list)) return tabs[last];
			return std::nullopt;
		}
	};
	// RMI-092 (owner 2026-11-12; probe C1G on ECA083A7): a row shows only its tab's numeric columns, packed right (Value 87
	// wide ending kValueRightInset inside the list, the others 54 wide to its left), so another tab's measurement is the wrong
	// layout for this tab: only the side's name place is kept from it. With nothing measured, the name sits at 86 (Inventory,
	// player side) or 42 (container/merchant side: no status column).
#define INVENTORYHEADERS_COLUMN_MODEL 1
	inline Measure OtherTabEstimate(const Measure& measure)
	{
		Measure out;
		out.list=measure.list;
		out.nameX=measure.nameX;
		return out;
	}
	inline double DefaultNameX(int kind, Box list) { return list.x+(kind==4 ? 42 : 86); }
	// Bottom of the list guides: the laid-out rows; while a tab's new rows are not laid out yet, the height they had.
	inline double RowsBottom(double measured, double held, int items)
	{
		return measured>0 ? measured : items!=0 ? held : 0;
	}

	// RMI-077 round 2: the game resizes the A-Z wrapper per tab in a post-event, and the next paint moves every later
	// FilterScrollbox slot by the difference (probe 2026-11-09). assumed: the widths the current render translations were
	// placed with; painted: last arranged widths; desired: what the next paint arranges. Returns each child's slot move
	// still to come, and adopts the desired widths. A resize the paint already arranged is left to the measured placement.
#define INVENTORYHEADERS_SLOT_SHIFT 1
	inline constexpr int kPanelChildren = 9;
	inline std::array<double,kPanelChildren> SlotShifts(std::array<double,kPanelChildren>& assumed,
		const std::array<double,kPanelChildren>& painted, const std::array<double,kPanelChildren>& desired, int count)
	{
		std::array<double,kPanelChildren> shift{};
		double run = 0;
		for (int i = 0; i < count && i < kPanelChildren; ++i) {
			shift[i] = run;
			if (desired[i] < 0) continue;
			const bool arranged = painted[i] >= 0 && std::abs(painted[i] - desired[i]) < 0.5;
			if (assumed[i] >= 0 && !arranged) run += desired[i] - assumed[i];
			assumed[i] = desired[i];
		}
		return shift;
	}

	// RMI-078 (owner screenshot 2026-11-09): the native controller Y glyph stays in the native filter row, behind the tab
	// title. It goes just right of the header row's end, centred on the strip.
#define INVENTORYHEADERS_HINT_BOX 1
	inline constexpr double kHintGap = 8;
	inline Box HintBox(const Plan& plan, Box hint)
	{
		const auto& top = plan.guides[kGuides - 2];
		return { top.x + top.w + kHintGap, top.y + (kHeight + kRule - hint.h) / 2, hint.w, hint.h };
	}

#define INVENTORYHEADERS_CELL_FILL 1
	inline Box ControlFill(const Plan& plan, Box control)
	{
		const auto& top = plan.guides[kGuides - 2];
		const auto& bottom = plan.guides.back();
		const double center = control.x + control.w / 2;
		double left = top.x, right = top.x + top.w;
		for (int i = 0; i < kListGuide; ++i) {
			const auto& divider = plan.guides[i];
			if (!plan.rule[i]) continue;
			if (divider.x + divider.w <= center) left = std::max(left, divider.x + divider.w);
			if (divider.x >= center) right = std::min(right, divider.x);
		}
		return {left - control.x, top.y + top.h - control.y,
			std::max(0.0, right - left), std::max(0.0, bottom.y - top.y - top.h)};
	}

	// Page titles are baked (every preset, MRLayout_P/MRCustom_P): the Inventory's inv_cont_title (C.1) and Magic_TabName
	// (C.2, RMI-079) have Auto slots and Min Desired Width 300; no run-time title label.
	// Our own upper header rule: Magic only. The Inventory strip (Inventory, container/barter sides) shows the game's
	// HeaderList_Separator instead, 2 units above it at full list width (owner 2026-11-13, probe MRSEP).
	inline bool DrawsUpperRule(int kind) { return kind==1 || kind==6; }
}
