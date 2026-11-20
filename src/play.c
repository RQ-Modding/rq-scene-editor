/* The stage playing: a scene file's steps on the editor's stage as the game plays them, without the
 * game.
 *
 * ▶ plays the selected step from the stage before it; Play from here goes on through the steps after
 * it. The player works on a copy of the file taken when it starts (an edit meanwhile does not move
 * it). Each step becomes beats, one after another: a transition between the screen before and the
 * screen after (fx.c), a background's slide, the shrinking shape of a set's effect, a flash, a page
 * whose letters come in, a wait for a click, the choice menu, the world map. A shake runs beside
 * them: the game starts a scene's shakes when the next page begins to show.
 *
 * What the game does, read in its code: EFFECT (画面更新.jaf), 背景イン and 背景アウト, セット表示
 * and エフェクトセット表示 with Ｒ８画面エフェクト and スプライトズーム, 立ち絵表示 (a standing
 * picture taken away in 200 ms before the next), 全画面揺れ, 顔揺れ, 立ち絵揺れ, 画面フラッシュ,
 * 地図点滅 and T_WorldMap; the letters' speed and fade and the click mark from the message box. What
 * the editor cannot know (a condition on the save, a mod's function, a command of the game) it asks,
 * or passes by with a note. */
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "system4.h"

#include "editor.h"

enum {
	B_WAIT,     /* the stage as it is, for ms (a shake's time; 0: at once) */
	B_TRANS,    /* a transition from `from` to `to` */
	B_SLIDE,    /* the background `bg` from x0 to x1 */
	B_SHAPE,    /* set's 7001 and 7002: the shape shrinking over black, then a crossfade to `to` */
	B_FLASH,    /* white, fading to `to` */
	B_TEXT,     /* a page of the line coming in, then a click */
	B_CLICK,    /* a click (wait_click) */
	B_CHOICE,   /* the menu, until a row is clicked */
	B_ASK,      /* an if: then or else, asked */
	B_MAP,      /* the world map: in, a click, black */
	B_END,      /* the scene ended: a click to leave */
};

struct beat {
	int kind;
	struct stage from, to;
	int from_page, to_page;
	int effect;
	float ms;
	const cJSON *bg;
	float x0, x1;          /* a slide: SLIDE_IN_X for minus the background's width, SLIDE_OUT_X for 1024 plus it */
	int jingle;            /* a sound that starts with the beat */
	const cJSON *step;     /* the choice, the if, the world map */
	bool wait;             /* B_TEXT: the page waits for a click */
	int shape;             /* B_SHAPE: the CG */
	/* while it plays */
	bool started;
	double t0;
	bool clicked;
	double click_t;
};

#define SLIDE_IN_X (-1e9f)
#define SLIDE_OUT_X (1e9f)
#define MAX_BEATS 8
#define MAX_DEPTH 64

static struct {
	bool active, on, preview;
	int preview_effect;
	unsigned preview_seen;
	double restart_at;
	cJSON *root;               /* the file as it was when the play began */
	char place[256];           /* where it began */
	const cJSON *step;         /* the step playing (in root) */
	struct stage cur;
	int page;
	const cJSON *stack[MAX_DEPTH];   /* the next step of each list, outer first; NULL at a list's end */
	int depth;
	bool one_left;             /* ▶: the selected step only */
	struct beat beats[MAX_BEATS];
	int nbeats, at;
	double t_next;             /* when the next beat starts */
	double start;              /* the clock's zero */
	double fixed;              /* --shot: the clock held here (-1: free) */
	bool click;                /* a click waiting for the beat */
	int pick;                  /* a choice row clicked, or -1 */
	int ask;                   /* an if answered: 1 then, 0 else, -1 none */
	char note[300];
	char status[300];
} P = { .fixed = -1, .pick = -1, .ask = -1 };

struct shake {
	bool on;
	double t0;
	float ms, sx, sy;
};

static struct shake shakes[3];   /* the screen, the face, the standing picture */
static int pending[8][2];        /* shakes the next page starts: the part and the mode */
static int npending;

static const cJSON *get(const cJSON *o, const char *k)
{
	return cJSON_IsObject(o) ? cJSON_GetObjectItemCaseSensitive(o, k) : NULL;
}

static int num(const cJSON *o, const char *k, int def)
{
	const cJSON *v = get(o, k);
	return cJSON_IsNumber(v) ? (int)v->valuedouble : def;
}

static double clock_ms(void)
{
	return P.fixed >= 0 ? P.fixed : igGetTime() * 1000.0 - P.start;
}

static void note(const char *fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	vsnprintf(P.note, sizeof P.note, fmt, ap);
	va_end(ap);
}

/* ---- sounds ---- */

static void sound_jingle(int no)
{
	/* None in a shot, and none in a hovered row's preview, which plays over and over. */
	if (no > 0 && !UI.shot && !P.preview && media_can_play(SOUND_EFFECT, no))
		mixer_sound(no);
}

