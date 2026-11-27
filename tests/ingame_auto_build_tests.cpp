// In-game Pack Builder Phase 4c: the automatic build when the window closes (src/builder/AutoBuild.h).
// - a close starts a build only when the Custom preset is set (uPreset=2 in the file now), the colour file reads, and the
//   colours or sizes differ from the installed packs' manifests; unsaved edits are saved first (the build reads the file)
// - a close during a running build queues one re-check for when it ends; it builds again only after a successful build
//   and only if something still differs (a failed or cancelled build never restarts by itself)
// - uPreset is read like GetPrivateProfileInt + the plugin's clamp to 0..2
// - status lines: "Rebuilding the pack... <phase>" while an automatic build runs, "Ready next launch. <result>" when it installed
#include <cstdio>
#include <string>
#include "../src/builder/AutoBuild.h"

using namespace builder::autobuild;

unsigned checks = 0, failures = 0;
void Check(bool ok, const char* why)
{
	++checks;
	if (!ok) {
		++failures;
		std::printf("FAIL %s\n", why);
	}
}

static Inputs In(bool custom, bool stale, bool dirty, bool running = false, bool broken = false)
{
	Inputs i;
	i.customPreset = custom;
	i.packStale = stale;
	i.dirty = dirty;
	i.jobRunning = running;
	i.brokenFile = broken;
	return i;
}

int main()
{
	// ---- OnClose ----
	Check(OnClose(In(true, true, false)) == Action::Build, "custom + stale + saved: build");
	Check(OnClose(In(true, true, true)) == Action::SaveAndBuild, "custom + stale + unsaved: save, then build");
	Check(OnClose(In(true, false, false)) == Action::None, "colours match the packs: nothing");
	Check(OnClose(In(true, false, true)) == Action::None, "unsaved edits that need no build (run-time tints): nothing, edits stay pending");
	Check(OnClose(In(false, true, false)) == Action::None, "Classic/Dark preset: no build (a build would switch to Custom)");
	Check(OnClose(In(false, true, true)) == Action::None, "Classic/Dark preset with unsaved edits: no save, no build");
	Check(OnClose(In(true, true, true, false, true)) == Action::None, "broken colour file: nothing");
	Check(OnClose(In(true, true, false, true)) == Action::Queue, "build running: queue one re-check");
	Check(OnClose(In(true, false, false, true)) == Action::Queue, "build running, manifests not yet updated: still queue (re-checked after)");
	Check(OnClose(In(false, true, false, true)) == Action::None, "build running but not Custom: nothing queued");
	Check(OnClose(In(true, true, false, true, true)) == Action::None, "build running, broken file: nothing queued");

	// ---- AfterJob ----
	Check(AfterJob(In(true, true, false), JobEnd::Done, false) == Action::None, "nothing queued: nothing");
	Check(AfterJob(In(true, true, false), JobEnd::Done, true) == Action::Build, "queued, done, still stale: build again");
	Check(AfterJob(In(true, true, true), JobEnd::Done, true) == Action::SaveAndBuild, "queued, done, still stale, unsaved: save and build");
	Check(AfterJob(In(true, false, false), JobEnd::Done, true) == Action::None, "queued, done, packs match now: nothing");
	Check(AfterJob(In(true, true, false), JobEnd::Failed, true) == Action::None, "queued, failed: no automatic retry");
	Check(AfterJob(In(true, true, false), JobEnd::Cancelled, true) == Action::None, "queued, cancelled: no automatic restart");
	Check(AfterJob(In(false, true, false), JobEnd::Done, true) == Action::None, "queued, done, preset no longer Custom: nothing");
	Check(AfterJob(In(true, true, false, true), JobEnd::Done, true) == Action::Queue, "queued, another build already running: keep waiting");
	Check(AfterJob(In(true, true, false, false, true), JobEnd::Done, true) == Action::None, "queued, broken file: nothing");

	// ---- PresetIn (GetPrivateProfileInt + clamp 0..2) ----
	Check(PresetIn("[Style]\nuPreset=2\n", 0) == 2, "plain uPreset=2");
	Check(PresetIn("[Style]\r\nuPreset=1\r\n", 0) == 1, "CRLF uPreset=1");
	Check(PresetIn("\xEF\xBB\xBF[Style]\nuPreset=2\n", 0) == 2, "BOM");
	Check(PresetIn("[style]\n  UPRESET = 2 ; Custom\n", 0) == 2, "section and key case-insensitive, spaces, trailing comment");
	Check(PresetIn("[Layout]\nuPreset=2\n[Style]\nbOther=1\n", 0) == 0, "uPreset in another section: fallback");
	Check(PresetIn("[Style]\n; uPreset=2\n", 0) == 0, "commented line: fallback");
	Check(PresetIn("", 0) == 0, "empty file: fallback");
	Check(PresetIn("[Style]\nuPreset=7\n", 0) == 2, "clamped to 2");
	Check(PresetIn("[Style]\nuPreset=-3\n", 0) == 0, "clamped to 0");
	Check(PresetIn("[Style]\nuPreset=abc\n", 2) == 0, "found but not a number: 0 (GetPrivateProfileInt)");
	Check(PresetIn("[Style]\nuPreset=1\nuPreset=2\n", 0) == 1, "first key wins");
	Check(PresetIn("[Style]\nuPresetX=2\n", 0) == 0, "other key with the same prefix: fallback");
	Check(PresetIn("[Style]\nuPreset=2", 0) == 2, "no final newline");
	Check(PresetIn("[Style]\nuPreset=2\n", 1) == 2 && PresetIn("[Style]\n", 1) == 1, "fallback used only when missing");

	// ---- status lines ----
	Check(RunningLine(true, "Baking T_X", 3, 10) == "Rebuilding the pack... Baking T_X (3 of 10)", "automatic running line with progress");
	Check(RunningLine(true, "Starting", 0, 0) == "Rebuilding the pack... Starting", "automatic running line before the total is known");
	Check(RunningLine(false, "Baking T_X", 3, 10) == "Baking T_X (3 of 10)", "manual running line unchanged");
	Check(RunningLine(false, "Starting", 0, 0) == "Starting", "manual running line without total unchanged");
	Check(DoneStatus(true, JobEnd::Done, "Built in 24 s.") == "Ready next launch. Built in 24 s.", "automatic success: ready next launch");
	Check(DoneStatus(true, JobEnd::Failed, "Build failed: x.") == "Build failed: x.", "automatic failure: the reason as today");
	Check(DoneStatus(true, JobEnd::Cancelled, "Build cancelled.") == "Build cancelled.", "automatic cancel: as today");
	Check(DoneStatus(false, JobEnd::Done, "Built in 24 s.") == "Built in 24 s.", "manual success unchanged");

	std::printf("%u checks, %u failures\n", checks, failures);
	return failures ? 1 : 0;
}
