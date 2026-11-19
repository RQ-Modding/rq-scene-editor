# Changelog

Each version, newest first. A version in RQ MOD's package is named in RQ MOD's own CHANGELOG.

## 0.2.0 (not released)

Each kind of step with fields of its own, and a sketch of the stage.

- **Fields for every kind.** The selected step is edited in fields made for its kind; its JSON
  stays under them, folded, for what the fields do not cover yet:
  - a line (`say`, `think`, `narrate`): who speaks, picked from the cast with their faces, a face
    by its CG number or a picture in the mod's folder; each page in a text box that wraps, with its
    line count, red past the box's 4 lines; pages added and removed;
  - a picture (`background`, `portrait`, `cg` and the others): a CG number or a file of the mod,
    with a small picture beside the field and what it is; the effect and its time, with the
    game's default shown in the empty field;
  - `set` with its five layers; `music` with its fade; `sound` and `jingle`, with a Play button and
    a warning for a number the game does not have;
  - a choice's rows, its comment and its place on the screen; a condition as JSON on one line;
    `else` added or removed; a label (a name given twice is marked); `goto` with the file's labels
    to pick from; `end` with its value and buttons for the game's quest results; `flag`, `unflag`,
    `call`, `cmd` and a note.
- **Typing shows at once.** The list and the stage follow a field as it is typed, and one undo
  takes back what was typed in a field, not each letter. Save, undo and redo first close what is
  being typed.
- **A sketch of the stage.** Above the fields, the scene as it stands at the selected step: the
  background and the pictures each step before it has set, the message box with the speaker's face
  and name, and the page in the game's own font, broken into lines where the game breaks it (a
  page longer than the box shows its 5th line in red). A choice shows its menu. Play, in a later
  version, shows the truth; this is a sketch.
- **The list shows more.** A line shows the speaker's face, their name and its first page, with a
  mark for each further page; a picture step a small copy of its picture; `music`, `sound` and
  `jingle` a Play button and the track's name.
- **Moving steps by dragging** them in the list, also into a branch (`then`, `else`, a choice's
  rows) or out of one; a step is never moved into its own branches. A **+** between two rows adds
  a step there.
- **The cast** in the Cast and file tab: each entry with its face, its name plate and the lines
  that use it. An entry is added, renamed (its lines follow) and removed while no line uses it.
  **Plays once** gives the file its `once` flag (`<mod>:<file>-seen`, which can be renamed). The faces are named from RQ MOD's names for the
  game's pictures, an expression under its character.
- **Music and sounds** play from the game's own files.
- **For checks and agents,** `--do` gained `page=N`, `json`, `cast=NAME`, `castadd` and
  `move=FROM|LIST|INDEX`.

## 0.1.0 (not released)

The editor's skeleton: the window, the files, and writing them back.

- **The window.** A top bar (the mod, the file, Save, Undo, Redo, the look), the mod's scene files
  and the file's labels on the left, the steps in the middle, the selected step on the right, and
  the file's problems and a status line at the bottom. Two looks, Copandon (dark, the default) and
  Parchment (light), switched in the top bar and remembered.
- **The game and its mods.** The game folder is found from where the program sits, or asked for
  once and remembered; a folder that is not Rance Quest Magnum's is refused. The mods are the
  folders in the game's `mods` folder with a `mod.json`; a mod's folder elsewhere opens by hand.
- **The scene files.** Every `.json` under the mod's `scenes` folder, and any other `.json` in the
  mod's folder that is a scene file, with its line count or why it cannot be read. New scene writes
  `scenes/<name>.json` with `$schema`, `format` and no steps.
- **Reading as RQ MOD reads.** A byte-order mark skipped, at most 4 MB, no zero byte, no field
  twice in one object, at most 200 levels deep, and the loader's own messages ("not valid JSON at
  line 2, column 1"). A file in a newer format opens read-only.
- **The steps.** Each step summed up in a sentence, with a coloured tag for its kind, and the steps
  under it (`then`, `else`, a choice's rows) as a tree that folds. Add a step of any of the 35
  kinds, copy, move up and down, delete; a step is edited as its JSON, and JSON that does not read
  is not applied. Double-click a `goto` to reach its label.
- **One layout.** Every file is written in the editor's layout, so that a change moves only its own
  lines. A file laid out another way keeps its values and is rewritten after one notice.
- **Undo and redo**, 300 changes deep, each one selecting the step it changed. A file changed on
  disk is read again, or, with unsaved changes, the editor asks which to keep. Saving writes a new
  file and moves it over the old one.
- **For checks and agents:** `--check` (the round trip on a folder or a game's mods), `--format`,
  `--shot` with `--do` (the window's buttons pressed before the shot) and `--selftest`. These runs
  print a failure and exit 3 instead of opening a dialog.
