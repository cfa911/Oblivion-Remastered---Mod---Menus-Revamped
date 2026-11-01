#include "search/WorldMapSearch.h"
#include "search/SearchInternal.h"
#include "search/SearchConfig.h"
#include "engine/WorldMap.h"
#include "engine/Ui.h"
#include "engine/UiText.h"
#include "keyboard/Keyboard.h"
#include <Xinput.h>
#include <cmath>

namespace mapsearch
{
	namespace
	{
		constexpr int kRows = 12;
		constexpr double kHeight = 36;
		struct Row { reflect::Handle frame, frameSlot, label, labelSlot, icon, iconSlot; };
		std::array<Row, kRows> g_rows;
		reflect::Handle g_canvas, g_ring, g_ringSlot;
		std::vector<maprules::Location> g_locations, g_results;
		std::string g_query;
		std::uint64_t g_generation = 0;
		std::uint32_t g_filterMask = 0;
		std::uint64_t g_revision = 1;
		std::atomic<std::uint64_t> g_renderedVersion{ 0 }, g_commandVersion{ 0 };
		int g_selected = 0, g_first = 0;
		bool g_suppressed = false, g_wasTyping = false, g_wasExternal = false, g_ringReady = false;
		std::atomic<bool> g_highlight{ false };
		bool g_dirty = true;
		ULONGLONG g_nextHighlight = 0;
		double g_highlightX = 0, g_highlightY = 0;
		double g_displayX = 0, g_displayY = 0;
		bool g_displayReady = false;
		int g_displaySettle = 0;
		float g_highlightZoom = 0;
		std::atomic<bool> g_visible{ false }, g_active{ false }, g_dismiss{ false }, g_mouseUp{ false };
		std::atomic<int> g_move{ 0 }, g_choose{ -1 }, g_hover{ -1 };
		std::atomic<unsigned> g_swallowKeys{ 0 };
		std::mutex g_hitsMutex;
		std::array<search::detail::PxRect, kRows> g_hits{};
		std::array<int, kRows> g_hitIndex{};
		search::detail::PxRect g_lastPixels{};

