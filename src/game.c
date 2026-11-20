/* The game running, for Play: RQ MOD's pipe asked a little at a time, the values Play gives a
 * file's {name}s, and what the game said of a file it refused.
 *
 * What the editor asks, one command at a time and never often (another program, such as an agent's
 * rqmod-ctl, may want the pipe):
 *   mods                which mods RQ MOD loaded and from which folders (every 2 s while no game
 *                       answers, every 10 s after)
 *   facts               RQ MOD's version, once a game answers
 *   scenefunctions MOD  the functions the open mod gave rq.scene.functions (every 10 s)
 *   playscene status    what plays (every 4 s; every 0.4 s while a scene plays or waits)
 * and, on the buttons, playscene MOD FILE [FROM] [name=value ...] and playscene stop. Each answer
 * says which process gave it: another one is the game started again, asked everything anew. */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

#include "editor.h"

enum { T_MODS = 1, T_VERSION, T_FUNCS, T_STATUS, T_PLAY, T_STOP };

static bool enabled = true;       /* --shot: off until an action asks for the game */
static bool asked;                /* an answer came, or no game was found */
static bool connected;            /* a game answered the last time it was asked */
static char silent[200];          /* why the game did not answer, while it does not */
static bool paused;               /* rqmod-ctl pause: commands that need a frame wait */
static char version[32];
static unsigned long game_pid;     /* the game's process: another one is a game started again */
static bool version_asked, old;
static int inflight;              /* polls asked and not answered */
static double next_mods, next_funcs, next_status;

/* What the editor has open. */
static char open_mod[128], open_dir[1024], open_rel[512];

/* The open mod in the game: loaded, and from which folder. */
static bool mods_known, mod_loaded, mod_same;
static char mod_game_dir[1024];

/* The open mod's functions (rq.scene.functions). */
static char (*funcs)[128];
static int nfuncs, capfuncs;
static bool funcs_known;
static char funcs_mod[128];
static unsigned gen = 1;

/* What plays: one line a file, the outermost first. */
struct playing { char mod[128]; char file[512]; char place[256]; };
static struct playing plays[8];
static int nplays;
static bool waiting;
static char waiting_mod[128], waiting_file[512];
static bool stopping, play_asked;
static double play_asked_at;

/* The game's refusal of a file Play sent: its lines, for the file as it was then. */
static char (*refused)[600];
static int nrefused, caprefused;
static char refused_mod[128], refused_rel[512];
static unsigned long long refused_hash;
static unsigned refused_gen = 1;

/* ---- small helpers ---- */

static void wide_of(const char *s, wchar_t *out, int n)
{
	/* The game's folder comes as UTF-8, or in the ANSI code page when its name is not ASCII. */
	if (!MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, s, -1, out, n))
		if (!MultiByteToWideChar(CP_ACP, 0, s, -1, out, n))
			out[0] = 0;
}

/* Two folders the same: full paths, either slash, any case, no slash at the end. */
static bool same_folder(const char *a, const char *b)
{
	wchar_t wa[1100], wb[1100], fa[1100], fb[1100];
	wide_of(a, wa, 1100);
	wide_of(b, wb, 1100);
	if (!_wfullpath(fa, wa, 1100) || !_wfullpath(fb, wb, 1100))
		return false;
	wchar_t *v[2] = { fa, fb };
	for (int k = 0; k < 2; k++) {
		for (wchar_t *p = v[k]; *p; p++)
			if (*p == L'/')
				*p = L'\\';
		size_t n = wcslen(v[k]);
		while (n > 3 && v[k][n - 1] == L'\\')
			v[k][--n] = 0;
	}
	return _wcsicmp(fa, fb) == 0;
}

/* "2.61.0" against another: <0, 0, >0. */
static int version_cmp(const char *a, const char *b)
{
	int x[3] = { 0 }, y[3] = { 0 };
	sscanf(a, "%d.%d.%d", &x[0], &x[1], &x[2]);
	sscanf(b, "%d.%d.%d", &y[0], &y[1], &y[2]);
	for (int i = 0; i < 3; i++)
		if (x[i] != y[i])
			return x[i] < y[i] ? -1 : 1;
	return 0;
}

/* The words of a line as playscene reads them: a word in "..." may hold spaces. */
static int words(const char *s, char out[][512], int max)
{
	int n = 0;
	while (*s && n < max) {
		while (*s == ' ' || *s == '\t')
			s++;
		if (!*s)
			break;
		const char *a = s, *b;
		if (*s == '"') {
			a = s + 1;
			b = strchr(a, '"');
			if (!b)
				b = a + strlen(a);
			s = *b ? b + 1 : b;
		} else {
			b = s;
			while (*b && *b != ' ' && *b != '\t')
				b++;
			s = b;
		}
		snprintf(out[n++], 512, "%.*s", (int)(b - a), a);
	}
	return n;
}

static void slashes(char *s)
{
	for (; *s; s++)
		if (*s == '\\')
			*s = '/';
}

