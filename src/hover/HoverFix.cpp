#include "hover/HoverFix.h"

#include "core/Config.h"
#include "engine/PEHook.h"
#include "engine/Reflect.h"
#include "engine/Ui.h"
#include "engine/ContainerMenu.h"

#include <algorithm>
#include <map>

namespace hoverfix
{
	namespace
	{
		using Clock = std::chrono::steady_clock;

		enum Target { kNone = -1, kInventory = 0, kMagic = 1 };

		// ---- the hover throttle ----
		struct Pending
		{
			reflect::Handle obj, item;
			UE::UFunction*  fn = nullptr;
			bool            has = false;
		};
		struct State
		{
			reflect::Handle owner;
			ULONGLONG lastAt = 0;
			Pending pending;
		};

		std::vector<State> g_state;
		reflect::Handle g_containerHoverOwner;
		UE::UObject* g_replayObject = nullptr;
		UE::UFunction* g_replayFunction = nullptr;
		bool g_replayReady = false;
		std::uint32_t g_let = 0, g_skipped = 0;

		// Which of the lists' events a UFunction is (cached per UFunction): the bound OnItemHovered of the Inventory's list view and of the Magic menu's spell list.
		int TargetOf(UE::UFunction* a_fn)
		{
			static std::map<UE::UFunction*, int> s_cache;
			if (const auto it = s_cache.find(a_fn); it != s_cache.end()) return it->second;
			const auto name = pe::FunctionName(a_fn);
			int        t = kNone;
			if (name.find("OnItemHovered__DelegateSignature") != std::string::npos) {
				if (name.find("inv_cont_listview") != std::string::npos) t = kInventory;
				else if (name.find("Magic_Spell_List") != std::string::npos) t = kMagic;
			}
			if (t != kNone) {
				if (reinterpret_cast<UE::UStruct*>(a_fn)->propertiesSize != static_cast<std::int32_t>(sizeof(void*))) {   // one parameter, the hovered item
					logger::warn("hover fix: {} has an unexpected parameter layout (size {}) - left alone", name, reinterpret_cast<UE::UStruct*>(a_fn)->propertiesSize);
					t = kNone;
				} else {
					logger::info("hover fix: the {} hover event is {}", t == kInventory ? "Inventory" : "Magic", name);
				}
			}
			s_cache.emplace(a_fn, t);
			return t;
		}

		// Pending hovers belong to the receiving list widget, never to a menu-wide slot.
		// This keeps player/merchant replay independent even when they share a UFunction.
		bool Filter(UE::UObject* a_obj, UE::UFunction* a_fn, void* a_params)
		{
			const int t = TargetOf(a_fn);
			if (t == kNone || !a_obj || !a_params) return false;
			const auto& cfg = mrconfig::Get().smooth;
			const bool inContainer = t == kInventory && container::Open();
			const bool enabled = t == kMagic ? cfg.magicHover :
				inContainer ? cfg.containerHover : cfg.inventoryHover;
			if (!enabled) return false;
			const bool replay = g_replayReady && g_replayObject == a_obj && g_replayFunction == a_fn;
			if (g_replayObject && !replay) {
				// A deferred hover rebuilds rows, which emit synthetic hover delegates.
				// Suppress those exact validated callbacks WITHOUT queuing another replay;
				// otherwise both sides continually reactivate each other while idle.
				++g_skipped;
				return true;
			}
			// Real hover activity supersedes the other side's deferred focus request.
			// Synthetic callbacks during replay returned above and cannot steal this ownership.
			if (inContainer && !replay) g_containerHoverOwner = reflect::Hold(a_obj);
			std::erase_if(g_state, [](const auto& st) { return !reflect::Get(st.owner); });
			auto it = std::find_if(g_state.begin(), g_state.end(), [&](const auto& st) {
				return reflect::Get(st.owner) == a_obj;
			});
			if (it == g_state.end()) {
				g_state.push_back({ reflect::Hold(a_obj), 0, {} });
				it = std::prev(g_state.end());
			}
			auto& st = *it;
			const auto now = GetTickCount64();
			if (replay) g_replayReady = false;   // one call only; nested rebuild hovers stay gated
			if (replay || now - st.lastAt >= static_cast<ULONGLONG>(cfg.hoverIntervalMs)) {
				st.lastAt = now;
				st.pending.has = false;
				++g_let;
				return false;
			}
			auto* item = *static_cast<UE::UObject**>(a_params);
			if (item && reflect::IsLive(item)) {
				st.pending = { reflect::Hold(a_obj), reflect::Hold(item), a_fn, true };
			} else {
				st.pending.has = false;
			}
			++g_skipped;
			return true;
		}

