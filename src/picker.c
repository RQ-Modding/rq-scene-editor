/* The pickers: a picture by its look and its name, a track or a sound by its name with Play.
 *
 * The picture picker's tabs are the game's CG folders as a scene uses them: the faces, the standing
 * pictures, the backgrounds (and the wallpapers among them), the small scene CGs, the scene CGs, the
 * other folders, and the pictures in the mod's own folder. The faces and the standing pictures go by
 * character (RQ MOD's names, else the game's name plate for the face); the rest by hundreds. The
 * scene CGs stay hidden until asked for, once a session: the picker never opens on one.
 *
 * A pick writes the step's value as a field does: one step to undo. For a line's speaker, a face the
 * file's cast has writes the cast's name, and any other face can go into the cast with a name. */
#include <ctype.h>
#include <float.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include "system4.h"
#include "system4/file.h"

#include "editor.h"

/* ---- the pictures, by tab and group ---- */

struct group {
	char name[128];
	int *items;      /* CG numbers, or indices in mod_files */
	int n, cap;
	int first;       /* the first number, to sort by */
	bool unnamed;    /* sorted after the named ones */
};

struct tab {
	struct group *g;
	int n, cap;
	int count;
};

static struct tab tabs[PICK_TABS];
static bool built;

#define MOD_FILES_MAX 2000
static char (*mod_files)[512];
static int nmod_files;
static char mod_for[1024];

static const char *const tab_titles[PICK_TABS] = {
	"Faces", "Standing pictures", "Backgrounds", "Small scene CGs", "Scene CGs", "Other CGs", "The mod's pictures",
};

static cJSON *item(const cJSON *o, const char *k)
{
	return cJSON_IsObject(o) ? cJSON_GetObjectItemCaseSensitive(o, k) : NULL;
}

/* Which tab a CG goes in: its folder (01_ to 05_ are the scene's layers, the other numbered ones
 * the game's screens), else, for the folders added later, its number as the numbered folders hold
 * them. */
static int tab_of(const struct cg_entry *e)
{
	const char *jp = e->folder >= 0 && e->folder < G.ncg_folders ? G.cg_folders[e->folder].jp : "";
	static const char *const pre[] = { "01_", "02_", "03_", "04_", "05_" };
	static const int to[] = { PICK_STANDING, PICK_FACES, PICK_SCENE, PICK_SMALL, PICK_BACKGROUNDS };
	for (int i = 0; i < 5; i++)
		if (!strncmp(jp, pre[i], 3))
			return to[i];
	if (jp[0] >= '0' && jp[0] <= '9')
		return PICK_OTHER;
	int no = e->no;
	if (no >= 1000 && no < 2000)
		return PICK_STANDING;
	if (no >= 2000 && no < 3000)
		return PICK_FACES;
	if ((no >= 3000 && no < 4000) || (no >= 8000 && no < 8550))
		return PICK_SCENE;
	if (no >= 4000 && no < 5000)
		return PICK_SMALL;
	if (no >= 5000 && no < 6000)
		return PICK_BACKGROUNDS;
	return PICK_OTHER;
}

static int pick_tab_of_cg(int no)
{
	int i = game_find_cg(no);
	return i >= 0 ? tab_of(&G.cgs[i]) : -1;
}

/* "Rance: standing picture" -> "Rance". */
static const char *use_name(const char *use, char *out, size_t n)
{
	const char *c = use ? strstr(use, ": ") : NULL;
	if (!c)
		return NULL;
	snprintf(out, n, "%.*s", (int)(c - use), use);
	return out;
}

/* A standing picture's character: RQ MOD's name for a picture of its ten. The game keeps no table of
 * them (the faces have the plates' table), and the face 1000 above is not always the same person. */
static const char *standing_owner(int no, char *out, size_t n)
{
	int ten = no / 10 * 10;
	for (int k = ten; k < ten + 10; k++) {
		int i = game_find_cg(k);
		if (i >= 0 && G.cgs[i].use && strstr(G.cgs[i].use, ": standing") && use_name(G.cgs[i].use, out, n))
			return out;
	}
	return NULL;
}

static void group_name(int tab, int no, char *out, size_t n, bool *unnamed)
{
	*unnamed = false;
	char buf[128];
	if (tab == PICK_FACES) {
		const char *who = media_face_owner(no);
		if (!who)
			who = media_face_name(no);
		if (who) {
			snprintf(out, n, "%s", who);
			return;
		}
		*unnamed = true;
		snprintf(out, n, "Faces without a name");
		return;
	}
	if (tab == PICK_STANDING) {
		const char *who = standing_owner(no, buf, sizeof buf);
		if (who) {
			snprintf(out, n, "%s", who);
			return;
		}
		*unnamed = true;
		snprintf(out, n, "%d to %d", no / 100 * 100, no / 100 * 100 + 99);
		return;
	}
	int h = no / 100 * 100;
	if (tab == PICK_OTHER) {
		int i = game_find_cg(no);
		const struct cg_folder *f = i >= 0 && G.cgs[i].folder >= 0 ? &G.cg_folders[G.cgs[i].folder] : NULL;
		snprintf(out, n, "%s, %d to %d", f && f->en ? f->en : "Other", h, h + 99);
	} else {
		snprintf(out, n, "%d to %d", h, h + 99);
	}
}

static struct group *group_get(struct tab *t, const char *name, int first, bool unnamed)
{
	for (int i = 0; i < t->n; i++)
		if (!strcmp(t->g[i].name, name))
			return &t->g[i];
	if (t->n == t->cap) {
		t->cap = t->cap ? t->cap * 2 : 32;
		t->g = xrealloc(t->g, (size_t)t->cap * sizeof *t->g);
	}
	struct group *g = &t->g[t->n++];
	memset(g, 0, sizeof *g);
	snprintf(g->name, sizeof g->name, "%s", name);
	g->first = first;
	g->unnamed = unnamed;
	return g;
}

static void group_add(struct group *g, int v)
{
	if (g->n == g->cap) {
		g->cap = g->cap ? g->cap * 2 : 16;
		g->items = xrealloc(g->items, (size_t)g->cap * sizeof *g->items);
	}
	g->items[g->n++] = v;
}

static int int_cmp(const void *a, const void *b)
{
	int x = *(const int *)a, y = *(const int *)b;
	return x < y ? -1 : x > y;
}

static int group_cmp(const void *a, const void *b)
{
	const struct group *x = a, *y = b;
	if (x->unnamed != y->unnamed)
		return x->unnamed ? 1 : -1;
	return x->first - y->first;
}

static void tab_clear(struct tab *t)
{
	for (int i = 0; i < t->n; i++)
		free(t->g[i].items);
	free(t->g);
	memset(t, 0, sizeof *t);
}

