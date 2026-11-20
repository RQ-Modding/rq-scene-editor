/* The open scene file: read, saved, its steps to undo and redo, and the file on disk watched.
 *
 * Each change keeps the whole file as the editor writes it (a few hundred deep), so undo is a parse
 * of an older text. Saving writes a file beside it and moves it over the old one. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "system4.h"
#include "system4/file.h"

#include "editor.h"

struct doc D = { .saved = -1 };

static void snaps_free(void)
{
	for (int i = 0; i < D.nsnaps; i++)
		free(D.snaps[i].text);
	free(D.snaps);
	D.snaps = NULL;
	D.nsnaps = 0;
	D.at = -1;
	D.saved = -1;
}

static void snap_push(const char *text, const char *place)
{
	for (int i = D.at + 1; i < D.nsnaps; i++)
		free(D.snaps[i].text);
	if (D.saved > D.at)
		D.saved = -1;
	D.nsnaps = D.at + 1;
	if (D.nsnaps >= UNDO_DEPTH) {
		free(D.snaps[0].text);
		memmove(D.snaps, D.snaps + 1, (size_t)(D.nsnaps - 1) * sizeof *D.snaps);
		D.nsnaps--;
		D.at--;
		if (D.saved >= 0)
			D.saved--;
	}
	D.snaps = xrealloc(D.snaps, (size_t)(D.nsnaps + 1) * sizeof *D.snaps);
	D.snaps[D.nsnaps].text = xstrdup(text);
	snprintf(D.snaps[D.nsnaps].place, sizeof D.snaps[D.nsnaps].place, "%s", place ? place : "");
	D.at = D.nsnaps++;
}

void doc_close(void)
{
	cJSON_Delete(D.root);
	snaps_free();
	memset(&D, 0, sizeof D);
	D.at = -1;
	D.saved = -1;
}

bool doc_open(const char *path, const char *rel)
{
	doc_close();
	D.open = true;
	snprintf(D.path, sizeof D.path, "%s", path);
	snprintf(D.rel, sizeof D.rel, "%s", rel);
	if (!file_stat(path, &D.mtime, &D.size))
		D.gone = true;

	char *raw = NULL, why[512];
	size_t len = 0;
	if (!scene_read(path, &D.root, &raw, &len, why, sizeof why)) {
		snprintf(D.why, sizeof D.why, "%s", why);
		D.readonly = true;
		free(raw);
		return false;
	}
	const cJSON *steps = cJSON_GetObjectItemCaseSensitive(D.root, "steps");
	const cJSON *fmt = cJSON_GetObjectItemCaseSensitive(D.root, "format");
	if (!cJSON_IsObject(D.root)) {
		snprintf(D.why, sizeof D.why, "expected an object with \"format\" and \"steps\"");
		D.readonly = true;
	} else if (!steps) {
		snprintf(D.why, sizeof D.why, "needs \"steps\": the editor opens a file with a list of steps");
		D.readonly = true;
	} else if (!cJSON_IsArray(steps)) {
		snprintf(D.why, sizeof D.why, "steps: expected a list of steps");
		D.readonly = true;
	} else if (cJSON_IsNumber(fmt) && fmt->valuedouble > SCENE_FORMAT) {
		snprintf(D.why, sizeof D.why, "format %g is newer than this editor writes (%d): the file opens read-only. "
		         "A newer RQ Scene Editor writes it.", fmt->valuedouble, SCENE_FORMAT);
		D.readonly = true;
	}
	char *text = scene_write(D.root);
	D.styled = len == strlen(text) && !memcmp(raw, text, len);
	snap_push(text, "");
	D.saved = D.at;
	free(text);
	free(raw);
	return true;
}

bool doc_dirty(void)
{
	return D.open && D.root && D.at >= 0 && (D.saved != D.at || D.live);
}

bool doc_editable(void)
{
	return D.open && D.root && !D.readonly;
}

cJSON *doc_steps(void)
{
	cJSON *s = D.root ? cJSON_GetObjectItemCaseSensitive(D.root, "steps") : NULL;
	return cJSON_IsArray(s) ? s : NULL;
}

bool doc_save(char *why, size_t n)
{
	doc_settle();
	if (!doc_editable() || D.at < 0) {
		snprintf(why, n, "%s", D.readonly ? "the file is read-only here" : "no file open");
		return false;
	}
	const char *text = D.snaps[D.at].text;
	char dir[1600];
	snprintf(dir, sizeof dir, "%s", D.path);
	char *a = strrchr(dir, '/'), *b = strrchr(dir, '\\');
	char *slash = a > b ? a : b;
	if (slash) {
		*slash = 0;
		mkdir_p(dir);
	}
	char tmp[1700];
	snprintf(tmp, sizeof tmp, "%s.rqse-new", D.path);
	FILE *f = file_open_utf8(tmp, "wb");
	if (!f) {
		snprintf(why, n, "could not write %s", tmp);
		return false;
	}
	size_t len = strlen(text);
	bool ok = fwrite(text, 1, len, f) == len;
	ok = fclose(f) == 0 && ok;
	if (!ok || !win_replace(tmp, D.path)) {
		remove_utf8(tmp);
		snprintf(why, n, "could not write %s", D.path);
		return false;
	}
	D.saved = D.at;
	D.styled = true;
	D.gone = false;
	D.changed_outside = false;
	file_stat(D.path, &D.mtime, &D.size);
	return true;
}

void doc_settle(void)
{
	if (!D.live)
		return;
	D.live = false;
	doc_changed(D.live_place);
}

void doc_changed(const char *place)
{
	D.live = false;
	if (!D.root)
		return;
	char *text = scene_write(D.root);
	if (D.at >= 0 && !strcmp(text, D.snaps[D.at].text)) {
		free(text);
		return;
	}
	snap_push(text, place);
	free(text);
}

static void reparse(void)
{
	char why[256];
	cJSON_Delete(D.root);
	D.root = NULL;
	scene_parse(D.snaps[D.at].text, &D.root, why, sizeof why);
}

bool doc_can_undo(void)
{
	return doc_editable() && (D.at > 0 || D.live);
}

bool doc_can_redo(void)
{
	return doc_editable() && D.at + 1 < D.nsnaps;
}

bool doc_undo(char *place, size_t n)
{
	doc_settle();
	if (!doc_can_undo())
		return false;
	char where[256];
	snprintf(where, sizeof where, "%s", D.snaps[D.at].place);
	D.at--;
	reparse();
	if (!where[0] || !place_exists(D.root, where))
		snprintf(where, sizeof where, "%s", D.snaps[D.at].place);
	snprintf(place, n, "%s", where);
	return true;
}

bool doc_redo(char *place, size_t n)
{
	doc_settle();
	if (!doc_can_redo())
		return false;
	D.at++;
	reparse();
	snprintf(place, n, "%s", D.snaps[D.at].place);
	return true;
}

bool doc_reload(void)
{
	char path[1600], rel[512];
	snprintf(path, sizeof path, "%s", D.path);
	snprintf(rel, sizeof rel, "%s", D.rel);
	return doc_open(path, rel);
}

int doc_watch(void)
{
	if (!D.open)
		return 0;
	long long m, s;
	if (!file_stat(D.path, &m, &s)) {
		D.gone = true;
		return 0;
	}
	bool back = D.gone;
	D.gone = false;
	if (!back && m == D.mtime && s == D.size)
		return 0;
	if (!doc_dirty()) {
		doc_reload();
		return 1;
	}
	D.mtime = m;
	D.size = s;
	D.changed_outside = true;
	return 2;
}

/* ---- places ---- */