/* ---- shakes ---- */

/* The game's shake modes (全画面揺れ; 顔揺れ and 立ち絵揺れ at 16, 32 and 64): the size across,
 * the size up and down, the time. */
void shake_mode(int part, int mode, float *sx, float *sy, float *ms)
{
	static const float screen[4][3] = { { 20, 0, 200 }, { 0, 20, 100 }, { 50, 50, 300 }, { 100, 100, 800 } };
	static const float small[4][3] = { { 16, 0, 200 }, { 0, 16, 100 }, { 32, 32, 300 }, { 64, 64, 800 } };
	int m = mode >= 1 && mode <= 3 ? mode : 0;
	const float *v = part == 0 ? screen[m] : small[m];
	*sx = v[0];
	*sy = v[1];
	*ms = v[2];
}

static void shake_start(int part, float sx, float sy, float ms, double t0)
{
	shakes[part] = (struct shake){ true, t0, ms, sx, sy };
}

static void shake_mode_start(int part, int mode, double t0)
{
	float sx, sy, ms;
	shake_mode(part, mode, &sx, &sy, &ms);
	shake_start(part, sx, sy, ms, t0);
}

/* A random offset within half the size each way, the size falling to nothing over the time. */
static void shake_at(int part, double now, float *dx, float *dy)
{
	struct shake *s = &shakes[part];
	*dx = *dy = 0;
	if (!s->on)
		return;
	float p = s->ms > 0 ? (float)((now - s->t0) / s->ms) : 1;
	if (p >= 1 || p < 0) {
		if (p >= 1)
			s->on = false;
		return;
	}
	float rx = s->sx * (1 - p), ry = s->sy * (1 - p);
	if (rx >= 1)
		*dx = (float)(rand() % (int)rx) - rx / 2;
	if (ry >= 1)
		*dy = (float)(rand() % (int)ry) - ry / 2;
}

/* ---- the steps' place ---- */

static bool take(const char **p, const char *word, int *num_out)
{
	size_t n = strlen(word);
	if (strncmp(*p, word, n))
		return false;
	const char *q = *p + n;
	if (*q < '0' || *q > '9')
		return false;
	long v = 0;
	while (*q >= '0' && *q <= '9' && v < 1000000)
		v = v * 10 + (*q++ - '0');
	if (*q != ']')
		return false;
	*num_out = (int)v;
	*p = q + 1;
	return true;
}

/* The lists to play on from a step's place: each outer list at the step after the one that holds the
 * next list, the innermost at the step itself (as the loader's runner does). */
static bool stack_at(const char *place)
{
	P.depth = 0;
	const cJSON *list = get(P.root, "steps");
	const char *p = place;
	int k;
	if (!cJSON_IsArray(list) || !take(&p, "steps[", &k))
		return false;
	for (;;) {
		const cJSON *st = cJSON_GetArrayItem(list, k);
		if (!st || P.depth >= MAX_DEPTH)
			return false;
		if (!*p) {
			P.stack[P.depth++] = st;
			return true;
		}
		P.stack[P.depth++] = st->next;
		int r;
		const cJSON *next = NULL;
		if (take(&p, ".then[", &k))
			next = get(st, "then");
		else if (take(&p, ".else[", &k))
			next = get(st, "else");
		else if (take(&p, ".choice[", &r) && take(&p, ".steps[", &k)) {
			const cJSON *rows = get(st, "choice");
			next = get(cJSON_IsArray(rows) ? cJSON_GetArrayItem(rows, r) : NULL, "steps");
		}
		if (!cJSON_IsArray(next))
			return false;
		list = next;
	}
}

struct label_find { const char *name; char place[256]; };

static void label_each(cJSON *st, const char *place, void *ud)
{
	struct label_find *f = ud;
	const cJSON *l = get(st, "label");
	if (!f->place[0] && cJSON_IsString(l) && !strcmp(l->valuestring, f->name))
		snprintf(f->place, sizeof f->place, "%s", place);
}

static bool go_to(const char *label)
{
	struct label_find f = { label, "" };
	cJSON *steps = cJSON_GetObjectItemCaseSensitive(P.root, "steps");
	if (cJSON_IsArray(steps))
		steps_each(steps, "steps", label_each, &f);
	if (!f.place[0] || !stack_at(f.place)) {
		note("goto \"%s\": no such label", label);
		return false;
	}
	return true;
}

struct step_find { const cJSON *st; char place[256]; };

static void step_each(cJSON *st, const char *place, void *ud)
{
	struct step_find *f = ud;
	if (st == f->st)
		snprintf(f->place, sizeof f->place, "%s", place);
}

const char *play_step_place(void)
{
	static struct step_find f;
	const cJSON *st = P.active ? P.step : NULL;
	if (st == f.st)
		return f.place;   /* found already */
	f.st = st;
	f.place[0] = 0;
	cJSON *steps = P.root ? cJSON_GetObjectItemCaseSensitive(P.root, "steps") : NULL;
	if (f.st && cJSON_IsArray(steps))
		steps_each(steps, "steps", step_each, &f);
	return f.place;
}

