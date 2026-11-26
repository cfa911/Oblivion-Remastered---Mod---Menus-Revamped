#pragma once
// In-game Pack Builder: a retoc to-legacy extraction read as the recipes' stock (Phase 3 build work folder, Phase 4a live
// preview cache). Read: an asset's .uasset/.uexp[/.ubulk] by its data path; Inputs: the recipes' other stock textures by name
// (pipeline.stock_dds: the first extracted .uasset of that name in sorted path order, Windows paths compared without case) and
// the menus data folder's files. Every file call uses the \\?\ form (PackPaths.h, RMI-114). Standard library only.
// SPDX-License-Identifier: GPL-3.0-or-later
#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <map>
#include <string>
#include <utility>
#include <vector>

#include "builder/PackBuild.h"
#include "builder/PackPaths.h"

namespace builder::pack
{
	class StockFolder
	{
	public:
		// a_legacy: the folder retoc wrote (it holds OblivionRemastered/Content); a_menus: the data files' folder
		StockFolder(const std::filesystem::path& a_legacy, const std::filesystem::path& a_menus) :
			_legacy(Long(a_legacy)), _content(Long(a_legacy / L"OblivionRemastered" / L"Content")), _menus(a_menus)
		{}

		bool Read(const std::string& a_asset, PackageFiles& a_out) const
		{
			const auto base = LongAssetPath(_content, a_asset).wstring();   // RMI-114: "Art/UI/..." as backslashes
			a_out = {};
			if (!ReadBytes(base + L".uasset", a_out.uasset) || !ReadBytes(base + L".uexp", a_out.uexp)) return false;
			a_out.hasUbulk = ReadBytes(base + L".ubulk", a_out.ubulk);
			return true;
		}

		// the recipes' inputs; the name index is built on the first call (the folder must be complete by then)
		BakeInputs Inputs()
		{
			if (!_indexed) Index();
			BakeInputs inputs;
			inputs.stock = [this](const std::string& a_name, Dds& a_out) {
				const auto it = _byName.find(Lower(a_name));
				if (it == _byName.end()) return false;
				PackageFiles f;
				auto         base = it->second;
				base.replace_extension();
				if (!ReadBytes(base.wstring() + L".uasset", f.uasset) || !ReadBytes(base.wstring() + L".uexp", f.uexp)) return false;
				f.hasUbulk = ReadBytes(base.wstring() + L".ubulk", f.ubulk);
				TexturePackage tex;
				std::string    why;
				if (!ParseTexture(f.uasset, f.uexp, f.hasUbulk ? &f.ubulk : nullptr, tex, why)) return false;
				a_out = { tex.format, tex.width, tex.height, static_cast<int>(tex.mips.size()), GatherMips(tex, f.uexp, f.hasUbulk ? &f.ubulk : nullptr) };
				return true;
			};
			inputs.menusFile = [this](const std::string& a_file, Bytes& a_out) { return ReadBytes(Long(_menus / std::filesystem::path(a_file)), a_out); };
			// no owner art in shipped builds: the png recipe (development only, no data row) is skipped and listed
			return inputs;
		}

		static bool ReadBytes(const std::filesystem::path& a_path, Bytes& a_out)
		{
			std::ifstream in(a_path, std::ios::binary);
			if (!in) return false;
			a_out.assign(std::istreambuf_iterator<char>(in), {});
			return true;
		}

		static std::filesystem::path Long(const std::filesystem::path& a_path)
		{
			std::error_code ec;
			return LongPath(std::filesystem::absolute(a_path, ec));
		}

	private:
		static std::string Lower(std::string a_s)
		{
			std::transform(a_s.begin(), a_s.end(), a_s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
			return a_s;
		}

		void Index()
		{
			_indexed = true;
			std::error_code                                         ec;
			std::vector<std::pair<std::string, std::filesystem::path>> all;
			for (const auto& e : std::filesystem::recursive_directory_iterator(_legacy, ec))
				if (e.is_regular_file() && e.path().extension() == L".uasset") all.emplace_back(Lower(e.path().generic_string()), e.path());
			std::sort(all.begin(), all.end());
			for (const auto& [key, path] : all) _byName.emplace(Lower(path.stem().string()), path);
		}

		std::filesystem::path                        _legacy, _content, _menus;
		bool                                         _indexed = false;
		std::map<std::string, std::filesystem::path> _byName;
	};
}
