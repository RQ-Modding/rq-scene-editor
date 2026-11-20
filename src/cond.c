/* The condition builder: a condition of a scene file (an if step's, a choice row's) as rows of checks,
 * each picked from a list (a flag set, a variable, the mod's function, a quest cleared, a character
 * recruited or in the party, an item owned, the gold, a chance), with "Not" on any of them, and "All
 * of", "Any of", "Not all of" and "None of" as groups. Quests, characters and items come from RQ MOD's
 * names (rq.ids) by their pictures and names, and are written by their key.
 *
 * A check is found by its path in the condition: "" the whole, "1" the second check of the group at
 * the top, "1.0" the first of the group that is that one. The builder's buttons ask for a change by
 * path; it is made once the builder is drawn, as --do cond= makes it. A check the builder cannot show
 * (two checks in one object, a field it does not know) shows as its JSON, kept as it is. */
#include <ctype.h>
#include <float.h>
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "system4.h"

#include "editor.h"

static cJSON *item(const cJSON *o, const char *k)
{
	return cJSON_IsObject(o) ? cJSON_GetObjectItemCaseSensitive(o, k) : NULL;
}

static void put(cJSON *o, const char *k, cJSON *v)
{
	if (cJSON_GetObjectItemCaseSensitive(o, k))
		cJSON_ReplaceItemInObjectCaseSensitive(o, k, v);
	else
		cJSON_AddItemToObject(o, k, v);
}

static void drop(cJSON *o, const char *k)
{
	cJSON_DeleteItemFromObjectCaseSensitive(o, k);
}

/* ---- the kinds of check ---- */

struct ckind {
	const char *key, *title, *tip;
	const char *ids;      /* rq.ids' kind of its value, or NULL */
};

static const struct ckind ckinds[] = {
	{ "seen", "Flag set", "An event flag is set: one of the mod's (mymod:met), or one of the game's own", NULL },
	{ "var", "Variable", "A variable of the scene: a choice's pick, what a function gave, or a value given at the start", NULL },
	{ "call", "Mod's function", "One of the mod's functions for scene files says yes (anything but nil and false)", NULL },
	{ "cleared", "Quest cleared", "A quest was cleared at least once", "quest" },
	{ "recruited", "Recruited", "A character is in the roster", "chara" },
	{ "in_party", "In the party", "A character is in the party", "chara" },
	{ "has_item", "Has the item", "The player has the item: worn, in the vault or in the storage", "item" },
	{ "gold", "Gold", "The player has at least that much gold", NULL },
	{ "chance", "Chance", "A roll: holds that many times in 100", NULL },
};
#define NCKINDS ((int)(sizeof ckinds / sizeof ckinds[0]))

static const char *const group_keys[] = { "all", "any" };

static bool is_not(const cJSON *v)
{
	return cJSON_IsObject(v) && v->child && !v->child->next && v->child->string && !strcmp(v->child->string, "not");
}

static const cJSON *strip_not(const cJSON *v, bool *neg)
{
	*neg = is_not(v);
	return *neg ? v->child : v;
}

/* A group: { "all": [...] } or { "any": [...] }; *any says which. */
static bool is_group(const cJSON *x, bool *any)
{
	if (!cJSON_IsObject(x) || !x->child || x->child->next || !x->child->string || !cJSON_IsArray(x->child))
		return false;
	bool a = !strcmp(x->child->string, "any");
	if (!a && strcmp(x->child->string, "all"))
		return false;
	if (any)
		*any = a;
	return true;
}

/* The kind of a check the builder shows (ckinds' index), or -1. */
static int kind_of(const cJSON *x)
{
	if (!cJSON_IsObject(x))
		return -1;
	int k = -1;
	for (const cJSON *c = x->child; c; c = c->next) {
		int j = -1;
		for (int i = 0; i < NCKINDS; i++)
			if (c->string && !strcmp(c->string, ckinds[i].key))
				j = i;
		if (j >= 0) {
			if (k >= 0)
				return -1;    /* two checks in one object */
			k = j;
		}
	}
	if (k < 0)
		return -1;
	for (const cJSON *c = x->child; c; c = c->next) {
		const char *s = c->string ? c->string : "";
		if (!strcmp(s, ckinds[k].key))
			continue;
		if (!strcmp(ckinds[k].key, "var") && (!strcmp(s, "is") || !strcmp(s, "at_least") || !strcmp(s, "at_most")))
			continue;
		if (!strcmp(ckinds[k].key, "call") && !strcmp(s, "args") && cJSON_IsArray(c))
			continue;
		return -1;
	}
	return k;
}

static int ckind_find(const char *key)
{
	for (int i = 0; i < NCKINDS; i++)
		if (!strcmp(ckinds[i].key, key))
			return i;
	return -1;
}

/* ---- RQ MOD's names ---- */

static const struct id_kind *ids_kind(const char *kind)
{
	for (int k = 0; kind && k < G.nkinds; k++)
		if (!strcmp(G.kinds[k].kind, kind))
			return &G.kinds[k];
	return NULL;
}

static bool same_ci(const char *a, const char *b)
{
	for (; *a && *b; a++, b++)
		if (tolower((unsigned char)*a) != tolower((unsigned char)*b))
			return false;
	return !*a && !*b;
}

/* s starts with what, ignoring case. */
static bool ci_prefix(const char *s, const char *what)
{
	for (; *what; s++, what++)
		if (!*s || tolower((unsigned char)*s) != tolower((unsigned char)*what))
			return false;
	return true;
}

/* A quest, character or item value as rq.ids names it: a number, a key or a name. */
static const struct id_entry *ids_find(const char *kind, const cJSON *v)
{
	const struct id_kind *k = ids_kind(kind);
	if (!k || !v)
		return NULL;
	for (int i = 0; i < k->n; i++) {
		const struct id_entry *e = &k->e[i];
		if (cJSON_IsNumber(v) ? e->no == (int)v->valuedouble
		                      : cJSON_IsString(v) && (!strcmp(e->key, v->valuestring) || same_ci(e->name, v->valuestring)))
			return e;
	}
	return NULL;
}

