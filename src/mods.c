/* The game folder's mods (a folder with a mod.json in the game's mods folder), and a mod's scene
 * files: every .json under its scenes folder, and any other .json in the mod's folder that is a
 * scene file (one with "format" and "steps"). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include "system4.h"
#include "system4/file.h"

#include "editor.h"

struct mod *MODS;
int NMODS;
struct scene_file *FILES;
int NFILES;

#define MAX_DEPTH 6
#define MAX_FILES 2000

/* The time to the 100 ns, so a change in the same second as the last one is seen. */
bool file_stat(const char *path, long long *mtime, long long *size)
{
	return win_stat(path, mtime, size);
}

static char *read_text(const char *path)
{
	size_t len = 0;
	char *raw = file_read(path, &len);
	if (!raw)
		return NULL;
	char *text = xmalloc(len + 1);
	memcpy(text, raw, len);
	text[len] = 0;
	free(raw);
	return text;
}

static void trim_slash(char *dir)
{
	size_t n = strlen(dir);
	while (n > 3 && (dir[n - 1] == '/' || dir[n - 1] == '\\'))
		dir[--n] = 0;
}

bool mod_read(const char *dir, struct mod *m)
{
	memset(m, 0, sizeof *m);
	snprintf(m->dir, sizeof m->dir, "%s", dir);
	trim_slash(m->dir);
	char path[1200];
	snprintf(path, sizeof path, "%s/mod.json", m->dir);
	char *text = read_text(path);
	if (!text)
		return false;
	cJSON *j = cJSON_Parse(text);
	free(text);
	const cJSON *id = cJSON_GetObjectItemCaseSensitive(j, "id");
	const cJSON *name = cJSON_GetObjectItemCaseSensitive(j, "name");
	if (cJSON_IsString(id) && id->valuestring[0])
		snprintf(m->id, sizeof m->id, "%s", id->valuestring);
	else
		snprintf(m->id, sizeof m->id, "%s", base_name(m->dir));
	snprintf(m->name, sizeof m->name, "%s", cJSON_IsString(name) && name->valuestring[0] ? name->valuestring : m->id);
	cJSON_Delete(j);
	return true;
}

static int mod_cmp(const void *a, const void *b)
{
	const struct mod *x = a, *y = b;
	if (x->outside != y->outside)
		return x->outside ? 1 : -1;
	return strcasecmp(x->name, y->name);
}

static bool same_dir(const char *a, const char *b)
{
	for (; *a && *b; a++, b++) {
		char x = *a == '\\' ? '/' : *a, y = *b == '\\' ? '/' : *b;
		if (x >= 'A' && x <= 'Z')
			x += 32;
		if (y >= 'A' && y <= 'Z')
			y += 32;
		if (x != y)
			return false;
	}
	return !*a && !*b;
}

void mods_scan(const char *game_dir, const char *outside_dir)
{
	free(MODS);
	MODS = NULL;
	NMODS = 0;
	int cap = 0;
	if (game_dir && game_dir[0]) {
		char dir[1100];
		snprintf(dir, sizeof dir, "%s/mods", game_dir);
		UDIR *d = opendir_utf8(dir);
		char *name;
		while (d && (name = readdir_utf8(d))) {
			if (name[0] != '.') {
				char full[1200];
				snprintf(full, sizeof full, "%s/%s", dir, name);
				struct mod m;
				if (is_directory(full) && mod_read(full, &m)) {
					if (NMODS == cap) {
						cap = cap ? cap * 2 : 32;
						MODS = xrealloc(MODS, (size_t)cap * sizeof *MODS);
					}
					MODS[NMODS++] = m;
				}
			}
			free(name);
		}
		if (d)
			closedir_utf8(d);
	}
	if (outside_dir && outside_dir[0]) {
		struct mod m;
		bool known = false;
		for (int i = 0; i < NMODS && !known; i++)
			known = same_dir(MODS[i].dir, outside_dir);
		if (!known && mod_read(outside_dir, &m)) {
			m.outside = true;
			MODS = xrealloc(MODS, (size_t)(NMODS + 1) * sizeof *MODS);
			MODS[NMODS++] = m;
		}
	}
	if (NMODS > 1)
		qsort(MODS, (size_t)NMODS, sizeof *MODS, mod_cmp);
}

int mod_find(const char *id_or_dir)
{
	if (!id_or_dir || !id_or_dir[0])
		return -1;
	for (int i = 0; i < NMODS; i++)
		if (!strcmp(MODS[i].id, id_or_dir))
			return i;
	for (int i = 0; i < NMODS; i++)
		if (same_dir(MODS[i].dir, id_or_dir))
			return i;
	return -1;
}