static bool same_file(const char *a, const char *b)
{
	char x[512], y[512];
	snprintf(x, sizeof x, "%s", a);
	snprintf(y, sizeof y, "%s", b);
	slashes(x);
	slashes(y);
	const char *p = x, *q = y;
	if (!strncmp(p, "./", 2))
		p += 2;
	if (!strncmp(q, "./", 2))
		q += 2;
	return !_stricmp(p, q);
}

static void forget_game(void)
{
	connected = false;
	paused = false;
	version[0] = 0;
	version_asked = false;
	old = false;
	mods_known = mod_loaded = mod_same = false;
	mod_game_dir[0] = 0;
	if (funcs_known || nfuncs)
		gen++;
	funcs_known = false;
	nfuncs = 0;
	nplays = 0;
	waiting = false;
	stopping = false;
	game_pid = 0;
}

static void send(int tag, const char *line)
{
	if (pipe_send(tag, line))
		inflight += tag != T_PLAY && tag != T_STOP;
}

/* ---- the answers ---- */

/* The game sends a folder in the ANSI code page when it is not ASCII: as UTF-8 for the window. */
static void to_utf8(char *s, size_t n)
{
	if (!*s || MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, s, -1, NULL, 0))
		return;
	wchar_t w[1100];
	if (MultiByteToWideChar(CP_ACP, 0, s, -1, w, 1100))
		WideCharToMultiByte(CP_UTF8, 0, w, -1, s, (int)n, NULL, NULL);
}

static void on_mods(const char *text)
{
	cJSON *j = cJSON_Parse(text);
	cJSON *arr = cJSON_GetObjectItemCaseSensitive(j, "mods");
	bool was_loaded = mod_loaded;
	mods_known = cJSON_IsArray(arr);
	mod_loaded = mod_same = false;
	mod_game_dir[0] = 0;
	for (cJSON *m = cJSON_IsArray(arr) ? arr->child : NULL; m && open_mod[0]; m = m->next) {
		cJSON *name = cJSON_GetObjectItemCaseSensitive(m, "name"), *path = cJSON_GetObjectItemCaseSensitive(m, "path");
		if (!cJSON_IsString(name) || strcmp(name->valuestring, open_mod))
			continue;
		mod_loaded = true;
		snprintf(mod_game_dir, sizeof mod_game_dir, "%s", cJSON_IsString(path) ? path->valuestring : "");
		to_utf8(mod_game_dir, sizeof mod_game_dir);
		mod_same = open_dir[0] && same_folder(mod_game_dir, open_dir);
	}
	cJSON_Delete(j);
	if (mod_loaded && !was_loaded)
		next_funcs = 0;   /* its functions at once */
	if (!mod_loaded && funcs_known) {
		funcs_known = false;
		nfuncs = 0;
		gen++;
	}
}

static void on_version(const char *text)
{
	const char *v = strstr(text, "\"version\":\"");
	if (v) {
		v += 11;
		size_t n = strcspn(v, "\"");
		snprintf(version, sizeof version, "%.*s", (int)(n < sizeof version ? n : sizeof version - 1), v);
		old = version_cmp(version, "2.61.0") < 0;
	}
}

static void on_funcs(const char *text)
{
	if (!strncmp(text, "err paused", 10)) {
		paused = true;
		return;
	}
	if (!strncmp(text, "err unknown", 11)) {   /* RQ MOD before 2.61.0 */
		old = true;
		return;
	}
	if (strncmp(text, "ok", 2)) {
		if (!strncmp(text, "err no mod", 10)) {
			mod_loaded = false;
			if (funcs_known)
				gen++;
			funcs_known = false;
			nfuncs = 0;
		}
		return;
	}
	paused = false;
	/* The new list; the checker runs again only when it changed. */
	char (*was)[128] = NULL;
	int nwas = nfuncs;
	if (nfuncs) {
		was = malloc((size_t)nfuncs * sizeof *was);
		memcpy(was, funcs, (size_t)nfuncs * sizeof *was);
	}
	nfuncs = 0;
	const char *p = strchr(text, '\n');
	while (p) {
		p++;
		const char *e = strchr(p, '\n');
		size_t n = e ? (size_t)(e - p) : strlen(p);
		while (n && p[n - 1] == '\r')
			n--;
		if (n && n < 128) {
			if (nfuncs == capfuncs) {
				capfuncs = capfuncs ? capfuncs * 2 : 32;
				funcs = realloc(funcs, (size_t)capfuncs * sizeof *funcs);
			}
			memcpy(funcs[nfuncs], p, n);
			funcs[nfuncs++][n] = 0;
		}
		p = e;
	}
	bool same = funcs_known && nwas == nfuncs && !strcmp(funcs_mod, open_mod)
	            && (!nfuncs || !memcmp(was, funcs, (size_t)nfuncs * sizeof *funcs));
	free(was);
	funcs_known = true;
	snprintf(funcs_mod, sizeof funcs_mod, "%s", open_mod);
	if (!same)
		gen++;
}