static void build_cgs(void)
{
	for (int k = 0; k < PICK_MOD; k++)
		tab_clear(&tabs[k]);
	for (int i = 0; i < G.ncg; i++) {
		const struct cg_entry *e = &G.cgs[i];
		int t = tab_of(e);
		char name[128];
		bool unnamed;
		group_name(t, e->no, name, sizeof name, &unnamed);
		struct group *g = group_get(&tabs[t], name, e->no, unnamed);
		if (e->no < g->first)
			g->first = e->no;
		group_add(g, e->no);
		tabs[t].count++;
	}
	for (int k = 0; k < PICK_MOD; k++) {
		for (int i = 0; i < tabs[k].n; i++)
			qsort(tabs[k].g[i].items, (size_t)tabs[k].g[i].n, sizeof(int), int_cmp);
		qsort(tabs[k].g, (size_t)tabs[k].n, sizeof *tabs[k].g, group_cmp);
	}
	built = true;
}

static bool picture_ext(const char *name)
{
	const char *dot = strrchr(name, '.');
	return dot && (!strcasecmp(dot, ".png") || !strcasecmp(dot, ".webp") || !strcasecmp(dot, ".jpg")
	               || !strcasecmp(dot, ".jpeg") || !strcasecmp(dot, ".gif"));
}

static void walk_pictures(const char *root, const char *rel, int depth)
{
	char dir[1600];
	snprintf(dir, sizeof dir, "%s%s%s", root, rel[0] ? "/" : "", rel);
	UDIR *d = opendir_utf8(dir);
	if (!d)
		return;
	char *name;
	while ((name = readdir_utf8(d)) && nmod_files < MOD_FILES_MAX) {
		if (name[0] == '.' || !strcmp(name, "node_modules")) {
			free(name);
			continue;
		}
		char sub[512], full[2200];
		snprintf(sub, sizeof sub, "%s%s%s", rel, rel[0] ? "/" : "", name);
		snprintf(full, sizeof full, "%s/%s", root, sub);
		if (is_directory(full)) {
			if (depth < 6)
				walk_pictures(root, sub, depth + 1);
		} else if (picture_ext(name)) {
			snprintf(mod_files[nmod_files++], 512, "%s", sub);
		}
		free(name);
	}
	closedir_utf8(d);
}

static int path_cmp(const void *a, const void *b)
{
	return strcasecmp(a, b);
}

/* The pictures in the mod's folder, by folder: read again each time the picker opens. */
static void build_mod(void)
{
	tab_clear(&tabs[PICK_MOD]);
	if (!mod_files)
		mod_files = xmalloc((size_t)MOD_FILES_MAX * 512);
	nmod_files = 0;
	snprintf(mod_for, sizeof mod_for, "%s", media_mod_dir());
	if (mod_for[0])
		walk_pictures(mod_for, "", 0);
	qsort(mod_files, (size_t)nmod_files, 512, path_cmp);
	for (int i = 0; i < nmod_files; i++) {
		const char *slash = strrchr(mod_files[i], '/');
		char name[128];
		if (slash)
			snprintf(name, sizeof name, "%.*s/", (int)(slash - mod_files[i]), mod_files[i]);
		else
			snprintf(name, sizeof name, "The mod's folder");
		struct group *g = group_get(&tabs[PICK_MOD], name, i, !slash);
		group_add(g, i);
		tabs[PICK_MOD].count++;
	}
}

/* ---- the picker's state ---- */

static struct {
	bool want_open;
	bool sound;              /* the sound picker, not the picture one */
	struct pick_target t;
	int tab;
	int group[PICK_TABS];
	bool tab_set;            /* the tab bar follows tab once */
	int sel_no;              /* a CG */
	char sel_path[512];      /* or a picture of the mod's */
	char sel_cast[128];      /* or a name of the cast (a speaker) */
	char find[64];
	bool scroll_to_sel;
	bool scroll_group;       /* the list of groups shows the selected one */
	bool list_top;           /* the search changed: the list of groups from its top */
	int shown_tab, shown_group;   /* the grid's last, to start a new one at its top */
	bool scene_shown;        /* the scene CGs asked for, this session */
	bool adding;
	char add_name[64];
	char add_why[160];
	bool use_now;            /* --do use, or a double click */
	bool add_now;            /* --do addcast */
	int playing;             /* the sound picker started a sound: stop it on closing */
} P;

bool picker_requested;
char picker_requested_key[32];

static const char *find_text(void)
{
	const char *s = P.find;
	while (*s == ' ')
		s++;
	return s;
}

static bool find_is_number(void)
{
	const char *s = find_text();
	if (!*s)
		return false;
	for (; *s; s++)
		if (!isdigit((unsigned char)*s) && *s != ' ')
			return false;
	return true;
}

static bool number_matches(int no)
{
	if (!find_is_number())
		return true;
	char digits[16], have[16];
	snprintf(digits, sizeof digits, "%d", atoi(find_text()));
	snprintf(have, sizeof have, "%d", no);
	return !strncmp(have, digits, strlen(digits));
}

static bool name_matches(const char *name)
{
	const char *f = find_text();
	if (!*f || find_is_number())
		return true;
	size_t n = strlen(f);
	for (const char *p = name; *p; p++)
		if (!strncasecmp(p, f, n))
			return true;
	return false;
}

static bool item_matches(int tab, const struct group *g, int v)
{
	if (tab == PICK_MOD) {
		const char *f = find_text();
		if (!*f)
			return true;
		size_t n = strlen(f);
		for (const char *p = mod_files[v]; *p; p++)
			if (!strncasecmp(p, f, n))
				return true;
		return false;
	}
	if (find_is_number())
		return number_matches(v);
	return name_matches(g->name);
}

static int group_shown(int tab, const struct group *g)
{
	int n = 0;
	for (int k = 0; k < g->n; k++)
		n += item_matches(tab, g, g->items[k]);
	return n;
}

static bool is_selected(int tab, int v)
{
	if (tab == PICK_MOD)
		return P.sel_path[0] && v < nmod_files && !strcmp(P.sel_path, mod_files[v]);
	return !P.sel_path[0] && P.sel_no == v;
}

/* The group and tab of a CG or a mod picture, selected. */
static bool locate(int no, const char *path)
{
	for (int t = 0; t < PICK_TABS; t++) {
		if ((t == PICK_MOD) != (path != NULL))
			continue;
		for (int i = 0; i < tabs[t].n; i++) {
			const struct group *g = &tabs[t].g[i];
			for (int k = 0; k < g->n; k++) {
				if (path ? !strcmp(mod_files[g->items[k]], path) : g->items[k] == no) {
					P.tab = t;
					P.group[t] = i;
					P.tab_set = true;
					P.scroll_to_sel = true;
					P.scroll_group = true;
					return true;
				}
			}
		}
	}
	return false;
}

static void select_cg(int no)
{
	P.sel_no = no;
	P.sel_path[0] = 0;
	P.sel_cast[0] = 0;
	P.adding = false;
}

static void select_path(const char *path)
{
	P.sel_no = 0;
	snprintf(P.sel_path, sizeof P.sel_path, "%s", path);
	P.sel_cast[0] = 0;
	P.adding = false;
}

/* The cast's entry whose face is this CG or this picture, or NULL. */
static const char *cast_with(int no, const char *path)
{
	const cJSON *cast = item(D.root, "cast");
	for (const cJSON *e = cast ? cast->child : NULL; e; e = e->next) {
		const cJSON *f = item(e, "face");
		if (path ? cJSON_IsString(f) && !strcmp(f->valuestring, path) : cJSON_IsNumber(f) && (int)f->valuedouble == no)
			return e->string;
	}
	return NULL;
}

