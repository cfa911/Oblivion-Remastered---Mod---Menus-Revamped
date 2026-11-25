// In-game Pack Builder (Phase 3a): the build on a background thread (see PackJob.h). The pipeline steps follow
// tools/pack_builder/pipeline.py _build and app.build_pack: retoc to-legacy of every needed name, bake each asset
// (src/builder/PackBuild.h), retoc to-zen, the manifest, the read-back (retoc verify + list), then the files are moved into
// place. KB mods/menus-revamped/evidence/in-game-pack-builder-plan.md (Phase 3).
// SPDX-License-Identifier: GPL-3.0-or-later
#include "builder/PackJob.h"

#include "builder/PackBuild.h"

#include <Windows.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <fstream>
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

		int Run(const fs::path& a_exe, const std::vector<std::wstring>& a_args, std::string& a_output)
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
				if (cancel.load() && !killed) {
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

		void Retoc(const fs::path& a_retoc, const std::vector<std::wstring>& a_args, std::string& a_output, const char* a_what)
		{
			const int code = Run(a_retoc, a_args, a_output);
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

		void TestBuild(fs::path a_plugins)
		{
			SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_BELOW_NORMAL);
			const fs::path mr = a_plugins / L"MenusRevamped";
			const fs::path retoc = mr / L"PackBuilder" / L"retoc" / L"retoc.exe";
			const fs::path menus = mr / L"PackBuilder" / L"menus";
			const fs::path colours = mr / L"CustomColours.ini";
			const fs::path target = mr / L"CustomPack" / L"TestBuild";
			const fs::path work = mr / L"CustomPack" / L".PackBuilder-test";
			const fs::path paks = a_plugins.parent_path().parent_path().parent_path().parent_path() / L"Content" / L"Paks";
			const std::string pack = "MRCustom_P";
			std::vector<std::string> log;
			auto note = [&](const std::string& a_line) { log.push_back(a_line); };
			auto writeLog = [&] {
				std::string text;
				for (const auto& l : log) text += l + "\r\n";
				try {
					WriteBytes(target / L"TestBuild.log", text.data(), text.size());
				} catch (const Stop&) {}
			};
			std::error_code ec;
			const auto      started = std::chrono::steady_clock::now();
			try {
				note("Menus Revamped in-game Pack Builder: test build (Phase 3a, the recipes ported so far; this pack is never installed)");
				SetPhase("Checking the game and the builder files", 0, 0);
				if (!fs::is_regular_file(retoc, ec)) throw Stop{ "retoc not found: " + retoc.string() + " (it ships in MenusRevamped/PackBuilder/retoc)" };
				if (!fs::is_directory(paks, ec)) throw Stop{ "the game's Paks folder was not found: " + paks.string() };
				std::vector<std::pair<std::string, std::string>> files;
				for (const auto& e : fs::directory_iterator(menus, ec))
					if (e.path().extension() == L".json") files.emplace_back(e.path().filename().string(), ReadText(e.path()));
				if (files.empty()) throw Stop{ "no data files in " + menus.string() };
				PackData    data;
				std::string error;
				if (!LoadPackData(files, data, error)) throw Stop{ error };
				Values      values;
				const bool  haveColours = fs::is_regular_file(colours, ec);
				std::string colourText = haveColours ? ReadText(colours) : std::string();
				if (!PackValues(haveColours ? &colourText : nullptr, values, error)) throw Stop{ error };
				fs::remove_all(work, ec);
				fs::create_directories(work, ec);
				const fs::path legacy = work / L"legacy", stage = work / L"stage", out = work / L"out";
				const auto     names = NeededNames(data, false);
				note("Reading " + std::to_string(names.size()) + " stock assets from " + paks.string());
				SetPhase("Reading the game's assets (retoc)", 0, static_cast<int>(data.textures.size()));
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

				BuildRecord record;
				int         done = 0, baked = 0, later = 0;
				const auto  content = legacy / L"OblivionRemastered" / L"Content";
				for (const auto& [asset, recipe] : data.textures) {
					CheckCancel();
					SetPhase("Baking " + AssetName(asset), done, -1);
					PackageFiles stock;
					const auto   base = (content / Wide(asset)).wstring();
					BakedAsset   result;
					std::string  reason;
					BakeResult   r = BakeResult::Skipped;
					if (!ReadBytes(base + L".uasset", stock.uasset) || !ReadBytes(base + L".uexp", stock.uexp))
						reason = "not extracted from the game (game update or wrong folder?)";
					else {
						stock.hasUbulk = ReadBytes(base + L".ubulk", stock.ubulk);
						r = BakeAsset(asset, recipe, stock, values, data, result, reason);
					}
					if (r == BakeResult::Baked) {
						const auto to = (stage / L"OblivionRemastered" / L"Content" / Wide(asset)).wstring();
						WriteBytes(to + L".uasset", result.files.uasset.data(), result.files.uasset.size());
						WriteBytes(to + L".uexp", result.files.uexp.data(), result.files.uexp.size());
						if (result.files.hasUbulk) WriteBytes(to + L".ubulk", result.files.ubulk.data(), result.files.ubulk.size());
						record.Add(asset, result, values);
						++baked;
						note(AssetName(asset) + ": " + recipe);
					} else if (r == BakeResult::Later) {
						++later;
						note("later phase " + AssetName(asset) + ": " + recipe);
					} else {
						record.Skip(asset, recipe, reason);
						note("SKIPPED " + AssetName(asset) + ": " + reason);
					}
					++done;
				}
				SetPhase("Writing the pack (retoc)", done, -1);
				if (!baked) throw Stop{ "no asset passed its check: nothing to build" };
				fs::create_directories(out, ec);
				const fs::path utoc = out / Wide(pack + ".utoc");
				Retoc(retoc, { L"to-zen", L"--version", L"UE5_3", stage.wstring(), utoc.wstring() }, output, "to-zen");
				CheckCancel();
				std::vector<PackFileHash> hashes;
				for (const char* ext : { "pak", "utoc", "ucas" }) {
					Bytes file;
					if (!ReadBytes(out / Wide(pack + "." + ext), file)) throw Stop{ "retoc wrote no " + pack + "." + ext };
					hashes.push_back({ pack + "." + ext, Sha256Hex(file) });
				}
				const auto manifest = json::Dump(Manifest(pack, "MenusRevamped.dll", record, hashes)) + "\n";
				WriteBytes(out / Wide(pack + ".json"), manifest.data(), manifest.size());

				SetPhase("Reading the new pack back (retoc)", done, -1);
				Retoc(retoc, { L"verify", utoc.wstring() }, output, "verify");
				if (Lower(output).find("verified") == std::string::npos) throw Stop{ "read-back: retoc could not verify " + pack + ".utoc" };
				Retoc(retoc, { L"list", L"--path", utoc.wstring() }, output, "list");
				std::set<std::string> listed;
				std::istringstream    lines(output);
				for (std::string line; std::getline(lines, line);) {
					if (line.find("ExportBundleData") == std::string::npos) continue;
					while (!line.empty() && (line.back() == '\r' || line.back() == ' ')) line.pop_back();
					listed.insert(Lower(line.substr(line.find_last_of(' ') + 1)));
				}
				int missing = 0;
				for (const auto& asset : record.textures.keys)
					missing += !listed.count(Lower("../../../OblivionRemastered/Content/" + asset + ".uasset"));
				if (missing) throw Stop{ "read-back: the new pack lacks " + std::to_string(missing) + " package(s)" };
				note("Read back: " + std::to_string(listed.size()) + " packages verified");

				fs::create_directories(target, ec);
				for (const char* ext : { "pak", "utoc", "ucas", "json" }) {
					const auto to = target / Wide(pack + "." + ext);
					fs::remove(to, ec);
					fs::rename(out / Wide(pack + "." + ext), to, ec);
					if (ec) throw Stop{ "could not move the test pack into " + target.string() + " (" + ec.message() + ")" };
				}
				fs::remove_all(work, ec);
				const auto seconds = std::chrono::duration_cast<std::chrono::seconds>(std::chrono::steady_clock::now() - started).count();
				const std::string message = "Test build done in " + std::to_string(seconds) + " s: " + std::to_string(baked) + " baked, " +
				                            std::to_string(record.skipped.items.size()) + " skipped, " + std::to_string(later) +
				                            " for later phases. Written to CustomPack\\TestBuild (not installed).";
				note(message);
				writeLog();
				Finish(State::Done, message);
			} catch (const Cancelled&) {
				fs::remove_all(work, ec);
				note("Cancelled.");
				writeLog();
				Finish(State::Cancelled, "Test build cancelled. Nothing was written.");
			} catch (const Stop& stop) {
				fs::remove_all(work, ec);
				note("FAILED: " + stop.message);
				writeLog();
				Finish(State::Failed, "Test build failed: " + stop.message);
			} catch (const std::exception& e) {
				fs::remove_all(work, ec);
				note(std::string("FAILED: ") + e.what());
				writeLog();
				Finish(State::Failed, std::string("Test build failed: ") + e.what());
			}
			running = false;
		}
	}

	bool StartTestBuild(const std::filesystem::path& a_plugins, std::string& a_why)
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
		std::thread(TestBuild, a_plugins).detach();
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