/* ---- in words, for the list ---- */

struct out { char *p; size_t n, o; };

static void addf(struct out *o, const char *fmt, ...) __attribute__((format(printf, 2, 3)));
static void addf(struct out *o, const char *fmt, ...)
{
	if (o->o + 1 >= o->n)
		return;
	va_list ap;
	va_start(ap, fmt);
	int k = vsnprintf(o->p + o->o, o->n - o->o, fmt, ap);
	va_end(ap);
	if (k > 0)
		o->o = o->o + (size_t)k < o->n ? o->o + (size_t)k : o->n - 1;
}

static void add_value(struct out *o, const cJSON *v)
{
	if (cJSON_IsString(v)) {
		addf(o, "%s", v->valuestring);
	} else {
		char *j = v ? json_inline(v) : NULL;
		addf(o, "%s", j ? j : "?");
		free(j);
	}
}

static void add_thing(struct out *o, const char *kind, const cJSON *v)
{
	const struct id_entry *e = ids_find(kind, v);
	if (e)
		addf(o, "%s", e->name);
	else if (cJSON_IsString(v) && !v->valuestring[0])
		addf(o, "?");
	else
		add_value(o, v);
}

static void words(struct out *o, const cJSON *c, bool inner)
{
	bool neg;
	const cJSON *x = strip_not(c, &neg);
	bool any;
	if (neg) {
		addf(o, "not ");
		if (is_group(x, NULL) || is_not(x)) {
			addf(o, "(");
			words(o, x, false);
			addf(o, ")");
			return;
		}
	}
	if (is_group(x, &any)) {
		const cJSON *l = x->child;
		int n = cJSON_GetArraySize(l);
		/* Two or three short checks read as a sentence; more, as their count. */
		char tmp[200];
		struct out t = { tmp, sizeof tmp, 0 };
		int k = 0;
		for (const cJSON *e = l->child; e; e = e->next, k++) {
			if (k)
				addf(&t, any ? " or " : " and ");
			words(&t, e, true);
		}
		bool fits = n >= 1 && n <= 3 && t.o < 70 && t.o + 1 < sizeof tmp;
		if (fits && inner && n > 1)
			addf(o, "(%s)", tmp);
		else if (fits)
			addf(o, "%s", tmp);
		else
			addf(o, "%s of %d", any ? "any" : "all", n);
		return;
	}
	int k = kind_of(x);
	if (k < 0) {
		add_value(o, x);
		return;
	}
	const char *key = ckinds[k].key;
	const cJSON *v = item(x, key);
	if (!strcmp(key, "seen")) {
		addf(o, "flag ");
		add_value(o, v);
	} else if (!strcmp(key, "var")) {
		add_value(o, v);
		const cJSON *is = item(x, "is"), *lo = item(x, "at_least"), *hi = item(x, "at_most");
		if (cJSON_IsNull(is))
			addf(o, " not set");
		else if (is) {
			addf(o, " is ");
			add_value(o, is);
		}
		if (lo && hi) {
			addf(o, " ");
			add_value(o, lo);
			addf(o, " to ");
			add_value(o, hi);
		} else if (lo) {
			addf(o, " at least ");
			add_value(o, lo);
		} else if (hi) {
			addf(o, " at most ");
			add_value(o, hi);
		}
	} else if (!strcmp(key, "call")) {
		add_value(o, v);
		const cJSON *args = item(x, "args");
		char *j = cJSON_IsArray(args) ? json_inline(args) : NULL;
		size_t len = j ? strlen(j) : 0;
		if (len >= 2) {
			j[len - 1] = 0;
			addf(o, "(%s)", j + 1);
		} else {
			addf(o, "()");
		}
		free(j);
	} else if (!strcmp(key, "cleared")) {
		addf(o, "cleared ");
		add_thing(o, "quest", v);
	} else if (!strcmp(key, "recruited")) {
		add_thing(o, "chara", v);
		addf(o, " recruited");
	} else if (!strcmp(key, "in_party")) {
		add_thing(o, "chara", v);
		addf(o, " in the party");
	} else if (!strcmp(key, "has_item")) {
		addf(o, "has ");
		add_thing(o, "item", v);
	} else if (!strcmp(key, "gold")) {
		addf(o, "gold ");
		add_value(o, v);
	} else if (!strcmp(key, "chance")) {
		add_value(o, v);
		addf(o, "%% chance");
	}
}

void cond_words(const cJSON *c, char *buf, size_t n)
{
	struct out o = { buf, n, 0 };
	if (n)
		buf[0] = 0;
	if (c)
		words(&o, c, false);
}

/* ---- the file's own names: flags, variables, functions ---- */

struct names { char (*s)[128]; int n, cap; };

static void names_add(struct names *ns, const char *s)
{
	if (!s || !s[0])
		return;
	for (int i = 0; i < ns->n; i++)
		if (!strcmp(ns->s[i], s))
			return;
	if (ns->n == ns->cap) {
		ns->cap = ns->cap ? ns->cap * 2 : 32;
		ns->s = xrealloc(ns->s, (size_t)ns->cap * sizeof *ns->s);
	}
	snprintf(ns->s[ns->n++], 128, "%s", s);
}

static int names_cmp(const void *a, const void *b)
{
	return strcmp((const char *)a, (const char *)b);
}

enum { N_FLAGS, N_VARS, N_FUNCS };

static void collect(const cJSON *v, struct names *ns, int what)
{
	if (cJSON_IsArray(v)) {
		for (const cJSON *c = v->child; c; c = c->next)
			collect(c, ns, what);
		return;
	}
	if (!cJSON_IsObject(v))
		return;
	static const char *const keys[3][3] = { { "seen", "flag", "unflag" }, { "into", "var", NULL }, { "call", NULL, NULL } };
	for (int i = 0; i < 3; i++) {
		const cJSON *s = keys[what][i] ? item(v, keys[what][i]) : NULL;
		if (cJSON_IsString(s))
			names_add(ns, s->valuestring);
	}
	for (const cJSON *c = v->child; c; c = c->next)
		if (cJSON_IsObject(c) || cJSON_IsArray(c))
			collect(c, ns, what);
}