static cJSON *target_obj(void)
{
	if (!D.root)
		return NULL;
	cJSON *o = P.t.place[0] ? place_find(D.root, P.t.place, NULL, NULL) : item(item(D.root, "cast"), P.t.cast);
	if (o && P.t.sub[0]) {
		cJSON *s = item(o, P.t.sub);
		if (!cJSON_IsObject(s)) {
			s = cJSON_CreateObject();
			cJSON_AddItemToObject(o, P.t.sub, s);
		}
		o = s;
	}
	return o;
}

static const char *target_words(void)
{
	static char out[300];
	if (P.t.place[0])
		snprintf(out, sizeof out, "%s%s%s", P.t.place, P.t.sub[0] ? "." : "", P.t.sub[0] ? P.t.sub : "");
	else
		snprintf(out, sizeof out, "the cast's %s", P.t.cast);
	return out;
}

void picker_open(const struct pick_target *t)
{
	memset(&P.t, 0, sizeof P.t);
	P.t = *t;
	P.want_open = true;
	P.sound = t->sound >= 0;
	P.find[0] = 0;
	P.adding = false;
	P.add_why[0] = 0;
	P.use_now = P.add_now = false;
	P.sel_no = 0;
	P.sel_path[0] = 0;
	P.sel_cast[0] = 0;
	if (P.sound) {
		const cJSON *v = item(target_obj(), t->key);
		P.sel_no = cJSON_IsNumber(v) ? (int)v->valuedouble : 0;
		P.scroll_to_sel = true;
		return;
	}
	if (!built)
		build_cgs();
	build_mod();
	P.tab = t->tab;
	P.tab_set = true;
	P.scroll_group = true;
	P.shown_tab = -1;
	const cJSON *v = item(target_obj(), t->key);
	const cJSON *cast = item(D.root, "cast");
	if (t->speaker && cJSON_IsString(v) && !strchr(v->valuestring, '.') && item(cast, v->valuestring)) {
		const cJSON *f = item(item(cast, v->valuestring), "face");
		if (cJSON_IsNumber(f)) {
			select_cg((int)f->valuedouble);
			locate(P.sel_no, NULL);
		} else if (cJSON_IsString(f)) {
			select_path(f->valuestring);
			locate(0, P.sel_path);
		}
		snprintf(P.sel_cast, sizeof P.sel_cast, "%s", v->valuestring);
	} else if (cJSON_IsNumber(v) && v->valuedouble > 0) {
		select_cg((int)v->valuedouble);
		if (!locate(P.sel_no, NULL))
			P.sel_no = 0;
	} else if (cJSON_IsString(v) && strchr(v->valuestring, '.')) {
		select_path(v->valuestring);
		if (!locate(0, P.sel_path))
			P.tab = PICK_MOD;
	} else if (!strcmp(t->key, "wallpaper") || !strcmp(t->key, "mask_wallpaper")) {
		/* The game's wallpapers are its backgrounds from 5500 (its mask is 5550). */
		int want = !strcmp(t->key, "mask_wallpaper") ? 5550 : 5500;
		for (int i = 0; i < tabs[PICK_BACKGROUNDS].n; i++)
			if (tabs[PICK_BACKGROUNDS].g[i].first / 100 == want / 100)
				P.group[PICK_BACKGROUNDS] = i;
	}
	if (P.tab < 0 || P.tab >= PICK_TABS)
		P.tab = PICK_FACES;
}

bool picker_is_open(void)
{
	return igIsPopupOpen_Str("Pick a picture##picker", 0) || igIsPopupOpen_Str("###sounds", 0);
}

/* ---- writing the pick ---- */

static void put(cJSON *o, const char *k, cJSON *v)
{
	if (cJSON_GetObjectItemCaseSensitive(o, k))
		cJSON_ReplaceItemInObjectCaseSensitive(o, k, v);
	else
		cJSON_AddItemToObject(o, k, v);
}

/* What the pick writes, as JSON text for the footer, or "" when nothing is picked. */
static void pick_value(char *out, size_t n, bool *is_cast)
{
	*is_cast = false;
	out[0] = 0;
	if (P.sound) {
		if (P.sel_no > 0)
			snprintf(out, n, "%d", P.sel_no);
		return;
	}
	const char *c = NULL;
	if (P.t.speaker)
		c = P.sel_cast[0] ? P.sel_cast : cast_with(P.sel_no, P.sel_path[0] ? P.sel_path : NULL);
	if (c) {
		snprintf(out, n, "\"%s\"", c);
		*is_cast = true;
	} else if (P.sel_path[0]) {
		snprintf(out, n, "\"%s\"", P.sel_path);
	} else if (P.sel_no > 0) {
		snprintf(out, n, "%d", P.sel_no);
	}
}

static bool apply(void)
{
	cJSON *o = target_obj();
	if (!o || !doc_editable())
		return false;
	char text[600];
	bool is_cast;
	pick_value(text, sizeof text, &is_cast);
	if (!text[0])
		return false;
	doc_settle();
	cJSON *v;
	if (text[0] == '"') {
		text[strlen(text) - 1] = 0;
		v = cJSON_CreateString(text + 1);
	} else {
		v = cJSON_CreateNumber(atoi(text));
	}
	put(o, P.t.key, v);
	doc_changed(P.t.place);
	ui_status("%s: %s", target_words(), text);
	return true;
}

static bool cast_name_ok(const char *s)
{
	if (!s[0] || !((s[0] >= 'A' && s[0] <= 'Z') || (s[0] >= 'a' && s[0] <= 'z') || s[0] == '_'))
		return false;
	for (const char *p = s; *p; p++)
		if (!((*p >= 'A' && *p <= 'Z') || (*p >= 'a' && *p <= 'z') || (*p >= '0' && *p <= '9') || *p == '_'))
			return false;
	return true;
}

/* A name for the cast from the face's: "magic", "rance_2003" when "rance" is taken. */
static void suggest_name(void)
{
	char base[64] = "someone";
	const char *who = P.sel_no ? media_face_name(P.sel_no) : NULL;
	if (!who && P.sel_no)
		who = media_face_owner(P.sel_no);
	if (who) {
		int k = 0;
		for (const char *p = who; *p && k < 40; p++) {
			if (isalnum((unsigned char)*p))
				base[k++] = (char)tolower((unsigned char)*p);
			else if (k && base[k - 1] != '_')
				base[k++] = '_';
		}
		while (k && base[k - 1] == '_')
			k--;
		base[k] = 0;
		if (!k || isdigit((unsigned char)base[0]))
			snprintf(base, sizeof base, "someone");
	} else if (P.sel_path[0]) {
		const char *b = strrchr(P.sel_path, '/');
		b = b ? b + 1 : P.sel_path;
		int k = 0;
		for (; *b && *b != '.' && k < 40; b++)
			base[k++] = isalnum((unsigned char)*b) ? (char)tolower((unsigned char)*b) : '_';
		base[k] = 0;
		if (!k || isdigit((unsigned char)base[0]))
			snprintf(base, sizeof base, "someone");
	}
	const cJSON *cast = item(D.root, "cast");
	snprintf(P.add_name, sizeof P.add_name, "%s", base);
	if (item(cast, P.add_name) && P.sel_no)
		snprintf(P.add_name, sizeof P.add_name, "%s_%d", base, P.sel_no);
	for (int i = 2; item(cast, P.add_name); i++)
		snprintf(P.add_name, sizeof P.add_name, "%s%d", base, i);
}

