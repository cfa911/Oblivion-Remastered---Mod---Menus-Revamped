// In-game Pack Builder (Phase 3a, Build Phase 3e): the build on a background thread (see PackJob.h). The pipeline steps follow
// tools/pack_builder/pipeline.py _build and app.build_pack: retoc to-legacy of every needed name, bake each asset
// (src/builder/PackBuild.h), retoc to-zen, the manifest, the read-back (retoc verify + list), then the files are moved into
// place. KB mods/menus-revamped/evidence/in-game-pack-builder-plan.md (Phase 3).
// SPDX-License-Identifier: GPL-3.0-or-later
#include "builder/PackJob.h"

#include "builder/PackBuild.h"
#include "builder/PackInstall.h"
#include "builder/PackPaths.h"
#include "builder/PackStock.h"

#include <Windows.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <fstream>
#include <map>
#include <functional>
#include <mutex>
#include <sstream>
#include <thread>
#include <vector>

namespace builder::pack::job
{
	namespace
	{
		namespace fs = std::filesystem;

		std::mutex        lock;
		Snapshot          snapshot;
		std::atomic<bool> cancel{ false };
		std::atomic<bool> running{ false };

		void SetPhase(const std::string& a_phase, int a_done = -1, int a_total = -1)
		{
			std::scoped_lock guard(lock);
			snapshot.phase = a_phase;
			if (a_done >= 0) snapshot.done = a_done;
			if (a_total >= 0) snapshot.total = a_total;
		}

		void Finish(State a_state, const std::string& a_message)
		{
			std::scoped_lock guard(lock);
			snapshot.state = a_state;
			snapshot.message = a_message;
			snapshot.phase.clear();
		}

		struct Stop
		{
			std::string message;
		};
		struct Cancelled
		{};

		void CheckCancel()
		{
			if (cancel.load()) throw Cancelled{};
		}

		bool ReadBytes(const fs::path& a_path, Bytes& a_out)
		{
			std::ifstream in(a_path, std::ios::binary);
			if (!in) return false;
			a_out.assign(std::istreambuf_iterator<char>(in), {});
			return true;
		}

		std::string ReadText(const fs::path& a_path)
		{
			std::ifstream     in(a_path, std::ios::binary);
			std::stringstream s;
			s << in.rdbuf();
			return s.str();
		}

		void WriteBytes(const fs::path& a_path, const void* a_data, size_t a_size)
		{
			std::error_code ec;
			fs::create_directories(a_path.parent_path(), ec);
			std::ofstream out(a_path, std::ios::binary | std::ios::trunc);
			out.write(static_cast<const char*>(a_data), static_cast<std::streamsize>(a_size));
			if (!out) throw Stop{ "could not write " + a_path.string() };
		}

		std::wstring Quote(const std::wstring& a_arg)
		{
			if (!a_arg.empty() && a_arg.find_first_of(L" \t\"") == std::wstring::npos) return a_arg;
			std::wstring out = L"\"";
			for (wchar_t c : a_arg) {
				if (c == L'"') out += L'\\';
				out += c;
			}
			return out + L"\"";
		}

		// one child process (no window, below normal priority, killed with the game through the job object); its output is
		// collected; a cancel ends it
		HANDLE ChildJob()
		{
			static HANDLE job = [] {
				HANDLE                               h = CreateJobObjectW(nullptr, nullptr);
				JOBOBJECT_EXTENDED_LIMIT_INFORMATION info{};
				info.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
				if (h) SetInformationJobObject(h, JobObjectExtendedLimitInformation, &info, sizeof(info));
				return h;
			}();
			return job;
		}

