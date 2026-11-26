// Driver for tests/ingame_pack_parity_tests.py: bakes every asset of the data files with the C++ build pipeline
// (src/builder/PackBuild.h) from a retoc to-legacy extraction, so the test can compare each staged file with the Python
// builder's byte for byte.
//   driver <menus folder> <legacy folder (holds OblivionRemastered/Content)> <CustomColours.ini or -> <out folder> [--sizes]
//          [--extra <file>] [--art <folder>]
// Phase 3c: other stock textures by name (pipeline's stock_dds: the first extracted .uasset of that name), the menus data
// files and the owner art folder reach the recipes through BakeInputs. --extra: a JSON list of [asset, recipe] pairs (recipes
// no data row uses) baked into <out>/extra-stage/<row index> and reported under "extra".
// Writes <out>/stage/OblivionRemastered/Content/<asset>.uasset/.uexp[/.ubulk] and <out>/report.json:
//   {"values": {key: value}, "assets": [{"asset", "recipe", "result": baked|skipped|later, "reason", "entry"}]}
// --sizes (Phase 3d, tests/ingame_pack_layout_parity_tests.py): the [Sizes] of the file (- = today's sizes) are baked too, the
// layout-only packages staged after the colour assets (pipeline._build), MRLayout_P staged into <out>/layout-stage
// (pipeline.build_layout), and both manifests written into <out>/colour-pack/MRCustom_P.json and
// <out>/layout-pack/MRLayout_P.json over stand-in pack files (<pack>.<ext> holding the text "<pack>.<ext>"); report.json then
// also has "sizes" (used_sizes) and "layout_order" (the layout packages in data order).
// Phase 4a: every baked texture row of the data files is baked again through the live preview (LivePreviewCore.h BakePreview);
// report.json "preview": {"textures": n, "mismatches": [assets whose preview bytes differ from the staged package's mips]}.
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <algorithm>
#include <cctype>
#include <sstream>
#include "../src/builder/PackBuild.h"
#include "../src/builder/LivePreviewCore.h"

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
	bool     withSizes = false;
	fs::path extraFile, artFolder;
	for (int i = 5; i < argc; ++i) {
		const std::string a = argv[i];
		if (a == "--sizes") withSizes = true;
		else if (a == "--extra" && i + 1 < argc) extraFile = argv[++i];
		else if (a == "--art" && i + 1 < argc) artFolder = argv[++i];
		else argc = 0;
	}
	if (argc < 5) {
		std::cerr << "usage: driver <menus> <legacy> <ini|-> <out> [--sizes] [--extra <file>] [--art <folder>]\n";
		return 2;
	}
	const fs::path menus = argv[1], legacy = fs::path(argv[2]) / "OblivionRemastered/Content", out = argv[4];
	std::map<std::string, fs::path> byName;   // lower-case stem -> the first .uasset in sorted path order
	{
		std::vector<std::pair<std::string, fs::path>> all;
		for (const auto& e : fs::recursive_directory_iterator(argv[2]))
			if (e.is_regular_file() && e.path().extension() == ".uasset") {
				std::string key = e.path().generic_string();
				for (auto& ch : key) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
				all.emplace_back(key, e.path());
			}
		std::sort(all.begin(), all.end());
		for (const auto& [key, path] : all) {
			std::string stem = path.stem().string();
			for (auto& ch : stem) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
			byName.emplace(stem, path);
		}
	}
	BakeInputs inputs;
	inputs.stock = [&](const std::string& a_name, Dds& a_out) {
		std::string key = a_name;
		for (auto& ch : key) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
		const auto it = byName.find(key);
		if (it == byName.end()) return false;
		PackageFiles f;
		auto         base = it->second;
		base.replace_extension();
		if (!ReadBytes(fs::path(base.string() + ".uasset"), f.uasset) || !ReadBytes(fs::path(base.string() + ".uexp"), f.uexp)) return false;
		f.hasUbulk = ReadBytes(fs::path(base.string() + ".ubulk"), f.ubulk);
		TexturePackage tex;
		std::string    why;
		if (!ParseTexture(f.uasset, f.uexp, f.hasUbulk ? &f.ubulk : nullptr, tex, why)) return false;
		a_out = { tex.format, tex.width, tex.height, static_cast<int>(tex.mips.size()), GatherMips(tex, f.uexp, f.hasUbulk ? &f.ubulk : nullptr) };
		return true;
	};
	inputs.menusFile = [&](const std::string& a_file, Bytes& a_out) { return ReadBytes(menus / a_file, a_out); };
	inputs.artFolder = artFolder.string();
	inputs.art = [&](const std::string& a_name, Bytes& a_out) { return !artFolder.empty() && ReadBytes(artFolder / (a_name + ".png"), a_out); };
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
	size_t      previewed = 0;
	auto        previewMismatches = json::MakeArray();
	auto report = json::MakeObject();
	auto vals = json::MakeObject();
	for (const auto& [k, v] : values) json::Set(vals, k, ValueJson(v));
	json::Set(report, "values", vals);
	auto bakeAll = [&](const std::vector<std::pair<std::string, std::string>>& a_list, const fs::path& a_stage, bool a_record) {
	auto assets = json::MakeArray();
	size_t index = 0;
	for (const auto& [asset, recipe] : a_list) {
		const fs::path stageRoot = a_record ? a_stage : a_stage / std::to_string(index++);   // extras: one folder per row (an asset can repeat)
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
			result = BakeAsset(asset, recipe, stock, values, data, baked, reason, withSizes ? &sizes : nullptr, inputs);
		}
		json::Set(row, "result", json::MakeString(result == BakeResult::Baked ? "baked" : result == BakeResult::Later ? "later" : "skipped"));
		json::Set(row, "reason", json::MakeString(reason));
		if (result == BakeResult::Baked) {
			json::Set(row, "entry", baked.entry);
			const auto target = stageRoot / "OblivionRemastered/Content" / asset;
			WriteBytes(fs::path(target.string() + ".uasset"), baked.files.uasset);
			WriteBytes(fs::path(target.string() + ".uexp"), baked.files.uexp);
			if (baked.files.hasUbulk) WriteBytes(fs::path(target.string() + ".ubulk"), baked.files.ubulk);
			if (a_record) record.Add(asset, baked, values);
			if (a_record && Recipe(recipe, data).kind == RecipeKind::Texture) {   // the live preview gives the bytes the build staged
				builder::pack::live::Preview preview;
				std::string                  why;
				TexturePackage               tex;
				const auto&                  f = baked.files;
				const bool same = builder::pack::live::BakePreview({ asset, recipe }, stock, values, data, inputs, preview, why) &&
				                  ParseTexture(f.uasset, f.uexp, f.hasUbulk ? &f.ubulk : nullptr, tex, why) &&
				                  preview.body == GatherMips(tex, f.uexp, f.hasUbulk ? &f.ubulk : nullptr);
				++previewed;
				if (!same) previewMismatches.items.push_back(json::MakeString(asset));
			}
		} else if (result == BakeResult::Skipped && a_record)
			record.Skip(asset, recipe, reason);
		assets.items.push_back(std::move(row));
	}
	return assets;
	};
	json::Set(report, "assets", bakeAll(data.textures, out / "stage", true));
	{
		auto preview = json::MakeObject();
		json::Value n;
		n.type = json::Type::Int;
		n.i = static_cast<std::int64_t>(previewed);
		json::Set(preview, "textures", n);
		json::Set(preview, "mismatches", previewMismatches);
		json::Set(report, "preview", preview);
	}
	if (!extraFile.empty()) {
		json::Value list;
		std::string why;
		if (!json::Parse(ReadText(extraFile), list, why)) {
			std::cerr << "extra: " << why << "\n";
			return 6;
		}
		std::vector<std::pair<std::string, std::string>> extra;
		for (const auto& pair : list.items) extra.emplace_back(pair.items[0].s, pair.items[1].s);
		json::Set(report, "extra", bakeAll(extra, out / "extra-stage", false));
	}
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