/* The face goes into the cast as P.add_name, and the line names it. */
static bool add_to_cast(void)
{
	cJSON *o = target_obj();
	if (!o || !doc_editable())
		return false;
	cJSON *cast = item(D.root, "cast");
	if (!cast_name_ok(P.add_name)) {
		snprintf(P.add_why, sizeof P.add_why, "A name is letters, digits and _, not starting with a digit");
		return false;
	}
	if (item(cast, P.add_name)) {
		snprintf(P.add_why, sizeof P.add_why, "The cast has a %s already", P.add_name);
		return false;
	}
	doc_settle();
	if (!cJSON_IsObject(cast)) {
		cast = cJSON_CreateObject();
		cJSON_AddItemToObject(D.root, "cast", cast);
	}
	cJSON *e = cJSON_CreateObject();
	if (P.sel_path[0])
		cJSON_AddStringToObject(e, "face", P.sel_path);
	else
		cJSON_AddNumberToObject(e, "face", P.sel_no);
	cJSON_AddItemToObject(cast, P.add_name, e);
	put(o, P.t.key, cJSON_CreateString(P.add_name));
	doc_changed(P.t.place);
	ui_status("%s: \"%s\", added to the cast", target_words(), P.add_name);
	P.adding = false;
	return true;
}

/* ---- drawing ---- */

static void draw_tex(ImDrawList *dl, unsigned tex, int w, int h, V2 a, V2 b, bool cover)
{
	if (!tex || w <= 0 || h <= 0)
		return;
	float bw = b.x - a.x, bh = b.y - a.y;
	if (cover) {
		float s = fmaxf(bw / w, bh / h);
		float dw = w * s, dh = h * s;
		float u0 = (dw - bw) / 2 / dw, vh = bh / dh;
		ImDrawList_AddImageRounded(dl, texref(tex), a, b, v2(u0, 0), v2(1 - u0, vh), IM_COL32_WHITE, S(3), 0);
	} else {
		float s = fminf(bw / w, bh / h);
		float dw = w * s, dh = h * s;
		V2 p = v2(a.x + (bw - dw) / 2, a.y + (bh - dh) / 2);
		ImDrawList_AddImage(dl, texref(tex), p, v2(p.x + dw, p.y + dh), v2(0, 0), v2(1, 1), IM_COL32_WHITE);
	}
}

static bool tab_covers(int tab)
{
	return tab == PICK_FACES;
}

static void tile_size(int tab, float *w, float *h)
{
	switch (tab) {
	case PICK_FACES: *w = S(96); *h = S(112); break;
	case PICK_STANDING: *w = S(96); *h = S(150); break;
	case PICK_MOD: *w = S(120); *h = S(120); break;
	default: *w = S(160); *h = S(120); break;
	}
}

/* A picture in a box: the CG's thumbnail, or the mod's picture made small; the whole picture when
 * whole is true (the footer, a tooltip). */
static void draw_pick(ImDrawList *dl, int tab, int v, V2 a, V2 b, bool whole)
{
	bool cover = tab_covers(tab);
	if (tab == PICK_MOD) {
		const struct picture *p = media_mod_picture(mod_files[v]);
		if (p->state == PIC_READY)
			draw_tex(dl, p->tex, p->w, p->h, a, b, cover);
		else if (p->state == PIC_FAILED) {
			/* A GIF or a moving WebP: the game moves it (rq.image), the editor's reader has no frames. */
			const char *dot = strrchr(mod_files[v], '.');
			bool moving = dot && (!strcasecmp(dot, ".gif") || !strcasecmp(dot, ".webp"));
			char kind[16] = "";
			for (int i = 0; dot && dot[i + 1] && i < 8; i++)
				kind[i] = (char)toupper((unsigned char)dot[i + 1]);
			ImDrawList_PushClipRect(dl, a, b, true);
			ImDrawList_AddText_FontPtr(dl, F_BOLD, S(15), v2(a.x + S(6), a.y + S(6)), col(C_DIM), kind, NULL, 0, NULL);
			ImDrawList_AddText_FontPtr(dl, F_UI, S(13), v2(a.x + S(6), a.y + S(26)), col(C_FAINT),
			                           moving ? "not drawn here;\nthe game shows it" : "not read", NULL, 0, NULL);
			ImDrawList_PopClipRect(dl);
		}
		return;
	}
	if (whole) {
		const struct picture *p = media_cg(v);
		if (p->state == PIC_READY) {
			draw_tex(dl, p->tex, p->w, p->h, a, b, cover);
			return;
		}
	}
	struct thumb *t = media_thumb(v);
	if (t->state == 2 && t->tex)
		draw_tex(dl, t->tex, t->w, t->h, a, b, cover);
}

static const char *cg_folder_label(int no)
{
	int i = game_find_cg(no);
	if (i < 0 || G.cgs[i].folder < 0 || G.cgs[i].folder >= G.ncg_folders)
		return "";
	const struct cg_folder *f = &G.cg_folders[G.cgs[i].folder];
	return f->en ? f->en : f->jp;
}

static void tooltip(int tab, int v)
{
	if (!igBeginTooltip())
		return;
	float bw = S(400), bh = S(320);
	if (tab == PICK_FACES) {
		bw = S(220);
		bh = S(260);
	} else if (tab == PICK_STANDING) {
		bw = S(260);
		bh = S(420);
	}
	V2 p = igGetCursorScreenPos();
	igDummy(v2(bw, bh));
	draw_pick(igGetWindowDrawList(), tab, v, p, v2(p.x + bw, p.y + bh), true);
	if (tab == PICK_MOD) {
		const struct picture *pic = media_mod_picture(mod_files[v]);
		igText("%s", mod_files[v]);
		if (pic->state == PIC_READY)
			igTextColored(colv(C_DIM), "%d by %d", pic->w, pic->h);
	} else {
		const struct picture *pic = media_cg(v);
		int i = game_find_cg(v);
		igText("CG %d", v);
		if (i >= 0 && G.cgs[i].use)
			igTextColored(colv(C_DIM), "%s", G.cgs[i].use);
		igTextColored(colv(C_DIM), "%s", cg_folder_label(v));
		if (pic->state == PIC_READY)
			igTextColored(colv(C_FAINT), "%d by %d", pic->w, pic->h);
	}
	igEndTooltip();
}

