/* RQ Scene Editor: a window to write RQ MOD's scene files for Rance Quest Magnum.
 *
 * A scene file is a mod's scene as JSON (format 1): the cast, then the steps the game plays (lines,
 * pictures, music, choices, branches). The editor lists a mod's scene files, shows each file's steps
 * as a list with its nested lists as a tree, and writes the file back in one layout of its own. It
 * reads the game folder through Rance Browser's readers (core.h) and draws with Dear ImGui (cimgui). */
#ifndef EDITOR_H
#define EDITOR_H

#include "core.h"
#include "cJSON.h"

#define APP_NAME "RQ Scene Editor"
#define APP_EXE "rq-scene-editor"

#ifndef EDITOR_VERSION
#define EDITOR_VERSION "0.0.0"
#endif
#ifndef EDITOR_BUILD_NOTE
#define EDITOR_BUILD_NOTE ""
#endif

/* The newest scene format the editor reads and writes; a file of a newer one opens read-only. */
#define SCENE_FORMAT 1
#define SCENE_SCHEMA "https://rq-modding.github.io/rq-modding-docs/schemas/scene.schema.json"

/* A size in the layout at the display's scale (1 for --shot unless --scale says otherwise). */
#define S(x) ((float)(x) * CORE.scale)

/* ---- the looks (look.c) ---- */

enum look_id { LOOK_COPANDON, LOOK_PARCHMENT, LOOK_COUNT };

/* The looks' colours, by role. */
enum col {
	C_PAGE, C_BG, C_PANEL, C_CHILD, C_RAISE, C_BORDER, C_LINE,
	C_TEXT, C_DIM, C_FAINT,
	C_ACCENT, C_ACCENT_HI, C_ACCENT_INK,
	C_BTN, C_BTN_HI, C_FIELD, C_FIELD_B,
	C_SEL, C_SEL_BAR, C_HOVER, C_TITLE,
	C_BAD, C_WARN, C_OK, C_PLAY,
	C_T_LINE, C_T_PIC, C_T_SND, C_T_FLOW, C_T_DATA, C_T_NOTE,
	C_MODAL_DIM,
	C_COUNT
};

extern int LOOK;                        /* the look in use */
extern const char *const look_keys[LOOK_COUNT];    /* "copandon", "parchment" */
extern const char *const look_names[LOOK_COUNT];   /* "Copandon", "Parchment" */
int look_find(const char *key);         /* -1 when unknown */
void look_apply(int look);              /* ImGui's colours and sizes for this look at CORE.scale */
ImU32 col(int c);
ImU32 col_a(int c, float alpha);        /* the colour with its alpha times alpha */
ImVec4_c colv(int c);

extern ImFont *F_UI, *F_BOLD, *F_GAME, *F_MONO;  /* the window's text, its tags, the game's font, code */
#define UI_SIZE 17.0f
#define GAME_SIZE 18.0f
#define MONO_SIZE 15.0f
void fonts_load(const char *game_font);

/* ---- reading and writing a scene file (json.c) ---- */

/* Reads a scene file as the loader reads it: a byte-order mark skipped, at most 4 MB, no zero byte,
 * cJSON's parse, no field twice in one object, 200 levels deep at most. On failure *why is the
 * loader's own message ("not valid JSON at line 3, column 5"). *raw is the file's bytes (or NULL). */
bool scene_read(const char *path, cJSON **root, char **raw, size_t *raw_len, char *why, size_t n);
bool scene_parse(const char *text, cJSON **root, char *why, size_t n);
/* The editor's one layout for a whole file (malloc'd, ends in a newline). */
char *scene_write(const cJSON *root);
/* One step in the editor's layout, as it sits in a file at indent 0 (malloc'd, no newline at the end). */
char *step_write(const cJSON *step);
/* A value on one line, keys in their own order: { "a": 1 }, ["a", "b"] (malloc'd). */
char *json_inline(const cJSON *v);
/* The same values: objects compared by key (the layout puts keys in its own order). */
bool json_same(const cJSON *a, const cJSON *b);
/* The step kind of an object: the first of its keys that names a step, or NULL. */
const char *step_kind(const cJSON *st);
int utf8_columns(const char *s);

/* ---- the step kinds (steps.c) ---- */

enum tag { TAG_LINE, TAG_PIC, TAG_SND, TAG_FLOW, TAG_DATA, TAG_NOTE, TAG_BAD };

struct kind_info {
	const char *key;        /* "say" */
	const char *tag;        /* "SAY" */
	int group;              /* enum tag: the tag's colour */
	const char *title;      /* "A line" */
	const char *make;       /* a new step of this kind, as JSON */
};

extern const struct kind_info kinds[];
extern const int nkinds;
const struct kind_info *kind_find(const char *key);
int tag_colour(int group);   /* enum col of a tag group */
/* What a step does, in a sentence for the list (malloc'd). */
char *step_summary(const cJSON *st, const cJSON *cast);
/* The lines (say, think, narrate) in a list of steps and the lists under it. */
int count_lines(const cJSON *steps);

