/* --check: each scene file read as RQ MOD reads it, written in the editor's layout and read again,
 * so a file the editor saves holds the same values (the round trip) and a second save writes the
 * same text. Each step is also written alone (as the right side shows it) and read back. The
 * results go out as JSON.
 *
 * --format: files written in the editor's layout, as the window's Save writes them. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include "system4.h"
#include "system4/file.h"

#include "editor.h"

struct item {
	char mod[128];
	char rel[512];
	char path[1600];
};

static struct item *items;
static int nitems, capitems;

static void add_item(const char *mod, const char *rel, const char *path)
{
	if (nitems == capitems) {
		capitems = capitems ? capitems * 2 : 64;
		items = xrealloc(items, (size_t)capitems * sizeof *items);
	}
	struct item *it = &items[nitems++];
	snprintf(it->mod, sizeof it->mod, "%s", mod ? mod : "");
	snprintf(it->rel, sizeof it->rel, "%s", rel);
	snprintf(it->path, sizeof it->path, "%s", path);
}

static void walk(const char *root, const char *rel, int depth)
{
	char dir[1600];
	snprintf(dir, sizeof dir, "%s%s%s", root, rel[0] ? "/" : "", rel);
	UDIR *d = opendir_utf8(dir);
	if (!d)
		return;
	char *name;
	while ((name = readdir_utf8(d))) {
		if (name[0] != '.') {
			char sub[512], full[2200];
			snprintf(sub, sizeof sub, "%s%s%s", rel, rel[0] ? "/" : "", name);
			snprintf(full, sizeof full, "%s/%s", root, sub);
			size_t n = strlen(name);
			if (is_directory(full)) {
				if (depth < 8)
					walk(root, sub, depth + 1);
			} else if (n > 5 && !strcasecmp(name + n - 5, ".json")) {
				add_item(NULL, sub, full);
			}
		}
		free(name);
	}
	closedir_utf8(d);
}

static int item_cmp(const void *a, const void *b)
{
	const struct item *x = a, *y = b;
	int c = strcmp(x->mod, y->mod);
	return c ? c : strcmp(x->rel, y->rel);
}

/* Every step written alone and read back, the lists under it too. Returns the steps that failed. */
static int steps_alone(const cJSON *list, int *count)
{
	int bad = 0;
	for (const cJSON *st = cJSON_IsArray(list) ? list->child : NULL; st; st = st->next) {
		(*count)++;
		char *t = step_write(st);
		cJSON *back = NULL;
		char why[256];
		if (!scene_parse(t, &back, why, sizeof why) || !json_same(st, back))
			bad++;
		cJSON_Delete(back);
		free(t);
		const char *kind = step_kind(st);
		if (kind && !strcmp(kind, "if")) {
			bad += steps_alone(cJSON_GetObjectItemCaseSensitive(st, "then"), count);
			bad += steps_alone(cJSON_GetObjectItemCaseSensitive(st, "else"), count);
		} else if (kind && !strcmp(kind, "choice")) {
			const cJSON *rows = cJSON_GetObjectItemCaseSensitive(st, "choice");
			for (const cJSON *r = cJSON_IsArray(rows) ? rows->child : NULL; r; r = r->next)
				if (cJSON_IsObject(r))
					bad += steps_alone(cJSON_GetObjectItemCaseSensitive(r, "steps"), count);
		}
	}
	return bad;
}