static void tile(int tab, int v, float tw, float th)
{
	float pad = S(5), cap = S(22);
	igPushID_Int(v);
	V2 p = igGetCursorScreenPos();
	V2 size = v2(tw + pad * 2, th + cap + pad * 2);
	bool clicked = igInvisibleButton("##t", size, 0);
	bool hovered = igIsItemHovered(0);
	bool visible = igIsItemVisible();
	bool sel = is_selected(tab, v);
	ImDrawList *dl = igGetWindowDrawList();
	V2 q = v2(p.x + size.x, p.y + size.y);
	ImDrawList_AddRectFilled(dl, p, q, col(hovered ? C_HOVER : C_CHILD), S(5), 0);
	if (visible) {
		V2 a = v2(p.x + pad, p.y + pad);
		draw_pick(dl, tab, v, a, v2(a.x + tw, a.y + th), false);
		char cap_text[128];
		const char *in_cast = P.t.speaker ? cast_with(tab == PICK_MOD ? 0 : v, tab == PICK_MOD ? mod_files[v] : NULL) : NULL;
		if (tab == PICK_MOD) {
			const char *b = strrchr(mod_files[v], '/');
			snprintf(cap_text, sizeof cap_text, "%s", b ? b + 1 : mod_files[v]);
		} else {
			snprintf(cap_text, sizeof cap_text, "%d", v);
		}
		V2 c = v2(p.x + pad + S(2), p.y + pad + th + S(4));
		ImDrawList_PushClipRect(dl, p, q, true);
		ImDrawList_AddText_FontPtr(dl, tab == PICK_MOD ? F_UI : F_MONO, S(14), c, col(C_TEXT), cap_text, NULL, 0, NULL);
		if (in_cast) {
			float w = ImFont_CalcTextSizeA(F_UI, S(13), FLT_MAX, 0, in_cast, NULL, NULL).x;
			ImDrawList_AddText_FontPtr(dl, F_UI, S(13), v2(q.x - pad - w - S(2), c.y + S(1)), col(C_ACCENT), in_cast, NULL, 0, NULL);
		}
		ImDrawList_PopClipRect(dl);
	}
	if (sel)
		ImDrawList_AddRect(dl, p, q, col(C_ACCENT), S(5), S(2), 0);
	if (clicked) {
		if (tab == PICK_MOD)
			select_path(mod_files[v]);
		else
			select_cg(v);
	}
	if (hovered && igIsMouseDoubleClicked_Nil(0))
		P.use_now = true;
	if (hovered && igIsItemHovered(ImGuiHoveredFlags_DelayNormal))
		tooltip(tab, v);
	if (sel && P.scroll_to_sel) {
		igSetScrollHereY(0.4f);
		P.scroll_to_sel = false;
	}
	igPopID();
}

static void cast_bar(void)
{
	const cJSON *cast = item(D.root, "cast");
	if (!cast || !cast->child)
		return;
	igAlignTextToFramePadding();
	igTextColored(colv(C_FAINT), "In this file's cast:");
	for (const cJSON *e = cast->child; e; e = e->next) {
		igSameLine(0, S(8));
		igPushID_Str(e->string);
		const cJSON *f = item(e, "face");
		float h = S(30);
		float tw = ImFont_CalcTextSizeA(F_UI, S(15), FLT_MAX, 0, e->string, NULL, NULL).x;
		V2 p = igGetCursorScreenPos();
		V2 size = v2(h + tw + S(14), h);
		if (igGetContentRegionAvail().x < size.x) {
			igNewLine();
			p = igGetCursorScreenPos();
		}
		bool on = !strcmp(P.sel_cast, e->string);
		if (igInvisibleButton("##chip", size, 0)) {
			P.adding = false;
			if (cJSON_IsNumber(f)) {
				select_cg((int)f->valuedouble);
				locate(P.sel_no, NULL);
			} else if (cJSON_IsString(f)) {
				select_path(f->valuestring);
				locate(0, P.sel_path);
			}
			snprintf(P.sel_cast, sizeof P.sel_cast, "%s", e->string);
		}
		bool hov = igIsItemHovered(0);
		ImDrawList *dl = igGetWindowDrawList();
		V2 q = v2(p.x + size.x, p.y + size.y);
		ImDrawList_AddRectFilled(dl, p, q, col(hov ? C_HOVER : C_CHILD), h / 2, 0);
		ImDrawList_AddRect(dl, p, q, col(on ? C_ACCENT : C_BORDER), h / 2, S(on ? 2 : 1), 0);
		draw_picture_small(dl, f, v2(p.x + S(2), p.y + S(2)), v2(p.x + h - S(2), p.y + h - S(2)), true);
		ImDrawList_AddText_FontPtr(dl, F_UI, S(15), v2(p.x + h + S(4), p.y + (h - S(15)) / 2 - S(1)), col(C_TEXT), e->string, NULL, 0, NULL);
		igPopID();
	}
	igSpacing();
}

static void scene_cover(void)
{
	igSpacing();
	igPushTextWrapPos(igGetCursorPosX() + S(520));
	igTextColored(colv(C_DIM), "The game's scene CGs are its event pictures, adult scenes among them. They show here "
	              "only when asked, once a session; the picker never opens on them.");
	igPopTextWrapPos();
	igSpacing();
	if (igButton("Show the scene CGs", v2(0, 0)))
		P.scene_shown = true;
}

static void groups_list(int tab)
{
	struct tab *t = &tabs[tab];
	if (P.list_top) {
		igSetScrollY_Float(0);
		P.list_top = false;
	}
	for (int i = 0; i < t->n; i++) {
		struct group *g = &t->g[i];
		int shown = group_shown(tab, g);
		if (!shown && i != P.group[tab])
			continue;
		igPushID_Int(i);
		V2 p = igGetCursorScreenPos();
		float h = tab == PICK_FACES || tab == PICK_STANDING ? S(40) : S(30);
		if (igSelectable_Bool("##g", i == P.group[tab], 0, v2(0, h))) {
			P.group[tab] = i;
			P.scroll_to_sel = true;
		}
		if (i == P.group[tab] && P.scroll_group) {
			igSetScrollHereY(0.3f);
			P.scroll_group = false;
		}
		ImDrawList *dl = igGetWindowDrawList();
		float x = p.x + S(4);
		if (tab != PICK_MOD && g->n) {
			float fh = h - S(6);
			float fw = tab == PICK_FACES || tab == PICK_STANDING ? fh * 0.85f : fh * 4 / 3;
			V2 a = v2(x, p.y + S(3));
			ImDrawList_AddRectFilled(dl, a, v2(a.x + fw, a.y + fh), col(C_CHILD), S(3), 0);
			if (igIsItemVisible() && !(tab == PICK_SCENE && !P.scene_shown))
				draw_pick(dl, tab == PICK_STANDING ? PICK_FACES : tab, g->items[0], a, v2(a.x + fw, a.y + fh), false);
			x += fw + S(8);
		}
		char count[32];
		snprintf(count, sizeof count, "%d", find_text()[0] ? shown : g->n);
		float cw = ImFont_CalcTextSizeA(F_UI, S(14), FLT_MAX, 0, count, NULL, NULL).x;
		float right = p.x + igGetContentRegionAvail().x;
		ImDrawList_PushClipRect(dl, v2(x, p.y), v2(right - cw - S(10), p.y + h), true);
		ImDrawList_AddText_FontPtr(dl, F_UI, S(15), v2(x, p.y + (h - S(17)) / 2), col(g->unnamed ? C_DIM : C_TEXT), g->name, NULL, 0, NULL);
		ImDrawList_PopClipRect(dl);
		ImDrawList_AddText_FontPtr(dl, F_UI, S(14), v2(right - cw - S(4), p.y + (h - S(16)) / 2), col(C_FAINT), count, NULL, 0, NULL);
		igPopID();
	}
}

