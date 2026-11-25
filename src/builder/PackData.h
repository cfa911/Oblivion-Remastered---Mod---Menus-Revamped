#pragma once
// In-game Pack Builder (Phase 3a): what the build bakes, read from the same data files as the Python builder
// (PackBuilder/menus/<menu>.json: tools/pack_builder/pipeline.py load_menus, load_literals, load_layout), and the colour
// values the recipes read (custom_colours.pack_values: [Colours] and [Tab] over the Dark defaults, the baked [Skin] tints as
// "Skin.<name>"), and the layout and import_swaps sections (Phase 3d). Pure: tests/ingame_pack_core_tests.cpp, tests/ingame_pack_parity_tests.py.
// SPDX-License-Identifier: GPL-3.0-or-later
#include <algorithm>
#include <array>
#include <cstdint>
#include <map>
#include <set>
#include <string>
#include <string_view>
#include <type_traits>
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

	// the layout section (tools/menus/layout.json, pipeline.load_layout): stock-checked values and package rewrites (Phase 3d)
	struct LayoutFloat
	{
		std::int64_t offset = 0;
		bool         isDouble = false;   // "type": "double" (a Spacer size)
		json::Value  stock;              // as written (int or float: the refusal message prints it like Python)
		bool         hasSize = false;    // "size": the [Sizes] key; else "value", fixed
		std::string  size;
		double       value = 0;
		std::string  widget;
	};

	struct LayoutByte   // a stored enum byte (a HorizontalBoxSlot's Size rule)
	{
		std::int64_t offset = 0;
		json::Value  stock;
		long long    value = 0;
		std::string  widget;
	};

	struct LayoutRewrite   // one export gains stored values (KB techniques/unversioned-package-rewrite.md)
	{
		std::string                                     widget;
		std::int64_t                                    exportStart = 0, exportSize = 0, headerPos = 0;
		std::vector<int>                                stockFields, stockZeroes;
		std::vector<long long>                          fieldSizes;
		std::vector<std::pair<int, double>>             add;        // float values
		std::vector<std::pair<int, std::vector<std::uint8_t>>> addBytes;   // whole cooked values (hex in the file)
		std::vector<std::pair<long long, long long>>    shaMask;    // [start, length] zeroed for the stock check
		std::string                                     stockSha256;
	};

	struct LayoutRow
	{
		std::string                asset;
		std::vector<LayoutFloat>   floats;
		std::vector<LayoutByte>    bytes;
		std::vector<LayoutRewrite> rewrites;
	};

	struct ImportSwap   // import_swaps (RMI-096)
	{
		std::string fromPackage, fromObject, fromClass, toPackage, toObject, toClass;
	};

	namespace detail
	{
		inline std::string Text(const json::Value* a_v) { return a_v && a_v->type == json::Type::String ? a_v->s : std::string(); }
		inline long long   Whole(const json::Value* a_v) { return a_v ? (a_v->type == json::Type::Int ? a_v->i : static_cast<long long>(a_v->Number())) : 0; }
		inline std::vector<std::uint8_t> FromHex(std::string_view a_hex)   // bytes.fromhex (spaces allowed)
		{
			std::vector<std::uint8_t> out;
			int                       high = -1;
			for (const char c : a_hex) {
				const int v = c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : c >= 'A' && c <= 'F' ? c - 'A' + 10 : -1;
				if (v < 0) continue;
				if (high < 0) high = v;
				else {
					out.push_back(static_cast<std::uint8_t>(high * 16 + v));
					high = -1;
				}
			}
			return out;
		}
	}

	inline LayoutRewrite LayoutRewriteFromJson(const json::Value& a_row)
	{
		LayoutRewrite r;
		r.widget = detail::Text(a_row.Find("widget"));
		r.exportStart = detail::Whole(a_row.Find("export_start"));
		r.exportSize = detail::Whole(a_row.Find("export_size"));
		r.headerPos = detail::Whole(a_row.Find("header_pos"));
		auto ints = [&](const char* a_key, auto& a_out) {
			if (const auto* v = a_row.Find(a_key); v && v->type == json::Type::Array)
				for (const auto& e : v->items) a_out.push_back(static_cast<std::remove_reference_t<decltype(a_out[0])>>(detail::Whole(&e)));
		};
		ints("stock_fields", r.stockFields);
		ints("stock_zeroes", r.stockZeroes);
		ints("field_sizes", r.fieldSizes);
		if (const auto* v = a_row.Find("add"); v && v->type == json::Type::Array)
			for (const auto& e : v->items)
				if (e.items.size() == 2) r.add.emplace_back(static_cast<int>(detail::Whole(&e.items[0])), e.items[1].Number());
		if (const auto* v = a_row.Find("add_bytes"); v && v->type == json::Type::Array)
			for (const auto& e : v->items)
				if (e.items.size() == 2) r.addBytes.emplace_back(static_cast<int>(detail::Whole(&e.items[0])), detail::FromHex(e.items[1].s));
		if (const auto* v = a_row.Find("sha_mask"); v && v->type == json::Type::Array)
			for (const auto& e : v->items)
				if (e.items.size() == 2) r.shaMask.emplace_back(detail::Whole(&e.items[0]), detail::Whole(&e.items[1]));
		r.stockSha256 = detail::Text(a_row.Find("stock_export_sha256"));
		return r;
	}

	inline LayoutRow LayoutRowFromJson(const json::Value& a_row)
	{
		LayoutRow row;
		row.asset = detail::Text(a_row.Find("asset"));
		if (const auto* floats = a_row.Find("floats"); floats && floats->type == json::Type::Array)
			for (const auto& e : floats->items) {
				LayoutFloat f;
				f.offset = detail::Whole(e.Find("offset"));
				f.isDouble = detail::Text(e.Find("type")) == "double";
				if (const auto* s = e.Find("stock")) f.stock = *s;
				if (const auto* s = e.Find("size")) {
					f.hasSize = true;
					f.size = detail::Text(s);
				} else if (const auto* v = e.Find("value"))
					f.value = v->Number();
				f.widget = detail::Text(e.Find("widget"));
				row.floats.push_back(std::move(f));
			}
		if (const auto* bytes = a_row.Find("bytes"); bytes && bytes->type == json::Type::Array)
			for (const auto& e : bytes->items) {
				LayoutByte b;
				b.offset = detail::Whole(e.Find("offset"));
				if (const auto* s = e.Find("stock")) b.stock = *s;
				b.value = detail::Whole(e.Find("value"));
				b.widget = detail::Text(e.Find("widget"));
				row.bytes.push_back(std::move(b));
			}
		if (const auto* rewrites = a_row.Find("rewrites"); rewrites && rewrites->type == json::Type::Array)
			for (const auto& e : rewrites->items) row.rewrites.push_back(LayoutRewriteFromJson(e));
		return row;
	}

	inline ImportSwap ImportSwapFromJson(const json::Value& a_row)
	{
		return { detail::Text(a_row.Find("from_package")), detail::Text(a_row.Find("from_object")), detail::Text(a_row.Find("from_class")),
			detail::Text(a_row.Find("to_package")), detail::Text(a_row.Find("to_object")), detail::Text(a_row.Find("to_class")) };
	}

	struct PackData
	{
		std::vector<std::pair<std::string, std::string>>          textures;   // stock asset (path under Content) -> recipe
		std::map<std::string, std::vector<std::string>>           glyphMasks;
		std::map<std::string, std::map<std::string, RichRow>>     richRows;
		std::map<std::string, std::vector<BlueprintColour>>       blueprints;   // blueprint_colours and material_colours
		std::map<std::string, std::vector<Literal>>               literals;     // blueprint_literals
		std::set<std::string>                                     importSwaps;  // packages with import_swaps
		std::map<std::string, std::vector<ImportSwap>>            swaps;        // package -> its import swaps (the last menu's list)
		std::vector<std::string>                                  layoutPackages;   // the layout section's packages, in file order
		std::map<std::string, LayoutRow>                          layout;       // package -> its layout row
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
				for (size_t k = 0; k < layout->keys.size(); ++k) {
					const auto& package = layout->keys[k];
					if (a_out.layout.count(package)) {
						a_error = "layout package " + package + " is listed twice";
						return false;
					}
					a_out.layoutPackages.push_back(package);
					a_out.layout[package] = LayoutRowFromJson(layout->items[k]);
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
				for (size_t k = 0; k < swaps->keys.size(); ++k) {   // IMPORT_SWAPS.update: a later menu replaces the list
					std::vector<ImportSwap> rows;
					for (const auto& row : swaps->items[k].items) rows.push_back(ImportSwapFromJson(row));
					a_out.importSwaps.insert(swaps->keys[k]);
					a_out.swaps[swaps->keys[k]] = std::move(rows);
				}
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