		void Show(bool yes)
		{
			const bool previous = g_visible.exchange(yes);
			if (previous == yes) return;
			g_dirty = true;
			if (!yes) for (const auto& row : g_rows) {
				if (auto* o = reflect::Get(row.frame)) ui::Visible(o, false);
				if (auto* o = reflect::Get(row.label)) ui::Visible(o, false);
				if (auto* o = reflect::Get(row.icon)) ui::Visible(o, false);
			}
		}
		bool Build(UE::UObject* canvas)
		{
			if (reflect::Get(g_canvas) == canvas && reflect::Get(g_rows[0].frame) && reflect::Get(g_rows[0].label)) return true;
			for (auto& row : g_rows) {
				for (const auto& handle : { row.frame, row.label, row.icon }) if (auto* o = reflect::Get(handle)) { ui::Call rm(o, L"RemoveFromParent"); rm.Run(); }
				row = {};
			}
			if (auto* ring = reflect::Get(g_ring)) { ui::Call rm(ring, L"RemoveFromParent"); rm.Run(); }
			g_canvas = reflect::Hold(canvas);
			for (int i = 0; i < kRows; ++i) {
				UE::UObject *fs = nullptr, *ls = nullptr, *is = nullptr;
				const auto suffix = std::to_wstring(i);
				auto* frame = ui::AddToCanvas(canvas, L"/Script/UMG.Image", (L"MRMapResultFrame" + suffix).c_str(), &fs);
				auto* label = ui::AddToCanvas(canvas, L"/Script/UMG.TextBlock", (L"MRMapResultText" + suffix).c_str(), &ls);
				auto* icon = ui::AddToCanvas(canvas, L"/Script/UMG.Image", (L"MRMapResultIcon" + suffix).c_str(), &is);
				if (!frame || !label || !icon || !fs || !ls || !is) return false;
				g_rows[i] = { reflect::Hold(frame), reflect::Hold(fs), reflect::Hold(label), reflect::Hold(ls), reflect::Hold(icon), reflect::Hold(is) };
				ui::Visible(frame, false); ui::Visible(label, false); ui::Visible(icon, false);
				const int z = 1100 + i * 3, tz = z + 1;
				ui::CallFirst(fs, L"SetZOrder", &z, sizeof(z)); ui::CallFirst(ls, L"SetZOrder", &tz, sizeof(tz)); ui::CallFirst(is, L"SetZOrder", &tz, sizeof(tz));
			}
			UE::UObject* slot = nullptr;
			auto* ring = ui::AddToCanvas(canvas, L"/Script/UMG.Image", L"MRMapSelectedLocation", &slot);
			g_ring = reflect::Hold(ring); g_ringSlot = reflect::Hold(slot);
			const float fill[4]{ 0, 0, 0, 0 }, edge[4]{ 0.95f, 0.72f, 0.25f, 1 };
			g_ringReady = ring && slot && ui::RoundedBox(ring, true, fill, edge, 3, 24);
			if (slot) { const int z = 1090; ui::CallFirst(slot, L"SetZOrder", &z, sizeof(z)); }
			if (ring) ui::Visible(ring, false);
			g_dirty = true;
			return true;
		}
		void Highlight()
		{
			auto* ring = reflect::Get(g_ring);
			if (!ring) return;
			double x = 0, y = 0; float zoom = 0;
			if (!g_ringReady || !worldmap::Position(x, y, zoom) || std::abs(x-g_highlightX)>0.01 || std::abs(y-g_highlightY)>0.01 || std::abs(zoom-g_highlightZoom)>0.001) g_highlight = false;
			if (!g_highlight) { ui::Visible(ring, false); return; }
			double mapX=0,mapY=0;
			if (!worldmap::DisplayedCenter(mapX,mapY)) { ui::Visible(ring,false); return; }
			if (!g_displayReady) {
				// GetCachedGeometry needs the layout after the native centering call.
				if (g_displaySettle++ < 2) { ui::Visible(ring,false); return; }
				g_displayX=mapX; g_displayY=mapY; g_displayReady=true;
			} else if (std::abs(mapX-g_displayX)>0.000001 || std::abs(mapY-g_displayY)>0.000001) {
				g_highlight=false; ui::Visible(ring,false); return;
			}
			if (GetTickCount64() < g_nextHighlight) return;
			g_nextHighlight = GetTickCount64() + 100;
			double vx=0,vy=0,vw=0,vh=0,cx=0,cy=0,cw=0,ch=0,fx=0,fy=0,fw=0,fh=0;
			const auto& bar = search::detail::g_bar[static_cast<int>(menus::Menu::kWorldMap)];
			auto* view = worldmap::View();
			if (!view || !ui::Measure(view,vx,vy,vw,vh) || !ui::Measure(reflect::Get(g_canvas),cx,cy,cw,ch) ||
				!ui::Measure(reflect::Get(bar.frame),fx,fy,fw,fh) || fw < 1) { ui::Visible(ring,false); return; }
			const double units = bar.w/fw;
			auto* slot = reflect::Get(g_ringSlot);
			ui::Vec2(slot,L"SetPosition",(vx+vw*0.5-cx)*units-24,(vy+vh*0.5-cy)*units-24);
			ui::Vec2(slot,L"SetSize",48,48); ui::Visible(ring,true);
		}
		const char* TypeName(int type)
		{
			static constexpr const char* names[]{ "", "Camp", "Cave", "City", "Ayleid ruin", "Fort ruin", "Mine", "Landmark", "Tavern", "Settlement", "Shrine", "Oblivion gate", "Entrance" };
			return type>=1 && type<=12 ? names[type] : "Location";
		}
		void Paint()
		{
			const auto& bar = search::detail::g_bar[static_cast<int>(menus::Menu::kWorldMap)];
			search::detail::PxRect pixels;
			{ std::scoped_lock lock(search::detail::g_qMutex); pixels = search::detail::g_barPx[static_cast<int>(menus::Menu::kWorldMap)]; }
			const double scale = pixels.valid && bar.w > 0 ? pixels.w/bar.w : 1;
			const int count = g_results.empty() ? 1 : std::min(kRows, static_cast<int>(g_results.size())-g_first);
			std::array<search::detail::PxRect,kRows> hits{};
			std::array<int,kRows> indices{};
			for (int i=0;i<kRows;++i) {
				const auto& row = g_rows[i];
				auto* frame=reflect::Get(row.frame); auto* label=reflect::Get(row.label);
				const bool shown=i<count;
				ui::Visible(frame,shown); ui::Visible(label,shown);
				auto* icon=reflect::Get(row.icon); ui::Visible(icon,false);
				if (!shown) continue;
				const int index=g_first+i;
				const double y=bar.y+bar.h+4+i*kHeight;
				ui::Vec2(reflect::Get(row.frameSlot),L"SetPosition",bar.x,y);
				ui::Vec2(reflect::Get(row.frameSlot),L"SetSize",bar.w,kHeight);
				ui::Vec2(reflect::Get(row.labelSlot),L"SetPosition",bar.x+10,y+5);
				ui::Vec2(reflect::Get(row.labelSlot),L"SetSize",bar.w-54,kHeight-7);
				const bool selected=!g_results.empty() && index==g_selected;
				ui::Colour(frame,selected?0.20f:0.045f,selected?0.30f:0.035f,selected?0.46f:0.025f,0.98f);
				std::string text="No discovered locations found";
				if (!g_results.empty()) {
					text=g_results[index].name;
					ui::Vec2(reflect::Get(row.iconSlot),L"SetPosition",bar.x+bar.w-34,y+3);
					ui::Vec2(reflect::Get(row.iconSlot),L"SetSize",30,30);
					ui::Visible(icon,worldmap::CopyFilterIcon(g_results[index].type,icon));
					const bool duplicate=std::count_if(g_results.begin(),g_results.end(),[&](const auto& l){ return l.name==text; })>1;
					if (duplicate) text+="  ·  "+std::string(TypeName(g_results[index].type));
					if (g_results.size()>kRows && i==count-1) text+="   ["+std::to_string(index+1)+"/"+std::to_string(g_results.size())+"]";
				}
				uix::SetText(label,text);
				const float ink[4]{0.93f,0.90f,0.82f,1}; uix::Ink(label,ink);
				uix::ApplyFont(label,searchcfg::Get().barFontSize,uix::NeedsFallbackFont(text));
				if (pixels.valid) { hits[i]={pixels.x,pixels.y+pixels.h+(4+i*kHeight)*scale,pixels.w,kHeight*scale,true}; indices[i]=g_results.empty()?-1:index; }
			}
			{ std::scoped_lock lock(g_hitsMutex); g_hits=hits; g_hitIndex=indices; }
			g_renderedVersion = g_revision;
		}
	}