static void grid(int tab)
{
	struct tab *t = &tabs[tab];
	if (tab == PICK_FACES && P.t.speaker)
		cast_bar();
	if (tab == PICK_SCENE && !P.scene_shown) {
		scene_cover();
		return;
	}
	if (!t->n) {
		igTextColored(colv(C_DIM), tab == PICK_MOD ? "No picture in the mod's folder: a PNG, WebP, JPEG or GIF put there shows here."
		                                           : "None in this game.");
		return;
	}
	if (P.group[tab] >= t->n)
		P.group[tab] = 0;
	/* A search that leaves the group empty moves to the first group it shows. */
	if (find_text()[0] && !group_shown(tab, &t->g[P.group[tab]])) {
		for (int i = 0; i < t->n; i++) {
			if (group_shown(tab, &t->g[i])) {
				P.group[tab] = i;
				break;
			}
		}
	}
	struct group *g = &t->g[P.group[tab]];
	if (tab != P.shown_tab || P.group[tab] != P.shown_group) {
		igSetScrollY_Float(0);
		P.shown_tab = tab;
		P.shown_group = P.group[tab];
	}
	igTextUnformatted(g->name, NULL);
	igSameLine(0, S(10));
	if (tab == PICK_MOD)
		igTextColored(colv(C_FAINT), "%d picture%s", g->n, g->n == 1 ? "" : "s");
	else if (g->n)
		igTextColored(colv(C_FAINT), "%d to %d \xC2\xB7 %d", g->items[0], g->items[g->n - 1], g->n);
	igSpacing();
	float tw, th;
	tile_size(tab, &tw, &th);
	float gap = S(10);
	float cell = tw + S(10);
	float avail = igGetContentRegionAvail().x;
	int cols = (int)((avail + gap) / (cell + gap));
	if (cols < 1)
		cols = 1;
	igPushStyleVar_Vec2(ImGuiStyleVar_ItemSpacing, v2(gap, gap));
	int shown = 0;
	for (int k = 0; k < g->n; k++) {
		if (!item_matches(tab, g, g->items[k]))
			continue;
		if (shown % cols)
			igSameLine(0, gap);
		tile(tab, g->items[k], tw, th);
		shown++;
	}
	igPopStyleVar(1);
	if (!shown)
		igTextColored(colv(C_DIM), "Nothing here matches \"%s\".", find_text());
}

/* The footer: the pick, what it writes, and the buttons. True when the picker closes. */
static bool footer(float h)
{
	ImDrawList *dl = igGetWindowDrawList();
	V2 p = igGetCursorScreenPos();
	float bh = h - S(20), bw = bh * 0.85f;
	int tab = P.sel_path[0] ? PICK_MOD : P.sel_no ? pick_tab_of_cg(P.sel_no) : -1;
	bool hidden = tab == PICK_SCENE && !P.scene_shown;
	bool any = P.sel_path[0] || P.sel_no > 0;
	if (tab != PICK_FACES && tab >= 0)
		bw = bh * 4 / 3;
	V2 a = v2(p.x, p.y + S(10));
	ImDrawList_AddRectFilled(dl, a, v2(a.x + bw, a.y + bh), col(C_CHILD), S(4), 0);
	if (any && !hidden) {
		int v = P.sel_no;
		if (tab == PICK_MOD) {
			for (int i = 0; i < nmod_files; i++)
				if (!strcmp(mod_files[i], P.sel_path))
					v = i;
		}
		if (tab == PICK_MOD && v == P.sel_no) {
			cJSON *s = cJSON_CreateString(P.sel_path);
			draw_picture_small(dl, s, a, v2(a.x + bw, a.y + bh), false);
			cJSON_Delete(s);
		} else {
			draw_pick(dl, tab, v, a, v2(a.x + bw, a.y + bh), true);
		}
		ImDrawList_AddRect(dl, a, v2(a.x + bw, a.y + bh), col(C_ACCENT), S(4), S(2), 0);
	}
	float x = a.x + bw + S(16);
	float bx = p.x + igGetContentRegionAvail().x;
	igSetCursorScreenPos(v2(x, a.y));
	float text_w = bx - x - S(330);
	igPushTextWrapPos(igGetCursorPosX() + text_w);
	char title[300] = "", what[300] = "";
	if (P.sel_path[0]) {
		snprintf(title, sizeof title, "%s", P.sel_path);
		const struct picture *pic = media_mod_picture(P.sel_path);
		snprintf(what, sizeof what, "a picture in the mod's folder");
		if (pic->state == PIC_READY)
			snprintf(what, sizeof what, "a picture in the mod's folder, %d by %d", pic->w, pic->h);
	} else if (P.sel_no > 0) {
		const char *who = tab == PICK_FACES ? media_face_owner(P.sel_no) : NULL;
		char buf[128];
		if (!who && tab == PICK_STANDING)
			who = standing_owner(P.sel_no, buf, sizeof buf);
		snprintf(title, sizeof title, "%d%s%s", P.sel_no, who ? " \xC2\xB7 " : "", who ? who : "");
		const char *plate = tab == PICK_FACES ? media_face_name(P.sel_no) : NULL;
		snprintf(what, sizeof what, "%s%s%s%s", cg_folder_label(P.sel_no), plate ? " \xC2\xB7 the name plate shows \"" : "",
		         plate ? plate : "", plate ? "\"" : "");
	} else {
		snprintf(title, sizeof title, "Nothing picked");
		snprintf(what, sizeof what, "Click a picture; a double click uses it.");
	}
	igPushFont(F_BOLD, 0);
	igTextUnformatted(title, NULL);
	igPopFont();
	igSetCursorPosX(x - igGetWindowPos().x);
	igTextColored(colv(C_DIM), "%s", what);
	char value[600];
	bool is_cast;
	pick_value(value, sizeof value, &is_cast);
	if (value[0] && !P.adding) {
		igSetCursorPosX(x - igGetWindowPos().x);
		igPushFont(NULL, 14);
		if (is_cast)
			igTextColored(colv(C_FAINT), "Writes %s, the cast's name for this face, in %s.", value, target_words());
		else
			igTextColored(colv(C_FAINT), "Writes %s in %s.", value, target_words());
		igPopFont();
	}
	igPopTextWrapPos();
	bool can_add = P.t.speaker && any && !is_cast && doc_editable();
	if (P.adding) {
		igSetCursorPosX(x - igGetWindowPos().x);
		igAlignTextToFramePadding();
		igTextColored(colv(C_DIM), "Into the cast as");
		igSameLine(0, S(8));
		igSetNextItemWidth(S(200));
		igPushFont(F_MONO, MONO_SIZE);
		bool enter = igInputText("##castname", P.add_name, sizeof P.add_name, ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_CharsNoBlank, NULL, NULL);
		igPopFont();
		igSameLine(0, S(8));
		if (igButton("Add", v2(0, 0)) || enter || P.add_now) {
			P.add_now = false;
			if (add_to_cast())
				return true;
		}
		if (P.add_why[0]) {
			igSetCursorPosX(x - igGetWindowPos().x);
			igTextColored(colv(C_BAD), "%s", P.add_why);
		}
	}
	/* The buttons, at the right. */
	float bwid = S(96);
	igSetCursorScreenPos(v2(bx - bwid * 2 - S(8) - (can_add && !P.adding ? S(176) : 0), a.y + bh - S(32)));
	if (can_add && !P.adding) {
		if (igButton("Add to the cast as\xE2\x80\xA6", v2(S(168), 0)) || P.add_now) {
			P.adding = true;
			P.add_why[0] = 0;
			suggest_name();
			if (P.add_now) {
				P.add_now = false;
				if (add_to_cast())
					return true;
			}
		}
		igSameLine(0, S(8));
	}
	if (igButton("Cancel", v2(bwid, 0)))
		return true;
	igSameLine(0, S(8));
	igBeginDisabled(!value[0] || !doc_editable());
	igPushStyleColor_Vec4(ImGuiCol_Button, colv(C_ACCENT));
	igPushStyleColor_Vec4(ImGuiCol_ButtonHovered, colv(C_ACCENT_HI));
	igPushStyleColor_Vec4(ImGuiCol_ButtonActive, colv(C_ACCENT));
	igPushStyleColor_Vec4(ImGuiCol_Text, colv(C_ACCENT_INK));
	igPushStyleColor_Vec4(ImGuiCol_Border, colv(C_ACCENT));
	bool use = igButton("Use", v2(bwid, 0));
	igPopStyleColor(5);
	igEndDisabled();
	if ((use || P.use_now || (igIsKeyPressed_Bool(ImGuiKey_Enter, false) && !igIsAnyItemActive())) && value[0]) {
		P.use_now = false;
		return apply();
	}
	P.use_now = false;
	return false;
}