		void Replay()
		{
			std::erase_if(g_state, [](const auto& st) { return !reflect::Get(st.owner); });
			// Copy due calls before ProcessEvent: its nested events can grow g_state.
			std::vector<Pending> due;
			const auto now = GetTickCount64();
			const bool inContainer = container::Open();
			for (auto& st : g_state) {
				if (inContainer && reflect::Get(st.owner) != reflect::Get(g_containerHoverOwner)) {
					st.pending.has = false;
					continue;
				}
				if (!st.pending.has || now - st.lastAt < static_cast<ULONGLONG>(mrconfig::Get().smooth.hoverIntervalMs)) continue;
				due.push_back(st.pending);
				st.pending.has = false;
				st.lastAt = now;
			}
			for (const auto& pending : due) {
				auto* obj = reflect::Get(pending.obj);
				auto* item = reflect::Get(pending.item);
				if (!obj || !item || !pending.fn) continue;
				void* params = item;
				g_replayObject = obj;
				g_replayFunction = pending.fn;
				g_replayReady = true;
				obj->ProcessEvent(pending.fn, &params);
				g_replayReady = false;
				g_replayObject = nullptr;
				g_replayFunction = nullptr;
			}
		}

		// ---- metrics ([Metrics] bEnabled) ----
		struct Metrics
		{
			std::vector<double> frameMs;    // from the menu's own Tick event (the Inventory only)
						std::vector<double> deltaMs;    // the engine's frame time, sampled on every tick of the plugin (any menu)
						double              lastClock = 0;
						double              lastTickMs = -1;
			std::uint32_t       moves = 0, tiles = 0;
			std::int64_t        liveAtLast = 0;
		};
		Metrics    g_metrics;
		std::mutex g_metricsMutex;

		double QpcMs()
		{
			static const double freq = [] { LARGE_INTEGER f; QueryPerformanceFrequency(&f); return static_cast<double>(f.QuadPart); }();
			LARGE_INTEGER now;
			QueryPerformanceCounter(&now);
			return static_cast<double>(now.QuadPart) * 1000.0 / freq;
		}

		// The engine's own frame time from its real-time clock (GameplayStatics.GetRealTimeSeconds: it keeps running while the game is paused in a menu, unlike the world's
				// delta, which is 0 there), sampled on each tick of the plugin: the clock changes once per frame, so the difference between two distinct values is one frame.
				// Works in any menu (the Magic menu has no Tick event to listen to).
				void SampleFrame(menus::Menu a_menu)
				{
					static auto* cls = UE::StaticFindObject<UE::UClass>(nullptr, nullptr, L"/Script/Engine.GameplayStatics");
								auto*        cdo = cls ? cls->GetDefaultObject(false) : nullptr;
								auto*        ctx = a_menu == menus::Menu::kContainerYours ? container::Widget() : menus::Widget(a_menu);
								static bool  s_said[5] = {};
								const auto   fail = [&](int a_i, const char* a_why) {
					                if (!s_said[a_i]) {
					                    s_said[a_i] = true;
					                    logger::info("metrics: engine frame time unavailable - {}", a_why);
					                }
					            };
								if (!cdo) return fail(0, "GameplayStatics has no default object");
								if (!ctx) return fail(1, "the menu has no widget yet");
								ui::Call c(cdo, L"GetRealTimeSeconds");
											if (!c) return fail(2, "GetRealTimeSeconds not found");
								if (!c.Set("WorldContextObject", ctx)) return fail(3, "its WorldContextObject parameter was not found");
								if (!c.RunGuarded()) return fail(4, "the call failed");
								const double now = c.Size("ReturnValue") == 8 ? c.Get<double>("ReturnValue") : static_cast<double>(c.Get<float>("ReturnValue"));
											static bool  s_first = false;
											if (!s_first) {
												s_first = true;
												logger::info("metrics: engine real-time clock works (first value {:.3f} s, {} bytes)", now, c.Size("ReturnValue"));
											}
											std::scoped_lock l(g_metricsMutex);
											if (g_metrics.lastClock > 0 && now != g_metrics.lastClock) {
												const double dt = (now - g_metrics.lastClock) * 1000.0;
												if (dt > 0.0 && dt < 2000.0) g_metrics.deltaMs.push_back(dt);
											}
											g_metrics.lastClock = now;
				}

