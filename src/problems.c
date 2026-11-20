/* Problems: what RQ MOD says of a scene file, and what the editor sees the game would show otherwise.
 *
 * RQ MOD's own checks run here as the loader runs them: its checker (scene_check.lua, embedded at
 * the build) in a Lua 5.4 of the editor's own, on the file's JSON turned into Lua as the loader
 * turns it (a whole number an integer, an array with the checker's metatable, null its sentinel).
 * The names of the game's quests, characters and items come from RQ MOD's names (rq.ids, through
 * Rance Browser's readers) with the loader's rules. The mod's functions come from the game while it
 * runs the mod from this folder (scenefunctions); without the game a call passes here, as does a
 * mod's own id ("mymod:thing"), which only the game knows: what it refused when Play sent the file
 * is added to the file's problems until the file changes. A picture's file is looked for in the
 * mod's folder. What the checker finds, RQ MOD refuses: the file does not play.
 *
 * The editor's own warnings come from the game's box: a page longer than the box holds, a letter
 * the game's font cannot draw, and a {name} no step of the file sets. The game plays such a file,
 * but not as it was meant to look. */
#include <ctype.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "lua.h"
#include "lauxlib.h"
#include "lualib.h"
#include "system4/file.h"
#include "editor.h"

extern const char scene_check_lua[];
extern const unsigned scene_check_lua_len;

static lua_State *L;
static int check_ref = LUA_NOREF, null_ref = LUA_NOREF, array_ref = LUA_NOREF;
static char load_why[300];
static const char *mod_dir_now;
bool problems_box;
/* The file being checked, for its values for Play (a {name} given one is not warned of). */
static const char *vals_mod, *vals_rel;

/* ---- the checker in Lua ---- */

static bool checker_load(void)
{
	if (L)
		return check_ref != LUA_NOREF;
	L = luaL_newstate();
	if (!L) {
		snprintf(load_why, sizeof load_why, "no memory for Lua");
		return false;
	}
	/* Only what the checker uses: no files, no system. */
	static const luaL_Reg libs[] = {
		{ LUA_GNAME, luaopen_base }, { LUA_STRLIBNAME, luaopen_string },
		{ LUA_TABLIBNAME, luaopen_table }, { LUA_MATHLIBNAME, luaopen_math },
		{ LUA_UTF8LIBNAME, luaopen_utf8 },
	};
	for (size_t i = 0; i < sizeof libs / sizeof libs[0]; i++) {
		luaL_requiref(L, libs[i].name, libs[i].func, 1);
		lua_pop(L, 1);
	}
	if (luaL_loadbuffer(L, scene_check_lua, scene_check_lua_len, "=scene_check") != LUA_OK ||
	    lua_pcall(L, 0, 1, 0) != LUA_OK) {
		snprintf(load_why, sizeof load_why, "RQ MOD's checker did not load: %s", lua_tostring(L, -1));
		lua_settop(L, 0);
		return false;
	}
	if (!lua_istable(L, -1)) {
		snprintf(load_why, sizeof load_why, "RQ MOD's checker did not load: it gave no table");
		lua_settop(L, 0);
		return false;
	}
	lua_getfield(L, -1, "NULL");
	null_ref = luaL_ref(L, LUA_REGISTRYINDEX);
	lua_getfield(L, -1, "ARRAY");
	array_ref = luaL_ref(L, LUA_REGISTRYINDEX);
	lua_getfield(L, -1, "check");
	check_ref = luaL_ref(L, LUA_REGISTRYINDEX);
	lua_settop(L, 0);
	return true;
}

