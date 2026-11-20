/* --check: each scene file read as RQ MOD reads it, written in the editor's layout and read again,
 * so a file the editor saves holds the same values (the round trip) and a second save writes the
 * same text. Each step is also written alone (as the right side shows it) and read back. Each file
 * also goes through RQ MOD's own checker, as the game checks it before it plays it (with the game's
 * names when the game is open), and the editor's warnings that need no font. The results go out
 * as JSON.
 *
 * --format: files written in the editor's layout, as the window's Save writes them. */
#include <stdio.h>
#include <stdlib.h>
#include <wchar.h>
#include <string.h>
#include <strings.h>

#include "system4.h"
#include "system4/file.h"
#include "system4/utfsjis.h"

#include "editor.h"

struct item {
	char mod[128];
	char rel[512];
	char path[1600];
	char mod_dir[1600];   /* the mod's folder, for its pictures; "" when none was found */
	char in_mod[512];     /* the file's path from it, as the game's log names the file */
};

static struct item *items;
static int nitems, capitems;

/* The nearest folder above a file that holds a mod.json: the mod the file belongs to, and the file's
 * path from it with / (as the game's log names the file). */
static bool mod_dir_above(const char *path, char *out, size_t n, char *rest, size_t rn)
{
	char full[1600], d[1600];
	/* The wide call: the narrow one answers in the ANSI code page, not UTF-8. */
	wchar_t *w = utf8_to_wchar(path), wfull[1600];
	char *u = w && _wfullpath(wfull, w, 1600) ? wchar_to_utf8(wfull) : NULL;
	snprintf(full, sizeof full, "%s", u ? u : path);
	free(u);
	free(w);
	snprintf(d, sizeof d, "%s", full);
	for (int up = 0; up < 12; up++) {
		char *x = strrchr(d, '/'), *y = strrchr(d, '\\');
		char *cut = x > y ? x : y;
		if (!cut)
			return false;
		*cut = 0;
		char m[1700];
		snprintf(m, sizeof m, "%s/mod.json", d);
		if (d[0] && file_exists(m)) {
			snprintf(out, n, "%s", d);
			snprintf(rest, rn, "%s", full + strlen(d) + 1);
			for (char *c = rest; *c; c++)
				if (*c == '\\')
					*c = '/';
			return true;
		}
	}
	return false;
}

