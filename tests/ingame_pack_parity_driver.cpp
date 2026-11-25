// Driver for tests/ingame_pack_parity_tests.py: bakes every asset of the data files with the C++ build pipeline
// (src/builder/PackBuild.h) from a retoc to-legacy extraction, so the test can compare each staged file with the Python
// builder's byte for byte.
//   driver <menus folder> <legacy folder (holds OblivionRemastered/Content)> <CustomColours.ini or -> <out folder>
// Writes <out>/stage/OblivionRemastered/Content/<asset>.uasset/.uexp[/.ubulk] and <out>/report.json:
//   {"values": {key: value}, "assets": [{"asset", "recipe", "result": baked|skipped|later, "reason", "entry"}]}
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include "../src/builder/PackBuild.h"

namespace fs = std::filesystem;
using namespace builder::pack;

static bool ReadBytes(const fs::path& p, Bytes& out)
{
	std::ifstream in(p, std::ios::binary);
	if (!in) return false;
	out.assign(std::istreambuf_iterator<char>(in), {});
	return true;
}

static void WriteBytes(const fs::path& p, const Bytes& b)
{
	fs::create_directories(p.parent_path());
	std::ofstream out(p, std::ios::binary | std::ios::trunc);
	out.write(reinterpret_cast<const char*>(b.data()), static_cast<std::streamsize>(b.size()));
}

static std::string ReadText(const fs::path& p)
{
	std::ifstream     in(p, std::ios::binary);
	std::stringstream s;
	s << in.rdbuf();
	return s.str();
}

int main(int argc, char** argv)
{
	if (argc != 5) {
		std::cerr << "usage: driver <menus> <legacy> <ini|-> <out>\n";
		return 2;
	}
	const fs::path menus = argv[1], legacy = fs::path(argv[2]) / "OblivionRemastered/Content", out = argv[4];
	std::vector<std::pair<std::string, std::string>> files;
	for (const auto& e : fs::directory_iterator(menus))
		if (e.path().extension() == ".json") files.emplace_back(e.path().filename().string(), ReadText(e.path()));
	PackData    data;
	std::string error;
	if (!LoadPackData(files, data, error)) {
		std::cerr << "data: " << error << "\n";
		return 3;
	}
	Values      values;
	std::string ini;
	const bool  haveIni = std::string(argv[3]) != "-";
	if (haveIni) ini = ReadText(argv[3]);
	if (!PackValues(haveIni ? &ini : nullptr, values, error)) {
		std::cerr << "colours: " << error << "\n";
		return 4;
	}
	auto report = json::MakeObject();
	auto vals = json::MakeObject();
	for (const auto& [k, v] : values) json::Set(vals, k, ValueJson(v));
	json::Set(report, "values", vals);
	auto assets = json::MakeArray();
	for (const auto& [asset, recipe] : data.textures) {
		PackageFiles stock;
		const auto   base = legacy / asset;
		auto         row = json::MakeObject();
		json::Set(row, "asset", json::MakeString(asset));
		json::Set(row, "recipe", json::MakeString(recipe));
		BakedAsset  baked;
		std::string reason;
		BakeResult  result = BakeResult::Skipped;
		if (!ReadBytes(fs::path(base.string() + ".uasset"), stock.uasset) || !ReadBytes(fs::path(base.string() + ".uexp"), stock.uexp))
			reason = "not extracted";
		else {
			stock.hasUbulk = ReadBytes(fs::path(base.string() + ".ubulk"), stock.ubulk);
			result = BakeAsset(asset, recipe, stock, values, data, baked, reason);
		}
		json::Set(row, "result", json::MakeString(result == BakeResult::Baked ? "baked" : result == BakeResult::Later ? "later" : "skipped"));
		json::Set(row, "reason", json::MakeString(reason));
		if (result == BakeResult::Baked) {
			json::Set(row, "entry", baked.entry);
			const auto target = out / "stage/OblivionRemastered/Content" / asset;
			WriteBytes(fs::path(target.string() + ".uasset"), baked.files.uasset);
			WriteBytes(fs::path(target.string() + ".uexp"), baked.files.uexp);
			if (baked.files.hasUbulk) WriteBytes(fs::path(target.string() + ".ubulk"), baked.files.ubulk);
		}
		assets.items.push_back(std::move(row));
	}
	json::Set(report, "assets", assets);
	fs::create_directories(out);
	std::ofstream(out / "report.json", std::ios::binary) << json::Dump(report);
	return 0;
}