int run_check(const char *dir, const char *game_dir, const char *mod, const char *out_path)
{
	if (dir) {
		walk(dir, "", 0);
	} else {
		mods_scan(game_dir, NULL);
		int found = 0;
		for (int i = 0; i < NMODS; i++) {
			if (mod && strcmp(MODS[i].id, mod))
				continue;
			found++;
			files_scan(&MODS[i]);
			for (int k = 0; k < NFILES; k++)
				add_item(MODS[i].id, FILES[k].rel, FILES[k].path);
		}
		if (mod && !found) {
			fprintf(stderr, APP_EXE ": no mod %s in %s/mods\n", mod, game_dir);
			return 1;
		}
	}
	if (nitems > 1)
		qsort(items, (size_t)nitems, sizeof *items, item_cmp);

	cJSON *out = cJSON_CreateObject();
	cJSON_AddStringToObject(out, "editor", EDITOR_VERSION);
	cJSON_AddNumberToObject(out, "format", SCENE_FORMAT);
	cJSON_AddStringToObject(out, "from", dir ? dir : game_dir);
	cJSON *files = cJSON_AddArrayToObject(out, "files");
	int nread = 0, nunread = 0, nfail = 0, nlayout = 0, nro = 0;
	for (int i = 0; i < nitems; i++) {
		struct item *it = &items[i];
		cJSON *f = cJSON_CreateObject();
		if (it->mod[0])
			cJSON_AddStringToObject(f, "mod", it->mod);
		cJSON_AddStringToObject(f, "file", it->rel);
		doc_open(it->path, it->rel);
		if (!D.root) {
			nunread++;
			cJSON_AddBoolToObject(f, "read", false);
			cJSON_AddStringToObject(f, "why", D.why);
		} else {
			nread++;
			cJSON_AddBoolToObject(f, "read", true);
			if (D.readonly) {
				nro++;
				cJSON_AddStringToObject(f, "read_only", D.why);
			}
			char *t1 = scene_write(D.root);
			cJSON *again = NULL;
			char why[256] = "";
			bool parsed = scene_parse(t1, &again, why, sizeof why);
			bool same = parsed && json_same(D.root, again);
			char *t2 = parsed ? scene_write(again) : NULL;
			bool stable = t2 && !strcmp(t1, t2);
			int count = 0, bad = steps_alone(doc_steps(), &count);
			bool ok = parsed && same && stable && !bad;
			if (!ok)
				nfail++;
			if (D.styled)
				nlayout++;
			cJSON_AddBoolToObject(f, "round_trip", ok);
			if (!parsed)
				cJSON_AddStringToObject(f, "written_not_read", why);
			else if (!same)
				cJSON_AddStringToObject(f, "problem", "the values read back differ");
			else if (!stable)
				cJSON_AddStringToObject(f, "problem", "a second save writes other text");
			else if (bad)
				cJSON_AddNumberToObject(f, "steps_alone_failed", bad);
			cJSON_AddBoolToObject(f, "in_layout", D.styled);
			cJSON_AddNumberToObject(f, "steps", count);
			cJSON_AddNumberToObject(f, "lines", count_lines(doc_steps()));
			cJSON_Delete(again);
			free(t1);
			free(t2);
		}
		cJSON_AddItemToArray(files, f);
	}
	doc_close();
	cJSON_AddNumberToObject(out, "read", nread);
	cJSON_AddNumberToObject(out, "unreadable", nunread);
	cJSON_AddNumberToObject(out, "read_only", nro);
	cJSON_AddNumberToObject(out, "round_trip_failed", nfail);
	cJSON_AddNumberToObject(out, "in_layout", nlayout);
	char *text = cJSON_Print(out);
	cJSON_Delete(out);
	if (out_path) {
		if (!file_write(out_path, (uint8_t *)text, strlen(text))) {
			fprintf(stderr, APP_EXE ": could not write %s\n", out_path);
			free(text);
			return 1;
		}
	} else {
		printf("%s\n", text);
	}
	free(text);
	fprintf(stderr, "%d file%s: %d read (%d read-only, %d already in the layout), %d unreadable, %d round trip%s failed\n",
	        nitems, nitems == 1 ? "" : "s", nread, nro, nlayout, nunread, nfail, nfail == 1 ? "" : "s");
	fflush(stdout);
	fflush(stderr);
	return nfail ? 1 : 0;
}

/* ---- --selftest ---- */

static int st_fail, st_count;

static void expect(bool ok, const char *what)
{
	st_count++;
	printf("%s %s\n", ok ? "ok  " : "FAIL", what);
	if (!ok)
		st_fail++;
}

static char *read_all(const char *path)
{
	size_t len = 0;
	char *raw = file_read(path, &len);
	if (!raw)
		return NULL;
	char *t = xmalloc(len + 1);
	memcpy(t, raw, len);
	t[len] = 0;
	free(raw);
	return t;
}

static bool write_text(const char *path, const char *text)
{
	return file_write(path, (uint8_t *)text, strlen(text));
}

static int nsteps(void)
{
	return doc_steps() ? cJSON_GetArraySize(doc_steps()) : -1;
}