static void on_status(const char *text)
{
	if (!strncmp(text, "err paused", 10)) {
		paused = true;
		return;
	}
	if (!strncmp(text, "err", 3))
		return;
	paused = false;
	nplays = 0;
	waiting = false;
	for (const char *p = text; p && *p;) {
		const char *e = strchr(p, '\n');
		char line[1200];
		snprintf(line, sizeof line, "%.*s", (int)(e ? e - p : (long long)strlen(p)), p);
		char w[4][512];
		int n = words(line, w, 4);
		if (n == 4 && !strcmp(w[0], "playing") && nplays < (int)(sizeof plays / sizeof plays[0])) {
			snprintf(plays[nplays].mod, sizeof plays[0].mod, "%s", w[1]);
			snprintf(plays[nplays].file, sizeof plays[0].file, "%s", w[2]);
			snprintf(plays[nplays].place, sizeof plays[0].place, "%s", w[3]);
			nplays++;
		} else if (n == 3 && !strcmp(w[0], "waiting")) {
			waiting = true;
			snprintf(waiting_mod, sizeof waiting_mod, "%s", w[1]);
			snprintf(waiting_file, sizeof waiting_file, "%s", w[2]);
		}
		p = e ? e + 1 : NULL;
	}
	if (!nplays && !waiting)
		stopping = false;
	/* A play answered "ok" a moment ago may not show yet: status asked again soon. */
	if (nplays || waiting)
		play_asked = false;
}

static void on_play(const char *text)
{
	if (!strcmp(text, "ok")) {
		ui_status("Sent to the game: it plays as soon as the game lets a scene in");
		next_status = 0;
		return;
	}
	play_asked = false;
	if (!strncmp(text, "err the file has", 16)) {
		/* The game's checker refused it: its lines, for the Problems panel. */
		nrefused = 0;
		const char *p = strchr(text, '\n');
		while (p) {
			p++;
			const char *e = strchr(p, '\n');
			size_t n = e ? (size_t)(e - p) : strlen(p);
			if (n) {
				if (nrefused == caprefused) {
					caprefused = caprefused ? caprefused * 2 : 16;
					refused = realloc(refused, (size_t)caprefused * sizeof *refused);
				}
				snprintf(refused[nrefused++], 600, "%.*s", (int)n, p);
			}
			p = e;
		}
		refused_gen++;
		char first[200];
		snprintf(first, sizeof first, "%.*s", (int)strcspn(text + 4, "\n"), text + 4);
		ui_error("The game does not play it: %s (in Problems)", first);
		return;
	}
	if (!strncmp(text, "err paused", 10)) {
		paused = true;
		ui_error("The game is paused (rqmod-ctl pause): Play waits for rqmod-ctl resume");
		return;
	}
	ui_error("The game does not play it: %s", !strncmp(text, "err ", 4) ? text + 4 : text);
}

static void on_stop(const char *text)
{
	if (!strcmp(text, "ok")) {
		stopping = true;
		next_status = 0;
		ui_status("Stopping: the scene ends at its next step, after the click it waits for");
	} else if (!strncmp(text, "err no scene", 12)) {
		stopping = false;
		nplays = 0;
		waiting = false;
	} else {
		ui_error("The game did not stop it: %s", !strncmp(text, "err ", 4) ? text + 4 : text);
	}
}

static void on_answer(struct pipe_answer *a, double now)
{
	if (a->tag != T_PLAY && a->tag != T_STOP && inflight > 0)
		inflight--;
	asked = true;
	if (a->how == PIPE_NO_GAME) {
		if (connected)
			ui_status("The game is not running any more");
		forget_game();
		silent[0] = 0;
		next_mods = now + 2;
		if (a->tag == T_PLAY || a->tag == T_STOP) {
			play_asked = false;
			ui_error("The game is not running");
		}
		return;
	}
	if (a->how == PIPE_NO_ANSWER) {
		snprintf(silent, sizeof silent, "its pipe stayed busy or gave nothing back");
		if (a->tag == T_PLAY || a->tag == T_STOP) {
			play_asked = false;
			ui_error("The game did not answer: try again");
		}
		return;
	}
	const char *t = a->text ? a->text : "";
	if (a->pid) {
		/* Another process: the game was started again between two questions. All asked anew. */
		if (game_pid && a->pid != game_pid) {
			forget_game();
			next_mods = now;
			ui_status("The game was started again: the editor asks it anew");
		}
		game_pid = a->pid;
	}
	if (!connected) {
		connected = true;
		next_status = 0;
	}
	if (!strncmp(t, "err timeout", 11)) {
		snprintf(silent, sizeof silent, "%s", t + 4);
		if (a->tag == T_PLAY || a->tag == T_STOP) {
			play_asked = false;
			ui_error("The game did not answer: %s", t + 4);
		}
		return;
	}
	silent[0] = 0;
	switch (a->tag) {
	case T_MODS: on_mods(t); break;
	case T_VERSION: on_version(t); break;
	case T_FUNCS: on_funcs(t); break;
	case T_STATUS: on_status(t); break;
	case T_PLAY: on_play(t); break;
	case T_STOP: on_stop(t); break;
	}
}

/* ---- once a frame ---- */

void game_enable(bool on)
{
	enabled = on;
}

bool game_enabled(void)
{
	return enabled;
}

