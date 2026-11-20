/* The selected step's fields, by kind: a line's speaker, name plate and pages, a picture's number,
 * a track with a play button, a label, a go-to, a flag, a function and its values...
 *
 * A field changes the file's JSON in place as it is typed, so the list and the stage follow, and the
 * change becomes one step to undo when the field is left. A checkbox or a list's pick is a step at
 * once. An optional value left empty is taken out of the file, so the game's default applies. */
#include <float.h>
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "system4.h"

#include "editor.h"

static char cur_place[256];   /* the step being edited */
static bool ro;
static ImGuiID live_id;       /* the field being typed in (D.live) */

int fields_page;              /* the page the stage shows */
static char page_for[256];

#define SCRATCH (64 * 1024)
static char scratch[SCRATCH];

static float label_w(void) { return S(108); }

static float fs(float base) { return base * CORE.scale; }

/* Once a frame, before the window: typing that left its field becomes a step to undo. */
void fields_frame(void)
{
	if (D.live && igGetActiveID() != live_id)
		doc_settle();
}

/* A change from a text field: the file follows as it is typed. */
static void typed(void)
{
	if (!D.live) {
		D.live = true;
		snprintf(D.live_place, sizeof D.live_place, "%s", cur_place);
	}
	live_id = igGetItemID();
}

/* A change from a checkbox, a list or a button: a step to undo now. */
static void picked(void)
{
	doc_settle();
	doc_changed(cur_place);
}

static void after_text_item(void)
{
	if (igIsItemDeactivatedAfterEdit())
		doc_settle();
}

/* ---- the layout of a field: its label on the left ---- */

static void row_label(const char *label, const char *tip)
{
	igAlignTextToFramePadding();
	igTextColored(colv(C_DIM), "%s", label);
	if (tip && igIsItemHovered(ImGuiHoveredFlags_DelayShort))
		igSetTooltip("%s", tip);
	igSameLine(label_w(), 0);
}

static void hint_line(const char *fmt, ...)
{
	char buf[600];
	va_list ap;
	va_start(ap, fmt);
	vsnprintf(buf, sizeof buf, fmt, ap);
	va_end(ap);
	igSetCursorPosX(igGetCursorPosX() + label_w());
	igPushTextWrapPos(0);
	igPushFont(NULL, 14);
	igTextColored(colv(C_FAINT), "%s", buf);
	igPopFont();
	igPopTextWrapPos();
}

static void bad_line(const char *msg)
{
	igSetCursorPosX(igGetCursorPosX() + label_w());
	igPushTextWrapPos(0);
	igPushFont(NULL, 14);
	igTextColored(colv(C_BAD), "%s", msg);
	igPopFont();
	igPopTextWrapPos();
}

/* ---- values in the JSON ---- */

static cJSON *item(cJSON *o, const char *k)
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

static bool name_ok(const char *s)
{
	if (!s[0] || !((s[0] >= 'A' && s[0] <= 'Z') || (s[0] >= 'a' && s[0] <= 'z') || s[0] == '_'))
		return false;
	for (const char *p = s; *p; p++)
		if (!((*p >= 'A' && *p <= 'Z') || (*p >= 'a' && *p <= 'z') || (*p >= '0' && *p <= '9') || *p == '_'))
			return false;
	return true;
}

/* ---- the fields ---- */

/* A text: required (kept, even empty) or optional (taken out when empty). */
static void text_field(const char *id, cJSON *o, const char *k, const char *hint, bool optional)
{
	cJSON *v = item(o, k);
	snprintf(scratch, sizeof scratch, "%s", cJSON_IsString(v) ? v->valuestring : "");
	igSetNextItemWidth(-1);
	if (igInputTextWithHint(id, hint ? hint : "", scratch, sizeof scratch, ro ? ImGuiInputTextFlags_ReadOnly : 0, NULL, NULL)) {
		if (optional && !scratch[0])
			drop(o, k);
		else
			put(o, k, cJSON_CreateString(scratch));
		typed();
	}
	after_text_item();
}

/* A name (letters, digits and _): the same, and a word when it is not one. */
static void name_field(const char *id, cJSON *o, const char *k, const char *hint, bool optional)
{
	text_field(id, o, k, hint, optional);
	cJSON *v = item(o, k);
	if (cJSON_IsString(v) && !name_ok(v->valuestring))
		bad_line(v->valuestring[0] ? "A name is letters, digits and _, not starting with a digit"
		                           : "A name is needed here");
}

static void multiline_field(const char *id, cJSON *o, const char *k, float h)
{
	cJSON *v = item(o, k);
	snprintf(scratch, sizeof scratch, "%s", cJSON_IsString(v) ? v->valuestring : "");
	if (igInputTextMultiline(id, scratch, sizeof scratch, v2(-1, h), ImGuiInputTextFlags_WordWrap | (ro ? ImGuiInputTextFlags_ReadOnly : 0), NULL, NULL)) {
		put(o, k, cJSON_CreateString(scratch));
		typed();
	}
	after_text_item();
}

/* A whole number, typed; empty takes an optional one out (the game's default, shown as the hint). */
static void number_field(const char *id, cJSON *o, const char *k, const char *hint, bool optional, float w)
{
	cJSON *v = item(o, k);
	if (cJSON_IsNumber(v))
		snprintf(scratch, 64, "%.15g", v->valuedouble);
	else if (v)
		snprintf(scratch, 64, "%s", "");
	else
		scratch[0] = 0;
	igSetNextItemWidth(w);
	if (igInputTextWithHint(id, hint ? hint : "", scratch, 64, ImGuiInputTextFlags_CharsDecimal | (ro ? ImGuiInputTextFlags_ReadOnly : 0),
	                        NULL, NULL)) {
		char *end = NULL;
		double d = strtod(scratch, &end);
		if (!scratch[0]) {
			if (optional)
				drop(o, k);
		} else if (end && end != scratch) {
			put(o, k, cJSON_CreateNumber(floor(d) == d ? d : floor(d)));
		}
		typed();
	}
	after_text_item();
	if (v && !cJSON_IsNumber(v)) {
		char *j = json_inline(v);
		char msg[300];
		snprintf(msg, sizeof msg, "The file holds %s here: a whole number is expected", j);
		free(j);
		bad_line(msg);
	}
}

/* true or false with a default: the default takes the key out of the file. */
static void bool_field(const char *label, cJSON *o, const char *k, bool def, const char *tip)
{
	cJSON *v = item(o, k);
	bool on = cJSON_IsBool(v) ? cJSON_IsTrue(v) : def;
	igBeginDisabled(ro);
	if (igCheckbox(label, &on)) {
		if (on == def)
			drop(o, k);
		else
			put(o, k, cJSON_CreateBool(on));
		picked();
	}
	igEndDisabled();
	if (tip && igIsItemHovered(ImGuiHoveredFlags_DelayShort))
		igSetTooltip("%s", tip);
}

/* ---- pictures ---- */

