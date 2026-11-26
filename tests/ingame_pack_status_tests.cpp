// In-game Pack Builder Phase 3e: the window's "Build pack to apply" status (src/builder/PackStatus.h), a port of
// tools/pack_builder/model.py needs_build / sizes_changed and window.py refresh_status.
// - a baked value differing from the colour pack manifest's style (tolerance 1e-4) needs a build; run-time [Skin] tints never do
// - a baked tint missing from the manifest counts; a missing [Colours] key does not (an older pack)
// - sizes count when the layout pack's sizes differ or are missing (text role scales are not baked as such)
#include <cstdio>
#include <string>
#include "../src/builder/PackStatus.h"

using namespace builder::pack;
namespace model = builder::model;

unsigned checks = 0, failures = 0;
void Check(bool ok, const char* why)
{
	++checks;
	if (!ok) {
		++failures;
		std::printf("FAIL %s\n", why);
	}
}

static json::Value StyleOf(const std::vector<model::Entry>& a_entries)   // a manifest style matching the entries exactly
{
	auto style = json::MakeObject();
	for (const auto& e : a_entries) {
		if (e.section == "Skin" && !(e.name == "pattern" || e.name == "scrollTrack" || e.name == "scrollBorder" || e.name == "scrollInside" ||
										e.name == "ornament" || e.name == "emphasisInk"))
			continue;
		json::Value v;
		if (e.value.n == 1) v = json::MakeFloat(e.value.v[0]);
		else {
			v = json::MakeArray();
			for (int i = 0; i < e.value.n; ++i) v.items.push_back(json::MakeFloat(e.value.v[static_cast<size_t>(i)]));
		}
		json::Set(style, e.section == "Skin" ? "Skin." + e.name : e.name, v);
	}
	return style;
}

int main()
{
	model::ColourModel m;
	auto               entries = m.Entries();
	auto               style = StyleOf(entries);
	Check(NeedsBuild(entries, &style).empty(), "a pack built from these values needs no build");
	Check(!NeedsBuild(entries, nullptr).empty(), "no pack: the baked tints need a build");

	auto changed = style;
	for (size_t i = 0; i < changed.keys.size(); ++i)
		if (changed.keys[i] == "frame") changed.items[i].items[0].f += 0.5;
	Check(NeedsBuild(entries, &changed) == std::vector<std::string>{ "frame" }, "a changed colour needs a build");
	auto tiny = style;
	for (size_t i = 0; i < tiny.keys.size(); ++i)
		if (tiny.keys[i] == "frame") tiny.items[i].items[0].f += 0.00005;
	Check(NeedsBuild(entries, &tiny).empty(), "within the tolerance");

	auto noFrame = json::MakeObject(), noTint = json::MakeObject();
	for (size_t i = 0; i < style.keys.size(); ++i) {
		if (style.keys[i] != "frame") json::Set(noFrame, style.keys[i], style.items[i]);
		if (style.keys[i] != "Skin.pattern") json::Set(noTint, style.keys[i], style.items[i]);
	}
	Check(NeedsBuild(entries, &noFrame).empty(), "a colour key missing from an older manifest is not reported");
	Check(NeedsBuild(entries, &noTint) == std::vector<std::string>{ "Skin.pattern" }, "a baked tint missing from the manifest is reported");

	auto withRule = style;   // a run-time tint in the manifest style: never compared
	json::Set(withRule, "Skin.rule", json::MakeFloat(42));
	Check(NeedsBuild(entries, &withRule).empty(), "run-time tints never need a build");

	auto sizes = json::MakeObject();
	for (const auto& s : model::detail::SizeDefaults())
		if (!model::detail::IsTextRole(s.key)) json::Set(sizes, s.key, json::MakeFloat(s.def));
	Check(SizesChanged(m, &sizes).empty(), "sizes as baked");
	Check(!SizesChanged(m, nullptr).empty() && SizesChanged(m, nullptr).size() == sizes.keys.size(), "no layout pack: every size counts");
	auto bigger = sizes;
	bigger.items[0].f += 1;
	Check(SizesChanged(m, &bigger) == std::vector<std::string>{ bigger.keys[0] }, "a changed size");

	const auto text = BuildStatus(entries, { "frame", "Skin.pattern" }, true);
	std::string frameHelp, patternHelp;
	for (const auto& e : entries) {
		if (e.section == "Colours" && e.name == "frame") frameHelp = e.help;
		if (e.section == "Skin" && e.name == "pattern") patternHelp = e.help;
	}
	Check(text == "Build pack to apply: " + frameHelp + ", " + patternHelp + ", sizes", "the status names what changed, then sizes");
	Check(BuildStatus(entries, {}, false) == "The packs match the colours and sizes.", "nothing to build");

	std::printf("%u checks, %u failures\n", checks, failures);
	return failures ? 1 : 0;
}
