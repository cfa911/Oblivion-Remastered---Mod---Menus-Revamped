#pragma once
// In-game Pack Builder (Phase 3a): what the build bakes, read from the same data files as the Python builder
// (PackBuilder/menus/<menu>.json: tools/pack_builder/pipeline.py load_menus, load_literals, load_layout), and the colour
// values the recipes read (custom_colours.pack_values: [Colours] and [Tab] over the Dark defaults, the baked [Skin] tints as
// "Skin.<name>"). Pure: tests/ingame_pack_core_tests.cpp, tests/ingame_pack_parity_tests.py.
// SPDX-License-Identifier: GPL-3.0-or-later
#include <algorithm>
#include <array>
#include <cstdint>
#include <map>
#include <set>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "builder/ColourModel.h"
#include "builder/PackJson.h"

namespace builder::pack
{
	using Values = std::map<std::string, model::Value>;

	struct BlueprintColour
	{
		std::int64_t          offset = 0;
		std::array<double, 4> stock{};
		std::string           colour;
		bool                  hasScale = false;
		double                scale = 1;
	};

	struct Literal
	{
		std::int64_t offset = 0;
		std::string  type;   // "float" (default), "double", "int32"
		double       stock = 0;
		json::Value  value;
		std::string  widget;
	};

	struct RichRow
	{
		std::int64_t          offset = 0;
		std::array<double, 4> stock{};
	};

	struct PackData
	{
		std::vector<std::pair<std::string, std::string>>          textures;   // stock asset (path under Content) -> recipe
		std::map<std::string, std::vector<std::string>>           glyphMasks;
		std::map<std::string, std::map<std::string, RichRow>>     richRows;
		std::map<std::string, std::vector<BlueprintColour>>       blueprints;   // blueprint_colours and material_colours
		std::map<std::string, std::vector<Literal>>               literals;     // blueprint_literals
		std::set<std::string>                                     importSwaps;  // packages with import_swaps (Phase 3d)
		std::vector<std::string>                                  layoutPackages;   // the layout section's packages (Phase 3d)
	};

	inline constexpr const char* kPanelMask = "T_Enchantment_Masks";   // pipeline.PANEL_MASKS (altar_panel*)

	namespace detail
	{
		inline bool Stock4(const json::Value* a_v, std::array<double, 4>& a_out)
		{
			if (!a_v || a_v->type != json::Type::Array || a_v->items.size() != 4) return false;
			for (size_t i = 0; i < 4; ++i) a_out[i] = a_v->items[i].Number();
			return true;
		}
		inline std::string Str(const json::Value* a_v) { return a_v && a_v->type == json::Type::String ? a_v->s : std::string(); }
		inline std::int64_t Offset(const json::Value& a_row) { return a_row.Find("offset") ? static_cast<std::int64_t>(a_row.Find("offset")->Number()) : 0; }
	}