static void cg_thumb(ImDrawList *dl, int no, V2 a, V2 b, bool top)
{
	struct thumb *t = media_thumb(no);
	ImDrawList_AddRectFilled(dl, a, b, col(C_CHILD), S(3), 0);
	if (t->state != 2 || !t->tex)
		return;
	float bw = b.x - a.x, bh = b.y - a.y;
	float s = fmaxf(bw / t->w, bh / t->h);
	float dw = t->w * s, dh = t->h * s;
	float u0 = (dw - bw) / 2 / dw, v0 = top ? 0 : (dh - bh) / 2 / dh;
	ImDrawList_AddImageRounded(dl, texref(t->tex), a, b, v2(u0, v0), v2(1 - u0, v0 + bh / dh), IM_COL32_WHITE, S(3), 0);
}

/* A picture value in a small box: a CG's thumbnail, or a mod picture made small. */
void draw_picture_small(ImDrawList *dl, const cJSON *v, V2 a, V2 b, bool top)
{
	if (cJSON_IsNumber(v)) {
		cg_thumb(dl, (int)v->valuedouble, a, b, top);
		return;
	}
	ImDrawList_AddRectFilled(dl, a, b, col(C_CHILD), S(3), 0);
	const struct picture *p = media_picture(v);
	if (p->state != PIC_READY)
		return;
	float bw = b.x - a.x, bh = b.y - a.y;
	float s = fmaxf(bw / p->w, bh / p->h);
	float dw = p->w * s, dh = p->h * s;
	float u0 = (dw - bw) / 2 / dw, v0 = top ? 0 : (dh - bh) / 2 / dh;
	ImDrawList_AddImageRounded(dl, texref(p->tex), a, b, v2(u0, v0), v2(1 - u0, v0 + bh / dh), IM_COL32_WHITE, S(3), 0);
}

/* What a picture value is, in words: "CG 5240 · Rance: face", "not in the game", "a picture in the
 * mod's folder". */
static void picture_words(const cJSON *v, char *out, size_t n, bool *bad)
{
	*bad = false;
	if (cJSON_IsNumber(v)) {
		int no = (int)v->valuedouble;
		int i = game_find_cg(no);
		if (no == 0) {
			snprintf(out, n, "0: the layer emptied");
		} else if (i < 0) {
			snprintf(out, n, "CG %d is not in the game (a number from rq.image.add works in the game)", no);
			*bad = true;
		} else {
			const struct cg_entry *e = &G.cgs[i];
			const char *folder = e->folder >= 0 && e->folder < G.ncg_folders ? (G.cg_folders[e->folder].en ? G.cg_folders[e->folder].en : G.cg_folders[e->folder].jp) : "";
			snprintf(out, n, "%s%s%s", e->use ? e->use : "", e->use && folder[0] ? " \xC2\xB7 " : "", e->use ? "" : folder);
		}
	} else if (cJSON_IsString(v)) {
		const struct picture *p = media_picture(v);
		if (p->state == PIC_READY)
			snprintf(out, n, "a picture in the mod's folder, %d by %d", p->w, p->h);
		else if (p->state == PIC_LOADING)
			snprintf(out, n, "a picture in the mod's folder");
		else {
			snprintf(out, n, "not found in the mod's folder, or not a PNG, WebP or JPEG this editor reads");
			*bad = true;
		}
	} else if (v) {
		snprintf(out, n, "expected a CG number or a picture path");
		*bad = true;
	} else {
		out[0] = 0;
	}
}

/* A picture: a CG number of the game, or a path in the mod's folder ("faces/mira.png"). */
static void picture_field(const char *id, cJSON *o, const char *k, bool optional)
{
	cJSON *v = item(o, k);
	if (cJSON_IsNumber(v))
		snprintf(scratch, 600, "%.15g", v->valuedouble);
	else if (cJSON_IsString(v))
		snprintf(scratch, 600, "%s", v->valuestring);
	else
		scratch[0] = 0;
	float box_w = S(58), box_h = S(42);
	V2 p = igGetCursorScreenPos();
	igSetNextItemWidth(-box_w - S(10));
	if (igInputTextWithHint(id, optional ? "empty: the layer emptied" : "a CG number, or a picture in the mod's folder",
	                        scratch, 600, ro ? ImGuiInputTextFlags_ReadOnly : 0, NULL, NULL)) {
		char *end = NULL;
		long no = strtol(scratch, &end, 10);
		if (!scratch[0]) {
			if (optional)
				drop(o, k);
		} else if (end && !*end && no >= 0) {
			put(o, k, cJSON_CreateNumber((double)no));
		} else {
			put(o, k, cJSON_CreateString(scratch));
		}
		typed();
	}
	after_text_item();
	v = item(o, k);
	float right = igGetItemRectMax().x + S(10);
	ImDrawList *dl = igGetWindowDrawList();
	bool shown = v && !(cJSON_IsNumber(v) && v->valuedouble <= 0);
	if (shown)
		draw_picture_small(dl, v, v2(right, p.y), v2(right + box_w, p.y + box_h), false);
	char words[300];
	bool bad;
	picture_words(v, words, sizeof words, &bad);
	float y_after = shown ? p.y + box_h + S(4) : 0;
	if (words[0]) {
		igPushTextWrapPos(right - igGetWindowPos().x - S(6));
		igSetCursorPosX(igGetCursorPosX() + label_w());
		igPushFont(NULL, 14);
		igTextColored(colv(bad ? C_BAD : C_FAINT), "%s", words);
		igPopFont();
		igPopTextWrapPos();
	}
	if (igGetCursorScreenPos().y < y_after)
		igDummy(v2(0, y_after - igGetCursorScreenPos().y));
}

/* ---- values (args, end, a variable's value) ---- */

static const char *const vtypes[] = { "Text", "Number", "true", "false", "nothing" };

static int value_type(const cJSON *v)
{
	if (cJSON_IsString(v)) return 0;
	if (cJSON_IsNumber(v)) return 1;
	if (cJSON_IsTrue(v)) return 2;
	if (cJSON_IsFalse(v)) return 3;
	return 4;
}

/* A plain value in a list or an object: its type, then a field for a text or a number. Returns the
 * new item when it was replaced (the caller's pointer goes stale). */
static void value_edit(const char *id, cJSON *parent, const char *key, int index, float w)
{
	cJSON *v = key ? item(parent, key) : cJSON_GetArrayItem(parent, index);
	int t = value_type(v);
	igPushID_Str(id);
	igSetNextItemWidth(S(90));
	igBeginDisabled(ro);
	if (igBeginCombo("##type", vtypes[t], 0)) {
		for (int i = 0; i < 5; i++) {
			if (igSelectable_Bool(vtypes[i], i == t, 0, v2(0, 0)) && i != t) {
				cJSON *nv = i == 0 ? cJSON_CreateString("") : i == 1 ? cJSON_CreateNumber(0)
				          : i == 2 ? cJSON_CreateTrue() : i == 3 ? cJSON_CreateFalse() : cJSON_CreateNull();
				if (key)
					put(parent, key, nv);
				else
					cJSON_ReplaceItemInArray(parent, index, nv);
				picked();
				v = nv;
				t = i;
			}
		}
		igEndCombo();
	}
	igEndDisabled();
	if (t == 0 || t == 1) {
		igSameLine(0, S(6));
		if (t == 0)
			snprintf(scratch, 4096, "%s", v->valuestring);
		else
			snprintf(scratch, 64, "%.15g", v->valuedouble);
		igSetNextItemWidth(w > 0 ? w - S(96) : -1);
		if (igInputText("##v", scratch, t == 0 ? 4096 : 64, (t == 1 ? ImGuiInputTextFlags_CharsScientific : 0) | (ro ? ImGuiInputTextFlags_ReadOnly : 0),
		                NULL, NULL)) {
			cJSON *nv;
			if (t == 0) {
				nv = cJSON_CreateString(scratch);
			} else {
				char *end = NULL;
				double d = strtod(scratch, &end);
				nv = cJSON_CreateNumber(end && end != scratch ? d : 0);
			}
			if (key)
				put(parent, key, nv);
			else
				cJSON_ReplaceItemInArray(parent, index, nv);
			typed();
		}
		after_text_item();
	}
	igPopID();
}