/* ---- a mod's scene files ---- */

/* Everything looked at, listed or not, so a file is read again only when it changes. */
static struct scene_file *seen;
static int nseen;
static char seen_mod[1024];

static bool ends_json(const char *name)
{
	size_t n = strlen(name);
	return n > 5 && !strcasecmp(name + n - 5, ".json");
}

static void walk(const char *root, const char *rel, int depth, char (*out)[512], int *n)
{
	char dir[1600];
	snprintf(dir, sizeof dir, "%s%s%s", root, rel[0] ? "/" : "", rel);
	UDIR *d = opendir_utf8(dir);
	if (!d)
		return;
	char *name;
	while ((name = readdir_utf8(d)) && *n < MAX_FILES) {
		if (name[0] == '.' || !strcmp(name, "node_modules")) {
			free(name);
			continue;
		}
		char sub[512], full[2200];
		snprintf(sub, sizeof sub, "%s%s%s", rel, rel[0] ? "/" : "", name);
		snprintf(full, sizeof full, "%s/%s", root, sub);
		if (is_directory(full)) {
			if (depth < MAX_DEPTH)
				walk(root, sub, depth + 1, out, n);
		} else if (ends_json(name) && !(depth == 0 && !strcasecmp(name, "mod.json"))) {
			snprintf(out[(*n)++], 512, "%s", sub);
		}
		free(name);
	}
	closedir_utf8(d);
}

static void look_at(struct scene_file *f)
{
	cJSON *root = NULL;
	char why[256];
	f->read = false;
	f->why[0] = 0;
	f->lines = 0;
	if (!scene_read(f->path, &root, NULL, NULL, why, sizeof why)) {
		snprintf(f->why, sizeof f->why, "%s", why);
	} else if (!cJSON_IsObject(root) || !cJSON_GetObjectItemCaseSensitive(root, "steps")
	           || !cJSON_GetObjectItemCaseSensitive(root, "format")) {
		snprintf(f->why, sizeof f->why, "not a scene file: it needs \"format\" and \"steps\"");
	} else {
		f->read = true;
		f->lines = count_lines(cJSON_GetObjectItemCaseSensitive(root, "steps"));
	}
	cJSON_Delete(root);
}

static int file_cmp(const void *a, const void *b)
{
	const struct scene_file *x = a, *y = b;
	if (x->in_scenes != y->in_scenes)
		return x->in_scenes ? -1 : 1;
	return strcasecmp(x->rel, y->rel);
}

void files_scan(const struct mod *m)
{
	if (!m) {
		NFILES = 0;
		return;
	}
	if (strcmp(seen_mod, m->dir)) {
		free(seen);
		seen = NULL;
		nseen = 0;
		snprintf(seen_mod, sizeof seen_mod, "%s", m->dir);
	}
	char (*rels)[512] = xmalloc((size_t)MAX_FILES * 512);
	int n = 0;
	walk(m->dir, "", 0, rels, &n);
	struct scene_file *now = xcalloc((size_t)(n ? n : 1), sizeof *now);
	for (int i = 0; i < n; i++) {
		struct scene_file *f = &now[i];
		snprintf(f->rel, sizeof f->rel, "%s", rels[i]);
		snprintf(f->path, sizeof f->path, "%s/%s", m->dir, rels[i]);
		f->in_scenes = !strncasecmp(f->rel, "scenes/", 7);
		long long mt = 0, sz = 0;
		file_stat(f->path, &mt, &sz);
		bool known = false;
		for (int k = 0; k < nseen && !known; k++) {
			if (!strcmp(seen[k].rel, f->rel) && seen[k].mtime == mt && seen[k].size == sz) {
				*f = seen[k];
				known = true;
			}
		}
		if (!known) {
			f->mtime = mt;
			f->size = sz;
			look_at(f);
		}
	}
	free(rels);
	free(seen);
	seen = now;
	nseen = n;

	free(FILES);
	FILES = xcalloc((size_t)(n ? n : 1), sizeof *FILES);
	NFILES = 0;
	for (int i = 0; i < n; i++)
		if (seen[i].in_scenes || seen[i].read)
			FILES[NFILES++] = seen[i];
	if (NFILES > 1)
		qsort(FILES, (size_t)NFILES, sizeof *FILES, file_cmp);
}

int file_find(const char *rel)
{
	for (int i = 0; i < NFILES; i++)
		if (!strcmp(FILES[i].rel, rel))
			return i;
	return -1;
}

bool scene_name_ok(const char *name)
{
	size_t n = strlen(name);
	if (!n || n > 64)
		return false;
	for (const char *p = name; *p; p++) {
		char c = *p;
		if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-' || c == '_'))
			return false;
	}
	return true;
}