static struct names file_names(int what)
{
	struct names ns = { 0 };
	if (D.root)
		collect(item(D.root, "steps"), &ns, what);
	if (ns.n)
		qsort(ns.s, (size_t)ns.n, sizeof *ns.s, names_cmp);
	return ns;
}

/* ---- finding a check by its path ---- */

struct slot { cJSON *parent; const char *key; int index; };   /* parent[key], or parent's [index] */

static cJSON *slot_get(struct slot s)
{
	return s.key ? item(s.parent, s.key) : cJSON_GetArrayItem(s.parent, s.index);
}

static void slot_set(struct slot s, cJSON *v)
{
	if (s.key)
		put(s.parent, s.key, v);
	else
		cJSON_ReplaceItemInArray(s.parent, s.index, v);
}

/* A group's list, through its not. */
static cJSON *group_list(cJSON *v, bool *any)
{
	bool neg;
	cJSON *x = (cJSON *)strip_not(v, &neg);
	return is_group(x, any) ? x->child : NULL;
}

static bool resolve(cJSON *owner, const char *key, const char *path, struct slot *out)
{
	struct slot s = { owner, key, -1 };
	if (!slot_get(s))
		return false;
	for (const char *p = path; *p;) {
		char *end;
		long i = strtol(p, &end, 10);
		if (end == p || i < 0)
			return false;
		cJSON *l = group_list(slot_get(s), NULL);
		if (!l || i >= cJSON_GetArraySize(l))
			return false;
		s.parent = l;
		s.key = NULL;
		s.index = (int)i;
		p = *end == '.' ? end + 1 : end;
		if (*end && *end != '.')
			return false;
	}
	*out = s;
	return true;
}

static void child_path(char *out, size_t n, const char *path, int i)
{
	snprintf(out, n, "%s%s%d", path, path[0] ? "." : "", i);
}

static bool parent_path(const char *path, char *out, size_t n, int *index)
{
	if (!path[0])
		return false;
	const char *dot = strrchr(path, '.');
	snprintf(out, n, "%.*s", dot ? (int)(dot - path) : 0, path);
	*index = atoi(dot ? dot + 1 : path);
	return true;
}

/* ---- the changes ---- */

static cJSON *negate(cJSON *v)
{
	if (is_not(v)) {
		cJSON *in = cJSON_Duplicate(v->child, true);
		cJSON_Delete(v);
		return in;
	}
	cJSON *n = cJSON_CreateObject();
	cJSON_AddItemToObject(n, "not", v);
	return n;
}

static cJSON *check_new(int k)
{
	cJSON *c = cJSON_CreateObject();
	const char *key = ckinds[k].key;
	if (!strcmp(key, "var")) {
		struct names ns = file_names(N_VARS);
		cJSON_AddStringToObject(c, "var", ns.n ? ns.s[0] : "pick");
		free(ns.s);
		cJSON_AddNumberToObject(c, "is", 1);
	} else if (!strcmp(key, "gold")) {
		cJSON_AddNumberToObject(c, "gold", 1000);
	} else if (!strcmp(key, "chance")) {
		cJSON_AddNumberToObject(c, "chance", 50);
	} else {
		cJSON_AddStringToObject(c, key, "");
	}
	return c;
}

/* The kind a new check of a group takes: the last check's, else a flag. */
static int kind_like(const cJSON *list_or_check)
{
	const cJSON *last = cJSON_IsArray(list_or_check) ? list_or_check->child : list_or_check;
	while (cJSON_IsArray(list_or_check) && last && last->next)
		last = last->next;
	bool neg;
	int k = kind_of(strip_not(last, &neg));
	return k >= 0 ? k : 0;
}

static const char *const op_names[COND_OPS] = { "add", "group", "del", "not", "kind", "mode", "value" };

bool cond_apply(cJSON *owner, const char *key, bool required, const char *path, int op, const char *arg, char *why, size_t n)
{
	struct slot s;
	cJSON *root = item(owner, key);
	if (why && n)
		why[0] = 0;
	if (op == COND_ADD || op == COND_GROUP) {
		int k = arg && arg[0] ? ckind_find(arg) : -1;
		if (!root) {
			put(owner, key, check_new(k >= 0 ? k : 0));
			return true;
		}
		if (!resolve(owner, key, path, &s))
			return false;
		cJSON *v = slot_get(s);
		bool any = false;
		cJSON *l = group_list(v, &any);
		if (!l && path[0])
			return false;   /* a check's own: only a group takes another */
		cJSON *add;
		if (k < 0)
			k = kind_like(l ? l : v);
		if (op == COND_GROUP) {
			add = cJSON_CreateObject();
			cJSON *gl = cJSON_CreateArray();
			cJSON_AddItemToArray(gl, check_new(k));
			cJSON_AddItemToObject(add, l ? group_keys[!any] : "any", gl);
		} else {
			add = check_new(k);
		}
		if (l) {
			cJSON_AddItemToArray(l, add);
		} else {
			/* One check at the top: both in an "all of". */
			cJSON *g = cJSON_CreateObject(), *gl = cJSON_CreateArray();
			cJSON_AddItemToArray(gl, cJSON_Duplicate(v, true));
			cJSON_AddItemToArray(gl, add);
			cJSON_AddItemToObject(g, "all", gl);
			slot_set(s, g);
		}
		return true;
	}
	if (!resolve(owner, key, path, &s))
		return false;
	cJSON *v = slot_get(s);
	switch (op) {
	case COND_DEL: {
		char pp[64];
		int i;
		if (!parent_path(path, pp, sizeof pp, &i)) {
			if (required) {
				if (why)
					snprintf(why, n, "An if keeps its condition: change its checks instead");
				return false;
			}
			drop(owner, key);
			return true;
		}
		struct slot ps;
		if (!resolve(owner, key, pp, &ps))
			return false;
		cJSON *pv = slot_get(ps);
		bool neg = is_not(pv);
		cJSON *l = group_list(pv, NULL);
		int left = cJSON_GetArraySize(l) - 1;
		if (left == 0)
			return cond_apply(owner, key, required, pp, COND_DEL, NULL, why, n);
		cJSON_DeleteItemFromArray(l, i);
		if (left == 1 && !pp[0]) {
			/* One check left at the top: it stands alone. */
			cJSON *one = cJSON_Duplicate(l->child, true);
			slot_set(ps, neg ? negate(one) : one);
		}
		return true;
	}
	case COND_NOT:
		slot_set(s, negate(cJSON_Duplicate(v, true)));
		return true;
	case COND_KIND: {
		int k = arg ? ckind_find(arg) : -1;
		if (k < 0)
			return false;
		bool neg;
		const cJSON *x = strip_not(v, &neg);
		if (is_group(x, NULL))
			return false;
		cJSON *c = check_new(k);
		/* A character stays the same from Recruited to In the party. */
		int was = kind_of(x);
		if (was >= 0 && ckinds[was].ids && ckinds[k].ids && !strcmp(ckinds[was].ids, ckinds[k].ids))
			put(c, ckinds[k].key, cJSON_Duplicate(item(x, ckinds[was].key), true));
		slot_set(s, neg ? negate(c) : c);
		return true;
	}
	case COND_MODE: {
		bool any;
		cJSON *l = group_list(v, &any);
		if (!l || !arg)
			return false;
		bool to_any = !strcmp(arg, "any") || !strcmp(arg, "none");
		bool to_neg = !strcmp(arg, "notall") || !strcmp(arg, "none");
		if (!to_any && !to_neg && strcmp(arg, "all"))
			return false;
		cJSON *g = cJSON_CreateObject();
		cJSON_AddItemToObject(g, group_keys[to_any], cJSON_Duplicate(l, true));
		slot_set(s, to_neg ? negate(g) : g);
		return true;
	}
	case COND_VALUE: {
		cJSON *nv = NULL;
		char w[256];
		if (!arg || !scene_parse(arg, &nv, w, sizeof w)) {
			if (why)
				snprintf(why, n, "%s", arg ? w : "no JSON");
			return false;
		}
		slot_set(s, nv);
		return true;
	}
	}
	return false;
}