/* A cJSON value as the loader gives it to the checker. */
static bool push_json(const cJSON *v, int depth)
{
	if (depth > 200 || !lua_checkstack(L, 4))
		return false;
	if (cJSON_IsNull(v)) {
		lua_rawgeti(L, LUA_REGISTRYINDEX, null_ref);
	} else if (cJSON_IsBool(v)) {
		lua_pushboolean(L, cJSON_IsTrue(v));
	} else if (cJSON_IsNumber(v)) {
		double d = v->valuedouble;
		lua_Integer i;
		if (d >= -9007199254740992.0 && d <= 9007199254740992.0 && d == (double)(i = (lua_Integer)d))
			lua_pushinteger(L, i);
		else
			lua_pushnumber(L, d);
	} else if (cJSON_IsString(v)) {
		lua_pushstring(L, v->valuestring ? v->valuestring : "");
	} else if (cJSON_IsArray(v)) {
		lua_newtable(L);
		lua_rawgeti(L, LUA_REGISTRYINDEX, array_ref);
		lua_setmetatable(L, -2);
		lua_Integer n = 0;
		for (const cJSON *c = v->child; c; c = c->next) {
			if (!push_json(c, depth + 1))
				return false;
			lua_rawseti(L, -2, ++n);
		}
	} else if (cJSON_IsObject(v)) {
		lua_newtable(L);
		for (const cJSON *c = v->child; c; c = c->next) {
			if (!push_json(c, depth + 1))
				return false;
			lua_setfield(L, -2, c->string ? c->string : "");
		}
	} else {
		return false;
	}
	return true;
}

/* ---- the game's names, by the loader's rules (rq.ids.find, then a mod's own id) ---- */

/* As rq.ids compares: lower case, é as e, nothing but letters and digits. */
static void fold(const char *s, char *out, size_t n)
{
	size_t o = 0;
	for (const unsigned char *p = (const unsigned char *)s; *p && o + 1 < n;) {
		if (p[0] == 0xC3 && p[1] == 0xA9) {             /* é */
			out[o++] = 'e';
			p += 2;
		} else if (p[0] == 0xEF && p[1] == 0xBD && p[2] == 0xA5) {   /* ｅ */
			out[o++] = 'e';
			p += 3;
		} else {
			if (*p < 0x80 && isalnum(*p))
				out[o++] = (char)tolower(*p);
			p++;
		}
	}
	out[o] = 0;
}

static const struct id_kind *kind_of(const char *kind)
{
	for (int k = 0; k < G.nkinds; k++)
		if (!strcmp(G.kinds[k].kind, kind))
			return &G.kinds[k];
	return NULL;
}

int problems_find_id(const char *kind, const char *name, char *why, size_t n)
{
	static const char *const text[][2] = { { "quest", "quest" }, { "chara", "character" }, { "item", "item" } };
	const char *what = kind;
	for (int i = 0; i < 3; i++)
		if (!strcmp(text[i][0], kind))
			what = text[i][1];
	if (why && n)
		why[0] = 0;
	const struct id_kind *k = kind_of(kind);
	if (!k)
		return -1;   /* no names to look in: not known here */
	char want[256], f[256];
	fold(name, want, sizeof want);
	int hits[64], nh = 0, by_key = 0, nk = 0, plain = 0;
	for (int i = 0; i < k->n; i++) {
		fold(k->e[i].name, f, sizeof f);
		if (!strcmp(f, want) && nh < 64)
			hits[nh++] = k->e[i].no;
		fold(k->e[i].key, f, sizeof f);
		if (!strcmp(f, want)) {
			nk++;
			by_key = k->e[i].no;
			if (!plain)
				plain = k->e[i].no;
		}
	}
	/* A repeated name whose plain key is the name: that one (rq.ids' preferred keys). */
	if (nh > 1 && plain)
		return plain;
	if (nh == 1)
		return hits[0];
	if (nh == 0 && nk == 1)
		return by_key;
	if (nh > 1) {
		if (why) {
			char list[300] = "";
			for (int i = 0; i < nh; i++) {
				size_t o = strlen(list);
				snprintf(list + o, sizeof list - o, "%s%d", i ? ", " : "", hits[i]);
			}
			snprintf(why, n, "more than one %s is named \"%s\": %s", kind, name, list);
		}
		return 0;
	}
	/* A mod's own id is known only with the game running. */
	if (strchr(name, ':'))
		return -1;
	if (why)
		snprintf(why, n, "no %s \"%s\": a name or key as rq.ids gives it, a number, or a mod's own id", what, name);
	return 0;
}

static int l_find_game(lua_State *l)
{
	const char *kind = luaL_checkstring(l, 1);
	if (lua_isinteger(l, 2)) {
		lua_pushvalue(l, 2);
		return 1;
	}
	const char *name = luaL_checkstring(l, 2);
	char why[400];
	int no = problems_find_id(kind, name, why, sizeof why);
	if (no) {
		lua_pushinteger(l, no > 0 ? no : 1);
		return 1;
	}
	lua_pushnil(l);
	lua_pushstring(l, why);
	return 2;
}

