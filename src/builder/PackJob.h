#pragma once
// In-game Pack Builder (Phase 3a): the build on a background thread. It touches only files (retoc's extraction, the staged
// packages, the pack) and never a UObject (plan research item R3): retoc runs as a child process with BELOW_NORMAL priority
// and no window, in a job object that closes with the game; the thread itself runs below normal priority. Cancel stops between
// assets and ends a running retoc. Phase 3a has one entry, the developer Test build: it builds MRCustom_P from the recipes
// ported so far into MenusRevamped/CustomPack/TestBuild (never installed) with its log TestBuild.log.
// Win32 and the standard library only (the style preview tool compiles it with Shell.cpp).
// SPDX-License-Identifier: GPL-3.0-or-later
#include <filesystem>
#include <string>

namespace builder::pack::job
{
	enum class State { Idle, Running, Done, Failed, Cancelled };

	struct Snapshot
	{
		State       state = State::Idle;
		int         done = 0, total = 0;   // assets handled / assets in the data files
		std::string phase;                 // what it is doing now
		std::string message;               // the result (Done, Failed, Cancelled)
	};

	// a_plugins: the OBSE plugins folder. false (with a_why) when a build is already running.
	bool StartTestBuild(const std::filesystem::path& a_plugins, std::string& a_why);
	void Cancel();
	Snapshot Get();

	// preview tool only: show a running build without starting one
	void PreviewRunning(int a_done, int a_total, const char* a_phase);
}