void play_pick(int row)
{
	if (P.active)
		P.pick = row;
}

void play_answer(bool then)
{
	if (P.active)
		P.ask = then ? 1 : 0;
}

static void push_list(const cJSON *list)
{
	if (cJSON_IsArray(list) && list->child && P.depth < MAX_DEPTH)
		P.stack[P.depth++] = list->child;
}

/* ---- beats ---- */

static struct beat *beat(int kind, const struct stage *to)
{
	if (P.nbeats >= MAX_BEATS)
		return NULL;
	struct beat *b = &P.beats[P.nbeats++];
	memset(b, 0, sizeof *b);
	b->kind = kind;
	b->from = P.nbeats > 1 ? P.beats[P.nbeats - 2].to : P.cur;
	b->from_page = P.nbeats > 1 ? P.beats[P.nbeats - 2].to_page : P.page;
	b->to = *to;
	b->to_page = b->from_page;
	return b;
}

/* The stage after the beats pushed so far. */
static struct stage last(void)
{
	return P.nbeats ? P.beats[P.nbeats - 1].to : P.cur;
}


/* EFFECT(no, time): at once for 0 or no time, a shake of 128 by 128 for 27, else the transition. */
static void effect_to(const struct stage *to, int no, int ms)
{
	if (P.preview && P.preview_effect > 0)
		no = P.preview_effect;
	else if (P.preview && P.preview_effect == PREVIEW_DEFAULT)
		no = 1;
	int e = effect_played(no);
	if (e == 0 || ms <= 0) {
		beat(B_WAIT, to);
		return;
	}
	struct beat *b = beat(e == 27 ? B_WAIT : B_TRANS, to);
	if (!b)
		return;
	b->effect = no;
	b->ms = (float)ms;
	if (e == 27)
		b->shape = -27;   /* the shake starts with the beat */
}

static int pic_no(const cJSON *v)
{
	return cJSON_IsNumber(v) ? (int)v->valuedouble : cJSON_IsString(v) && v->valuestring[0] ? 1 : 0;
}

static void step_picture(const cJSON *st, const char *kind)
{
	int def_ms = !strcmp(kind, "portrait") || !strcmp(kind, "mini") ? 200 : 500;
	int e = num(st, "effect", 1), ms = num(st, "time", def_ms);
	if (e < 0)
		e = 1;
	if (ms < 0)
		ms = def_ms;
	struct stage to = last();
	if (!strcmp(kind, "portrait") && to.layer[L_PORTRAIT] && pic_no(get(st, kind)) > 0) {
		/* 立ち絵表示: the standing picture there goes first, in a crossfade of 200 ms. */
		to.layer[L_PORTRAIT] = NULL;
		effect_to(&to, 1, 200);
	}
	stage_apply(&to, st);
	effect_to(&to, e, ms);
}

static void step_set(const cJSON *st)
{
	const cJSON *set = get(st, "set");
	const cJSON *e = get(st, "effect");
	struct stage to = last();
	stage_apply(&to, st);
	if (P.preview && P.preview_effect == PREVIEW_DEFAULT)
		e = NULL;   /* the default hovered */
	if (!e && !get(set, "cg") && !(P.preview && P.preview_effect > 0)) {
		/* セット表示: a crossfade of 500 ms. */
		struct beat *b = beat(B_TRANS, &to);
		if (b) {
			b->effect = 1;
			b->ms = 500;
		}
		return;
	}
	int no = P.preview && P.preview_effect > 0 ? P.preview_effect : cJSON_IsNumber(e) ? (int)e->valuedouble : 1;
	/* エフェクトセット表示: the screen cleared at once and the box taken away, then jingle 408. */
	struct stage black = last();
	for (int i = 0; i < L_COUNT; i++)
		black.layer[i] = NULL;
	black.line = NULL;
	black.mask_cg = 0;
	black.screen = 0;
	black.face_off = true;
	beat(B_WAIT, &black);
	if (no == 1) {
		/* Effect 1: the waves (60) for a second. */
		struct beat *b = beat(B_TRANS, &to);
		if (b) {
			b->effect = 60;
			b->ms = 1000;
			b->jingle = 408;
		}
		return;
	}
	/* 7001, and 7002 for any other number: the shape closing for 500 ms, then a crossfade of 500. */
	struct beat *b = beat(B_SHAPE, &to);
	if (b) {
		b->shape = no == 7001 ? 7001 : 7002;
		b->ms = 1000;
		b->jingle = 408;
	}
}