		int Run(const fs::path& a_exe, const std::vector<std::wstring>& a_args, std::string& a_output, const std::atomic<bool>& a_cancel)
		{
			a_output.clear();
			SECURITY_ATTRIBUTES sa{ sizeof(sa), nullptr, TRUE };
			HANDLE              readEnd = nullptr, writeEnd = nullptr;
			if (!CreatePipe(&readEnd, &writeEnd, &sa, 0)) throw Stop{ "could not start retoc (pipe)" };
			SetHandleInformation(readEnd, HANDLE_FLAG_INHERIT, 0);
			SIZE_T attrSize = 0;
			InitializeProcThreadAttributeList(nullptr, 1, 0, &attrSize);
			std::vector<std::uint8_t> attrBuffer(attrSize);
			auto* attrs = reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(attrBuffer.data());
			InitializeProcThreadAttributeList(attrs, 1, 0, &attrSize);
			UpdateProcThreadAttribute(attrs, 0, PROC_THREAD_ATTRIBUTE_HANDLE_LIST, &writeEnd, sizeof(HANDLE), nullptr, nullptr);   // only the pipe is inherited
			STARTUPINFOEXW si{};
			si.StartupInfo.cb = sizeof(si);
			si.StartupInfo.dwFlags = STARTF_USESTDHANDLES;
			si.StartupInfo.hStdOutput = writeEnd;
			si.StartupInfo.hStdError = writeEnd;
			si.lpAttributeList = attrs;
			std::wstring cmd = Quote(a_exe.wstring());
			for (const auto& a : a_args) cmd += L" " + Quote(a);
			PROCESS_INFORMATION pi{};
			const BOOL started = CreateProcessW(nullptr, cmd.data(), nullptr, nullptr, TRUE,
				CREATE_NO_WINDOW | BELOW_NORMAL_PRIORITY_CLASS | CREATE_SUSPENDED | EXTENDED_STARTUPINFO_PRESENT, nullptr,
				a_exe.parent_path().c_str(), &si.StartupInfo, &pi);
			DeleteProcThreadAttributeList(attrs);
			CloseHandle(writeEnd);
			if (!started) {
				CloseHandle(readEnd);
				throw Stop{ "could not start " + a_exe.string() + " (error " + std::to_string(GetLastError()) + ")" };
			}
			if (ChildJob()) AssignProcessToJobObject(ChildJob(), pi.hProcess);
			ResumeThread(pi.hThread);
			CloseHandle(pi.hThread);
			bool killed = false;
			char buffer[4096];
			for (;;) {
				DWORD avail = 0;
				while (PeekNamedPipe(readEnd, nullptr, 0, nullptr, &avail, nullptr) && avail) {
					DWORD got = 0;
					if (!ReadFile(readEnd, buffer, std::min<DWORD>(avail, sizeof(buffer)), &got, nullptr) || !got) break;
					a_output.append(buffer, got);
				}
				if (WaitForSingleObject(pi.hProcess, 50) == WAIT_OBJECT_0) break;
				if (a_cancel.load() && !killed) {
					TerminateProcess(pi.hProcess, 1);
					killed = true;
				}
			}
			DWORD avail = 0, got = 0;
			while (PeekNamedPipe(readEnd, nullptr, 0, nullptr, &avail, nullptr) && avail && ReadFile(readEnd, buffer, std::min<DWORD>(avail, sizeof(buffer)), &got, nullptr) && got)
				a_output.append(buffer, got);
			DWORD code = 1;
			GetExitCodeProcess(pi.hProcess, &code);
			CloseHandle(pi.hProcess);
			CloseHandle(readEnd);
			if (killed) throw Cancelled{};
			return static_cast<int>(code);
		}

		std::wstring Wide(const std::string& a_s) { return fs::path(a_s).wstring(); }

		std::string Tail(const std::string& a_s, size_t a_n = 2000) { return a_s.size() > a_n ? a_s.substr(a_s.size() - a_n) : a_s; }

		void Retoc(const fs::path& a_retoc, const std::vector<std::wstring>& a_args, std::string& a_output, const char* a_what,
			const std::atomic<bool>& a_cancel = cancel)
		{
			const int code = Run(a_retoc, a_args, a_output, a_cancel);
			if (code == 0) return;
			std::string hint;
			if (!fs::exists(a_retoc.parent_path() / L"oo2core_9_win64.dll"))
				hint = "\nretoc needs Oodle (oo2core_9_win64.dll) to read the game's containers. It downloads the DLL next to itself on its first run, "
				       "which needs internet and a writable folder.";
			throw Stop{ std::string("retoc ") + a_what + " failed (" + std::to_string(code) + "):\n" + Tail(a_output) + hint };
		}