static void add_item(const char *mod, const char *rel, const char *path, const char *mod_dir)
{
	if (nitems == capitems) {
		capitems = capitems ? capitems * 2 : 64;
		items = xrealloc(items, (size_t)capitems * sizeof *items);
	}
	struct item *it = &items[nitems++];
	snprintf(it->mod, sizeof it->mod, "%s", mod ? mod : "");
	snprintf(it->rel, sizeof it->rel, "%s", rel);
	snprintf(it->path, sizeof it->path, "%s", path);
	it->mod_dir[0] = it->in_mod[0] = 0;
	if (mod_dir) {
		snprintf(it->mod_dir, sizeof it->mod_dir, "%s", mod_dir);
		snprintf(it->in_mod, sizeof it->in_mod, "%s", rel);
	} else if (!mod_dir_above(path, it->mod_dir, sizeof it->mod_dir, it->in_mod, sizeof it->in_mod)) {
		it->mod_dir[0] = it->in_mod[0] = 0;
	}
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
				add_item(NULL, sub, full, NULL);
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

int run_check(const char *dir, const char *game_dir, const char *mod, const char *out_path, bool strict)
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
				add_item(MODS[i].id, FILES[k].rel, FILES[k].path, MODS[i].dir);
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
	cJSON_AddBoolToObject(out, "game_names", G.nkinds > 0);
	cJSON *files = cJSON_AddArrayToObject(out, "files");
	int nread = 0, nunread = 0, nfail = 0, nlayout = 0, nro = 0, nprob = 0, nwarn = 0;
	for (int i = 0; i < nitems; i++) {
		struct item *it = &items[i];
		cJSON *f = cJSON_CreateObject();
		if (it->mod[0])
			cJSON_AddStringToObject(f, "mod", it->mod);
		cJSON_AddStringToObject(f, "file", it->rel);
		if (it->in_mod[0])
			cJSON_AddStringToObject(f, "in_mod", it->in_mod);
		doc_open(it->path, it->rel);
		if (!D.root) {
			nunread++;
			nprob++;
			cJSON_AddBoolToObject(f, "read", false);
			cJSON_AddStringToObject(f, "why", D.why);
		} else {
			/* RQ MOD's problems, as the game writes them after the file's path, and the warnings. */
			struct problems ps = { 0 };
			problems_check(D.root, it->mod_dir, &ps);
			cJSON *pa = cJSON_AddArrayToObject(f, "problems");
			cJSON *wa = cJSON_AddArrayToObject(f, "warnings");
			for (int k = 0; k < ps.n; k++) {
				char line[900];
				if (ps.p[k].at[0])
					snprintf(line, sizeof line, "%s: %s", ps.p[k].at, ps.p[k].msg);
				else
					snprintf(line, sizeof line, "%s", ps.p[k].msg);
				cJSON_AddItemToArray(ps.p[k].level == PROB_ERROR ? pa : wa, cJSON_CreateString(line));
			}
			if (ps.errors)
				nprob++;
			if (ps.warnings)
				nwarn++;
			problems_free(&ps);
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
	cJSON_AddNumberToObject(out, "with_problems", nprob);
	cJSON_AddNumberToObject(out, "with_warnings", nwarn);
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
	fprintf(stderr, "%d file%s: %d read (%d read-only, %d already in the layout), %d unreadable, %d round trip%s failed; "
	        "%d that RQ MOD does not play, %d with warnings%s\n",
	        nitems, nitems == 1 ? "" : "s", nread, nro, nlayout, nunread, nfail, nfail == 1 ? "" : "s", nprob, nwarn,
	        G.nkinds > 0 ? "" : " (no game: the game's names not checked)");
	fflush(stdout);
	fflush(stderr);
	/* --strict: a file RQ MOD would not play (a problem, or one it cannot read) fails too. */
	return nfail || (strict && nprob) ? 1 : 0;
}

/* ---- --selftest ---- */

static int st_fail, st_count;

static void expect(bool ok, const char *what)
{
	st_count++;
	printf("%s %s\n", ok ? "ok  " : "FAIL", what);
	fflush(stdout);   /* the lines kept if a later test stops the run */
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

/* owner[key] is the JSON want (NULL: not there). */
static bool cond_is(cJSON *owner, const char *key, const char *want)
{
	cJSON *w = want ? cJSON_Parse(want) : NULL;
	cJSON *v = cJSON_GetObjectItemCaseSensitive(owner, key);
	bool ok = want ? v && w && json_same(v, w) : !v;
	if (!ok) {
		char *j = v ? json_inline(v) : NULL;
		printf("     got %s\n", j ? j : "nothing");
		free(j);
	}
	cJSON_Delete(w);
	return ok;
}

/* The condition builder's changes (cond.c), on a step and a row of their own. */
static void cond_tests(void)
{
	char why[200], words[300];
	cJSON *st = cJSON_Parse("{ \"if\": { \"gold\": 100 }, \"then\": [] }");
	cond_apply(st, "if", true, "", COND_ADD, NULL, why, sizeof why);
	expect(cond_is(st, "if", "{ \"all\": [ { \"gold\": 100 }, { \"gold\": 1000 } ] }"),
	       "cond: + Check on one check: both in All of, the new one of the same kind");
	cond_apply(st, "if", true, "1", COND_NOT, NULL, why, sizeof why);
	cond_apply(st, "if", true, "1", COND_KIND, "seen", why, sizeof why);
	expect(cond_is(st, "if", "{ \"all\": [ { \"gold\": 100 }, { \"not\": { \"seen\": \"\" } } ] }"),
	       "cond: Not, then another kind: the Not stays");
	cond_do(st, "if", true, "value:1:{ \"not\": { \"seen\": \"lab:met\" } }", why, sizeof why);
	cond_do(st, "if", true, "mode::none", why, sizeof why);
	expect(cond_is(st, "if", "{ \"not\": { \"any\": [ { \"gold\": 100 }, { \"not\": { \"seen\": \"lab:met\" } } ] } }"),
	       "cond: a check as JSON, and None of as not any");
	cond_words(cJSON_GetObjectItemCaseSensitive(st, "if"), words, sizeof words);
	expect(!strcmp(words, "not (gold 100 or not flag lab:met)"), words);
	cond_apply(st, "if", true, "", COND_GROUP, NULL, why, sizeof why);
	expect(cond_is(st, "if", "{ \"not\": { \"any\": [ { \"gold\": 100 }, { \"not\": { \"seen\": \"lab:met\" } }, "
	                         "{ \"all\": [ { \"seen\": \"\" } ] } ] } }"),
	       "cond: + Group in an Any of: an All of, its check of the last check's kind");
	cond_apply(st, "if", true, "2.0", COND_DEL, NULL, why, sizeof why);
	expect(cond_is(st, "if", "{ \"not\": { \"any\": [ { \"gold\": 100 }, { \"not\": { \"seen\": \"lab:met\" } } ] } }"),
	       "cond: a group's last check taken away: the group with it");
	cond_apply(st, "if", true, "0", COND_DEL, NULL, why, sizeof why);
	expect(cond_is(st, "if", "{ \"seen\": \"lab:met\" }"), "cond: one check left at the top stands alone (None of x: not x)");
	bool del = cond_apply(st, "if", true, "", COND_DEL, NULL, why, sizeof why);
	expect(!del && why[0] && cond_is(st, "if", "{ \"seen\": \"lab:met\" }"), "cond: an if keeps its condition");
	cJSON_Delete(st);

	cJSON *row = cJSON_Parse("{ \"text\": \"Leave\" }");
	cond_do(row, "if", false, "add::chance", why, sizeof why);
	expect(cond_is(row, "if", "{ \"chance\": 50 }"), "cond: a row's first check");
	cond_do(row, "if", false, "value::{ \"recruited\": \"KAORU_QUINCY\" }", why, sizeof why);
	cond_do(row, "if", false, "kind::in_party", why, sizeof why);
	expect(cond_is(row, "if", "{ \"in_party\": \"KAORU_QUINCY\" }"), "cond: Recruited to In the party keeps the character");
	cond_do(row, "if", false, "add:", why, sizeof why);
	cond_do(row, "if", false, "del:0", why, sizeof why);
	expect(cond_is(row, "if", "{ \"in_party\": \"\" }"), "cond: the first of two taken away: the other alone");
	cond_do(row, "if", false, "del:", why, sizeof why);
	expect(cond_is(row, "if", NULL), "cond: a row's condition taken away: shown always");
	cJSON_Delete(row);
}

/* The problems: RQ MOD's checker run here, its places mapped to the editor's, the editor's warnings. */
static bool has_problem(const struct problems *ps, int level, const char *place, const char *msg_part)
{
	for (int i = 0; i < ps->n; i++)
		if (ps->p[i].level == level && !strcmp(ps->p[i].place, place) && strstr(ps->p[i].msg, msg_part))
			return true;
	return false;
}

static void problem_tests(void)
{
	static const char *const places[][2] = {
		{ "steps[3]", "steps[3]" },
		{ "steps[3].say", "steps[3]" },
		{ "steps[3].if.recruited", "steps[3]" },
		{ "steps[1].then[0].text", "steps[1].then[0]" },
		{ "steps[2].choice[1]", "steps[2].choice[1]" },
		{ "steps[2].choice[1].if", "steps[2].choice[1]" },
		{ "steps[2].choice[1].steps[0].goto", "steps[2].choice[1].steps[0]" },
		{ "steps[2].choice", "steps[2]" },
		{ "cast.mira.face", "cast.mira" },
		{ "cast", "cast" },
		{ "format", "" },
		{ "", "" },
	};
	bool all = true;
	for (size_t i = 0; i < sizeof places / sizeof places[0]; i++) {
		char out[256];
		problem_place(places[i][0], out, sizeof out);
		if (strcmp(out, places[i][1])) {
			printf("     %s -> %s (not %s)\n", places[i][0], out, places[i][1]);
			all = false;
		}
	}
	expect(all, "a checker's place to the step, row or cast entry it is in");

	cJSON *doc = NULL;
	char why[256];
	bool read = scene_parse("{ \"format\": 1, \"cast\": { \"t\": { \"face\": 12, \"name\": \"Teller\" } }, \"steps\": ["
	                        " { \"goto\": \"nowhere\" },"
	                        " { \"say\": \"t\", \"text\": \"Hi {who}, {{literal}} and {picked}\" },"
	                        " { \"choice\": [ { \"text\": \"A\", \"end\": \"clear\", \"goto\": \"x\" } ], \"into\": \"picked\" },"
	                        " { \"background\": \"pics/none.png\" } ] }", &doc, why, sizeof why);
	expect(read, "a file with problems reads");
	if (!read)
		return;
	struct problems ps = { 0 };
	problems_check(doc, NULL, &ps);
	expect(has_problem(&ps, PROB_ERROR, "steps[0]", "no label \"nowhere\""), "RQ MOD's checker: a go-to with no label, at its step");
	expect(has_problem(&ps, PROB_ERROR, "steps[2].choice[0]", "a row goes one way"), "RQ MOD's checker: a row's problem, at the row");
	expect(has_problem(&ps, PROB_WARN, "steps[1]", "{who}") && !has_problem(&ps, PROB_WARN, "steps[1]", "{picked}")
	       && !has_problem(&ps, PROB_WARN, "steps[1]", "literal"),
	       "{name}: a warning for one no step sets, none for a choice's into or {{braces}}");
	expect(!has_problem(&ps, PROB_ERROR, "steps[3]", "none.png"), "no mod folder: a picture's file not looked for");
	expect(ps.errors == 3 && ps.warnings == 1, "three problems (the row's go-to too) and one warning, no more");
	if (ps.errors != 3 || ps.warnings != 1)
		for (int i = 0; i < ps.n; i++)
			printf("     %s %s: %s\n", ps.p[i].level == PROB_ERROR ? "error" : "warning", ps.p[i].at, ps.p[i].msg);
	problems_free(&ps);

	/* A picture looked for in the mod's folder (this run's own folder has no pics/none.png). */
	problems_check(doc, ".", &ps);
	expect(has_problem(&ps, PROB_ERROR, "steps[3]", "no file pics/none.png in the mod's folder"),
	       "with a mod folder: a picture not in it, as the loader says it");
	problems_free(&ps);
	cJSON_Delete(doc);

	char kwhy[300];
	expect(problems_find_id("chara", "Nobody At All", kwhy, sizeof kwhy) != 0, "a name with no game: not refused here");
	expect(problems_find_id("item", "testlab:no_such_item", kwhy, sizeof kwhy) == -1, "a mod's own id: known only with the game running");
	expect(!problems_lua_why(), "RQ MOD's checker loads in the editor's Lua");
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

	cond_tests();
	problem_tests();

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
