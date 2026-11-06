"""Run the actual Alchemy search rectangle against a scaled viewport."""
from pathlib import Path
import subprocess
import sys
import source_paths

out = Path(sys.argv[1]).resolve()
out.mkdir(parents=True, exist_ok=True)
cpp = out / "alchemy_search_rect_tests.cpp"
cpp.write_text(r"""
#include <iostream>
namespace UE { struct UObject {}; }
UE::UObject canvas, list;
constexpr double kHeaderGap = 40, kGapTrimPx = 4, kSearchSpace = 44, kSearchHeight = 36;
int g_paddedFilters = 0;
UE::UObject* Widget() { return &list; }
UE::UObject* FilterWidget() { return &list; }
UE::UObject* Field(UE::UObject* widget, const char*) { return widget; }
void PadList(UE::UObject*, double, int&) {}
constexpr double kHeaderRuleDropPx = 3;
bool filters = false;
namespace ui {
double scale = 1;
double ViewportScale() { return scale; }
bool Measure(UE::UObject* widget, double& x, double& y, double& w, double& h) {
    x = 0; y = widget == &canvas ? 0 : 200 + (filters ? 0 : 3 / scale); w = 500; h = 300;
    return true;
}
}
""" + source_paths.function(source_paths.read("alchemy"), "bool SearchRect(") + r"""
int main() {
    int failures = 0;
    for (double scale : {1.0, 2.0}) {
        ui::scale = scale;
        double x, y, w, h;
        filters = false;
        const double oldY = 200 - (40 - 4 / scale) - 44;
        if (!SearchRect(&canvas, x, y, w, h, false) ||
            (oldY - y) * scale != 15 || x != 50 || w != 400 || h != 36) {
            std::cerr << "FAIL ingredient rectangle at scale " << scale << "\n";
            ++failures;
        }
        filters = true;
        if (!SearchRect(&canvas, x, y, w, h, true) || y != 156) {
            std::cerr << "FAIL effect search position at scale " << scale << "\n";
            ++failures;
        }
    }
    std::cout << "4 checks, " << failures << " failures\n";
    return failures != 0;
}
""", encoding="utf-8")
exe = out / "alchemy_search_rect_tests.exe"
subprocess.run(["cl", "/nologo", "/EHsc", "/std:c++latest", str(cpp),
                f"/Fe:{exe}", f"/Fo:{out}/"], check=True)
sys.exit(subprocess.run([str(exe)]).returncode)
