/* The game's log: RQ MOD writes logs/rqmod.log in the game's folder each time the game starts, a
 * line per message, each tagged with who wrote it ("[testlab] Test Lab ready", "[rqmod] ..."). The
 * editor reads what the game adds, twice a second, and keeps the last lines; the bottom panel shows
 * the open mod's (its tag, and the loader's lines that name it). */
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "editor.h"

#define KEEP 4000

static char path[1600];
static char *lines[KEEP];
static int first, count;     /* a ring: the oldest at first */
static long long offset;     /* bytes read so far */
static char *part;           /* a line not ended yet */
static size_t partn;
static double last_poll = -10;
static unsigned gen;
static bool exists;

static void clear(void)
{
	for (int i = 0; i < count; i++)
		free(lines[(first + i) % KEEP]);
	first = count = 0;
	offset = 0;
	free(part);
	part = NULL;
	partn = 0;
	gen++;
}

void gamelog_set_file(const char *p)
{
	if (p && !strcmp(p, path))
		return;
	clear();
	snprintf(path, sizeof path, "%s", p ? p : "");
	last_poll = -10;
}

void gamelog_set_game(const char *game_dir)
{
	char p[1600] = "";
	if (game_dir && game_dir[0])
		snprintf(p, sizeof p, "%s/logs/rqmod.log", game_dir);
	gamelog_set_file(p);
}

const char *gamelog_path(void)
{
	return path;
}

bool gamelog_exists(void)
{
	return exists;
}

static void push(const char *s, size_t n)
{
	while (n && (s[n - 1] == '\r' || s[n - 1] == '\n'))
		n--;
	char *l = malloc(n + 1);
	memcpy(l, s, n);
	l[n] = 0;
	if (count == KEEP) {
		free(lines[first]);
		lines[first] = l;
		first = (first + 1) % KEEP;
	} else {
		lines[(first + count++) % KEEP] = l;
	}
}

void gamelog_poll(double now, bool force)
{
	if (!path[0] || (!force && now - last_poll < 0.5 && now >= last_poll))
		return;
	last_poll = now;
	struct _stat64 st;
	exists = _stat64(path, &st) == 0;
	if (!exists) {
		if (count || offset)
			clear();
		return;
	}
	long long size = st.st_size;
	if (size < offset)
		clear();   /* the game started again: a new log */
	if (size == offset)
		return;
	FILE *f = fopen(path, "rb");
	if (!f)
		return;
	if (_fseeki64(f, offset, SEEK_SET) != 0) {
		fclose(f);
		return;
	}
	/* At most 4 MB a poll: a long log comes in over a few. */
	long long want = size - offset;
	if (want > 4 * 1024 * 1024)
		want = 4 * 1024 * 1024;
	char *buf = malloc((size_t)want);
	size_t got = fread(buf, 1, (size_t)want, f);
	fclose(f);
	offset += (long long)got;
	size_t a = 0;
	for (size_t i = 0; i < got; i++) {
		if (buf[i] != '\n')
			continue;
		if (part) {
			part = realloc(part, partn + (i - a) + 1);
			memcpy(part + partn, buf + a, i - a);
			push(part, partn + (i - a));
			free(part);
			part = NULL;
			partn = 0;
		} else {
			push(buf + a, i - a);
		}
		a = i + 1;
	}
	if (a < got) {
		part = realloc(part, partn + (got - a) + 1);
		memcpy(part + partn, buf + a, got - a);
		partn += got - a;
	}
	free(buf);
	if (got)
		gen++;
}

int gamelog_count(void)
{
	return count;
}

const char *gamelog_line(int i)
{
	return i >= 0 && i < count ? lines[(first + i) % KEEP] : NULL;
}

unsigned gamelog_gen(void)
{
	return gen;
}

static bool id_char(char c)
{
	return isalnum((unsigned char)c) || c == '-' || c == '_';
}

bool gamelog_for(const char *line, const char *mod)
{
	if (!mod || !mod[0])
		return true;
	size_t n = strlen(mod);
	if (line[0] == '[' && !strncmp(line + 1, mod, n) && line[1 + n] == ']')
		return true;
	if (strncmp(line, "[rqmod]", 7))
		return false;
	/* The loader's lines that name the mod ("Loading testlab", "...\mods\testlab\init.lua"). */
	for (const char *p = line + 7; (p = strstr(p, mod)); p += n)
		if (!id_char(p[-1]) && !id_char(p[n]))
			return true;
	return false;
}