bool cond_do(cJSON *owner, const char *key, bool required, const char *spec, char *why, size_t n)
{
	/* OP:PATH[:ARG] */
	char opname[16], path[64];
	const char *c1 = strchr(spec, ':');
	snprintf(opname, sizeof opname, "%.*s", c1 ? (int)(c1 - spec) : (int)strlen(spec), spec);
	const char *rest = c1 ? c1 + 1 : "";
	const char *c2 = strchr(rest, ':');
	snprintf(path, sizeof path, "%.*s", c2 ? (int)(c2 - rest) : (int)strlen(rest), rest);
	const char *arg = c2 ? c2 + 1 : NULL;
	for (int op = 0; op < COND_OPS; op++)
		if (!strcmp(op_names[op], opname))
			return cond_apply(owner, key, required, path, op, arg, why, n);
	if (why && n)
		snprintf(why, n, "no change %s", opname);
	return false;
}

/* ---- drawing ---- */

static struct {
	bool on;
	char path[64];
	int op;
	char arg[256];
} pend;

static void ask(const char *path, int op, const char *arg)
{
	pend.on = true;
	snprintf(pend.path, sizeof pend.path, "%s", path);
	pend.op = op;
	snprintf(pend.arg, sizeof pend.arg, "%s", arg ? arg : "");
}

static bool ro;
static float X0, R;            /* the builder's left edge and the right edge, in the window's x */
#define IND S(22)              /* a group's checks, further in */
#define DEL_W S(26)

static float fs(float base)
{
	return base * CORE.scale;
}

static void small_text(int c, const char *s)
{
	igPushFont(NULL, 14);
	igTextColored(colv(c), "%s", s);
	igPopFont();
}

/* A line under a check, where its fields start. */
static void under(float x)
{
	igSetCursorPosX(x);
}

static bool del_button(const char *tip, bool can)
{
	igSameLine(R - DEL_W, 0);
	igBeginDisabled(ro || !can);
	bool r = igButton("\xC3\x97##del", v2(DEL_W, 0));
	igEndDisabled();
	if (igIsItemHovered(ImGuiHoveredFlags_DelayShort | ImGuiHoveredFlags_AllowWhenDisabled))
		igSetTooltip("%s", tip);
	return r;
}

/* "Not": lit when the check (or group) is turned around. */
static bool not_button(bool on)
{
	if (on) {
		igPushStyleColor_Vec4(ImGuiCol_Button, colv(C_ACCENT));
		igPushStyleColor_Vec4(ImGuiCol_ButtonHovered, colv(C_ACCENT_HI));
		igPushStyleColor_Vec4(ImGuiCol_Text, colv(C_ACCENT_INK));
	} else {
		ImVec4_c clear = { 0, 0, 0, 0 };
		igPushStyleColor_Vec4(ImGuiCol_Button, clear);
		igPushStyleColor_Vec4(ImGuiCol_ButtonHovered, colv(C_BTN_HI));
		igPushStyleColor_Vec4(ImGuiCol_Text, colv(C_FAINT));
	}
	igBeginDisabled(ro);
	bool r = igButton("Not##not", v2(S(44), 0));
	igEndDisabled();
	igPopStyleColor(3);
	if (igIsItemHovered(ImGuiHoveredFlags_DelayShort))
		igSetTooltip(on ? "Turned around: holds when the check does not. Click to take Not away."
		                : "Click to turn it around: it holds when the check does not");
	return r;
}

static char scratch[4096];

/* A text value, typed; the file follows as it is typed. */
static void text_in(const char *id, cJSON *x, const char *k, const char *hint, float w, bool mono)
{
	cJSON *v = item(x, k);
	snprintf(scratch, sizeof scratch, "%s", cJSON_IsString(v) ? v->valuestring : "");
	igSetNextItemWidth(w);
	if (mono)
		igPushFont(F_MONO, MONO_SIZE);
	bool ch = igInputTextWithHint(id, hint, scratch, sizeof scratch, ro ? ImGuiInputTextFlags_ReadOnly : 0, NULL, NULL);
	if (mono)
		igPopFont();
	if (ch) {
		put(x, k, cJSON_CreateString(scratch));
		field_typed();
	}
	field_left();
}