	void Tick()
	{
		const bool active=searchcfg::Get().worldMap && worldmap::Open();
		g_active=active;
		if (!active) {
			Show(false); g_highlight=false; g_wasTyping=false; g_wasExternal=false; g_move=0; g_choose=-1; g_hover=-1; g_dismiss=false;
			auto& bar=search::detail::g_bar[static_cast<int>(menus::Menu::kWorldMap)];
			if (bar.visible) {
				if(auto* frame=reflect::Get(bar.frame)) ui::Visible(frame,false);
				if(auto* label=reflect::Get(bar.label)) ui::Visible(label,false);
				bar.visible=false;
				std::scoped_lock lock(search::detail::g_qMutex); search::detail::g_barPx[static_cast<int>(menus::Menu::kWorldMap)].valid=false;
			}
			if(auto* ring=reflect::Get(g_ring)) ui::Visible(ring,false);
			return;
		}
		const auto& bar=search::detail::g_bar[static_cast<int>(menus::Menu::kWorldMap)];
		auto* canvas=reflect::Get(bar.canvas);
		if (!canvas || !Build(canvas)) { Show(false); return; }
		{
			std::scoped_lock lock(search::detail::g_qMutex);
			const auto r=search::detail::g_barPx[static_cast<int>(menus::Menu::kWorldMap)];
			if (r.valid!=g_lastPixels.valid || r.x!=g_lastPixels.x || r.y!=g_lastPixels.y || r.w!=g_lastPixels.w || r.h!=g_lastPixels.h) {
				g_lastPixels=r; g_dirty=true;
			}
		}
		const auto query=search::GetText(menus::Menu::kWorldMap);
		const bool typing=search::detail::g_typing.load();
		const bool external=search::detail::g_external.load();
		const bool reopen=(typing && !g_wasTyping) || (external && !g_wasExternal);
		g_wasTyping=typing;
		g_wasExternal=external;
		const auto filters=worldmap::ActiveFilterMask();
		const bool changed=query!=g_query || worldmap::Generation()!=g_generation || filters!=g_filterMask;
		if (changed || reopen) {
			++g_revision;
			g_dirty = true;
			g_query=query; g_generation=worldmap::Generation(); g_filterMask=filters; g_suppressed=false;
			g_locations=worldmap::Snapshot(); g_results=maprules::Filter(g_locations,query,searchcfg::Get().ignoreAccents);
			g_selected=0; g_first=0;
		}
		if (g_dismiss.exchange(false)) { g_suppressed=true; search::detail::g_typing=false; }
		if (!g_results.empty()) {
			const int previousSelected = g_selected, previousFirst = g_first;
			const int hover=g_hover.exchange(-1); if (hover>=0 && hover<static_cast<int>(g_results.size())) g_selected=hover;
			g_selected=std::clamp(g_selected+g_move.exchange(0),0,static_cast<int>(g_results.size())-1);
			if (g_selected<g_first) g_first=g_selected;
			if (g_selected>=g_first+kRows) g_first=g_selected-kRows+1;
			if (g_selected!=previousSelected || g_first!=previousFirst) g_dirty=true;
		} else { g_move=0; g_hover=-1; }
		const int choose=g_choose.exchange(-1);
		if (choose>=0 && g_commandVersion.load()==g_revision && !g_suppressed && !g_results.empty()) {
			const int index=choose==100000 ? g_selected : choose;
			if (index>=0 && index<static_cast<int>(g_results.size())) {
				if (worldmap::Focus(g_results[index].id)) {
					g_suppressed=true; search::detail::g_typing=false;
					g_highlight=worldmap::Position(g_highlightX,g_highlightY,g_highlightZoom);
					g_displayReady=false; g_displaySettle=0; g_nextHighlight=0;
				} else { g_locations=worldmap::Snapshot(); g_results=maprules::Filter(g_locations,g_query,searchcfg::Get().ignoreAccents); g_selected=0; g_first=0; g_dirty=true; }
			}
		}
		Show(!query.empty() && !g_suppressed);
		if (g_visible && g_dirty) { Paint(); g_dirty=false; }
		Highlight();
	}

