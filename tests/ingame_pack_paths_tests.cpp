// In-game Pack Builder RMI-114: the Build read every stock asset through \\?\ paths holding '/' (Windows does not convert
// them there), so all 399 assets were "not extracted". src/builder/PackPaths.h must give backslashes only.
#include <cstdio>
#include <string>
#include "../src/builder/PackPaths.h"

using namespace builder::pack;

unsigned checks = 0, failures = 0;
void Check(bool ok, const char* why)
{
	++checks;
	if (!ok) {
		++failures;
		std::printf("FAIL %s\n", why);
	}
}

int main()
{
	const auto p = LongAssetPath(L"F:/Games/Game/MenusRevamped/.PackBuilder-1/legacy/OblivionRemastered/Content", "Art/UI/Common/T_Line_V").wstring();
	Check(p == L"\\\\?\\F:\\Games\\Game\\MenusRevamped\\.PackBuilder-1\\legacy\\OblivionRemastered\\Content\\Art\\UI\\Common\\T_Line_V",
		"the long form has backslashes only (base and asset path)");
	Check(p.find(L'/') == std::wstring::npos, "no forward slash anywhere");
	Check(LongPath(L"\\\\?\\F:\\x").wstring() == L"\\\\?\\F:\\x", "an already long path is kept");
	Check(LongPath(L"F:\\a\\b\\..\\c").wstring() == L"\\\\?\\F:\\a\\c", "dot-dot is resolved (\\\\?\\ paths are taken literally)");
	std::printf("%u checks, %u failures\n", checks, failures);
	return failures ? 1 : 0;
}
