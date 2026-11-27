"""Style preview of the in-game Pack Builder window over a game screenshot (developer tool).

Builds tools/builder_preview/preview.cpp with MSVC against the Dear ImGui package xmake installed for the plugin, renders
src/builder/Shell.cpp with the plugin's own DX12 backend on a WARP device over the screenshot, and writes a labelled PNG.

    python tools/builder_preview/compose.py --msvc <setup_x64.bat> --imgui <package dir> --backdrop <png> --out <png>

<package dir> is the xmake package folder holding include/ and lib/imgui.lib (for example
<XMAKE_GLOBALDIR>/.xmake/packages/i/imgui/v1.92.9/<hash>). SPDX-License-Identifier: GPL-3.0-or-later
"""
import argparse
import pathlib
import subprocess
import sys
import tempfile

from PIL import Image, ImageDraw, ImageFont

HERE = pathlib.Path(__file__).resolve().parent
REPO = HERE.parent.parent


def build(msvc: str, imgui: pathlib.Path, work: pathlib.Path) -> pathlib.Path:
    exe = work / "preview.exe"
    sources = f'"{HERE / "preview.cpp"}" "{REPO / "src/builder/Shell.cpp"}" "{REPO / "src/builder/PackJob.cpp"}"'
    cmd = (f'call "{msvc}" >nul && cl /nologo /std:c++latest /EHsc /O2 /MD /utf-8 /DNOMINMAX /DWIN32_LEAN_AND_MEAN /FIWindows.h '
           f'/I"{REPO / "src"}" /I"{imgui / "include"}" {sources} /Fe"{exe}" /Fo"{work}\\\\" '
           f'/link "{imgui / "lib/imgui.lib"}" d3d12.lib dxgi.lib user32.lib imm32.lib shell32.lib ole32.lib')
    script = work / "build.bat"
    script.write_text("@echo off\n" + cmd + "\n", encoding="utf-8")
    result = subprocess.run(["cmd", "/d", "/c", str(script)], capture_output=True, text=True)
    if result.returncode != 0:
        sys.exit(result.stdout[-4000:] + result.stderr[-2000:])
    return exe


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--msvc", required=True)
    parser.add_argument("--imgui", required=True, type=pathlib.Path)
    parser.add_argument("--backdrop", required=True, type=pathlib.Path)
    parser.add_argument("--out", required=True, type=pathlib.Path)
    parser.add_argument("--plugins", type=pathlib.Path, help="folder holding MenusRevamped/CustomColours.ini (Phase 2 values)")
    parser.add_argument("--tab", type=int, default=0, help="0 Colours, 1 Text, 2 Rows, 3 Runtime")
    parser.add_argument("--advanced", action="store_true")
    parser.add_argument("--building", help="DONE/TOTAL: show a running build at that point (Phase 3 progress line)")
    parser.add_argument("--live", help="the live preview's status line (Phase 4a), e.g. 'Live preview: reading the game's art once...'")
    parser.add_argument("--auto", type=int, default=0, help="Phase 4c: 1 automatic build running, 2 'Ready next launch', 3 'not Custom' note")
    parser.add_argument("--label", default="PROPOSED: Pack Builder window (Phase 1 shell)")
    args = parser.parse_args()
    backdrop = Image.open(args.backdrop).convert("RGBA")
    width, height = backdrop.size
    with tempfile.TemporaryDirectory() as tmp:
        work = pathlib.Path(tmp)
        exe = build(args.msvc, args.imgui, work)
        (work / "in.rgba").write_bytes(backdrop.tobytes())
        subprocess.run([str(exe), str(width), str(height), str(work / "in.rgba"), str(work / "out.rgba")]
                       + ([str(args.plugins), str(args.tab), str(int(args.advanced))] if args.plugins else [])
                       + ([args.building or "-"] if args.plugins and (args.building or args.live or args.auto) else [])
                       + ([args.live or ""] if args.plugins and (args.live or args.auto) else [])
                       + ([str(args.auto)] if args.plugins and args.auto else []), check=True)
        shot = Image.frombytes("RGBA", (width, height), (work / "out.rgba").read_bytes()).convert("RGB")
    draw = ImageDraw.Draw(shot)
    try:
        font = ImageFont.truetype("arialbd.ttf", max(18, height // 40))
    except OSError:
        font = ImageFont.load_default()
    box = draw.textbbox((0, 0), args.label, font=font)
    draw.rectangle((10, 10, 30 + box[2], 30 + box[3]), fill=(200, 30, 30))
    draw.text((20, 20), args.label, fill=(255, 255, 255), font=font)
    args.out.parent.mkdir(parents=True, exist_ok=True)
    shot.save(args.out)
    print(f"preview written: {args.out}")


if __name__ == "__main__":
    main()
