#pragma once
// In-game Pack Builder (Phase 3e, RMI-114): paths into the build's work folder. Our own file calls use the \\?\ long-path
// form (retoc's extraction paths pass 260 characters under the plugin folder), and Windows does not convert '/' inside a
// \\?\ path, so every piece (the data files' asset paths are "Art/UI/...") is joined and turned into backslashes first.
// Pure: tests/ingame_pack_paths_tests.cpp.
// SPDX-License-Identifier: GPL-3.0-or-later
#include <filesystem>
#include <string>

namespace builder::pack
{
	// a_base (absolute) as \\?\<base>, every separator a backslash
	inline std::filesystem::path LongPath(const std::filesystem::path& a_base)
	{
		std::wstring full = a_base.wstring();
		if (full.starts_with(L"\\\\?\\")) return std::filesystem::path(full).make_preferred();
		return std::filesystem::path(L"\\\\?\\" + a_base.lexically_normal().make_preferred().wstring());
	}

	// <base>/<asset path from the data files> in the long form
	inline std::filesystem::path LongAssetPath(const std::filesystem::path& a_base, const std::string& a_asset)
	{
		return (LongPath(a_base) / std::filesystem::path(a_asset)).make_preferred();
	}
}