/* A whole number, typed (0 when empty). */
static void number_in(const char *id, cJSON *x, const char *k, const char *hint, float w)
{
	cJSON *v = item(x, k);
	if (cJSON_IsNumber(v))
		snprintf(scratch, 64, "%.15g", v->valuedouble);
	else
		scratch[0] = 0;
	igSetNextItemWidth(w);
	if (igInputTextWithHint(id, hint, scratch, 64, ImGuiInputTextFlags_CharsDecimal | (ro ? ImGuiInputTextFlags_ReadOnly : 0), NULL, NULL)) {
		char *end = NULL;
		double d = strtod(scratch, &end);
		put(x, k, cJSON_CreateNumber(end && end != scratch ? floor(d) : 0));
		field_typed();
	}
	field_left();
	if (v && !cJSON_IsNumber(v)) {
		igSameLine(0, S(6));
		small_text(C_BAD, "not a number");
	}
}

/* A field's width: want, or what is left before × with the words after it (a nested row is narrow). */
static float fit_w(float want, const char *after)
{
	float room = R - DEL_W - S(8) - igGetCursorPosX() - (after ? S(6) + igCalcTextSize(after, NULL, false, -1).x : 0);
	if (room < S(44))
		room = S(44);
	return want < room ? want : room;
}

/* ▾ beside a name field: the names the file already uses. */
static void suggestions(const char *id, cJSON *x, const char *k, int what, const char *none)
{
	igSameLine(0, S(4));
	igSetNextItemWidth(S(28));
	igBeginDisabled(ro);
	if (igBeginCombo(id, "", ImGuiComboFlags_NoPreview | ImGuiComboFlags_HeightLarge | ImGuiComboFlags_PopupAlignLeft)) {
		struct names ns = file_names(what);
		const cJSON *v = item(x, k);
		for (int i = 0; i < ns.n; i++)
			if (igSelectable_Bool(ns.s[i], cJSON_IsString(v) && !strcmp(v->valuestring, ns.s[i]), 0, v2(0, 0))) {
				put(x, k, cJSON_CreateString(ns.s[i]));
				field_picked();
			}
		if (!ns.n)
			igTextColored(colv(C_FAINT), "%s", none);
		free(ns.s);
		igEndCombo();
	}
	igEndDisabled();
	if (igIsItemHovered(ImGuiHoveredFlags_DelayShort))
		igSetTooltip("%s", "The names this file already uses");
}

/* The values a function gets: each with its type, × to take it out, and + Value. */
static void args_in(cJSON *x)
{
	cJSON *args = item(x, "args");
	int n = cJSON_IsArray(args) ? cJSON_GetArraySize(args) : 0;
	float x1 = igGetCursorPosX();
	for (int i = 0; i < n; i++) {
		under(x1);
		igPushID_Int(i);
		field_value("##arg", args, NULL, i, R - DEL_W - S(8) - igGetCursorPosX());
		igSameLine(R - DEL_W, 0);
		igBeginDisabled(ro);
		bool del = igButton("\xC3\x97##argdel", v2(DEL_W, 0));
		igEndDisabled();
		igPopID();
		if (del) {
			cJSON_DeleteItemFromArray(args, i);
			if (!cJSON_GetArraySize(args))
				drop(x, "args");
			field_picked();
			return;
		}
	}
	under(x1);
	igBeginDisabled(ro);
	if (igSmallButton("+ Value")) {
		if (!cJSON_IsArray(args)) {
			args = cJSON_CreateArray();
			put(x, "args", args);
		}
		cJSON_AddItemToArray(args, cJSON_CreateNumber(0));
		field_picked();
	}
	igEndDisabled();
	if (igIsItemHovered(ImGuiHoveredFlags_DelayShort))
		igSetTooltip("A value the function gets, after the scene's own");
}

/* A quest, a character or an item: a list of RQ MOD's names with a search, and a mod's own id. */
static char thing_find[64], thing_own[128];

