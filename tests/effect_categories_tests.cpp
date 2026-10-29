#include "../src/sorting/rules/ItemTags.h"
#include "../src/sorting/rules/ControllerPolicy.h"
#include "remastered_effect_fixtures.h"
#include <cstdio>
#include <cstdlib>
#include <string_view>

unsigned checks = 0;
void Check(bool ok, const char* label)
{
	++checks;
	if (!ok) { std::fprintf(stderr, "FAIL %s\n", label); std::exit(1); }
}
int main()
{
	using namespace magiceffects;
	Check(kChoiceCount == 18, "All plus seventeen families");
	Check(effectfixture::kEffects.size() == 145, "all installed MGEF records");
	Check(ValidRules(), "unique verified code mappings");
	for (const auto& entry : effectfixture::kEffects) {
		const auto code = Code(entry.code);
		Check(Classify(code) == entry.expected, entry.name);
		Check(itemtags::EffectTags(&code, 1) == Bit(entry.expected), entry.name);
	}
	// Independently named edge cases: do not reproduce guessed Magicka/Shield identifiers.
	Check(Classify(0x50534552u) == Choice::Restore, "RESP Restore Magicka, literal native byte order");
	Check(Classify(0x50534241u) == Choice::Absorb, "ABSP Absorb Magicka");
	Check(Classify(0x50534F46u) == Choice::Fortify, "FOSP Fortify Magicka");
	Check(Classify(0x4853494Cu) == Choice::Shield, "LISH Shock Shield");
	for (auto invented : { "REMA", "ABMA", "FOMA", "SHSH" }) {
		const auto code = Code(invented);
		Check(Classify(code) == Choice::Other, "invented identifier is not recognized");
	}
	for (auto codeText : { "FISH", "RSFI", "WKFI", "ZFIA", "FOSP", "OPEN" }) {
		const auto code = Code(codeText);
		Check(!(Tags(&code, 1) & Bit(Choice::Fire)), "fire-related defense/summon/utility is not Fire Damage");
	}
	const std::uint32_t aetherius[] = { Code("FOSP"), Code("RSMA") };
	Check(Tags(aetherius, 2) == (Bit(Choice::Fortify) | Bit(Choice::Resistance)), "Gates of Aetherius fixture");
	const std::uint32_t jone[] = { Code("INVI"), Code("FOSK"), Code("FOSK"), Code("FOSK") };
	Check(Tags(jone, 4) == (Bit(Choice::Stealth) | Bit(Choice::Fortify)), "Jone's Shadow fixture");
	const std::uint32_t warden[] = { Code("OPEN") }, masters[] = { Code("FOSK") };
	Check(Tags(warden, 1) == Bit(Choice::Utility), "Warden Key fixture");
	Check(Tags(masters, 1) == Bit(Choice::Fortify), "The Master's Hand fixture");
	const std::uint32_t pilgrim[] = { Code("FOAT"), Code("FOAT"), Code("FOAT"), Code("FOAT"), Code("FOAT"), Code("FOAT"), Code("FOAT"), Code("FOAT") };
	Check(Tags(pilgrim, 8) == Bit(Choice::Fortify), "Pilgrim's Grace fixture");
	const std::uint32_t mixed[] = { Code("FIDG"), Code("RESP"), Code("SEFF"), 0 };
	Check(Tags(mixed, 4) == (Bit(Choice::Fire) | Bit(Choice::Restore)), "Other is fallback, not mixed-spell membership");
	Check(Tags(nullptr, 0) == Bit(Choice::Other), "empty spell fallback");
	Check(Tags(nullptr, 8) == Bit(Choice::Other), "null input bounded fallback");
	Check(Bit(Choice::All) == 0 && Bit(Choice::Count) == 0, "All/invalid have no tag bit");
	Check(!(Bit(Choice::Other) & ~itemtags::kTagBits), "all effect tags fit membership mask");
	const auto& info = itemtags::Info(itemtags::Tab::MagicEffect);
	for (unsigned i = 0; i < kChoiceCount; ++i) {
		Check(std::string_view(info.names[i]) == kChoices[i].name, "picker names align with classifier");
		Check(std::wstring_view(info.icons[i]) == kChoices[i].icon, "picker icons align with classifier");
		Check(std::string_view(kChoices[i].label).size() <= 7, "short display label, full tooltip preserved");
		Check(pickergrid::IsSelection(static_cast<int>(i), kChoiceCount), "every cell is a selection");
		Check(!pickergrid::IsSelection(pickergrid::kToggleCommand, kChoiceCount), "toggle cannot collide with choice index nine");
		Check(pickergrid::CellX(i, kChoiceCount) + pickergrid::CellWidth <= pickergrid::Width(kChoiceCount), "cell fits popup width");
		Check(pickergrid::CellY(i, kChoiceCount) + pickergrid::CellHeight <= pickergrid::Height(kChoiceCount), "cell fits expanded popup height");
		if (i) {
			const auto tag = Bit(static_cast<Choice>(i));
			Check(itemtags::Includes(i, tag), "selected effect bit includes matching spell");
			Check((static_cast<std::uint64_t>(tag) << 32 & (1ull << 63)) == 0, "effect bits cannot set pass-all sentinel");
		}
	}
	Check(pickergrid::Height(9) == 242 && pickergrid::Height(8) == 242, "count-based inventory/school dimensions");
	Check(pickergrid::Height(18) == 374, "five-row effect popup");
	Check(pickergrid::Position(1000, 244, 1280) == 1000, "popup fits canvas");
	Check(pickergrid::Position(1200, 244, 1280) == 1036, "right edge clamp");
	Check(pickergrid::Position(500, 440, 720) == 280, "bottom edge clamp");
	Check(pickergrid::Position(-10, 244, 1280) == 0, "left edge clamp");
	Check(pickergrid::Position(100, 440, 300) == 0, "undersized canvas clamp");
	Check(pickergrid::Position(100, 440, 0) == 100, "unavailable canvas geometry preserves anchor");
	Check(!pickergrid::IsSelection(-1, 18) && !pickergrid::IsSelection(18, 18), "out-of-range command rejected");
	std::printf("%u effect category checks passed\n", checks);
}