/* args: values in order, each with a remove button, and + Value. */
static void args_field(cJSON *o, const char *k, const char *what)
{
	row_label("Values", what);
	cJSON *args = item(o, k);
	int n = cJSON_IsArray(args) ? cJSON_GetArraySize(args) : 0;
	if (!n)
		igTextColored(colv(C_FAINT), "none");
	for (int i = 0; i < n; i++) {
		if (i)
			igSetCursorPosX(label_w());
		char id[32];
		snprintf(id, sizeof id, "arg%d", i);
		float avail = igGetContentRegionAvail().x - S(34);
		value_edit(id, args, NULL, i, avail);
		igSameLine(0, S(6));
		igPushID_Int(i);
		igBeginDisabled(ro);
		if (igButton("\xC3\x97##del", v2(S(26), 0))) {
			cJSON_DeleteItemFromArray(args, i);
			if (!cJSON_GetArraySize(args))
				drop(o, k);
			picked();
			igEndDisabled();
			igPopID();
			return;
		}
		igEndDisabled();
		igPopID();
	}
	igSetCursorPosX(label_w());
	igBeginDisabled(ro);
	if (igButton("+ Value", v2(0, 0))) {
		if (!cJSON_IsArray(args)) {
			args = cJSON_CreateArray();
			put(o, k, args);
		}
		cJSON_AddItemToArray(args, cJSON_CreateString(""));
		picked();
	}
	igEndDisabled();
}

/* A condition, until the condition builder: its JSON on one line, read when the field is left. */
static char cond_buf[4096], cond_for[300], cond_why[300];

static void json_value_field(const char *id, cJSON *o, const char *k, const char *hint)
{
	char key[300];
	snprintf(key, sizeof key, "%s/%s/%d", cur_place, k, D.at);
	cJSON *v = item(o, k);
	if (strcmp(cond_for, key) && !igIsItemActive()) {
		char *j = v ? json_inline(v) : NULL;
		snprintf(cond_buf, sizeof cond_buf, "%s", j ? j : "");
		free(j);
		snprintf(cond_for, sizeof cond_for, "%s", key);
		cond_why[0] = 0;
	}
	igSetNextItemWidth(-1);
	igPushFont(F_MONO, MONO_SIZE);
	igInputTextWithHint(id, hint, cond_buf, sizeof cond_buf, ro ? ImGuiInputTextFlags_ReadOnly : 0, NULL, NULL);
	igPopFont();
	if (igIsItemDeactivatedAfterEdit()) {
		cJSON *nv = NULL;
		char why[256];
		if (!scene_parse(cond_buf, &nv, why, sizeof why)) {
			snprintf(cond_why, sizeof cond_why, "Not applied: %s", why);
		} else {
			put(o, k, nv);
			cond_why[0] = 0;
			picked();
			snprintf(cond_for, sizeof cond_for, "%s/%s/%d", cur_place, k, D.at);
		}
	}
	if (cond_why[0])
		bad_line(cond_why);
}

/* ---- a line's pages ---- */

static void set_pages(cJSON *o, const char *k, cJSON *arr)
{
	/* One page is written as a text, more as a list. */
	int n = cJSON_GetArraySize(arr);
	if (n == 1 && cJSON_IsString(arr->child)) {
		put(o, k, cJSON_CreateString(arr->child->valuestring));
		cJSON_Delete(arr);
	} else {
		put(o, k, arr);
	}
}

static cJSON *pages_copy(cJSON *t)
{
	if (cJSON_IsArray(t))
		return cJSON_Duplicate(t, true);
	cJSON *a = cJSON_CreateArray();
	cJSON_AddItemToArray(a, cJSON_CreateString(cJSON_IsString(t) ? t->valuestring : ""));
	return a;
}

static void pages_field(cJSON *o, const char *k)
{
	cJSON *t = item(o, k);
	if (t && !cJSON_IsString(t) && !cJSON_IsArray(t)) {
		bad_line("The text is not a text or a list of pages; the JSON below shows it");
		return;
	}
	int n = cJSON_IsArray(t) ? cJSON_GetArraySize(t) : 1;
	for (int i = 0; i < n; i++) {
		const char *page = cJSON_IsArray(t) ? (cJSON_IsString(cJSON_GetArrayItem(t, i)) ? cJSON_GetArrayItem(t, i)->valuestring : "")
		                                    : (cJSON_IsString(t) ? t->valuestring : "");
		int lines = page_lines(o, page), most = line_limit(o);
		igPushID_Int(i);
		igTextUnformatted("Page", NULL);
		igSameLine(0, S(4));
		igText("%d", i + 1);
		igSameLine(0, S(6));
		igPushFont(NULL, 14);
		igTextColored(colv(lines > most ? C_BAD : C_FAINT), "\xC2\xB7 %d line%s of %d%s", lines, lines == 1 ? "" : "s", most,
		              lines > most ? ": the game draws the rest past the box" : "");
		igPopFont();
		if (n > 1) {
			igSameLine(igGetWindowWidth() - S(40), 0);
			igBeginDisabled(ro);
			if (igSmallButton("\xC3\x97##drop")) {
				cJSON *a = pages_copy(t);
				cJSON_DeleteItemFromArray(a, i);
				set_pages(o, k, a);
				picked();
				if (fields_page >= n - 1)
					fields_page = n - 2;
				igEndDisabled();
				igPopID();
				return;
			}
			igEndDisabled();
			if (igIsItemHovered(0))
				igSetTooltip("Remove page %d", i + 1);
		}
		snprintf(scratch, sizeof scratch, "%s", page);
		float h = S(24) + igGetTextLineHeight() * (float)(lines < 2 ? 2 : lines > 5 ? 5 : lines);
		if (igInputTextMultiline("##page", scratch, sizeof scratch, v2(-1, h), ImGuiInputTextFlags_WordWrap | (ro ? ImGuiInputTextFlags_ReadOnly : 0), NULL, NULL)) {
			if (n == 1) {
				put(o, k, cJSON_CreateString(scratch));
			} else {
				cJSON *a = pages_copy(t);
				cJSON_ReplaceItemInArray(a, i, cJSON_CreateString(scratch));
				set_pages(o, k, a);
			}
			typed();
			t = item(o, k);
		}
		if (igIsItemActive() || igIsItemFocused())
			fields_page = i;
		after_text_item();
		igPopID();
	}
	igBeginDisabled(ro);
	if (igButton("+ Page", v2(0, 0))) {
		cJSON *a = pages_copy(t);
		cJSON_AddItemToArray(a, cJSON_CreateString(""));
		set_pages(o, k, a);
		fields_page = n;
		picked();
	}
	igEndDisabled();
	if (igIsItemHovered(ImGuiHoveredFlags_DelayShort))
		igSetTooltip("Another page: the game waits for a click between pages");
}