static void thing_in(cJSON *x, const char *k, const char *kind, float w)
{
	const struct id_kind *ik = ids_kind(kind);
	cJSON *v = item(x, k);
	const struct id_entry *cur = ids_find(kind, v);
	bool chara = !strcmp(kind, "chara");
	const char *what = !strcmp(kind, "quest") ? "quest" : chara ? "character" : "item";
	igSetNextItemWidth(w);
	igBeginDisabled(ro);
	igSetNextWindowSizeConstraints(v2(S(320), 0), v2(FLT_MAX, FLT_MAX), NULL, NULL);
	bool open = igBeginCombo("##thing", "", ImGuiComboFlags_CustomPreview | ImGuiComboFlags_HeightLargest);
	if (open) {
		if (igIsWindowAppearing()) {
			thing_find[0] = 0;
			snprintf(thing_own, sizeof thing_own, "%s", cJSON_IsString(v) && !cur ? v->valuestring : "");
			igSetKeyboardFocusHere(0);
		}
		igSetNextItemWidth(-1);
		igInputTextWithHint("##find", "a number or a name", thing_find, sizeof thing_find, 0, NULL, NULL);
		igBeginChild_Str("##list", v2(0, S(chara ? 330 : 300)), 0, 0);
		int shown = 0;
		char *endp = NULL;
		long want = strtol(thing_find, &endp, 10);
		bool by_no = thing_find[0] && endp && !*endp;
		for (int i = 0; ik && i < ik->n; i++) {
			const struct id_entry *e = &ik->e[i];
			if (thing_find[0]) {
				char no[16];
				snprintf(no, sizeof no, "%d", e->no);
				bool hit = by_no ? !strncmp(no, thing_find, strlen(thing_find)) && want >= 0 : false;
				if (!hit) {
					/* Any part of the name or the key, ignoring case. */
					for (const char *p = e->name; *p && !hit; p++)
						hit = ci_prefix(p, thing_find);
					for (const char *p = e->key; *p && !hit; p++)
						hit = ci_prefix(p, thing_find);
				}
				if (!hit)
					continue;
			}
			shown++;
			igPushID_Int(i);
			float h = chara ? S(34) : igGetTextLineHeightWithSpacing();
			V2 p = igGetCursorScreenPos();
			if (igSelectable_Bool("##e", e == cur, 0, v2(0, h))) {
				put(x, k, cJSON_CreateString(e->key));
				field_picked();
				igCloseCurrentPopup();
			}
			ImDrawList *dl = igGetWindowDrawList();
			float tx = p.x + S(4);
			if (chara) {
				static cJSON *cg;
				if (!cg)
					cg = cJSON_CreateNumber(0);
				cJSON_SetNumberValue(cg, e->cg);
				if (e->cg > 0)
					draw_picture_small(dl, cg, p, v2(p.x + S(28), p.y + h - S(2)), true);
				tx += S(34);
			}
			char line[300];
			snprintf(line, sizeof line, "%d", e->no);
			float size = igGetFontSize(), ty = p.y + (h - size) / 2;
			ImDrawList_AddText_FontPtr(dl, F_MONO, fs(13), v2(tx, ty + S(1)), col(C_FAINT), line, NULL, 0, NULL);
			ImDrawList_AddText_FontPtr(dl, F_UI, size, v2(tx + S(40), ty), col(C_TEXT), e->name, NULL, 0, NULL);
			if (e->detail && e->detail[0]) {
				float nw = ImFont_CalcTextSizeA(F_UI, size, FLT_MAX, 0, e->name, NULL, NULL).x;
				ImDrawList_AddText_FontPtr(dl, F_UI, fs(13), v2(tx + S(40) + nw + S(10), ty + S(1)), col(C_FAINT), e->detail, NULL, 0, NULL);
			}
			if (e == cur && igIsWindowAppearing())
				igSetScrollHereY(0.4f);
			igPopID();
		}
		if (!shown)
			igTextColored(colv(C_FAINT), "No %s of RQ MOD's names matches", what);
		igEndChild();
		igSeparator();
		igAlignTextToFramePadding();
		igTextColored(colv(C_DIM), "A mod's own %s:", what);
		igSameLine(0, S(6));
		igSetNextItemWidth(-S(60));
		igPushFont(F_MONO, MONO_SIZE);
		igInputTextWithHint("##own", "mymod:its_id", thing_own, sizeof thing_own, 0, NULL, NULL);
		igPopFont();
		igSameLine(0, S(6));
		igBeginDisabled(!thing_own[0]);
		if (igButton("Use", v2(-1, 0))) {
			put(x, k, cJSON_CreateString(thing_own));
			field_picked();
			igCloseCurrentPopup();
		}
		igEndDisabled();
		igEndCombo();
	}
	if (igBeginComboPreview()) {
		if (cur) {
			if (chara && cur->cg > 0) {
				V2 p = igGetCursorScreenPos();
				float fh = igGetFrameHeight() - S(6);
				cJSON *cg = cJSON_CreateNumber(cur->cg);
				draw_picture_small(igGetWindowDrawList(), cg, v2(p.x, p.y - S(1)), v2(p.x + fh * 0.85f, p.y + fh - S(1)), true);
				cJSON_Delete(cg);
				igDummy(v2(fh * 0.85f, 1));
				igSameLine(0, S(6));
			}
			igTextUnformatted(cur->name, NULL);
			igSameLine(0, S(8));
			igTextColored(colv(C_FAINT), "%d", cur->no);
		} else if (cJSON_IsString(v) && v->valuestring[0]) {
			igPushFont(F_MONO, MONO_SIZE);
			igTextUnformatted(v->valuestring, NULL);
			igPopFont();
			igSameLine(0, S(8));
			igTextColored(colv(C_FAINT), "a mod's own");
		} else if (cJSON_IsNumber(v)) {
			igText("%d", (int)v->valuedouble);
			igSameLine(0, S(8));
			igTextColored(colv(C_FAINT), "not in RQ MOD's names");
		} else {
			igTextColored(colv(C_BAD), "Pick %s %s\xE2\x80\xA6", what[0] == 'i' ? "an" : "a", what);
		}
		igEndComboPreview();
	}
	igEndDisabled();
}

static void node(cJSON *owner, const char *key, bool required, const char *path, int depth);

static char json_for[96], json_buf[2048], json_why[256], json_why_for[64];

/* A check the builder cannot show: its JSON on one line, read when the field is left. */
static void unknown_check(cJSON *v, const char *path, float x)
{
	char k[96];
	snprintf(k, sizeof k, "%s#%d", path, D.at);
	if (strcmp(json_for, k) && igGetActiveID() != igGetID_Str("##json")) {
		char *j = json_inline(v);
		snprintf(json_buf, sizeof json_buf, "%s", j);
		free(j);
		snprintf(json_for, sizeof json_for, "%s", k);
		json_why[0] = 0;
	}
	igSetNextItemWidth(R - DEL_W - S(8) - x);
	igPushFont(F_MONO, MONO_SIZE);
	igInputText("##json", json_buf, sizeof json_buf, ro ? ImGuiInputTextFlags_ReadOnly : 0, NULL, NULL);
	igPopFont();
	if (igIsItemDeactivatedAfterEdit()) {
		char w[256];
		cJSON *nv = NULL;
		if (scene_parse(json_buf, &nv, w, sizeof w)) {
			cJSON_Delete(nv);
			ask(path, COND_VALUE, json_buf);
			json_why[0] = 0;
			json_for[0] = 0;
		} else {
			snprintf(json_why, sizeof json_why, "Not applied: %s", w);
			snprintf(json_why_for, sizeof json_why_for, "%s", path);
		}
	}
}

static const char *const var_modes[] = { "is", "is not set", "is at least", "is at most", "is from" };

static int var_mode(const cJSON *x)
{
	const cJSON *is = item(x, "is"), *lo = item(x, "at_least"), *hi = item(x, "at_most");
	if (lo && hi)
		return 4;
	if (lo)
		return 2;
	if (hi)
		return 3;
	if (cJSON_IsNull(is))
		return 1;
	return 0;
}

