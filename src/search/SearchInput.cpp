#include "search/SearchInternal.h"

#include "search/SearchConfig.h"
#include "engine/ContainerMenu.h"
#include "engine/PEHook.h"
#include "engine/QuestsMenu.h"
#include "engine/Reflect.h"
#include "engine/TextUtil.h"
#include "engine/Ui.h"
#include "engine/UiText.h"

#include <imm.h>

namespace search
{
	namespace detail
	{
		// ---- the typing: the game window's messages ----
		// one more character on the end of a search text (UTF-8), unless it is not a printable one or the text is full
		bool AppendTyped(std::string& a_q, char32_t a_cp)
		{
			if (!textutil::Typable(a_cp) || textutil::CodePoints(a_q) >= kMaxText) return false;
			std::string add;
			textutil::AppendCodePoint(add, a_cp);
			if (a_q.size() + add.size() > kMaxBytes) return false;
			a_q += add;
			return true;
		}
	}

	namespace
	{
		using namespace detail;

		wchar_t               g_highSurrogate = 0;   // the first half of a typed character outside the BMP, until the second half arrives - under g_qMutex

		void PasteInto(std::string& a_q)
		{
			if (!OpenClipboard(nullptr)) return;
			if (HANDLE h = GetClipboardData(CF_UNICODETEXT)) {
				if (const auto* w = static_cast<const wchar_t*>(GlobalLock(h))) {
					textutil::ForEachCodePoint(std::wstring_view(w),[&](char32_t a_cp) { AppendTyped(a_q, a_cp); });   // a line break and other controls are skipped
					GlobalUnlock(h);
				}
			}
			CloseClipboard();
		}

		void Typed(wchar_t a_c, menus::Menu a_menu)
		{
			std::scoped_lock l(g_qMutex);
			auto&            q = g_queries[static_cast<int>(a_menu) % 6];
			bool&            sel = g_selAll[static_cast<int>(a_menu) % 6];
			if (a_c == 8) {
				g_highSurrogate = 0;
				if (sel) q.clear();
				else textutil::PopCodePoint(q);   // a whole character, however many bytes it takes
				sel = false;
			} else if (a_c == 0x16) {   // Ctrl+V
				g_highSurrogate = 0;
				if (sel) q.clear();
				sel = false;
				PasteInto(q);
			} else if (a_c >= 0xD800 && a_c <= 0xDBFF) {
				g_highSurrogate = a_c;   // the first half of a character outside the BMP (an emoji, a rare CJK character): the second half is the next WM_CHAR
			} else {
				char32_t cp = a_c;
				if (a_c >= 0xDC00 && a_c <= 0xDFFF) {
					cp = g_highSurrogate ? 0x10000 + ((static_cast<char32_t>(g_highSurrogate) - 0xD800) << 10) + (a_c - 0xDC00) : 0;   // a lone second half is dropped
				}
				g_highSurrogate = 0;
				if (!textutil::Typable(cp)) return;
				if (sel) q.clear();   // a selected text is replaced by what is typed
				sel = false;
				AppendTyped(q, cp);
			}
		}

		bool InsideBar(menus::Menu a_menu, LPARAM a_l)
		{
			const int x = static_cast<short>(LOWORD(a_l)), y = static_cast<short>(HIWORD(a_l));
			std::scoped_lock l(g_qMutex);
			const auto& r = g_barPx[static_cast<int>(a_menu) % 6];
			return r.valid && x >= r.x && x <= r.x + r.w && y >= r.y && y <= r.y + r.h;
		}

		bool ControllerKeyboardUp() { return g_external.load(); }

		// the container / barter menu's side the mouse is over: yours is on the left (cont_give), the other on the right
		menus::Menu SideUnderCursor()
		{
			const bool yours = g_sideShown[static_cast<int>(menus::Menu::kContainerYours)].load(), theirs = g_sideShown[static_cast<int>(menus::Menu::kContainerTheirs)].load();
			if (yours && !theirs) return menus::Menu::kContainerYours;
			if (theirs && !yours) return menus::Menu::kContainerTheirs;
			POINT pt{};
			RECT  rc{};
			if (g_hwnd && GetCursorPos(&pt) && ScreenToClient(g_hwnd, &pt) && GetClientRect(g_hwnd, &rc) && pt.x > (rc.right - rc.left) / 2) return menus::Menu::kContainerTheirs;
			return menus::Menu::kContainerYours;
		}

