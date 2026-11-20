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

/* Dear ImGui's colour macros, which cimgui's C header leaves out. */
#ifndef IM_COL32
#define IM_COL32(r, g, b, a) (((ImU32)(a) << 24) | ((ImU32)(b) << 16) | ((ImU32)(g) << 8) | (ImU32)(r))
#define IM_COL32_WHITE IM_COL32(255, 255, 255, 255)
#endif

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
char *row_write(const cJSON *row);   /* a choice's row, the same way */
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

/* ---- the game's pictures, music and names (media.c) ---- */

enum { PIC_LOADING = 1, PIC_READY = 2, PIC_FAILED = 3 };

struct picture {
	unsigned tex;
	int w, h;
	int state;      /* PIC_* */
};

enum { SOUND_MUSIC, SOUND_EFFECT };

void media_init(bool thumbs);          /* after game_open; thumbs: start the thumbnail thread */
void media_frame(void);                /* once a frame */
bool media_idle(void);                 /* nothing waits to be read (for --shot) */
void media_set_mod(const char *dir);   /* where a picture path of the scene files starts */
const char *media_mod_dir(void);
/* A picture value of a scene file (a CG number, or a path in the mod's folder), whole, for the
 * stage; PIC_LOADING for a frame or two while it is read. */
const struct picture *media_picture(const cJSON *v);
const struct picture *media_cg(int no);   /* a CG of the game by its number, the same way */
const struct picture *media_mod_picture(const char *path);   /* a picture in the mod's folder, the same way */
struct thumb *media_thumb(int no);     /* a CG's thumbnail, made on Rance Browser's thread */
bool media_cg_exists(int no);
const char *media_face_owner(int face);   /* rq.ids' character for a face CG, or NULL */
const char *media_face_name(int face);    /* the plate's name for it, from the game's table */
bool media_plates_known(void);            /* the game's table was read from its .ain */
const char *media_music_name(int no);     /* rq.ids.music's name, or NULL */
/* A place of the game's world map (1 to 40), as its code names it, and its point. */
#define PLACES_MAX 40
bool media_place(int no, int *x, int *y, const char **name);
bool media_can_play(int kind, int no);
bool media_playing(int kind, int no);
void media_play(int kind, int no);        /* plays, or stops when it is playing */
void media_stop(void);
/* Play from here: a sound beside the music (a jingle, a sound), and the music with a fade in ms
 * (-1: the game's, none). No sound for 0. */
/* Play from here's sounds (mixer.c): several at once, the music as the game's SYS_PlayBGM plays it. */
void mixer_sound(int no);
void mixer_music(int no, int effect, int ms);   /* effect and ms -1: the game's defaults (at once) */
int mixer_music_now(void);
void mixer_stop(int fade_ms);
double mixer_sound_ms(int no);                  /* a sound's length in ms, 0 when unknown */
void mixer_shutdown(void);
int mixer_check(void);                          /* --mixer-check: 0 when every check holds */

/* ---- the stage (stage.c): the game's screen at a step, from the game's own pieces ---- */

enum { L_WALL, L_MASK, L_BG, L_PORTRAIT, L_MINI, L_CG, L_COUNT };

/* The scene as the game shows it at a step: the pictures on each layer, the screen, the line in the
 * message box and, when the step is a choice, its rows. Values point into the file's JSON. */
struct stage {
	const cJSON *layer[L_COUNT];
	int screen;              /* the screen's colour under the pictures: 0, 'b' black, 'w' white, 'c' screen_col */
	ImU32 screen_col;
	int mask_cg;             /* the mask wallpaper a set puts in (the game's 5550), when no step named one */
	const cJSON *line;       /* the say, think or narrate on show */
	bool face_off;           /* the face cleared since the line (a scene CG, a set, black, white, clear) */
	int speed;               /* the text's milliseconds a letter (speed): -1 the player's, 0 at once */
	int music;               /* the track the steps so far put on, 0 none (not drawn) */
	const cJSON *choice;     /* the selected step, when it is a choice */
	int choice_row;          /* its row under the cursor (a row selected), or -1 */
	const cJSON *cast;
	const char *line_name;   /* a name plate given by the caller (the cast tab), or NULL */
	/* What moves while the stage plays (play.c); all 0 while it stands still. In game pixels. */
	struct {
		bool playing;
		float bg_x;                       /* the background's x from its place: a slide */
		float dx, dy;                     /* the screen's shake */
		float face_dx, face_dy, portrait_dx, portrait_dy;
		float text_ms;                    /* since the page began to show: its letters come in */
		bool no_mark;                     /* no click mark (the page does not wait) */
		float white;                      /* white over the screen, 0 to 1 */
	} m;
};

