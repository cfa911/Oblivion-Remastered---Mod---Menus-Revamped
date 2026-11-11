"""Entry point of MenusRevampedPackBuilder.exe (frozen by tools/build_pack_builder.py).

No arguments: the builder window. Any argument: the make_dark_pack.py command line (for example --check), its output written
to the console it was started from (the exe is a windowed program, so it attaches to that console).
"""
import sys


def attach_console():
    """A windowed exe has no stdout: borrow the parent console (PowerShell, cmd) when there is one. Output redirected to a
    file by the caller already works and is kept."""
    if sys.stdout is not None:
        return
    try:
        import ctypes
        if ctypes.windll.kernel32.AttachConsole(-1):
            sys.stdout = open("CONOUT$", "w", encoding="utf-8", errors="replace")
            sys.stderr = sys.stdout
    except (OSError, AttributeError):
        pass


def run():
    if len(sys.argv) > 1:
        if getattr(sys, "frozen", False):
            attach_console()
        import make_dark_pack
        make_dark_pack.main()
    else:
        from pack_builder import window
        window.main()


if __name__ == "__main__":
    run()