/* ---- the speaker ---- */

static void face_preview(ImDrawList *dl, const cJSON *face, V2 p, float h)
{
	draw_picture_small(dl, face, p, v2(p.x + h * 0.85f, p.y + h), true);
}

static void speaker_field(cJSON *st, const char *k, const cJSON *cast)
{
	cJSON *v = item(st, k);
	bool is_cast = cJSON_IsString(v) && !strchr(v->valuestring, '.');
	bool is_path = cJSON_IsString(v) && !is_cast;
	const cJSON *face = is_cast ? item(item((cJSON *)cast, v->valuestring), "face") : v;
	char preview[300];
	if (is_cast) {
		const cJSON *e = item((cJSON *)cast, v->valuestring);
		if (!e)
			snprintf(preview, sizeof preview, "%s  \xC2\xB7 not in the cast", v->valuestring);
		else if (cJSON_IsNumber(face))
			snprintf(preview, sizeof preview, "%s  \xC2\xB7 face %d, from the cast", v->valuestring, (int)face->valuedouble);
		else
			snprintf(preview, sizeof preview, "%s  \xC2\xB7 from the cast", v->valuestring);
	} else if (cJSON_IsNumber(v)) {
		const char *who = media_face_owner((int)v->valuedouble);
		snprintf(preview, sizeof preview, "face %d%s%s", (int)v->valuedouble, who ? "  \xC2\xB7 " : "", who ? who : "");
	} else if (is_path) {
		snprintf(preview, sizeof preview, "a picture: %s", v->valuestring);
	} else {
		snprintf(preview, sizeof preview, "%s", "none");
	}
	row_label("Speaker", "The face beside the line: a name of the cast, a face CG number, or a picture in the mod's folder");
	float ih = S(30);
	igSetNextItemWidth(-1);
	igPushStyleVar_Vec2(ImGuiStyleVar_FramePadding, v2(S(9), (ih - igGetFontSize()) / 2));
	igBeginDisabled(ro);
	bool open = igBeginCombo("##speaker", "", ImGuiComboFlags_CustomPreview | ImGuiComboFlags_HeightLarge);
	igPopStyleVar(1);
	if (igBeginComboPreview()) {
		V2 p = igGetCursorScreenPos();
		ImDrawList *dl = igGetWindowDrawList();
		if (face)
			face_preview(dl, face, v2(p.x, p.y - S(4)), ih - S(6));
		igSetCursorScreenPos(v2(p.x + (face ? ih : 0), p.y));
		igTextUnformatted(preview, NULL);
		igEndComboPreview();
	}
	if (open) {
		int i = 0;
		for (const cJSON *e = cast ? cast->child : NULL; e; e = e->next, i++) {
			igPushID_Int(i);
			V2 p = igGetCursorScreenPos();
			if (igSelectable_Bool("##c", is_cast && !strcmp(v->valuestring, e->string), 0, v2(0, S(34)))) {
				put(st, k, cJSON_CreateString(e->string));
				picked();
			}
			ImDrawList *dl = igGetWindowDrawList();
			const cJSON *f = item((cJSON *)e, "face");
			face_preview(dl, f, v2(p.x + S(2), p.y + S(2)), S(30));
			ImDrawList_AddText_FontPtr(dl, F_MONO, fs(15), v2(p.x + S(34), p.y + S(8)), col(C_TEXT), e->string, NULL, 0, NULL);
			char meta[200];
			const cJSON *nm = item((cJSON *)e, "name");
			if (cJSON_IsNumber(f))
				snprintf(meta, sizeof meta, "face %d%s%s", (int)f->valuedouble, cJSON_IsString(nm) ? " \xC2\xB7 " : "",
				         cJSON_IsString(nm) ? nm->valuestring : "");
			else
				snprintf(meta, sizeof meta, "%s", cJSON_IsString(nm) ? nm->valuestring : "");
			float mx = p.x + S(46) + ImFont_CalcTextSizeA(F_MONO, fs(15), FLT_MAX, 0, e->string, NULL, NULL).x;
			ImDrawList_AddText_FontPtr(dl, F_UI, fs(14), v2(mx, p.y + S(9)), col(C_FAINT), meta, NULL, 0, NULL);
			igPopID();
		}
		if (i)
			igSeparator();
		if (igSelectable_Bool("A face by its CG number", cJSON_IsNumber(v), 0, v2(0, 0)) && !cJSON_IsNumber(v)) {
			int no = cJSON_IsNumber(face) ? (int)face->valuedouble : 2000;
			put(st, k, cJSON_CreateNumber(no));
			picked();
		}
		if (igSelectable_Bool("A picture in the mod's folder", is_path, 0, v2(0, 0)) && !is_path) {
			put(st, k, cJSON_CreateString("faces/name.png"));
			picked();
		}
		igEndCombo();
	}
	igEndDisabled();
	if (cJSON_IsNumber(v)) {
		row_label("Face", "A face CG of the game (the faces start at 2000)");
		picture_field("##face", st, k, false);
	} else if (is_path) {
		row_label("Picture", "A picture in the mod's folder: PNG, WebP, JPEG, or a GIF that moves");
		picture_field("##face", st, k, false);
	} else if (is_cast && !item((cJSON *)cast, v->valuestring)) {
		bad_line("This name is not in the cast: add it in Cast and file, or pick another speaker");
	}
}

/* ---- by kind ---- */

static void line_fields(cJSON *st, const char *kind, const cJSON *cast)
{
	bool narr = !strcmp(kind, "narrate");
	if (!narr) {
		speaker_field(st, kind, cast);
		bool game = false;
		cJSON *nm = item(st, "name");
		const char *plate = NULL;
		if (!cJSON_IsString(nm)) {
			cJSON *tmp = cJSON_Duplicate(st, true);
			drop(tmp, "name");
			plate = line_plate(tmp, cast, &game);
			char hint[300];
			if (plate && game)
				snprintf(hint, sizeof hint, "the game's name for this face (%s)", plate);
			else if (plate)
				snprintf(hint, sizeof hint, "%s (the cast's)", plate);
			else
				snprintf(hint, sizeof hint, "the game's name for this face");
			row_label("Name plate", "The name above the box; empty: the cast's, else the game's for the face");
			text_field("##name", st, "name", hint, true);
			cJSON_Delete(tmp);
		} else {
			row_label("Name plate", "The name above the box; empty: the cast's, else the game's for the face");
			text_field("##name", st, "name", "the game's name for this face", true);
		}
		igSetCursorPosX(label_w());
		bool_field("No face", st, "hide_face", false, "The name plate with no face beside it");
		igSameLine(0, S(16));
	} else {
		igSetCursorPosX(label_w());
		bool_field("Paper", st, "paper", false, "The paper frame, for a letter or a note");
		igSameLine(0, S(16));
	}
	bool_field("Wait for a click", st, "wait", true,
	           "Off: the last page shows without waiting, as for a question above a choice");
	igSetCursorPosX(label_w());
	bool_field("Clear the box first", st, "page", false, "The game's E frame: the box starts empty");
	igSpacing();
	pages_field(st, narr ? "narrate" : "text");
}

