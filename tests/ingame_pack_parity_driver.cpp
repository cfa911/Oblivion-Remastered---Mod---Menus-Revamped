// Driver for tests/ingame_pack_parity_tests.py: bakes every asset of the data files with the C++ build pipeline
// (src/builder/PackBuild.h) from a retoc to-legacy extraction, so the test can compare each staged file with the Python
// builder's byte for byte.
//   driver <menus folder> <legacy folder (holds OblivionRemastered/Content)> <CustomColours.ini or -> <out folder> [--sizes]
// Writes <out>/stage/OblivionRemastered/Content/<asset>.uasset/.uexp[/.ubulk] and <out>/report.json:
//   {"values": {key: value}, "assets": [{"asset", "recipe", "result": baked|skipped|later, "reason", "entry"}]}
// --sizes (Phase 3d, tests/ingame_pack_layout_parity_tests.py): the [Sizes] of the file (- = today's sizes) are baked too, the
// layout-only packages staged after the colour assets (pipeline._build), MRLayout_P staged into <out>/layout-stage
// (pipeline.build_layout), and both manifests written into <out>/colour-pack/MRCustom_P.json and
// <out>/layout-pack/MRLayout_P.json over stand-in pack files (<pack>.<ext> holding the text "<pack>.<ext>"); report.json then
// also has "sizes" (used_sizes) and "layout_order" (the layout packages in data order).
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
	const bool withSizes = argc == 6 && std::string(argv[5]) == "--sizes";
	if (argc != 5 && !withSizes) {
		std::cerr << "usage: driver <menus> <legacy> <ini|-> <out> [--sizes]\n";
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
	Sizes sizes;
	if (withSizes && !LoadSizes(haveIni ? &ini : nullptr, sizes, error)) {
		std::cerr << "sizes: " << error << "\n";
		return 5;
	}
	BuildRecord record;
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
			result = BakeAsset(asset, recipe, stock, values, data, baked, reason, withSizes ? &sizes : nullptr);
		}
		json::Set(row, "result", json::MakeString(result == BakeResult::Baked ? "baked" : result == BakeResult::Later ? "later" : "skipped"));
		json::Set(row, "reason", json::MakeString(reason));
		if (result == BakeResult::Baked) {
			json::Set(row, "entry", baked.entry);
			const auto target = out / "stage/OblivionRemastered/Content" / asset;
			WriteBytes(fs::path(target.string() + ".uasset"), baked.files.uasset);
			WriteBytes(fs::path(target.string() + ".uexp"), baked.files.uexp);
			if (baked.files.hasUbulk) WriteBytes(fs::path(target.string() + ".ubulk"), baked.files.ubulk);
			record.Add(asset, baked, values);
		} else if (result == BakeResult::Skipped)
			record.Skip(asset, recipe, reason);
		assets.items.push_back(std::move(row));
	}
	json::Set(report, "assets", assets);
	if (withSizes) {
		const ReadStock read = [&](const std::string& a_asset, PackageFiles& a_out) {
			const auto base = legacy / a_asset;
			return ReadBytes(fs::path(base.string() + ".uasset"), a_out.uasset) && ReadBytes(fs::path(base.string() + ".uexp"), a_out.uexp);
		};
		auto stageAll = [&](const fs::path& a_stage, const std::vector<StagedFiles>& a_files) {
			for (const auto& f : a_files) {
				const auto target = a_stage / "OblivionRemastered/Content" / f.asset;
				WriteBytes(fs::path(target.string() + ".uasset"), f.files.uasset);
				WriteBytes(fs::path(target.string() + ".uexp"), f.files.uexp);
			}
		};
		auto manifest = [&](const fs::path& a_folder, const std::string& a_pack, const BuildRecord& a_record) {
			std::vector<PackFileHash> hashes;
			for (const char* ext : { "pak", "utoc", "ucas" }) {
				const auto  name = a_pack + "." + ext;
				const Bytes body(name.begin(), name.end());
				WriteBytes(a_folder / name, body);
				hashes.push_back({ name, Sha256Hex(body) });
			}
			std::ofstream(a_folder / (a_pack + ".json"), std::ios::binary) << json::Dump(Manifest(a_pack, "tools/make_dark_pack.py", a_record, hashes, &data)) + "\n";
		};
		record.SetSizes(sizes);
		std::vector<StagedFiles> staged;
		LayoutEntries(data, sizes, read, record, staged);
		stageAll(out / "stage", staged);
		manifest(out / "colour-pack", "MRCustom_P", record);
		BuildRecord layout;
		layout.layoutPack = true;
		layout.SetSizes(sizes);
		staged.clear();
		LayoutEntries(data, sizes, read, layout, staged);
		stageAll(out / "layout-stage", staged);
		manifest(out / "layout-pack", "MRLayout_P", layout);
		json::Set(report, "sizes", UsedSizes(sizes));
		auto order = json::MakeArray();
		for (const auto& package : data.layoutPackages) order.items.push_back(json::MakeString(package));
		json::Set(report, "layout_order", order);
	}
	fs::create_directories(out);
	std::ofstream(out / "report.json", std::ios::binary) << json::Dump(report);
	return 0;
}