				// the menus' own per-frame Tick and their OnMouseMove
		void MenuMetric(UE::UObject*, UE::UFunction* a_fn, void*)
		{
			static std::map<UE::UFunction*, int> s_kind;
			auto                                 it = s_kind.find(a_fn);
			if (it == s_kind.end()) {
				const auto n = pe::FunctionName(a_fn);
				it = s_kind.emplace(a_fn, n == "Tick" ? 1 : n == "OnMouseMove" ? 2 : 0).first;
			}
			if (!it->second) return;
			std::scoped_lock l(g_metricsMutex);
			if (it->second == 2) {
				++g_metrics.moves;
				return;
			}
			const double now = QpcMs();
			if (g_metrics.lastTickMs >= 0 && now - g_metrics.lastTickMs < 2000) g_metrics.frameMs.push_back(now - g_metrics.lastTickMs);
			g_metrics.lastTickMs = now;
		}

		// a row tile built = a row (re)built
		void TileMetric(UE::UObject*, UE::UFunction* a_fn, void*)
		{
			static UE::UFunction* s_construct = nullptr;
			if (!s_construct && pe::FunctionName(a_fn) == "Construct") s_construct = a_fn;
			if (a_fn != s_construct) return;
			std::scoped_lock l(g_metricsMutex);
			++g_metrics.tiles;
		}

		std::int64_t LiveObjects()
		{
			auto* arr = UE::FUObjectArray::GetSingleton();
			return arr ? static_cast<std::int64_t>(arr->GetObjectArrayNum()) - static_cast<std::int64_t>(arr->objAvailableList.Num()) : 0;
		}

		void Report(menus::Menu a_menu)
		{
			std::vector<double> f, ft;
						std::uint32_t       moves, tiles;
						{
							std::scoped_lock l(g_metricsMutex);
							f.swap(g_metrics.deltaMs);
							ft.swap(g_metrics.frameMs);
				moves = g_metrics.moves;
				tiles = g_metrics.tiles;
				g_metrics.moves = g_metrics.tiles = 0;
				g_metrics.lastTickMs = -1;
			}
			const auto live = LiveObjects();
			const auto grew = g_metrics.liveAtLast ? live - g_metrics.liveAtLast : 0;
			g_metrics.liveAtLast = live;
			if (f.size() < 20 || (a_menu != menus::Menu::kInventory && a_menu != menus::Menu::kMagic && a_menu != menus::Menu::kContainerYours)) return;
			std::sort(f.begin(), f.end());
			double sum = 0;
			for (const double v : f) sum += v;
			std::uint32_t hitches = 0;
			for (const double v : f) {
				if (v > 50.0) ++hitches;
			}
			const auto pct = [&](double p) { return f[std::min(f.size() - 1, static_cast<std::size_t>(p * static_cast<double>(f.size())))]; };
						double sumTick = 0;   // the Inventory's own Tick events as a cross-check of the engine's frame time
						for (const double v : ft) sumTick += v;
						logger::info("metrics {}: frames={} avg_ms={:.1f} fps={:.0f} p95_ms={:.1f} p99_ms={:.1f} max_ms={:.0f} hitches50={} fps_by_tick_event={:.0f} moves={} hover_let={} hover_skipped={} tiles_built={} objects_grew={} live={} hover_fix={}",
							a_menu == menus::Menu::kInventory ? "inventory" : a_menu == menus::Menu::kMagic ? "magic" : "container/barter", f.size(), sum / static_cast<double>(f.size()), 1000.0 * static_cast<double>(f.size()) / sum, pct(0.95), pct(0.99), f.back(),
							hitches, ft.size() >= 20 ? 1000.0 * static_cast<double>(ft.size()) / sumTick : 0.0, moves, g_let, g_skipped, tiles, grew, live,
				(a_menu == menus::Menu::kInventory ? mrconfig::Get().smooth.inventoryHover : a_menu == menus::Menu::kMagic ? mrconfig::Get().smooth.magicHover : mrconfig::Get().smooth.containerHover) ? 1 : 0);
		}

		// ---- the object guard ----
		// The game stops when the object array is full. Once a second the live objects are counted (slots minus free slots); above the ini's limit a garbage collection is
		// asked for through KismetSystemLibrary.CollectGarbage (the engine runs it at the start of its next frame), at most once per cooldown.
		void Guard(Clock::time_point a_now)
		{
			static auto next = a_now, lastCollect = a_now - 1h;
			if (!mrconfig::Get().smooth.objectGuard || a_now < next) return;
			next = a_now + 1s;
			const auto live = LiveObjects();
			if (live < mrconfig::Get().smooth.objectGuardLimit || a_now < lastCollect + std::chrono::seconds(mrconfig::Get().smooth.objectGuardCooldown)) return;
			static auto* cls = UE::StaticFindObject<UE::UClass>(nullptr, nullptr, L"/Script/Engine.KismetSystemLibrary");
			auto*        cdo = cls ? cls->GetDefaultObject(false) : nullptr;
			ui::Call     c(cdo, L"CollectGarbage");
			if (c && c.Run()) {
				lastCollect = a_now;
				logger::warn("object guard: {} live objects (limit {}, the game stops at 2162688) - a garbage collection was asked for", live, mrconfig::Get().smooth.objectGuardLimit);
			} else {
				static bool s_said = false;
				if (!s_said) {
					s_said = true;
					logger::error("object guard: CollectGarbage could not be called - the guard cannot act");
				}
			}
		}