static bool take(const char **p, const char *word, int *num)
{
	size_t n = strlen(word);
	if (strncmp(*p, word, n))
		return false;
	const char *q = *p + n;
	if (*q < '0' || *q > '9')
		return false;
	long v = 0;
	while (*q >= '0' && *q <= '9') {
		v = v * 10 + (*q - '0');
		if (v > 1000000)
			return false;
		q++;
	}
	if (*q != ']')
		return false;
	*num = (int)v;
	*p = q + 1;
	return true;
}

cJSON *place_find(const cJSON *root, const char *place, cJSON **list, int *index)
{
	if (!root || !place)
		return NULL;
	const char *p = place;
	int k;
	if (!take(&p, "steps[", &k))
		return NULL;
	cJSON *l = cJSON_GetObjectItemCaseSensitive(root, "steps");
	if (!cJSON_IsArray(l))
		return NULL;
	cJSON *item = cJSON_GetArrayItem(l, k);
	while (item && *p) {
		int r;
		cJSON *next = NULL;
		if (take(&p, ".then[", &k)) {
			next = cJSON_GetObjectItemCaseSensitive(item, "then");
		} else if (take(&p, ".else[", &k)) {
			next = cJSON_GetObjectItemCaseSensitive(item, "else");
		} else if (take(&p, ".choice[", &r) && take(&p, ".steps[", &k)) {
			cJSON *rows = cJSON_GetObjectItemCaseSensitive(item, "choice");
			cJSON *row = cJSON_IsArray(rows) ? cJSON_GetArrayItem(rows, r) : NULL;
			next = cJSON_IsObject(row) ? cJSON_GetObjectItemCaseSensitive(row, "steps") : NULL;
		} else {
			return NULL;
		}
		if (!cJSON_IsArray(next) || !cJSON_IsObject(item))
			return NULL;
		l = next;
		item = cJSON_GetArrayItem(l, k);
	}
	if (!item)
		return NULL;
	if (list)
		*list = l;
	if (index)
		*index = k;
	return item;
}