static int l_true(lua_State *l)
{
	lua_pushboolean(l, 1);
	return 1;
}

static int l_file_exists(lua_State *l)
{
	const char *p = luaL_checkstring(l, 1);
	char full[2048];
	snprintf(full, sizeof full, "%s/%s", mod_dir_now, p);
	lua_pushboolean(l, file_exists(full));
	return 1;
}

/* ---- the list ---- */

static struct problem *add(struct problems *ps, int level, const char *at, const char *place)
{
	if (ps->n == ps->cap) {
		ps->cap = ps->cap ? ps->cap * 2 : 16;
		ps->p = realloc(ps->p, (size_t)ps->cap * sizeof *ps->p);
	}
	struct problem *p = &ps->p[ps->n++];
	memset(p, 0, sizeof *p);
	p->level = level;
	snprintf(p->at, sizeof p->at, "%s", at ? at : "");
	snprintf(p->place, sizeof p->place, "%s", place ? place : "");
	if (level == PROB_ERROR)
		ps->errors++;
	else
		ps->warnings++;
	return p;
}

static void addf(struct problems *ps, int level, const char *place, const char *fmt, ...) __attribute__((format(printf, 4, 5)));
static void addf(struct problems *ps, int level, const char *place, const char *fmt, ...)
{
	struct problem *p = add(ps, level, place, place);
	va_list ap;
	va_start(ap, fmt);
	vsnprintf(p->msg, sizeof p->msg, fmt, ap);
	va_end(ap);
}

void problems_free(struct problems *ps)
{
	free(ps->p);
	memset(ps, 0, sizeof *ps);
}

static bool take_index(const char **s, const char *head)
{
	size_t n = strlen(head);
	if (strncmp(*s, head, n))
		return false;
	const char *p = *s + n;
	if (!isdigit((unsigned char)*p))
		return false;
	while (isdigit((unsigned char)*p))
		p++;
	if (*p != ']')
		return false;
	*s = p + 1;
	return true;
}

void problem_place(const char *at, char *out, size_t n)
{
	out[0] = 0;
	if (!strncmp(at, "cast.", 5)) {
		size_t k = strcspn(at + 5, ".[");
		snprintf(out, n, "cast.%.*s", (int)k, at + 5);
		return;
	}
	if (!strcmp(at, "cast")) {
		snprintf(out, n, "cast");
		return;
	}
	const char *s = at;
	if (!take_index(&s, "steps["))
		return;
	const char *end = s;
	for (;;) {
		const char *t = s;
		if (take_index(&t, ".then[") || take_index(&t, ".else[")) {
			s = end = t;
			continue;
		}
		t = s;
		if (take_index(&t, ".choice[")) {
			const char *u = t;
			if (take_index(&u, ".steps[")) {
				s = end = u;
				continue;
			}
			if (!*t || *t == '.' || *t == '[')
				end = t;   /* the row */
		}
		break;
	}
	snprintf(out, n, "%.*s", (int)(end - at), at);
}

/* "steps[2].text: expected a text" -> at and what; a problem of the whole file has no place. */
static void split(const char *s, char *at, size_t an, const char **msg)
{
	const char *c = strstr(s, ": ");
	at[0] = 0;
	*msg = s;
	if (!c || c == s)
		return;
	for (const char *p = s; p < c; p++)
		if (!(isalnum((unsigned char)*p) || strchr("_.$[]-", *p)))
			return;
	snprintf(at, an, "%.*s", (int)(c - s), s);
	*msg = c + 2;
}