		std::string Lower(std::string a_s)
		{
			std::transform(a_s.begin(), a_s.end(), a_s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
			return a_s;
		}

		// our own file calls on the work folder use the \\?\ form: retoc's extraction paths pass 260 characters under the game's
		// plugin folder (Phase 3d finding), which the Python builder handled and plain Win32 paths do not
		fs::path Long(const fs::path& a_path)   // RMI-114: backslashes only (PackPaths.h)
		{
			std::error_code ec;
			return LongPath(fs::absolute(a_path, ec));
		}

		std::string ReadIni(const fs::path& a_path)
		{
			std::error_code ec;
			return fs::is_regular_file(a_path, ec) ? ReadText(a_path) : std::string();
		}

		// one pack: retoc to-zen of a stage folder, its manifest, the read-back (retoc verify + list); returns the manifest
		json::Value WritePack(const fs::path& a_retoc, const fs::path& a_stage, const fs::path& a_out, const std::string& a_pack, const BuildRecord& a_record,
			const PackData& a_data, const std::function<void(const std::string&)>& a_note)
		{
			std::error_code ec;
			fs::create_directories(Long(a_out), ec);
			const fs::path utoc = a_out / Wide(a_pack + ".utoc");
			std::string    output;
			Retoc(a_retoc, { L"to-zen", L"--version", L"UE5_3", a_stage.wstring(), utoc.wstring() }, output, "to-zen");
			CheckCancel();
			std::vector<PackFileHash> hashes;
			for (const char* ext : { "pak", "utoc", "ucas" }) {
				Bytes file;
				if (!ReadBytes(Long(a_out / Wide(a_pack + "." + ext)), file)) throw Stop{ "retoc wrote no " + a_pack + "." + ext };
				hashes.push_back({ a_pack + "." + ext, Sha256Hex(file) });
			}
			auto       manifest = Manifest(a_pack, "MenusRevamped.dll", a_record, hashes, &a_data);
			const auto text = json::Dump(manifest) + "\n";
			WriteBytes(Long(a_out / Wide(a_pack + ".json")), text.data(), text.size());
			Retoc(a_retoc, { L"verify", utoc.wstring() }, output, "verify");
			if (Lower(output).find("verified") == std::string::npos)
				throw Stop{ "read-back: retoc could not verify " + a_pack + ".utoc; the installed pack was not changed" };
			Retoc(a_retoc, { L"list", L"--path", utoc.wstring() }, output, "list");
			std::set<std::string> listed;
			std::istringstream    lines(output);
			for (std::string line; std::getline(lines, line);) {
				if (line.find("ExportBundleData") == std::string::npos) continue;
				while (!line.empty() && (line.back() == '\r' || line.back() == ' ')) line.pop_back();
				listed.insert(Lower(line.substr(line.find_last_of(' ') + 1)));
			}
			std::vector<std::string> missing;
			for (const auto& asset : a_record.textures.keys)
				if (!listed.count(Lower("../../../OblivionRemastered/Content/" + asset + ".uasset"))) missing.push_back(AssetName(asset));
			if (!missing.empty()) {
				std::string names;
				for (size_t i = 0; i < missing.size(); ++i) names += (i ? ", " : "") + missing[i];
				throw Stop{ "read-back: the new pack lacks " + std::to_string(missing.size()) + " package(s): " + names + "; the installed pack was not changed" };
			}
			a_note("Read back: " + std::to_string(listed.size()) + " packages verified");
			return manifest;
		}

		FileOps DiskOps()
		{
			FileOps ops;
			ops.exists = [](const fs::path& p) {
				std::error_code ec;
				return fs::is_regular_file(Long(p), ec);
			};
			ops.replace = [](const fs::path& a, const fs::path& b) {
				return MoveFileExW(Long(a).c_str(), Long(b).c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_COPY_ALLOWED | MOVEFILE_WRITE_THROUGH) != 0;
			};
			ops.remove = [](const fs::path& p) {
				std::error_code ec;
				fs::remove(Long(p), ec);
			};
			return ops;
		}

		void WriteStage(const fs::path& a_stage, const std::string& a_asset, const PackageFiles& a_files)
		{
			const auto to = LongAssetPath(Long(a_stage / L"OblivionRemastered" / L"Content"), a_asset).wstring();   // RMI-114
			WriteBytes(to + L".uasset", a_files.uasset.data(), a_files.uasset.size());
			WriteBytes(to + L".uexp", a_files.uexp.data(), a_files.uexp.size());
			if (a_files.hasUbulk) WriteBytes(to + L".ubulk", a_files.ubulk.data(), a_files.ubulk.size());
		}

		// app.build_pack + window.Controller.build: MRCustom_P (with the sizes unless bLayoutPack=0), MRLayout_P, the read-back,
		// both packs put in place together with their .bak, uPreset=2, PackBuilder.log. The colour file was saved by the window.
		void Build(fs::path a_plugins)
		{
			SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_BELOW_NORMAL);
			const fs::path mr = a_plugins / L"MenusRevamped";
			const fs::path retoc = mr / L"PackBuilder" / L"retoc" / L"retoc.exe";
			const fs::path menus = mr / L"PackBuilder" / L"menus";
			const fs::path colours = mr / L"CustomColours.ini";
			const fs::path pluginIni = a_plugins / L"MenusRevamped.ini";
			const fs::path target = mr / L"CustomPack";
			const fs::path logFile = mr / L"PackBuilder" / L"PackBuilder.log";
			const fs::path work = mr / Wide(".PackBuilder-" + std::to_string(GetTickCount64()));
			const fs::path paks = PaksFolder(a_plugins);
			const std::string colourPack = "MRCustom_P", layoutPack = "MRLayout_P";
			std::vector<std::string> log;
			auto note = [&](const std::string& a_line) { log.push_back(a_line); };
			auto writeLog = [&] {
				std::string text = std::string("Menus Revamped - Pack Builder (in game) ") + kBuilderVersion + "\n";
				for (const auto& l : log) text += l + "\n";
				try {
					WriteBytes(Long(logFile), text.data(), text.size());
				} catch (const Stop&) {}
			};
			std::error_code ec;
			const auto      started = std::chrono::steady_clock::now();
			bool            installing = false;
			try {
				SetPhase("Checking the game and the builder files", 0, 0);
				if (!fs::is_regular_file(retoc, ec)) throw Stop{ "retoc not found: " + retoc.string() + " (it ships in MenusRevamped/PackBuilder/retoc)" };
				if (!fs::exists(retoc.parent_path() / L"oo2core_9_win64.dll", ec)) {
					const auto probe = retoc.parent_path() / L".write-test";
					std::ofstream test(probe);
					const bool    writable = static_cast<bool>(test);
					test.close();
					fs::remove(probe, ec);
					if (!writable)
						throw Stop{ "retoc needs Oodle (oo2core_9_win64.dll) to read the game's containers. It downloads the DLL into " +
						            retoc.parent_path().string() + " on its first run, which needs internet and a writable folder." };
				}
				if (!fs::is_directory(paks, ec)) throw Stop{ "the game's Paks folder was not found: " + paks.string() };
				std::vector<std::pair<std::string, std::string>> files;
				for (const auto& e : fs::directory_iterator(menus, ec))
					if (e.path().extension() == L".json") files.emplace_back(e.path().filename().string(), ReadText(e.path()));
				if (files.empty()) throw Stop{ "no data files in " + menus.string() };
				PackData    data;
				std::string error;
				if (!LoadPackData(files, data, error)) throw Stop{ error };
				const bool        haveColours = fs::is_regular_file(colours, ec);
				const std::string colourText = haveColours ? ReadText(colours) : std::string();
				Values            values;
				if (!PackValues(haveColours ? &colourText : nullptr, values, error)) throw Stop{ error };
				const bool layout = LayoutEnabled(ReadIni(pluginIni));
				Sizes      sizes;
				if (layout && !LoadSizes(haveColours ? &colourText : nullptr, sizes, error)) throw Stop{ error };

				fs::remove_all(Long(work), ec);
				fs::create_directories(Long(work), ec);
				const fs::path legacy = work / L"legacy", stage = work / L"stage", layoutStage = work / L"layout-stage", out = work / L"out";
				const auto     names = NeededNames(data, layout);
				note("Reading " + std::to_string(names.size()) + " stock assets from " + paks.string());
				SetPhase("Reading the game's assets (retoc; the first build downloads Oodle)", 0, static_cast<int>(data.textures.size()));
				std::vector<std::wstring> args{ L"to-legacy", L"--no-shaders", L"--version", L"UE5_3" };
				for (const auto& n : names) {
					args.push_back(L"-f");
					args.push_back(Wide(n));
				}
				args.push_back(paks.wstring());
				args.push_back(legacy.wstring());
				std::string output;
				Retoc(retoc, args, output, "to-legacy");
				CheckCancel();

				StockFolder     stock(legacy, menus);   // the recipes' stock and other inputs (PackStock.h, shared with the live preview)
				const ReadStock read = [&](const std::string& a_asset, PackageFiles& a_out) { return stock.Read(a_asset, a_out); };
				const BakeInputs inputs = stock.Inputs();

				BuildRecord record;
				int         done = 0;
				for (const auto& [asset, recipe] : data.textures) {
					CheckCancel();
					SetPhase("Baking " + AssetName(asset), done, -1);
					PackageFiles original;
					BakedAsset   result;
					std::string  reason;
					BakeResult   r = BakeResult::Skipped;
					if (!read(asset, original)) reason = "not extracted from the game (game update or wrong folder?)";
					else r = BakeAsset(asset, recipe, original, values, data, result, reason, layout ? &sizes : nullptr, inputs);
					if (r == BakeResult::Later) throw Stop{ "recipe " + recipe + " (" + AssetName(asset) + ") is not part of this build yet" };
					if (r == BakeResult::Baked) {
						WriteStage(stage, asset, result.files);
						record.Add(asset, result, values);
						note(AssetName(asset) + ": " + recipe);
					} else {
						record.Skip(asset, recipe, reason);
						note("SKIPPED " + AssetName(asset) + ": " + reason);
					}
					++done;
				}
				if (layout) {
					record.SetSizes(sizes);
					std::vector<StagedFiles> staged;
					LayoutEntries(data, sizes, read, record, staged);
					for (const auto& f : staged) WriteStage(stage, f.asset, f.files);
				}
				if (record.textures.keys.empty())
					throw Stop{ "no asset passed its check (" + std::to_string(record.skipped.items.size()) + " skipped): nothing to build" };
				SetPhase("Writing the colour pack (retoc)", done, -1);
				note("Writing the pack");
				const auto colourManifest = WritePack(retoc, stage, out, colourPack, record, data, note);

				json::Value layoutManifest;
				bool        layoutBuilt = false;
				std::string layoutProblem;
				if (layout) {
					try {
						SetPhase("Writing the sizes pack (retoc)", done, -1);
						BuildRecord sizesRecord;
						sizesRecord.layoutPack = true;
						sizesRecord.SetSizes(sizes);
						std::vector<StagedFiles> staged;
						LayoutEntries(data, sizes, read, sizesRecord, staged);
						if (staged.empty())
							throw Stop{ "no layout package passed its check (" + std::to_string(sizesRecord.skipped.items.size()) + " skipped): nothing to build" };
						for (const auto& f : staged) WriteStage(layoutStage, f.asset, f.files);
						layoutManifest = WritePack(retoc, layoutStage, out, layoutPack, sizesRecord, data, note);
						layoutBuilt = true;
					} catch (const Stop& stop) {
						layoutProblem = stop.message;
						note("Sizes pack not built: " + layoutProblem);
					}
				}

				CheckCancel();
				installing = true;   // from here a cancel waits: the packs go in place together
				SetPhase("Putting the packs in place", done, -1);
				fs::create_directories(Long(target), ec);   // install.install: target.mkdir(parents=True) (a first build has no CustomPack)
				const auto ops = DiskOps();
				if (!InstallPack(out, target, colourPack, ops, error)) throw Stop{ error };
				if (layoutBuilt && !InstallPack(out, target, layoutPack, ops, error)) {
					layoutProblem = error;
					layoutBuilt = false;
				}
				std::string message = BuildSummary(colourManifest, layoutBuilt ? &layoutManifest : nullptr, target.string());
				if (!layoutProblem.empty())
					message += "\nThe sizes pack (" + layoutPack + ") was not built: " + layoutProblem + " The installed one was not changed.";
				note(message);
				std::string presetError;
				const auto  ini = ReadIni(pluginIni);
				std::ofstream presetOut(pluginIni, std::ios::binary | std::ios::trunc);   // in place: MenusRevamped.ini is a Vortex hard link (tooling lesson)
				const auto  preset = model::SetPreset(ini, 2);
				presetOut.write(preset.data(), static_cast<std::streamsize>(preset.size()));
				presetOut.close();
				const bool presetOk = static_cast<bool>(presetOut);
				note(presetOk ? "Custom preset set (uPreset=2 in MenusRevamped.ini)." : "Could not set uPreset=2 in MenusRevamped.ini: set it by hand.");
				fs::remove_all(Long(work), ec);
				writeLog();
				const auto seconds = std::chrono::duration_cast<std::chrono::seconds>(std::chrono::steady_clock::now() - started).count();
				const auto skipped = record.skipped.items.size() + (layoutBuilt ? layoutManifest.Find("skipped")->items.size() : 0);
				Finish(State::Done, "Built in " + std::to_string(seconds) + " s: " + std::to_string(record.textures.keys.size()) + " assets baked, " +
				                        std::to_string(skipped) + " skipped" + (layoutBuilt ? ", sizes pack built" : layout ? ", sizes pack NOT built" : "") +
				                        (presetOk ? "; Custom preset set." : "; set uPreset=2 by hand.") +
				                        " Restart the game to see the new pack. Open log has the details.");
			} catch (const Cancelled&) {
				fs::remove_all(Long(work), ec);
				note("Cancelled. The installed packs were not changed.");
				writeLog();
				Finish(State::Cancelled, "Build cancelled. The installed packs were not changed.");
			} catch (const Stop& stop) {
				fs::remove_all(Long(work), ec);
				note("FAILED: " + stop.message);
				writeLog();
				Finish(State::Failed, "Build failed: " + stop.message + (installing ? "" : ". The installed packs were not changed."));
			} catch (const std::exception& e) {
				fs::remove_all(Long(work), ec);
				note(std::string("FAILED: ") + e.what());
				writeLog();
				Finish(State::Failed, std::string("Build failed (") + e.what() + "). The installed packs were not changed.");
			}
			running = false;
		}
	}

