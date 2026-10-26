#include "../sortingoverhaul/ItemTags.h"

#include <bit>
#include <cstdio>
#include <cstdlib>

int main()
{
	using weaponkinds::Kind;
	using weaponkinds::Bit;
	int checks = 0;
	const auto check = [&](bool ok, const char* label) {
		++checks;
		if (!ok) { std::fprintf(stderr, "FAIL %s\n", label); std::exit(1); }
	};
	struct Fixture { int type; const char* model; const char* icon; Kind expected; };
	// Actual installed-master WEAP asset paths/types. These are not live inventory captures.
	const Fixture fixtures[]{
		{ 0, "Weapons\\Ebony\\Dagger.NIF", "Weapons\\EbonyDagger.dds", Kind::Dagger },
		{ 0, "Weapons\\Gromite\\GromiteDagger.NIF", "Weapons\\Gromite\\GromiteDagger.dds", Kind::Dagger },
		{ 0, "Weapons\\Apostle\\ApostleDagger.NIF", "Weapons\\Apostle\\IconApostleDagger.dds", Kind::Dagger },
		{ 0, "Weapons\\Iron\\ShortSword.NIF", "Weapons\\IronShortsword.dds", Kind::Shortsword },
		{ 0, "Weapons\\DarkSeducer\\Shortsword.NIF", "Weapons\\DarkShortSword.dds", Kind::Shortsword },
		{ 0, "Weapons\\Madness\\Longsword.NIF", "Weapons\\Madness\\IconMadnessLongsword.dds", Kind::Longsword },
		{ 0, "Weapons\\Amber\\Longsword.NIF", "Weapons\\AmberSword.dds", Kind::Longsword },
		{ 1, "Weapons\\Jyygylag\\Claymore.NIF", "Weapons\\KnightofOrderBlade.dds", Kind::Claymore },
		{ 2, "Weapons\\Madness\\WarAxe.NIF", "Weapons\\Madness\\IconMadnessWarAxe.dds", Kind::WarAxe },
		{ 3, "Weapons\\BlackAxe\\BattleAxe.NIF", "Weapons\\DaedricBattleAxe.dds", Kind::Battleaxe },
		{ 2, "Weapons\\Amber\\Mace.NIF", "Weapons\\AmberMace.dds", Kind::Mace },
		{ 3, "Weapons\\Amber\\Warhammer.NIF", "Weapons\\AmberHammer.dds", Kind::Warhammer },
		{ 3, "Weapons\\SylsWarhammer\\SylsWarhammer.NIF", "Weapons\\Se07SylsWarhammer.dds", Kind::Warhammer },
		{ 5, "Weapons\\Elven\\Bow.NIF", "Weapons\\ElvenBow.dds", Kind::Bow },
		{ 4, "Staffs\\TestStaff.NIF", "Weapons\\Staff.dds", Kind::Staff },
		// Real outliers: no inference from localized labels, stats or known-looking names.
		{ 1, "Weapons\\Blacksword\\Longsword.NIF", "Weapons\\ShadowBlade.dds", Kind::Other },
		{ 2, "Weapons\\Amber\\Warhammer.NIF", "Weapons\\AmberHammer.dds", Kind::Other },
		{ 0, "Weapons\\forkofhorripilation\\forkofhorripilation.NIF", "", Kind::Other },
		{ 2, "Weapons\\Club\\Club.NIF", "Weapons\\club.dds", Kind::Other },
		{ 0, "Weapons\\Elven\\MS04Witsplinter.NIF", "Weapons\\MS04Witsplinter.dds", Kind::Other },
	};
	for (const auto& row : fixtures) {
		const auto tags = itemtags::WeaponTags(row.type, row.model, row.icon);
		check(tags == Bit(row.expected), "master WEAP fixture");
		check(std::popcount(tags) == 1, "each weapon has exactly one subtype");
		const auto categoryTags = tags | (1u << 28);
		check(itemtags::IncludesAll(1, static_cast<unsigned>(row.expected), categoryTags), "All Items Weapons subtype");
		check(!itemtags::IncludesAll(2, static_cast<unsigned>(row.expected), categoryTags), "cannot leak into Gear");
	}
	check(itemtags::WeaponTags(0, "DAGGER.NIF") == Bit(Kind::Dagger), "case-insensitive model");
	check(itemtags::WeaponTags(0, "weapons/steel/shortsword.nif") == Bit(Kind::Shortsword), "forward slash");
	check(itemtags::WeaponTags(0, "Weapons/Dagger/unknown.nif") == Bit(Kind::Other), "ignore misleading directories");
	check(itemtags::WeaponTags(0, "", "Weapons/IconSteelDagger.dds") == Bit(Kind::Dagger), "specific icon fallback");
	check(itemtags::WeaponTags(0, "custom.nif", "custom.dds") == Bit(Kind::Other), "unknown modded item");
	check(itemtags::WeaponTags(0, "dagger.nif", "longsword.dds") == Bit(Kind::Other), "conflicting paths");
	check(itemtags::WeaponTags(0, "daggerlongsword.nif", "dagger.dds") == Bit(Kind::Other), "ambiguous model cannot use icon");
	check(itemtags::WeaponTags(0, "dagger.nif", "daggerlongsword.dds") == Bit(Kind::Other), "ambiguous icon cannot use model");
	check(itemtags::WeaponTags(0, "mace.nif") == Bit(Kind::Other), "wrong skill asset");
	check(itemtags::WeaponTags(2, "battleaxe.nif") == Bit(Kind::Other), "wrong handedness asset");
	check(itemtags::WeaponTags(5, "dagger.nif") == Bit(Kind::Bow), "native Bow wins");
	check(itemtags::WeaponTags(4, "dagger.nif") == Bit(Kind::Staff), "native Staff wins");
	check(itemtags::WeaponTags(0) == Bit(Kind::Other), "missing paths stay accessible");
	const auto& info = itemtags::Info(itemtags::Tab::Weapons);
	check(info.count == weaponkinds::kCount && info.count <= pickergrid::kMaxChoices, "picker bounds");
	check(pickergrid::Rows(info.count) == 4 && pickergrid::Height(info.count) == 308, "expanded grid geometry");
	for (unsigned i = 0; i < info.count; ++i) {
		check(info.names[i] && info.icons[i], "each choice has a label and editable icon");
		check(pickergrid::IsSelection(static_cast<int>(i), info.count), "each choice has a mouse/controller selection");
	}
	std::printf("%d weapon kind checks passed\n", checks);
}
