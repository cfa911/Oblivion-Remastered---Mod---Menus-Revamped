// Developer harness for src/builder/PackJob.cpp (RMI-114): runs the in-game Build outside the game on a plugins folder laid out
// like the game's (<root>/OblivionRemastered/Binaries/Win64/OBSE/Plugins with MenusRevamped/PackBuilder/{retoc,menus},
// CustomColours.ini, MenusRevamped.ini, and <root>/OblivionRemastered/Content/Paks, for example a junction to the game's).
//   harness <plugins folder>     prints the progress and the result; exit 0 = Done
// Build: cl /std:c++latest /EHsc /utf-8 /DNOMINMAX /I src tests/ingame_pack_job_harness.cpp src/builder/PackJob.cpp
#include <Windows.h>
#include <cstdio>
#include <string>
#include "../src/builder/PackJob.h"

int wmain(int argc, wchar_t** argv)
{
	if (argc != 2) {
		std::printf("usage: harness <plugins folder>\n");
		return 2;
	}
	using namespace builder::pack::job;
	std::string why;
	if (!StartBuild(argv[1], why)) {
		std::printf("not started: %s\n", why.c_str());
		return 3;
	}
	std::string last;
	for (;;) {
		Sleep(500);
		const auto s = Get();
		if (s.state == State::Running) {
			if (s.phase != last && (s.phase.rfind("Baking", 0) != 0 || s.done % 50 == 0)) std::printf("%d/%d %s\n", s.done, s.total, s.phase.c_str());
			last = s.phase;
			continue;
		}
		std::printf("RESULT %s\n", s.message.c_str());
		return s.state == State::Done ? 0 : 1;
	}
}