static void run_checker(const cJSON *root, const char *mod_dir, struct problems *ps)
{
	if (!checker_load()) {
		addf(ps, PROB_ERROR, "", "%s", load_why);
		return;
	}
	lua_settop(L, 0);
	lua_rawgeti(L, LUA_REGISTRYINDEX, check_ref);
	if (!push_json(root, 0)) {
		lua_settop(L, 0);
		addf(ps, PROB_ERROR, "", "nested too deep");
		return;
	}
	lua_newtable(L);
	if (G.nkinds > 0) {
		lua_pushcfunction(L, l_find_game);
		lua_setfield(L, -2, "find_game");
		lua_pushcfunction(L, l_true);
		lua_setfield(L, -2, "save_loaded");
	}
	mod_dir_now = mod_dir;
	if (mod_dir && mod_dir[0]) {
		lua_pushcfunction(L, l_file_exists);
		lua_setfield(L, -2, "file_exists");
	}
	if (game_functions_known()) {
		/* The mod's functions as the game has them: a call to another is a problem, as in the game. */
		lua_newtable(L);
		for (int i = 0, n = game_function_count(); i < n; i++) {
			lua_pushboolean(L, 1);
			lua_setfield(L, -2, game_function(i));
		}
		lua_setfield(L, -2, "functions");
	}
	if (lua_pcall(L, 2, 1, 0) != LUA_OK) {
		addf(ps, PROB_ERROR, "", "RQ MOD's checker stopped: %s", lua_tostring(L, -1));
		lua_settop(L, 0);
		return;
	}
	lua_getfield(L, -1, "problems");
	int n = (int)luaL_len(L, -1);
	for (int i = 1; i <= n; i++) {
		lua_rawgeti(L, -1, i);
		const char *s = lua_tostring(L, -1), *msg;
		char at[256], place[256];
		split(s ? s : "", at, sizeof at, &msg);
		problem_place(at, place, sizeof place);
		struct problem *p = add(ps, PROB_ERROR, at, place);
		snprintf(p->msg, sizeof p->msg, "%s", msg);
		lua_pop(L, 1);
	}
	lua_settop(L, 0);
	lua_gc(L, LUA_GCCOLLECT);
}

/* ---- the editor's own: the game's box ---- */

static uint32_t utf8_next(const char **s)
{
	const unsigned char *p = (const unsigned char *)*s;
	uint32_t c = p[0];
	int n = c < 0x80 ? 1 : (c >> 5) == 6 ? 2 : (c >> 4) == 14 ? 3 : (c >> 3) == 30 ? 4 : 1;
	if (n > 1) {
		c &= 0x3F >> (n - 1);
		for (int i = 1; i < n; i++) {
			if ((p[i] & 0xC0) != 0x80) {
				n = i;
				break;
			}
			c = (c << 6) | (p[i] & 0x3F);
		}
	}
	*s += n;
	return c;
}

/* The letters of a text the game's font cannot draw. */
static void letters(struct problems *ps, const char *place, const char *what, const char *s)
{
	if (!s || F_GAME == F_UI)
		return;
	uint32_t miss[4];
	int nm = 0, more = 0;
	for (const char *p = s; *p;) {
		uint32_t c = utf8_next(&p);
		if (c < 0x20 || c == 0x7F)
			continue;
		bool has = c <= 0xFFFF && ImFont_IsGlyphInFont(F_GAME, (ImWchar)(c == '\'' ? 0x2019 : c));
		if (has)
			continue;
		bool seen = false;
		for (int i = 0; i < nm; i++)
			seen |= miss[i] == c;
		if (seen)
			continue;
		if (nm < 4)
			miss[nm++] = c;
		else
			more++;
	}
	if (!nm)
		return;
	char list[200] = "", codes[120] = "";
	for (int i = 0; i < nm; i++) {
		char u[8] = { 0 };
		uint32_t c = miss[i];
		if (c < 0x80) u[0] = (char)c;
		else if (c < 0x800) { u[0] = (char)(0xC0 | c >> 6); u[1] = (char)(0x80 | (c & 0x3F)); }
		else if (c < 0x10000) { u[0] = (char)(0xE0 | c >> 12); u[1] = (char)(0x80 | ((c >> 6) & 0x3F)); u[2] = (char)(0x80 | (c & 0x3F)); }
		else { u[0] = (char)(0xF0 | c >> 18); u[1] = (char)(0x80 | ((c >> 12) & 0x3F)); u[2] = (char)(0x80 | ((c >> 6) & 0x3F)); u[3] = (char)(0x80 | (c & 0x3F)); }
		size_t o = strlen(list);
		snprintf(list + o, sizeof list - o, "%s\xE2\x80\x9C%s\xE2\x80\x9D", i ? ", " : "", u);
		o = strlen(codes);
		snprintf(codes + o, sizeof codes - o, "%sU+%04X", i ? ", " : "", (unsigned)c);
	}
	addf(ps, PROB_WARN, place, "%s: the game's font has no %s (%s)%s, so the game cannot show %s",
	     what, list, codes, more ? " and more" : "", nm + more == 1 ? "it" : "them");
}