void stage_start(struct stage *s, const cJSON *root);       /* the stage before a file's first step */
void stage_apply(struct stage *s, const cJSON *step);        /* the stage after a step */
void stage_walk(const cJSON *root, const char *place, struct stage *s);
/* Draws the stage at the cursor, w wide (4:3), the line's page `page` (from 0). */
void stage_draw(const struct stage *s, int page, float w);
/* The stage drawn into a draw list, its top left at o, k pixels to a game pixel. */
void stage_paint(ImDrawList *dl, V2 o, float k, const struct stage *s, int page);
/* A texture of the game's screen (fx.c) at the cursor in the stage's place, w wide. */
void stage_draw_texture(unsigned tex, float w);
/* The letters of a page as the game shows them, and how long the page takes to come in. */
int stage_letters(const cJSON *line, const char *page);
float stage_page_ms(const struct stage *s, int page);
/* The choice row (its index in the step's list) the menu last drawn has at a game pixel, or -1. */
int stage_menu_hit(float gx, float gy);
/* The stage before the step at a place (stage_walk without the step itself). */
void stage_walk_before(const cJSON *root, const char *place, struct stage *s);
/* The game's message frame for a line (1 a line, 2 a thought, 3 narration, 4 on paper, 6 to 8 the
 * E frames of a page: true line), or 0; and the lines its box holds (4, or 3 in an E frame). */
int line_frame(const cJSON *line);
int line_limit(const cJSON *line);
const cJSON *line_text(const cJSON *line);            /* text, or narrate's value */
int line_pages(const cJSON *line);                     /* 0 when it is not a line */
const char *line_page(const cJSON *line, int page);   /* NULL past the end */
/* A page of a line as the game lays it out in the line's frame: how many lines it takes. */
int page_lines(const cJSON *line, const char *page);
/* The plate a line shows: its name, else the cast's, else the game's name for the face (*game
 * true), else NULL. */
const char *line_plate(const cJSON *line, const cJSON *cast, bool *game);
/* The face a line shows (a number or a path), from the cast when it names one; NULL for none. */
const cJSON *line_face(const cJSON *line, const cJSON *cast);
/* The box's text at the game's 1024 by 768: the game's size 40, measured on its shots as 32 pixels
 * to the em in its font, which Dear ImGui calls 43.6 (it sizes a font by its whole height), on lines
 * 35 pixels apart (its size 40 and line space -5). Other text of the game's is drawn at its size times BOX_TEXT_SIZE / 40. */
#define BOX_TEXT_SIZE 43.6f
#define BOX_LINE_H 35.0f

/* ---- the game's transitions (fx.c) ---- */

#define FX_W 1024
#define FX_H 768
#define FX_SLOTS 4           /* textures of the game's screen to draw into */
#define EFFECT_LAST 60       /* the engine's effects: 1 to 60 */

bool fx_ok(void);                       /* the textures could be made */
ImDrawList *fx_draw(int slot);          /* a draw list in game pixels for the texture, drawn after the frame */
void fx_mix(int out, int effect, int old, int new, float p);   /* the transition at p, 0 to 1 */
/* The game's スプライトズーム: the screen in `under` covered in the shape CG's top left colour but
 * where the shape, `mag` times its size about the middle, is clear. */
void fx_shape(int out, int under, unsigned shape_tex, int w, int h, float mag);
unsigned fx_texture(int slot);
void fx_render(void);                   /* after igRender: the frame's textures, in order */
void fx_shutdown(void);                 /* before Dear ImGui's context goes */
const char *effect_name(int no);        /* the engine's effect in words, or NULL */
bool effect_written(int no);            /* xsystem4 draws it (else it plays as 51) */
int effect_played(int no);              /* what plays: the number, 51, 27 (a shake) or 0 (at once) */

