/* The step kinds of a scene file (format 1): their tags and colours, a new step of each kind, and
 * what a step does in a sentence for the list. */
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "system4.h"

#include "editor.h"

/* @cast: the cast's first name, else Rance's face; @mod: the mod's id. */
const struct kind_info kinds[] = {
	{ "say", "SAY", TAG_LINE, "A line", "{ \"say\": \"@cast\", \"text\": \"\" }" },
	{ "think", "THINK", TAG_LINE, "A thought", "{ \"think\": \"@cast\", \"text\": \"\" }" },
	{ "narrate", "NARRATE", TAG_LINE, "Narration", "{ \"narrate\": \"\" }" },
	{ "clear_text", "CLEAR TEXT", TAG_LINE, "Clear the message box", "{ \"clear_text\": true }" },
	{ "speed", "SPEED", TAG_LINE, "Text speed", "{ \"speed\": 0 }" },
	{ "wait_click", "WAIT", TAG_LINE, "Wait for a click", "{ \"wait_click\": true }" },
	{ "background", "BACKGROUND", TAG_PIC, "A background", "{ \"background\": 5280 }" },
	{ "wallpaper", "WALLPAPER", TAG_PIC, "A wallpaper", "{ \"wallpaper\": 5510 }" },
	{ "mask_wallpaper", "MASK", TAG_PIC, "A wallpaper's mask", "{ \"mask_wallpaper\": 0 }" },
	{ "portrait", "PORTRAIT", TAG_PIC, "A standing picture", "{ \"portrait\": 0 }" },
	{ "mini", "MINI", TAG_PIC, "A small picture", "{ \"mini\": 0 }" },
	{ "cg", "CG", TAG_PIC, "A scene CG", "{ \"cg\": 0 }" },
	{ "background_in", "BG IN", TAG_PIC, "A background comes in", "{ \"background_in\": 5280 }" },
	{ "background_out", "BG OUT", TAG_PIC, "The background goes", "{ \"background_out\": true }" },
	{ "set", "SET", TAG_PIC, "Several pictures at once", "{ \"set\": { \"background\": 5280 } }" },
	{ "black", "BLACK", TAG_PIC, "The screen to black", "{ \"black\": true }" },
	{ "white", "WHITE", TAG_PIC, "The screen to white", "{ \"white\": true }" },
	{ "clear", "CLEAR", TAG_PIC, "Clear the screen", "{ \"clear\": true }" },
	{ "flash", "FLASH", TAG_PIC, "A flash", "{ \"flash\": true }" },
	{ "shake", "SHAKE", TAG_PIC, "Shake the screen", "{ \"shake\": true }" },
	{ "shake_face", "SHAKE", TAG_PIC, "Shake the face", "{ \"shake_face\": true }" },
	{ "shake_portrait", "SHAKE", TAG_PIC, "Shake the standing picture", "{ \"shake_portrait\": true }" },
	{ "world_map", "WORLD MAP", TAG_PIC, "The world map", "{ \"world_map\": 1 }" },
	{ "music", "MUSIC", TAG_SND, "Music", "{ \"music\": 93 }" },
	{ "sound", "SOUND", TAG_SND, "A sound", "{ \"sound\": 5012 }" },
	{ "jingle", "JINGLE", TAG_SND, "A jingle", "{ \"jingle\": 1008 }" },
	{ "choice", "CHOICE", TAG_FLOW, "A choice", "{ \"choice\": [\"Yes\", \"No\"], \"into\": \"pick\" }" },
	{ "if", "IF", TAG_FLOW, "If", "{ \"if\": { \"chance\": 50 }, \"then\": [], \"else\": [] }" },
	{ "label", "LABEL", TAG_FLOW, "A label", "{ \"label\": \"here\" }" },
	{ "goto", "GO TO", TAG_FLOW, "Go to a label", "{ \"goto\": \"here\" }" },
	{ "end", "END", TAG_FLOW, "End the scene", "{ \"end\": \"clear\" }" },
	{ "flag", "FLAG", TAG_DATA, "Set a flag", "{ \"flag\": \"@mod:seen\" }" },
	{ "unflag", "UNFLAG", TAG_DATA, "Clear a flag", "{ \"unflag\": \"@mod:seen\" }" },
	{ "call", "CALL", TAG_DATA, "Call the mod's function", "{ \"call\": \"name\" }" },
	{ "cmd", "CMD", TAG_DATA, "A game command", "{ \"cmd\": \"command\", \"args\": [] }" },
	{ "note", "NOTE", TAG_NOTE, "A note", "{ \"note\": \"\" }" },
};
const int nkinds = sizeof kinds / sizeof kinds[0];

const struct kind_info *kind_find(const char *key)
{
	for (int i = 0; i < nkinds; i++)
		if (!strcmp(kinds[i].key, key))
			return &kinds[i];
	return NULL;
}

