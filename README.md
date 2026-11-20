# RQ Scene Editor

A window for writing [RQ MOD](https://github.com/RQ-Modding/RQ-Mod)'s scene files for Rance Quest
Magnum without Lua and without typing JSON by hand.

A scene file is a mod's scene as JSON: the cast, then the steps the game plays (lines, pictures,
music, choices, branches). A mod plays one with `rq.scene.file`. The editor lists a mod's scene
files, shows each file's steps as a list with its branches as a tree, and writes the file back.

**State: 0.6.0, not released.** What it does so far:

- it finds the game folder and lists the game's mods, or opens a mod's folder from anywhere;
- it lists the mod's scene files, with a line count and why a file cannot be read;
- it shows a file's steps, a line with its speaker's face and first page, a picture step with a
  small copy of its picture, music and sounds with a Play button, and the branches (`then`,
  `else`, a choice's rows) folded under their step;
- it adds a step of any kind, copies, deletes and drags steps, also into and out of branches;
- it edits each kind of step in fields of its own, with its JSON folded under them;
- it draws the stage at the selected step as the game draws its screen, from the game's own
  pictures: the layers each step before it has set, the game's message box with the face, the name
  plate and the page in the game's font, its buttons, and a choice's menu;
- it plays a step's motion on the stage (▶): the game's transitions, a background sliding in or
  out, a picture set's effects, shakes, the flash, the letters coming in at the text speed, the
  world map with its place's tag; and **Play from here** plays the scene on from the selected step,
  with its pages, choices, branches, labels, music and sounds, without the game;
- it picks an effect, a shake and a place of the world map by name, and a row hovered in the list
  plays on the stage;
- it picks a face, a picture, a track or a sound in a window of the game's own, by its look and its
  name (the faces and the standing pictures by character), or a picture in the mod's folder;
- it builds a condition from a list of checks (a flag, a variable, the mod's function, a quest
  cleared, a character recruited or in the party, an item owned, the gold, a chance), with Not and
  groups of "all of" and "any of"; quests, characters and items come from RQ MOD's names;
- it edits a choice's rows: the text, the comment, the condition that shows the row, what the row
  does (its own steps, a go-to or an end), its colour and its sound; and labels, renamed with the
  go-tos that lead to them;
- it edits the cast: faces, name plates, renaming an entry with the lines that use it;
- it has undo and redo, and it notices when a file changes on disk;
- it reads each file as RQ MOD reads it, so a file the game refuses shows the game's own message;
- it has two looks, Copandon (dark, the default) and Parchment (light).

Still to come: RQ MOD's own checks on the file in a list of problems; and Play in the game, which starts the scene in the running game. It will
ship in RQ MOD's package, as Rance Browser does.

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
- **Steps:** click a step to see it on the right: the stage as it stands there, then the step's
  fields. **+ Add step** puts a new one after the selected step, and the **+** that shows between
  two rows puts one there. Drag a step to move it, also into a branch or out of one. Double-click a
  `goto` to jump to its label.
- **Fields:** what is typed shows in the list and on the stage at once; one undo takes back a
  field's whole change. An optional field left empty is taken out of the file, so the game's
  default applies (the empty field shows it). **JSON**, under the fields, shows the step as the
  file holds it and takes any change by hand.
- **Stage:** **▶** plays the selected step's motion from the stage before it. **Play from here**
  plays on from the selected step: a click on the stage or Space (or Enter) shows the rest of a
  page, then the next one; a choice waits for a click on one of its rows; an `if` asks which way
  it goes; `goto` and labels are followed; the music changes as the game changes it, and the
  sounds and jingles play. A mod's function (`call`, `cmd`) and the event flags are passed by and
  named under the stage. Escape or **Stop** ends it. The step playing is marked in the list.
- **Effects and shakes:** beside an effect's number, the engine's effects by name; beside a
  shake's mode, what it does. A row hovered in the list plays the step with it on the stage, over
  and over. An effect the engine does not draw plays as the engine plays it (a crossfade through a
  blur). The world map's step picks one of the game's places by name, or a point of the mod's own.
- **Pick...:** beside a picture, a face, a track or a sound, a window of the game's own. Pictures
  come in tabs: the faces and the standing pictures by character (RQ MOD's names, and for the faces
  the game's own name plates), the backgrounds (the wallpapers among them, from 5500), the small
  scene CGs, the scene CGs, the game's other CGs by hundreds, and the pictures in the mod's folder.
  Search by number or name; a picture under the mouse shows larger; a double click or **Use**
  writes the pick. For a line's speaker, the file's cast shows above the faces: a face the cast has
  writes the cast's name, and **Add to the cast as...** puts another face into the cast with a
  name. The scene CGs, the game's event pictures with its adult scenes among them, show only after
  **Show the scene CGs**, once a session. A track or a sound plays with ▶ before it is picked.
- **Choices:** a choice's rows are listed under it. Click a row (or **›** beside it in the choice's
  fields) for its own fields: the text, the comment, **Shown if** (the row shows only when its
  condition holds), **Does** (goes on after the choice, its own steps under it in the list, goes to
  a label, or ends the scene with a value such as `clear`), its colour and its sound. Up, Down,
  Copy and Delete move, copy and remove the selected row; **+ Add step** on a row puts a step into
  its own steps. On the stage the row shows under the cursor, and a row with a condition is marked
  "if" (the editor's mark: the game shows the row or not).
- **Conditions:** an `if` and a row's **Shown if** are built from checks: each check is a kind from
  the list and its value, **Not** turns it around, **+ Check** adds one and **+ Group** a group of
  its own. A group is **All of**, **Any of**, **Not all of** or **None of**. A flag, a variable or a
  function offers the names the file already uses (▾); a quest, a character or an item is picked
  from RQ MOD's names by name or number, or typed as a mod's own id (`mymod:thing`). A check the
  builder does not show (a value from the mod's own code, say) stays as its JSON, to edit there.
- **Labels and go-tos:** a go-to picks one of the file's labels, with the label's place beside it;
  a go-to with no such label is red in the list and in its fields. A label renamed takes the go-tos
  to it with it, unless another label had the old name too.
- **Cast and file:** the cast, each entry with its face, its name plate and the lines that use it,
  and the file's own fields (Plays once, the format, the schema).
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
rq-scene-editor --mixer-check [--game DIR]
```

- `--check` reads every scene file of a folder (or of the game's mods) as RQ MOD reads it, writes
  it in the editor's layout, reads that back, and prints a JSON report. A file passes when it reads
  back to the same values and a second write gives the same text. Exit 1 when one does not.
- `--format` writes the files in the editor's layout.
- `--shot` draws the window without showing it and saves it. `--do` presses the window's buttons
  first: `select=PLACE`, `add=KIND`, `apply=JSON`, `copy`, `delete`, `up`, `down`, `undo`,
  `redo`, `tab=steps|cast`, `modal=unsaved|layout|changed|new`, `fold`, `page=N` (the page shown
  on the stage), `json` (the JSON unfolded), `cast=NAME`, `castadd` and `move=FROM|LIST|INDEX`
  (the step at FROM put at INDEX of the list LIST, such as `steps` or `steps[2].then`). `stage`
  draws the stage alone over the whole window: with `--size 1024x768 --scale 1` it is the game's
  screen pixel for pixel. Nothing is saved. On the stage: `play` (▶ on the selected step),
  `playon` (Play from here), `clock=MS` (the player's clock held at MS), `click`, `pick=ROW`
  (a row of the menu on show), `answer=then|else` (the `if` asked), `preview=EFFECT` (a row of an
  effect's or a shake's list hovered) and `print` (the step playing and the line under the stage,
  on stdout). The pickers: `picker` or `picker=KEY` (Pick... beside the selected step's value, or
  beside the one of that key, such as `background` in a picture set), then `find=TEXT`,
  `ptab=faces|standing|backgrounds|small|scene|other|mod`, `group=N`, `tile=NUMBER|PATH`,
  `showscene`, `use` and `addcast[=NAME]`. `printstep` prints the selected step and the cast as
  JSON on stdout. `mouse=X,Y`, `mdown` and `mup` move the mouse and press its left button, as the
  window takes a mouse. The branches: `cond=OP:PATH[:ARG]` changes the condition of the selected
  `if` or row, as the builder's buttons do: OP is `add` (ARG a kind, such as `gold`), `group`,
  `del`, `not`, `kind` (ARG the new kind), `mode` (ARG `all`, `any`, `notall` or `none`) or `value`
  (ARG the check as JSON); PATH is the check's place in the groups, empty for the whole condition,
  `1` for the second check of the top group, `1.0` for the first check in that one's group.
  `does=on|steps|goto:LABEL|end:VALUE` sets what the selected row does, `label=NAME` renames the
  selected label with its go-tos, and `printfile` prints the whole file as the editor writes it.
  `frame` draws a frame before the actions after it, so a run can go through a scene; the shot
  waits a few frames after the last actions.
- `--mixer-check` mixes Play from here's sounds without a sound device and checks them: two sounds
  at once, a track's fade halfway, its loop, a sound's end.
- `--selftest` tries editing, undo, saving and changes on disk on FILE. FILE must be a copy: it is
  written.
- A step's place is written as `steps[4]`, `steps[2].then[0]` or `steps[3].choice[1].steps[0]`;
  a choice's row as `steps[3].choice[1]`.

The runs that need no one (`--check`, `--format`, `--shot`, `--selftest`, `--mixer-check`) print a
failure to the console and exit with code 3 instead of opening a dialog. `--shot` plays no sound.

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
The stage and the lines in the list use the game's own font, read from the game folder; the stage's
pictures and the names on its plates come from the game's own files.

## License

GPL-2.0-or-later (`COPYING`). `CREDITS.md` names what it is built from.