static void picture_picker(void)
{
	ImGuiIO *io = igGetIO_Nil();
	V2 size = v2(fminf(S(1120), io->DisplaySize.x - S(40)), fminf(S(720), io->DisplaySize.y - S(40)));
	igSetNextWindowSize(size, ImGuiCond_Always);
	igSetNextWindowPos(v2(io->DisplaySize.x / 2, io->DisplaySize.y / 2), ImGuiCond_Always, v2(0.5f, 0.5f));
	igPushStyleVar_Vec2(ImGuiStyleVar_WindowPadding, v2(S(14), S(10)));
	bool open = igBeginPopupModal("Pick a picture##picker", NULL, ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize);
	igPopStyleVar(1);
	if (!open)
		return;
	if (strcmp(mod_for, media_mod_dir()))
		build_mod();
	/* The heading: what it is for, and the search. */
	igAlignTextToFramePadding();
	igTextColored(colv(C_DIM), "For %s", target_words());
	igSameLine(0, 0);
	float fw = S(330);
	igSetCursorPosX(igGetCursorPosX() + igGetContentRegionAvail().x - fw);
	igSetNextItemWidth(fw);
	if (igIsWindowAppearing() && !UI.shot)
		igSetKeyboardFocusHere(0);
	if (igInputTextWithHint("##find", "Search: a number or a name", P.find, sizeof P.find, 0, NULL, NULL)) {
		P.list_top = true;
		/* A number the game has: its tab and group at once. */
		if (find_is_number()) {
			int no = atoi(find_text());
			int t = pick_tab_of_cg(no);
			if (t >= 0 && (t != PICK_SCENE || P.scene_shown) && locate(no, NULL))
				select_cg(no);
		}
	}
	if (igBeginTabBar("##tabs", 0)) {
		for (int t = 0; t < PICK_TABS; t++) {
			char label[96];
			if (t == PICK_SCENE && !P.scene_shown)
				snprintf(label, sizeof label, "%s###tab%d", tab_titles[t], t);
			else
				snprintf(label, sizeof label, "%s  %d###tab%d", tab_titles[t], tabs[t].count, t);
			/* The bar shows P.tab (the picker sets it too: a search, a cast name, --do). */
			int flags = P.tab == t ? ImGuiTabItemFlags_SetSelected : 0;
			if (igBeginTabItem(label, NULL, flags))
				igEndTabItem();
			if (igIsItemClicked(0) && P.tab != t) {
				P.tab = t;
				P.scroll_group = true;
			}
		}
		igEndTabBar();
	}
	P.tab_set = false;
	float foot_h = S(150);
	float body_h = igGetContentRegionAvail().y - foot_h - S(8);
	float left_w = S(280);
	igBeginChild_Str("##groups", v2(left_w, body_h), ImGuiChildFlags_Borders, 0);
	if (!(P.tab == PICK_SCENE && !P.scene_shown))
		groups_list(P.tab);
	igEndChild();
	igSameLine(0, S(10));
	igPushStyleVar_Vec2(ImGuiStyleVar_WindowPadding, v2(S(12), S(10)));
	igBeginChild_Str("##grid", v2(0, body_h), ImGuiChildFlags_Borders | ImGuiChildFlags_AlwaysUseWindowPadding, 0);
	grid(P.tab);
	igEndChild();
	igPopStyleVar(1);
	bool close = footer(foot_h);
	if (close || (igIsKeyPressed_Bool(ImGuiKey_Escape, false) && !P.adding))
		igCloseCurrentPopup();
	else if (igIsKeyPressed_Bool(ImGuiKey_Escape, false))
		P.adding = false;
	igEndPopup();
}

/* ---- the sound picker ---- */