	bool Message(HWND hwnd, UINT message, WPARAM key, LPARAM position)
	{
		const unsigned bit=key==VK_RETURN?1:key==VK_ESCAPE?2:0;
		if ((message==WM_KEYDOWN || message==WM_KEYUP) && bit && (g_swallowKeys.load()&bit)) {
			if(message==WM_KEYUP) g_swallowKeys.fetch_and(~bit);
			return true;
		}
		if (message==WM_LBUTTONUP && g_mouseUp.exchange(false)) return true;
		if (search::detail::g_external.load()) return false;
		if (!g_active || (!g_visible && !search::detail::g_typing.load())) return false;
		if (message==WM_KEYDOWN) {
			if(key==VK_UP || key==VK_DOWN || key==VK_PRIOR || key==VK_NEXT) { g_move.fetch_add(key==VK_UP?-1:key==VK_DOWN?1:key==VK_PRIOR?-kRows:kRows); return true; }
			if(key==VK_RETURN && g_visible) { g_commandVersion=g_renderedVersion.load(); g_choose=100000; g_swallowKeys.fetch_or(1); return true; }
			if(key==VK_ESCAPE) { g_dismiss=true; g_swallowKeys.fetch_or(2); return true; }
		}
		if (!g_visible || (message!=WM_MOUSEMOVE && message!=WM_LBUTTONDOWN && message!=WM_MOUSEWHEEL)) return false;
		POINT point{static_cast<short>(LOWORD(position)),static_cast<short>(HIWORD(position))};
		if(message==WM_MOUSEWHEEL) ScreenToClient(hwnd,&point);
		std::scoped_lock lock(g_hitsMutex);
		for(int i=0;i<kRows;++i) {
			const auto& r=g_hits[i];
			if(!r.valid || point.x<r.x || point.x>r.x+r.w || point.y<r.y || point.y>r.y+r.h) continue;
			if(message==WM_LBUTTONDOWN) { g_commandVersion=g_renderedVersion.load(); g_choose=g_hitIndex[i]; g_mouseUp=true; }
			else if(message==WM_MOUSEWHEEL) g_move.fetch_add(static_cast<short>(HIWORD(key))>0?-3:3);
			else g_hover=g_hitIndex[i];
			return true;
		}
		return false;
	}
	bool Controller(std::uint16_t& buttons, short& lx, short& ly, short& rx, short& ry, bool keyboardOpen)
	{
		static std::uint16_t previous=0, swallowed=0;
		static ULONGLONG next=0;
		const auto raw=buttons, pressed=static_cast<std::uint16_t>(raw&~previous);
		previous=raw; swallowed &= raw; buttons &= static_cast<std::uint16_t>(~swallowed);
		if (keyboardOpen || !g_active || (!g_visible && !search::detail::g_typing.load())) return false;
		const auto now=GetTickCount64();
		const int direction=(raw&XINPUT_GAMEPAD_DPAD_UP || ly>16000)?-1:(raw&XINPUT_GAMEPAD_DPAD_DOWN || ly<-16000)?1:0;
		if(direction && (now>=next || (pressed&(XINPUT_GAMEPAD_DPAD_UP|XINPUT_GAMEPAD_DPAD_DOWN)))) { g_move.fetch_add(direction); next=now+160; }
		if(pressed&XINPUT_GAMEPAD_A) { g_commandVersion=g_renderedVersion.load(); g_choose=100000; }
		if(pressed&XINPUT_GAMEPAD_B) g_dismiss=true;
		constexpr std::uint16_t owned=XINPUT_GAMEPAD_A|XINPUT_GAMEPAD_B|XINPUT_GAMEPAD_DPAD_UP|XINPUT_GAMEPAD_DPAD_DOWN;
		swallowed|=raw&owned; buttons &= static_cast<std::uint16_t>(~owned);
		lx=ly=rx=ry=0;
		return true;
	}
}