static void step_slide(const cJSON *st, const char *kind)
{
	int ms = num(st, "time", 500);
	if (ms < 0)
		ms = 500;
	struct stage to = last();
	/* 背景アウト: the background there goes out to the right first (500 ms for 背景イン). */
	if (to.layer[L_BG] && pic_no(to.layer[L_BG]) > 0) {
		const cJSON *old = to.layer[L_BG];
		to.layer[L_BG] = NULL;
		struct beat *b = beat(B_SLIDE, &to);
		if (b) {
			b->bg = old;
			b->x0 = 0;
			b->x1 = SLIDE_OUT_X;
			b->ms = !strcmp(kind, "background_in") ? 500.0f : (float)ms;
		}
	}
	if (!strcmp(kind, "background_out"))
		return;
	const cJSON *v = get(st, kind);
	if (pic_no(v) <= 0)
		return;
	stage_apply(&to, st);
	struct beat *b = beat(B_SLIDE, &to);
	if (b) {
		b->bg = v;
		b->x0 = SLIDE_IN_X;
		b->x1 = 0;
		b->ms = (float)ms;
		b->jingle = 406;
	}
}

static void step_line(const cJSON *st)
{
	struct stage to = last();
	stage_apply(&to, st);
	int pages = line_pages(st);
	bool wait = !cJSON_IsFalse(get(st, "wait"));
	for (int i = 0; i < pages; i++) {
		struct beat *b = beat(B_TEXT, &to);
		if (!b)
			break;
		b->to_page = i;
		b->wait = i < pages - 1 || wait;
	}
}

static void step_shake(const cJSON *st, const char *kind)
{
	int part = !strcmp(kind, "shake") ? 0 : !strcmp(kind, "shake_face") ? 1 : 2;
	int mode = num(st, "mode", 1);
	if (P.preview && P.preview_effect > 0)
		mode = P.preview_effect;
	else if (P.preview && P.preview_effect == PREVIEW_DEFAULT)
		mode = 1;
	if (P.on) {
		/* The game starts it as the next page begins to show. */
		if (npending < 8) {
			pending[npending][0] = part;
			pending[npending][1] = mode;
			npending++;
		}
		return;
	}
	/* ▶ shows it at once. */
	float sx, sy, ms;
	shake_mode(part, mode, &sx, &sy, &ms);
	struct stage to = last();
	struct beat *b = beat(B_WAIT, &to);
	if (b) {
		b->ms = ms;
		b->shape = -1 - part;   /* the shake starts with the beat */
		b->x0 = (float)mode;
	}
}

/* Plays a step: its beats, or a change at once, or the lists it goes into. */
static void play_step(const cJSON *st)
{
	P.step = st;
	const char *kind = step_kind(st);
	snprintf(P.status, sizeof P.status, "%s", kind ? kind : "?");
	if (!kind) {
		note("a step the editor does not know: passed by");
		return;
	}
	if (!strcmp(kind, "say") || !strcmp(kind, "think") || !strcmp(kind, "narrate")) {
		step_line(st);
	} else if (!strcmp(kind, "background") || !strcmp(kind, "wallpaper") || !strcmp(kind, "mask_wallpaper")
	           || !strcmp(kind, "portrait") || !strcmp(kind, "mini") || !strcmp(kind, "cg")
	           || !strcmp(kind, "black") || !strcmp(kind, "white")) {
		step_picture(st, kind);
	} else if (!strcmp(kind, "background_in") || !strcmp(kind, "background_out")) {
		step_slide(st, kind);
	} else if (!strcmp(kind, "set")) {
		step_set(st);
	} else if (!strcmp(kind, "flash")) {
		int ms = num(st, "time", 200);
		struct stage to = last();
		struct beat *b = beat(B_FLASH, &to);
		if (b)
			b->ms = (float)(ms > 0 ? ms : 200);
	} else if (!strncmp(kind, "shake", 5)) {
		step_shake(st, kind);
	} else if (!strcmp(kind, "wait_click")) {
		struct stage to = last();
		beat(B_CLICK, &to);
	} else if (!strcmp(kind, "choice")) {
		struct stage to = last();
		to.choice = st;
		to.choice_row = -1;
		struct beat *b = beat(B_CHOICE, &to);
		if (b)
			b->step = st;
	} else if (!strcmp(kind, "if")) {
		if (!P.on) {
			note("if: Play from here asks which way it goes");
			return;
		}
		struct stage to = last();
		struct beat *b = beat(B_ASK, &to);
		if (b)
			b->step = st;
	} else if (!strcmp(kind, "world_map")) {
		struct stage to = last();
		stage_apply(&to, st);
		struct beat *b = beat(B_MAP, &to);
		if (b) {
			b->step = st;
			b->jingle = 407;
		}
	} else if (!strcmp(kind, "music")) {
		const cJSON *v = get(st, "music");
		int no = cJSON_IsNumber(v) ? (int)v->valuedouble : 0;
		/* ▶ leaves it to the field's own Play. */
		if (!UI.shot && P.on)
			mixer_music(no, num(st, "effect", -1), num(st, "time", -1));
	} else if (!strcmp(kind, "sound") || !strcmp(kind, "jingle")) {
		const cJSON *v = get(st, kind);
		int no = cJSON_IsNumber(v) ? (int)v->valuedouble : 0;
		sound_jingle(no);
		if (!strcmp(kind, "jingle") && cJSON_IsTrue(get(st, "wait"))) {
			struct stage to = last();
			struct beat *b = beat(B_WAIT, &to);
			if (b)
				b->ms = (float)mixer_sound_ms(no);
		}
	} else if (!strcmp(kind, "goto")) {
		const cJSON *v = get(st, "goto");
		if (P.on && cJSON_IsString(v))
			go_to(v->valuestring);
		else if (!P.on)
			note("goto: Play from here follows it");
	} else if (!strcmp(kind, "end")) {
		if (P.on) {
			struct stage to = last();
			beat(B_END, &to);
			P.depth = 0;
		}
	} else if (!strcmp(kind, "call")) {
		const cJSON *v = get(st, "call");
		note("call \"%s\": a mod's function runs only in the game", cJSON_IsString(v) ? v->valuestring : "?");
	} else if (!strcmp(kind, "cmd")) {
		const cJSON *v = get(st, "cmd");
		note("cmd \"%s\": the game's command runs only in the game", cJSON_IsString(v) ? v->valuestring : "?");
	} else if (!strcmp(kind, "flag") || !strcmp(kind, "unflag")) {
		note("%s: the save is the game's", kind);
	} else {
		/* clear, clear_text, speed, label, note: at once. */
		stage_apply(&P.cur, st);
	}
}