void game_frame(double now, const char *mod, const char *mod_dir, const char *rel)
{
	struct pipe_answer a;
	while (pipe_take(&a)) {
		on_answer(&a, now);
		free(a.text);
	}
	if (strcmp(open_mod, mod ? mod : "") || strcmp(open_dir, mod_dir ? mod_dir : "")) {
		snprintf(open_mod, sizeof open_mod, "%s", mod ? mod : "");
		snprintf(open_dir, sizeof open_dir, "%s", mod_dir ? mod_dir : "");
		mods_known = mod_loaded = mod_same = false;
		if (funcs_known)
			gen++;
		funcs_known = false;
		nfuncs = 0;
		next_mods = now;
	}
	snprintf(open_rel, sizeof open_rel, "%s", rel ? rel : "");
	if (!enabled || inflight)
		return;
	bool scene = nplays || waiting || play_asked || stopping;
	if (now >= next_mods) {
		send(T_MODS, "mods");
		next_mods = now + (connected ? 10 : 2);
	} else if (!connected) {
		return;
	} else if (!version_asked) {
		version_asked = true;
		send(T_VERSION, "facts");
	} else if (old) {
		return;
	} else if (open_mod[0] && mod_loaded && now >= next_funcs) {
		char line[200];
		snprintf(line, sizeof line, "scenefunctions %s", open_mod);
		send(T_FUNCS, line);
		next_funcs = now + 10;
	} else if (now >= next_status) {
		send(T_STATUS, "playscene status");
		next_status = now + (scene ? 0.4 : 4);
	}
	if (play_asked && now - play_asked_at > 3)
		play_asked = false;
}

int game_state(void)
{
	if (!enabled && !asked)
		return GAME_OFF;
	if (!asked)
		return GAME_LOOKING;
	if (!connected)
		return silent[0] ? GAME_SILENT : GAME_OFF;
	if (silent[0])
		return GAME_SILENT;
	if (old)
		return GAME_OLD;
	if (!open_mod[0])
		return GAME_READY;
	if (!mods_known)
		return GAME_LOOKING;   /* the open mod asked for, not answered yet */
	if (!mod_loaded)
		return GAME_NO_MOD;
	if (!mod_same)
		return GAME_OTHER_DIR;
	return GAME_READY;
}

const char *game_version(void)
{
	return version;
}

const char *game_mod_dir(void)
{
	return mod_game_dir;
}

const char *game_silent_why(void)
{
	return silent;
}

bool game_paused(void)
{
	return connected && paused;
}

int game_scene(void)
{
	if (nplays)
		return 2;
	return waiting || play_asked ? 1 : 0;
}

bool game_stopping(void)
{
	return stopping && (nplays || waiting);
}

const char *game_place_for(const char *mod, const char *rel)
{
	for (int i = nplays - 1; i >= 0; i--)
		if (!strcmp(plays[i].mod, mod) && same_file(plays[i].file, rel))
			return plays[i].place;
	return NULL;
}

bool game_waiting_for(const char *mod, const char *rel)
{
	return (waiting && !strcmp(waiting_mod, mod) && same_file(waiting_file, rel)) || (play_asked && !nplays && !waiting);
}

/* The innermost file playing: its mod, file and place. */
bool game_playing_now(const char **mod, const char **file, const char **place)
{
	if (!nplays)
		return false;
	*mod = plays[nplays - 1].mod;
	*file = plays[nplays - 1].file;
	*place = plays[nplays - 1].place;
	return true;
}

bool game_functions_known(void)
{
	return funcs_known && game_state() == GAME_READY && !strcmp(funcs_mod, open_mod);
}

int game_function_count(void)
{
	return game_functions_known() ? nfuncs : 0;
}

const char *game_function(int i)
{
	return i >= 0 && i < nfuncs ? funcs[i] : "";
}

bool game_has_function(const char *name)
{
	for (int i = 0; i < nfuncs; i++)
		if (!strcmp(funcs[i], name))
			return true;
	return false;
}

unsigned game_gen(void)
{
	return gen + refused_gen * 7919u + (game_functions_known() ? 1u : 0u);
}

/* ---- Play and Stop ---- */

static bool word_ok(const char *s)
{
	for (; *s; s++)
		if (*s == '"' || (unsigned char)*s < 0x20)
			return false;
	return true;
}

static size_t put_word(char *out, size_t n, size_t o, const char *w)
{
	bool quote = strchr(w, ' ') || strchr(w, '\t') || !w[0];
	if (o < n)
		o += (size_t)snprintf(out + o, n - o, quote ? " \"%s\"" : " %s", w);
	return o;
}

/* The values for Play after a playscene line's FILE and FROM: false and why when one cannot go. */
static bool values_words(char *line, size_t ln, size_t *o, char *why, size_t wn)
{
	struct value_name names[128];
	int n = values_names(NULL, names, 128);
	for (int i = 0; i < n; i++) {
		char w[700];
		bool quoted;
		if (!values_word(names[i].name, w, sizeof w, &quoted))
			continue;
		if (!word_ok(w)) {
			snprintf(why, wn, "the value of {%s} has a double quote or a line break: the game's pipe cannot take it",
			         names[i].name);
			return false;
		}
		if (quoted && version[0] && version_cmp(version, "2.62.1") < 0) {
			snprintf(why, wn, "{%s} is the text \"%s\", which RQ MOD %s reads as a number, true or false (or a text "
			         "without its quotes): sending it as a text needs RQ MOD 2.62.1; or give {%s} as a Number, true or false",
			         names[i].name, values_get(names[i].name), version, names[i].name);
			return false;
		}
		*o = put_word(line, ln, *o, w);
	}
	if (*o >= ln || strlen(line) > pipe_line_max()) {
		snprintf(why, wn, "the command is %zu bytes: the game's pipe takes %zu (shorter values)", strlen(line), pipe_line_max());
		return false;
	}
	return true;
}

