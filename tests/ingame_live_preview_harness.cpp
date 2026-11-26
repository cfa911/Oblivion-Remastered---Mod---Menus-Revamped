// Developer harness for the Phase 4a live preview's worker path outside the game (src/builder/LivePreview.cpp does the same
// steps on its worker): the stock extraction the preview caches (PackJob ExtractStock, timed), then the preview of every
// texture the "lines" role reaches, baked from that extraction (PackStock.h + LivePreviewCore.h) with a changed colour.
//   harness <plugins folder> <out folder>   (the plugins folder laid out like for tests/ingame_pack_job_harness.cpp)
// Build: cl /std:c++latest /EHsc /utf-8 /DNOMINMAX /I src tests/ingame_live_preview_harness.cpp src/builder/PackJob.cpp
#include <atomic>
#include <chrono>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>
#include "../src/builder/LivePreviewCore.h"
#include "../src/builder/PackJob.h"
#include "../src/builder/PackStock.h"

namespace fs = std::filesystem;
using namespace builder::pack;

int wmain(int argc, wchar_t** argv)
{
	if (argc != 3) {
		std::printf("usage: harness <plugins folder> <out folder>\n");
		return 2;
	}
	const fs::path plugins = argv[1], out = argv[2], menus = plugins / L"MenusRevamped" / L"PackBuilder" / L"menus";
	std::vector<std::pair<std::string, std::string>> files;
	for (const auto& e : fs::directory_iterator(menus))
		if (e.path().extension() == L".json") {
			std::ifstream     in(e.path(), std::ios::binary);
			std::stringstream s;
			s << in.rdbuf();
			files.emplace_back(e.path().filename().string(), s.str());
		}
	PackData    data;
	std::string error;
	if (!LoadPackData(files, data, error)) {
		std::printf("data: %s\n", error.c_str());
		return 3;
	}
	const auto names = NeededNames(data, false);
	std::error_code ec;
	fs::remove_all(StockFolder::Long(out), ec);
	std::atomic<bool> cancel{ false };
	const auto        started = std::chrono::steady_clock::now();
	if (!job::ExtractStock(plugins, names, out, cancel, error)) {
		std::printf("extract: %s\n", error.c_str());
		return 4;
	}
	const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - started).count();
	std::printf("EXTRACT %zu names in %lld ms\n", names.size(), static_cast<long long>(ms));

	StockFolder stock(out, menus);
	const auto  inputs = stock.Inputs();
	Values      values;
	PackValues(nullptr, values, error);
	values["columnLine"] = builder::model::Value{ 3, { 220, 40, 40 } };
	const auto targets = live::AffectedTextures(data, live::LiveKeys());
	int        ok = 0, bad = 0;
	const auto bakeStart = std::chrono::steady_clock::now();
	for (const auto& t : targets) {
		PackageFiles  files2;
		live::Preview p;
		std::string   why;
		if (stock.Read(t.asset, files2) && live::BakePreview(t, files2, values, data, inputs, p, why)) {
			++ok;
			std::printf("  %s %dx%d mips %zu format %d bytes %zu\n", AssetName(t.asset).c_str(), p.width, p.height, p.mips.size(), static_cast<int>(p.format), p.body.size());
		} else {
			++bad;
			std::printf("  FAILED %s: %s\n", t.asset.c_str(), why.c_str());
		}
	}
	const auto bakeMs = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - bakeStart).count();
	std::printf("PREVIEW lines: %zu textures, %d baked, %d failed, %lld ms\n", targets.size(), ok, bad, static_cast<long long>(bakeMs));
	return bad ? 1 : 0;
}
