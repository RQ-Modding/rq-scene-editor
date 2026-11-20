/* The window: the top bar (the mod, the file, Save, Undo, Redo, the look), the scene files and the
 * file's labels on the left, the steps in the middle (a list, its nested lists as a tree), the
 * selected step on the right, and the problems and a status line at the bottom.
 *
 * Until each kind of step has its own fields, the right side edits a step as the JSON the file
 * holds; every change is a step to undo, and the file is written in the editor's layout. */
#include <float.h>
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "system4.h"
#include "system4/file.h"

#include "editor.h"

struct ui UI;

static char game_dir[1024];
static char game_title[256];
static int cur_mod = -1;
static int doc_gen;             /* a new file or a reload: the right side fills again */
static double next_scan, next_watch;

/* What a press waits for when the open file has unsaved changes. */
enum { P_NONE, P_FILE, P_MOD, P_QUIT, P_NEW, P_GAME };
static int pending;
static char pending_arg[1600];

enum { M_NONE, M_UNSAVED, M_LAYOUT, M_CHANGED, M_NEW };
static int modal_open;
static const char *const modal_names[] = { "", "Unsaved changes###unsaved", "The editor's layout###layout",
                                           "Changed on disk###changed", "New scene###new" };

/* The right side's JSON field. */
#define EDIT_MAX (256 * 1024)
static char edit_buf[EDIT_MAX];
static char edit_src[EDIT_MAX];
static char edit_for[256];
static int edit_at = -1, edit_gen = -1;
static char edit_why[256];

/* The steps shown, in order, for the arrow keys. */
static char (*vis)[256];
static int nvis, capvis;

static bool problems_folded;
static int want_mid_tab = -1;   /* 0 Steps, 1 Cast and file: opened next frame */
static bool cast_tab_shown;     /* the middle shows Cast and file: the right side shows the cast's pick */
static int last_problem_count;

static char new_name[80];
static char new_why[160];

/* ---- small helpers ---- */

static float fs(float base)
{
	return base * CORE.scale;
}

static float text_w(ImFont *f, float size, const char *s)
{
	return ImFont_CalcTextSizeA(f, size, FLT_MAX, 0, s, NULL, NULL).x;
}

/* s cut to w with … at the end when it does not fit (in out). */
static const char *fit(ImFont *f, float size, const char *s, float w, char *out, size_t n)
{
	if (text_w(f, size, s) <= w)
		return s;
	const char *ell = "\xE2\x80\xA6";
	float ew = text_w(f, size, ell);
	const char *rem = s;
	if (w > ew)
		ImFont_CalcTextSizeA(f, size, w - ew, 0, s, NULL, &rem);
	size_t k = (size_t)(rem - s);
	if (k + 4 > n)
		k = n > 4 ? n - 4 : 0;
	memcpy(out, s, k);
	snprintf(out + k, n - k, "%s", ell);
	return out;
}

/* The content's right edge in the window's own coordinates, for igSameLine. */
static float content_right(void)
{
	return igGetCursorPosX() + igGetContentRegionAvail().x;
}

static void draw_text(ImDrawList *dl, ImFont *f, float size, V2 pos, ImU32 c, const char *s, float max_x)
{
	char buf[1024];
	const char *t = fit(f, size, s, max_x - pos.x, buf, sizeof buf);
	ImDrawList_AddText_FontPtr(dl, f, size, pos, c, t, NULL, 0, NULL);
}

void ui_status(const char *fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	vsnprintf(UI.status, sizeof UI.status, fmt, ap);
	va_end(ap);
	UI.status_until = igGetTime() + 5;
	UI.status_bad = false;
}

void ui_error(const char *fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	vsnprintf(UI.status, sizeof UI.status, fmt, ap);
	va_end(ap);
	UI.status_until = igGetTime() + 8;
	UI.status_bad = true;
}

static struct mod *mod_now(void)
{
	return cur_mod >= 0 && cur_mod < NMODS ? &MODS[cur_mod] : NULL;
}

void ui_title(char *out, size_t n)
{
	struct mod *m = mod_now();
	if (D.open && m)
		snprintf(out, n, APP_NAME " \xE2\x80\x94 %s \xC2\xB7 %s%s", m->id, D.rel, doc_dirty() ? " \xE2\x97\x8F" : "");
	else
		snprintf(out, n, APP_NAME " " EDITOR_VERSION);
}

static void accent_button_push(void)
{
	igPushStyleColor_Vec4(ImGuiCol_Button, colv(C_ACCENT));
	igPushStyleColor_Vec4(ImGuiCol_ButtonHovered, colv(C_ACCENT_HI));
	igPushStyleColor_Vec4(ImGuiCol_ButtonActive, colv(C_ACCENT));
	igPushStyleColor_Vec4(ImGuiCol_Text, colv(C_ACCENT_INK));
	igPushStyleColor_Vec4(ImGuiCol_Border, colv(C_ACCENT));
}

static bool accent_button(const char *label, V2 size)
{
	accent_button_push();
	bool r = igButton(label, size);
	igPopStyleColor(5);
	return r;
}

/* A heading with a line to the right edge: "Scenes  4 ———". */
static void section(const char *title, const char *count)
{
	igSpacing();
	igTextUnformatted(title, NULL);
	if (count) {
		igSameLine(0, S(6));
		igTextColored(colv(C_FAINT), "%s", count);
	}
	igSameLine(0, S(8));
	V2 p = igGetCursorScreenPos();
	float right = igGetCursorScreenPos().x + igGetContentRegionAvail().x;
	float y = p.y + igGetTextLineHeight() / 2;
	ImDrawList_AddLine(igGetWindowDrawList(), v2(p.x, y), v2(right, y), col(C_LINE), S(1));
	igNewLine();
}

/* ---- selection ---- */

static void select_place(const char *place, bool scroll)
{
	snprintf(UI.sel, sizeof UI.sel, "%s", place ? place : "");
	if (scroll)
		UI.scroll_to_sel = true;
}

static cJSON *sel_step(cJSON **list, int *index)
{
	return D.root && UI.sel[0] ? place_find(D.root, UI.sel, list, index) : NULL;
}

/* The step that holds a list: steps[2] for steps[2].then or steps[2].choice[1].steps; "" for steps. */
static void parent_of_list(const char *list_place, char *out, size_t n)
{
	const char *cut = NULL;
	const char *marks[] = { ".then", ".else", ".choice[" };
	for (int i = 0; i < 3; i++) {
		const char *p = list_place, *q;
		while ((q = strstr(p, marks[i]))) {
			if (!cut || q > cut)
				cut = q;
			p = q + 1;
		}
	}
	if (!cut)
		out[0] = 0;
	else
		snprintf(out, n, "%.*s", (int)(cut - list_place), list_place);
}

/* ---- labels ---- */

struct label {
	char name[128];
	char place[256];
};
static struct label labels[256];
static int nlabels;

static void collect_labels(const cJSON *list, const char *lp)
{
	int k = 0;
	for (const cJSON *st = list ? list->child : NULL; st; st = st->next, k++) {
		char place[256];
		place_join(place, sizeof place, lp, k);
		const char *kind = step_kind(st);
		if (!kind)
			continue;
		if (!strcmp(kind, "label")) {
			const cJSON *v = cJSON_GetObjectItemCaseSensitive(st, "label");
			if (cJSON_IsString(v) && nlabels < (int)(sizeof labels / sizeof labels[0])) {
				snprintf(labels[nlabels].name, sizeof labels[0].name, "%s", v->valuestring);
				snprintf(labels[nlabels].place, sizeof labels[0].place, "%s", place);
				nlabels++;
			}
		} else if (!strcmp(kind, "if")) {
			char sub[256];
			const cJSON *l = cJSON_GetObjectItemCaseSensitive(st, "then");
			snprintf(sub, sizeof sub, "%s.then", place);
			if (cJSON_IsArray(l))
				collect_labels(l, sub);
			l = cJSON_GetObjectItemCaseSensitive(st, "else");
			snprintf(sub, sizeof sub, "%s.else", place);
			if (cJSON_IsArray(l))
				collect_labels(l, sub);
		} else if (!strcmp(kind, "choice")) {
			const cJSON *rows = cJSON_GetObjectItemCaseSensitive(st, "choice");
			int r = 0;
			for (const cJSON *row = cJSON_IsArray(rows) ? rows->child : NULL; row; row = row->next, r++) {
				const cJSON *l = cJSON_IsObject(row) ? cJSON_GetObjectItemCaseSensitive(row, "steps") : NULL;
				if (cJSON_IsArray(l)) {
					char sub[256];
					snprintf(sub, sizeof sub, "%s.choice[%d].steps", place, r);
					collect_labels(l, sub);
				}
			}
		}
	}
}

static const char *label_place(const char *name)
{
	for (int i = 0; i < nlabels; i++)
		if (!strcmp(labels[i].name, name))
			return labels[i].place;
	return NULL;
}

int ui_label_count(void)
{
	return nlabels;
}

const char *ui_label_name(int i)
{
	return i >= 0 && i < nlabels ? labels[i].name : "";
}

const char *ui_mod_id(void)
{
	struct mod *m = mod_now();
	return m ? m->id : "mod";
}

/* ---- files and mods ---- */

static void open_file(const char *rel)
{
	int f = file_find(rel);
	if (f < 0) {
		doc_close();
		UI.sel[0] = 0;
		return;
	}
	doc_open(FILES[f].path, FILES[f].rel);
	doc_gen++;
	UI.sel[0] = 0;
	if (place_find(D.root, "steps[0]", NULL, NULL))
		select_place("steps[0]", false);
	struct mod *m = mod_now();
	if (!UI.shot && m) {
		settings_set("mod", m->outside ? m->dir : m->id);
		settings_set("file", rel);
	}
}

static void open_mod(int i, const char *file)
{
	cur_mod = i;
	struct mod *m = mod_now();
	media_set_mod(m ? m->dir : NULL);
	files_scan(m);
	next_scan = igGetTime() + 2;
	int f = file ? file_find(file) : -1;
	if (f < 0 && !UI.shot) {
		const char *last_mod = settings_get("mod"), *last_file = settings_get("file");
		if (m && last_mod && last_file && (!strcmp(last_mod, m->id) || !strcmp(last_mod, m->dir)))
			f = file_find(last_file);
	}
	if (f < 0 && NFILES > 0)
		f = 0;
	if (f >= 0) {
		open_file(FILES[f].rel);
	} else {
		doc_close();
		UI.sel[0] = 0;
		doc_gen++;
		if (!UI.shot && m)
			settings_set("mod", m->outside ? m->dir : m->id);
	}
}

static void rescan(void)
{
	struct mod *m = mod_now();
	if (m)
		files_scan(m);
	next_scan = igGetTime() + 2;
}

void ui_init(const char *game, const char *mod_dir, const char *mod, const char *file, const char *select)
{
	snprintf(game_dir, sizeof game_dir, "%s", game);
	snprintf(game_title, sizeof game_title, "%s", G.title[0] ? G.title : "Rance Quest Magnum");
	const char *outside = mod_dir ? mod_dir : (UI.shot ? NULL : settings_get("mod_dir"));
	mods_scan(game_dir, outside);
	int m = -1;
	if (mod_dir)
		m = mod_find(mod_dir);
	if (m < 0 && mod)
		m = mod_find(mod);
	if (m < 0 && !UI.shot)
		m = mod_find(settings_get("mod"));
	if (m < 0) {
		/* The first mod that has scene files, else the first. */
		for (int i = 0; i < NMODS && m < 0; i++) {
			files_scan(&MODS[i]);
			if (NFILES > 0)
				m = i;
		}
		if (m < 0 && NMODS > 0)
			m = 0;
	}
	if (m >= 0)
		open_mod(m, file);
	if (select && select[0])
		select_place(select, true);
}

/* ---- what a press does once the unsaved changes are settled ---- */

static void do_pending(int what, const char *arg)
{
	switch (what) {
	case P_FILE:
		open_file(arg);
		break;
	case P_MOD:
		open_mod(mod_find(arg), NULL);
		break;
	case P_QUIT:
		UI.quit = true;
		break;
	case P_NEW:
		new_name[0] = 0;
		new_why[0] = 0;
		modal_open = M_NEW;
		break;
	case P_GAME:
		if (win_relaunch(arg))
			UI.quit = true;
		else
			ui_error("Could not start the editor again on that game");
		break;
	}
}

static void want(int what, const char *arg)
{
	if (doc_dirty()) {
		pending = what;
		snprintf(pending_arg, sizeof pending_arg, "%s", arg ? arg : "");
		modal_open = M_UNSAVED;
		return;
	}
	do_pending(what, arg ? arg : "");
}

bool ui_wants_quit(void)
{
	if (!doc_dirty())
		return true;
	want(P_QUIT, NULL);
	return false;
}

static bool really_save(void)
{
	char why[300];
	if (!doc_save(why, sizeof why)) {
		ui_error("Not saved: %s", why);
		return false;
	}
	ui_status("Saved %s", D.rel);
	rescan();
	return true;
}

/* Save; a file not yet in the editor's layout asks once first. Returns true when it saved now. */
static bool save_now(void)
{
	if (!doc_editable() || !doc_dirty() || UI.shot)
		return false;
	if (!D.styled) {
		modal_open = M_LAYOUT;
		return false;
	}
	return really_save();
}