static int default_time(const char *kind)
{
	return !strcmp(kind, "portrait") || !strcmp(kind, "mini") || !strcmp(kind, "flash") ? 200 : 500;
}

/* ---- effects, shakes and the world map: a number, and the list in words beside it ---- */

/* A row of a list of numbers: a click puts its number in (PREVIEW_DEFAULT takes the key out), and
 * while it is hovered the stage plays the step with it. */
static void pick_row(cJSON *st, const char *k, int no, const char *label, bool selected)
{
	if (igSelectable_Bool(label, selected, 0, v2(0, 0)) && !selected) {
		if (no == PREVIEW_DEFAULT)
			drop(st, k);
		else
			put(st, k, cJSON_CreateNumber(no));
		picked();
	}
	if (igIsItemHovered(0))
		play_effect_preview(no);
}

/* set's effects (エフェクトセット表示): 1, 7001, and 7002 for any other number. */
static const char *set_effect_words(bool has, int no)
{
	if (!has)
		return "None: a crossfade of half a second";
	if (no == 1)
		return "Black, then waves for a second";
	if (no == 7001)
		return "Black, an R closing on green, then a crossfade";
	if (no == 7002)
		return "Black, a ghost's shape closing, then a crossfade";
	return "As 7002: black, a ghost's shape closing, then a crossfade";
}

static void effect_words(bool has, int no, char *out, size_t n)
{
	if (!has) {
		snprintf(out, n, "The default: a crossfade (1)");
		return;
	}
	const char *name = effect_name(no);
	if (!name)
		snprintf(out, n, "Not an effect: the pictures show at once");
	else if (no != 27 && !effect_written(no))
		snprintf(out, n, "%s (the engine plays it as 51)", name);
	else
		snprintf(out, n, "%s", name);
}

static void effect_field(cJSON *st, const char *kind)
{
	bool set = !strcmp(kind, "set");
	row_label("Effect", set ? "How the new pictures come in" : "The transition, from the engine's list");
	number_field("##effect", st, "effect", set ? "none" : "1", true, S(64));
	cJSON *v = item(st, "effect");
	bool has = cJSON_IsNumber(v);
	int no = has ? (int)v->valuedouble : 0;
	char shown[200];
	if (set)
		snprintf(shown, sizeof shown, "%s", set_effect_words(has, no));
	else
		effect_words(has, no, shown, sizeof shown);
	igSameLine(0, S(6));
	igSetNextItemWidth(-1);
	igBeginDisabled(ro);
	if (igBeginCombo("##effect-list", shown, ImGuiComboFlags_HeightLarge)) {
		char row[200];
		if (set) {
			static const int nos[] = { 1, 7001, 7002 };
			pick_row(st, "effect", PREVIEW_DEFAULT, set_effect_words(false, 0), !v);
			for (int i = 0; i < 3; i++) {
				snprintf(row, sizeof row, "%-5d %s", nos[i], set_effect_words(true, nos[i]));
				pick_row(st, "effect", nos[i], row, has && no == nos[i]);
			}
		} else {
			pick_row(st, "effect", PREVIEW_DEFAULT, "The default: a crossfade (1)", !v);
			for (int i = 1; i <= EFFECT_LAST; i++) {
				if (!effect_name(i))
					continue;
				char words[160];
				effect_words(true, i, words, sizeof words);
				snprintf(row, sizeof row, "%-3d %s", i, words);
				pick_row(st, "effect", i, row, has && no == i);
			}
		}
		igEndCombo();
	}
	igEndDisabled();
	if (igIsItemHovered(ImGuiHoveredFlags_DelayShort))
		igSetTooltip("A row hovered in the list plays on the stage");
}

/* A shake's mode in words (全画面揺れ, 顔揺れ and 立ち絵揺れ). */
static void shake_words(int part, int mode, char *out, size_t n)
{
	float sx, sy, ms;
	shake_mode(part, mode, &sx, &sy, &ms);
	const char *way = sx > 0 && sy > 0 ? "Every way" : sy > 0 ? "Up and down" : "Side to side";
	snprintf(out, n, "%s, %g pixels, %g s", way, sx > sy ? sx : sy, ms / 1000);
}

static void shake_field(cJSON *st, const char *kind)
{
	int part = !strcmp(kind, "shake") ? 0 : !strcmp(kind, "shake_face") ? 1 : 2;
	row_label("Mode", "How the game shakes it");
	number_field("##mode", st, "mode", "1", true, S(64));
	cJSON *v = item(st, "mode");
	bool has = cJSON_IsNumber(v);
	int mode = has ? (int)v->valuedouble : 1;
	char shown[160], words[120];
	shake_words(part, mode, words, sizeof words);
	snprintf(shown, sizeof shown, "%s%s", has ? "" : "The default: ", words);
	igSameLine(0, S(6));
	igSetNextItemWidth(-1);
	igBeginDisabled(ro);
	if (igBeginCombo("##mode-list", shown, 0)) {
		char row[160];
		shake_words(part, 1, words, sizeof words);
		snprintf(row, sizeof row, "The default: %s", words);
		pick_row(st, "mode", PREVIEW_DEFAULT, row, !v);
		for (int m = 1; m <= 4; m++) {
			shake_words(part, m, words, sizeof words);
			snprintf(row, sizeof row, "%d  %s%s", m, words, m == 4 ? " (any other number)" : "");
			pick_row(st, "mode", m, row, has && (m < 4 ? mode == m : mode < 1 || mode > 3));
		}
		igEndCombo();
	}
	igEndDisabled();
	if (igIsItemHovered(ImGuiHoveredFlags_DelayShort))
		igSetTooltip("A row hovered in the list plays on the stage");
}

