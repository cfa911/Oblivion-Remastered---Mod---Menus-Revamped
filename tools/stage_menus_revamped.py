"""One-time, non-destructive migration into a staging folder, never the live install."""
from pathlib import Path
import argparse
import configparser
import json
import shutil

ROOT = Path(__file__).resolve().parents[1]


def read_ini(path):
    raw = path.read_bytes()
    if raw.startswith((b"\xff\xfe", b"\xfe\xff")):
        text = raw.decode("utf-16")
    else:
        try:
            text = raw.decode("utf-8-sig")
        except UnicodeDecodeError:
            text = raw.decode("cp1252")
    parser = configparser.RawConfigParser(strict=False)
    parser.optionxform = str
    parser.read_string(text)
    return parser


def parser():
    result = configparser.RawConfigParser()
    result.optionxform = str
    return result


def merge_known(target, source, template, sections):
    source_sections = {s.casefold(): s for s in source.sections()}
    for section in sections:
        found = source_sections.get(section.casefold())
        if found is None:
            continue
        options = {k.casefold(): v for k, v in source.items(found)}
        for key in template[section]:
            if key.casefold() in options:
                if not target.has_section(section):
                    target.add_section(section)
                target.set(section, key, options[key.casefold()])


def copy_missing(source, destination):
    if not source.is_dir():
        return
    for file in source.rglob("*.png"):
        if file.is_symlink() or not file.is_file():
            continue
        target = destination / file.relative_to(source)
        if target.exists():
            continue
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(file, target)


def stage(plugins, destination, root=ROOT):
    plugins = Path(plugins).resolve()
    destination = Path(destination).resolve()
    if destination == plugins or destination.is_relative_to(plugins):
        raise ValueError("Migration must stage outside the live plugin directory")
    destination.mkdir(parents=True, exist_ok=True)
    unified = destination / "MenusRevamped.ini"
    template = read_ini(root / "revamped/MenusRevamped.defaults.ini")
    if not unified.exists():
        existing = plugins / unified.name
        if existing.exists():
            shutil.copy2(existing, unified)
        else:
            merged = parser()
            sources = (
                ("MenuSearch.ini", ("Search", "Bar", "Inventory", "Magic", "Quest",
                                    "ContainerYours", "ContainerTheirs", "ContainerTake", "Filter")),
                ("MenuSearchControllerKeyboard.ini", ("SearchKeyboard",)),
                ("SortingOverhaul.ini", ("Controller",)),
            )
            for filename, sections in sources:
                source = plugins / filename
                # Do not write explicit default placements over legacy [Bar] fallbacks.
                values = read_ini(source) if source.exists() else template
                merge_known(merged, values, template, sections)
            merged.add_section("Log")
            merged.set("Log", "uLogLevel", "3")
            with unified.open("w", encoding="utf-8", newline="\n") as stream:
                stream.write("; Menus Revamped - imported once; originals left untouched.\n")
                merged.write(stream)
    assets = destination / "MenusRevamped"
    # Existing unified customization wins, then legacy customization, then defaults.
    copy_missing(plugins / "MenusRevamped", assets)
    for old, new, defaults in (
        ("MenuSearch", "Search", root / "menusearch/assets"),
        ("MenuSearchControllerKeyboard", "Keyboard", root / "searchkb/assets"),
        ("SortingOverhaul", "Sorting", root / "sortingoverhaul/assets/SortingOverhaul"),
    ):
        copy_missing(plugins / old, assets / new)
        copy_missing(defaults, assets / new)
    result = {
        "status": "staged; original configuration/artwork unchanged",
        "ini": unified.name,
        "png_count": sum(1 for _ in assets.rglob("*.png")),
    }
    (destination / "migration.json").write_text(json.dumps(result, indent=2), encoding="utf-8")
    return result


if __name__ == "__main__":
    cli = argparse.ArgumentParser(description=__doc__)
    cli.add_argument("--plugins", type=Path, required=True)
    cli.add_argument("--destination", type=Path, required=True)
    cli.add_argument("--source-root", type=Path, default=ROOT,
                     help="Menus Revamped source checkout (required when run from a KB copy)")
    args = cli.parse_args()
    if not (args.source_root / "revamped/MenusRevamped.defaults.ini").is_file():
        cli.error("--source-root must name the Menus Revamped source checkout")
    print(json.dumps(stage(args.plugins, args.destination, args.source_root), indent=2))
