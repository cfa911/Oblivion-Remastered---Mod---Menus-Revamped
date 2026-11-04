// IMP-MR-004/005/006: Alchemy menu sort order, effect filter counts and active-filter labels.
#include "../src/sorting/rules/AlchemyRules.h"

#include <cstdio>
#include <string>
#include <vector>

int main()
{
	using namespace alchemyrules;
	int checks = 0, failures = 0;
	const auto check = [&](bool ok, const char* label) {
		++checks;
		if (!ok) { ++failures; std::fprintf(stderr, "FAIL %s\n", label); }
	};

	// ---- IMP-MR-005: sort keys cycle Name > Count > Weight > Value > Known effects > Name
	check(NextSort(SortKey::Name) == SortKey::Count, "Name -> Count");
	check(NextSort(SortKey::Count) == SortKey::Weight, "Count -> Weight");
	check(NextSort(SortKey::Weight) == SortKey::Value, "Weight -> Value");
	check(NextSort(SortKey::Value) == SortKey::KnownEffects, "Value -> Known effects");
	check(NextSort(SortKey::KnownEffects) == SortKey::Name, "Known effects -> Name");
	check(std::string(SortLabel(SortKey::Name)) == "Name" && std::string(SortLabel(SortKey::KnownEffects)) == "Known effects", "labels");
	check(SortFromIni(3) == SortKey::Value && SortFromIni(99) == SortKey::Name && SortFromIni(-1) == SortKey::Name, "INI index, out of range = Name");

	const std::vector<Ingredient> items{
		{ "[Ingr] Wolfsbane Petals", 2, 0.1f, 5, { "Restore Fatigue", "Dispel" } },
		{ "[Ingr] alkanet Flower", 10, 0.1f, 1, { "Restore Intelligence" } },
		{ "[Ingr] Bonemeal", 10, 0.2f, 2, { "Damage Fatigue", "Fortify Agility", "Resist Fire" } },
		{ "[Ingr] Glow Dust", 1, 0.1f, 18, {} },
	};
	using V = std::vector<int>;
	check(SortOrder(items, SortKey::Name) == V{ 1, 2, 3, 0 }, "name: A-Z, case-insensitive, tag prefix ignored");
	check(SortOrder(items, SortKey::Count) == V{ 1, 2, 0, 3 }, "count: most first, ties by name");
	check(SortOrder(items, SortKey::Weight) == V{ 1, 3, 0, 2 }, "weight: lightest first, ties by name");
	check(SortOrder(items, SortKey::Value) == V{ 3, 0, 2, 1 }, "value: most valuable first");
	check(SortOrder(items, SortKey::KnownEffects) == V{ 2, 0, 1, 3 }, "known effects: most first, ties by name");
	const std::vector<Ingredient> unknown{ { "B", 1, -1.0f, -1, {} }, { "A", 1, 0.5f, 3, {} } };
	check(SortOrder(unknown, SortKey::Weight) == V{ 1, 0 } && SortOrder(unknown, SortKey::Value) == V{ 1, 0 }, "unknown weight/value go last");
	check(SortOrder({}, SortKey::Count).empty(), "empty list");

	// ---- IMP-MR-004: counts on effect filter rows
	check(MatchKey("\xE2\x98\xBA[Alte] Fire Shield") == "fire shield", "glyph and tag prefix stripped");
	check(MatchKey("Restore Fatigue (7)") == "restore fatigue", "our count suffix stripped");
	check(MatchKey("  Light ") == "light", "spaces trimmed");
	check(MatchKey("[Rest] Cure Disease (12)") == "cure disease", "prefix and suffix together");
	const auto counts = EffectCounts({
		{ "a", 1, 0, 0, { "Restore Fatigue", "Light" } },
		{ "b", 5, 0, 0, { "[Rest] Restore Fatigue" } },
		{ "c", 9, 0, 0, { "Restore Fatigue", "Restore Fatigue" } },   // one ingredient counts once
	});
	check(counts.count("restore fatigue") && counts.at("restore fatigue") == 3, "ingredients with the effect, not quantities");
	check(counts.count("light") && counts.at("light") == 1, "second effect");
	check(!counts.count("dispel"), "absent effect has no entry (zero)");
	check(WithCount("\xE2\x98\xBA[Rest] Restore Fatigue", 7) == "\xE2\x98\xBA[Rest] Restore Fatigue (7)", "count appended, prefix kept");
	check(WithCount("Restore Fatigue (7)", 3) == "Restore Fatigue (3)", "suffix replaced, not stacked");
	check(WithCount("Light", 0) == "Light (0)", "zero shown");
	check(WithCount("Potion (Strong)", 2) == "Potion (Strong) (2)", "a non-number bracket is not our suffix");

	// ---- IMP-MR-006: active-filter labels
	check(ChipLabel("\xE2\x98\xBA[Alte] Fire Shield") == "Fire Shield", "chip: no glyph, no tag");
	check(ChipLabel("Restore Fatigue (7)") == "Restore Fatigue", "chip: no count");
	check(ChipLabel("Light") == "Light", "chip: plain name kept");

	// ---- title row layout (owner screenshot 2026-11-04: our labels overlapped the native Filters button)
	{
		const Box title{ 78, 75, 200, 40 }, filters{ 705, 75, 140, 40 };
		const auto l = PlaceTitleRow(title, filters, 145, 14);
		check(l.sortX == 306 && l.sortY == 95, "sort label right of the Ingredients title, same centre line");
		check(l.sortMaxWidth == 385, "sort label stops before the Filters button");
		check(l.sortX + l.sortMaxWidth <= filters.x - 14, "never reaches the Filters button");
		check(l.chipsX == 78 && l.chipsRight == 845, "filter line spans title left to Filters right");
		check(l.chipsY == 130, "filter line centred between the title row and the list");
		const auto tight = PlaceTitleRow({ 78, 75, 600, 40 }, filters, 145, 14);
		check(tight.sortMaxWidth == 0, "no room: zero width, not negative");
		check(TextWidth(600, 190) == 190, "stretched title: its text width");
		const auto b = ButtonBox(100, 130, 80, 20, 8, 3);
		check(b.x == 92 && b.y == 117 && b.w == 96 && b.h == 26, "button box pads the label on every side");
		const std::vector<Box> buttons{ b, ButtonBox(220, 130, 50, 20, 8, 3) };
		check(HitIndex(buttons, 95, 120) == 0 && HitIndex(buttons, 213, 140) == 1, "point on a button");
		check(HitIndex(buttons, 200, 130) == -1 && HitIndex(buttons, 100, 150) == -1, "between or below buttons: none");
		check(HitIndex({}, 1, 1) == -1, "no buttons");
		check(StripTint(false) == 1.0f, "strip at full colour normally");
		// owner screenshot 2026-11-04 13: at 0.78 the strip stayed brighter than the greyed panel (sRGB 183 vs 146); the
		// tint multiplies linear colour, so the panel's screen ratio 0.71 needs 0.71^2.2 = 0.47
		check(StripTint(true) > 0.46f && StripTint(true) < 0.48f, "strip greyed like the panel while Filters is open");
		// owner PSD placement 2026-11-04: on 2560x1440 (viewport scale 1.333) the strip is 829x200 at 64,9 units; the panel's
		// background widget measured about 72,69 834 wide - so the strip sits 8 left, 60 above, 5 narrower, aspect kept
		const Box  panel{ 72, 69, 834, 870 };
		const auto st = PlaceStrip(panel, 266.0 / 1105.0, -8, -60, -5);
		check(st.box.x == 64 && st.box.y == 9 && st.box.w == 829, "strip at the owner's PSD place relative to the panel");
		check(st.box.h > 199.5 && st.box.h < 199.6, "aspect kept (no squashing)");
		check(st.chipsY > st.box.y + 40 && st.chipsY < st.box.y + st.box.h * 0.6, "buttons in the strip's solid space");
		const std::vector<std::pair<int, std::string>> texts{ { 0x38, "Burden" }, { 0x50, "âº[Alte] Fire Shield" }, { 0x68, "Fire Shield" } };
		check(LearnTextOffset(texts, "âº[Alte] Fire Shield (2)") == 0x50, "the field matching the row (with our count) is learned");
		check(LearnTextOffset(texts, "Light (1)") == -1, "no match: unknown");
		check(LearnTextOffset({}, "Light") == -1, "no texts: unknown");
		const std::vector<std::vector<std::pair<int, std::string>>> perItem{
			{ { 0x30, "Fortify" }, { 0x50, "[Alte] Fire Shield" } },
			{ { 0x30, "Fortify" }, { 0x50, "[Alte] Frost Shield" } },
			{ { 0x30, "Light" }, { 0x50, "[Illu] Light" } },   // 0x30 matches one row by chance; 0x50 matches all three
		};
		const std::vector<std::string> rows{ "[Alte] Fire Shield (2)", "[Alte] Frost Shield (1)", "[Illu] Light (1)" };
		check(LearnTextOffsetFromRows(perItem, rows) == 0x50, "the offset matching the most rows wins");
		check(LearnTextOffsetFromRows(perItem, { "Burden (6)" }) == -1, "no row matches: unknown");
		check(LearnTextOffsetFromRows({}, rows) == -1, "no items: unknown");
		check(TextWidth(190, 0) == 190 && TextWidth(190, 250) == 190, "unknown or larger desired width: the measured one");
		const auto noList = PlaceTitleRow(title, filters, 0, 14);
		check(noList.chipsY == 115 + 16, "list not measured: the line sits just under the title row");
	}
	// ---- RMI-021: never read the ingredient data before the game built this opening's list
	// ---- RMI-022: read the game's ingredient texts as rarely as possible (each read can meet a freed text)
	check(NeedCounts(true, -1, 72) && NeedCounts(false, 72, 70), "counts: on opening, and when the ingredient count changes (brewing)");
	check(!NeedCounts(false, 72, 72), "counts: not again for a filter click (same ingredients)");
	check(SortReadsEffects(SortKey::KnownEffects), "known-effects sort reads the effects");
	{   // RMI-023: the effect name field of the struct the array really holds
		const std::vector<std::pair<std::string, int>> modern{ { "Index", 0 }, { "EffectID", 4 }, { "EffectName", 8 }, { "AdditionalEffectName", 0x20 } };
		const std::vector<std::pair<std::string, int>> legacy{ { "bIsUnknown", 0 }, { "Icon", 8 }, { "Effect", 0x10 } };
		check(EffectNameField(modern) == 8, "ModernEffectItemData: EffectName, not AdditionalEffectName");
		check(EffectNameField(legacy) == 0x10, "OriginalInventoryMenuIngredientEffects: Effect");
		check(EffectNameField({ { "Icon", 8 } }) == -1, "no name field: none");
		const int T = 0x18;   // sizeof(FText)
		check(TextField({ { "bIsKnown", 0, 1 }, { "DisplayName", 8, T }, { "Description", 0x20, T }, { "Icon", 0x38, 8 } }, T) == 8,
			"first text-sized field with Name in it");
		check(TextField({ { "Index", 0, 4 }, { "EffectName", 8, T }, { "AdditionalEffectName", 0x20, T } }, T) == 8, "the known name first");
		check(TextField({ { "Flag", 0, 1 }, { "Text", 8, T }, { "Other", 0x20, T } }, T) == 8, "no Name field: the first text-sized field");
		check(TextField({ { "Flag", 0, 1 }, { "Icon", 8, 8 } }, T) == -1, "no text-sized field: none");
		check(UnknownFlag({ { "bIsUnknown", 0, 1 } }) == std::make_pair(0, false), "bIsUnknown as is");
		check(UnknownFlag({ { "Name", 8, T }, { "bIsKnown", 0x38, 1 } }) == std::make_pair(0x38, true), "bIsKnown inverted");
		check(UnknownFlag({ { "Name", 8, T } }).first == -1, "no flag: none");
		// the real struct (log 2026-11-04 12:03): ModernItemEffectProperties spells it bIsUnknow
		check(UnknownFlag({ { "Icon", 0, 8 }, { "Text", 8, T }, { "TextKey", 0x20, T }, { "bIsUnknow", 0x38, 1 }, { "bIsHostile", 0x39, 1 } }) ==
			std::make_pair(0x38, false), "bIsUnknow (the game's spelling) is the unknown flag");
		check(TextField({ { "Icon", 0, 8 }, { "Text", 8, T }, { "TextKey", 0x20, T }, { "bIsUnknow", 0x38, 1 } }, T) == 8, "Text, not TextKey");
		// "+N" flicker: same input, same answer; marker room is kept when not all fit
		const std::vector<double> w{ 120, 160, 150, 190, 200 };
		check(FitButtons(w, 1000, 14, 40) == 5, "all fit");
		check(FitButtons(w, 720, 14, 40) == 4 && FitButtons(w, 720, 14, 40) == 4, "four fit with the marker (716 of 720), stable");
		check(FitButtons(w, 640, 14, 40) == 3, "the fourth would leave no room for the +N marker");
		check(FitButtons(w, 100, 14, 40) == 1, "the first button always shows");
		check(FitButtons({}, 500, 14, 40) == 0, "no buttons");
		// two rows (owner 2026-11-04)
		const auto a = FlowButtons({ 100, 100, 100 }, 400, 10, 30, 2);
		check(a.row == std::vector<int>{ 0, 0, 0 } && a.x == std::vector<double>{ 0, 110, 220 } && !a.more, "all on the first row");
		const auto b = FlowButtons({ 150, 150, 150, 150, 150 }, 400, 10, 30, 2);
		check(b.row == std::vector<int>{ 0, 0, 1, 1 } && b.x == std::vector<double>{ 0, 160, 0, 160 }, "wraps to the second row");
		check(b.more && b.moreRow == 1 && b.moreX == 320, "+N after the last shown button, second row");
		const auto c = FlowButtons(std::vector<double>(7, 120), 400, 10, 40, 2);
		check(c.row == std::vector<int>{ 0, 0, 0, 1, 1 } && c.more && c.moreRow == 1 && c.moreX == 260,
			"last row keeps room for the +N marker");
		const auto d = FlowButtons(std::vector<double>(6, 120), 400, 10, 40, 2);
		check(d.row.size() == 6 && !d.more, "everything fits on two rows: no marker");
		const auto e = FlowButtons({ 500, 100 }, 400, 10, 30, 2);
		check(e.row == std::vector<int>{ 0, 1 } && !e.more, "a too-wide first button still shows");
		const auto f = FlowButtons(std::vector<double>(5, 150), 400, 10, 30, 1);
		check(f.row.size() == FitButtons(std::vector<double>(5, 150), 400, 10, 30), "one row = FitButtons");
		check(FlowButtons({}, 400, 10, 30, 2).row.empty(), "no buttons, no rows");
	}
	check(InImage(0x7FF629580000ull + 0x5000000, 0x7FF629580000ull, 0x8000000), "vtable inside the game image");
	check(!InImage(0x8000000000ull, 0x7FF629580000ull, 0x8000000), "the logged fake pointer is rejected");
	check(!InImage(0x7FF629580000ull + 0x8000000, 0x7FF629580000ull, 0x8000000), "one past the image is rejected");
	check(!InImage(0x1B8ED25E300ull, 0x7FF629580000ull, 0x8000000), "a heap address is not a vtable");
	for (auto k : { SortKey::Name, SortKey::Count, SortKey::Weight, SortKey::Value }) check(!SortReadsEffects(k), "other sorts do not read effect texts");


	{
		OpenState st;
		check(!st.CanRead(0, 500), "closed: no read");
		st.Opened(1000);
		check(!st.CanRead(1000, 500), "just opened, list not built: no read");
		check(!st.CanRead(1499, 500), "before the settle time: no read");
		check(st.CanRead(1500, 500), "settle time passed: read");
		st.Closed();
		st.Opened(5000);
		check(!st.CanRead(5001, 500), "reopened: the old opening's state does not carry over");
		st.ListBuilt();
		check(st.CanRead(5001, 500), "the game built the list: read at once");
		st.Closed();
		st.ListBuilt();
		check(!st.CanRead(9000, 500), "a list built while closed does not allow a read");
	}
	check(PlausibleText(0x29154972D00ull, 0x29154972CF0ull), "heap pointers pass");
	check(!PlausibleText(0x3Cull, 0x29154972CF0ull), "small value rejected (the 0x44 crash)");
	check(!PlausibleText(0x29154972D00ull, 0), "null controller rejected");
	check(!PlausibleText(0x29154972D03ull, 0x29154972CF0ull), "misaligned rejected");
	check(!PlausibleText(0xFFFF800000000000ull, 0x29154972CF0ull), "kernel-range rejected");
	{   // RMI-024: a reused row of an active filter gets its selected look back
		const std::vector<std::string> sel{ "\xE2\x98\xBA[Alte] Water Breathing", "Invisibility" };
		check(ShouldRestoreSelected("\xE2\x98\xBA[Alte] Water Breathing", false, sel), "active filter shown unselected: restore");
		check(ShouldRestoreSelected("\xE2\x98\xBA[Illu] Invisibility (6)", false, sel), "prefix and count do not matter");
		check(!ShouldRestoreSelected("\xE2\x98\xBA[Alte] Water Breathing", true, sel), "already selected: nothing to do");
		check(!ShouldRestoreSelected("\xE2\x98\xBA[Alte] Water Walking", false, sel), "not an active filter: nothing");
		check(!ShouldRestoreSelected("Shield", false, {}), "no active filters: nothing");
		check(!ShouldRestoreSelected("", false, { "" }), "empty text never matches");
	}

	std::printf("%d checks, %d failures\n", checks, failures);
	return failures ? 1 : 0;
}