/* The next step's beats; false at the end. */
static bool next_step(void)
{
	while (P.depth > 0) {
		const cJSON *st = P.stack[P.depth - 1];
		if (!st) {
			P.depth--;
			continue;
		}
		P.stack[P.depth - 1] = st->next;
		if (P.one_left) {
			P.depth = 0;
			P.one_left = false;
		}
		P.nbeats = P.at = 0;
		play_step(st);
		if (P.nbeats)
			return true;
	}
	return false;
}

/* ---- starting and stopping ---- */

void play_stop(void)
{
	if (P.active && P.on && !UI.shot)
		mixer_stop(300);
	P.active = P.on = P.preview = false;
	if (P.root)
		cJSON_Delete(P.root);
	P.root = NULL;
	memset(shakes, 0, sizeof shakes);
	npending = 0;
}

static void begin(const char *place, bool on)
{
	if (!D.root || !place || !place[0])
		return;
	cJSON *copy = cJSON_Duplicate(D.root, true);
	play_stop();
	P.root = copy;
	snprintf(P.place, sizeof P.place, "%s", place);
	P.on = on;
	P.one_left = !on;
	P.note[0] = P.status[0] = 0;
	P.click = false;
	P.pick = P.ask = -1;
	P.nbeats = P.at = 0;
	P.step = NULL;
	stage_walk_before(P.root, place, &P.cur);
	/* The line before shows its last page. */
	P.page = P.cur.line ? line_pages(P.cur.line) - 1 : 0;
	if (P.page < 0)
		P.page = 0;
	if (!stack_at(place)) {
		play_stop();
		return;
	}
	P.start = igGetTime() * 1000.0;
	P.t_next = 0;
	P.active = true;
	/* The track the steps before put on, as the game would be playing it. */
	if (on && !UI.shot && P.cur.music > 0)
		mixer_music(P.cur.music, 0, 0);
}

void play_start(const char *place, bool on)
{
	begin(place, on);
	P.preview = false;
}

bool play_active(void)
{
	return P.active;
}

bool play_on(void)
{
	return P.active && P.on;
}

const char *play_note(void)
{
	return P.note;
}

void play_fixed_clock(double ms)
{
	P.fixed = ms;
}

void play_key(void)
{
	if (P.active)
		P.click = true;
}

void play_effect_preview(int effect)
{
	P.preview_seen = UI.frame;
	if (P.active && P.preview && P.preview_effect == effect)
		return;
	if (P.active && !P.preview)
		return;   /* the player's own play goes on */
	begin(UI.sel, false);
	if (!P.active)
		return;
	P.preview = true;
	P.preview_effect = effect;
	P.restart_at = -1;
}

/* ---- the clock ---- */

static bool timed(int kind)
{
	return kind == B_WAIT || kind == B_TRANS || kind == B_SLIDE || kind == B_SHAPE || kind == B_FLASH;
}

static void begin_beat(struct beat *b, double t)
{
	b->started = true;
	b->t0 = t;
	if (b->jingle)
		sound_jingle(b->jingle);
	if (b->kind == B_WAIT && b->shape == -27)
		shake_start(0, 128, 128, b->ms, t);
	else if (b->kind == B_WAIT && b->shape <= -1 && b->shape >= -3)
		shake_mode_start(-1 - b->shape, (int)b->x0, t);
	if (b->kind == B_TEXT) {
		for (int i = 0; i < npending; i++)
			shake_mode_start(pending[i][0], pending[i][1], t);
		npending = 0;
	}
}