		// ---- the IME (Japanese, Chinese ...) ----
		// A game window normally takes the input method away from the window (no text field wants it), so the keys of a Japanese or Chinese IME arrive as plain
		// Latin letters. While a bar is being typed into the window gets its default input context back; the committed text arrives in WM_IME_COMPOSITION
		// (GCS_RESULTSTR) and is typed into the bar from there, while the system's own composition and candidate windows draw the text still being composed.
		bool g_imeGiven = false;   // we gave the default input context back and have to take it away again

		void SyncIme(HWND a_h)
		{
			static bool s_on = false;
			const bool  want = g_typing.load() && searchcfg::Get().ime;
			if (want == s_on) return;
			s_on = want;
			if (want) {
				if (HIMC c = ImmGetContext(a_h)) {
					ImmReleaseContext(a_h, c);   // the window has one already: the game left it alone
				} else if (ImmAssociateContextEx(a_h, nullptr, IACE_DEFAULT)) {
					g_imeGiven = true;
					logger::info("ime: the window had no input context - the default one is on while a bar is typed into");
				}
			} else if (g_imeGiven) {
				ImmAssociateContext(a_h, nullptr);
				g_imeGiven = false;
			}
		}

		// the composition window under the bar's left end, the candidate list below it
		// Only from WM_IME_STARTCOMPOSITION, never from WM_IME_NOTIFY: ImmSetCompositionWindow / ImmSetCandidateWindow send a WM_IME_NOTIFY of their own, so
		// placing the windows on every notification loops without end (the stack overflow of 2026-10-20). The guard stops a nested call all the same.
		void PlaceImeWindows(HWND a_h, menus::Menu a_menu)
		{
			static bool s_in = false;
			if (s_in) return;
			s_in = true;
			struct Reset { ~Reset() { s_in = false; } } reset;
			HIMC c = ImmGetContext(a_h);
			if (!c) return;
			PxRect r;
			{
				std::scoped_lock l(g_qMutex);
				r = g_barPx[static_cast<int>(a_menu) % 6];
			}
			if (r.valid) {
				COMPOSITIONFORM cf{};
				cf.dwStyle = CFS_POINT;
				cf.ptCurrentPos = { static_cast<LONG>(r.x + 8), static_cast<LONG>(r.y + r.h * 0.15) };
				ImmSetCompositionWindow(c, &cf);
				CANDIDATEFORM cand{};
				cand.dwStyle = CFS_CANDIDATEPOS;
				cand.ptCurrentPos = { static_cast<LONG>(r.x), static_cast<LONG>(r.y + r.h) };
				ImmSetCandidateWindow(c, &cand);
			}
			ImmReleaseContext(a_h, c);
		}