int run_selftest(const char *path)
{
	char place[256], why[300];
	st_fail = st_count = 0;

	doc_open(path, "selftest.json");
	expect(D.root && doc_steps() && !D.readonly, "the file reads as a scene the editor writes");
	if (!D.root || !doc_steps() || D.readonly) {
		printf("%s: %s\n", path, D.why);
		return 1;
	}
	expect(!doc_dirty() && !doc_can_undo() && !doc_can_redo(), "just opened: nothing to save, undo or redo");
	char *orig = xstrdup(D.snaps[D.at].text);
	int n0 = nsteps();
	expect(n0 >= 2, "two steps or more to work on");

	cJSON_DeleteItemFromArray(doc_steps(), 0);
	doc_changed("steps[0]");
	expect(doc_dirty() && doc_can_undo() && !doc_can_redo() && nsteps() == n0 - 1,
	       "a step removed: unsaved, a step to undo");

	doc_undo(place, sizeof place);
	expect(!doc_dirty() && !strcmp(D.snaps[D.at].text, orig) && nsteps() == n0 && !strcmp(place, "steps[0]"),
	       "undo: the file as it was, the change's place to select");
	doc_redo(place, sizeof place);
	expect(doc_dirty() && nsteps() == n0 - 1 && !strcmp(place, "steps[0]"), "redo: the step removed again");

	int at = D.at;
	doc_changed("steps[0]");
	expect(D.at == at, "a change that changes nothing is no step to undo");

	bool saved = doc_save(why, sizeof why);
	expect(saved && !doc_dirty() && D.styled, "saved: nothing left to save, the file in the layout");
	char *disk = read_all(path);
	expect(disk && !strcmp(disk, D.snaps[D.at].text), "on disk: the text the editor wrote");
	free(disk);
	doc_undo(place, sizeof place);
	expect(doc_dirty(), "undo after saving: unsaved again");
	doc_redo(place, sizeof place);
	expect(!doc_dirty(), "redo back to what was saved: nothing to save");

	cJSON *mem = cJSON_Duplicate(D.root, true);
	doc_reload();
	expect(json_same(mem, D.root) && D.styled && !doc_dirty(), "read again: the same values, in the layout");
	cJSON_Delete(mem);

	/* Typing in a field: the file follows each letter (as fields.c does it), and the field left
	 * makes one step to undo for the whole word. */
	int at0 = D.at;
	char *before = xstrdup(D.snaps[D.at].text);
	const char *word[] = { "a", "ab", "abc" };
	for (int i = 0; i < 3; i++) {
		cJSON *typed = cJSON_CreateObject();
		cJSON_AddStringToObject(typed, "note", word[i]);
		cJSON_ReplaceItemInArray(doc_steps(), 0, typed);
		D.live = true;
		snprintf(D.live_place, sizeof D.live_place, "steps[0]");
	}
	expect(doc_dirty() && D.at == at0 && doc_can_undo(), "typing: unsaved, no step to undo yet, Undo ready");
	doc_settle();
	expect(!D.live && D.at == at0 + 1 && strstr(D.snaps[D.at].text, "\"abc\""), "the field left: one step for the word");
	doc_undo(place, sizeof place);
	expect(!strcmp(D.snaps[D.at].text, before) && !strcmp(place, "steps[0]"), "one undo takes the whole word back");
	doc_redo(place, sizeof place);
	cJSON_ReplaceItemInArray(doc_steps(), 0, cJSON_Parse("{ \"note\": \"abcd\" }"));
	D.live = true;
	doc_undo(place, sizeof place);
	expect(!D.live && D.at == at0 + 1 && strstr(D.snaps[D.at].text, "\"abc\"") && doc_can_redo(),
	       "Undo while typing: the typing kept as a step, then taken back");
	doc_redo(place, sizeof place);
	cJSON_ReplaceItemInArray(doc_steps(), 0, cJSON_Parse("{ \"note\": \"abcde\" }"));
	D.live = true;
	saved = doc_save(why, sizeof why);
	disk = read_all(path);
	expect(saved && !D.live && !doc_dirty() && disk && strstr(disk, "\"abcde\""), "Save while typing: what was typed is saved");
	free(disk);
	free(before);

	/* Changed on disk with nothing unsaved: read again. */
	cJSON *other = cJSON_Duplicate(D.root, true);
	cJSON *note = cJSON_CreateObject();
	cJSON_AddStringToObject(note, "note", "written from outside");
	cJSON_AddItemToArray(cJSON_GetObjectItemCaseSensitive(other, "steps"), note);
	char *t = scene_write(other);
	cJSON_Delete(other);
	write_text(path, t);
	free(t);
	int w = doc_watch();
	const cJSON *last = doc_steps() ? cJSON_GetArrayItem(doc_steps(), nsteps() - 1) : NULL;
	expect(w == 1 && last && cJSON_GetObjectItemCaseSensitive(last, "note") && !doc_dirty(),
	       "changed on disk, nothing unsaved: read again");
	expect(doc_watch() == 0, "the same file on disk: nothing to do");

	/* Changed on disk with unsaved changes: asked, once. */
	cJSON_DeleteItemFromArray(doc_steps(), 0);
	doc_changed("steps[0]");
	disk = read_all(path);
	char *more = xmalloc(strlen(disk) + 2);
	snprintf(more, strlen(disk) + 2, "%s\n", disk);
	write_text(path, more);
	free(more);
	free(disk);
	w = doc_watch();
	expect(w == 2 && D.changed_outside && doc_dirty(), "changed on disk with unsaved changes: the editor asks");
	expect(doc_watch() == 0, "asked once");
	saved = doc_save(why, sizeof why);
	expect(saved && !D.changed_outside && !doc_dirty(), "Keep mine, then Save: the editor's text on disk");

	/* The undo depth. */
	doc_reload();
	cJSON *first = cJSON_GetArrayItem(doc_steps(), 0);
	for (int i = 0; i < UNDO_DEPTH + 20; i++) {
		char n[32];
		snprintf(n, sizeof n, "change %d", i);
		cJSON_ReplaceItemInArray(doc_steps(), 0, cJSON_CreateObject());
		first = cJSON_GetArrayItem(doc_steps(), 0);
		cJSON_AddStringToObject(first, "note", n);
		doc_changed("steps[0]");
	}
	int undos = 0;
	while (doc_undo(place, sizeof place))
		undos++;
	expect(D.nsnaps == UNDO_DEPTH && undos == UNDO_DEPTH - 1, "a few hundred steps to undo, the oldest dropped");
	expect(doc_dirty(), "the saved state dropped from the undo: still unsaved");

	/* A newer format: read-only. */
	write_text(path, "{ \"format\": 2, \"steps\": [] }\n");
	doc_open(path, "selftest.json");
	expect(D.root && D.readonly && !doc_save(why, sizeof why), "a newer format: read, not written");

	/* Not JSON: the loader's own message. */
	write_text(path, "{ \"format\": 1,\n  \"steps\": [ }\n");
	doc_open(path, "selftest.json");
	expect(!D.root && !strcmp(D.why, "not valid JSON at line 2, column 14"), D.why[0] ? D.why : "not JSON: why");

	/* A field twice. */
	write_text(path, "{ \"format\": 1, \"steps\": [], \"steps\": [] }\n");
	doc_open(path, "selftest.json");
	expect(!D.root && !strcmp(D.why, "the field \"steps\" twice in one object"), "a field twice: refused as the loader does");

	/* The file back as it was opened, in the editor's layout (FILE is a copy). */
	write_text(path, orig);
	free(orig);
	doc_close();
	printf("%d/%d\n", st_count - st_fail, st_count);
	fflush(stdout);
	return st_fail ? 1 : 0;
}

int run_format(int argc, char **argv, int first)
{
	int code = 0;
	for (int i = first; i < argc; i++) {
		const char *path = argv[i];
		doc_open(path, path);
		char why[300];
		if (!D.root || D.readonly) {
			fprintf(stderr, "%s: not written: %s\n", path, D.why);
			code = 1;
		} else if (D.styled) {
			printf("%s: already in the layout\n", path);
		} else if (!doc_save(why, sizeof why)) {
			fprintf(stderr, "%s: %s\n", path, why);
			code = 1;
		} else {
			printf("%s: written in the layout\n", path);
		}
	}
	doc_close();
	fflush(stdout);
	fflush(stderr);
	return code;
}