/* ---- the mods and their scene files (mods.c) ---- */

struct mod {
	char id[128];
	char name[256];
	char dir[1024];
	bool outside;      /* a folder outside the game's mods, opened by hand */
};

struct scene_file {
	char rel[512];     /* from the mod's folder, with /: scenes/meeting.json */
	char path[1600];
	bool in_scenes;    /* under scenes/ */
	bool read;         /* read as a scene file */
	char why[256];     /* why it could not be read */
	int lines;         /* its lines (say, think, narrate) */
	long long mtime, size;
};

extern struct mod *MODS;
extern int NMODS;
extern struct scene_file *FILES;
extern int NFILES;
void mods_scan(const char *game_dir, const char *outside_dir);
int mod_find(const char *id_or_dir);
bool mod_read(const char *dir, struct mod *m);   /* a folder with a mod.json */
void files_scan(const struct mod *m);            /* again when it changes; keeps what it read */
int file_find(const char *rel);
bool file_stat(const char *path, long long *mtime, long long *size);
bool scene_name_ok(const char *name);            /* letters, digits, - and _ */

/* ---- the open file (doc.c) ---- */

struct snap {
	char *text;        /* the file in the editor's layout */
	char place[256];   /* the step the change was at */
};

struct doc {
	bool open;
	char path[1600];
	char rel[512];
	cJSON *root;       /* NULL when the file cannot be read as a scene */
	char why[512];     /* why it cannot be read, or why it is read-only */
	bool readonly;
	bool styled;       /* the file on disk is in the editor's layout */
	long long mtime, size;
	bool gone;         /* no longer on disk */
	bool changed_outside;   /* changed on disk while there are unsaved changes */
	struct snap *snaps;
	int nsnaps, at;
	int saved;         /* the snapshot on disk, or -1 */
};

extern struct doc D;
#define UNDO_DEPTH 300

bool doc_open(const char *path, const char *rel);
void doc_close(void);
bool doc_save(char *why, size_t n);
bool doc_dirty(void);
bool doc_editable(void);
void doc_changed(const char *place);    /* after a change to D.root: a step to undo */
bool doc_undo(char *place, size_t n);   /* the place to select after it */
bool doc_redo(char *place, size_t n);
bool doc_can_undo(void);
bool doc_can_redo(void);
bool doc_reload(void);
int doc_watch(void);    /* 1: reloaded from disk; 2: changed on disk with unsaved changes */
cJSON *doc_steps(void);

/* A step's place: steps[3], steps[2].then[0], steps[2].choice[1].steps[0]. */
cJSON *place_find(const cJSON *root, const char *place, cJSON **list, int *index);
void place_join(char *out, size_t n, const char *list_place, int index);   /* list_place + [index] */
bool place_list(const char *place, char *list_place, size_t n, int *index);   /* the reverse */

/* ---- the window (ui.c) ---- */

struct ui {
	unsigned frame;
	char sel[256];          /* the selected step's place, or "" */
	bool scroll_to_sel;
	char status[512];
	double status_until;
	bool status_bad;
	bool quit;              /* the window may close */
	bool shot;              /* --shot: nothing written, no settings changed */
};
extern struct ui UI;

/* mod_dir: a mod's folder outside the game's mods (--mod-dir), or NULL for the last one used. */
void ui_init(const char *game_dir, const char *mod_dir, const char *mod, const char *file, const char *select);
void ui_frame(void);
void ui_status(const char *fmt, ...);
void ui_error(const char *fmt, ...);
void ui_title(char *out, size_t n);
bool ui_wants_quit(void);   /* the window's close: asks first when there are unsaved changes */
/* An action as the window's buttons do it, for --shot --do: select=PLACE, add=KIND, apply=JSON,
 * type=JSON, copy, delete, up, down, undo, redo, tab=steps|cast, modal=unsaved|layout|changed|new,
 * fold. False when it is unknown or did nothing it could check. */
bool ui_do(const char *action);

/* ---- Windows (win.c) ---- */

void win_init(void);
bool win_exe_dir(char *out, size_t n);
bool win_pick_folder(const char *title, char *out, size_t n);
void win_message(const char *title, const char *text);
void win_attach_console(void);
bool win_relaunch(const char *game_dir);
bool win_replace(const char *from, const char *to);   /* a file moved over another */
bool win_stat(const char *path, long long *mtime, long long *size);   /* mtime in 100 ns steps */
const char *settings_get(const char *key);
void settings_set(const char *key, const char *value);

/* ---- --check and --format (check.c) ---- */

int run_check(const char *dir, const char *game_dir, const char *mod, const char *out_path);
int run_format(int argc, char **argv, int first);
/* The open file's life on a copy that may be changed: edits, undo and redo, saving, a change on
 * disk with and without unsaved changes, the undo depth. */
int run_selftest(const char *path);

#endif /* EDITOR_H */