cJSON *row_find(const cJSON *root, const char *place, cJSON **rows, int *index)
{
	if (!root || !place)
		return NULL;
	/* The last ".choice[N]", with nothing after it. */
	const char *c = NULL, *p = place, *q;
	while ((q = strstr(p, ".choice["))) {
		c = q;
		p = q + 1;
	}
	if (!c)
		return NULL;
	const char *r = c + 8;
	int k;
	if (!take(&r, "", &k) || *r)
		return NULL;
	char head[256];
	snprintf(head, sizeof head, "%.*s", (int)(c - place), place);
	cJSON *st = place_find(root, head, NULL, NULL);
	cJSON *l = cJSON_IsObject(st) ? cJSON_GetObjectItemCaseSensitive(st, "choice") : NULL;
	cJSON *row = cJSON_IsArray(l) ? cJSON_GetArrayItem(l, k) : NULL;
	if (!row)
		return NULL;
	if (rows)
		*rows = l;
	if (index)
		*index = k;
	return row;
}

bool place_exists(const cJSON *root, const char *place)
{
	return place_find(root, place, NULL, NULL) || row_find(root, place, NULL, NULL);
}

bool row_step_place(const char *place, char *out, size_t n)
{
	const char *c = NULL, *p = place, *q;
	while ((q = strstr(p, ".choice["))) {
		c = q;
		p = q + 1;
	}
	if (!c)
		return false;
	snprintf(out, n, "%.*s", (int)(c - place), place);
	return true;
}

cJSON *row_object(cJSON *root, const char *place, bool make)
{
	cJSON *rows;
	int i;
	cJSON *row = row_find(root, place, &rows, &i);
	if (cJSON_IsObject(row))
		return row;
	if (!make || !cJSON_IsString(row) || !strcmp(row->valuestring, "-"))
		return NULL;
	cJSON *o = cJSON_CreateObject();
	cJSON_AddStringToObject(o, "text", row->valuestring);
	cJSON_ReplaceItemInArray(rows, i, o);
	return o;
}

void place_join(char *out, size_t n, const char *list_place, int index)
{
	snprintf(out, n, "%s[%d]", list_place, index);
}

bool place_list(const char *place, char *list_place, size_t n, int *index)
{
	const char *b = strrchr(place, '[');
	if (!b || b == place)
		return false;
	snprintf(list_place, n, "%.*s", (int)(b - place), place);
	*index = atoi(b + 1);
	return true;
}

void steps_each(cJSON *list, const char *lp, void (*fn)(cJSON *st, const char *place, void *ud), void *ud)
{
	int k = 0;
	for (cJSON *st = cJSON_IsArray(list) ? list->child : NULL; st; st = st->next, k++) {
		char place[256], sub[256];
		place_join(place, sizeof place, lp, k);
		fn(st, place, ud);
		const char *kind = step_kind(st);
		if (!kind)
			continue;
		if (!strcmp(kind, "if")) {
			snprintf(sub, sizeof sub, "%s.then", place);
			steps_each(cJSON_GetObjectItemCaseSensitive(st, "then"), sub, fn, ud);
			snprintf(sub, sizeof sub, "%s.else", place);
			steps_each(cJSON_GetObjectItemCaseSensitive(st, "else"), sub, fn, ud);
		} else if (!strcmp(kind, "choice")) {
			cJSON *rows = cJSON_GetObjectItemCaseSensitive(st, "choice");
			int r = 0;
			for (cJSON *row = cJSON_IsArray(rows) ? rows->child : NULL; row; row = row->next, r++) {
				snprintf(sub, sizeof sub, "%s.choice[%d].steps", place, r);
				steps_each(cJSON_IsObject(row) ? cJSON_GetObjectItemCaseSensitive(row, "steps") : NULL, sub, fn, ud);
			}
		}
	}
}