/* ---- the stage playing (play.c) ---- */

/* Plays the step at a place of the open file from the stage before it (one), or on through the
 * steps after it (on). */
void play_start(const char *place, bool on);
void play_stop(void);
bool play_active(void);
bool play_on(void);                     /* playing on (Play from here) */
/* While playing: draws the stage at the cursor, w wide, takes its clicks, and returns true. */
bool play_draw(float w);
void play_key(void);                    /* Space or Enter: the next page, as a click */
void play_effect_preview(int effect);   /* while an effect is hovered in a list: the step played with it */
#define PREVIEW_DEFAULT (-2)            /* play_effect_preview's "the step's default" (a mode for a shake) */
const char *play_note(void);            /* what the player passed by, or "" */
const char *play_step_place(void);      /* the place of the step playing, or "" */
void play_pick(int row);                /* a row of the menu on show clicked */
void play_answer(bool then);            /* the if asked: then or else */
const char *play_status(void);          /* what the player waits for, and its note */
bool play_preview(void);                /* playing an effect's preview */
const char *play_place(void);           /* the place it began at */
/* The game's shake mode for the screen (part 0), the face (1) or the standing picture (2): the size
 * across and up and down in game pixels, and the time in ms. */
void shake_mode(int part, int mode, float *sx, float *sy, float *ms);
/* The world map's tag (T_WorldMap): the place tag's Flash at a point and the name over it, ms since
 * the map came in. */
void map_tag_paint(ImDrawList *dl, V2 o, float k, int x, int y, const char *name, double ms);
/* A Flash file of the game's (by its name without a folder) at x, y of the 1024 by 768 screen, ms into its
 * loop; false when it is not there. */
bool flash_draw(ImDrawList *dl, V2 o, float k, const char *base, float x, float y, double ms);
void play_fixed_clock(double ms);       /* --shot: the clock held at ms after the start (-1: free) */

/* ---- the selected step's fields (fields.c) ---- */

void fields_frame(void);   /* once a frame: typing that left its field becomes a step to undo */
/* The fields of a step by its kind; they change the step in place. place: its place in the file. */
void fields_step(cJSON *st, const char *place, bool readonly);
extern int fields_page;    /* the page of a line the stage shows (the page being typed in) */
/* A cast entry's fields: its name in the file (renamed in every line), face and name plate. Returns
 * the entry's name after it (another when it was renamed). */
const char *fields_cast(const char *key, bool readonly);
/* The lines that name a cast entry, and the first one's place. */
int cast_uses(const char *key, char *first, size_t n);
/* A picture value (a CG number or a mod picture) in a box, cropped to fill it; top: keep its top. */
void draw_picture_small(ImDrawList *dl, const cJSON *v, V2 a, V2 b, bool top);
/* A choice's row: its text, comment, condition, what it does, colour, sound and id. */
void fields_row(cJSON *rows, int r, const char *place, bool readonly);
/* A label step renamed, the go-tos to it with it (when its name was the only one): how many moved, or
 * -1 and why. */
int fields_label_rename(cJSON *st, const char *to, char *why, size_t n);
/* For the condition builder: a field typed in, a pick (a step to undo), a text field left, a value
 * with its type (a text, a number, true, false, null) in an object (key) or a list (index). */
void field_typed(void);
void field_picked(void);
void field_left(void);
void field_value(const char *id, cJSON *parent, const char *key, int index, float w);

/* ---- conditions (cond.c) ---- */

/* A change to a condition, at a check's path ("" the whole, "1" the second of the group at the top,
 * "1.0" the first of that one's group): add a check to a group (arg: its kind), add a group, take a
 * check away, turn it around (Not), change its kind (arg), a group's mode (arg: all, any, notall,
 * none), or the check as JSON (arg). */