int tag_colour(int group)
{
	switch (group) {
	case TAG_LINE: return C_T_LINE;
	case TAG_PIC: return C_T_PIC;
	case TAG_SND: return C_T_SND;
	case TAG_FLOW: return C_T_FLOW;
	case TAG_DATA: return C_T_DATA;
	case TAG_NOTE: return C_T_NOTE;
	}
	return C_BAD;
}

/* ---- a step in a sentence ---- */

struct out {
	char *p;
	size_t n, cap;
};

static void add(struct out *o, const char *fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	char tmp[1024];
	int k = vsnprintf(tmp, sizeof tmp, fmt, ap);
	va_end(ap);
	if (k < 0)
		return;
	size_t len = (size_t)k < sizeof tmp ? (size_t)k : sizeof tmp - 1;
	if (o->n + len + 1 > o->cap) {
		o->cap = (o->n + len + 1) * 2;
		o->p = xrealloc(o->p, o->cap);
	}
	memcpy(o->p + o->n, tmp, len);
	o->n += len;
	o->p[o->n] = 0;
}

/* A value as it reads in a sentence: a text as it is, anything else as JSON. */
static void add_value(struct out *o, const cJSON *v)
{
	if (cJSON_IsString(v)) {
		add(o, "%s", v->valuestring);
	} else {
		char *j = json_inline(v);
		add(o, "%s", j);
		free(j);
	}
}

/* A page with its line breaks shown as ↵. */
static void add_page(struct out *o, const char *s)
{
	for (const char *p = s; *p; p++) {
		if (*p == '\n')
			add(o, " \xE2\x86\xB5 ");
		else
			add(o, "%c", *p);
	}
}

static void add_text(struct out *o, const cJSON *text)
{
	if (cJSON_IsString(text)) {
		add_page(o, text->valuestring);
	} else if (cJSON_IsArray(text) && text->child) {
		int n = cJSON_GetArraySize(text);
		if (cJSON_IsString(text->child))
			add_page(o, text->child->valuestring);
		if (n > 1)
			add(o, "   (+%d page%s)", n - 1, n == 2 ? "" : "s");
	} else if (text) {
		add_value(o, text);
	}
}

static void add_face(struct out *o, const cJSON *face, const cJSON *cast)
{
	if (cJSON_IsNumber(face)) {
		add(o, "face %d", (int)face->valuedouble);
	} else if (cJSON_IsString(face)) {
		const cJSON *e = cJSON_GetObjectItemCaseSensitive(cast, face->valuestring);
		const cJSON *plate = cJSON_GetObjectItemCaseSensitive(e, "name");
		if (cJSON_IsString(plate))
			add(o, "%s", plate->valuestring);
		else
			add(o, "%s", face->valuestring);
	} else if (face) {
		add_value(o, face);
	}
}

static void add_picture_opts(struct out *o, const cJSON *st)
{
	const cJSON *effect = cJSON_GetObjectItemCaseSensitive(st, "effect");
	const cJSON *time = cJSON_GetObjectItemCaseSensitive(st, "time");
	if (cJSON_IsNumber(effect))
		add(o, " \xC2\xB7 effect %d", (int)effect->valuedouble);
	if (cJSON_IsNumber(time))
		add(o, " \xC2\xB7 %d ms", (int)time->valuedouble);
}