/* When the beat is over, or a negative number while it waits. */
static double beat_end(struct beat *b, double now)
{
	if (timed(b->kind)) {
		if (b->clicked && P.on)
			return b->click_t;
		return b->t0 + b->ms;
	}
	if (b->kind == B_TEXT) {
		struct stage s = b->to;
		double whole = b->t0 + stage_page_ms(&s, b->to_page);
		if (!b->wait)
			return whole;
		return b->clicked && b->click_t >= whole ? b->click_t : -1;
	}
	if (b->kind == B_MAP)
		return b->clicked ? b->click_t + 100 : -1;
	if (b->kind == B_CLICK || b->kind == B_END)
		return b->clicked ? b->click_t : -1;
	return -1;   /* a choice or an if: their own answer */
}

static void finish(void)
{
	if (P.preview) {
		/* The preview plays again after a breath. */
		if (P.restart_at < 0)
			P.restart_at = clock_ms() + 600;
		return;
	}
	if (P.on && P.nbeats && P.beats[P.nbeats - 1].kind == B_END)
		return;
	if (P.on) {
		struct stage to = P.cur;
		P.nbeats = P.at = 0;
		beat(B_END, &to);
		return;
	}
	P.active = false;
	cJSON_Delete(P.root);
	P.root = NULL;
}

static void update(void)
{
	double now = clock_ms();
	for (int guard = 0; guard < 400 && P.active; guard++) {
		if (P.at >= P.nbeats) {
			if (P.nbeats && P.beats[P.nbeats - 1].kind == B_END) {
				play_stop();
				return;
			}
			if (!next_step()) {
				finish();
				return;
			}
			continue;
		}
		struct beat *b = &P.beats[P.at];
		if (!b->started)
			begin_beat(b, P.t_next > now ? now : P.t_next);
		if (P.click && !b->clicked) {
			P.click = false;
			if (b->kind == B_TEXT) {
				double whole = b->t0 + stage_page_ms(&b->to, b->to_page);
				if (now < whole)
					b->t0 = now - stage_page_ms(&b->to, b->to_page);   /* the page whole at once */
				else if (b->wait) {
					b->clicked = true;
					b->click_t = now;
				}
			} else if (b->kind == B_MAP) {
				if (now >= b->t0 + 1000) {
					b->clicked = true;
					b->click_t = now;
				}
			} else if (b->kind != B_CHOICE && b->kind != B_ASK) {
				b->clicked = true;
				b->click_t = now;
			}
		}
		if (b->kind == B_CHOICE && P.pick >= 0) {
			int r = P.pick;
			P.pick = -1;
			const cJSON *rows = get(b->step, "choice");
			const cJSON *row = cJSON_IsArray(rows) ? cJSON_GetArrayItem(rows, r) : NULL;
			P.cur = b->to;
			P.cur.choice = NULL;
			P.page = b->to_page;
			P.at = P.nbeats;
			P.t_next = now;
			if (!P.on) {
				finish();
				return;
			}
			const cJSON *jg = get(row, "goto"), *je = get(row, "end");
			if (cJSON_IsArray(get(row, "steps")))
				push_list(get(row, "steps"));
			else if (cJSON_IsString(jg))
				go_to(jg->valuestring);
			else if (je) {
				struct stage to = P.cur;
				P.nbeats = P.at = 0;
				beat(B_END, &to);
				P.depth = 0;
			}
			continue;
		}
		if (b->kind == B_ASK && P.ask >= 0) {
			int a = P.ask;
			P.ask = -1;
			P.cur = b->to;
			P.at = P.nbeats;
			P.t_next = now;
			push_list(get(b->step, a ? "then" : "else"));
			continue;
		}
		double end = beat_end(b, now);
		if (end >= 0 && now >= end) {
			P.cur = b->to;
			P.page = b->to_page;
			P.t_next = end;
			P.at++;
			continue;
		}
		break;
	}
}

/* ---- drawing ---- */

static float ease_out4(float t)
{
	float u = 1 - t;
	return 1 - u * u * u * u;
}

/* The world map's screen (T_WorldMap): CG 5530, the place's tag and its name. */
static void map_paint(ImDrawList *dl, V2 o, float k, const cJSON *step, double ms, bool tag);

static void add_shakes(struct stage *v, double now)
{
	float dx, dy;
	shake_at(0, now, &dx, &dy);
	v->m.dx += dx;
	v->m.dy += dy;
	shake_at(1, now, &dx, &dy);
	v->m.face_dx += dx;
	v->m.face_dy += dy;
	shake_at(2, now, &dx, &dy);
	v->m.portrait_dx += dx;
	v->m.portrait_dy += dy;
}

static void paint_to(int slot, const struct stage *s, int page)
{
	ImDrawList *dl = fx_draw(slot);
	if (dl)
		stage_paint(dl, v2(0, 0), 1, s, page);
}

