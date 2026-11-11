"""The builder window (Tkinter). Thin: Controller holds the logic (colour model, game folder, Build through app.build_pack) and
is tested without Tk; App only draws it. Owner decision 11: one picker per CustomColours.ini entry, grouped by section and
labelled with the file's help text, swatches as the preview, buttons Build, Reset to Dark, Open log.
"""
import json
import os
import queue
import threading
from pathlib import Path

from pack_builder import VERSION, app, paths
from pack_builder.model import BUILD_NOTE, ColourModel, ModelError

TITLE = "Menus Revamped - Custom Pack Builder"
SECTIONS = (("Colours", "Colours (baked)"), ("Tab", "Tabs (baked)"), ("Skin", "Skin (run time)"))


class Controller:
    def __init__(self, paks, target, retoc, colours_path, ddstools=None, art=None, log_path=None, pack="MRCustom_P"):
        self.paks, self.target, self.retoc = paks, Path(target), retoc
        self.colours_path, self.ddstools, self.art = Path(colours_path), ddstools, art
        self.log_path, self.pack = Path(log_path) if log_path else None, pack
        self.model = ColourModel(self.colours_path)   # ModelError for a broken file: the window offers Reset to Dark

    def manifest_style(self):
        try:
            return json.loads((self.target / f"{self.pack}.json").read_text(encoding="utf-8")).get("style", {})
        except (OSError, ValueError):
            return {}

    def stale(self):
        return self.model.needs_build(self.manifest_style())

    def save(self):
        return self.model.save(self.colours_path)

    def build(self, log):
        """Saves the colour file (the plugin compares the pack with it), then the one build; the log file gets every line."""
        self.save()
        lines = []

        def both(line):
            lines.append(str(line))
            log(str(line))
        result = app.build_pack(paks=self.paks, target=self.target, retoc=self.retoc, colours_path=self.colours_path,
                                ddstools=self.ddstools, art=self.art, pack=self.pack, log=both)
        if self.log_path:
            try:
                self.log_path.parent.mkdir(parents=True, exist_ok=True)
                self.log_path.write_text(f"{TITLE} {VERSION}\n" + "\n".join(lines) + "\n", encoding="utf-8")
            except OSError:
                pass
        return result


def controller_from_defaults():
    d = paths.defaults()
    return Controller(paks=d["paks"], target=d["out"], retoc=d["retoc"], colours_path=d["colours"], ddstools=d["ddstools"],
                      art=d["art"], log_path=d["log"])