/* The world map: one of the game's 40 places, or a point of the mod's own with its name. */
static void world_map_field(cJSON *st)
{
	cJSON *w = item(st, "world_map");
	bool point = cJSON_IsObject(w);
	row_label("Where", "A place of the game's world map, or a point of the mod's own");
	igBeginDisabled(ro);
	if (igRadioButton_Bool("A place of the game's", !point) && point) {
		put(st, "world_map", cJSON_CreateNumber(1));
		picked();
	}
	igSameLine(0, S(16));
	if (igRadioButton_Bool("A point of its own", point) && !point) {
		int x = 512, y = 384;
		const char *name = NULL;
		media_place(cJSON_IsNumber(w) ? (int)w->valuedouble : 1, &x, &y, &name);
		cJSON *o = cJSON_CreateObject();
		cJSON_AddNumberToObject(o, "x", x);
		cJSON_AddNumberToObject(o, "y", y);
		cJSON_AddStringToObject(o, "name", name ? name : "A place");
		put(st, "world_map", o);
		picked();
	}
	igEndDisabled();
	w = item(st, "world_map");
	if (cJSON_IsObject(w)) {
		row_label("Point", "Where its tag points: x 0 to 1023, y 0 to 767; left of 512 the tag opens to the right");
		number_field("##x", w, "x", "x", false, S(80));
		igSameLine(0, S(8));
		number_field("##y", w, "y", "y", false, S(80));
		row_label("Name", "What its tag shows");
		text_field("##name", w, "name", "the tag's text", false);
		return;
	}
	int no = cJSON_IsNumber(w) ? (int)w->valuedouble : 0;
	int x = 0, y = 0;
	const char *name = NULL;
	bool known = media_place(no, &x, &y, &name);
	row_label("Place", "One of the game's places, 1 to 40");
	number_field("##place", st, "world_map", "1 to 40", false, S(64));
	char shown[200];
	if (known)
		snprintf(shown, sizeof shown, "%s (%d, %d)", name, x, y);
	else
		snprintf(shown, sizeof shown, "%s", cJSON_IsNumber(w) ? "Not a place of the game's" : "Pick a place");
	igSameLine(0, S(6));
	igSetNextItemWidth(-1);
	igBeginDisabled(ro);
	if (igBeginCombo("##place-list", shown, ImGuiComboFlags_HeightLarge)) {
		for (int i = 1; i <= PLACES_MAX; i++) {
			int px, py;
			const char *pn = NULL;
			if (!media_place(i, &px, &py, &pn))
				continue;
			char row[200];
			snprintf(row, sizeof row, "%-3d %s (%d, %d)", i, pn, px, py);
			if (igSelectable_Bool(row, i == no, 0, v2(0, 0)) && i != no) {
				put(st, "world_map", cJSON_CreateNumber(i));
				picked();
			}
		}
		igEndCombo();
	}
	igEndDisabled();
	if (cJSON_IsNumber(w) && !known)
		bad_line(no >= 1 && no <= PLACES_MAX ? "The game's list has no data for this place here" : "A place is 1 to 40");
}

static void effect_time(cJSON *st, const char *kind, bool effect, bool time)
{
	char hint[64];
	if (effect)
		effect_field(st, kind);
	if (time) {
		row_label("Time", "How long the transition takes, in milliseconds");
		snprintf(hint, sizeof hint, "%d ms: the default", default_time(kind));
		number_field("##time", st, "time", hint, true, S(150));
		igSameLine(0, S(6));
		igAlignTextToFramePadding();
		igTextColored(colv(C_FAINT), "ms");
	}
}

static void sound_field(cJSON *st, const char *kind, int what)
{
	row_label(!strcmp(kind, "music") ? "Track" : !strcmp(kind, "sound") ? "Sound" : "Jingle", NULL);
	number_field("##no", st, kind, "its number", false, S(110));
	cJSON *v = item(st, kind);
	int no = cJSON_IsNumber(v) ? (int)v->valuedouble : -1;
	if (what >= 0 && no >= 0 && media_can_play(what, no)) {
		igSameLine(0, S(6));
		bool on = media_playing(what, no);
		if (igButton(on ? "\xE2\x96\xA0  Stop" : "\xE2\x96\xB6  Play", v2(0, 0)))
			media_play(what, no);
	}
	if (no >= 0) {
		const char *name = !strcmp(kind, "music") ? media_music_name(no) : NULL;
		if (name)
			hint_line("%s", name);
		else if (what >= 0 && !media_can_play(what, no))
			bad_line(what == SOUND_MUSIC ? "Not a track of the game" : "Not a sound of the game");
	}
}

static const char *const end_values[] = { "clear", "fail", "abandon", "escape" };

