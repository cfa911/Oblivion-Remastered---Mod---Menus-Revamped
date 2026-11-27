#pragma once
// In-game Pack Builder Phase 4c: the automatic build when the builder window closes (KB
// mods/menus-revamped/evidence/in-game-pack-builder-plan.md, plan row 4). Pure decisions, unit-tested by
// tests/ingame_auto_build_tests.cpp; the window (Shell.cpp) does the file work and starts pack::job::StartBuild, the only
// build path (PackJob: background thread, below normal priority, cancellable, retoc killed with the game).
// Rules:
// - a close builds only when the Custom preset is set (uPreset=2 in MenusRevamped.ini now: a build sets uPreset=2, so with
//   Classic or Dark it would switch presets behind the player's back; there the manual Build stays the way), the colour file
//   reads, and the "Build pack to apply" comparison (baked colours and sizes against the installed manifests) finds a change
// - unsaved edits are saved first only when a build starts (the build reads the file; the live preview showed the in-memory
//   model, so the pack then matches what was previewed); otherwise they stay pending as before
// - a close while a build runs queues one re-check for its end: a successful build is followed by another only if something
//   still differs; a failed or cancelled build never restarts by itself (no retry loop)
// SPDX-License-Identifier: GPL-3.0-or-later
#include <algorithm>
#include <string>
#include <string_view>

namespace builder::autobuild
{
	enum class Action { None, Build, SaveAndBuild, Queue };

	struct Inputs
	{
		bool brokenFile = false;    // the colour file cannot be read (the window shows the broken-file page)
		bool customPreset = false;  // MenusRevamped.ini [Style] uPreset=2 now (the file, not the value at launch)
		bool packStale = false;     // the window's "Build pack to apply": a baked colour or a size differs from the installed manifests
		bool dirty = false;         // unsaved edits (the live preview shows the in-memory model)
		bool jobRunning = false;    // pack::job state Running
	};

	enum class JobEnd { Done, Failed, Cancelled };

	namespace detail
	{
		inline Action Decide(const Inputs& a_in)
		{
			if (a_in.brokenFile || !a_in.customPreset) return Action::None;
			if (a_in.jobRunning) return Action::Queue;
			if (!a_in.packStale) return Action::None;
			return a_in.dirty ? Action::SaveAndBuild : Action::Build;
		}

		inline std::string_view Trim(std::string_view a_s)
		{
			while (!a_s.empty() && (a_s.front() == ' ' || a_s.front() == '\t')) a_s.remove_prefix(1);
			while (!a_s.empty() && (a_s.back() == ' ' || a_s.back() == '\t')) a_s.remove_suffix(1);
			return a_s;
		}

		inline bool SameText(std::string_view a_a, std::string_view a_b)
		{
			if (a_a.size() != a_b.size()) return false;
			for (size_t i = 0; i < a_a.size(); ++i) {
				auto lower = [](char c) { return c >= 'A' && c <= 'Z' ? static_cast<char>(c - 'A' + 'a') : c; };
				if (lower(a_a[i]) != lower(a_b[i])) return false;
			}
			return true;
		}
	}

	// the window closed (any way: Close, Escape, F11, the gear)
	inline Action OnClose(const Inputs& a_in) { return detail::Decide(a_in); }

	// the window is closed and the build it queued behind has ended (a_queued: a close happened during that build)
	inline Action AfterJob(const Inputs& a_in, JobEnd a_end, bool a_queued)
	{
		if (!a_queued || a_end != JobEnd::Done) return Action::None;
		return detail::Decide(a_in);
	}

	// [Style] uPreset of MenusRevamped.ini text, read like the plugin (GetPrivateProfileInt, then clamped to 0..2): section and
	// key case-insensitive, the first key wins, a found value that is not a number reads 0, a missing key gives a_fallback
	inline int PresetIn(std::string_view a_ini, int a_fallback)
	{
		if (a_ini.starts_with("\xEF\xBB\xBF")) a_ini.remove_prefix(3);
		bool style = false;
		while (!a_ini.empty()) {
			const auto stop = a_ini.find_first_of("\r\n");
			const auto line = detail::Trim(a_ini.substr(0, stop));
			a_ini.remove_prefix(stop == std::string_view::npos ? a_ini.size() : stop + 1);
			if (line.starts_with("[")) {
				const auto close = line.find(']');
				style = close != std::string_view::npos && detail::SameText(detail::Trim(line.substr(1, close - 1)), "style");
				continue;
			}
			if (!style || line.starts_with(";")) continue;
			const auto eq = line.find('=');
			if (eq == std::string_view::npos || !detail::SameText(detail::Trim(line.substr(0, eq)), "uPreset")) continue;
			auto value = detail::Trim(line.substr(eq + 1));
			bool negative = false;
			if (!value.empty() && (value.front() == '-' || value.front() == '+')) {
				negative = value.front() == '-';
				value.remove_prefix(1);
			}
			long long n = 0;
			for (char c : value) {
				if (c < '0' || c > '9') break;
				n = std::min(n * 10 + (c - '0'), 1000000LL);
			}
			return std::clamp(static_cast<int>(negative ? -n : n), 0, 2);
		}
		return std::clamp(a_fallback, 0, 2);
	}

	// the progress bar's text: automatic builds say what is happening ("Rebuilding the pack... <phase> (n of m)")
	inline std::string RunningLine(bool a_automatic, const std::string& a_phase, int a_done, int a_total)
	{
		std::string line = a_total > 0 ? a_phase + " (" + std::to_string(a_done) + " of " + std::to_string(a_total) + ")" : a_phase;
		return a_automatic ? "Rebuilding the pack... " + line : line;
	}

	// the status line after a build: an automatic build that installed says "Ready next launch."; failures keep the reason
	inline std::string DoneStatus(bool a_automatic, JobEnd a_end, const std::string& a_message)
	{
		return a_automatic && a_end == JobEnd::Done ? "Ready next launch. " + a_message : a_message;
	}
}