enum { COND_ADD, COND_GROUP, COND_DEL, COND_NOT, COND_KIND, COND_MODE, COND_VALUE, COND_OPS };
bool cond_apply(cJSON *owner, const char *key, bool required, const char *path, int op, const char *arg, char *why, size_t n);
/* --do cond=OP:PATH[:ARG], OP one of add, group, del, not, kind, mode, value. */
bool cond_do(cJSON *owner, const char *key, bool required, const char *spec, char *why, size_t n);
/* The builder for owner[key] at the cursor (required: an if's, which keeps a check). True when it
 * changed the condition. */
bool cond_builder(cJSON *owner, const char *key, bool required, bool readonly);
/* A condition in a few words, for the list: "50% chance", "not gold 100", "flag mymod:met and gold 100". */
void cond_words(const cJSON *c, char *buf, size_t n);

/* ---- the pickers (picker.c) ---- */

/* The picture picker's tabs. */
enum { PICK_FACES, PICK_STANDING, PICK_BACKGROUNDS, PICK_SMALL, PICK_SCENE, PICK_OTHER, PICK_MOD, PICK_TABS };

/* Where a pick goes: a value of a step (or of its set), or a cast entry's face. */
struct pick_target {
	char place[256];   /* the step's place; "" for a cast entry */
	char cast[128];    /* the cast entry, when place is "" */
	char sub[16];      /* "set": a layer of a set */
	char key[32];      /* the value's key */
	int tab;           /* PICK_*: the tab it opens on when the value says nothing */
	bool speaker;      /* a line's speaker: the cast's names are offered */
	int sound;         /* -1: the picture picker; SOUND_MUSIC or SOUND_EFFECT: the sound picker */
};

void picker_open(const struct pick_target *t);   /* opens at the end of the frame */
void picker_frame(void);                         /* in the root window, after the rest */
bool picker_is_open(void);
/* --do: find=TEXT, ptab=NAME, group=N, tile=NO|PATH, showscene, use, addcast[=NAME]. */
bool picker_do(const char *name, const char *arg);
/* --do picker[=KEY]: the next field drawn with a Pick button (of that key) opens its picker. */
extern bool picker_requested;
extern char picker_requested_key[32];

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
	bool live;         /* a field changed D.root as it was typed: not yet a step to undo */
	char live_place[256];
};

extern struct doc D;
#define UNDO_DEPTH 300

bool doc_open(const char *path, const char *rel);
void doc_close(void);
bool doc_save(char *why, size_t n);
bool doc_dirty(void);
bool doc_editable(void);
void doc_changed(const char *place);    /* after a change to D.root: a step to undo */
void doc_settle(void);                  /* a field's typing (D.live) made a step to undo */
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
/* A choice's row by its place (steps[2].choice[1]): the row, the step's list of rows and its index. */
cJSON *row_find(const cJSON *root, const char *place, cJSON **rows, int *index);
bool place_exists(const cJSON *root, const char *place);   /* a step's or a row's */
bool row_step_place(const char *place, char *out, size_t n);   /* steps[2] for steps[2].choice[1] */
/* A row as an object to write a field into: a row that is only a text becomes { "text": ... } when
 * make, else NULL (a separator, or nothing there). */
cJSON *row_object(cJSON *root, const char *place, bool make);
bool place_list(const char *place, char *list_place, size_t n, int *index);   /* the reverse */
/* Every step of a list and of the lists under it, in order, with its place. */
void steps_each(cJSON *list, const char *lp, void (*fn)(cJSON *st, const char *place, void *ud), void *ud);

/* ---- problems (problems.c) ---- */

/* PROB_ERROR: RQ MOD's checker refuses the file (it does not play); PROB_WARN: the editor's, from the
 * game's box (the game plays it, not as it was meant to look). */
enum { PROB_ERROR, PROB_WARN };

struct problem {
	int level;
	char place[256];   /* where the editor shows it: a step's or a row's place, cast.NAME, or "" */
	char at[256];      /* the place as the check names it (steps[2].choice[0].if.gold), or "" */
	char msg[600];     /* what is wrong */
};

struct problems {
	struct problem *p;
	int n, cap;
	int errors, warnings;
};

/* The box's rules (lines and letters) need the game's font measured: the window sets it. */
extern bool problems_box;
/* RQ MOD's checks and the editor's on a file's JSON, into *ps (emptied first). mod_dir: where a
 * picture's file is looked for, or NULL. */