void fields_step(cJSON *st, const char *place, bool readonly)
{
	snprintf(cur_place, sizeof cur_place, "%s", place);
	ro = readonly;
	if (strcmp(page_for, place)) {
		fields_page = 0;
		snprintf(page_for, sizeof page_for, "%s", place);
	}
	const cJSON *cast = item(D.root, "cast");
	const char *kind = step_kind(st);
	if (!kind) {
		igPushTextWrapPos(0);
		igTextColored(colv(C_BAD), "Not a step the editor knows: a step is an object named by one of its keys, such as "
		              "{ \"say\": 2000, \"text\": \"Hello.\" }. Its JSON is below.");
		igPopTextWrapPos();
		return;
	}
	igPushID_Str(place);
	igPushStyleVar_Vec2(ImGuiStyleVar_ItemSpacing, v2(S(8), S(7)));
	if (!strcmp(kind, "say") || !strcmp(kind, "think") || !strcmp(kind, "narrate")) {
		line_fields(st, kind, cast);
	} else if (!strcmp(kind, "clear_text")) {
		igTextColored(colv(C_DIM), "Empties the message box. The next line starts in an empty box.");
	} else if (!strcmp(kind, "speed")) {
		row_label("Speed", "How fast the text appears");
		number_field("##speed", st, "speed", "ms a letter", false, S(110));
		hint_line("Milliseconds a letter; 0 shows each page at once; -1 goes back to the player's setting.");
	} else if (!strcmp(kind, "wait_click")) {
		igTextColored(colv(C_DIM), "Waits for a click before the next step.");
	} else if (!strcmp(kind, "background") || !strcmp(kind, "wallpaper") || !strcmp(kind, "mask_wallpaper")
	           || !strcmp(kind, "portrait") || !strcmp(kind, "mini") || !strcmp(kind, "cg")) {
		row_label("Picture", "A CG number of the game, or a picture in the mod's folder");
		picture_field("##pic", st, kind, false);
		effect_time(st, kind, true, true);
	} else if (!strcmp(kind, "background_in")) {
		row_label("Picture", "A CG number of the game, or a picture in the mod's folder");
		picture_field("##pic", st, kind, false);
		effect_time(st, kind, false, true);
	} else if (!strcmp(kind, "background_out")) {
		igTextColored(colv(C_DIM), "Takes the background out.");
		effect_time(st, kind, false, true);
	} else if (!strcmp(kind, "set")) {
		cJSON *set = item(st, "set");
		if (!cJSON_IsObject(set)) {
			set = cJSON_CreateObject();
			put(st, "set", set);
		}
		static const char *const keys[] = { "wallpaper", "background", "portrait", "mini", "cg" };
		static const char *const names[] = { "Wallpaper", "Background", "Standing", "Small picture", "Scene CG" };
		for (int i = 0; i < 5; i++) {
			igPushID_Int(i);
			row_label(names[i], NULL);
			picture_field("##layer", set, keys[i], true);
			igPopID();
		}
		hint_line("A layer left empty is emptied. With a scene CG or an effect, the layers change in one transition.");
		effect_time(st, kind, true, false);
	} else if (!strcmp(kind, "black") || !strcmp(kind, "white")) {
		igTextColored(colv(C_DIM), "The screen goes %s.", kind);
		effect_time(st, kind, true, true);
	} else if (!strcmp(kind, "clear")) {
		igTextColored(colv(C_DIM), "Takes the scene's pictures away.");
	} else if (!strcmp(kind, "flash")) {
		effect_time(st, kind, false, true);
	} else if (!strcmp(kind, "shake") || !strcmp(kind, "shake_face") || !strcmp(kind, "shake_portrait")) {
		shake_field(st, kind);
	} else if (!strcmp(kind, "world_map")) {
		world_map_field(st);
		hint_line("The game's world map with the place's tag (jingle 407); a click goes on.");
	} else if (!strcmp(kind, "music")) {
		sound_field(st, kind, SOUND_MUSIC);
		row_label("Fade", "The fade type; -1 the game's");
		number_field("##effect", st, "effect", "-1: the game's", true, S(150));
		row_label("Fade time", "The fade's time; -1 the game's");
		number_field("##time", st, "time", "-1: the game's", true, S(150));
	} else if (!strcmp(kind, "sound")) {
		sound_field(st, kind, SOUND_EFFECT);
	} else if (!strcmp(kind, "jingle")) {
		sound_field(st, kind, SOUND_EFFECT);   /* the game's jingles are among its sounds (1008) */
		igSetCursorPosX(label_w());
		bool_field("Wait for its end", st, "wait", false, NULL);
	} else if (!strcmp(kind, "choice")) {
		cJSON *rows = item(st, "choice");
		if (cJSON_IsArray(rows)) {
			int n = cJSON_GetArraySize(rows), r = 0;
			for (cJSON *row = rows->child; row; row = row->next, r++) {
				igPushID_Int(r);
				char label[32];
				snprintf(label, sizeof label, "Row %d", r + 1);
				if (cJSON_IsString(row) && !strcmp(row->valuestring, "-")) {
					row_label(label, NULL);
					igTextColored(colv(C_FAINT), "\xE2\x80\x94 a separator \xE2\x80\x94");
				} else if (cJSON_IsString(row)) {
					row_label(label, "The row's text; its place in the list goes into the choice's variable");
					snprintf(scratch, 4096, "%s", row->valuestring);
					igSetNextItemWidth(-S(34));
					if (igInputText("##text", scratch, 4096, ro ? ImGuiInputTextFlags_ReadOnly : 0, NULL, NULL)) {
						cJSON_ReplaceItemInArray(rows, r, cJSON_CreateString(scratch));
						typed();
					}
					after_text_item();
				} else if (cJSON_IsObject(row)) {
					row_label(label, "The row's text");
					igSetNextItemWidth(-S(34));
					cJSON *t = item(row, "text");
					snprintf(scratch, 4096, "%s", cJSON_IsString(t) ? t->valuestring : "");
					if (igInputText("##text", scratch, 4096, ro ? ImGuiInputTextFlags_ReadOnly : 0, NULL, NULL)) {
						put(row, "text", cJSON_CreateString(scratch));
						typed();
					}
					after_text_item();
				} else {
					row_label(label, NULL);
					igTextColored(colv(C_BAD), "not a row");
				}
				igSameLine(0, S(6));
				igBeginDisabled(ro || n <= 1);
				bool del = igButton("\xC3\x97##del", v2(S(26), 0));
				igEndDisabled();
				if (del) {
					cJSON_DeleteItemFromArray(rows, r);
					picked();
					igPopID();
					break;
				}
				if (cJSON_IsObject(row)) {
					igSetCursorPosX(label_w());
					igPushFont(NULL, 14);
					cJSON *c = item(row, "comment");
					snprintf(scratch, 4096, "%s", cJSON_IsString(c) ? c->valuestring : "");
					igSetNextItemWidth(-S(34));
					if (igInputTextWithHint("##comment", "a comment in the game's box (none)", scratch, 4096, ro ? ImGuiInputTextFlags_ReadOnly : 0,
					                        NULL, NULL)) {
						if (scratch[0])
							put(row, "comment", cJSON_CreateString(scratch));
						else
							drop(row, "comment");
						typed();
					}
					after_text_item();
					char what[300] = "";
					size_t o = 0;
					if (item(row, "if"))
						o += (size_t)snprintf(what + o, sizeof what - o, "shown when its if holds; ");
					cJSON *steps = item(row, "steps"), *go = item(row, "goto"), *end = item(row, "end");
					if (cJSON_IsArray(steps) && o < sizeof what)
						o += (size_t)snprintf(what + o, sizeof what - o, "plays its %d step%s (in the list)", cJSON_GetArraySize(steps),
						                      cJSON_GetArraySize(steps) == 1 ? "" : "s");
					else if (cJSON_IsString(go) && o < sizeof what)
						o += (size_t)snprintf(what + o, sizeof what - o, "goes to %s", go->valuestring);
					else if (end && o < sizeof what)
						o += (size_t)snprintf(what + o, sizeof what - o, "ends the scene");
					if (what[0]) {
						igSetCursorPosX(label_w());
						igTextColored(colv(C_FAINT), "%s", what);
					}
					igPopFont();
				}
				igPopID();
			}
			igSetCursorPosX(label_w());
			igBeginDisabled(ro);
			if (igButton("+ Row", v2(0, 0))) {
				char t[32];
				snprintf(t, sizeof t, "Row %d", cJSON_GetArraySize(rows) + 1);
				cJSON_AddItemToArray(rows, cJSON_CreateString(t));
				picked();
			}
			igSameLine(0, S(6));
			if (igButton("+ Separator", v2(0, 0))) {
				cJSON_AddItemToArray(rows, cJSON_CreateString("-"));
				picked();
			}
			igSameLine(0, S(6));
			if (igButton("+ Row with steps", v2(0, 0))) {
				cJSON *row = cJSON_CreateObject();
				char t[32];
				snprintf(t, sizeof t, "Row %d", cJSON_GetArraySize(rows) + 1);
				cJSON_AddStringToObject(row, "text", t);
				cJSON_AddItemToObject(row, "steps", cJSON_CreateArray());
				cJSON_AddItemToArray(rows, row);
				picked();
			}
			igEndDisabled();
			hint_line("A row's condition, go-to and end, and its colour and sound: in its JSON below for now.");
		} else {
			row_label("Rows", "Rows from the mod's function");
			json_value_field("##rows", st, "choice", "{ \"call\": \"name\" }");
		}
		row_label("Into", "A variable that gets the picked row's id (its place in the list from 1, or its id)");
		name_field("##into", st, "into", "none", true);
		row_label("Place", "Where the list goes on the screen; empty: the game's");
		number_field("##x", st, "x", "x: the game's", true, S(110));
		igSameLine(0, S(8));
		number_field("##y", st, "y", "y: 360", true, S(110));
	} else if (!strcmp(kind, "if")) {
		row_label("If", "A condition: a flag, a variable, the mod's function, or one of the game's checks");
		json_value_field("##if", st, "if", "{ \"seen\": \"mymod:flag\" }");
		hint_line("Its steps are under it in the list. The condition builder comes in a later version.");
		cJSON *els = item(st, "else");
		igSetCursorPosX(label_w());
		igBeginDisabled(ro);
		if (!els) {
			if (igButton("+ Else", v2(0, 0))) {
				put(st, "else", cJSON_CreateArray());
				picked();
			}
		} else if (cJSON_IsArray(els) && !els->child) {
			if (igButton("Remove the empty else", v2(0, 0))) {
				drop(st, "else");
				picked();
			}
		}
		igEndDisabled();
	} else if (!strcmp(kind, "label")) {
		row_label("Label", "A place a go-to (and Play from here) can start at; one name each in the file");
		name_field("##label", st, "label", "a name", false);
		cJSON *v = item(st, "label");
		int same = 0;
		for (int i = 0; i < ui_label_count(); i++)
			if (cJSON_IsString(v) && !strcmp(ui_label_name(i), v->valuestring))
				same++;
		if (same > 1)
			bad_line("Another label has this name: a go-to would not know which");
	} else if (!strcmp(kind, "goto")) {
		cJSON *v = item(st, "goto");
		row_label("Go to", "A label of this file");
		igSetNextItemWidth(-1);
		igBeginDisabled(ro);
		if (igBeginCombo("##goto", cJSON_IsString(v) ? v->valuestring : "", ImGuiComboFlags_HeightLarge)) {
			for (int i = 0; i < ui_label_count(); i++) {
				const char *n = ui_label_name(i);
				if (igSelectable_Bool(n, cJSON_IsString(v) && !strcmp(n, v->valuestring), 0, v2(0, 0))) {
					put(st, "goto", cJSON_CreateString(n));
					picked();
				}
			}
			if (!ui_label_count())
				igTextColored(colv(C_FAINT), "No label in this file yet");
			igEndCombo();
		}
		igEndDisabled();
		bool found = false;
		for (int i = 0; i < ui_label_count(); i++)
			if (cJSON_IsString(v) && !strcmp(ui_label_name(i), v->valuestring))
				found = true;
		if (!found)
			bad_line("No label of this name in the file");
		else
			hint_line("Double-click the step in the list to see the label.");
	} else if (!strcmp(kind, "end")) {
		row_label("Value", "What the play call returns; a quest's run ends with clear, fail, abandon or escape");
		value_edit("end", st, "end", 0, igGetContentRegionAvail().x);
		igSetCursorPosX(label_w());
		igBeginDisabled(ro);
		for (int i = 0; i < 4; i++) {
			if (i)
				igSameLine(0, S(6));
			if (igSmallButton(end_values[i])) {
				put(st, "end", cJSON_CreateString(end_values[i]));
				picked();
			}
		}
		igEndDisabled();
		hint_line("For a quest's run: clear, fail, abandon or escape.");
	} else if (!strcmp(kind, "flag") || !strcmp(kind, "unflag")) {
		row_label("Flag", "An event flag's name; the game's own scene names work too");
		char hint[160];
		snprintf(hint, sizeof hint, "%s:a_name", ui_mod_id());
		text_field("##flag", st, kind, hint, false);
		hint_line(!strcmp(kind, "flag") ? "Sets the flag: a seen condition holds from now on, in this save."
		                                : "Clears the flag.");
	} else if (!strcmp(kind, "call")) {
		row_label("Function", "One of the mod's functions for scene files (rq.scene.functions)");
		name_field("##call", st, "call", "its name", false);
		args_field(st, "args", "The values the function gets, in order");
		row_label("Into", "A variable that keeps what the function returns");
		name_field("##into", st, "into", "none", true);
	} else if (!strcmp(kind, "cmd")) {
		row_label("Command", "Any of the game's scene commands by its name, as rq.scene.cmd");
		text_field("##cmd", st, "cmd", "the game's name", false);
		args_field(st, "args", "The values the command gets, in order");
	} else if (!strcmp(kind, "note")) {
		row_label("Note", "For the people who read the file; the game does nothing with it");
		igNewLine();
		multiline_field("##note", st, "note", S(110));
	}
	igPopStyleVar(1);
	igPopID();
}