const char *game_values_why(char *why, size_t wn)
{
	why[0] = 0;
	char line[2048];
	size_t o = (size_t)snprintf(line, sizeof line, "playscene");
	o = put_word(line, sizeof line, o, open_mod);
	o = put_word(line, sizeof line, o, open_rel);
	o = put_word(line, sizeof line, o, "steps[0].choice[0].steps[0]");   /* room for a long From here */
	if (!values_words(line, sizeof line, &o, why, wn) && !why[0])
		snprintf(why, wn, "the values cannot be sent");
	return why;
}

bool game_play(const char *from, unsigned long long hash, char *why, size_t wn)
{
	if (!open_mod[0] || !open_rel[0]) {
		snprintf(why, wn, "no scene file open");
		return false;
	}
	if (game_state() != GAME_READY) {
		snprintf(why, wn, "the game cannot play it now");
		return false;
	}
	char line[2048];
	size_t o = (size_t)snprintf(line, sizeof line, "playscene");
	o = put_word(line, sizeof line, o, open_mod);
	o = put_word(line, sizeof line, o, open_rel);
	if (!word_ok(open_rel)) {
		snprintf(why, wn, "the file's name has a double quote");
		return false;
	}
	if (from && from[0])
		o = put_word(line, sizeof line, o, from);
	if (!values_words(line, sizeof line, &o, why, wn))
		return false;
	if (!pipe_send(T_PLAY, line)) {
		snprintf(why, wn, "the editor's queue to the game is full");
		return false;
	}
	/* What the game may refuse is for the file as it is now. */
	nrefused = 0;
	refused_gen++;
	snprintf(refused_mod, sizeof refused_mod, "%s", open_mod);
	snprintf(refused_rel, sizeof refused_rel, "%s", open_rel);
	refused_hash = hash;
	play_asked = true;
	play_asked_at = igGetTime();
	stopping = false;
	return true;
}

void game_stop(void)
{
	pipe_send(T_STOP, "playscene stop");
}

int game_refusal(const char *mod, const char *rel, unsigned long long hash, const char **line, int i)
{
	if (!nrefused || strcmp(mod, refused_mod) || !same_file(rel, refused_rel) || hash != refused_hash)
		return 0;
	if (line)
		*line = i >= 0 && i < nrefused ? refused[i] : "";
	return nrefused;
}

/* ---- the pill in the top bar ---- */

void game_pill(char *out, size_t n, int *colour, char *tip, size_t tn)
{
	int st = game_state();
	*colour = C_FAINT;
	tip[0] = 0;
	switch (st) {
	case GAME_LOOKING:
		snprintf(out, n, "Looking for the game\xE2\x80\xA6");
		return;
	case GAME_OFF:
		snprintf(out, n, "Game not running");
		snprintf(tip, tn, "Start the game with RQ MOD (the Mods copy): the editor finds it on its own, through "
		         "RQ MOD's pipe, and Play sends the scene to it.");
		return;
	case GAME_SILENT:
		*colour = C_WARN;
		snprintf(out, n, "The game does not answer");
		snprintf(tip, tn, "RQ MOD's pipe: %s. The editor asks again in a moment.", silent);
		return;
	case GAME_OLD:
		*colour = C_WARN;
		snprintf(out, n, "RQ MOD %s: Play needs 2.61.0", version[0] ? version : "(too old)");
		snprintf(tip, tn, "This RQ MOD cannot play a scene file from the editor: update it to 2.61.0 or newer.");
		return;
	case GAME_NO_MOD:
		*colour = C_WARN;
		snprintf(out, n, "Game running \xC2\xB7 %s not loaded", open_mod);
		snprintf(tip, tn, "RQ MOD %s runs without %s: is the mod in the game's mods folder, turned on, and does "
		         "the game have what its mod.json asks for? The game's log says why (Game log, every line).",
		         version, open_mod);
		return;
	case GAME_OTHER_DIR: {
		*colour = C_WARN;
		char a[1100], b[1100];
		snprintf(out, n, "Game running \xC2\xB7 %s from another folder", open_mod);
		snprintf(tip, tn, "The game runs %s from %s, not from the folder the editor has open (%s): Play would "
		         "play that copy.", open_mod, path_shown(mod_game_dir, a, sizeof a), path_shown(open_dir, b, sizeof b));
		return;
	}
	}
	const char *pm, *pf, *pp;
	if (paused) {
		*colour = C_WARN;
		snprintf(out, n, "Game paused");
		snprintf(tip, tn, "rqmod-ctl pause holds the game on a frame: Play waits for rqmod-ctl resume.");
	} else if (stopping && (nplays || waiting)) {
		*colour = C_WARN;
		snprintf(out, n, "Stopping\xE2\x80\xA6");
		snprintf(tip, tn, "The scene ends at its next step: after the click its line or menu waits for.");
	} else if (game_playing_now(&pm, &pf, &pp)) {
		*colour = C_PLAY;
		if (!strcmp(pm, open_mod) && same_file(pf, open_rel)) {
			snprintf(out, n, "Playing in the game \xC2\xB7 %s", pp);
			snprintf(tip, tn, "The game plays this file at %s (lit in the list). Click to select it.", pp);
		} else {
			snprintf(out, n, "Playing in the game \xC2\xB7 %s %s", pm, pf);
			snprintf(tip, tn, "The game plays another file: %s's %s, at %s.", pm, pf, pp);
		}
	} else if (waiting || play_asked) {
		*colour = C_PLAY;
		snprintf(out, n, "Waiting for the game to let the scene in");
		snprintf(tip, tn, "The game starts a scene at its next frame, unless the System menu or Config is open, "
		         "a scene of its own plays, or it is saving.");
	} else {
		*colour = C_OK;
		snprintf(out, n, "Game running \xC2\xB7 RQ MOD %s", version[0] ? version : "");
		if (funcs_known)
			snprintf(tip, tn, "RQ MOD %s runs %s from this folder: Play sends the scene to it. The mod names "
			         "%d function%s for its scene files.", version, open_mod, nfuncs, nfuncs == 1 ? "" : "s");
		else
			snprintf(tip, tn, "RQ MOD %s: Play sends the scene to it.", version);
	}
}