		// ---- finding the classes once their menu is open ----
		void Discover(menus::Menu a_menu)
		{
			static bool s_invFilter = false, s_magicFilter = false, s_tile = false, s_invMetric = false, s_magicMetric = false;
			if (a_menu == menus::Menu::kContainerYours || a_menu == menus::Menu::kContainerTheirs) {
				// Existing weak-cached side roots; no new global or per-frame tree scan.
				if (!s_invFilter) {
					for (const int side : { 3, 4 }) {
						auto* content = container::SideContent(side);
						if (content && pe::Utf8(content->GetClass()->GetFName().ToString()) == "WBP_ModernMenu_ContainerInventory_MainContent_C") {
							s_invFilter = pe::Intercept(content->GetClass(), &Filter);
							break;
						}
					}
				}
				if (mrconfig::Get().smooth.metrics && !s_tile) {
					for (auto* cls : ui::TreeClasses(container::Widget(), 900)) {
						if (pe::Utf8(cls->GetFName().ToString()) == "WBP_OriginalImageTile_C") {
							s_tile = pe::Watch(cls, &TileMetric);
							break;
						}
					}
				}
			} else if (a_menu == menus::Menu::kInventory) {
				auto* menu = menus::Widget(menus::Menu::kInventory);
				if (!menu) return;
				if (mrconfig::Get().smooth.metrics && !s_invMetric) {
					s_invMetric = true;
					logger::info("hover fix: Inventory frame metrics {}", pe::Watch(menus::ClassOf(menus::Menu::kInventory), &MenuMetric) ? "on" : "failed");
				}
				if (s_invFilter && (s_tile || !mrconfig::Get().smooth.metrics)) return;
				for (auto* c : ui::TreeClasses(menu, 900)) {
					const auto name = pe::Utf8(c->GetFName().ToString());
					if (!s_invFilter && name == "WBP_ModernMenu_ContainerInventory_MainContent_C") {
						s_invFilter = pe::Intercept(c, &Filter);
					}
					if (mrconfig::Get().smooth.metrics && !s_tile && name == "WBP_OriginalImageTile_C") {
						s_tile = true;
						logger::info("hover fix: row tile metrics {}", pe::Watch(c, &TileMetric) ? "on" : "failed");
					}
				}
			} else if (a_menu == menus::Menu::kMagic) {
				auto* cls = menus::ClassOf(menus::Menu::kMagic);
				if (!cls) return;
				if (mrconfig::Get().smooth.metrics && !s_magicMetric) {
					s_magicMetric = true;
					logger::info("hover fix: Magic frame metrics {}", pe::Watch(cls, &MenuMetric) ? "on" : "failed");
				}
				if (!s_magicFilter) {
					s_magicFilter = pe::Intercept(cls, &Filter);
				}
			}
		}
	}

	void Tick(menus::Menu a_menu)
	{
		static auto nextDiscover = Clock::now(), nextReport = Clock::now() + 5s;
		static auto previous = menus::Menu::kNone;
		if (container::Open()) a_menu = menus::Menu::kContainerYours;
		if (previous != a_menu) {
			g_state.clear();   // never replay an old menu/side event after a close or transition
			g_containerHoverOwner = {};
			previous = a_menu;
		}
		const auto now = Clock::now();
		if (a_menu != menus::Menu::kNone && now >= nextDiscover) {
			nextDiscover = now + 1s;
			Discover(a_menu);
		}
		if (a_menu != menus::Menu::kNone) Replay();
		Guard(now);   // one guard, in every menu and gameplay, from the existing UI tick
		if (mrconfig::Get().smooth.metrics && a_menu != menus::Menu::kNone) SampleFrame(a_menu);
		if (now >= nextReport) {
			nextReport = now + 5s;
			if (mrconfig::Get().smooth.metrics) Report(a_menu);
			g_let = g_skipped = 0;
		}
	}
}