/* Draws the beat at `now` into the window, w wide; returns the stage it shows, for its clicks. */
static void draw_beat(struct beat *b, double now, float w)
{
	struct stage v = b->to, f = b->from;
	v.m.playing = f.m.playing = true;
	v.m.text_ms = f.m.text_ms = -1;
	v.m.no_mark = f.m.no_mark = true;
	add_shakes(&v, now);
	f.m.dx = v.m.dx;
	f.m.dy = v.m.dy;
	float t = (float)(now - b->t0);
	float p = b->ms > 0 ? t / b->ms : 1;
	if (p < 0)
		p = 0;
	if (p > 1)
		p = 1;
	int page = b->to_page;
	bool tex = fx_ok();
	switch (b->kind) {
	case B_TRANS:
		if (tex) {
			paint_to(0, &f, b->from_page);
			paint_to(1, &v, page);
			fx_mix(2, b->effect, 0, 1, p);
			stage_draw_texture(fx_texture(2), w);
			return;
		}
		break;
	case B_SLIDE: {
		const struct picture *pic = media_picture(b->bg);
		float bw = pic->state == PIC_READY ? (float)pic->w : 1024;
		float x0 = b->x0 == SLIDE_IN_X ? -bw : b->x0, x1 = b->x1 == SLIDE_OUT_X ? 1024 + bw : b->x1;
		f.layer[L_BG] = b->bg;
		f.m.bg_x = x0 + (x1 - x0) * p;
		f.m.face_dx = v.m.face_dx;
		f.m.face_dy = v.m.face_dy;
		f.m.portrait_dx = v.m.portrait_dx;
		f.m.portrait_dy = v.m.portrait_dy;
		stage_draw(&f, b->from_page, w);
		return;
	}
	case B_SHAPE: {
		const struct picture *shape = media_cg(b->shape);
		struct stage under = f;
		under.line = NULL;
		if (!tex || shape->state != PIC_READY)
			break;
		paint_to(0, &under, 0);
		if (t < 500) {
			float mag = 20.0f - 20.0f * ease_out4(t / 500.0f);
			fx_shape(2, 0, shape->tex, shape->w, shape->h, mag);
		} else {
			fx_shape(3, 0, shape->tex, shape->w, shape->h, 0);
			paint_to(1, &v, page);
			fx_mix(2, 1, 3, 1, (t - 500) / 500.0f);
		}
		stage_draw_texture(fx_texture(2), w);
		return;
	}
	case B_FLASH:
		v.m.white = 1 - p;
		break;
	case B_TEXT:
		v.m.text_ms = t;
		v.m.no_mark = !b->wait;
		break;
	case B_CHOICE:
		v.choice = b->step;
		break;
	case B_MAP: {
		ImDrawList *dl = NULL;
		if (b->clicked) {
			/* A click: black in 100 ms. */
			float q = (float)((now - b->click_t) / 100.0);
			if (tex && (dl = fx_draw(0))) {
				map_paint(dl, v2(0, 0), 1, b->step, t, true);
				struct stage black;
				stage_start(&black, P.root);
				paint_to(1, &black, 0);
				fx_mix(2, 1, 0, 1, q);
				stage_draw_texture(fx_texture(2), w);
				return;
			}
		} else if (t < 1000 && tex && (dl = fx_draw(1))) {
			/* The map crossfades in over a second. */
			paint_to(0, &f, b->from_page);
			map_paint(dl, v2(0, 0), 1, b->step, t, false);
			fx_mix(2, 1, 0, 1, t / 1000.0f);
			stage_draw_texture(fx_texture(2), w);
			return;
		}
		ImDrawList *wdl = igGetWindowDrawList();
		V2 o = igGetCursorScreenPos();
		o.x = floorf(o.x);
		o.y = floorf(o.y);
		float H = floorf(w * 0.75f);
		ImDrawList_PushClipRect(wdl, o, v2(o.x + w, o.y + H), true);
		map_paint(wdl, o, w / 1024.0f, b->step, t, t >= 1000);
		ImDrawList_PopClipRect(wdl);
		ImDrawList_AddRect(wdl, o, v2(o.x + w, o.y + H), col(C_BORDER), S(4), S(1), 0);
		igDummy(v2(w, H));
		return;
	}
	default:
		break;
	}
	stage_draw(&v, page, w);
}