/* ---- the values Play gives {name}s ---- */

static cJSON *mem_values;   /* --shot: kept in memory, not in the settings */
static unsigned values_generation = 1;

/* play_values -> mod -> file -> name -> a text, a number, true or false. */
static cJSON *values_root(bool make)
{
	cJSON *tree = UI.shot ? NULL : settings_tree();
	if (!tree) {
		if (!mem_values && make)
			mem_values = cJSON_CreateObject();
		return mem_values;
	}
	cJSON *v = cJSON_GetObjectItemCaseSensitive(tree, "play_values");
	if (!cJSON_IsObject(v) && make) {
		cJSON_DeleteItemFromObjectCaseSensitive(tree, "play_values");
		v = cJSON_AddObjectToObject(tree, "play_values");
	}
	return cJSON_IsObject(v) ? v : NULL;
}

static cJSON *values_of(const char *mod, const char *rel, bool make)
{
	cJSON *root = values_root(make);
	if (!root || !mod || !rel)
		return NULL;
	cJSON *m = cJSON_GetObjectItemCaseSensitive(root, mod);
	if (!cJSON_IsObject(m)) {
		if (!make)
			return NULL;
		cJSON_DeleteItemFromObjectCaseSensitive(root, mod);
		m = cJSON_AddObjectToObject(root, mod);
	}
	cJSON *f = cJSON_GetObjectItemCaseSensitive(m, rel);
	if (!cJSON_IsObject(f)) {
		if (!make)
			return NULL;
		cJSON_DeleteItemFromObjectCaseSensitive(m, rel);
		f = cJSON_AddObjectToObject(m, rel);
	}
	return f;
}

static const cJSON *value_item(const char *mod, const char *rel, const char *name)
{
	const cJSON *v = cJSON_GetObjectItemCaseSensitive(values_of(mod, rel, false), name);
	return cJSON_IsString(v) || cJSON_IsNumber(v) || cJSON_IsBool(v) ? v : NULL;
}

/* The open file's: the window's mod and file, known before the game is asked anything. */
static const cJSON *value_now(const char *name)
{
	return D.open ? value_item(ui_mod_id(), D.rel, name) : NULL;
}

/* A number as the scene's fill shows it: a whole one with no point, else Lua's %.14g. */
static void number_shown(double d, char *out, size_t n)
{
	if (d == floor(d) && d > -1e15 && d < 1e15)
		snprintf(out, n, "%.0f", d);
	else
		snprintf(out, n, "%.14g", d);
}

static const char *value_text(const cJSON *v)
{
	static char ring[8][64];
	static int at;
	if (!v)
		return NULL;
	if (cJSON_IsString(v))
		return v->valuestring;
	if (cJSON_IsBool(v))
		return cJSON_IsTrue(v) ? "true" : "false";
	char *b = ring[at++ & 7];
	number_shown(v->valuedouble, b, 64);
	return b;
}

const char *values_get_for(const char *mod, const char *rel, const char *name)
{
	return value_text(value_item(mod, rel, name));
}

const char *values_get(const char *name)
{
	return value_text(value_now(name));
}

int values_type(const char *name)
{
	const cJSON *v = value_now(name);
	return !v ? VAL_NONE : cJSON_IsString(v) ? VAL_TEXT : cJSON_IsNumber(v) ? VAL_NUMBER : cJSON_IsTrue(v) ? VAL_TRUE : VAL_FALSE;
}

