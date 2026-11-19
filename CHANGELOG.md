# Changelog

Each version, newest first. A version in RQ MOD's package is named in RQ MOD's own CHANGELOG.

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
