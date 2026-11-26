// In-game Pack Builder Phase 3c: the image recipes (src/builder/PackRecipesImage.h, PackCharcoal.h) bake the same bytes as
// tools/pack_builder/pipeline.py on a synthetic stock texture (oracle: tests/ingame_pack_recipes_expect.py -> .inc), Recipe()
// knows every kind of the data files (none "later"), and the Python helpers they rest on (sum(), the skip texts) behave alike.
// SPDX-License-Identifier: GPL-3.0-or-later
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

#include "builder/PackBuild.h"

using namespace builder::pack;

static int g_checks = 0, g_fails = 0;
static void Check(bool a_ok, const std::string& a_what)
{
	++g_checks;
	if (!a_ok) {
		++g_fails;
		std::printf("FAIL %s\n", a_what.c_str());
	}
}

struct Expect
{
	const char* recipe;
	const char* sha256;
	const char* reason;
};
static const Expect kExpect[] = {
#include "ingame_pack_recipes_expect.inc"
};

static Image Synthetic()   // ingame_pack_recipes_expect.pixel
{
	Image img = NewImage(Mode::RGBA, 64, 32);
	for (int y = 0; y < 32; ++y)
		for (int x = 0; x < 64; ++x) {
			std::uint8_t* p = img.At(x, y);
			p[0] = static_cast<std::uint8_t>((x * 7 + y * 3) % 256);
			p[1] = static_cast<std::uint8_t>((x * x + y * 5) % 256);
			p[2] = static_cast<std::uint8_t>((y * 9 + x) % 256);
			p[3] = (x + y) % 7 ? 255 : 90;
		}
	return img;
}

static builder::model::Value V3(double a_r, double a_g, double a_b) { return builder::model::Value{ 3, { a_r, a_g, a_b, 0 } }; }