/* The {name}s of a text: after {{ and }}, a { a name }. */
struct names { char (*s)[64]; int n, cap; };

static void name_add(struct names *ns, const char *s, size_t len)
{
	if (len >= 64)
		return;
	for (int i = 0; i < ns->n; i++)
		if (strlen(ns->s[i]) == len && !strncmp(ns->s[i], s, len))
			return;
	if (ns->n == ns->cap) {
		ns->cap = ns->cap ? ns->cap * 2 : 16;
		ns->s = realloc(ns->s, (size_t)ns->cap * sizeof *ns->s);
	}
	memcpy(ns->s[ns->n], s, len);
	ns->s[ns->n++][len] = 0;
}

static bool name_set(const struct names *ns, const char *s, size_t len)
{
	for (int i = 0; i < ns->n; i++)
		if (strlen(ns->s[i]) == len && !strncmp(ns->s[i], s, len))
			return true;
	return false;
}

struct walk {
	struct problems *ps;
	struct names set;
	bool sets_pass;
	const cJSON *cast;
};

static void values(struct walk *w, const char *place, const char *what, const char *s)
{
	if (!s || !strchr(s, '{'))
		return;
	/* As the loader does: every {{ out first, then every }}, then each {name} left. */
	size_t n = strlen(s);
	char *t = malloc(n + 1);
	size_t o = 0;
	for (size_t i = 0; i < n; i++) {
		if (s[i] == '{' && s[i + 1] == '{') {
			t[o++] = 1;
			i++;
		} else {
			t[o++] = s[i];
		}
	}
	t[o] = 0;
	for (size_t i = 0, k = 0; i <= o; i++)
		if (t[i] == '}' && t[i + 1] == '}') {
			t[k++] = 2;
			i++;
		} else {
			t[k++] = t[i];
		}
	struct names told = { 0 };
	for (const char *p = t; *p; p++) {
		if (*p != '{' || !(isalpha((unsigned char)p[1]) || p[1] == '_'))
			continue;
		const char *a = p + 1, *b = a;
		while (isalnum((unsigned char)*b) || *b == '_')
			b++;
		if (*b != '}')
			continue;
		size_t len = (size_t)(b - a);
		char nm[64];
		snprintf(nm, sizeof nm, "%.*s", (int)(len < 63 ? len : 63), a);
		bool given = vals_rel && values_get_for(vals_mod, vals_rel, nm);
		if (!given && !name_set(&w->set, a, len) && !name_set(&told, a, len)) {
			name_add(&told, a, len);
			addf(w->ps, PROB_WARN, place, "%s: no step sets {%.*s} (no \"into\" names it): unless the mod gives it, "
			     "the game shows \"{%.*s}\"", what, (int)len, a, (int)len, a);
		}
		p = b;
	}
	free(told.s);
	free(t);
}

static const char *str_of(const cJSON *o, const char *k)
{
	const cJSON *v = cJSON_GetObjectItemCaseSensitive(o, k);
	return cJSON_IsString(v) ? v->valuestring : NULL;
}