	std::filesystem::path PaksFolder(const std::filesystem::path& a_plugins)
	{
		return a_plugins.parent_path().parent_path().parent_path().parent_path() / L"Content" / L"Paks";
	}

	bool ExtractStock(const std::filesystem::path& a_plugins, const std::vector<std::string>& a_names, const std::filesystem::path& a_to,
		const std::atomic<bool>& a_cancel, std::string& a_why)
	{
		const fs::path retoc = a_plugins / L"MenusRevamped" / L"PackBuilder" / L"retoc" / L"retoc.exe";
		std::error_code ec;
		if (!fs::is_regular_file(retoc, ec)) {
			a_why = "retoc not found: " + retoc.string();
			return false;
		}
		std::vector<std::wstring> args{ L"to-legacy", L"--no-shaders", L"--version", L"UE5_3" };
		for (const auto& n : a_names) {
			args.push_back(L"-f");
			args.push_back(Wide(n));
		}
		args.push_back(PaksFolder(a_plugins).wstring());
		args.push_back(a_to.wstring());
		try {
			std::string output;
			Retoc(retoc, args, output, "to-legacy", a_cancel);
			return true;
		} catch (const Stop& stop) {
			a_why = stop.message;
		} catch (const Cancelled&) {
			a_why = "cancelled";
		} catch (const std::exception& e) {
			a_why = e.what();
		}
		return false;
	}

	bool StartBuild(const std::filesystem::path& a_plugins, std::string& a_why)
	{
		bool expected = false;
		if (!running.compare_exchange_strong(expected, true)) {
			a_why = "A build is already running.";
			return false;
		}
		cancel = false;
		{
			std::scoped_lock guard(lock);
			snapshot = Snapshot{ State::Running, 0, 0, "Starting", {} };
		}
		std::thread(Build, a_plugins).detach();
		return true;
	}

	void Cancel() { cancel = true; }

	Snapshot Get()
	{
		std::scoped_lock guard(lock);
		return snapshot;
	}

	void PreviewRunning(int a_done, int a_total, const char* a_phase)
	{
		std::scoped_lock guard(lock);
		snapshot = Snapshot{ State::Running, a_done, a_total, a_phase, {} };
	}
}