static void after_save_continue(void)
{
	int what = pending;
	pending = P_NONE;
	if (what != P_NONE)
		do_pending(what, pending_arg);
}

/* ---- changing the steps ---- */

static void changed(const char *place)
{
	doc_changed(place);
}

static void op_delete(void)
{
	cJSON *list;
	int i;
	if (!doc_editable() || !sel_step(&list, &i))
		return;
	char lp[256], old[256];
	int dummy;
	snprintf(old, sizeof old, "%s", UI.sel);
	place_list(UI.sel, lp, sizeof lp, &dummy);
	cJSON_DeleteItemFromArray(list, i);
	int n = cJSON_GetArraySize(list);
	char where[256];
	if (n > 0)
		place_join(where, sizeof where, lp, i < n ? i : n - 1);
	else
		parent_of_list(lp, where, sizeof where);
	changed(old);
	select_place(where, true);
}

static void op_duplicate(void)
{
	cJSON *list;
	int i;
	cJSON *st = sel_step(&list, &i);
	if (!doc_editable() || !st)
		return;
	char lp[256];
	int dummy;
	place_list(UI.sel, lp, sizeof lp, &dummy);
	cJSON_InsertItemInArray(list, i + 1, cJSON_Duplicate(st, true));
	char where[256];
	place_join(where, sizeof where, lp, i + 1);
	changed(where);
	select_place(where, true);
}

static void op_move(int dir)
{
	cJSON *list;
	int i;
	if (!doc_editable() || !sel_step(&list, &i))
		return;
	int n = cJSON_GetArraySize(list), to = i + dir;
	if (to < 0 || to >= n)
		return;
	char lp[256];
	int dummy;
	place_list(UI.sel, lp, sizeof lp, &dummy);
	cJSON *it = cJSON_DetachItemFromArray(list, i);
	cJSON_InsertItemInArray(list, to, it);
	char where[256];
	place_join(where, sizeof where, lp, to);
	changed(where);
	select_place(where, true);
}

/* A new step of a kind after the selected one (or at the end), from the kind's JSON. */
static void op_add(const struct kind_info *k)
{
	if (!doc_editable())
		return;
	char text[512], face[160] = "2005";
	const cJSON *cast = cJSON_GetObjectItemCaseSensitive(D.root, "cast");
	if (cJSON_IsObject(cast) && cast->child && cast->child->string)
		snprintf(face, sizeof face, "\"%s\"", cast->child->string);
	struct mod *m = mod_now();
	/* @cast and @mod in the kind's JSON. */
	const char *src = k->make;
	size_t o = 0;
	while (*src && o + 1 < sizeof text) {
		const char *rep = NULL;
		size_t skip = 0;
		if (!strncmp(src, "\"@cast\"", 7)) {
			rep = face;
			skip = 7;
		} else if (!strncmp(src, "@mod", 4)) {
			rep = m ? m->id : "mod";
			skip = 4;
		}
		if (rep) {
			o += (size_t)snprintf(text + o, sizeof text - o, "%s", rep);
			if (o >= sizeof text)
				o = sizeof text - 1;
			src += skip;
		} else {
			text[o++] = *src++;
		}
	}
	text[o] = 0;
	cJSON *st = cJSON_Parse(text);
	if (!st)
		return;
	cJSON *list;
	int i;
	char lp[256] = "steps", where[256];
	int at;
	if (sel_step(&list, &i)) {
		int dummy;
		place_list(UI.sel, lp, sizeof lp, &dummy);
		at = i + 1;
	} else {
		list = doc_steps();
		if (!list) {
			cJSON_Delete(st);
			return;
		}
		at = cJSON_GetArraySize(list);
	}
	cJSON_InsertItemInArray(list, at, st);
	place_join(where, sizeof where, lp, at);
	changed(where);
	select_place(where, true);
}

static void op_undo(void)
{
	char place[256];
	if (doc_undo(place, sizeof place))
		select_place(place, true);
}

static void op_redo(void)
{
	char place[256];
	if (doc_redo(place, sizeof place))
		select_place(place, true);
}

/* The JSON field's text as the selected step. */
static bool apply_edit(void)
{
	cJSON *list;
	int i;
	if (!doc_editable() || !sel_step(&list, &i))
		return false;
	cJSON *st = NULL;
	char why[256];
	if (!scene_parse(edit_buf, &st, why, sizeof why)) {
		snprintf(edit_why, sizeof edit_why, "%s", why);
		return false;
	}
	if (!cJSON_IsObject(st)) {
		cJSON_Delete(st);
		snprintf(edit_why, sizeof edit_why, "expected a step: an object such as { \"say\": 2000, \"text\": \"Hello.\" }");
		return false;
	}
	cJSON_ReplaceItemInArray(list, i, st);
	edit_why[0] = 0;
	changed(UI.sel);
	edit_gen = -1;   /* fill again in the editor's layout */
	return true;
}

/* ---- the keys ---- */

static bool any_popup(void)
{
	return igIsPopupOpen_Str("", ImGuiPopupFlags_AnyPopupId | ImGuiPopupFlags_AnyPopupLevel);
}

static void keys(void)
{
	if (any_popup() || UI.shot)
		return;
	ImGuiIO *io = igGetIO_Nil();
	if (igIsKeyChordPressed_Nil(ImGuiMod_Ctrl | ImGuiKey_S)) {
		if (io->WantTextInput && strcmp(edit_buf, edit_src))
			apply_edit();
		save_now();
	}
	if (io->WantTextInput)
		return;
	if (play_active()) {
		/* The stage playing: Space or Enter for the next page, Escape to stop. */
		if (igIsKeyPressed_Bool(ImGuiKey_Escape, false))
			play_stop();
		if (igIsKeyPressed_Bool(ImGuiKey_Space, false) || igIsKeyPressed_Bool(ImGuiKey_Enter, false)
		    || igIsKeyPressed_Bool(ImGuiKey_KeypadEnter, false))
			play_key();
	}
	if (igIsKeyChordPressed_Nil(ImGuiMod_Ctrl | ImGuiKey_Z))
		op_undo();
	if (igIsKeyChordPressed_Nil(ImGuiMod_Ctrl | ImGuiKey_Y) || igIsKeyChordPressed_Nil(ImGuiMod_Ctrl | ImGuiMod_Shift | ImGuiKey_Z))
		op_redo();
	if (igIsKeyChordPressed_Nil(ImGuiMod_Ctrl | ImGuiKey_D))
		op_duplicate();
	if (igIsKeyPressed_Bool(ImGuiKey_Delete, false))
		op_delete();
	if (igIsKeyChordPressed_Nil(ImGuiMod_Alt | ImGuiKey_UpArrow))
		op_move(-1);
	else if (igIsKeyChordPressed_Nil(ImGuiMod_Alt | ImGuiKey_DownArrow))
		op_move(1);
	else if (!io->KeyAlt && !io->KeyCtrl && nvis > 0) {
		int dir = igIsKeyPressed_Bool(ImGuiKey_UpArrow, true) ? -1 : igIsKeyPressed_Bool(ImGuiKey_DownArrow, true) ? 1 : 0;
		if (dir) {
			int at = -1;
			for (int i = 0; i < nvis; i++)
				if (!strcmp(vis[i], UI.sel))
					at = i;
			at = at < 0 ? 0 : at + dir;
			if (at >= 0 && at < nvis)
				select_place(vis[at], true);
		}
	}
}

/* ---- the top bar ---- */

/* Undo and Redo: a half circle with its arrow head at the left end (undo) or the right (redo),
 * drawn, as the fonts' own arrows are too small to read at the bar's size. */
static bool turn_button(const char *id, bool redo)
{
	bool pressed = igButton(id, v2(S(36), igGetFrameHeight()));
	V2 a = igGetItemRectMin(), b = igGetItemRectMax();
	ImDrawList *dl = igGetWindowDrawList();
	ImU32 ink = igGetColorU32_Col(ImGuiCol_Text, 1);
	float r = S(6);
	V2 c = v2(floorf((a.x + b.x) / 2) + 0.5f, floorf((a.y + b.y) / 2 + S(2.5f)) + 0.5f);
	ImDrawList_PathArcTo(dl, c, r, 3.14159265f, 6.2831853f, 18);
	ImDrawList_PathStroke(dl, ink, S(1.7f), 0);
	float x = redo ? c.x + r : c.x - r;
	ImDrawList_AddTriangleFilled(dl, v2(x - S(3.8f), c.y - S(1)), v2(x + S(3.8f), c.y - S(1)), v2(x, c.y + S(4.5f)), ink);
	return pressed;
}

static void look_switch(void)
{
	float w = S(54), h = S(26);
	V2 p = igGetCursorScreenPos();
	if (igInvisibleButton("##look", v2(w, h), 0)) {
		look_apply(LOOK == LOOK_COPANDON ? LOOK_PARCHMENT : LOOK_COPANDON);
		if (!UI.shot)
			settings_set("look", look_keys[LOOK]);
	}
	if (igIsItemHovered(ImGuiHoveredFlags_DelayShort))
		igSetTooltip("%s", LOOK == LOOK_COPANDON ? "The dark look (Copandon). Click for the light one (Parchment)."
		                                         : "The light look (Parchment). Click for the dark one (Copandon).");
	ImDrawList *dl = igGetWindowDrawList();
	ImDrawList_AddRectFilled(dl, p, v2(p.x + w, p.y + h), col(C_FIELD), h / 2, 0);
	ImDrawList_AddRect(dl, p, v2(p.x + w, p.y + h), col(C_BORDER), h / 2, S(1), 0);
	bool dark = LOOK == LOOK_COPANDON;
	float r = h / 2 - S(3);
	V2 c = v2(dark ? p.x + h / 2 : p.x + w - h / 2, p.y + h / 2);
	ImDrawList_AddCircleFilled(dl, c, r, col(C_RAISE), 0);
	const char *glyph = dark ? "\xE2\x98\xBE" : "\xE2\x98\x80";
	float size = fs(14);
	float gw = text_w(F_UI, size, glyph);
	ImDrawList_AddText_FontPtr(dl, F_UI, size, v2(c.x - gw / 2, c.y - size / 2 - S(1)), col(C_ACCENT), glyph, NULL, 0, NULL);
}

static void mod_combo(void)
{
	struct mod *m = mod_now();
	igSetNextItemWidth(S(200));
	if (igBeginCombo("##mod", "", ImGuiComboFlags_CustomPreview | ImGuiComboFlags_HeightLarge)) {
		for (int i = 0; i < NMODS; i++) {
			char label[512];
			snprintf(label, sizeof label, "%s  (%s)%s##m%d", MODS[i].name, MODS[i].id, MODS[i].outside ? "  \xC2\xB7 outside the game" : "", i);
			if (igSelectable_Bool(label, i == cur_mod, 0, v2(0, 0)) && i != cur_mod)
				want(P_MOD, MODS[i].dir);
			if (MODS[i].outside && igIsItemHovered(ImGuiHoveredFlags_DelayShort))
				igSetTooltip("%s", MODS[i].dir);
		}
		if (!NMODS)
			igTextDisabled("No mod in %s/mods", game_dir);
		igSeparator();
		if (igSelectable_Bool("Another mod's folder...", false, 0, v2(0, 0)) && !UI.shot) {
			char dir[1024];
			if (win_pick_folder("Pick a mod's folder (the one with mod.json)", dir, sizeof dir)) {
				struct mod probe;
				if (!mod_read(dir, &probe)) {
					ui_error("No mod.json in %s", dir);
				} else {
					settings_set("mod_dir", dir);
					char keep[1024];
					snprintf(keep, sizeof keep, "%s", m ? m->dir : "");
					mods_scan(game_dir, dir);
					cur_mod = mod_find(keep);
					want(P_MOD, probe.dir);
				}
			}
		}
		igEndCombo();
	}
	if (igBeginComboPreview()) {
		if (m) {
			igTextUnformatted(m->name, NULL);
			igSameLine(0, S(8));
			igTextColored(colv(C_FAINT), "(%s)", m->id);
		} else {
			igTextColored(colv(C_FAINT), "No mod");
		}
		igEndComboPreview();
	}
}

