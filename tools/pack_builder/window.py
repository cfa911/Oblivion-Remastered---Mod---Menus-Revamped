"""The builder window (Tkinter). Thin: Controller holds the logic (colour model, game folder, Build through app.build_pack,
Vanilla colours) and is tested without Tk; App only draws it.

Step B (owner 2026-11-12, decisions 38-45): tabs Colours, Text, Rows, Runtime. Colours shows one row per role; the Advanced
switch shows each role's elements under it ("own value" until Reset). Text shows the text role scales (percent of today's size)
and, with Advanced, each font size; Rows shows row height, icon size and count font per list. Buttons: Build, Save, Reset to
Dark, Vanilla colours (uPreset=0: the game's own colours, the sizes stay; Build sets uPreset=2 again), Open log.
"""
import json
import os
import queue
import threading
from contextlib import suppress
from pathlib import Path

from pack_builder import VERSION, app, install, paths
from pack_builder.model import BUILD_NOTE, ColourModel, ModelError

TITLE = "Menus Revamped - Custom Pack Builder"
LAYOUT_PACK = "MRLayout_P"


class Controller:
    def __init__(self, paks, target, retoc, colours_path, ddstools=None, art=None, log_path=None, pack="MRCustom_P"):
        self.paks, self.target, self.retoc = paks, Path(target), retoc
        self.colours_path, self.ddstools, self.art = Path(colours_path), ddstools, art
        self.log_path, self.pack = Path(log_path) if log_path else None, pack
        self.model = ColourModel(self.colours_path)   # ModelError for a broken file: the window offers Reset to Dark

    def manifest(self, pack):
        try:
            return json.loads((self.target / f"{pack}.json").read_text(encoding="utf-8"))
        except (OSError, ValueError):
            return {}

    def manifest_style(self):
        return self.manifest(self.pack).get("style", {})

    def stale(self):
        return self.model.needs_build(self.manifest_style())

    def stale_sizes(self):
        return self.model.sizes_changed(self.manifest(LAYOUT_PACK).get("sizes", {}))

    def save(self):
        return self.model.save(self.colours_path)

    def vanilla(self):
        """Decisions 40 and 47: [Style] uPreset=0 in MenusRevamped.ini. At the next start the plugin's Classic path removes the
        colour pack from ~mods, installs the sizes pack and runs none of its run-time Dark styling; the packs stay in place
        (Build writes uPreset=2 back)."""
        ini = app.set_preset(self.colours_path, 0)
        return (f"Classic preset set (uPreset=0 in {ini.name}). Start the game: the game's own colours show and the sizes stay. "
                f"Build switches back to the Custom colours (uPreset=2).")

    def build(self, log):
        """Saves the colour file (the plugin compares the pack with it), then the one build; the log file gets every line."""
        self.save()
        lines = []

        def both(line):
            lines.append(str(line))
            log(str(line))
        result = app.build_pack(paks=self.paks, target=self.target, retoc=self.retoc, colours_path=self.colours_path,
                                ddstools=self.ddstools, art=self.art, pack=self.pack, log=both)
        if result.ok:   # decision 47: a good Build switches to the Custom colours
            try:
                app.set_preset(self.colours_path, 2)
                both("Custom preset set (uPreset=2 in MenusRevamped.ini).")
            except OSError as error:
                both(f"Could not set uPreset=2 in MenusRevamped.ini ({error}): set it by hand.")
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
        self.rows, self.role_rows, self.size_rows, self.advanced_rows = {}, {}, {}, []
        self.events = queue.Queue()
        self.advanced = tk.BooleanVar(value=False)
        root.title(TITLE)
        root.geometry("860x760")
        top = ttk.Frame(root, padding=8)
        top.pack(fill="x")
        self.game_label = ttk.Label(top, text="")
        self.game_label.grid(row=0, column=0, sticky="w")
        ttk.Button(top, text="Change game folder...", command=self.pick_game).grid(row=0, column=1, padx=6)
        ttk.Checkbutton(top, text="Advanced (each element)", variable=self.advanced, command=self.show_advanced).grid(row=0, column=2)
        ttk.Label(top, text=f"Colour file: {controller.colours_path}").grid(row=1, column=0, columnspan=3, sticky="w")
        top.columnconfigure(0, weight=1)
        book = ttk.Notebook(root)
        book.pack(fill="both", expand=True, padx=8)
        for label, groups in controller.model.pages():
            book.add(self.tab_page(book, label, groups), text=label)
        ttk.Label(root, text=BUILD_NOTE, wraplength=830, padding=(8, 6, 8, 0)).pack(fill="x")
        self.status = ttk.Label(root, text="", wraplength=830, padding=(8, 2))
        self.status.pack(fill="x")
        buttons = ttk.Frame(root, padding=8)
        buttons.pack(fill="x")
        self.buttons = [ttk.Button(buttons, text=t, command=f) for t, f in
                        (("Build", self.build), ("Save", self.save), ("Reset to Dark", self.reset),
                         ("Vanilla colours", self.vanilla), ("Open log", self.open_log))]
        for b in self.buttons:
            b.pack(side="left", padx=4)
        self.show_game()
        self.show_advanced()
        self.refresh_status()

    # ---- the pages ----
    def scrolled(self, book):
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
        return outer, inner

    def tab_page(self, book, label, groups):
        outer, inner = self.scrolled(book)
        i = 0
        for heading, entries in groups:
            if label == "Colours":   # one row per role; its elements are Advanced rows
                role = self.c.model.role_of(entries[0].key)
                self.role_row(inner, i, role, heading)
                i += 1
                for e in entries:
                    self.colour_row(inner, i, e, advanced=True)
                    i += 1
                continue
            self.ttk.Label(inner, text=heading, font=("Segoe UI", 10, "bold"), wraplength=780).grid(
                row=i, column=0, columnspan=5, sticky="w", pady=(10 if i else 0, 2))
            i += 1
            for e in entries:
                if label == "Runtime":
                    self.colour_row(inner, i, e, advanced=False)
                else:
                    self.size_row(inner, i, e, advanced=e.kind == "font")
                i += 1
        return outer

    def role_row(self, inner, i, role, heading):
        tk, ttk = self.tk, self.ttk
        swatch = tk.Button(inner, width=4, relief="groove", command=lambda: self.pick_role(role, heading))
        swatch.grid(row=i, column=0, padx=(0, 6), pady=(8, 2))
        ttk.Label(inner, text=heading, font=("Segoe UI", 9, "bold"), wraplength=520, justify="left").grid(
            row=i, column=1, sticky="w", pady=(8, 2))
        text = tk.StringVar(value=self.c.model.role_text(role))
        field = ttk.Entry(inner, textvariable=text, width=16)
        field.grid(row=i, column=2, padx=6, pady=(8, 2))
        field.bind("<Return>", lambda ev: self.take_role(role))
        field.bind("<FocusOut>", lambda ev: self.take_role(role))
        self.role_rows[role] = (swatch, text)
        self.paint_role(role)

    def colour_row(self, inner, i, e, advanced):
        tk, ttk = self.tk, self.ttk
        indent = 24 if advanced else 0
        swatch = tk.Button(inner, width=3, relief="groove", command=lambda e=e: self.pick_colour(e))
        swatch.grid(row=i, column=0, padx=(indent, 6), pady=1, sticky="e")
        label = ttk.Label(inner, text=e.help, wraplength=500, justify="left")
        label.grid(row=i, column=1, sticky="w")
        text = tk.StringVar(value=self.c.model.text(e.section, e.name))
        field = ttk.Entry(inner, textvariable=text, width=22)
        field.grid(row=i, column=2, padx=6)
        field.bind("<Return>", lambda ev, e=e: self.take_text(e))
        field.bind("<FocusOut>", lambda ev, e=e: self.take_text(e))
        own = ttk.Label(inner, text="")
        own.grid(row=i, column=3, sticky="w")
        widgets = [swatch, label, field, own]
        if advanced:
            reset = ttk.Button(inner, text="Reset", width=6, command=lambda e=e: self.reset_element(e))
            reset.grid(row=i, column=4)
            widgets.append(reset)
            self.advanced_rows.append(widgets)
        self.rows[e.key] = (swatch, text, own)
        self.paint(e)

    def size_row(self, inner, i, s, advanced):
        tk, ttk = self.tk, self.ttk
        label = ttk.Label(inner, text=s.help + (" (%)" if s.kind == "scale" else ""), wraplength=500, justify="left")
        label.grid(row=i, column=1, sticky="w", padx=(24 if advanced else 0, 0))
        text = tk.StringVar(value=self.c.model.size_text(s.key))
        field = ttk.Entry(inner, textvariable=text, width=10)
        field.grid(row=i, column=2, padx=6, sticky="w")
        field.bind("<Return>", lambda ev, s=s: self.take_size(s))
        field.bind("<FocusOut>", lambda ev, s=s: self.take_size(s))
        own = ttk.Label(inner, text="")
        own.grid(row=i, column=3, sticky="w")
        widgets = [label, field, own]
        if advanced:
            reset = ttk.Button(inner, text="Reset", width=6, command=lambda s=s: self.reset_size(s))
            reset.grid(row=i, column=4)
            widgets.append(reset)
            self.advanced_rows.append(widgets)
        self.size_rows[s.key] = (text, own)

    def show_advanced(self):
        for widgets in self.advanced_rows:
            for w in widgets:
                (w.grid if self.advanced.get() else w.grid_remove)()

    # ---- painting ----
    def paint(self, e):
        swatch, text, own = self.rows[e.key]
        colour = self.c.model.swatch(e.section, e.name)
        swatch.configure(bg=colour, activebackground=colour)
        text.set(self.c.model.text(e.section, e.name))
        own.configure(text="own value" if self.c.model.is_own(e.key) else "")

    def paint_role(self, role):
        swatch, text = self.role_rows[role]
        colour = self.c.model.role_swatch(role)
        swatch.configure(bg=colour, activebackground=colour)
        text.set(self.c.model.role_text(role))

    def paint_sizes(self):
        for key, (text, own) in self.size_rows.items():
            text.set(self.c.model.size_text(key))
            own.configure(text="own value" if self.c.model.is_own_size(key) else "")

    def paint_all(self):
        for role in self.role_rows:
            self.paint_role(role)
        for key in self.rows:
            self.paint(self.c.model.by_key(key))
        self.paint_sizes()
        self.refresh_status()

    # ---- edits ----
    def error(self, message):
        from tkinter import messagebox
        messagebox.showerror(TITLE, message, parent=self.root)

    def take_text(self, e):
        _, text, _ = self.rows[e.key]
        if text.get().strip() == self.c.model.text(e.section, e.name):
            return
        try:
            self.c.model.set_text(e.section, e.name, text.get())
        except ValueError as error:
            self.error(str(error))
        self.paint(e)
        self.refresh_status()

    def take_role(self, role):
        _, text = self.role_rows[role]
        if text.get().strip() == self.c.model.role_text(role):
            return
        try:
            self.c.model.set_role_text(role, text.get())
        except ValueError as error:
            self.error(str(error))
        self.paint_all()

    def take_size(self, s):
        text, _ = self.size_rows[s.key]
        if text.get().strip() == self.c.model.size_text(s.key):
            return
        try:
            self.c.model.set_size(s.key, text.get())
        except ValueError as error:
            self.error(str(error))
        self.paint_sizes()
        self.refresh_status()

    def pick_colour(self, e):
        from tkinter import colorchooser
        picked = colorchooser.askcolor(self.c.model.swatch(e.section, e.name), parent=self.root, title=e.help)
        if picked and picked[0]:
            self.c.model.set_rgb(e.section, e.name, picked[0])
            self.paint(e)
            self.refresh_status()

    def pick_role(self, role, heading):
        from tkinter import colorchooser
        picked = colorchooser.askcolor(self.c.model.role_swatch(role), parent=self.root, title=heading)
        if picked and picked[0]:
            self.c.model.set_role(role, picked[0])
            self.paint_all()

    def reset_element(self, e):
        self.c.model.reset_element(e.key)
        self.paint(e)
        self.refresh_status()

    def reset_size(self, s):
        self.c.model.reset_size(s.key)
        self.paint_sizes()
        self.refresh_status()

    def refresh_status(self):
        stale = self.c.stale()
        labels = {("Skin." + e.name if e.section == "Skin" else e.name): e.help for e in self.c.model.entries}
        parts = [labels[key] for key in stale]
        if self.c.stale_sizes():
            parts.append("sizes")
        self.status.configure(text=("Build pack to apply: " + ", ".join(parts)) if parts else
                              "The packs match the colours and sizes.")

    # ---- game folder ----
    def show_game(self):
        self.game_label.configure(text=f"Game: {self.c.paks}" if self.c.paks else "Game: not found - choose the game folder")
        self.buttons[0].configure(state="normal" if self.c.paks else "disabled")

    def pick_game(self):
        from tkinter import filedialog
        folder = filedialog.askdirectory(parent=self.root, title="Choose the Oblivion Remastered game folder")
        if not folder:
            return
        try:
            self.c.paks = paths.check_paks(folder)
        except SystemExit as stop:
            self.error(str(stop.code))
        self.show_game()

    # ---- buttons ----
    def save(self):
        try:
            path = self.c.save()
        except OSError as error:
            self.error(f"Could not write the colour file: {error}")
            return
        self.status.configure(text=f"Saved {path}. " + BUILD_NOTE)

    def reset(self):
        self.c.model.reset_to_dark()
        self.paint_all()
        self.status.configure(text="Reset to the Dark colours and today's sizes (not saved yet): Build or Save to keep them.")

    def vanilla(self):
        from tkinter import messagebox
        if not messagebox.askyesno(TITLE, "Switch to the game's own colours (uPreset=0 in MenusRevamped.ini)? The sizes stay. "
                                          "Build switches back to the Custom colours.", parent=self.root):
            return
        try:
            message = self.c.vanilla()
        except OSError as error:
            message = f"Could not write MenusRevamped.ini ({error})."
        self.status.configure(text=message)

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