int main()
{
	Values values;
	values["parchmentDark"] = V3(8, 8, 8);
	values["parchmentLight"] = V3(33, 33, 33);
	values["panel"] = V3(28, 28, 28);
	values["frame"] = V3(12, 12, 12);
	values["pattern"] = V3(201, 160, 75.5);
	values["detailsPage"] = builder::model::Value{ 1, { 0.01, 0, 0, 0 } };
	values["effectIcon"] = V3(230, 190, 90);
	values["menuTitle"] = V3(80, 120, 210);
	values["enchantingFrame"] = V3(150, 160, 180);
	values["altarPaper"] = V3(40, 41, 44);
	values["listHighlight"] = V3(120, 90, 40);
	values["previewFrameLight"] = V3(200, 190, 170);
	values["previewFrameDark"] = V3(10, 12, 14);
	values["enchantedDot"] = V3(90, 200, 255);
	values["alchemyMetal"] = V3(180, 140, 60);
	values["alchemyStone"] = V3(90, 95, 92);
	values["tabPaperDark"] = V3(20, 20, 22);
	values["tabPaperLight"] = V3(60, 60, 64);
	values["tabGlyph"] = V3(240, 238, 230);

	const Dds stock{ PixelFormat::DXT5, 64, 32, 1, EncodeBc(Synthetic(), BcFormat::DXT5) };
	const PackData data;
	for (const auto& e : kExpect) {
		Bytes       out;
		std::string skip;
		const bool  ok = BakeTextureAll(e.recipe, "T_Synthetic", stock, values, data, BakeInputs{}, out, skip);
		if (*e.sha256) {
			Check(ok, std::string(e.recipe) + ": baked (C++ skipped: " + skip + ")");
			Check(ok && Sha256Hex(out) == e.sha256, std::string(e.recipe) + ": the same bytes as pipeline.py");
		} else {
			Check(!ok, std::string(e.recipe) + ": skipped like pipeline.py");
			Check(skip == e.reason, std::string(e.recipe) + ": skip text '" + skip + "', Python '" + e.reason + "'");
		}
	}

	// every recipe of the data files is known (Phase 3c ports the last family: nothing answers "later")
	const auto menus = std::filesystem::path(__FILE__).parent_path().parent_path() / "tools/menus";
	std::vector<std::pair<std::string, std::string>> files;
	for (const auto& f : std::filesystem::directory_iterator(menus))
		if (f.path().extension() == ".json") {
			std::ifstream     in(f.path(), std::ios::binary);
			std::stringstream s;
			s << in.rdbuf();
			files.emplace_back(f.path().filename().string(), s.str());
		}
	PackData    real;
	std::string error;
	Check(LoadPackData(files, real, error), "the data files load: " + error);
	int later = 0, unknown = 0;
	for (const auto& [asset, recipe] : real.textures) {
		const auto info = Recipe(recipe, real);
		later += info.kind == RecipeKind::Later;
		unknown += info.kind == RecipeKind::Unknown;
	}
	Check(!real.textures.empty() && later == 0, "no data recipe is 'later' (" + std::to_string(later) + ")");
	Check(unknown == 0, "every data recipe is known (" + std::to_string(unknown) + " unknown)");

	// recipe() keys and its refusals
	Check(Recipe("tab:2", real).keys == std::vector<std::string>{ "selected", "unselected", "gap", "outline", "glyph", "glyphDim" }, "tab reads TAB_STYLE");
	Check(Recipe("popup_paper:0", real).keys.size() == 2 && Recipe("popup_paper", real).keys.size() == 3, "popup_paper: no pattern at strength 0");
	Check(Recipe("laurel_tint", real).keys == std::vector<std::string>{ "pattern" } && Recipe("laurel_tint:effectIcon:42", real).keys == std::vector<std::string>{ "effectIcon" },
		"laurel_tint's key");
	Check(Recipe("charcoal_paper:x", real).error == "no page texture for charcoal_paper preset 'x'", "charcoal_paper refuses an unknown preset at recipe()");
	Check(Recipe("parchment:other", real).error == "KeyError: 'parchment:other'", "only parchment:enchanting has an argument");

	// CPython 3.12+ sum() of floats is compensated: sum([0.1] * 10) == 1.0 (a plain loop gives 0.9999999999999999)
	Check(py::Sum(std::vector<double>(10, 0.1)) == 1.0, "py::Sum is Neumaier's sum");
	Check(py::FloorDiv(-7, 255) == -1 && py::FloorDiv(7, 255) == 0, "py::FloorDiv floors");

	// a punch-through BC1 stock block is refused by the charcoal encoder
	Bytes bc1(8, 0);
	bc1[4] = 0xFF;
	bool threw = false;
	try {
		charcoal::NeutralGreyBc1(bc1, NewImage(Mode::L, 4, 4));
	} catch (const RecipeSkip& s) {
		threw = s.reason == "a stock BC1 block uses punch-through transparency";
	}
	Check(threw, "neutral_grey_bc1 refuses punch-through");

	// BakeAsset keeps its 3a/3d call (no inputs) and refuses an alchemy stock that is not the verified one
	BakedAsset  baked;
	std::string reason;
	PackageFiles files3;
	Check(BakeAsset("Game/UI/T_UI_Alchemy_Background", "alchemy", files3, values, real, baked, reason) == BakeResult::Skipped &&
	          reason == "T_UI_Alchemy_Background: stock asset differs from the verified game build (game update?)",
		"alchemy checks the stock .uexp SHA-256: " + reason);
	BakeInputs art;
	art.artFolder = "F:/art";
	Check(BakeAsset("Game/UI/T_X", "png", files3, values, real, baked, reason, nullptr, art) == BakeResult::Skipped &&
	          reason == "owner art missing (T_X.png in F:/art); the game's stock texture shows",
		"png needs the owner art: " + reason);

	std::printf("%d checks, %d failures\n", g_checks, g_fails);
	return g_fails ? 1 : 0;
}