static void walk_step(cJSON *st, const char *place, void *ud)
{
	struct walk *w = ud;
	const char *kind = step_kind(st);
	if (!kind)
		return;
	if (w->sets_pass) {
		const char *into = str_of(st, "into");
		if (into && (!strcmp(kind, "choice") || !strcmp(kind, "call")))
			name_add(&w->set, into, strlen(into));
		return;
	}
	if (!strcmp(kind, "say") || !strcmp(kind, "think") || !strcmp(kind, "narrate")) {
		int pages = line_pages(st);
		int lim = line_limit(st);
		for (int i = 0; i < pages; i++) {
			const char *pg = line_page(st, i);
			char what[32];
			if (pages > 1)
				snprintf(what, sizeof what, "page %d", i + 1);
			else
				snprintf(what, sizeof what, "the text");
			if (!pg)
				continue;
			if (problems_box) {
				int n = page_lines(st, pg);
				if (n > lim)
					addf(w->ps, PROB_WARN, place, "%s takes %d lines: the game's box shows %d, the rest is cut off",
					     what, n, lim);
			}
			if (problems_box)
				letters(w->ps, place, what, pg);
			values(w, place, what, pg);
		}
		const char *name = str_of(st, "name");
		if (problems_box)
			letters(w->ps, place, "the name plate", name);
		values(w, place, "the name plate", name);
	} else if (!strcmp(kind, "choice")) {
		const cJSON *rows = cJSON_GetObjectItemCaseSensitive(st, "choice");
		int r = 0;
		for (const cJSON *row = cJSON_IsArray(rows) ? rows->child : NULL; row; row = row->next, r++) {
			char rp[300];
			snprintf(rp, sizeof rp, "%s.choice[%d]", place, r);
			const char *text = cJSON_IsString(row) ? (strcmp(row->valuestring, "-") ? row->valuestring : NULL)
			                                       : str_of(row, "text");
			const char *comment = cJSON_IsObject(row) ? str_of(row, "comment") : NULL;
			if (problems_box) {
				letters(w->ps, rp, "the row's text", text);
				letters(w->ps, rp, "the row's comment", comment);
			}
			values(w, rp, "the row's text", text);
			values(w, rp, "the row's comment", comment);
		}
	}
}

static void editor_checks(const cJSON *root, struct problems *ps)
{
	cJSON *steps = cJSON_GetObjectItemCaseSensitive(root, "steps");
	if (!cJSON_IsArray(steps))
		return;
	struct walk w = { .ps = ps, .cast = cJSON_GetObjectItemCaseSensitive(root, "cast") };
	w.sets_pass = true;
	steps_each(steps, "steps", walk_step, &w);
	w.sets_pass = false;
	steps_each(steps, "steps", walk_step, &w);
	if (problems_box && cJSON_IsObject(w.cast))
		for (const cJSON *e = w.cast->child; e; e = e->next) {
			char cp[300];
			snprintf(cp, sizeof cp, "cast.%s", e->string ? e->string : "");
			letters(ps, cp, "the name plate", str_of(e, "name"));
		}
	free(w.set.s);
}

void problems_check(const cJSON *root, const char *mod_dir, struct problems *ps)
{
	free(ps->p);
	memset(ps, 0, sizeof *ps);
	if (!root)
		return;
	run_checker(root, mod_dir, ps);
	editor_checks(root, ps);
}

const char *problems_lua_why(void)
{
	return checker_load() ? NULL : load_why;
}

/* ---- the open file's, checked again when it changes ---- */

static uint64_t hash(const cJSON *v, uint64_t h)
{
	for (; v; v = v->next) {
		h = (h ^ (uint64_t)(v->type & 0xFF)) * 1099511628211ULL;
		for (const char *s = v->string; s && *s; s++)
			h = (h ^ (unsigned char)*s) * 1099511628211ULL;
		if (cJSON_IsString(v))
			for (const char *s = v->valuestring; s && *s; s++)
				h = (h ^ (unsigned char)*s) * 1099511628211ULL;
		if (cJSON_IsNumber(v)) {
			uint64_t b;
			memcpy(&b, &v->valuedouble, sizeof b);
			h = (h ^ b) * 1099511628211ULL;
		}
		if (v->child)
			h = hash(v->child, h ^ 0x5bd1e995);
		h = (h ^ 0xFF) * 1099511628211ULL;
	}
	return h;
}

static struct problems now;
static uint64_t now_hash;
static double now_at;
static bool now_box;
static char now_path[1600];
static unsigned now_gen;

unsigned long long problems_doc_hash(void)
{
	return D.root ? hash(D.root, 14695981039346656037ULL) : 0;
}