char *step_summary(const cJSON *st, const cJSON *cast)
{
	struct out o = { 0 };
	const char *kind = step_kind(st);
	const cJSON *v = kind ? cJSON_GetObjectItemCaseSensitive(st, kind) : NULL;
	if (!kind) {
		add_value(&o, st);
		return o.p ? o.p : xstrdup("");
	}
	if (!strcmp(kind, "say") || !strcmp(kind, "think")) {
		const cJSON *plate = cJSON_GetObjectItemCaseSensitive(st, "name");
		if (cJSON_IsString(plate))
			add(&o, "%s", plate->valuestring);
		else
			add_face(&o, v, cast);
		add(&o, ":  ");
		add_text(&o, cJSON_GetObjectItemCaseSensitive(st, "text"));
	} else if (!strcmp(kind, "narrate")) {
		add_text(&o, v);
	} else if (!strcmp(kind, "speed")) {
		add(&o, "Text speed: ");
		add_value(&o, v);
		add(&o, " ms a letter");
	} else if (!strcmp(kind, "background") || !strcmp(kind, "wallpaper") || !strcmp(kind, "mask_wallpaper")
	           || !strcmp(kind, "portrait") || !strcmp(kind, "mini") || !strcmp(kind, "cg")
	           || !strcmp(kind, "background_in")) {
		add(&o, "%s ", kind_find(kind)->title);
		add_value(&o, v);
		add_picture_opts(&o, st);
	} else if (!strcmp(kind, "set")) {
		if (cJSON_IsObject(v)) {
			for (const cJSON *c = v->child; c; c = c->next) {
				add(&o, "%s%s ", c == v->child ? "" : ", ", c->string);
				add_value(&o, c);
			}
		} else {
			add_value(&o, v);
		}
		add_picture_opts(&o, st);
	} else if (!strcmp(kind, "music") || !strcmp(kind, "sound") || !strcmp(kind, "jingle")) {
		add(&o, "%s ", kind_find(kind)->title);
		add_value(&o, v);
		add_picture_opts(&o, st);
	} else if (!strcmp(kind, "choice")) {
		const cJSON *into = cJSON_GetObjectItemCaseSensitive(st, "into");
		if (cJSON_IsArray(v)) {
			int n = cJSON_GetArraySize(v);
			add(&o, "A choice of %d row%s", n, n == 1 ? "" : "s");
		} else if (cJSON_IsObject(v) && cJSON_IsString(cJSON_GetObjectItemCaseSensitive(v, "call"))) {
			add(&o, "Rows from %s()", cJSON_GetObjectItemCaseSensitive(v, "call")->valuestring);
		} else {
			add_value(&o, v);
		}
		if (cJSON_IsString(into))
			add(&o, "  \xE2\x86\x92 %s", into->valuestring);
	} else if (!strcmp(kind, "if")) {
		char cw[300];
		cond_words(v, cw, sizeof cw);
		add(&o, "If %s", cw);
	} else if (!strcmp(kind, "label")) {
		add(&o, "\xE2\x97\x86 ");
		add_value(&o, v);
	} else if (!strcmp(kind, "goto")) {
		add(&o, "\xE2\x86\xAA ");
		add_value(&o, v);
	} else if (!strcmp(kind, "end")) {
		add(&o, "End the scene: ");
		add_value(&o, v);
	} else if (!strcmp(kind, "flag") || !strcmp(kind, "unflag")) {
		add(&o, "%s ", kind_find(kind)->title);
		add_value(&o, v);
	} else if (!strcmp(kind, "call")) {
		const cJSON *args = cJSON_GetObjectItemCaseSensitive(st, "args");
		const cJSON *into = cJSON_GetObjectItemCaseSensitive(st, "into");
		add_value(&o, v);
		if (cJSON_IsArray(args)) {
			char *j = json_inline(args);
			size_t len = strlen(j);
			if (len >= 2) {
				j[len - 1] = 0;
				add(&o, "(%s)", j + 1);
			}
			free(j);
		} else {
			add(&o, "()");
		}
		if (cJSON_IsString(into))
			add(&o, "  \xE2\x86\x92 %s", into->valuestring);
	} else if (!strcmp(kind, "cmd")) {
		const cJSON *args = cJSON_GetObjectItemCaseSensitive(st, "args");
		add_value(&o, v);
		if (args) {
			add(&o, " ");
			add_value(&o, args);
		}
	} else if (!strcmp(kind, "note")) {
		add_value(&o, v);
	} else if (!strcmp(kind, "world_map")) {
		int x, y;
		const char *name;
		const cJSON *n = cJSON_GetObjectItemCaseSensitive(v, "name");
		if (cJSON_IsNumber(v) && media_place((int)v->valuedouble, &x, &y, &name))
			add(&o, "The world map at %s (%d)", name, (int)v->valuedouble);
		else if (cJSON_IsObject(v) && cJSON_IsString(n)) {
			const cJSON *jx = cJSON_GetObjectItemCaseSensitive(v, "x"), *jy = cJSON_GetObjectItemCaseSensitive(v, "y");
			add(&o, "The world map at %s, %d, %d", n->valuestring, cJSON_IsNumber(jx) ? (int)jx->valuedouble : 0,
			    cJSON_IsNumber(jy) ? (int)jy->valuedouble : 0);
		} else {
			add(&o, "The world map at ");
			add_value(&o, v);
		}
	} else {
		add(&o, "%s", kind_find(kind)->title);
		const cJSON *mode = cJSON_GetObjectItemCaseSensitive(st, "mode");
		if (cJSON_IsNumber(mode))
			add(&o, " \xC2\xB7 mode %d", (int)mode->valuedouble);
		add_picture_opts(&o, st);
	}
	return o.p ? o.p : xstrdup("");
}

int count_lines(const cJSON *steps)
{
	int n = 0;
	if (!cJSON_IsArray(steps))
		return 0;
	for (const cJSON *st = steps->child; st; st = st->next) {
		const char *kind = step_kind(st);
		if (!kind)
			continue;
		if (!strcmp(kind, "say") || !strcmp(kind, "think") || !strcmp(kind, "narrate")) {
			n++;
		} else if (!strcmp(kind, "if")) {
			n += count_lines(cJSON_GetObjectItemCaseSensitive(st, "then"));
			n += count_lines(cJSON_GetObjectItemCaseSensitive(st, "else"));
		} else if (!strcmp(kind, "choice")) {
			const cJSON *rows = cJSON_GetObjectItemCaseSensitive(st, "choice");
			if (cJSON_IsArray(rows))
				for (const cJSON *r = rows->child; r; r = r->next)
					n += count_lines(cJSON_GetObjectItemCaseSensitive(r, "steps"));
		}
	}
	return n;
}
