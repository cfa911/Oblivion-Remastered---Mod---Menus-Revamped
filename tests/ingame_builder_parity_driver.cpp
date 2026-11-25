// Driver for tests/ingame_builder_parity_tests.py: runs an edit script on the C++ builder model (src/builder/ColourModel.h)
// and writes what it saves, so the test can compare it byte for byte with the Python builder (tools/pack_builder/model.py).
// Script lines (tab separated): load <path> | role <key> <r,g,b> | text <key> <value> | rgb <key> <r,g,b> | size <key> <value>
// | reset <key> | resetsize <key> | dark | save <path>. A refused edit prints "ERR <line number>".
#include <cstdio>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>
#include "../src/builder/ColourModel.h"

static std::vector<std::string> Split(const std::string& line)
{
	std::vector<std::string> out;
	std::stringstream        in(line);
	std::string              part;
	while (std::getline(in, part, '\t')) out.push_back(part);
	return out;
}

static std::array<double, 3> Three(const std::string& text)
{
	std::array<double, 3> v{};
	std::sscanf(text.c_str(), "%lf,%lf,%lf", &v[0], &v[1], &v[2]);
	return v;
}

int main(int argc, char** argv)
{
	if (argc < 2) return 2;
	std::ifstream            script(argv[1], std::ios::binary);
	builder::model::ColourModel m;
	std::string              line, error;
	int                      number = 0;
	while (std::getline(script, line)) {
		++number;
		if (!line.empty() && line.back() == '\r') line.pop_back();
		const auto f = Split(line);
		if (f.empty()) continue;
		bool ok = true;
		if (f[0] == "load") {
			std::ifstream      in(f[1], std::ios::binary);
			std::ostringstream text;
			if (in) text << in.rdbuf();
			ok = m.Load(text.str(), error);
		} else if (f[0] == "role") {
			ok = m.SetRole(f[1], Three(f[2]), error);
		} else if (f[0] == "text") {
			ok = m.SetText(f[1], f[2], error);
		} else if (f[0] == "rgb") {
			m.SetRgb(f[1], Three(f[2]));
		} else if (f[0] == "size") {
			ok = m.SetSize(f[1], f[2], error);
		} else if (f[0] == "reset") {
			m.ResetElement(f[1]);
		} else if (f[0] == "resetsize") {
			m.ResetSize(f[1]);
		} else if (f[0] == "dark") {
			m.ResetToDark();
		} else if (f[0] == "save") {
			std::ofstream out(f[1], std::ios::binary);
			out << m.Render();
		}
		if (!ok) std::printf("ERR %d\n", number);
	}
	return 0;
}
