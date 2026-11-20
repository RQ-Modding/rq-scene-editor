# Changelog

Each version, newest first. A version in RQ MOD's package is named in RQ MOD's own CHANGELOG.

## 0.5.0 (not released)

Pickers: the game's pictures, music and sounds by their look and their names.

- **Pick... beside every picture.** A window of the game's own pictures, in tabs as a scene uses
  them: the faces and the standing pictures by character (RQ MOD's names; the faces also by the
  game's own name plates), the backgrounds (the wallpapers among them, from 5500), the small scene
  CGs, the scene CGs, the game's other CGs by hundreds, and the pictures in the mod's own folder by
  folder. A search by number or name, a larger picture under the mouse, and the pick shown at the
  bottom with what it writes where; a double click or **Use** writes it, one step to undo. It opens
  on the value the field holds.
- **Faces and the cast.** For a line's speaker the file's cast shows above the faces; a face the
  cast has writes the cast's name, and **Add to the cast as...** puts another face into the cast
  with a name and writes that name. A cast entry's face has a Pick... of its own.
- **The scene CGs** (the game's event pictures, its adult scenes among them) show only when asked,
  once a session; the picker never opens on them.
- **Tracks and sounds** in a list, the tracks with their names, each with ▶ to hear it before
  picking it; a track that does not loop says so. The window's title says which: a track or a
  sound.
- A GIF or a moving WebP in the mod's folder shows its kind in the picker: the game moves it, the
  editor does not draw it.
- **For checks and agents,** `--do picker[=KEY]` opens a picker and `find`, `ptab`, `group`,
  `tile`, `showscene`, `use` and `addcast` drive it; `printstep` prints the step and the cast as
  JSON; `mouse=X,Y`, `mdown` and `mup` reach the window as a mouse does. A shot waits a few frames
  after its last actions, so a tab or a scroll they set shows.

## 0.4.0 (not released)

The stage moves: a step's motion, and the scene played on from a step, without the game.

- **▶ plays a step.** On the stage, ▶ plays the selected step's motion from the stage before it:
  - the game's transitions, as RQ MOD's engine draws them (its shaders, and the effects it draws in
    C written again as shaders); an effect it does not draw plays as its crossfade through a blur,
    27 shakes the screen, and 0 or no time shows the new screen at once;
  - a background sliding out and in (`background_in`, `background_out`) with the game's jingle;
  - a picture set's effects: the crossfade, 1 (the screen cleared, then waves for a second), 7001
    and 7002 (the game's shape shrinking over the screen, then a crossfade), with the jingle;
  - shakes of the screen, the face and the portrait in the game's modes, the flash, black and
    white;
  - the letters coming in one after another at the text speed (`speed`, or the player's), each
    fading in, and the click mark turning;
  - the world map: the game's map, its jingle, the place's tag (the game's Flash file, playing) and
    the place's name.
- **Play from here.** The scene plays on from the selected step: a click or Space shows the rest of
  a page, then the next; a choice waits for a click on its menu and goes on with that row; an `if`
  asks which way it goes; `goto` and labels are followed and `end` ends it; the music changes as
  the game's music command changes it (at once, or the old track falling while the new one rises),
  starting with the track the steps before put on; sounds and jingles play several at once. A
  mod's function and the event flags are passed by and named under the stage. The step playing is
  marked in the list. Escape or Stop ends it.
- **Effects, shakes and places by name.** Beside an effect's number, the engine's list of effects
  in words (a picture set's own three); beside a shake's mode, what it does ("Up and down, 20
  pixels, 0.1 s"); the world map's step picks one of the game's 40 places by name, or a point of
  the mod's own with its name. A row hovered in an effect's or a shake's list plays the step with
  it on the stage, over and over.
- **For checks and agents,** `--do` drives the stage (`play`, `playon`, `clock=MS`, `click`,
  `pick=ROW`, `answer=then|else`, `preview=EFFECT`, `print`), and `frame` lets a run go through a
  scene between frames; `--mixer-check` checks the sounds' mixing without a sound device.

## 0.3.0 (not released)

The stage as the game draws it, from the game's own pictures.

- **The game's screen.** The stage is no longer a sketch. It draws the screen from the game's own
  pictures, at the game's places and in the game's order:
  - the wallpaper, the scene CG, the background in its frame, the small picture and the portrait;
    `cg`, `black`, `white` and `clear` empty the layers as the game does, and `set` puts the game's
    mask picture under its layers;
  - the game's message box for each kind of line, with the say, think and narrate frames, the wide
    frames of a `page` line (three lines, no face) and the stage directions' frame with its edge;
  - the face in its frame, the name plate in the game's bold yellow, shrunk to fit as the game
    shrinks it, the AUTO, SKIP and CONFIG buttons and the click mark;
  - the page in the game's font and colours, at the game's place and line height, an opening quote
    hanging left of the lines, and an apostrophe drawn as the game's box draws it (’);
  - a choice's menu in the game's look, at its place, with its buttons and separators, over an empty
    box.

  Measured against shots of the game, the text falls within a pixel or two of where the game puts
  it. Motion (effects, slides, shakes) does not show yet.
- **Name plates from the game.** A face's plate shows the name the game gives that face, read from
  the game's own code, so a face the game shows without a plate has none here either. Without the
  game's code, the first word of the character's name stands in.
- **The line limit per box.** A page's line count, in its field and on the stage, is checked
  against the box its line uses: 4 lines, or 3 in the wide frames of a `page` line.
- **For checks and agents,** `--do stage` draws the stage alone over the whole window; at
  `--size 1024x768 --scale 1` it is the game's screen pixel for pixel.

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