class App:
    def __init__(self, root, controller):
        import tkinter as tk
        from tkinter import ttk
        self.tk, self.ttk, self.root, self.c = tk, ttk, root, controller
        self.rows, self.events = {}, queue.Queue()
        root.title(TITLE)
        root.geometry("760x720")
        top = ttk.Frame(root, padding=8)
        top.pack(fill="x")
        self.game_label = ttk.Label(top, text="")
        self.game_label.grid(row=0, column=0, sticky="w")
        ttk.Button(top, text="Change game folder...", command=self.pick_game).grid(row=0, column=1, padx=6)
        ttk.Label(top, text=f"Colour file: {controller.colours_path}").grid(row=1, column=0, columnspan=2, sticky="w")
        top.columnconfigure(0, weight=1)
        book = ttk.Notebook(root)
        book.pack(fill="both", expand=True, padx=8)
        for section, label in SECTIONS:
            book.add(self.section_page(book, section), text=f"{label} - {len(controller.model.section(section))}")
        ttk.Label(root, text=BUILD_NOTE, wraplength=730, padding=(8, 6, 8, 0)).pack(fill="x")
        self.status = ttk.Label(root, text="", wraplength=730, padding=(8, 2))
        self.status.pack(fill="x")
        buttons = ttk.Frame(root, padding=8)
        buttons.pack(fill="x")
        self.buttons = [ttk.Button(buttons, text=t, command=f) for t, f in
                        (("Build", self.build), ("Save", self.save), ("Reset to Dark", self.reset), ("Open log", self.open_log))]
        for b in self.buttons:
            b.pack(side="left", padx=4)
        self.show_game()
        self.refresh_status()

    # ---- the colour pages ----
    def section_page(self, book, section):
        tk, ttk = self.tk, self.ttk
        outer = ttk.Frame(book)
        canvas = tk.Canvas(outer, highlightthickness=0)
        bar = ttk.Scrollbar(outer, orient="vertical", command=canvas.yview)
        inner = ttk.Frame(canvas, padding=6)
        inner.bind("<Configure>", lambda e: canvas.configure(scrollregion=canvas.bbox("all")))
        canvas.create_window((0, 0), window=inner, anchor="nw")
        canvas.configure(yscrollcommand=bar.set)
        canvas.pack(side="left", fill="both", expand=True)
        bar.pack(side="right", fill="y")
        canvas.bind("<Enter>", lambda e: canvas.bind_all("<MouseWheel>", lambda w: canvas.yview_scroll(-w.delta // 120, "units")))
        canvas.bind("<Leave>", lambda e: canvas.unbind_all("<MouseWheel>"))
        for i, e in enumerate(self.c.model.section(section)):
            swatch = tk.Button(inner, width=4, relief="groove", command=lambda e=e: self.pick_colour(e))
            swatch.grid(row=i, column=0, padx=(0, 6), pady=2)
            ttk.Label(inner, text=e.name, width=18).grid(row=i, column=1, sticky="w")
            text = tk.StringVar(value=self.c.model.text(e.section, e.name))
            field = ttk.Entry(inner, textvariable=text, width=24)
            field.grid(row=i, column=2, padx=6)
            field.bind("<Return>", lambda ev, e=e: self.take_text(e))
            field.bind("<FocusOut>", lambda ev, e=e: self.take_text(e))
            ttk.Label(inner, text=e.help).grid(row=i, column=3, sticky="w")
            self.rows[(e.section, e.name)] = (swatch, text)
            self.paint(e)
        return outer

    def paint(self, e):
        swatch, text = self.rows[(e.section, e.name)]
        colour = self.c.model.swatch(e.section, e.name)
        swatch.configure(bg=colour, activebackground=colour)
        text.set(self.c.model.text(e.section, e.name))

    def take_text(self, e):
        from tkinter import messagebox
        _, text = self.rows[(e.section, e.name)]
        if text.get().strip() == self.c.model.text(e.section, e.name):
            return
        try:
            self.c.model.set_text(e.section, e.name, text.get())
        except ValueError as error:
            messagebox.showerror(TITLE, str(error), parent=self.root)
        self.paint(e)
        self.refresh_status()

    def pick_colour(self, e):
        from tkinter import colorchooser
        picked = colorchooser.askcolor(self.c.model.swatch(e.section, e.name), parent=self.root, title=f"{e.name}: {e.help}")
        if picked and picked[0]:
            self.c.model.set_rgb(e.section, e.name, picked[0])
            self.paint(e)
            self.refresh_status()

    def refresh_status(self):
        stale = self.c.stale()
        self.status.configure(text=("Needs a Build: " + ", ".join(stale)) if stale else
                              "The Custom pack matches the baked colours.")

    # ---- game folder ----
    def show_game(self):
        self.game_label.configure(text=f"Game: {self.c.paks}" if self.c.paks else "Game: not found - choose the game folder")
        self.buttons[0].configure(state="normal" if self.c.paks else "disabled")

    def pick_game(self):
        from tkinter import filedialog, messagebox
        folder = filedialog.askdirectory(parent=self.root, title="Choose the Oblivion Remastered game folder")
        if not folder:
            return
        try:
            self.c.paks = paths.check_paks(folder)
        except SystemExit as stop:
            messagebox.showerror(TITLE, str(stop.code), parent=self.root)
        self.show_game()

    # ---- buttons ----
    def save(self):
        from tkinter import messagebox
        try:
            path = self.c.save()
        except OSError as error:
            messagebox.showerror(TITLE, f"Could not write the colour file: {error}", parent=self.root)
            return
        self.status.configure(text=f"Saved {path}. " + BUILD_NOTE)

    def reset(self):
        self.c.model.reset_to_dark()
        for e in self.c.model.entries:
            self.paint(e)
        self.refresh_status()
        self.status.configure(text="Reset to the Dark colours (not saved yet): Build or Save to keep them.")

    def open_log(self):
        from tkinter import messagebox
        log = self.c.log_path
        if log and log.is_file():
            os.startfile(log)
        else:
            messagebox.showinfo(TITLE, "No build log yet: it is written by the first Build.", parent=self.root)

    def build(self):
        self.root.focus_set()   # a typed value still in its field is taken first
        for b in self.buttons:
            b.configure(state="disabled")
        self.status.configure(text="Building... (about 10 to 30 seconds; the first build downloads Oodle for retoc)")
        threading.Thread(target=lambda: self.events.put(("done", self.c.build(lambda line: self.events.put(("log", line))))),
                         daemon=True).start()
        self.root.after(100, self.poll)

    def poll(self):
        while True:
            try:
                kind, payload = self.events.get_nowait()
            except queue.Empty:
                break
            if kind == "log":
                self.status.configure(text=str(payload)[:200])
            else:
                for b in self.buttons:
                    b.configure(state="normal")
                self.show_game()
                self.refresh_status()
                self.show_result(payload)
                return
        self.root.after(100, self.poll)

    def show_result(self, result):
        tk, ttk = self.tk, self.ttk
        win = tk.Toplevel(self.root)
        win.title(TITLE + (" - built" if result.ok else " - build failed"))
        win.geometry("700x360")
        box = tk.Text(win, wrap="word")
        box.insert("1.0", result.message)
        box.configure(state="disabled")
        box.pack(fill="both", expand=True, padx=8, pady=8)
        ttk.Button(win, text="OK", command=win.destroy).pack(pady=(0, 8))


def main():
    import tkinter as tk
    from tkinter import messagebox
    root = tk.Tk()
    root.title(TITLE)
    try:
        controller = controller_from_defaults()
    except ModelError as error:
        d = paths.defaults()
        if not messagebox.askyesno(TITLE, f"The colour file cannot be read:\n{error}\n\nStart from the Dark colours? "
                                          f"(the file is rewritten at the next Save or Build)"):
            root.destroy()
            return
        backup = Path(d["colours"]).with_suffix(".ini.broken")
        try:
            os.replace(d["colours"], backup)
        except OSError:
            pass
        controller = controller_from_defaults()
    if controller.paks is None:
        messagebox.showinfo(TITLE, "The builder did not find the game from its own folder. Choose the game folder "
                                   "(the one that holds OblivionRemastered).")
    window = App(root, controller)
    if controller.paks is None:
        root.after(200, window.pick_game)
    root.mainloop()