/* A number typed: the whole text, finite. */
static bool number_of(const char *s, double *d)
{
	while (*s == ' ')
		s++;
	if (!*s)
		return false;
	char *end = NULL;
	*d = strtod(s, &end);
	while (end && *end == ' ')
		end++;
	return end && !*end && isfinite(*d);
}

bool values_set_typed(const char *name, int type, const char *text)
{
	if (!D.open)
		return false;
	cJSON *nv = NULL;
	double d = 0;
	switch (type) {
	case VAL_TEXT: nv = cJSON_CreateString(text ? text : ""); break;
	case VAL_NUMBER:
		if (!number_of(text ? text : "", &d))
			return false;
		nv = cJSON_CreateNumber(d);
		break;
	case VAL_TRUE: nv = cJSON_CreateTrue(); break;
	case VAL_FALSE: nv = cJSON_CreateFalse(); break;
	}
	const cJSON *old_v = value_now(name);
	if ((!old_v && !nv) || (old_v && nv && cJSON_Compare(old_v, nv, true))) {
		cJSON_Delete(nv);
		return true;
	}
	cJSON *f = values_of(ui_mod_id(), D.rel, nv != NULL);
	if (!f) {
		cJSON_Delete(nv);
		return true;
	}
	cJSON_DeleteItemFromObjectCaseSensitive(f, name);
	if (nv)
		cJSON_AddItemToObject(f, name, nv);
	values_generation++;
	if (!UI.shot && settings_tree())
		settings_save();
	return true;
}

void values_set(const char *name, const char *text)
{
	values_set_typed(name, text && text[0] ? VAL_TEXT : VAL_NONE, text);
}

/* A text RQ MOD's playscene would type as something else: a whole number, a number with a point,
 * true, false, or a text in single quotes (whose quotes it takes off). */
static bool text_needs_quotes(const char *s)
{
	size_t n = strlen(s);
	if (!strcmp(s, "true") || !strcmp(s, "false"))
		return true;
	if (n >= 2 && s[0] == '\'' && s[n - 1] == '\'')
		return true;
	const char *p = s + (*s == '-');
	size_t digits = strspn(p, "0123456789");
	if (digits && !p[digits])
		return true;
	if (digits && p[digits] == '.') {
		size_t more = strspn(p + digits + 1, "0123456789");
		if (more && !p[digits + 1 + more])
			return true;
	}
	return false;
}

bool values_word(const char *name, char *out, size_t n, bool *quoted)
{
	const cJSON *v = value_now(name);
	*quoted = false;
	if (!v)
		return false;
	if (cJSON_IsString(v)) {
		*quoted = text_needs_quotes(v->valuestring);
		snprintf(out, n, *quoted ? "%s='%s'" : "%s=%s", name, v->valuestring);
	} else if (cJSON_IsBool(v)) {
		snprintf(out, n, "%s=%s", name, cJSON_IsTrue(v) ? "true" : "false");
	} else {
		/* A whole number as digits (a whole number in the game), else digits with a point. */
		double d = v->valuedouble;
		char num[400];
		if (d == floor(d) && d > -9e18 && d < 9e18) {
			snprintf(num, sizeof num, "%.0f", d);
		} else {
			/* The shortest that reads back the same, never with an exponent (the game takes digits). */
			snprintf(num, sizeof num, "%.15g", d);
			if (strtod(num, NULL) != d)
				snprintf(num, sizeof num, "%.17g", d);
			if (strchr(num, 'e') || strchr(num, 'E')) {
				snprintf(num, sizeof num, "%.20f", d);
				size_t k = strlen(num);
				while (k > 2 && num[k - 1] == '0' && num[k - 2] != '.')
					num[--k] = 0;
			}
		}
		snprintf(out, n, "%s=%s", name, num);
	}
	return true;
}

unsigned values_gen(void)
{
	return values_generation;
}

const char *values_fill(const char *s)
{
	static char *buf;
	static size_t cap;
	if (!s)
		return s;
	if (!strchr(s, '{') && !strchr(s, '}'))
		return s;
	size_t need = strlen(s) * 2 + 64;
	size_t o = 0;
	for (const char *p = s; *p;) {
		if (p[0] == '{' && p[1] == '{') {
			if (o + 2 > cap) { cap = (o + 2) * 2 + need; buf = realloc(buf, cap); }
			buf[o++] = '{';
			p += 2;
			continue;
		}
		if (p[0] == '}' && p[1] == '}') {
			if (o + 2 > cap) { cap = (o + 2) * 2 + need; buf = realloc(buf, cap); }
			buf[o++] = '}';
			p += 2;
			continue;
		}
		const char *add = NULL;
		size_t addn = 0, skip = 1;
		if (p[0] == '{' && (isalpha((unsigned char)p[1]) || p[1] == '_')) {
			const char *b = p + 1;
			while (isalnum((unsigned char)*b) || *b == '_')
				b++;
			if (*b == '}' && b - p - 1 < 64) {
				char name[64];
				snprintf(name, sizeof name, "%.*s", (int)(b - p - 1), p + 1);
				const char *v = values_get(name);
				if (v) {
					add = v;
					addn = strlen(add);
					skip = (size_t)(b - p) + 1;
				}
			}
		}
		if (!add) {
			add = p;
			addn = 1;
		}
		if (o + addn + 1 > cap) {
			cap = (o + addn + 1) * 2 + need;
			buf = realloc(buf, cap);
		}
		memcpy(buf + o, add, addn);
		o += addn;
		p += skip;
	}
	if (o + 1 > cap) {
		cap = o + 64;
		buf = realloc(buf, cap);
	}
	buf[o] = 0;
	return buf;
}