static void top_bar(float h)
{
	igPushStyleVar_Vec2(ImGuiStyleVar_WindowPadding, v2(S(12), (h - igGetFrameHeight()) / 2));
	igBeginChild_Str("##top", v2(0, h), ImGuiChildFlags_AlwaysUseWindowPadding,
	                 ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
	igPopStyleVar(1);
	mod_combo();

	igSameLine(0, S(14));
	igAlignTextToFramePadding();
	if (D.open) {
		const char *slash = strrchr(D.rel, '/');
		if (slash) {
			char dir[512];
			snprintf(dir, sizeof dir, "%.*s", (int)(slash - D.rel + 1), D.rel);
			igTextColored(colv(C_FAINT), "%s", dir);
			igSameLine(0, 0);
		}
		igTextUnformatted(slash ? slash + 1 : D.rel, NULL);
		if (doc_dirty()) {
			igSameLine(0, S(6));
			igTextColored(colv(C_ACCENT), "\xE2\x97\x8F");
			if (igIsItemHovered(0))
				igSetTooltip("Changes not saved yet");
		}
		if (D.readonly) {
			igSameLine(0, S(8));
			igTextColored(colv(C_WARN), "read-only");
		}
	} else {
		igTextColored(colv(C_FAINT), "No scene open");
	}

	igSameLine(0, S(14));
	igBeginDisabled(!doc_editable() || !doc_dirty());
	if (igButton("Save", v2(0, 0)))
		save_now();
	igEndDisabled();
	igSetItemTooltip("Save the file (Ctrl+S)");
	igSameLine(0, S(6));
	igBeginDisabled(!doc_can_undo());
	bool undo = turn_button("##undo", false);
	igEndDisabled();
	if (undo)
		op_undo();
	igSetItemTooltip("Undo (Ctrl+Z)");
	igSameLine(0, S(6));
	igBeginDisabled(!doc_can_redo());
	bool redo = turn_button("##redo", true);
	igEndDisabled();
	if (redo)
		op_redo();
	igSetItemTooltip("Redo (Ctrl+Y)");

	float right = igGetWindowWidth() - S(12) - S(54);
	igSameLine(right, 0);
	look_switch();
	igEndChild();
	V2 a = igGetItemRectMin(), b = igGetItemRectMax();
	ImDrawList_AddLine(igGetWindowDrawList(), v2(a.x, b.y - S(0.5f)), v2(b.x, b.y - S(0.5f)), col(C_LINE), S(1));
}

/* ---- the left side: scene files and labels ---- */

static void file_row(int i)
{
	struct scene_file *f = &FILES[i];
	bool sel = D.open && !strcmp(D.rel, f->rel);
	igPushID_Int(i);
	float h = S(30);
	float w = igGetContentRegionAvail().x;
	V2 p = igGetCursorScreenPos();
	if (igSelectable_Bool("##file", sel, 0, v2(w, h)) && !sel)
		want(P_FILE, f->rel);
	if (igIsItemHovered(ImGuiHoveredFlags_DelayShort))
		igSetTooltip("%s%s%s", f->rel, f->read ? "" : "\n", f->read ? "" : f->why);
	ImDrawList *dl = igGetWindowDrawList();
	if (sel)
		ImDrawList_AddRectFilled(dl, p, v2(p.x + S(3), p.y + h), col(C_SEL_BAR), 0, 0);
	float size = igGetFontSize(), y = p.y + (h - size) / 2;
	ImDrawList_AddText_FontPtr(dl, F_UI, size, v2(p.x + S(10), y), col(C_FAINT), "\xE2\x96\xA4", NULL, 0, NULL);
	const char *name = f->in_scenes ? f->rel + 7 : f->rel;
	char meta[64];
	if (f->read)
		snprintf(meta, sizeof meta, "%d line%s", f->lines, f->lines == 1 ? "" : "s");
	else
		snprintf(meta, sizeof meta, "%s", "cannot be read");
	float msize = fs(13), mw = text_w(F_UI, msize, meta);
	draw_text(dl, F_UI, size, v2(p.x + S(32), y), col(sel ? C_TEXT : C_TEXT), name, p.x + w - mw - S(16));
	ImDrawList_AddText_FontPtr(dl, F_UI, msize, v2(p.x + w - mw - S(8), p.y + (h - msize) / 2), col(f->read ? C_FAINT : C_BAD),
	                           meta, NULL, 0, NULL);
	igPopID();
}

static void left_side(void)
{
	char count[32];
	snprintf(count, sizeof count, "%d", NFILES);
	section("Scenes", count);
	igPushStyleVar_Vec2(ImGuiStyleVar_ItemSpacing, v2(S(8), S(2)));
	for (int i = 0; i < NFILES; i++)
		file_row(i);
	igPopStyleVar(1);
	if (!NFILES && mod_now()) {
		igPushTextWrapPos(0);
		igTextColored(colv(C_FAINT), "No scene in this mod yet. New scene writes scenes/<name>.json.");
		igPopTextWrapPos();
	}
	igSpacing();
	igBeginDisabled(!mod_now());
	if (igButton("+ New scene", v2(0, 0)))
		want(P_NEW, NULL);
	igEndDisabled();

	igSpacing();
	section("Labels in this file", NULL);
	if (!nlabels) {
		igPushTextWrapPos(0);
		igTextColored(colv(C_FAINT), "None. A label marks a place that a go-to or Play from here can start at.");
		igPopTextWrapPos();
	}
	for (int i = 0; i < nlabels; i++) {
		igPushID_Int(i);
		char label[300];
		snprintf(label, sizeof label, "\xE2\x97\x86  %s", labels[i].name);
		igPushStyleColor_Vec4(ImGuiCol_Text, colv(C_T_FLOW));
		if (igSelectable_Bool(label, !strcmp(UI.sel, labels[i].place), 0, v2(0, 0)))
			select_place(labels[i].place, true);
		igPopStyleColor(1);
		igSameLine(0, S(8));
		igTextColored(colv(C_FAINT), "%s", labels[i].place);
		igPopID();
	}
}

/* ---- the middle: the steps ---- */

static void vis_add(const char *place)
{
	if (nvis == capvis) {
		capvis = capvis ? capvis * 2 : 256;
		vis = xrealloc(vis, (size_t)capvis * sizeof *vis);
	}
	snprintf(vis[nvis++], 256, "%s", place);
}

static float tag_pill(ImDrawList *dl, V2 p, const char *label, int group, float h)
{
	float size = fs(11.5f);
	float tw = text_w(F_BOLD, size, label);
	float w = tw + S(14) > S(66) ? tw + S(14) : S(66);
	ImDrawList_AddRectFilled(dl, p, v2(p.x + w, p.y + h), col_a(tag_colour(group), 0.16f), S(3), 0);
	ImDrawList_AddText_FontPtr(dl, F_BOLD, size, v2(p.x + (w - tw) / 2, p.y + (h - size) / 2 - S(0.5f)),
	                           col(tag_colour(group)), label, NULL, 0, NULL);
	return w;
}

static bool is_kind(const char *kind, const char *a)
{
	return kind && !strcmp(kind, a);
}

static bool picture_kind(const char *kind)
{
	return is_kind(kind, "background") || is_kind(kind, "wallpaper") || is_kind(kind, "mask_wallpaper")
	    || is_kind(kind, "portrait") || is_kind(kind, "mini") || is_kind(kind, "cg") || is_kind(kind, "background_in");
}

/* A pill after a line's text: "+1 page". */
static float small_pill(ImDrawList *dl, V2 p, const char *label, float h, ImU32 ink)
{
	float size = fs(12.5f);
	float tw = text_w(F_UI, size, label);
	ImDrawList_AddRect(dl, p, v2(p.x + tw + S(14), p.y + h), col_a(C_FAINT, 0.7f), h / 2, S(1), 0);
	ImDrawList_AddText_FontPtr(dl, F_UI, size, v2(p.x + S(7), p.y + (h - size) / 2 - S(0.5f)), ink, label, NULL, 0, NULL);
	return tw + S(14);
}

/* A page on one line of the list in the game's font, its line breaks as ↵ in the window's (the
 * game's font has no such sign), cut with … at max_x. Returns where it ends. */
static float draw_page_line(ImDrawList *dl, V2 pos, float gsize, ImU32 c, const char *s, float max_x)
{
	const char *mark = " \xE2\x86\xB5 ";
	float usize = gsize * 0.85f, mw = text_w(F_UI, usize, mark), x = pos.x;
	char seg[1200], cut[1200];
	for (const char *p = s; *p;) {
		const char *nl = strchr(p, '\n');
		size_t len = nl ? (size_t)(nl - p) : strlen(p);
		snprintf(seg, sizeof seg, "%.*s", (int)(len < sizeof seg ? len : sizeof seg - 1), p);
		const char *shown = fit(F_GAME, gsize, seg, max_x - x, cut, sizeof cut);
		ImDrawList_AddText_FontPtr(dl, F_GAME, gsize, v2(x, pos.y), c, shown, NULL, 0, NULL);
		x += text_w(F_GAME, gsize, shown);
		if (shown != seg || !nl)
			return x;
		if (x + mw + text_w(F_GAME, gsize, "\xE2\x80\xA6") > max_x) {
			ImDrawList_AddText_FontPtr(dl, F_GAME, gsize, v2(x, pos.y), c, "\xE2\x80\xA6", NULL, 0, NULL);
			return x + text_w(F_GAME, gsize, "\xE2\x80\xA6");
		}
		ImDrawList_AddText_FontPtr(dl, F_UI, usize, v2(x, pos.y + (gsize - usize) * 0.55f), col(C_FAINT), mark, NULL, 0, NULL);
		x += mw;
		p = nl + 1;
	}
	return x;
}

/* A page on one line of the list: its line breaks as ↵ (in out). */
static const char *one_line(const char *s, char *out, size_t n)
{
	size_t o = 0;
	for (const char *p = s; *p && o + 6 < n; p++) {
		if (*p == '\n') {
			memcpy(out + o, " \xE2\x86\xB5 ", 5);
			o += 5;
		} else {
			out[o++] = *p;
		}
	}
	out[o] = 0;
	return out;
}

/* A round play button over a row's selectable (right after it, on its line); true when pressed. */
static bool row_play(ImDrawList *dl, V2 c, float r, bool playing, const char *id)
{
	igSameLine(0, 0);
	igSetCursorScreenPos(v2(c.x - r, c.y - r));
	bool pressed = igInvisibleButton(id, v2(r * 2, r * 2), 0);
	bool hot = igIsItemHovered(0);
	ImDrawList_AddCircleFilled(dl, c, r, hot ? col(C_BTN_HI) : col(C_BTN), 20);
	ImU32 ink = playing ? col(C_PLAY) : col(C_TEXT);
	if (playing) {
		float s = r * 0.42f;
		ImDrawList_AddRectFilled(dl, v2(c.x - s, c.y - s), v2(c.x + s, c.y + s), ink, S(1), 0);
	} else {
		float s = r * 0.48f;
		ImDrawList_AddTriangleFilled(dl, v2(c.x - s * 0.7f, c.y - s), v2(c.x - s * 0.7f, c.y + s), v2(c.x + s, c.y), ink);
	}
	if (hot)
		igSetTooltip(playing ? "Stop" : "Play it here (the game's file, through this computer's sound)");
	return pressed;
}

/* ---- moving a step by dragging it ---- */

#define DRAG_STEP "RQSE_STEP"

/* A move asked by a drop, done after the list is drawn (the list is walked while drawing). */
static struct {
	bool on;
	char from[256];      /* the step's place */
	char list[256];      /* the list it goes into */
	int index;           /* before this step of that list */
} move_req;

/* A list by its place: steps, steps[2].then, steps[2].else, steps[2].choice[1].steps. */
static cJSON *list_find(const char *lp)
{
	if (!strcmp(lp, "steps"))
		return doc_steps();
	char head[256];
	size_t n = strlen(lp);
	const char *tails[] = { ".then", ".else" };
	for (int i = 0; i < 2; i++) {
		size_t k = strlen(tails[i]);
		if (n > k && !strcmp(lp + n - k, tails[i])) {
			snprintf(head, sizeof head, "%.*s", (int)(n - k), lp);
			cJSON *l = cJSON_GetObjectItemCaseSensitive(place_find(D.root, head, NULL, NULL), tails[i] + 1);
			return cJSON_IsArray(l) ? l : NULL;
		}
	}
	const char *c = strstr(lp, ".choice[");
	const char *last = NULL;
	while (c) {
		last = c;
		c = strstr(c + 1, ".choice[");
	}
	if (!last || n < 6 || strcmp(lp + n - 6, ".steps"))
		return NULL;
	snprintf(head, sizeof head, "%.*s", (int)(last - lp), lp);
	cJSON *rows = cJSON_GetObjectItemCaseSensitive(place_find(D.root, head, NULL, NULL), "choice");
	cJSON *row = cJSON_IsArray(rows) ? cJSON_GetArrayItem(rows, atoi(last + 8)) : NULL;
	cJSON *l = cJSON_IsObject(row) ? cJSON_GetObjectItemCaseSensitive(row, "steps") : NULL;
	return cJSON_IsArray(l) ? l : NULL;
}

struct find_ptr { const cJSON *want; char place[256]; };

static void find_ptr_fn(cJSON *st, const char *place, void *ud)
{
	struct find_ptr *f = ud;
	if (st == f->want)
		snprintf(f->place, sizeof f->place, "%s", place);
}

/* The step at `from` into list `lp` before its step `index` (at its end past the last). */
static bool op_move_to(const char *from, const char *lp, int index)
{
	cJSON *src_list, *dst = list_find(lp);
	int si;
	cJSON *st = place_find(D.root, from, &src_list, &si);
	size_t fl = strlen(from);
	if (!doc_editable() || !st || !dst)
		return false;
	/* Not into its own branches. */
	if (!strncmp(lp, from, fl) && lp[fl] == '.') {
		ui_status("The step stays: a step does not go into its own branches");
		return false;
	}
	cJSON *before = cJSON_GetArrayItem(dst, index);
	if (before == st)
		return false;
	cJSON_DetachItemFromArray(src_list, si);
	int at = 0;
	cJSON *c = dst->child;
	while (c && c != before) {
		c = c->next;
		at++;
	}
	cJSON_InsertItemInArray(dst, at, st);
	struct find_ptr f = { st, "" };
	steps_each(doc_steps(), "steps", find_ptr_fn, &f);
	changed(f.place);
	select_place(f.place, true);
	return true;
}

static void drop_line(float x0, float x1, float y)
{
	ImDrawList *dl = igGetWindowDrawList();
	ImDrawList_AddLine(dl, v2(x0, y), v2(x1, y), col(C_ACCENT), S(2.5f));
	ImDrawList_AddCircleFilled(dl, v2(x0, y), S(4), col(C_ACCENT), 12);
}

/* A row as a target: the dragged step goes before or after it, by the mouse's half of the row. */
static void step_drop_target(const char *place, V2 a, V2 b, float x0)
{
	if (!igBeginDragDropTarget())
		return;
	const ImGuiPayload *pl = igAcceptDragDropPayload(DRAG_STEP, ImGuiDragDropFlags_AcceptBeforeDelivery | ImGuiDragDropFlags_AcceptNoDrawDefaultRect);
	if (pl) {
		bool after = igGetMousePos().y > (a.y + b.y) / 2;
		drop_line(x0, b.x, after ? b.y + S(1) : a.y - S(1));
		if (pl->Delivery) {
			char lp[256];
			int k;
			if (place_list(place, lp, sizeof lp, &k)) {
				move_req.on = true;
				snprintf(move_req.from, sizeof move_req.from, "%s", (const char *)pl->Data);
				snprintf(move_req.list, sizeof move_req.list, "%s", lp);
				move_req.index = after ? k + 1 : k;
			}
		}
	}
	igEndDragDropTarget();
}

static bool want_add_menu;   /* the "+" on a row: the add menu opens next frame */

static void step_row(cJSON *st, const char *place, int index, int depth, const cJSON *cast)
{
	igPushID_Str(place);
	bool sel = !strcmp(UI.sel, place);
	float h = S(40);
	V2 p = igGetCursorScreenPos();
	float w = igGetContentRegionAvail().x;
	igSetNextItemAllowOverlap();
	if (igSelectable_Bool("##row", sel, ImGuiSelectableFlags_AllowDoubleClick, v2(w, h))) {
		select_place(place, false);
		const char *kind = step_kind(st);
		if (igIsMouseDoubleClicked_Nil(ImGuiMouseButton_Left) && kind && !strcmp(kind, "goto")) {
			const cJSON *v = cJSON_GetObjectItemCaseSensitive(st, "goto");
			const char *to = cJSON_IsString(v) ? label_place(v->valuestring) : NULL;
			if (to)
				select_place(to, true);
		}
	}
	if (doc_editable() && igBeginDragDropSource(0)) {
		igSetDragDropPayload(DRAG_STEP, place, strlen(place) + 1, 0);
		char *sum = step_summary(st, cast);
		char cut[200];
		igTextUnformatted(fit(F_UI, igGetFontSize(), sum, S(360), cut, sizeof cut), NULL);
		free(sum);
		igEndDragDropSource();
	}
	if (doc_editable())
		step_drop_target(place, p, v2(p.x + w, p.y + h), p.x + S(42) + (float)depth * S(22));
	if (sel && UI.scroll_to_sel) {
		igSetScrollHereY(0.4f);
		UI.scroll_to_sel = false;
	}
	vis_add(place);
	ImDrawList *dl = igGetWindowDrawList();
	V2 b = v2(p.x + w, p.y + h);
	if (sel) {
		ImDrawList_AddRect(dl, p, b, col_a(C_ACCENT, 0.6f), S(3), S(1), 0);
		ImDrawList_AddRectFilled(dl, p, v2(p.x + S(3), b.y), col(C_SEL_BAR), 0, 0);
	}
	/* The step Play from here is on: a mark, and the list brought to it when it moves on. */
	if (play_on() && !strcmp(play_step_place(), place)) {
		static char shown_playing[256];
		ImDrawList_AddRectFilled(dl, p, b, col_a(C_ACCENT, 0.10f), S(3), 0);
		ImDrawList_AddText_FontPtr(dl, F_UI, fs(12), v2(p.x + S(7), p.y + (h - fs(12)) / 2), col(C_ACCENT), "\xE2\x96\xB6", NULL, 0, NULL);
		if (strcmp(shown_playing, place)) {
			snprintf(shown_playing, sizeof shown_playing, "%s", place);
			if (!igIsRectVisible_Vec2(p, b))
				igSetScrollHereY(0.4f);
		}
	}
	char num[16];
	snprintf(num, sizeof num, "%d", index);
	float nsize = fs(12.5f);
	ImDrawList_AddText_FontPtr(dl, F_MONO, nsize, v2(p.x + S(30) - text_w(F_MONO, nsize, num), p.y + (h - nsize) / 2),
	                           col(C_FAINT), num, NULL, 0, NULL);
	float x = p.x + S(42) + (float)depth * S(22);
	const char *kind = step_kind(st);
	const struct kind_info *k = kind ? kind_find(kind) : NULL;
	tag_pill(dl, v2(x, p.y + S(11)), k ? k->tag : "?", k ? k->group : TAG_BAD, h - S(22));
	x += S(80);
	float size = igGetFontSize(), right = b.x - S(10);
	float ty = p.y + (h - size) / 2;
	const cJSON *v = kind ? cJSON_GetObjectItemCaseSensitive(st, kind) : NULL;
	char buf[1200];
	if (is_kind(kind, "say") || is_kind(kind, "think") || is_kind(kind, "narrate")) {
		bool narr = is_kind(kind, "narrate");
		if (!narr) {
			/* The face, then the name plate. */
			const cJSON *face = line_face(st, cast);
			V2 fa = v2(x, p.y + S(5)), fb = v2(x + S(27), p.y + h - S(5));
			if (face)
				draw_picture_small(dl, face, fa, fb, true);
			else
				ImDrawList_AddRect(dl, fa, fb, col(C_LINE), S(3), 0, S(1));
			x += S(36);
			bool game = false;
			const char *who = line_plate(st, cast, &game);
			if (!who && cJSON_IsString(v))
				who = v->valuestring;
			char tmp[64];
			if (!who && cJSON_IsNumber(v)) {
				snprintf(tmp, sizeof tmp, "face %d", (int)v->valuedouble);
				who = tmp;
			}
			draw_text(dl, F_UI, size, v2(x, ty), col(game ? C_FAINT : C_DIM), who ? who : "", x + S(90));
			x += S(98);
		}
		const cJSON *text = line_text(st);
		int pages = line_pages(st);
		const char *first = line_page(st, 0);
		if (first) {
			char pill[32] = "";
			float pw = 0;
			if (pages > 1) {
				snprintf(pill, sizeof pill, "+%d page%s", pages - 1, pages == 2 ? "" : "s");
				pw = text_w(F_UI, fs(12.5f), pill) + S(24);
			}
			float gsize = size * 1.06f;
			float end = draw_page_line(dl, v2(x, p.y + (h - gsize) / 2), gsize, col(C_TEXT), first, right - pw);
			if (pages > 1)
				small_pill(dl, v2(end + S(10), p.y + S(11)), pill, h - S(22), col(C_DIM));
		} else {
			char *j = text ? json_inline(text) : NULL;
			draw_text(dl, F_UI, size, v2(x, ty), col(text ? C_BAD : C_FAINT), j ? j : "no text", right);
			free(j);
		}
	} else if (picture_kind(kind) || is_kind(kind, "set")) {
		/* A small copy of the picture, then the sentence. */
		const cJSON *pic = v;
		if (is_kind(kind, "set")) {
			static const char *const order[] = { "background", "cg", "wallpaper", "portrait", "mini" };
			pic = NULL;
			for (int i = 0; i < 5 && !pic; i++) {
				const cJSON *c = cJSON_GetObjectItemCaseSensitive(v, order[i]);
				if ((cJSON_IsNumber(c) && c->valuedouble > 0) || cJSON_IsString(c))
					pic = c;
			}
		}
		bool shown = (cJSON_IsNumber(pic) && pic->valuedouble > 0) || cJSON_IsString(pic);
		if (shown) {
			bool face = is_kind(kind, "portrait") || is_kind(kind, "mini");
			draw_picture_small(dl, pic, v2(x, p.y + S(6)), v2(x + S(50), p.y + h - S(6)), face);
			x += S(60);
		}
		char *sum = step_summary(st, cast);
		draw_text(dl, F_UI, size, v2(x, ty), col(C_TEXT), sum, right);
		free(sum);
	} else if (is_kind(kind, "music") || is_kind(kind, "sound") || is_kind(kind, "jingle")) {
		int what = is_kind(kind, "music") ? SOUND_MUSIC : SOUND_EFFECT;
		int no = cJSON_IsNumber(v) ? (int)v->valuedouble : -1;
		if (no >= 0 && media_can_play(what, no)) {
			if (row_play(dl, v2(x + S(11), p.y + h / 2), S(11), media_playing(what, no), "##play"))
				media_play(what, no);
			x += S(30);
		}
		const char *name = what == SOUND_MUSIC && no >= 0 ? media_music_name(no) : NULL;
		if (name)
			snprintf(buf, sizeof buf, "%d \xC2\xB7 %s", no, name);
		else {
			char *sum = step_summary(st, cast);
			snprintf(buf, sizeof buf, "%s", sum);
			free(sum);
		}
		draw_text(dl, F_UI, size, v2(x, ty), col(C_TEXT), buf, right);
	} else {
		char *sum = step_summary(st, cast);
		ImU32 tc = col(C_TEXT);
		ImFont *f = F_UI;
		if (!k)
			tc = col(C_BAD);
		else if (k->group == TAG_NOTE) {
			tc = col(C_T_NOTE);
			one_line(sum, buf, sizeof buf);
			free(sum);
			sum = xstrdup(buf);
		} else if (!strcmp(k->key, "label") || !strcmp(k->key, "goto"))
			tc = col(C_T_FLOW);
		draw_text(dl, f, size, v2(x, ty), tc, sum, right);
		free(sum);
	}
	/* "+" at the row's foot while it is hovered: a new step after it. */
	if (doc_editable() && !igIsDragDropActive() && igIsWindowHovered(0) && igIsMouseHoveringRect(p, b, false)) {
		float r = S(8);
		V2 c = v2(p.x + S(4) + r, b.y - r - S(1));
		igSameLine(0, 0);
		igSetCursorScreenPos(v2(c.x - r, c.y - r));
		if (igInvisibleButton("##plus", v2(r * 2, r * 2), 0)) {
			select_place(place, false);
			want_add_menu = true;
		}
		bool hot = igIsItemHovered(0);
		ImDrawList_AddCircleFilled(dl, c, r, hot ? col(C_ACCENT_HI) : col_a(C_ACCENT, 0.75f), 16);
		ImU32 ink = col(C_ACCENT_INK);
		float s = r * 0.5f;
		ImDrawList_AddLine(dl, v2(c.x - s, c.y), v2(c.x + s, c.y), ink, S(1.6f));
		ImDrawList_AddLine(dl, v2(c.x, c.y - s), v2(c.x, c.y + s), ink, S(1.6f));
		if (hot)
			igSetTooltip("A new step after this one");
	}
	igPopID();
}

/* A branch's heading: then, else, or a choice's row. Returns whether its steps show. A step
 * dropped on it goes first in its list (list: the list's place, or NULL when it has none). */
static bool branch_row(const char *id, int depth, const char *label, const char *hint, ImU32 label_col, bool has_steps,
                       const char *list)
{
	igPushID_Str(id);
	ImGuiStorage *store = igGetStateStorage();
	ImGuiID key = igGetID_Str("##open");
	bool open = ImGuiStorage_GetBool(store, key, true);
	float h = S(26);
	V2 p = igGetCursorScreenPos();
	float w = igGetContentRegionAvail().x;
	if (igSelectable_Bool("##branch", false, 0, v2(w, h)) && has_steps) {
		open = !open;
		ImGuiStorage_SetBool(store, key, open);
	}
	if (list && doc_editable() && igBeginDragDropTarget()) {
		const ImGuiPayload *pl = igAcceptDragDropPayload(DRAG_STEP, ImGuiDragDropFlags_AcceptBeforeDelivery | ImGuiDragDropFlags_AcceptNoDrawDefaultRect);
		if (pl) {
			drop_line(p.x + S(42) + (float)(depth + 1) * S(22), p.x + w, p.y + h + S(1));
			if (pl->Delivery) {
				move_req.on = true;
				snprintf(move_req.from, sizeof move_req.from, "%s", (const char *)pl->Data);
				snprintf(move_req.list, sizeof move_req.list, "%s", list);
				move_req.index = 0;
			}
		}
		igEndDragDropTarget();
	}
	ImDrawList *dl = igGetWindowDrawList();
	float size = igGetFontSize(), y = p.y + (h - size) / 2;
	float x = p.x + S(42) + (float)depth * S(22);
	if (has_steps)
		ImDrawList_AddText_FontPtr(dl, F_UI, size, v2(x, y), col(C_T_FLOW), open ? "\xE2\x96\xBE" : "\xE2\x96\xB8", NULL, 0, NULL);
	x += S(16);
	draw_text(dl, F_UI, size, v2(x, y), label_col, label, p.x + w - S(10));
	if (hint && hint[0]) {
		float lw = text_w(F_UI, size, label);
		draw_text(dl, F_UI, fs(13), v2(x + lw + S(10), p.y + (h - fs(13)) / 2), col(C_FAINT), hint, p.x + w - S(10));
	}
	igPopID();
	return open && has_steps;
}

static void draw_steps(cJSON *list, const char *lp, int depth, const cJSON *cast)
{
	int k = 0;
	for (cJSON *st = list->child; st; st = st->next, k++) {
		char place[256];
		place_join(place, sizeof place, lp, k);
		step_row(st, place, k, depth, cast);
		const char *kind = step_kind(st);
		if (!kind)
			continue;
		if (!strcmp(kind, "if")) {
			static const char *const br[] = { "then", "else" };
			for (int i = 0; i < 2; i++) {
				cJSON *l = cJSON_GetObjectItemCaseSensitive(st, br[i]);
				if (!cJSON_IsArray(l))
					continue;
				char sub[256], hint[64];
				snprintf(sub, sizeof sub, "%s.%s", place, br[i]);
				int n = cJSON_GetArraySize(l);
				snprintf(hint, sizeof hint, "%d step%s", n, n == 1 ? "" : "s");
				if (branch_row(sub, depth + 1, br[i], hint, col(C_TEXT), n > 0, sub))
					draw_steps(l, sub, depth + 2, cast);
			}
		} else if (!strcmp(kind, "choice")) {
			cJSON *rows = cJSON_GetObjectItemCaseSensitive(st, "choice");
			if (!cJSON_IsArray(rows))
				continue;
			int r = 0;
			for (cJSON *row = rows->child; row; row = row->next, r++) {
				char rp[256], label[600], hint[300] = "";
				snprintf(rp, sizeof rp, "%s.choice[%d]", place, r);
				cJSON *steps = NULL;
				if (cJSON_IsString(row) && !strcmp(row->valuestring, "-")) {
					snprintf(label, sizeof label, "\xE2\x80\x94 a separator \xE2\x80\x94");
				} else if (cJSON_IsString(row)) {
					snprintf(label, sizeof label, "Row %d  \xE2\x80\x9C%s\xE2\x80\x9D", r + 1, row->valuestring);
				} else if (cJSON_IsObject(row)) {
					const cJSON *text = cJSON_GetObjectItemCaseSensitive(row, "text");
					snprintf(label, sizeof label, "Row %d  \xE2\x80\x9C%s\xE2\x80\x9D", r + 1,
					         cJSON_IsString(text) ? text->valuestring : "?");
					const cJSON *cond = cJSON_GetObjectItemCaseSensitive(row, "if");
					const cJSON *go = cJSON_GetObjectItemCaseSensitive(row, "goto");
					const cJSON *end = cJSON_GetObjectItemCaseSensitive(row, "end");
					size_t o = 0;
					if (cond) {
						char *j = json_inline(cond);
						o += (size_t)snprintf(hint + o, sizeof hint - o, "if %s   ", j);
						free(j);
					}
					if (o < sizeof hint && cJSON_IsString(go))
						o += (size_t)snprintf(hint + o, sizeof hint - o, "\xE2\x86\xAA %s", go->valuestring);
					if (o < sizeof hint && end) {
						char *j = cJSON_IsString(end) ? NULL : json_inline(end);
						snprintf(hint + o, sizeof hint - o, "end: %s", j ? j : end->valuestring);
						free(j);
					}
					steps = cJSON_GetObjectItemCaseSensitive(row, "steps");
					if (!cJSON_IsArray(steps))
						steps = NULL;
				} else {
					char *j = json_inline(row);
					snprintf(label, sizeof label, "Row %d  %s", r + 1, j);
					free(j);
				}
				char sp[256];
				snprintf(sp, sizeof sp, "%s.steps", rp);
				if (branch_row(rp, depth + 1, label, hint, col(C_TEXT), steps && steps->child, steps ? sp : NULL)) {
					draw_steps(steps, sp, depth + 2, cast);
				}
			}
		}
	}
}

static void add_menu(void)
{
	static const char *const groups[] = { "Lines", "Pictures and the screen", "Music and sounds", "Flow",
	                                      "Flags and the mod's code", "Notes" };
	for (int g = TAG_LINE; g <= TAG_NOTE; g++) {
		igTextColored(colv(tag_colour(g)), "%s", groups[g]);
		for (int i = 0; i < nkinds; i++) {
			if (kinds[i].group != g)
				continue;
			char label[128];
			snprintf(label, sizeof label, "   %s##%s", kinds[i].title, kinds[i].key);
			if (igMenuItem_Bool(label, kinds[i].key, false, true))
				op_add(&kinds[i]);
		}
	}
}

static void steps_toolbar(void)
{
	bool can = doc_editable();
	igBeginDisabled(!can);
	if (igButton("+ Add step  \xE2\x96\xBE", v2(0, 0)) || want_add_menu)
		igOpenPopup_Str("##add", 0);
	want_add_menu = false;
	igSetItemTooltip("A new step after the selected one");
	if (igBeginPopup("##add", 0)) {
		add_menu();
		igEndPopup();
	}
	bool has = sel_step(NULL, NULL) != NULL;
	igSameLine(0, S(6));
	igBeginDisabled(!has);
	if (igButton("Copy##dup", v2(0, 0)))
		op_duplicate();
	igSetItemTooltip("A copy of the step under it (Ctrl+D)");
	igSameLine(0, S(6));
	if (igButton("\xE2\x96\xB2##up", v2(S(34), 0)))
		op_move(-1);
	igSetItemTooltip("Up (Alt+Up)");
	igSameLine(0, S(6));
	if (igButton("\xE2\x96\xBC##down", v2(S(34), 0)))
		op_move(1);
	igSetItemTooltip("Down (Alt+Down)");
	igSameLine(0, S(6));
	if (igButton("Delete", v2(0, 0)))
		op_delete();
	igSetItemTooltip("Remove the step (Delete)");
	igEndDisabled();
	igEndDisabled();
}

static void cant_read_box(void)
{
	igSpacing();
	igPushStyleColor_Vec4(ImGuiCol_Text, colv(C_BAD));
	igPushTextWrapPos(0);
	igTextUnformatted(D.why, NULL);
	igPopTextWrapPos();
	igPopStyleColor(1);
	igSpacing();
	igPushTextWrapPos(0);
	igTextColored(colv(C_DIM), "The game refuses this file the same way. Fix it in a text editor: the editor reads it "
	              "again as soon as it changes on disk.");
	igPopTextWrapPos();
}

static char cast_sel[128];   /* the cast entry shown on the right */

static void cast_add(void)
{
	if (!doc_editable())
		return;
	cJSON *cast = cJSON_GetObjectItemCaseSensitive(D.root, "cast");
	if (!cJSON_IsObject(cast)) {
		cast = cJSON_CreateObject();
		/* After format and $schema, before the steps: as the editor writes a file. */
		cJSON_AddItemToObject(D.root, "cast", cast);
	}
	char key[64] = "someone";
	for (int i = 2; cJSON_GetObjectItemCaseSensitive(cast, key); i++)
		snprintf(key, sizeof key, "someone%d", i);
	cJSON *e = cJSON_CreateObject();
	cJSON_AddNumberToObject(e, "face", 2005);
	cJSON_AddItemToObject(cast, key, e);
	doc_changed("");
	snprintf(cast_sel, sizeof cast_sel, "%s", key);
}

static void cast_remove(const char *key)
{
	cJSON *cast = cJSON_GetObjectItemCaseSensitive(D.root, "cast");
	if (!doc_editable() || !cJSON_GetObjectItemCaseSensitive(cast, key))
		return;
	cJSON_DeleteItemFromObjectCaseSensitive(cast, key);
	if (!cast->child)
		cJSON_DeleteItemFromObjectCaseSensitive(D.root, "cast");
	doc_changed("");
	cast_sel[0] = 0;
}

static void cast_tab(void)
{
	const cJSON *cast = cJSON_GetObjectItemCaseSensitive(D.root, "cast");
	bool can = doc_editable();
	section("Cast", "names the lines use for a face");
	if (cJSON_IsObject(cast) && cast->child && !cJSON_GetObjectItemCaseSensitive(cast, cast_sel))
		snprintf(cast_sel, sizeof cast_sel, "%s", cast->child->string);
	if (!cJSON_IsObject(cast) || !cast->child) {
		igTextColored(colv(C_FAINT), "No cast: the lines name their faces by number.");
	} else if (igBeginTable("##cast", 4, ImGuiTableFlags_BordersInnerH, v2(0, 0), 0)) {
		igTableSetupColumn("Name in the file", ImGuiTableColumnFlags_WidthStretch, 1.1f, 0);
		igTableSetupColumn("Face", ImGuiTableColumnFlags_WidthStretch, 1, 0);
		igTableSetupColumn("Name plate", ImGuiTableColumnFlags_WidthStretch, 1.4f, 0);
		igTableSetupColumn("Used by", ImGuiTableColumnFlags_WidthStretch, 0.7f, 0);
		igPushStyleColor_Vec4(ImGuiCol_Text, colv(C_DIM));
		igTableHeadersRow();
		igPopStyleColor(1);
		int i = 0;
		for (const cJSON *e = cast->child; e; e = e->next, i++) {
			igPushID_Int(i);
			float h = S(44);
			igTableNextRow(0, h);
			igTableNextColumn();
			V2 p = igGetCursorScreenPos();
			bool sel = !strcmp(cast_sel, e->string);
			char first[256];
			int uses = cast_uses(e->string, first, sizeof first);
			if (igSelectable_Bool("##row", sel, ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_AllowDoubleClick, v2(0, h))) {
				snprintf(cast_sel, sizeof cast_sel, "%s", e->string);
				if (igIsMouseDoubleClicked_Nil(ImGuiMouseButton_Left) && first[0]) {
					select_place(first, true);
					want_mid_tab = 0;
				}
			}
			ImDrawList *dl = igGetWindowDrawList();
			float size = igGetFontSize();
			ImDrawList_AddText_FontPtr(dl, F_MONO, fs(MONO_SIZE), v2(p.x + S(6), p.y + (h - fs(MONO_SIZE)) / 2), col(C_TEXT),
			                           e->string, NULL, 0, NULL);
			igTableNextColumn();
			p = igGetCursorScreenPos();
			const cJSON *face = cJSON_GetObjectItemCaseSensitive(e, "face");
			if (face)
				draw_picture_small(dl, face, v2(p.x, p.y + S(5)), v2(p.x + S(30), p.y + h - S(5)), true);
			char buf[300];
			if (cJSON_IsNumber(face))
				snprintf(buf, sizeof buf, "%d", (int)face->valuedouble);
			else if (cJSON_IsString(face))
				snprintf(buf, sizeof buf, "%s", base_name(face->valuestring));
			else
				snprintf(buf, sizeof buf, "no face");
			draw_text(dl, F_UI, size, v2(p.x + S(38), p.y + (h - size) / 2), col(face ? C_TEXT : C_BAD), buf,
			          p.x + igGetContentRegionAvail().x);
			igTableNextColumn();
			p = igGetCursorScreenPos();
			const cJSON *name = cJSON_GetObjectItemCaseSensitive(e, "name");
			const char *who = cJSON_IsNumber(face) ? media_face_name((int)face->valuedouble) : NULL;
			if (cJSON_IsString(name))
				snprintf(buf, sizeof buf, "%s", name->valuestring);
			else
				snprintf(buf, sizeof buf, "%s%s", who ? who : "the game's", who ? " (the game's)" : "");
			draw_text(dl, F_UI, size, v2(p.x, p.y + (h - size) / 2), col(cJSON_IsString(name) ? C_TEXT : C_FAINT), buf,
			          p.x + igGetContentRegionAvail().x);
			igTableNextColumn();
			p = igGetCursorScreenPos();
			snprintf(buf, sizeof buf, uses ? "%d line%s" : "no line", uses, uses == 1 ? "" : "s");
			draw_text(dl, F_UI, size, v2(p.x, p.y + (h - size) / 2), col(uses ? C_TEXT : C_FAINT), buf, p.x + igGetContentRegionAvail().x);
			igPopID();
		}
		igEndTable();
	}
	igSpacing();
	igBeginDisabled(!can);
	if (igButton("+ Add to the cast", v2(0, 0)))
		cast_add();
	igEndDisabled();
	igSetItemTooltip("A name for a face, for the lines to use");
	igSpacing();
	section("The file", NULL);
	cJSON *once = cJSON_GetObjectItemCaseSensitive(D.root, "once");
	const cJSON *fmt = cJSON_GetObjectItemCaseSensitive(D.root, "format");
	const cJSON *schema = cJSON_GetObjectItemCaseSensitive(D.root, "$schema");
	igAlignTextToFramePadding();
	igTextColored(colv(C_DIM), "Plays once");
	igSameLine(S(130), 0);
	bool on = once != NULL;
	igBeginDisabled(!can);
	if (igCheckbox("Once per save, with the flag", &on)) {
		if (on) {
			char flag[300], base[200];
			snprintf(base, sizeof base, "%s", base_name(D.rel));
			char *dot = strrchr(base, '.');
			if (dot)
				*dot = 0;
			snprintf(flag, sizeof flag, "%s:%s-seen", ui_mod_id(), base);
			cJSON_AddItemToObject(D.root, "once", cJSON_CreateString(flag));
		} else {
			cJSON_DeleteItemFromObjectCaseSensitive(D.root, "once");
		}
		doc_changed("");
		once = cJSON_GetObjectItemCaseSensitive(D.root, "once");
	}
	igEndDisabled();
	igSetItemTooltip("The game plays it once in a save: a flag of this name is set at its start, and a save that has it skips it");
	if (cJSON_IsString(once)) {
		igSameLine(0, S(8));
		/* Written when the field is left; filled again when the file's flag changes (an undo). */
		static char flag_buf[256], flag_was[256];
		if (strcmp(flag_was, once->valuestring)) {
			snprintf(flag_buf, sizeof flag_buf, "%s", once->valuestring);
			snprintf(flag_was, sizeof flag_was, "%s", once->valuestring);
		}
		igSetNextItemWidth(S(260));
		igInputText("##once", flag_buf, sizeof flag_buf, ImGuiInputTextFlags_CharsNoBlank | (can ? 0 : ImGuiInputTextFlags_ReadOnly), NULL, NULL);
		if (igIsItemDeactivatedAfterEdit() && flag_buf[0] && strcmp(flag_buf, once->valuestring)) {
			cJSON_ReplaceItemInObjectCaseSensitive(D.root, "once", cJSON_CreateString(flag_buf));
			doc_changed("");
		}
	} else if (once) {
		igSameLine(0, S(8));
		igTextColored(colv(C_BAD), "not a flag's name: its JSON is kept");
	}
	igTextColored(colv(C_DIM), "Format");
	igSameLine(S(130), 0);
	if (cJSON_IsNumber(fmt))
		igText("%g", fmt->valuedouble);
	else
		igTextColored(colv(C_BAD), "missing: the game needs \"format\": 1");
	igTextColored(colv(C_DIM), "Schema");
	igSameLine(S(130), 0);
	if (cJSON_IsString(schema) && !strcmp(schema->valuestring, SCENE_SCHEMA))
		igText("\xE2\x9C\x93 the docs' address");
	else if (cJSON_IsString(schema))
		igText("%s", schema->valuestring);
	else
		igTextColored(colv(C_FAINT), "none (text editors check the file as it is typed when it has one)");
}


static void middle(void)
{
	if (!igBeginTabBar("##mid", 0))
		return;
	int want = want_mid_tab;
	want_mid_tab = -1;
	cast_tab_shown = false;
	if (igBeginTabItem("Steps", NULL, want == 0 ? ImGuiTabItemFlags_SetSelected : 0)) {
		if (!D.open) {
			igSpacing();
			igTextColored(colv(C_FAINT), mod_now() ? "Pick a scene on the left, or make a new one." : "No mod in this game folder.");
		} else if (!D.root || !doc_steps()) {
			cant_read_box();
		} else {
			if (D.readonly) {
				igPushTextWrapPos(0);
				igTextColored(colv(C_WARN), "%s", D.why);
				igPopTextWrapPos();
			}
			steps_toolbar();
			igSpacing();
			igPushStyleVar_Vec2(ImGuiStyleVar_ItemSpacing, v2(S(8), S(3)));
			igBeginChild_Str("##steps", v2(0, 0), 0, 0);
			keys();
			nvis = 0;
			cJSON *steps = doc_steps();
			if (!steps->child)
				igTextColored(colv(C_FAINT), "No steps yet: + Add step writes the first.");
			draw_steps(steps, "steps", 0, cJSON_GetObjectItemCaseSensitive(D.root, "cast"));
			if (move_req.on) {
				move_req.on = false;
				op_move_to(move_req.from, move_req.list, move_req.index);
			}
			igEndChild();
			igPopStyleVar(1);
		}
		igEndTabItem();
	} else {
		keys();
	}
	if (igBeginTabItem("Cast and file", NULL, want == 1 ? ImGuiTabItemFlags_SetSelected : 0)) {
		cast_tab_shown = true;
		if (D.root && cJSON_IsObject(D.root))
			cast_tab();
		else if (D.open)
			cant_read_box();
		igEndTabItem();
	}
	igEndTabBar();
}

/* ---- the right side: the selected step ---- */

static void fill_edit(void)
{
	cJSON *st = sel_step(NULL, NULL);
	edit_why[0] = 0;
	if (!st) {
		edit_buf[0] = edit_src[0] = 0;
	} else {
		char *t = step_write(st);
		snprintf(edit_buf, sizeof edit_buf, "%s", t);
		snprintf(edit_src, sizeof edit_src, "%s", t);
		free(t);
	}
	snprintf(edit_for, sizeof edit_for, "%s", UI.sel);
	edit_at = D.at;
	edit_gen = doc_gen;
}

/* The stage's foot: what is on each layer, and the page shown with its arrows. */
static void stage_foot(const struct stage *sg, bool own_line, int *page)
{
	static const char *const names[L_COUNT] = { "Wallpaper", "Mask", "Background", "Standing", "Small", "CG" };
	char info[600] = "";
	size_t o = 0;
	for (int i = 0; i < L_COUNT && o < sizeof info; i++) {
		const cJSON *v = sg->layer[i];
		if (!v || (cJSON_IsNumber(v) && v->valuedouble <= 0))
			continue;
		if (cJSON_IsNumber(v))
			o += (size_t)snprintf(info + o, sizeof info - o, "%s%s %d", o ? " \xC2\xB7 " : "", names[i], (int)v->valuedouble);
		else if (cJSON_IsString(v))
			o += (size_t)snprintf(info + o, sizeof info - o, "%s%s %s", o ? " \xC2\xB7 " : "", names[i], base_name(v->valuestring));
	}
	const cJSON *face = sg->line ? line_face(sg->line, sg->cast) : NULL;
	if (o < sizeof info && cJSON_IsNumber(face))
		o += (size_t)snprintf(info + o, sizeof info - o, "%sface %d", o ? " \xC2\xB7 " : "", (int)face->valuedouble);
	else if (o < sizeof info && cJSON_IsString(face))
		o += (size_t)snprintf(info + o, sizeof info - o, "%sface %s", o ? " \xC2\xB7 " : "", base_name(face->valuestring));
	if (sg->screen && o < sizeof info)
		snprintf(info + o, sizeof info - o, "%sthe screen %s", o ? " \xC2\xB7 " : "", sg->screen == 'w' ? "white" : "black");
	int pages = sg->line ? line_pages(sg->line) : 0;
	float arrows = pages > 1 ? S(150) : 0;
	igAlignTextToFramePadding();
	igPushFont(NULL, 14);
	char cut[700];
	igTextColored(colv(C_FAINT), "%s", fit(F_UI, fs(14), info[0] ? info : "Nothing on the stage yet", igGetContentRegionAvail().x - arrows - S(8),
	                                    cut, sizeof cut));
	igPopFont();
	if (pages > 1) {
		igSameLine(content_right() - arrows, 0);
		igBeginDisabled(*page <= 0);
		if (igArrowButton("##prev", ImGuiDir_Left))
			(*page)--;
		igEndDisabled();
		igSameLine(0, S(6));
		igAlignTextToFramePadding();
		igPushFont(NULL, 14);
		igTextColored(colv(C_DIM), "page %d of %d", *page + 1, pages);
		igPopFont();
		igSameLine(0, S(6));
		igBeginDisabled(*page >= pages - 1);
		if (igArrowButton("##next", ImGuiDir_Right))
			(*page)++;
		igEndDisabled();
		if (!own_line && igIsItemHovered(0))
			igSetTooltip("The line's pages: the box shows its last page until the next line");
	}
}

/* The stage's heading: the game's screen at the step, and its buttons to play it. */
static void stage_label(bool steps)
{
	igAlignTextToFramePadding();
	igTextUnformatted("Stage", NULL);
	igSameLine(0, S(8));
	igTextColored(colv(C_FAINT), "\xC2\xB7 the game's screen");
	igSetItemTooltip("Drawn from the game's own pictures and font at the selected step, where the step ends.\n"
	                 "\xE2\x96\xB6 plays the step's motion; Play from here goes on through the steps after it.");
	if (!steps)
		return;
	const char *a = "\xE2\x96\xB6", *b = "Play from here", *stop = "\xE2\x96\xA0  Stop";
	float gap = S(6), pad = igGetStyle()->FramePadding.x * 2;
	float wa = igCalcTextSize(a, NULL, false, 0).x + pad, wb = igCalcTextSize(b, NULL, false, 0).x + pad;
	float ws = igCalcTextSize(stop, NULL, false, 0).x + pad;
	bool can = D.root && UI.sel[0] && sel_step(NULL, NULL);
	if (play_active() && !play_preview()) {
		igSameLine(content_right() - ws, 0);
		if (igButton(stop, v2(0, 0)))
			play_stop();
		igSetItemTooltip("Stops the play (Escape)");
		return;
	}
	igSameLine(content_right() - wa - gap - wb, 0);
	igBeginDisabled(!can);
	if (igButton(a, v2(0, 0)))
		play_start(UI.sel, false);
	igSetItemTooltip("Plays the selected step from the stage before it: its transition, slide, shake or pages");
	igSameLine(0, gap);
	if (igButton(b, v2(0, 0)))
		play_start(UI.sel, true);
	igSetItemTooltip("Plays on from the selected step as the game would: a click or Space for each page,\n"
	                 "the menu's rows clicked, an if asked. Without the game: mods' functions and the game's\n"
	                 "commands are passed by.");
	igEndDisabled();
}

/* The stage's foot while it plays: what the player waits for. */
static void play_foot(void)
{
	igAlignTextToFramePadding();
	igPushFont(NULL, 14);
	char cut[700];
	igTextColored(colv(C_DIM), "%s", fit(F_UI, fs(14), play_status(), igGetContentRegionAvail().x - S(8), cut, sizeof cut));
	igPopFont();
}

/* The stage at the top of the right side, for the selected step. */
static void stage_for_step(const cJSON *st)
{
	struct stage sg;
	memset(&sg, 0, sizeof sg);
	if (D.root && st)
		stage_walk(D.root, UI.sel, &sg);
	else
		sg.cast = D.root ? cJSON_GetObjectItemCaseSensitive(D.root, "cast") : NULL;
	bool own = sg.line && sg.line == st;
	int pages = sg.line ? line_pages(sg.line) : 0;
	static const cJSON *last_line;
	static int other_page;
	/* The selected line shows the page being typed; a line before it shows its last page. */
	if (sg.line != last_line) {
		last_line = sg.line;
		other_page = pages > 0 ? pages - 1 : 0;
	}
	int *page = own ? &fields_page : &other_page;
	if (*page >= pages)
		*page = pages > 0 ? pages - 1 : 0;
	if (*page < 0)
		*page = 0;
	/* ▶ stops when another step is selected; Play from here goes on. */
	if (play_active() && !play_on() && strcmp(play_place(), UI.sel))
		play_stop();
	if (play_draw(igGetContentRegionAvail().x)) {
		play_foot();
		return;
	}
	stage_draw(&sg, *page, igGetContentRegionAvail().x);
	stage_foot(&sg, own, page);
}

static bool stage_only;  /* --do stage */

static void stage_alone(float w)
{
	const cJSON *st = sel_step(NULL, NULL);
	struct stage sg;
	memset(&sg, 0, sizeof sg);
	if (st)
		stage_walk(D.root, UI.sel, &sg);
	int pages = sg.line ? line_pages(sg.line) : 0;
	int page = sg.line == st ? fields_page : pages - 1;
	if (page >= pages)
		page = pages - 1;
	if (!play_draw(w))
		stage_draw(&sg, page < 0 ? 0 : page, w);
}

static bool json_open;   /* the step's JSON shown under its fields */

/* The step's JSON as the file holds it, editable, under the fields. */
static void step_json(bool ro)
{
	bool typing = strcmp(edit_buf, edit_src) != 0;
	igSetNextItemOpen(json_open, ImGuiCond_Always);
	json_open = igCollapsingHeader_TreeNodeFlags("JSON##stepjson", 0);
	if (!json_open)
		return;
	igPushFont(F_MONO, MONO_SIZE);
	igPushStyleColor_Vec4(ImGuiCol_FrameBg, colv(C_FIELD));
	float lines = 3;
	for (const char *c = edit_buf; *c; c++)
		lines += *c == '\n';
	if (lines > 18)
		lines = 18;
	igInputTextMultiline("##json", edit_buf, sizeof edit_buf, v2(-1, igGetTextLineHeight() * lines),
	                     ImGuiInputTextFlags_AllowTabInput | ImGuiInputTextFlags_WordWrap | (ro ? ImGuiInputTextFlags_ReadOnly : 0), NULL, NULL);
	bool left_after_edit = igIsItemDeactivatedAfterEdit();
	igPopStyleColor(1);
	igPopFont();
	typing = strcmp(edit_buf, edit_src) != 0;
	if (left_after_edit && typing)
		apply_edit();
	igBeginDisabled(ro || !typing);
	if (accent_button("Apply", v2(0, 0)))
		apply_edit();
	igSetItemTooltip("The text as the step (also when the field loses focus)");
	igSameLine(0, S(6));
	if (igButton("Revert", v2(0, 0))) {
		snprintf(edit_buf, sizeof edit_buf, "%s", edit_src);
		edit_why[0] = 0;
	}
	igEndDisabled();
	if (edit_why[0]) {
		igPushTextWrapPos(0);
		igTextColored(colv(C_BAD), "Not applied: %s", edit_why);
		igPopTextWrapPos();
	}
}

static void cast_side(void);

static void right_side(void)
{
	if (cast_tab_shown && D.root) {
		cast_side();
		return;
	}
	stage_label(true);
	cJSON *st = sel_step(NULL, NULL);
	bool typing = strcmp(edit_buf, edit_src) != 0;
	if (strcmp(edit_for, UI.sel) || edit_gen != doc_gen || (!typing && (edit_at != D.at || D.live)))
		fill_edit();
	stage_for_step(st);
	igSpacing();
	ImDrawList_AddLine(igGetWindowDrawList(), igGetCursorScreenPos(),
	                   v2(igGetCursorScreenPos().x + igGetContentRegionAvail().x, igGetCursorScreenPos().y), col(C_LINE), S(1));
	igSpacing();
	if (!st) {
		igPushTextWrapPos(0);
		igTextColored(colv(C_FAINT), D.open ? "Select a step: its fields come here." : "Open a scene to see its steps.");
		igPopTextWrapPos();
		return;
	}
	igBeginChild_Str("##fields", v2(0, 0), 0, 0);
	const char *kind = step_kind(st);
	const struct kind_info *k = kind ? kind_find(kind) : NULL;
	ImDrawList *dl = igGetWindowDrawList();
	V2 p = igGetCursorScreenPos();
	float h = S(24);
	float pw = tag_pill(dl, p, k ? k->tag : "?", k ? k->group : TAG_BAD, h);
	igDummy(v2(pw, h));
	igSameLine(0, S(10));
	igAlignTextToFramePadding();
	igTextUnformatted(k ? k->title : "Not a step", NULL);
	float pw2 = text_w(F_MONO, fs(13), UI.sel);
	igSameLine(content_right() - pw2, 0);
	igPushFont(F_MONO, 13);
	igTextColored(colv(C_FAINT), "%s", UI.sel);
	igPopFont();
	igSpacing();
	bool ro = !doc_editable();
	if (ro)
		igTextColored(colv(C_FAINT), "Read-only: %s", D.root ? "this editor does not write this file" : "the file cannot be read");
	fields_step(st, UI.sel, ro);
	igSpacing();
	igSpacing();
	step_json(ro);
	igEndChild();
}

/* The cast entry picked in Cast and file: its first line on the stage, then its fields. */
static void cast_side(void)
{
	cJSON *cast = cJSON_GetObjectItemCaseSensitive(D.root, "cast");
	cJSON *e = cJSON_GetObjectItemCaseSensitive(cast, cast_sel);
	stage_label(false);
	char first[256];
	int uses = e ? cast_uses(cast_sel, first, sizeof first) : 0;
	struct stage sg;
	memset(&sg, 0, sizeof sg);
	sg.cast = cast;
	cJSON *made = NULL;
	if (uses) {
		stage_walk(D.root, first, &sg);
	} else if (e) {
		/* No line names it yet: an empty line of its own. */
		made = cJSON_CreateObject();
		cJSON_AddStringToObject(made, "say", cast_sel);
		cJSON_AddStringToObject(made, "text", "");
		sg.line = made;
	}
	static int page;
	if (sg.line && page >= line_pages(sg.line))
		page = 0;
	stage_draw(&sg, page, igGetContentRegionAvail().x);
	igAlignTextToFramePadding();
	igPushFont(NULL, 14);
	if (uses)
		igTextColored(colv(C_FAINT), "Its first line, %s", first);
	else
		igTextColored(colv(C_FAINT), e ? "No line names it yet" : "Pick a name of the cast");
	igPopFont();
	cJSON_Delete(made);
	igSpacing();
	ImDrawList_AddLine(igGetWindowDrawList(), igGetCursorScreenPos(),
	                   v2(igGetCursorScreenPos().x + igGetContentRegionAvail().x, igGetCursorScreenPos().y), col(C_LINE), S(1));
	igSpacing();
	if (!e) {
		igPushTextWrapPos(0);
		igTextColored(colv(C_FAINT), "The cast names a face for the lines: a line says \"say\": \"name\" instead of a face's "
		              "number, and the name plate comes with it.");
		igPopTextWrapPos();
		return;
	}
	igBeginChild_Str("##castfields", v2(0, 0), 0, 0);
	ImDrawList *dl = igGetWindowDrawList();
	V2 p = igGetCursorScreenPos();
	float pw = tag_pill(dl, p, "CAST", TAG_LINE, S(24));
	igDummy(v2(pw, S(24)));
	igSameLine(0, S(10));
	igAlignTextToFramePadding();
	igTextUnformatted(cast_sel, NULL);
	char where[160];
	snprintf(where, sizeof where, "cast.%s", cast_sel);
	igSameLine(content_right() - text_w(F_MONO, fs(13), where), 0);
	igPushFont(F_MONO, 13);
	igTextColored(colv(C_FAINT), "%s", where);
	igPopFont();
	igSpacing();
	bool ro = !doc_editable();
	const char *now = fields_cast(cast_sel, ro);
	if (strcmp(now, cast_sel))
		snprintf(cast_sel, sizeof cast_sel, "%s", now);
	igSetCursorPosX(S(108));
	igAlignTextToFramePadding();
	igTextColored(colv(C_DIM), uses ? "Used by %d line%s" : "No line uses it", uses, uses == 1 ? "" : "s");
	if (uses) {
		igSameLine(0, S(10));
		if (igSmallButton("Show the first"))
			select_place(first, true), want_mid_tab = 0;
	}
	igSpacing();
	igSetCursorPosX(S(108));
	igBeginDisabled(ro || uses > 0);
	if (igButton("Remove from the cast", v2(0, 0)))
		cast_remove(cast_sel);
	igEndDisabled();
	if (uses && igIsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
		igSetTooltip("Its lines name it: give them another speaker first");
	igSpacing();
	igSpacing();
	cJSON *entry = cJSON_GetObjectItemCaseSensitive(cast, cast_sel);
	if (entry && igCollapsingHeader_TreeNodeFlags("JSON##castjson", 0)) {
		char *j = json_inline(entry);
		igPushFont(F_MONO, MONO_SIZE);
		igPushTextWrapPos(0);
		igTextColored(colv(C_DIM), "\"%s\": %s", cast_sel, j);
		igPopTextWrapPos();
		igPopFont();
		free(j);
	}
	igEndChild();
}

/* ---- the bottom: problems and the status line ---- */

static int problem_count(void)
{
	return D.open && D.why[0] ? 1 : 0;
}

static void bottom_panel(float tabs_h, float panel_h)
{
	int n = problem_count();
	if (n > 0 && last_problem_count == 0)
		problems_folded = false;
	last_problem_count = n;
	bool open = n > 0 && !problems_folded;

	igPushStyleVar_Vec2(ImGuiStyleVar_WindowPadding, v2(S(12), (tabs_h - igGetFrameHeight()) / 2));
	igBeginChild_Str("##tabs", v2(0, tabs_h), ImGuiChildFlags_AlwaysUseWindowPadding, ImGuiWindowFlags_NoScrollbar);
	igPopStyleVar(1);
	char label[64];
	snprintf(label, sizeof label, "%s  Problems  %d###problems", open ? "\xE2\x96\xBE" : "\xE2\x96\xB4", n);
	igPushStyleColor_Vec4(ImGuiCol_Button, v4(0, 0, 0, 0));
	igPushStyleColor_Vec4(ImGuiCol_Border, v4(0, 0, 0, 0));
	if (n)
		igPushStyleColor_Vec4(ImGuiCol_Text, colv(C_BAD));
	if (igButton(label, v2(0, 0)) && n)
		problems_folded = !problems_folded;
	if (n)
		igPopStyleColor(1);
	igPopStyleColor(2);
	const char *msg;
	ImVec4_c mc;
	if (!D.open) {
		msg = "";
		mc = colv(C_FAINT);
	} else if (n) {
		msg = D.root ? "The file reads, but this editor does not write it" : "RQ MOD cannot read this file";
		mc = colv(D.root ? C_WARN : C_BAD);
	} else {
		msg = "\xE2\x9C\x93 The file reads as RQ MOD reads it";
		mc = colv(C_OK);
	}
	float mw = igCalcTextSize(msg, NULL, false, -1).x;
	igSameLine(igGetWindowWidth() - mw - S(14), 0);
	igAlignTextToFramePadding();
	igTextColored(mc, "%s", msg);
	igEndChild();
	V2 a = igGetItemRectMin(), b = igGetItemRectMax();
	ImDrawList_AddLine(igGetWindowDrawList(), v2(a.x, a.y), v2(b.x, a.y), col(C_LINE), S(1));

	if (open) {
		igPushStyleVar_Vec2(ImGuiStyleVar_WindowPadding, v2(S(12), S(6)));
		igBeginChild_Str("##problems", v2(0, panel_h), ImGuiChildFlags_AlwaysUseWindowPadding, 0);
		igPopStyleVar(1);
		igTextColored(colv(D.root ? C_WARN : C_BAD), "\xE2\x97\x8F");
		igSameLine(0, S(8));
		igPushTextWrapPos(0);
		igText("%s: %s", D.rel, D.why);
		igPopTextWrapPos();
		igEndChild();
	}
}

static void status_line(float h)
{
	ImDrawList *dl = igGetWindowDrawList();
	V2 p = igGetCursorScreenPos();
	float w = igGetContentRegionAvail().x;
	ImDrawList_AddRectFilled(dl, p, v2(p.x + w, p.y + h), col(C_PAGE), 0, 0);
	igPushStyleVar_Vec2(ImGuiStyleVar_WindowPadding, v2(S(12), (h - igGetTextLineHeight()) / 2));
	igBeginChild_Str("##status", v2(0, h), ImGuiChildFlags_AlwaysUseWindowPadding, ImGuiWindowFlags_NoScrollbar);
	igPopStyleVar(1);
	igPushFont(NULL, 14);
	igTextUnformatted(APP_NAME " " EDITOR_VERSION, NULL);
	if (EDITOR_BUILD_NOTE[0]) {
		igSameLine(0, S(6));
		igTextColored(colv(C_FAINT), "(%s)", EDITOR_BUILD_NOTE);
	}
	igSameLine(0, S(16));
	igTextColored(colv(C_FAINT), "%s \xC2\xB7 %s", game_title, game_dir);
	if (igIsItemHovered(ImGuiHoveredFlags_DelayShort))
		igSetTooltip("Click to open another game folder");
	if (igIsItemClicked(ImGuiMouseButton_Left) && !UI.shot) {
		char dir[1024];
		if (win_pick_folder("Pick Rance Quest Magnum's folder (the one with System40.ini)", dir, sizeof dir)) {
			if (game_dir_ok(dir))
				want(P_GAME, dir);
			else
				ui_error("Not a game's folder: %s", dir);
		}
	}
	igSameLine(0, S(16));
	igTextColored(colv(C_FAINT), "scene format %d", SCENE_FORMAT);
	char right[700] = "";
	ImVec4_c rc = colv(C_FAINT);
	if (UI.status[0] && igGetTime() < UI.status_until) {
		snprintf(right, sizeof right, "%s", UI.status);
		rc = colv(UI.status_bad ? C_BAD : C_OK);
	} else if (D.open && mod_now()) {
		snprintf(right, sizeof right, "%s \xC2\xB7 %s", mod_now()->id, D.rel);
	}
	if (right[0]) {
		float rw = igCalcTextSize(right, NULL, false, -1).x;
		igSameLine(igGetWindowWidth() - rw - S(12), 0);
		igTextColored(rc, "%s", right);
	}
	igPopFont();
	igEndChild();
}

/* ---- the modals ---- */

static bool modal_begin(int m)
{
	igSetNextWindowSize(v2(S(520), 0), ImGuiCond_Always);
	igPushStyleVar_Vec2(ImGuiStyleVar_WindowPadding, v2(S(20), S(16)));
	bool open = igBeginPopupModal(modal_names[m], NULL, ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoMove);
	igPopStyleVar(1);
	if (open)
		igPushTextWrapPos(0);
	return open;
}

static void modal_end(void)
{
	igPopTextWrapPos();
	igEndPopup();
}

static void modals(void)
{
	if (modal_open) {
		igOpenPopup_Str(modal_names[modal_open], 0);
		modal_open = M_NONE;
	}
	if (modal_begin(M_UNSAVED)) {
		igText("%s has changes that are not saved.", D.rel);
		igSpacing();
		if (accent_button("Save", v2(S(110), 0))) {
			igCloseCurrentPopup();
			if (save_now())
				after_save_continue();
			else if (!D.styled && doc_dirty())
				; /* the layout's notice asks next, then goes on */
			else
				pending = P_NONE;
		}
		igSameLine(0, S(8));
		if (igButton("Don't save", v2(S(110), 0))) {
			igCloseCurrentPopup();
			char path[1600], rel[512];
			snprintf(path, sizeof path, "%s", D.path);
			snprintf(rel, sizeof rel, "%s", D.rel);
			doc_open(path, rel);   /* the changes go */
			doc_gen++;
			after_save_continue();
		}
		igSameLine(0, S(8));
		if (igButton("Cancel", v2(S(110), 0)) || igIsKeyPressed_Bool(ImGuiKey_Escape, false)) {
			pending = P_NONE;
			igCloseCurrentPopup();
		}
		modal_end();
	}
	if (modal_begin(M_LAYOUT)) {
		igTextUnformatted("This file was laid out by hand or by another program.", NULL);
		igSpacing();
		igTextColored(colv(C_DIM), "The editor writes every scene file in one layout: the same steps and values, its own "
		              "spaces, line breaks and order of fields. Saving rewrites %s in that layout; after that, only "
		              "your changes move lines.", D.rel);
		igSpacing();
		if (accent_button("Save in the editor's layout", v2(0, 0))) {
			igCloseCurrentPopup();
			if (really_save())
				after_save_continue();
			else
				pending = P_NONE;
		}
		igSameLine(0, S(8));
		if (igButton("Cancel", v2(S(110), 0)) || igIsKeyPressed_Bool(ImGuiKey_Escape, false)) {
			pending = P_NONE;
			igCloseCurrentPopup();
		}
		modal_end();
	}
	if (modal_begin(M_CHANGED)) {
		igText("%s changed on disk, and the editor has changes not saved.", D.rel);
		igSpacing();
		if (igButton("Read it from disk (your changes go)", v2(0, 0))) {
			igCloseCurrentPopup();
			doc_reload();
			doc_gen++;
		}
		igSameLine(0, S(8));
		if (accent_button("Keep mine", v2(0, 0))) {
			igCloseCurrentPopup();
			D.changed_outside = false;
			ui_status("Kept your changes: Save writes over the file on disk");
		}
		modal_end();
	}
	if (modal_begin(M_NEW)) {
		struct mod *m = mod_now();
		igText("A new scene in %s's folder:", m ? m->name : "the mod");
		igSpacing();
		if (igIsWindowAppearing())
			igSetKeyboardFocusHere(0);
		igSetNextItemWidth(-1);
		bool enter = igInputTextWithHint("##name", "its name: letters, digits, - and _", new_name, sizeof new_name,
		                                 ImGuiInputTextFlags_EnterReturnsTrue, NULL, NULL);
		char rel[160];
		snprintf(rel, sizeof rel, "scenes/%s.json", new_name[0] ? new_name : "<name>");
		igTextColored(colv(C_FAINT), "writes %s", rel);
		if (new_why[0])
			igTextColored(colv(C_BAD), "%s", new_why);
		igSpacing();
		if (accent_button("Create", v2(S(110), 0)) || enter) {
			char path[1600];
			snprintf(path, sizeof path, "%s/%s", m ? m->dir : "", rel);
			if (!m) {
				snprintf(new_why, sizeof new_why, "No mod");
			} else if (!scene_name_ok(new_name)) {
				snprintf(new_why, sizeof new_why, "A name of letters, digits, - and _ (64 at most)");
			} else if (file_exists(path)) {
				snprintf(new_why, sizeof new_why, "%s is already there", rel);
			} else {
				cJSON *root = cJSON_CreateObject();
				cJSON_AddStringToObject(root, "$schema", SCENE_SCHEMA);
				cJSON_AddNumberToObject(root, "format", SCENE_FORMAT);
				cJSON_AddItemToObject(root, "steps", cJSON_CreateArray());
				char *text = scene_write(root);
				cJSON_Delete(root);
				char dir[1600];
				snprintf(dir, sizeof dir, "%s/scenes", m->dir);
				mkdir_p(dir);
				bool ok = file_write(path, (uint8_t *)text, strlen(text));
				free(text);
				if (!ok) {
					snprintf(new_why, sizeof new_why, "Could not write %s", path);
				} else {
					igCloseCurrentPopup();
					rescan();
					open_file(rel);
					ui_status("Made %s", rel);
				}
			}
		}
		igSameLine(0, S(8));
		if (igButton("Cancel", v2(S(110), 0)) || igIsKeyPressed_Bool(ImGuiKey_Escape, false))
			igCloseCurrentPopup();
		modal_end();
	}
	picker_frame();
}

/* ---- an action by name (--shot --do), as the window's own buttons do it ---- */

static int held_preview;   /* preview=N: a list's row held as hovered */

bool ui_do(const char *action)
{
	const char *eq = strchr(action, '=');
	char name[32];
	snprintf(name, sizeof name, "%.*s", eq ? (int)(eq - action) : (int)strlen(action), action);
	const char *arg = eq ? eq + 1 : "";
	if (!strcmp(name, "select")) {
		select_place(arg, true);
		return place_find(D.root, arg, NULL, NULL) != NULL;
	}
	if (!strcmp(name, "add")) {
		const struct kind_info *k = kind_find(arg);
		if (k)
			op_add(k);
		return k != NULL;
	}
	if (!strcmp(name, "apply")) {
		snprintf(edit_buf, sizeof edit_buf, "%s", arg);
		snprintf(edit_for, sizeof edit_for, "%s", UI.sel);
		edit_at = D.at;
		edit_gen = doc_gen;
		return apply_edit();
	}
	if (!strcmp(name, "type")) {   /* text in the JSON field, not applied */
		fill_edit();
		snprintf(edit_buf, sizeof edit_buf, "%s", arg);
		return true;
	}
	if (!strcmp(name, "copy"))
		op_duplicate();
	else if (!strcmp(name, "delete"))
		op_delete();
	else if (!strcmp(name, "up"))
		op_move(-1);
	else if (!strcmp(name, "down"))
		op_move(1);
	else if (!strcmp(name, "undo"))
		op_undo();
	else if (!strcmp(name, "redo"))
		op_redo();
	else if (!strcmp(name, "tab"))
		want_mid_tab = !strcmp(arg, "cast") ? 1 : 0;
	else if (!strcmp(name, "modal"))
		modal_open = !strcmp(arg, "unsaved") ? M_UNSAVED : !strcmp(arg, "layout") ? M_LAYOUT
		           : !strcmp(arg, "changed") ? M_CHANGED : !strcmp(arg, "new") ? M_NEW : M_NONE;
	else if (!strcmp(name, "fold"))
		problems_folded = !problems_folded;
	else if (!strcmp(name, "page"))
		fields_page = atoi(arg);
	else if (!strcmp(name, "json"))
		json_open = true;
	else if (!strcmp(name, "cast")) {
		snprintf(cast_sel, sizeof cast_sel, "%s", arg);
		want_mid_tab = 1;
		return cJSON_GetObjectItemCaseSensitive(cJSON_GetObjectItemCaseSensitive(D.root, "cast"), arg) != NULL;
	} else if (!strcmp(name, "castadd"))
		cast_add();
	else if (!strcmp(name, "stage"))
		stage_only = true;
	else if (!strcmp(name, "play") || !strcmp(name, "playon")) {   /* the stage playing from the selected step */
		play_start(UI.sel, name[4] == 'o');
		return play_active();
	} else if (!strcmp(name, "clock"))   /* clock=MS: the player's clock held there, for a shot */
		play_fixed_clock(atof(arg));
	else if (!strcmp(name, "click"))
		play_key();
	else if (!strcmp(name, "pick"))   /* pick=N: the menu's row N clicked */
		play_pick(atoi(arg));
	else if (!strcmp(name, "answer"))   /* answer=then|else: the if asked */
		play_answer(!strcmp(arg, "then"));
	else if (!strcmp(name, "print")) {   /* what plays, on stdout */
		printf("play %s %s | %s\n", play_active() ? "on" : "off", play_step_place(), play_status());
		fflush(stdout);
	} else if (!strcmp(name, "mouse")) {   /* mouse=X,Y: the mouse moved there, as Dear ImGui takes it */
		float x = 0, y = 0;
		if (sscanf(arg, "%f,%f", &x, &y) != 2)
			return false;
		ImGuiIO_AddMousePosEvent(igGetIO_Nil(), x, y);
	} else if (!strcmp(name, "mdown") || !strcmp(name, "mup")) {   /* the left button pressed, let go */
		ImGuiIO_AddMouseButtonEvent(igGetIO_Nil(), 0, name[1] == 'd');
	} else if (!strcmp(name, "printstep")) {   /* the selected step and the cast as JSON, on stdout */
		cJSON *st = sel_step(NULL, NULL);
		char *j = st ? json_inline(st) : NULL;
		const cJSON *cast = D.root ? cJSON_GetObjectItemCaseSensitive(D.root, "cast") : NULL;
		char *c = cast ? json_inline(cast) : NULL;
		printf("step %s %s\ncast %s\n", UI.sel[0] ? UI.sel : "-", j ? j : "none", c ? c : "none");
		fflush(stdout);
		free(j);
		free(c);
	} else if (!strcmp(name, "preview"))   /* preview=N: as if the list's row N were hovered (-2 the default) */
		held_preview = atoi(arg);
	else if (!strcmp(name, "picker")) {   /* picker[=KEY]: Pick... beside the selected step's value */
		picker_requested = true;
		snprintf(picker_requested_key, sizeof picker_requested_key, "%s", arg);
	} else if (!strcmp(name, "find") || !strcmp(name, "ptab") || !strcmp(name, "group") || !strcmp(name, "tile")
	           || !strcmp(name, "showscene") || !strcmp(name, "use") || !strcmp(name, "addcast"))
		return picker_do(name, arg);
	else if (!strcmp(name, "move")) {   /* move=FROM|LIST|INDEX, as a drop does it */
		char from[256], list[256];
		int index = 0;
		if (sscanf(arg, "%255[^|]|%255[^|]|%d", from, list, &index) != 3)
			return false;
		return op_move_to(from, list, index);
	} else
		return false;
	return true;
}

/* ---- one frame ---- */

void ui_frame(void)
{
	ImGuiIO *io = igGetIO_Nil();
	double now = igGetTime();
	fields_frame();
	if (held_preview)
		play_effect_preview(held_preview);
	if (!UI.shot && now >= next_scan)
		rescan();
	if (!UI.shot && now >= next_watch) {
		next_watch = now + 0.5;
		int w = doc_watch();
		if (w == 1) {
			doc_gen++;
			ui_status("%s changed on disk: read again", D.rel);
		} else if (w == 2) {
			modal_open = M_CHANGED;
		}
	}
	nlabels = 0;
	if (D.root)
		collect_labels(doc_steps(), "steps");

	igSetNextWindowPos(v2(0, 0), ImGuiCond_Always, v2(0, 0));
	igSetNextWindowSize(io->DisplaySize, ImGuiCond_Always);
	igPushStyleVar_Vec2(ImGuiStyleVar_WindowPadding, v2(0, 0));
	igPushStyleVar_Vec2(ImGuiStyleVar_ItemSpacing, v2(0, 0));
	igBegin("##root", NULL, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings |
	        ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoScrollWithMouse);
	igPopStyleVar(2);
	if (stage_only) {
		/* --do stage: the stage alone over the whole window, to set beside the game's own shots
		 * (with --size 1024x768, one pixel for one). */
		stage_alone(io->DisplaySize.x);
		igEnd();
		return;
	}

	float top_h = S(54), tabs_h = S(38), status_h = S(28);
	int n = problem_count();
	float panel_h = n > 0 && !problems_folded ? S(110) : 0;
	float body_h = io->DisplaySize.y - top_h - tabs_h - status_h - panel_h;
	float left_w = S(260), right_w = S(480);
	float mid_w = io->DisplaySize.x - left_w - right_w;

	top_bar(top_h);

	ImDrawList *dl = igGetWindowDrawList();
	V2 body = igGetCursorScreenPos();
	igPushStyleVar_Vec2(ImGuiStyleVar_WindowPadding, v2(S(14), S(10)));
	igPushStyleVar_Vec2(ImGuiStyleVar_ItemSpacing, v2(S(8), S(6)));
	igBeginChild_Str("##left", v2(left_w, body_h), ImGuiChildFlags_AlwaysUseWindowPadding, 0);
	left_side();
	igEndChild();
	igSameLine(0, 0);
	igBeginChild_Str("##middle", v2(mid_w, body_h), ImGuiChildFlags_AlwaysUseWindowPadding, ImGuiWindowFlags_NoScrollbar);
	middle();
	igEndChild();
	igSameLine(0, 0);
	igBeginChild_Str("##right", v2(right_w, body_h), ImGuiChildFlags_AlwaysUseWindowPadding, 0);
	right_side();
	igEndChild();
	igPopStyleVar(2);
	ImDrawList_AddLine(dl, v2(body.x + left_w, body.y), v2(body.x + left_w, body.y + body_h), col(C_LINE), S(1));
	ImDrawList_AddLine(dl, v2(body.x + left_w + mid_w, body.y), v2(body.x + left_w + mid_w, body.y + body_h), col(C_LINE), S(1));

	igPushStyleVar_Vec2(ImGuiStyleVar_ItemSpacing, v2(S(8), S(6)));
	bottom_panel(tabs_h, panel_h);
	igPopStyleVar(1);
	igSetCursorScreenPos(v2(0, io->DisplaySize.y - status_h));
	status_line(status_h);

	igPushStyleVar_Vec2(ImGuiStyleVar_ItemSpacing, v2(S(8), S(8)));
	modals();
	igPopStyleVar(1);
	igEnd();
}