void problems_check(const cJSON *root, const char *mod_dir, struct problems *ps);
void problems_free(struct problems *ps);
/* The open file's, checked again when it changes (in a frame). */
const struct problems *problems_now(void);
void problems_forget(void);
/* The open file's problems at a place (a step's or a row's): how many, and the first. */
int problems_at(const char *place, const struct problem **first);
/* The place the editor shows for a checker's place: steps[2].choice[0].if.gold -> steps[2].choice[0]. */
void problem_place(const char *at, char *out, size_t n);
/* A file of the list, from disk, counted once each time it changes. */
void problems_count_file(const char *path, const char *mod_dir, long long mtime, long long size, int *errors, int *warnings);
/* A game name as the loader finds it: its number; 0 and why when there is none; -1 when it cannot
 * be known here (no names, or a mod's own id). */
int problems_find_id(const char *kind, const char *name, char *why, size_t n);
const char *problems_lua_why(void);   /* why RQ MOD's checker did not load, or NULL */
unsigned long long problems_doc_hash(void);   /* the open file's JSON, as the problems see it */

/* ---- the game's log (gamelog.c) ---- */

void gamelog_set_game(const char *game_dir);   /* its logs/rqmod.log */
void gamelog_set_file(const char *path);       /* another file (--do logfile=PATH) */
void gamelog_poll(double now, bool force);     /* reads what the game added (twice a second) */
const char *gamelog_path(void);
bool gamelog_exists(void);
int gamelog_count(void);
const char *gamelog_line(int i);
unsigned gamelog_gen(void);                    /* changes when lines come or the log starts again */
bool gamelog_for(const char *line, const char *mod);   /* a line of that mod */

/* ---- the game's pipe (pipe.c): RQ MOD's \\.\pipe\rqmod, on a thread of its own ---- */

enum { PIPE_ANSWER, PIPE_NO_GAME, PIPE_NO_ANSWER };

struct pipe_answer {
	int id, tag;
	int how;       /* PIPE_*: an answer, no game has the pipe, or it stayed busy or broke */
	char *text;    /* the answer without its last line end (malloc'd), or NULL */
	unsigned long pid;   /* the game's process, 0 when unknown */
};

/* A command queued (a line of RQ MOD's words): its id, or 0 when it is too long or the queue is
 * full. Its answer comes back from pipe_take, in order. */
int pipe_send(int tag, const char *line);
bool pipe_take(struct pipe_answer *out);
int pipe_waiting(void);
size_t pipe_line_max(void);                 /* the longest line the game reads */
void pipe_set_name(const char *name);       /* another pipe than rqmod (--do pipe=NAME) */

/* ---- the game running (game.c): Play, Stop, the values for Play ---- */

enum { GAME_LOOKING, GAME_OFF, GAME_SILENT, GAME_OLD, GAME_NO_MOD, GAME_OTHER_DIR, GAME_READY };

/* Once a frame: the open mod (its id and folder) and file; asks the game a little at a time. */
void game_frame(double now, const char *mod, const char *mod_dir, const char *rel);
void game_enable(bool on);                  /* --shot: off until an action asks for the game */
bool game_enabled(void);
int game_state(void);                       /* GAME_* */
const char *game_version(void);             /* RQ MOD's, "" until known */
const char *game_mod_dir(void);             /* where the game runs the open mod from */
const char *game_silent_why(void);
bool game_paused(void);                     /* rqmod-ctl pause */
int game_scene(void);                       /* 0 none, 1 a scene waits to start, 2 one plays */
bool game_stopping(void);
/* The step the game plays in a file (the place), or NULL. */
const char *game_place_for(const char *mod, const char *rel);
bool game_waiting_for(const char *mod, const char *rel);
bool game_playing_now(const char **mod, const char **file, const char **place);
/* Play the open file (saved): from a place or label (NULL: the top), with the values for Play;
 * hash is the file's JSON as it is now, for what the game may refuse. */
bool game_play(const char *from, unsigned long long hash, char *why, size_t n);
/* Whether the values for Play can be sent to this game (a text that reads as a number needs RQ MOD
 * 2.62.1, the line has a length): "" or why not. */
