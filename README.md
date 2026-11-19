# RQ Scene Editor

A window for writing [RQ MOD](https://github.com/RQ-Modding/RQ-Mod)'s scene files for Rance Quest
Magnum without Lua and without typing JSON by hand.

A scene file is a mod's scene as JSON: the cast, then the steps the game plays (lines, pictures,
music, choices, branches). A mod plays one with `rq.scene.file`. The editor lists a mod's scene
files, shows each file's steps as a list with its branches as a tree, and writes the file back.

**State: 0.1.0, not released.** This is the editor's skeleton:

- it finds the game folder and lists the game's mods, or opens a mod's folder from anywhere;
- it lists the mod's scene files, with a line count and why a file cannot be read;
- it shows a file's steps, with each step summed up in a sentence and its branches (`then`,
  `else`, a choice's rows) folded under it;
- it adds a step of any kind, copies, moves and deletes steps;
- it edits a step as the JSON the file holds;
- it has undo and redo, and it notices when a file changes on disk;
- it reads each file as RQ MOD reads it, so a file the game refuses shows the game's own message;
- it has two looks, Copandon (dark, the default) and Parchment (light).

Still to come: fields of their own for each kind of step, with faces, pictures, music and sounds
picked from the game; a sketch of the stage; the conditions; RQ MOD's own checks on the file; and
Play, which starts the scene in the running game. It will ship in RQ MOD's package, as Rance
Browser does.

It is a fan project. AliceSoft and MangaGamer do not make or endorse it, and it contains nothing of
their games.

## Using it

Run `rq-scene-editor.exe`. It finds the game when it sits in the game's folder or in a folder inside
it (RQ MOD's `xsystem4\`); otherwise it asks for the folder once and remembers it. Click the game's
name at the bottom to pick another.

- **Mods:** the box at the top left lists the mods in the game's `mods` folder (each folder with a
  `mod.json`). **Another mod's folder...** opens one that is somewhere else.
- **Scenes:** every `.json` under the mod's `scenes` folder, and any other `.json` in the mod's folder
  that is a scene file (one with `format` and `steps`). **+ New scene** writes `scenes/<name>.json`.
- **Steps:** click a step to see it on the right. **+ Add step** puts a new one after the selected
  step. Double-click a `goto` to jump to its label.
- **Keys:** Ctrl+S saves, Ctrl+Z undoes, Ctrl+Y (or Ctrl+Shift+Z) redoes, Up and Down move through the
  steps, Alt+Up and Alt+Down move a step, Ctrl+D copies it and Delete removes it.

### One layout for every file

The editor writes every scene file in a layout of its own, so that a change moves only the lines it
changes:

- the top fields in the order `$schema`, `format`, `once`, `cast`, `steps`, each cast entry on one
  line;
- each step on one line, with its own key first (`say`, `music`, `choice`...);
- a step that holds other steps (`then`, `else`, a choice row's `steps`) opens its list at the end of
  its line, with the steps under it four spaces further in;
- a choice that does not fit in 100 columns, or that has a row with steps, takes one row per line;
- two spaces of indentation, LF line ends, a final newline, no byte-order mark.

A file laid out by hand or by another program keeps its values. The first save asks before it
rewrites the file in this layout.

A file in a newer scene format than the editor knows opens read-only, and so does one the game
cannot read: fix that file in a text editor, and the editor reads it again as soon as it changes.

Nothing is written into the game. The program keeps a small `rq-scene-editor.json` beside itself,
with the game folder, the look, and the last mod and file.

### On the command line

```
rq-scene-editor [--game DIR] [--mod ID | --mod-dir DIR] [--file REL] [--look copandon|parchment] [--scale F]
rq-scene-editor --check [--dir DIR | --game DIR [--mod ID]] [--out FILE]
rq-scene-editor --format FILE...
rq-scene-editor --shot FILE.png --game DIR [--mod ID | --mod-dir DIR] [--file REL] [--select PLACE]
                [--look copandon|parchment] [--size WxH] [--scale F] [--do ACTION]...
rq-scene-editor --selftest FILE
```

- `--check` reads every scene file of a folder (or of the game's mods) as RQ MOD reads it, writes
  it in the editor's layout, reads that back, and prints a JSON report. A file passes when it reads
  back to the same values and a second write gives the same text. Exit 1 when one does not.
- `--format` writes the files in the editor's layout.
- `--shot` draws the window without showing it and saves it. `--do` presses the window's buttons
  first: `select=PLACE`, `add=KIND`, `apply=JSON`, `copy`, `delete`, `up`, `down`, `undo`,
  `redo`, `tab=steps|cast`, `modal=unsaved|layout|changed|new`, `fold`. Nothing is saved.
- `--selftest` tries editing, undo, saving and changes on disk on FILE. FILE must be a copy: it is
  written.
- A step's place is written as `steps[4]`, `steps[2].then[0]` or `steps[3].choice[1].steps[0]`.

The runs that need no one (`--check`, `--format`, `--shot`, `--selftest`) print a failure to the
console and exit with code 3 instead of opening a dialog.

## Building

Windows, with [MSYS2](https://www.msys2.org/) in its MINGW64 shell:

```
pacman -S --needed base-devel git mingw-w64-x86_64-gcc mingw-w64-x86_64-meson \
    mingw-w64-x86_64-pkg-config mingw-w64-x86_64-python mingw-w64-x86_64-SDL2 \
    mingw-w64-x86_64-libsndfile mingw-w64-x86_64-libjpeg-turbo mingw-w64-x86_64-libwebp \
    mingw-w64-x86_64-libpng mingw-w64-x86_64-libdeflate
git clone --recurse-submodules https://github.com/RQ-Modding/rq-scene-editor.git
cd rq-scene-editor
meson setup build --buildtype=release
ninja -C build
```

The program is `build/rq-scene-editor.exe`, linked statically: it needs only Windows' own DLLs.

libjpeg-turbo 3.2.1 cannot be linked statically beside zlib (both define `z_errmsg`). Until that is
fixed, install 3.2.0:
`pacman -U https://repo.msys2.org/mingw/mingw64/mingw-w64-x86_64-libjpeg-turbo-3.2.0-1-any.pkg.tar.zst`.

The editor reads the game through [Rance Browser](https://github.com/RQ-Modding/rance-browser)'s
readers, the static library `rance_core`, which comes with Dear ImGui (cimgui), SDL2 and libsys4. Both
are submodules in `subprojects/`. Inside RQ MOD, meson shares one copy of each with the engine.

The window uses Windows' own fonts: Segoe UI, Consolas for the JSON, Yu Gothic for Japanese text.

## License

GPL-2.0-or-later (`COPYING`). `CREDITS.md` names what it is built from.