		bool BarMessage(HWND a_h, UINT a_m, WPARAM a_w, LPARAM a_l, LRESULT& result)
		{

			if (OwnBarOn()) {
				SyncIme(a_h);
				auto       menu = menus::Active();
				const bool cont = menu == menus::Menu::kNone && container::Open();
				if (cont) menu = g_typing.load() ? static_cast<menus::Menu>(g_typingMenu.load()) : SideUnderCursor();   // the container / barter menu: the side typed into, else the one the mouse is on
				if (!cont && menu == menus::Menu::kNone && quests::Open()) menu = menus::Menu::kQuests;
				if (MenuEnabled(menu)) {
					if (g_typing.load()) {
						switch (a_m) {
						case WM_KEYDOWN:   // a key typed into the bar: nothing else sees it (the menu's hotkeys stay quiet)
							if (a_w == 'A' && (GetKeyState(VK_CONTROL) & 0x8000)) {   // Ctrl+A: select all
															std::scoped_lock l(g_qMutex);
															g_selAll[static_cast<int>(menu) % 6] = !g_queries[static_cast<int>(menu) % 6].empty();
															return true;
														}
														if (a_w == VK_DELETE) {   // Delete clears a selected text
															std::scoped_lock l(g_qMutex);
															if (g_selAll[static_cast<int>(menu) % 6]) g_queries[static_cast<int>(menu) % 6].clear();
															g_selAll[static_cast<int>(menu) % 6] = false;
															return true;
														}
														if (a_w == VK_ESCAPE || a_w == VK_RETURN) g_typing = false;
							return true;
						case WM_CHAR:
							Typed(static_cast<wchar_t>(a_w), menu);
							return true;
						case WM_IME_COMPOSITION:
							if (!searchcfg::Get().ime) break;   // [Search] bIme=0: the game sees the IME messages as it always did
							if (a_l & GCS_RESULTSTR) {   // text the IME has committed (kana turned into kanji, a pinyin choice): typed into the bar
								if (HIMC c = ImmGetContext(a_h)) {
									const LONG bytes = ImmGetCompositionStringW(c, GCS_RESULTSTR, nullptr, 0);
									if (bytes > 0) {
										std::wstring w(static_cast<std::size_t>(bytes) / sizeof(wchar_t), L'\0');
										ImmGetCompositionStringW(c, GCS_RESULTSTR, w.data(), bytes);
										for (const wchar_t ch : w) Typed(ch, menu);
									}
									ImmReleaseContext(a_h, c);
								}
							}
							// the system draws what is still being composed; without the result flags it makes no WM_IME_CHAR / WM_CHAR of the text we took
							{ result = DefWindowProcW(a_h, a_m, a_w, a_l & ~static_cast<LPARAM>(GCS_RESULTSTR | GCS_RESULTCLAUSE | GCS_RESULTREADSTR | GCS_RESULTREADCLAUSE)); return true; }
						case WM_IME_STARTCOMPOSITION:
							if (!searchcfg::Get().ime) break;
							PlaceImeWindows(a_h, menu);
							{ result = DefWindowProcW(a_h, a_m, a_w, a_l); return true; }
						case WM_IME_NOTIFY:
						case WM_IME_ENDCOMPOSITION:
						case WM_IME_SETCONTEXT:
							if (!searchcfg::Get().ime) break;
							{ result = DefWindowProcW(a_h, a_m, a_w, a_l); return true; }
						case WM_IME_CHAR:   // the committed text came through WM_IME_COMPOSITION: nothing is typed twice
							if (!searchcfg::Get().ime) break;
							return true;
						case WM_LBUTTONDOWN:
							if (cont && !InsideBar(menu, a_l)) {   // the other side's bar: typing moves there
								const auto other = menu == menus::Menu::kContainerYours ? menus::Menu::kContainerTheirs : menus::Menu::kContainerYours;
								if (MenuEnabled(other) && InsideBar(other, a_l)) {
									g_typingMenu = static_cast<int>(other);
									return true;
								}
							}
							if (!InsideBar(menu, a_l)) g_typing = false;   // a click elsewhere ends it and goes on to the game
							break;
						default: break;
						}
					} else if (!ControllerKeyboardUp()) {
						const bool ctrl = (GetKeyState(VK_CONTROL) & 0x8000) != 0, alt = (GetKeyState(VK_MENU) & 0x8000) != 0;
						if (a_m == WM_KEYDOWN && a_w == 'F' && ctrl && !alt) {
							g_typingMenu = static_cast<int>(menu);
							g_typing = true;
							SyncIme(a_h);
							return true;
						}
						if (a_m == WM_LBUTTONDOWN) {
							const auto other = !cont ? menus::Menu::kNone : menu == menus::Menu::kContainerYours ? menus::Menu::kContainerTheirs : menus::Menu::kContainerYours;
							for (const auto m : { menu, other }) {
								if (m != menus::Menu::kNone && MenuEnabled(m) && InsideBar(m, a_l)) {
									g_typingMenu = static_cast<int>(m);
									g_typing = true;
									SyncIme(a_h);
									return true;
								}
							}
						}
					}
				} else if (g_typing.load()) {
					g_typing = false;
				}
			}
			return false;
		}
	}

	void SetWindow(HWND window) { g_hwnd = window; }
	bool HandleMessage(HWND window, UINT message, WPARAM key, LPARAM position, LRESULT& result)
	{
		return BarMessage(window, message, key, position, result);
	}

	bool Typing() { return g_typing.load(); }

	bool CursorOverBar()
	{
		POINT pt{};
		if (!g_hwnd || !GetCursorPos(&pt) || !ScreenToClient(g_hwnd, &pt)) return false;
		std::scoped_lock l(g_qMutex);
		for (const auto& r : g_barPx)
			if (r.valid && pt.x >= r.x && pt.x <= r.x + r.w && pt.y >= r.y && pt.y <= r.y + r.h) return true;
		return false;
	}

	void SetExternalInput(bool a_open)
{
		g_external = a_open;
		if (a_open) g_typing = false;
	}
}