const char *game_values_why(char *why, size_t n);
void game_stop(void);
/* The open mod's functions (rq.scene.functions), known while the game runs it from this folder. */
bool game_functions_known(void);
int game_function_count(void);
const char *game_function(int i);
bool game_has_function(const char *name);
unsigned game_gen(void);                    /* changes with the functions and the game's refusals */
/* The game's refusal of a file Play sent, for the file as it was: how many lines, and line i. */
int game_refusal(const char *mod, const char *rel, unsigned long long hash, const char **line, int i);
/* The pill in the top bar: its text, its colour (enum col) and its tip. */
void game_pill(char *out, size_t n, int *colour, char *tip, size_t tn);

/* The values Play gives a file's {name}s (kept in the settings per mod and file): a text, a number,
 * true or false, sent as RQ MOD's playscene types them (a text that would read as a number, true or
 * false goes in single quotes). */
enum { VAL_NONE, VAL_TEXT, VAL_NUMBER, VAL_TRUE, VAL_FALSE };
struct value_name {
	char name[64];
	char set_by[256];   /* the first choice or call whose "into" names it, or "" */
	char first[256];    /* the first place that uses it */
	int uses;           /* in lines, name plates, rows and conditions */
	bool checked;       /* a condition checks it ("var") */
};
int values_names(const cJSON *root, struct value_name *out, int max);   /* root NULL: the open file */
/* The value as the game shows it in a line (a number as its fill shows it), or NULL for none. */
const char *values_get(const char *name);  /* the open file's */
const char *values_get_for(const char *mod, const char *rel, const char *name);
int values_type(const char *name);         /* VAL_*: the open file's */
void values_set(const char *name, const char *text);   /* a text; "" or NULL takes it away */
/* A value of a type (VAL_NONE takes it away); a number from its text, false when it is not one. */
bool values_set_typed(const char *name, int type, const char *text);
/* What playscene gets for it: name=5, name='5', name=true (into out), false when there is none. */
bool values_word(const char *name, char *out, size_t n, bool *quoted);
unsigned values_gen(void);
/* A text as the game shows it with the values for Play: {name} filled, {{ and }} as braces, a name
 * with no value as written (a static buffer, or s itself). */
const char *values_fill(const char *s);

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
	bool do_again;          /* --do: the action waits, and is done again next frame */
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
 * fold, page=N (a line's page on the stage), json (the step's JSON open), cast=NAME (picked in Cast
 * and file), castadd, move=FROM|LIST|INDEX (a step dropped into a list before its step INDEX),
 * printstep (the selected step and the cast as JSON on stdout), picker[=KEY] and the picker's own
 * (picker_do). False when it is unknown or did nothing it could check. */
bool ui_do(const char *action);
int ui_label_count(void);           /* the open file's labels */
const char *ui_label_name(int i);
const char *ui_label_place(int i);
void ui_select(const char *place);  /* a step or a choice's row selected, the list brought to it */
const char *ui_mod_id(void);        /* the open mod's id, or "mod" */

/* ---- Windows (win.c) ---- */

void win_init(void);
bool win_exe_dir(char *out, size_t n);
bool win_pick_folder(const char *title, char *out, size_t n);
void win_message(const char *title, const char *text);
void win_attach_console(void);
bool win_relaunch(const char *game_dir);
bool win_replace(const char *from, const char *to);   /* a file moved over another */
bool win_stat(const char *path, long long *mtime, long long *size);   /* mtime in 100 ns steps */
/* A folder as Windows writes it, for the window: back slashes, none at the end but a drive's. */
const char *path_shown(const char *path, char *out, size_t n);
const char *settings_get(const char *key);
void settings_set(const char *key, const char *value);
cJSON *settings_tree(void);                 /* the settings as a whole, or NULL */
void settings_save(void);

/* ---- --check and --format (check.c) ---- */

int run_check(const char *dir, const char *game_dir, const char *mod, const char *out_path);
int run_format(int argc, char **argv, int first);
/* The open file's life on a copy that may be changed: edits, undo and redo, saving, a change on
 * disk with and without unsaved changes, the undo depth. */
int run_selftest(const char *path);

#endif /* EDITOR_H */
