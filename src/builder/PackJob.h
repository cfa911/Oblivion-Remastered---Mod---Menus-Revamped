#pragma once
// In-game Pack Builder (Phase 3a): the build on a background thread. It touches only files (retoc's extraction, the staged
// packages, the pack) and never a UObject (plan research item R3): retoc runs as a child process with BELOW_NORMAL priority
// and no window, in a job object that closes with the game; the thread itself runs below normal priority. Cancel stops between
// assets and ends a running retoc (once the packs are being put in place it waits). Build (Phase 3e, the Python builder's
// Build): MRCustom_P with the sizes, MRLayout_P (unless [Layout] bLayoutPack=0), read back by retoc, both put in place in
// MenusRevamped/CustomPack with the previous set as .bak, uPreset=2, every line in MenusRevamped/PackBuilder/PackBuilder.log.
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

	// a_plugins: the OBSE plugins folder; the window saves the colour file first. false (with a_why) when a build is running.
	bool StartBuild(const std::filesystem::path& a_plugins, std::string& a_why);
	void Cancel();
	Snapshot Get();

	// preview tool only: show a running build without starting one
	void PreviewRunning(int a_done, int a_total, const char* a_phase);
}