static void var_set_mode(cJSON *x, int m)
{
	const cJSON *old[3] = { item(x, "is"), item(x, "at_least"), item(x, "at_most") };
	double n = 1;
	for (int i = 0; i < 3; i++)
		if (cJSON_IsNumber(old[i])) {
			n = old[i]->valuedouble;
			break;
		}
	drop(x, "is");
	drop(x, "at_least");
	drop(x, "at_most");
	if (m == 0)
		cJSON_AddNumberToObject(x, "is", n);
	else if (m == 1)
		cJSON_AddNullToObject(x, "is");
	else if (m == 2)
		cJSON_AddNumberToObject(x, "at_least", n);
	else if (m == 3)
		cJSON_AddNumberToObject(x, "at_most", n);
	else {
		cJSON_AddNumberToObject(x, "at_least", n);
		cJSON_AddNumberToObject(x, "at_most", n + 1);
	}
}

static bool name_ok(const char *s)
{
	if (!s[0] || !(isalpha((unsigned char)s[0]) || s[0] == '_'))
		return false;
	for (const char *p = s; *p; p++)
		if (!(isalnum((unsigned char)*p) || *p == '_'))
			return false;
	return true;
}

/* One check: Not, its kind, its value, ×; more of its fields on the lines under it. */
static void check_row(const char *path, int depth, cJSON *v, bool can_del, const char *del_tip)
{
	bool neg;
	cJSON *x = (cJSON *)strip_not(v, &neg);
	int k = kind_of(x);
	bool del_drawn = false;
	float x_line = X0 + (float)depth * IND;
	under(x_line);
	if (not_button(neg))
		ask(path, COND_NOT, NULL);
	igSameLine(0, S(6));
	float x_kind = igGetCursorPosX();
	char bad[200] = "";
	if (k < 0) {
		unknown_check(x, path, x_kind);
		if (json_why[0] && !strcmp(json_why_for, path))
			snprintf(bad, sizeof bad, "%s", json_why);
		else
			snprintf(bad, sizeof bad, "Not a check the builder shows: its JSON, kept as it is");
	} else {
		igSetNextItemWidth(S(128));
		igBeginDisabled(ro);
		if (igBeginCombo("##kind", ckinds[k].title, ImGuiComboFlags_HeightLarge)) {
			for (int i = 0; i < NCKINDS; i++) {
				if (igSelectable_Bool(ckinds[i].title, i == k, 0, v2(0, 0)) && i != k)
					ask(path, COND_KIND, ckinds[i].key);
				if (igIsItemHovered(ImGuiHoveredFlags_DelayShort))
					igSetTooltip("%s", ckinds[i].tip);
			}
			igEndCombo();
		}
		igEndDisabled();
		if (igIsItemHovered(ImGuiHoveredFlags_DelayShort))
			igSetTooltip("%s", ckinds[k].tip);
		igSameLine(0, S(6));
		const char *ck = ckinds[k].key;
		float x_val = igGetCursorPosX();
		float w = R - DEL_W - S(8) - x_val;
		cJSON *val = item(x, ck);
		/* The value in a narrow row (a nested group): on the line under the kind, × staying on this
		 * one. */
		float need = !strcmp(ck, "var") ? S(250) : !strcmp(ck, "gold") ? S(200) : !strcmp(ck, "chance") ? S(140) : S(170);
		if (w < need) {
			if (del_button(del_tip, can_del))
				ask(path, COND_DEL, NULL);
			del_drawn = true;
			under(x_kind);
			w = R - x_kind;
		}
		if (!strcmp(ck, "seen")) {
			char hint[160];
			snprintf(hint, sizeof hint, "%s:a_name", ui_mod_id());
			text_in("##seen", x, "seen", hint, w - S(32), true);
			suggestions("##flags", x, "seen", N_FLAGS, "No flag in this file yet");
			if (!cJSON_IsString(val) || !val->valuestring[0])
				snprintf(bad, sizeof bad, "A flag's name: the mod's own, such as %s:met, or one of the game's", ui_mod_id());
		} else if (!strcmp(ck, "var")) {
			text_in("##var", x, "var", "a name", S(110), true);
			suggestions("##vars", x, "var", N_VARS, "No variable in this file yet (a choice's or a function's into)");
			igSameLine(0, S(6));
			int m = var_mode(x);
			igSetNextItemWidth(R - DEL_W - S(8) - igGetCursorPosX());
			igBeginDisabled(ro);
			if (igBeginCombo("##mode", var_modes[m], 0)) {
				for (int i = 0; i < 5; i++)
					if (igSelectable_Bool(var_modes[i], i == m, 0, v2(0, 0)) && i != m) {
						var_set_mode(x, i);
						field_picked();
					}
				igEndCombo();
			}
			igEndDisabled();
			if (!cJSON_IsString(val) || !name_ok(val->valuestring))
				snprintf(bad, sizeof bad, "A variable's name: letters, digits and _, not starting with a digit");
		} else if (!strcmp(ck, "call")) {
			text_in("##call", x, "call", "a function's name", w - S(32), true);
			suggestions("##fns", x, "call", N_FUNCS, "No function named in this file yet");
			if (!cJSON_IsString(val) || !name_ok(val->valuestring))
				snprintf(bad, sizeof bad, "A function the mod names with rq.scene.functions");
		} else if (ckinds[k].ids) {
			thing_in(x, ck, ckinds[k].ids, w);
			if (val && !ids_find(ckinds[k].ids, val) && !(cJSON_IsString(val) && strchr(val->valuestring, ':')) && !(cJSON_IsString(val) && !val->valuestring[0]))
				snprintf(bad, sizeof bad, "Not in RQ MOD's names: a mod's own id has its mod's name before a colon");
		} else if (!strcmp(ck, "gold")) {
			igAlignTextToFramePadding();
			igTextColored(colv(C_DIM), "at least");
			igSameLine(0, S(6));
			number_in("##gold", x, "gold", "gold", fit_w(S(110), "G"));
			igSameLine(0, S(6));
			igAlignTextToFramePadding();
			igTextColored(colv(C_DIM), "G");
			if (cJSON_IsNumber(val) && val->valuedouble < 0)
				snprintf(bad, sizeof bad, "Gold is 0 or more");
		} else if (!strcmp(ck, "chance")) {
			number_in("##chance", x, "chance", "50", fit_w(S(70), "in 100"));
			igSameLine(0, S(6));
			igAlignTextToFramePadding();
			igTextColored(colv(C_DIM), "in 100");
			if (cJSON_IsNumber(val) && (val->valuedouble < 0 || val->valuedouble > 100))
				snprintf(bad, sizeof bad, "A chance is 0 to 100");
		}
	}
	if (!del_drawn && del_button(del_tip, can_del))
		ask(path, COND_DEL, NULL);
	if (k >= 0) {
		const char *ck = ckinds[k].key;
		/* The lines under it. */
		if (!strcmp(ck, "var")) {
			int m = var_mode(x);
			if (m != 1) {
				under(x_kind);
				if (m == 0) {
					field_value("##is", x, "is", -1, R - DEL_W - S(8) - x_kind);
				} else if (m == 4) {
					number_in("##lo", x, "at_least", "from", S(110));
					igSameLine(0, S(6));
					igAlignTextToFramePadding();
					igTextColored(colv(C_DIM), "to");
					igSameLine(0, S(6));
					number_in("##hi", x, "at_most", "to", S(110));
				} else {
					number_in("##n", x, m == 2 ? "at_least" : "at_most", "a number", S(110));
				}
			}
		} else if (!strcmp(ck, "call")) {
			under(x_kind);
			args_in(x);
		}
	}
	if (bad[0]) {
		under(x_kind);
		igPushTextWrapPos(R);
		small_text(k < 0 && !json_why[0] ? C_FAINT : C_BAD, bad);
		igPopTextWrapPos();
	}
}