/* What the game refused when Play sent the file as it is now: "scenes/a.json: steps[2]: what". */
static void game_problems(struct problems *ps, uint64_t h)
{
	const char *line;
	int n = game_refusal(ui_mod_id(), D.rel, h, &line, 0);
	for (int i = 0; i < n; i++) {
		game_refusal(ui_mod_id(), D.rel, h, &line, i);
		const char *s = line;
		size_t rl = strlen(D.rel);
		if (!strncmp(s, D.rel, rl) && !strncmp(s + rl, ": ", 2))
			s += rl + 2;
		char at[256], place[256];
		const char *msg;
		split(s, at, sizeof at, &msg);
		bool seen = false;
		for (int k = 0; k < ps->n && !seen; k++)
			seen = !strcmp(ps->p[k].at, at) && !strcmp(ps->p[k].msg, msg);
		if (seen)
			continue;
		problem_place(at, place, sizeof place);
		struct problem *p = add(ps, PROB_ERROR, at, place);
		snprintf(p->msg, sizeof p->msg, "%s (the game)", msg);
	}
}

const struct problems *problems_now(void)
{
	if (!D.open || !D.root) {
		problems_free(&now);
		now_path[0] = 0;
		return &now;
	}
	uint64_t h = hash(D.root, 14695981039346656037ULL);
	double t = igGetTime();
	unsigned g = game_gen() * 31u + values_gen();
	/* Again when the file changes, the game's functions or the values for Play, and every few
	 * seconds for the pictures' files. */
	if (h != now_hash || strcmp(now_path, D.path) || now_box != problems_box || g != now_gen || t - now_at > 3.0 || t < now_at) {
		vals_mod = ui_mod_id();
		vals_rel = D.rel;
		problems_check(D.root, media_mod_dir(), &now);
		vals_mod = vals_rel = NULL;
		game_problems(&now, h);
		now_gen = g;
		now_hash = h;
		now_at = t;
		now_box = problems_box;
		snprintf(now_path, sizeof now_path, "%s", D.path);
	}
	return &now;
}

void problems_forget(void)
{
	now_hash = 0;
	now_path[0] = 0;
}

int problems_at(const char *place, const struct problem **first)
{
	const struct problems *ps = problems_now();
	int n = 0;
	if (first)
		*first = NULL;
	for (int i = 0; i < ps->n; i++)
		if (!strcmp(ps->p[i].place, place)) {
			if (!n && first)
				*first = &ps->p[i];
			n++;
		}
	return n;
}

/* ---- each file of the list, from disk ---- */

struct counted { char path[1600]; long long mtime, size; int errors, warnings; bool box; unsigned gen; };
static struct counted *counted;
static int ncounted, capcounted;

void problems_count_file(const char *path, const char *mod_dir, long long mtime, long long size, int *errors, int *warnings)
{
	struct counted *c = NULL;
	for (int i = 0; i < ncounted; i++)
		if (!strcmp(counted[i].path, path)) {
			c = &counted[i];
			break;
		}
	unsigned g = game_gen() * 31u + values_gen();
	if (c && c->mtime == mtime && c->size == size && c->box == problems_box && c->gen == g) {
		*errors = c->errors;
		*warnings = c->warnings;
		return;
	}
	if (!c) {
		if (ncounted == capcounted) {
			capcounted = capcounted ? capcounted * 2 : 32;
			counted = realloc(counted, (size_t)capcounted * sizeof *counted);
		}
		c = &counted[ncounted++];
		memset(c, 0, sizeof *c);
		snprintf(c->path, sizeof c->path, "%s", path);
	}
	cJSON *root = NULL;
	char *raw = NULL, why[300];
	size_t rawn = 0;
	struct problems ps = { 0 };
	if (scene_read(path, &root, &raw, &rawn, why, sizeof why)) {
		/* Its values for Play: by its path in the mod's folder. */
		size_t dl = mod_dir ? strlen(mod_dir) : 0;
		if (dl && !strncmp(path, mod_dir, dl) && (path[dl] == '/' || path[dl] == '\\')) {
			vals_mod = ui_mod_id();
			vals_rel = path + dl + 1;
		}
		problems_check(root, mod_dir, &ps);
		vals_mod = vals_rel = NULL;
		c->errors = ps.errors;
		c->warnings = ps.warnings;
	} else {
		c->errors = 1;
		c->warnings = 0;
	}
	problems_free(&ps);
	cJSON_Delete(root);
	free(raw);
	c->mtime = mtime;
	c->size = size;
	c->box = problems_box;
	c->gen = g;
	*errors = c->errors;
	*warnings = c->warnings;
}