	// the data files as (file name, text), in any order; read in file-name order (the folder listing), the menus then sorted
	// by (order, menu) like load_menus
	inline bool LoadPackData(const std::vector<std::pair<std::string, std::string>>& a_files, PackData& a_out, std::string& a_error)
	{
		a_out = {};
		auto files = a_files;
		std::stable_sort(files.begin(), files.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
		struct Menu
		{
			std::string name;
			json::Value doc;
		};
		std::vector<Menu> menus;
		for (const auto& [name, text] : files) {
			Menu        m{ name, {} };
			std::string why;
			if (!json::Parse(text, m.doc, why) || m.doc.type != json::Type::Object) {
				a_error = name + ": not valid JSON (" + (why.empty() ? std::string("not an object") : why) + ")";
				return false;
			}
			menus.push_back(std::move(m));
		}
		// load_literals and load_layout: every file in folder order; a package listed twice is refused
		for (const auto& m : menus) {
			if (const auto* lits = m.doc.Find("blueprint_literals"); lits && lits->type == json::Type::Object) {
				for (size_t k = 0; k < lits->keys.size(); ++k) {
					const auto& package = lits->keys[k];
					if (a_out.literals.count(package)) {
						a_error = "blueprint_literals package " + package + " is listed twice";
						return false;
					}
					auto& rows = a_out.literals[package];
					for (const auto& row : lits->items[k].items) {
						Literal l;
						l.offset = detail::Offset(row);
						l.type = detail::Str(row.Find("type"));
						l.stock = row.Find("stock") ? row.Find("stock")->Number() : 0;
						if (row.Find("value")) l.value = *row.Find("value");
						l.widget = detail::Str(row.Find("widget"));
						rows.push_back(std::move(l));
					}
				}
			}
			if (const auto* layout = m.doc.Find("layout"); layout && layout->type == json::Type::Object) {
				for (const auto& package : layout->keys) {
					if (std::find(a_out.layoutPackages.begin(), a_out.layoutPackages.end(), package) != a_out.layoutPackages.end()) {
						a_error = "layout package " + package + " is listed twice";
						return false;
					}
					a_out.layoutPackages.push_back(package);
				}
			}
		}
		std::stable_sort(menus.begin(), menus.end(), [](const Menu& a, const Menu& b) {
			const auto*  oa = a.doc.Find("order");
			const auto*  ob = b.doc.Find("order");
			const double na = oa ? oa->Number() : 99, nb = ob ? ob->Number() : 99;
			if (na != nb) return na < nb;
			return detail::Str(a.doc.Find("menu")) < detail::Str(b.doc.Find("menu"));
		});
		std::set<std::string> seen;
		for (const auto& m : menus) {
			if (const auto* assets = m.doc.Find("assets"); assets && assets->type == json::Type::Array) {
				for (const auto& row : assets->items) {
					const auto asset = detail::Str(row.Find("asset"));
					if (!seen.insert(asset).second) {
						a_error = asset + " is listed twice in the data files";
						return false;
					}
					a_out.textures.emplace_back(asset, detail::Str(row.Find("recipe")));
				}
			}
			if (const auto* masks = m.doc.Find("glyph_masks"); masks && masks->type == json::Type::Object) {
				for (size_t k = 0; k < masks->keys.size(); ++k) {
					std::vector<std::string> names;
					for (const auto& n : masks->items[k].items) names.push_back(n.s);
					a_out.glyphMasks[masks->keys[k]] = std::move(names);
				}
			}
			if (const auto* rich = m.doc.Find("rich_rows"); rich && rich->type == json::Type::Object) {
				for (size_t k = 0; k < rich->keys.size(); ++k) {
					std::map<std::string, RichRow> rows;
					const auto&                    table = rich->items[k];
					for (size_t r = 0; r < table.keys.size(); ++r) {
						RichRow row;
						row.offset = detail::Offset(table.items[r]);
						detail::Stock4(table.items[r].Find("stock"), row.stock);
						rows[table.keys[r]] = row;
					}
					a_out.richRows[rich->keys[k]] = std::move(rows);
				}
			}
			if (const auto* swaps = m.doc.Find("import_swaps"); swaps && swaps->type == json::Type::Object)
				for (const auto& package : swaps->keys) a_out.importSwaps.insert(package);
			for (const char* section : { "blueprint_colours", "material_colours" }) {
				const auto* colours = m.doc.Find(section);
				if (!colours || colours->type != json::Type::Object) continue;
				for (size_t k = 0; k < colours->keys.size(); ++k) {
					std::vector<BlueprintColour> entries;
					for (const auto& e : colours->items[k].items) {
						BlueprintColour c;
						c.offset = detail::Offset(e);
						if (!detail::Stock4(e.Find("stock"), c.stock)) {
							a_error = m.name + ": " + colours->keys[k] + " has a stock value that is not four numbers";
							return false;
						}
						c.colour = detail::Str(e.Find("colour"));
						if (const auto* scale = e.Find("scale")) {
							c.hasScale = true;
							c.scale = scale->Number();
						}
						entries.push_back(std::move(c));
					}
					a_out.blueprints[colours->keys[k]] = std::move(entries);
				}
			}
		}
		return true;
	}

	inline std::string AssetName(std::string_view a_asset)   // Path(asset).name
	{
		const auto slash = a_asset.find_last_of("/\\");
		return std::string(slash == std::string_view::npos ? a_asset : a_asset.substr(slash + 1));
	}

	// pipeline.needed_names(colours=True, layout): every name retoc extracts, sorted
	inline std::vector<std::string> NeededNames(const PackData& a_data, bool a_layout)
	{
		std::set<std::string> names;
		for (const auto& [asset, recipe] : a_data.textures) names.insert(AssetName(asset));
		for (const auto& [kind, masks] : a_data.glyphMasks) names.insert(masks.begin(), masks.end());
		names.insert(kPanelMask);
		if (a_layout) names.insert(a_data.layoutPackages.begin(), a_data.layoutPackages.end());
		return { names.begin(), names.end() };
	}

	// custom_colours.BAKED_SKIN: the [Skin] tints the recipes read, as "Skin.<name>"
	inline constexpr std::array<const char*, 6> kBakedSkin{ "pattern", "scrollTrack", "scrollBorder", "scrollInside", "ornament", "emphasisInk" };

	// custom_colours.pack_values: a_text = the colour file (nullptr: no file, the defaults)
	inline bool PackValues(const std::string* a_text, Values& a_out, std::string& a_error)
	{
		a_out.clear();
		std::map<std::string, model::Value> skin;
		for (const auto& e : model::detail::DefaultEntries()) {
			if (e.section == "Skin") skin[e.name] = e.def;
			else a_out[e.name] = e.def;
		}
		if (a_text) {
			std::vector<model::Found> found;
			if (!model::ReadEntries(*a_text, found, a_error)) return false;
			for (const auto& f : found) {
				if (f.section == "Colours" || f.section == "Tab") a_out[f.name] = f.value;
				else if (f.section == "Skin") skin[f.name] = f.value;
			}
		}
		for (const char* name : kBakedSkin) {
			const auto it = skin.find(name);
			if (it == skin.end()) {
				a_error = std::string("no default for [Skin] ") + name;
				return false;
			}
			a_out[std::string("Skin.") + name] = it->second;
		}
		return true;
	}
}