static void node(cJSON *owner, const char *key, bool required, const char *path, int depth)
{
	struct slot s;
	if (!resolve(owner, key, path, &s))
		return;
	cJSON *v = slot_get(s);
	igPushID_Str(path[0] ? path : "root");
	bool any;
	cJSON *l = group_list(v, &any);
	bool root = !path[0];
	if (l) {
		bool neg = is_not(v);
		int m = (any ? 1 : 0) + (neg ? 2 : 0);
		static const char *const modes[] = { "All of", "Any of", "Not all of", "None of" };
		static const char *const keys[] = { "all", "any", "notall", "none" };
		static const char *const hints[] = { "every check holds", "one check holds, or more", "one check fails, or more",
		                                     "no check holds" };
		under(X0 + (float)depth * IND);
		igSetNextItemWidth(S(120));
		igBeginDisabled(ro);
		if (igBeginCombo("##mode", modes[m], 0)) {
			for (int i = 0; i < 4; i++)
				if (igSelectable_Bool(modes[i], i == m, 0, v2(0, 0)) && i != m)
					ask(path, COND_MODE, keys[i]);
			igEndCombo();
		}
		igEndDisabled();
		igSameLine(0, S(8));
		igAlignTextToFramePadding();
		igPushFont(NULL, 14);
		igTextColored(colv(C_FAINT), "%s", hints[m]);
		igPopFont();
		if (del_button(root ? "Take the condition away" : "Take this group away, with its checks", !(root && required)))
			ask(path, COND_DEL, NULL);
		float top = igGetCursorScreenPos().y;
		int i = 0;
		for (cJSON *c = l->child; c; c = c->next, i++) {
			char cp[64];
			child_path(cp, sizeof cp, path, i);
			node(owner, key, required, cp, depth + 1);
		}
		under(X0 + (float)(depth + 1) * IND);
		igBeginDisabled(ro);
		if (igSmallButton("+ Check"))
			ask(path, COND_ADD, NULL);
		igSetItemTooltip("Another check in this group");
		igSameLine(0, S(6));
		if (igSmallButton("+ Group"))
			ask(path, COND_GROUP, NULL);
		igSetItemTooltip(any ? "A group of checks that must all hold, as one check of this one"
		                     : "A group of which one check is enough, as one check of this one");
		igEndDisabled();
		/* A line down the group's checks. */
		float gx = igGetWindowPos().x - igGetScrollX() + X0 + (float)depth * IND + S(10);
		float bottom = igGetCursorScreenPos().y - S(4);
		ImDrawList_AddLine(igGetWindowDrawList(), v2(gx, top - S(2)), v2(gx, bottom), col(C_LINE), S(1.5f));
	} else {
		check_row(path, depth, v, !(root && required),
		          root && required ? "An if keeps a check: change it instead" : root ? "Take the condition away" : "Take this check away");
		if (root) {
			under(X0);
			igBeginDisabled(ro);
			if (igSmallButton("+ Check"))
				ask(path, COND_ADD, NULL);
			igEndDisabled();
			igSetItemTooltip("Another check: both in an \"All of\" group, which can become \"Any of\"");
		}
	}
	igPopID();
}

bool cond_builder(cJSON *owner, const char *key, bool required, bool readonly)
{
	ro = readonly;
	X0 = igGetCursorPosX();
	R = X0 + igGetContentRegionAvail().x;
	pend.on = false;
	igPushID_Str(key);
	cJSON *v = item(owner, key);
	if (!v) {
		igAlignTextToFramePadding();
		igTextColored(colv(C_FAINT), "%s", required ? "None" : "Always");
		igSameLine(0, S(10));
		igBeginDisabled(ro);
		if (igSmallButton("+ Condition"))
			ask("", COND_ADD, NULL);
		igEndDisabled();
		igSetItemTooltip("%s", required ? "A check" : "A check: the row shows only when it holds");
	} else {
		node(owner, key, required, "", 0);
	}
	igPopID();
	bool changed = false;
	if (pend.on) {
		char why[200];
		if (cond_apply(owner, key, required, pend.path, pend.op, pend.arg, why, sizeof why)) {
			field_picked();
			changed = true;
		} else if (why[0]) {
			ui_error("%s", why);
		}
		pend.on = false;
	}
	return changed;
}