/* ---- the cast ---- */

struct uses { const char *key; int n; char *first; size_t cap; const char *to; };

static void count_use(cJSON *st, const char *place, void *ud)
{
	struct uses *u = ud;
	const char *kind = step_kind(st);
	if (!kind || (strcmp(kind, "say") && strcmp(kind, "think")))
		return;
	cJSON *v = item(st, kind);
	if (!cJSON_IsString(v) || strcmp(v->valuestring, u->key))
		return;
	if (!u->n && u->first)
		snprintf(u->first, u->cap, "%s", place);
	u->n++;
	if (u->to)
		put(st, kind, cJSON_CreateString(u->to));
}

int cast_uses(const char *key, char *first, size_t n)
{
	struct uses u = { key, 0, first, n, NULL };
	if (first && n)
		first[0] = 0;
	steps_each(doc_steps(), "steps", count_use, &u);
	return u.n;
}

static char rename_buf[128], rename_for[128], rename_why[160];

const char *fields_cast(const char *key, bool readonly)
{
	static char out[128];
	snprintf(out, sizeof out, "%s", key);
	cJSON *cast = item(D.root, "cast");
	cJSON *e = item(cast, key);
	if (!e)
		return out;
	cur_place[0] = 0;
	ro = readonly;
	igPushID_Str(key);
	igPushStyleVar_Vec2(ImGuiStyleVar_ItemSpacing, v2(S(8), S(7)));

	/* Its name in the file: a rename changes every line that names it. */
	if (strcmp(rename_for, key)) {
		snprintf(rename_buf, sizeof rename_buf, "%s", key);
		snprintf(rename_for, sizeof rename_for, "%s", key);
		rename_why[0] = 0;
	}
	row_label("Name in the file", "The name the lines use for this face; renamed, every line follows");
	igSetNextItemWidth(-1);
	igPushFont(F_MONO, MONO_SIZE);
	igInputText("##key", rename_buf, sizeof rename_buf, ImGuiInputTextFlags_CharsNoBlank | (ro ? ImGuiInputTextFlags_ReadOnly : 0), NULL, NULL);
	igPopFont();
	if (igIsItemDeactivatedAfterEdit() && strcmp(rename_buf, key)) {
		if (!name_ok(rename_buf)) {
			snprintf(rename_why, sizeof rename_why, "Not renamed: a name is letters, digits and _, not starting with a digit");
		} else if (item(cast, rename_buf)) {
			snprintf(rename_why, sizeof rename_why, "Not renamed: the cast has a %s already", rename_buf);
		} else {
			struct uses u = { key, 0, NULL, 0, rename_buf };
			steps_each(doc_steps(), "steps", count_use, &u);
			if (!(e->type & cJSON_StringIsConst))
				cJSON_free(e->string);
			e->type &= ~cJSON_StringIsConst;
			e->string = cJSON_malloc(strlen(rename_buf) + 1);
			strcpy(e->string, rename_buf);
			doc_settle();
			doc_changed("");
			snprintf(out, sizeof out, "%s", rename_buf);
			snprintf(rename_for, sizeof rename_for, "%s", rename_buf);
			ui_status("Renamed %s to %s in the cast and in %d line%s", key, rename_buf, u.n, u.n == 1 ? "" : "s");
			rename_why[0] = 0;
		}
	}
	if (rename_why[0])
		bad_line(rename_why);

	row_label("Face", "A face CG of the game (the faces start at 2000), or a picture in the mod's folder");
	picture_field("##face", e, "face", false);
	cJSON *face = item(e, "face");
	const char *who = cJSON_IsNumber(face) ? media_face_name((int)face->valuedouble) : NULL;
	char hint[300];
	if (who)
		snprintf(hint, sizeof hint, "the game's name for this face (%s)", who);
	else if (cJSON_IsNumber(face))
		snprintf(hint, sizeof hint, "the game's name for this face");
	else
		snprintf(hint, sizeof hint, "none: a picture of the mod has no name of the game's");
	row_label("Name plate", "The name above the box for this face's lines; a line's own name wins");
	text_field("##name", e, "name", hint, true);
	igPopStyleVar(1);
	igPopID();
	return out;
}