static void sound_picker(void)
{
	ImGuiIO *io = igGetIO_Nil();
	bool music = P.t.sound == SOUND_MUSIC;
	struct audio_entry *list = music ? G.music : G.sounds;
	int n = music ? G.nmusic : G.nsounds;
	V2 size = v2(fminf(S(620), io->DisplaySize.x - S(40)), fminf(S(660), io->DisplaySize.y - S(40)));
	igSetNextWindowSize(size, ImGuiCond_Always);
	igSetNextWindowPos(v2(io->DisplaySize.x / 2, io->DisplaySize.y / 2), ImGuiCond_Always, v2(0.5f, 0.5f));
	igPushStyleVar_Vec2(ImGuiStyleVar_WindowPadding, v2(S(14), S(10)));
	bool open = igBeginPopupModal(music ? "Pick a track###sounds" : "Pick a sound###sounds", NULL, ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize);
	igPopStyleVar(1);
	if (!open)
		return;
	igAlignTextToFramePadding();
	igTextColored(colv(C_DIM), "%s for %s", music ? "A track" : !strcmp(P.t.key, "jingle") ? "A jingle (a sound)" : "A sound", target_words());
	igSameLine(0, 0);
	float fw = S(250);
	igSetCursorPosX(igGetCursorPosX() + igGetContentRegionAvail().x - fw);
	igSetNextItemWidth(fw);
	if (igIsWindowAppearing() && !UI.shot)
		igSetKeyboardFocusHere(0);
	igInputTextWithHint("##find", music ? "Search: a number or a name" : "Search: a number", P.find, sizeof P.find, 0, NULL, NULL);
	igSpacing();
	float foot_h = S(52);
	igBeginChild_Str("##list", v2(0, igGetContentRegionAvail().y - foot_h), ImGuiChildFlags_Borders, 0);
	int shown = 0;
	for (int i = 0; i < n; i++) {
		struct audio_entry *a = &list[i];
		if (!number_matches(a->no) || (!find_is_number() && find_text()[0] && !(a->name && name_matches(a->name))))
			continue;
		shown++;
		igPushID_Int(a->no);
		bool on = media_playing(P.t.sound, a->no);
		if (igSmallButton(on ? "\xE2\x96\xA0" : "\xE2\x96\xB6")) {
			media_play(P.t.sound, a->no);
			P.playing = 1;
		}
		igSameLine(0, S(10));
		igPushFont(F_MONO, MONO_SIZE);
		V2 p = igGetCursorScreenPos();
		float right = p.x + igGetContentRegionAvail().x;
		if (igSelectable_Bool("##row", P.sel_no == a->no, ImGuiSelectableFlags_AllowDoubleClick, v2(0, 0))) {
			P.sel_no = a->no;
			if (igIsMouseDoubleClicked_Nil(0))
				P.use_now = true;
		}
		ImDrawList *dl = igGetWindowDrawList();
		char no[16];
		snprintf(no, sizeof no, "%d", a->no);
		ImDrawList_AddText_FontPtr(dl, F_MONO, S(MONO_SIZE), v2(p.x, p.y), col(C_TEXT), no, NULL, 0, NULL);
		igPopFont();
		if (a->name)
			ImDrawList_AddText_FontPtr(dl, F_UI, S(UI_SIZE - 1), v2(p.x + S(64), p.y - S(1)), col(C_TEXT), a->name, NULL, 0, NULL);
		if (music && !a->has_loop) {
			const char *loop = "plays once";
			float w = ImFont_CalcTextSizeA(F_UI, S(14), FLT_MAX, 0, loop, NULL, NULL).x;
			ImDrawList_AddText_FontPtr(dl, F_UI, S(14), v2(right - w - S(6), p.y), col(C_FAINT), loop, NULL, 0, NULL);
		}
		if (P.sel_no == a->no && P.scroll_to_sel) {
			igSetScrollHereY(0.4f);
			P.scroll_to_sel = false;
		}
		igPopID();
	}
	if (!shown)
		igTextColored(colv(C_DIM), "Nothing matches \"%s\".", find_text());
	igEndChild();
	igSpacing();
	char value[32];
	bool is_cast;
	pick_value(value, sizeof value, &is_cast);
	igAlignTextToFramePadding();
	if (value[0]) {
		const char *name = music ? media_music_name(P.sel_no) : NULL;
		igTextColored(colv(C_FAINT), "Writes %s%s%s%s in %s.", value, name ? " (" : "", name ? name : "", name ? ")" : "", target_words());
	} else {
		igTextColored(colv(C_FAINT), "Click a row; a double click uses it; \xE2\x96\xB6 plays it.");
	}
	float bwid = S(96);
	igSameLine(0, 0);
	igSetCursorPosX(igGetCursorPosX() + igGetContentRegionAvail().x - bwid * 2 - S(8));
	bool close = false;
	if (igButton("Cancel", v2(bwid, 0)))
		close = true;
	igSameLine(0, S(8));
	igBeginDisabled(!value[0] || !doc_editable());
	igPushStyleColor_Vec4(ImGuiCol_Button, colv(C_ACCENT));
	igPushStyleColor_Vec4(ImGuiCol_ButtonHovered, colv(C_ACCENT_HI));
	igPushStyleColor_Vec4(ImGuiCol_ButtonActive, colv(C_ACCENT));
	igPushStyleColor_Vec4(ImGuiCol_Text, colv(C_ACCENT_INK));
	igPushStyleColor_Vec4(ImGuiCol_Border, colv(C_ACCENT));
	bool use = igButton("Use", v2(bwid, 0));
	igPopStyleColor(5);
	igEndDisabled();
	if ((use || P.use_now || (igIsKeyPressed_Bool(ImGuiKey_Enter, false) && !igIsAnyItemActive())) && value[0])
		close = apply() || close;
	P.use_now = false;
	if (close || igIsKeyPressed_Bool(ImGuiKey_Escape, false)) {
		if (P.playing)
			media_stop();
		P.playing = 0;
		igCloseCurrentPopup();
	}
	igEndPopup();
}

/* In the root window, after the rest of the frame. */
void picker_frame(void)
{
	if (P.want_open) {
		P.want_open = false;
		igOpenPopup_Str(P.sound ? "###sounds" : "Pick a picture##picker", 0);
	}
	if (P.sound)
		sound_picker();
	else
		picture_picker();
}

/* --do: picker[=KEY] (in fields.c), find=TEXT, ptab=NAME, tile=NO|PATH, group=N, showscene, use,
 * addcast[=NAME]. */
bool picker_do(const char *name, const char *arg)
{
	if (!strcmp(name, "find")) {
		snprintf(P.find, sizeof P.find, "%s", arg);
		P.list_top = true;
		if (!P.sound && find_is_number()) {
			int no = atoi(find_text());
			int t = pick_tab_of_cg(no);
			if (t >= 0 && (t != PICK_SCENE || P.scene_shown) && locate(no, NULL))
				select_cg(no);
		}
		return true;
	}
	if (!strcmp(name, "ptab")) {
		static const char *const keys[] = { "faces", "standing", "backgrounds", "small", "scene", "other", "mod" };
		for (int t = 0; t < PICK_TABS; t++) {
			if (!strcmp(arg, keys[t])) {
				P.tab = t;
				P.tab_set = true;
				return true;
			}
		}
		return false;
	}
	if (!strcmp(name, "group")) {
		P.group[P.tab] = atoi(arg);
		return P.group[P.tab] < tabs[P.tab].n;
	}
	if (!strcmp(name, "tile")) {
		if (P.sound) {
			P.sel_no = atoi(arg);
			P.scroll_to_sel = true;
			return true;
		}
		if (strchr(arg, '.')) {
			select_path(arg);
			return locate(0, P.sel_path);
		}
		select_cg(atoi(arg));
		return locate(P.sel_no, NULL);
	}
	if (!strcmp(name, "showscene")) {
		P.scene_shown = true;
		return true;
	}
	if (!strcmp(name, "use")) {
		P.use_now = true;
		return true;
	}
	if (!strcmp(name, "addcast")) {
		P.add_now = true;
		if (arg[0]) {
			P.adding = true;
			snprintf(P.add_name, sizeof P.add_name, "%s", arg);
		}
		return true;
	}
	return false;
}