/* The tag's place and the name's (T_WorldMap::SetPlate): left of x 512 it opens to the right. */
static void map_paint(ImDrawList *dl, V2 o, float k, const cJSON *step, double ms, bool tag)
{
	ImDrawList_AddRectFilled(dl, o, v2(o.x + 1024 * k, o.y + 768 * k), IM_COL32(0, 0, 0, 255), 0, 0);
	const struct picture *map = media_cg(5530);
	if (map->state == PIC_READY)
		ImDrawList_AddImage(dl, texref(map->tex), o, v2(o.x + map->w * k, o.y + map->h * k), v2(0, 0), v2(1, 1), IM_COL32_WHITE);
	if (!tag)
		return;
	const cJSON *w = get(step, "world_map");
	int x = 999, y = 999;
	const char *name = "No data";
	if (cJSON_IsNumber(w)) {
		media_place((int)w->valuedouble, &x, &y, &name);
	} else if (cJSON_IsObject(w)) {
		x = num(w, "x", 0);
		y = num(w, "y", 0);
		const cJSON *n = get(w, "name");
		name = cJSON_IsString(n) ? n->valuestring : "";
	}
	map_tag_paint(dl, o, k, x, y, name, ms);
}

bool play_draw(float w)
{
	if (!P.active)
		return false;
	if (P.preview && P.preview_seen + 1 < UI.frame) {
		play_stop();
		return false;
	}
	if (P.preview && P.restart_at >= 0 && clock_ms() >= P.restart_at) {
		int e = P.preview_effect;
		begin(UI.sel, false);
		if (!P.active)
			return false;
		P.preview = true;
		P.preview_effect = e;
		P.restart_at = -1;
	}
	update();
	if (!P.active)
		return false;
	double now = clock_ms();
	V2 o = igGetCursorScreenPos();
	o.x = floorf(o.x);
	o.y = floorf(o.y);
	float H = floorf(w * 0.75f), k = w / 1024.0f;
	struct beat *b = P.at < P.nbeats ? &P.beats[P.at] : NULL;
	if (b && b->started) {
		draw_beat(b, now, w);
	} else {
		struct stage v = P.cur;
		v.m.playing = true;
		v.m.text_ms = -1;
		v.m.no_mark = true;
		add_shakes(&v, now);
		stage_draw(&v, P.page, w);
	}
	V2 after = igGetCursorScreenPos();

	/* A click on the stage: the next page, a row of the menu. */
	bool over = igIsWindowHovered(0) && igIsMouseHoveringRect(o, v2(o.x + w, o.y + H), true);
	if (b && b->kind == B_ASK) {
		const cJSON *cond = get(b->step, "if");
		char *c = cond ? json_inline(cond) : NULL;
		ImDrawList *dl = igGetWindowDrawList();
		float bw = S(150), gap = S(12), by = o.y + H / 2;
		ImDrawList_AddRectFilled(dl, v2(o.x, by - S(52)), v2(o.x + w, by + S(36)), col_a(C_MODAL_DIM, 0.85f), 0, 0);
		char line[400];
		snprintf(line, sizeof line, "if %s", c ? c : "?");
		free(c);
		V2 ts = igCalcTextSize(line, NULL, false, 0);
		ImDrawList_AddText_Vec2(dl, v2(o.x + (w - fminf(ts.x, w - S(16))) / 2, by - S(44)), col(C_TEXT), line, NULL);
		igSetCursorScreenPos(v2(o.x + w / 2 - bw - gap / 2, by - S(14)));
		if (igButton("Then##ask", v2(bw, 0)))
			P.ask = 1;
		igSetItemTooltip("Play on as if the condition holds");
		igSameLine(0, gap);
		if (igButton(get(b->step, "else") ? "Else##ask" : "Not then##ask", v2(bw, 0)))
			P.ask = 0;
		igSetItemTooltip("Play on as if it does not");
		igSetCursorScreenPos(after);
	} else if (over && igIsMouseClicked_Bool(0, false) && !igIsAnyItemHovered()) {
		if (b && b->kind == B_CHOICE) {
			int r = stage_menu_hit((igGetIO_Nil()->MousePos.x - o.x) / k, (igGetIO_Nil()->MousePos.y - o.y) / k);
			if (r >= 0)
				P.pick = r;
		} else {
			P.click = true;
		}
	}
	return true;
}

const char *play_status(void)
{
	static char s[400];
	if (!P.active)
		return "";
	struct beat *b = P.at < P.nbeats ? &P.beats[P.at] : NULL;
	const char *what = "";
	if (b) {
		switch (b->kind) {
		case B_TEXT: what = b->wait ? "a click or Space for the next page" : "the page, not waiting"; break;
		case B_CLICK: what = "waiting for a click"; break;
		case B_CHOICE: what = P.on ? "click a row of the menu" : "the menu"; break;
		case B_ASK: what = "which way does the if go?"; break;
		case B_MAP: what = "the world map: a click goes on"; break;
		case B_END: what = "the scene ended: a click to leave"; break;
		default: what = P.status; break;
		}
	}
	snprintf(s, sizeof s, "%s%s%s%s", P.preview ? "Preview \xC2\xB7 " : P.on ? "Playing \xC2\xB7 " : "\xE2\x96\xB6 ",
	         what, P.note[0] ? " \xC2\xB7 " : "", P.note);
	return s;
}

bool play_preview(void)
{
	return P.active && P.preview;
}

const char *play_place(void)
{
	return P.place;
}