/* ---- the names a file uses ---- */

struct collect {
	struct value_name *out;
	int n, max;
};

static struct value_name *name_entry(struct collect *c, const char *s, size_t len)
{
	if (!len || len >= sizeof c->out[0].name)
		return NULL;
	for (int i = 0; i < c->n; i++)
		if (strlen(c->out[i].name) == len && !strncmp(c->out[i].name, s, len))
			return &c->out[i];
	if (c->n == c->max)
		return NULL;
	struct value_name *e = &c->out[c->n++];
	memset(e, 0, sizeof *e);
	memcpy(e->name, s, len);
	e->name[len] = 0;
	return e;
}

static void text_names(struct collect *c, const char *s, const char *place)
{
	if (!s || !strchr(s, '{'))
		return;
	for (const char *p = s; *p; p++) {
		if (p[0] == '{' && p[1] == '{') {
			p++;
			continue;
		}
		if (*p != '{' || !(isalpha((unsigned char)p[1]) || p[1] == '_'))
			continue;
		const char *a = p + 1, *b = a;
		while (isalnum((unsigned char)*b) || *b == '_')
			b++;
		if (*b != '}')
			continue;
		struct value_name *e = name_entry(c, a, (size_t)(b - a));
		if (e) {
			if (!e->uses)
				snprintf(e->first, sizeof e->first, "%s", place);
			e->uses++;
		}
		p = b;
	}
}

static void cond_names(struct collect *c, const cJSON *v, const char *place)
{
	if (cJSON_IsObject(v)) {
		const cJSON *var = cJSON_GetObjectItemCaseSensitive(v, "var");
		if (cJSON_IsString(var)) {
			struct value_name *e = name_entry(c, var->valuestring, strlen(var->valuestring));
			if (e) {
				if (!e->uses)
					snprintf(e->first, sizeof e->first, "%s", place);
				e->uses++;
				e->checked = true;
			}
		}
	}
	if (cJSON_IsObject(v) || cJSON_IsArray(v))
		for (const cJSON *k = v->child; k; k = k->next)
			cond_names(c, k, place);
}

static const char *str_of(const cJSON *o, const char *k)
{
	const cJSON *v = cJSON_GetObjectItemCaseSensitive(o, k);
	return cJSON_IsString(v) ? v->valuestring : NULL;
}

static void step_names(cJSON *st, const char *place, void *ud)
{
	struct collect *c = ud;
	const char *kind = step_kind(st);
	if (!kind)
		return;
	const char *into = str_of(st, "into");
	if (into && (!strcmp(kind, "choice") || !strcmp(kind, "call"))) {
		struct value_name *e = name_entry(c, into, strlen(into));
		if (e && !e->set_by[0])
			snprintf(e->set_by, sizeof e->set_by, "%s", place);
	}
	if (!strcmp(kind, "say") || !strcmp(kind, "think") || !strcmp(kind, "narrate")) {
		for (int i = 0, n = line_pages(st); i < n; i++)
			text_names(c, line_page(st, i), place);
		text_names(c, str_of(st, "name"), place);
	} else if (!strcmp(kind, "if")) {
		cond_names(c, cJSON_GetObjectItemCaseSensitive(st, "if"), place);
	} else if (!strcmp(kind, "choice")) {
		const cJSON *rows = cJSON_GetObjectItemCaseSensitive(st, "choice");
		int r = 0;
		for (const cJSON *row = cJSON_IsArray(rows) ? rows->child : NULL; row; row = row->next, r++) {
			char rp[300];
			snprintf(rp, sizeof rp, "%s.choice[%d]", place, r);
			if (cJSON_IsString(row)) {
				text_names(c, row->valuestring, rp);
			} else {
				text_names(c, str_of(row, "text"), rp);
				text_names(c, str_of(row, "comment"), rp);
				cond_names(c, cJSON_GetObjectItemCaseSensitive(row, "if"), rp);
			}
		}
	}
}

int values_names(const cJSON *root, struct value_name *out, int max)
{
	if (!root)
		root = D.root;
	cJSON *steps = root ? cJSON_GetObjectItemCaseSensitive(root, "steps") : NULL;
	if (!cJSON_IsArray(steps))
		return 0;
	struct collect c = { out, 0, max };
	steps_each(steps, "steps", step_names, &c);
	/* The cast's name plates. */
	const cJSON *cast = cJSON_GetObjectItemCaseSensitive(root, "cast");
	for (const cJSON *e = cJSON_IsObject(cast) ? cast->child : NULL; e; e = e->next) {
		char cp[200];
		snprintf(cp, sizeof cp, "cast.%s", e->string ? e->string : "");
		text_names(&c, str_of(e, "name"), cp);
	}
	return c.n;
}
