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
	inline Plan Arrange(int kind, Box list, double nameLeft, const std::array<Box,kCount>& columns,
		const std::array<bool,kCount>& offered, int count, Box status = {}, double rowsBottom = 0)
	{
		Plan out;
		if (kind!=0 || !std::isfinite(list.x+list.y+list.w+list.h) || list.w<200 || list.h<80 || count<1 || count>3) return out;
		out.enabled=true; out.inset=kInset;
		const auto& value=columns[int(itemsort::Key::Value)];
		double right=value.w>0 && std::isfinite(value.x+value.w)
			? value.x+value.w : list.x+list.w-22;
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
		out.filters[0]={statusRight-32,list.y-kFilterLift,30,kHeight};
		out.filters[1]={statusRight-68,list.y-kFilterLift,30,kHeight};
		const double categoryX=std::max(list.x,(list.x+nameX-kButton)/2);
		out.filters[2]={categoryX,list.y,kButton,kHeight};
		divider(nameX-3);
		divider(statsLeft);
		divider(statusRight-70);
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

	// RMI-073: the native tab title cuts "Miscellaneous" at a fixed 156-158 units whatever width is set on it (min width,
	// no scrolling, RetainerBox off; logs on 89d7ca0d and d7b00224). Our own label sits on the native text, wide enough
	// for the longest tab name.
#define INVENTORYHEADERS_TITLE_LABEL 1
	inline constexpr double kTitleLabelWidth=300;
	inline Box TitleLabel(Box text) { return { text.x, text.y, std::max(text.w, kTitleLabelWidth), text.h }; }
#define INVENTORYHEADERS_IMMEDIATE_TITLE 1
	inline std::string_view TitleText(std::optional<std::string_view> requested, std::string_view native)
	{
		return requested.value_or(native);
	}
}
