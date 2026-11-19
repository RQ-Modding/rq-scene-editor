/* A scene file read as RQ MOD's loader reads it, and written in the editor's one layout.
 *
 * Reading: the loader's own steps and messages (cJSON's parse, a byte-order mark skipped, at most
 * 4 MB, no zero byte, no field twice in one object, 200 levels at most), so a file the editor opens is
 * one the game opens, and a file it refuses gives the game's reason with the same line and column.
 *
 * The layout, one for every file:
 *   - the file's own fields two spaces in, in the order $schema, format, once, cast, steps, then any
 *     other as it came; the cast one name per line; the steps one per line, four spaces in;
 *   - a step on one line, its own key first ({ "say": "mira", "text": "..." }), then its options in
 *     the schema's order, then any other as it came;
 *   - a step that holds steps (an if's then and else, a choice row's steps) opens the list at the
 *     end of its line and writes those steps four spaces further in, the list's ] back under the
 *     step's {; a choice whose one line would pass 100 columns writes one row per line the same way;
 *   - a choice row's fields in the schema's order, a condition's own key first;
 *   - objects inside a line as { "a": 1 }, lists as ["a", "b"], numbers as the loader reads them
 *     (a whole number without a point), strings with JSON's escapes and their UTF-8 as it is;
 *   - a newline at the end, no byte-order mark, \n between lines. */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "system4.h"
#include "system4/file.h"

#include "editor.h"

#define SCENE_FILE_MAX (4 * 1024 * 1024)
#define SCENE_JSON_DEPTH 200
#define LINE_MAX_COLUMNS 100

/* ---- reading ---- */

static bool check_tree(const cJSON *v, int depth, char *why, size_t n)
{
	if (depth > SCENE_JSON_DEPTH) {
		snprintf(why, n, "nested too deep");
		return false;
	}
	if (cJSON_IsArray(v)) {
		for (const cJSON *c = v->child; c; c = c->next)
			if (!check_tree(c, depth + 1, why, n))
				return false;
	} else if (cJSON_IsObject(v)) {
		for (const cJSON *c = v->child; c; c = c->next) {
			const char *k = c->string ? c->string : "";
			for (const cJSON *d = v->child; d != c; d = d->next) {
				if (!strcmp(d->string ? d->string : "", k)) {
					snprintf(why, n, "the field \"%.100s\" twice in one object", k);
					return false;
				}
			}
			if (!check_tree(c, depth + 1, why, n))
				return false;
		}
	}
	return true;
}

bool scene_parse(const char *text, cJSON **root, char *why, size_t n)
{
	const char *end = NULL;
	*root = cJSON_ParseWithOpts(text, &end, 1);
	if (!*root) {
		int line = 1, col = 1;
		size_t len = strlen(text);
		if (!end || end < text || end > text + len)
			end = text;
		for (const char *c = text; c < end; c++) {
			if (*c == '\n') {
				line++;
				col = 1;
			} else if (((unsigned char)*c & 0xC0) != 0x80) {
				col++;
			}
		}
		snprintf(why, n, "not valid JSON at line %d, column %d", line, col);
		return false;
	}
	if (!check_tree(*root, 0, why, n)) {
		cJSON_Delete(*root);
		*root = NULL;
		return false;
	}
	return true;
}

bool scene_read(const char *path, cJSON **root, char **raw, size_t *raw_len, char *why, size_t n)
{
	*root = NULL;
	if (raw)
		*raw = NULL;
	if (raw_len)
		*raw_len = 0;
	FILE *f = file_open_utf8(path, "rb");
	if (!f) {
		snprintf(why, n, "no such file");
		return false;
	}
	fseek(f, 0, SEEK_END);
	long size = ftell(f);
	fseek(f, 0, SEEK_SET);
	if (size < 0 || size > SCENE_FILE_MAX) {
		fclose(f);
		snprintf(why, n, "%s", size < 0 ? "cannot be read" : "larger than 4 MB");
		return false;
	}
	char *buf = xmalloc((size_t)size + 1);
	if (fread(buf, 1, (size_t)size, f) != (size_t)size) {
		fclose(f);
		free(buf);
		snprintf(why, n, "cannot be read");
		return false;
	}
	fclose(f);
	buf[size] = 0;
	char *text = buf;
	if (size >= 3 && (unsigned char)text[0] == 0xEF && (unsigned char)text[1] == 0xBB
	    && (unsigned char)text[2] == 0xBF)
		text += 3;
	bool ok;
	if ((long)strlen(text) != size - (long)(text - buf)) {
		snprintf(why, n, "a zero byte in the file: not a JSON text file");
		ok = false;
	} else {
		ok = scene_parse(text, root, why, n);
	}
	if (raw) {
		*raw = buf;
		if (raw_len)
			*raw_len = (size_t)size;
	} else {
		free(buf);
	}
	return ok;
}

/* ---- the layout ---- */

struct buf {
	char *p;
	size_t n, cap;
};

static void put(struct buf *b, const char *s, size_t n)
{
	if (b->n + n + 1 > b->cap) {
		b->cap = (b->n + n + 1) * 2;
		b->p = xrealloc(b->p, b->cap);
	}
	memcpy(b->p + b->n, s, n);
	b->n += n;
	b->p[b->n] = 0;
}

static void puts_(struct buf *b, const char *s)
{
	put(b, s, strlen(s));
}

static void put_indent(struct buf *b, int n)
{
	static const char spaces[] = "                                                                ";
	while (n > 0) {
		int k = n < 64 ? n : 64;
		put(b, spaces, (size_t)k);
		n -= k;
	}
}

static char *done(struct buf *b)
{
	if (!b->p)
		return xstrdup("");
	return b->p;
}

int utf8_columns(const char *s)
{
	int n = 0;
	for (; *s; s++)
		if (((unsigned char)*s & 0xC0) != 0x80)
			n++;
	return n;
}

static void put_string(struct buf *b, const char *s)
{
	put(b, "\"", 1);
	const char *run = s;
	for (; *s; s++) {
		unsigned char c = (unsigned char)*s;
		const char *esc = NULL;
		char u[8];
		switch (c) {
		case '"': esc = "\\\""; break;
		case '\\': esc = "\\\\"; break;
		case '\n': esc = "\\n"; break;
		case '\r': esc = "\\r"; break;
		case '\t': esc = "\\t"; break;
		case '\b': esc = "\\b"; break;
		case '\f': esc = "\\f"; break;
		default:
			if (c < 0x20) {
				snprintf(u, sizeof u, "\\u%04x", c);
				esc = u;
			}
		}
		if (esc) {
			put(b, run, (size_t)(s - run));
			puts_(b, esc);
			run = s + 1;
		}
	}
	put(b, run, (size_t)(s - run));
	put(b, "\"", 1);
}

static void put_number(struct buf *b, double d)
{
	char t[64];
	if (isnan(d)) {
		snprintf(t, sizeof t, "null");
	} else if (isinf(d)) {
		snprintf(t, sizeof t, "%s", d > 0 ? "1e400" : "-1e400");
	} else if (d == 0) {
		snprintf(t, sizeof t, "0");
	} else if (d == floor(d) && fabs(d) <= 9007199254740992.0) {
		snprintf(t, sizeof t, "%.0f", d);
	} else {
		for (int p = 15; p <= 17; p++) {
			snprintf(t, sizeof t, "%.*g", p, d);
			if (strtod(t, NULL) == d)
				break;
		}
	}
	puts_(b, t);
}

/* What a value is, for its fields' order and for which lists take lines of their own. */
enum ctx { X_ANY, X_TOP, X_STEP, X_STEPS, X_COND, X_CONDS, X_ROW, X_ROWS, X_CAST, X_CAST_ENTRY };

static const char *const top_order[] = { "$schema", "format", "once", "cast", "steps", NULL };
static const char *const step_order[] = { "text", "name", "hide_face", "paper", "page", "wait", "effect", "time",
                                          "mode", "then", "else", "args", "into", "x", "y", NULL };
static const char *const cond_kinds[] = { "seen", "var", "call", "not", "all", "any", "cleared", "recruited",
                                          "in_party", "has_item", "gold", "chance", NULL };
static const char *const cond_order[] = { "is", "at_least", "at_most", "args", NULL };
static const char *const row_order[] = { "text", "id", "comment", "r", "g", "b", "sound", "if", "steps", "goto",
                                         "end", NULL };
static const char *const cast_order[] = { "face", "name", NULL };

const char *step_kind(const cJSON *st)
{
	if (!cJSON_IsObject(st))
		return NULL;
	for (const cJSON *c = st->child; c; c = c->next)
		if (c->string && kind_find(c->string))
			return c->string;
	return NULL;
}

static const char *cond_kind(const cJSON *v)
{
	for (const cJSON *c = v->child; c; c = c->next)
		for (int i = 0; c->string && cond_kinds[i]; i++)
			if (!strcmp(c->string, cond_kinds[i]))
				return c->string;
	return NULL;
}

/* An object's context, X_ANY when the value is not what the context wants. */
static int obj_ctx(const cJSON *v, int ctx)
{
	bool obj = cJSON_IsObject(v), arr = cJSON_IsArray(v);
	switch (ctx) {
	case X_TOP: case X_STEP: case X_COND: case X_ROW: case X_CAST: case X_CAST_ENTRY:
		return obj ? ctx : X_ANY;
	case X_STEPS: case X_CONDS: case X_ROWS:
		return arr ? ctx : X_ANY;
	}
	return X_ANY;
}

static const char *obj_kind(const cJSON *v, int ctx)
{
	if (ctx == X_STEP)
		return step_kind(v);
	if (ctx == X_COND)
		return cond_kind(v);
	return NULL;
}

static int field_ctx(int ctx, const char *kind, const char *key)
{
	switch (ctx) {
	case X_TOP:
		if (!strcmp(key, "cast"))
			return X_CAST;
		if (!strcmp(key, "steps"))
			return X_STEPS;
		return X_ANY;
	case X_CAST:
		return X_CAST_ENTRY;
	case X_STEP:
		if (kind && !strcmp(kind, "if")) {
			if (!strcmp(key, "if"))
				return X_COND;
			if (!strcmp(key, "then") || !strcmp(key, "else"))
				return X_STEPS;
		}
		if (kind && !strcmp(kind, "choice") && !strcmp(key, "choice"))
			return X_ROWS;
		return X_ANY;
	case X_COND:
		if (!strcmp(key, "not"))
			return X_COND;
		if (!strcmp(key, "all") || !strcmp(key, "any"))
			return X_CONDS;
		return X_ANY;
	case X_ROW:
		if (!strcmp(key, "if"))
			return X_COND;
		if (!strcmp(key, "steps"))
			return X_STEPS;
		return X_ANY;
	}
	return X_ANY;
}

static int item_ctx(int ctx)
{
	switch (ctx) {
	case X_STEPS: return X_STEP;
	case X_CONDS: return X_COND;
	case X_ROWS: return X_ROW;
	}
	return X_ANY;
}

/* The object's fields in the layout's order (malloc'd; *n of them). */
static const cJSON **ordered(const cJSON *v, int ctx, int *n)
{
	int count = 0;
	for (const cJSON *c = v->child; c; c = c->next)
		count++;
	const cJSON **out = xmalloc((size_t)(count ? count : 1) * sizeof *out);
	int k = 0;
	const char *first = obj_kind(v, ctx);
	const char *const *order = ctx == X_TOP ? top_order : ctx == X_STEP ? step_order : ctx == X_COND ? cond_order
	                         : ctx == X_ROW ? row_order : ctx == X_CAST_ENTRY ? cast_order : NULL;
	if (first) {
		for (const cJSON *c = v->child; c; c = c->next) {
			if (c->string && !strcmp(c->string, first)) {
				out[k++] = c;
				break;
			}
		}
	}
	for (int i = 0; order && order[i]; i++) {
		if (first && !strcmp(order[i], first))
			continue;
		for (const cJSON *c = v->child; c; c = c->next) {
			if (c->string && !strcmp(c->string, order[i])) {
				out[k++] = c;
				break;
			}
		}
	}
	for (const cJSON *c = v->child; c; c = c->next) {
		bool taken = false;
		for (int i = 0; i < k && !taken; i++)
			taken = out[i] == c;
		if (!taken)
			out[k++] = c;
	}
	*n = k;
	return out;
}

static void put_inline(struct buf *b, const cJSON *v, int ctx)
{
	ctx = obj_ctx(v, ctx);
	if (cJSON_IsObject(v)) {
		if (!v->child) {
			puts_(b, "{}");
			return;
		}
		int n;
		const cJSON **f = ordered(v, ctx, &n);
		const char *kind = obj_kind(v, ctx);
		puts_(b, "{ ");
		for (int i = 0; i < n; i++) {
			if (i)
				puts_(b, ", ");
			put_string(b, f[i]->string ? f[i]->string : "");
			puts_(b, ": ");
			put_inline(b, f[i], field_ctx(ctx, kind, f[i]->string ? f[i]->string : ""));
		}
		puts_(b, " }");
		free(f);
	} else if (cJSON_IsArray(v)) {
		puts_(b, "[");
		for (const cJSON *c = v->child; c; c = c->next) {
			if (c != v->child)
				puts_(b, ", ");
			put_inline(b, c, item_ctx(ctx));
		}
		puts_(b, "]");
	} else if (cJSON_IsString(v)) {
		put_string(b, v->valuestring ? v->valuestring : "");
	} else if (cJSON_IsNumber(v)) {
		put_number(b, v->valuedouble);
	} else if (cJSON_IsTrue(v)) {
		puts_(b, "true");
	} else if (cJSON_IsFalse(v)) {
		puts_(b, "false");
	} else {
		puts_(b, "null");
	}
}

char *json_inline(const cJSON *v)
{
	struct buf b = { 0 };
	put_inline(&b, v, X_ANY);
	return done(&b);
}

static char *inline_as(const cJSON *v, int ctx)
{
	struct buf b = { 0 };
	put_inline(&b, v, ctx);
	return done(&b);
}

static bool has_lines(const cJSON *v, int ctx);

/* A field's value takes lines of its own: a list of steps that is not empty, a choice's rows when
 * one of them holds steps. */
static bool field_lines(const cJSON *v, int fctx)
{
	fctx = obj_ctx(v, fctx);
	if (fctx == X_STEPS)
		return v->child != NULL;
	if (fctx == X_ROWS) {
		for (const cJSON *c = v->child; c; c = c->next)
			if (cJSON_IsObject(c) && has_lines(c, X_ROW))
				return true;
	}
	return false;
}

/* A step or a row whose fields take lines. */
static bool has_lines(const cJSON *v, int ctx)
{
	const char *kind = obj_kind(v, ctx);
	for (const cJSON *c = v->child; c; c = c->next)
		if (field_lines(c, field_ctx(ctx, kind, c->string ? c->string : "")))
			return true;
	return false;
}

static void put_item(struct buf *b, const cJSON *v, int ind, int ctx);

/* A step (X_STEP) or a choice's row (X_ROW), the cursor already at its indent. */
static void put_entry(struct buf *b, const cJSON *v, int ind, int ctx)
{
	const char *kind = obj_kind(v, ctx);
	bool wide = false;
	if (ctx == X_STEP && kind && !strcmp(kind, "choice")) {
		const cJSON *rows = cJSON_GetObjectItemCaseSensitive(v, "choice");
		if (cJSON_IsArray(rows) && rows->child) {
			char *one = inline_as(v, X_STEP);
			wide = ind + utf8_columns(one) > LINE_MAX_COLUMNS;
			free(one);
		}
	}
	if (!wide && !has_lines(v, ctx)) {
		put_inline(b, v, ctx);
		return;
	}
	int n;
	const cJSON **f = ordered(v, ctx, &n);
	puts_(b, "{ ");
	for (int i = 0; i < n; i++) {
		const char *key = f[i]->string ? f[i]->string : "";
		int fc = obj_ctx(f[i], field_ctx(ctx, kind, key));
		if (i)
			puts_(b, ", ");
		put_string(b, key);
		puts_(b, ": ");
		bool lines = field_lines(f[i], fc) || (fc == X_ROWS && wide && f[i]->child);
		if (!lines) {
			put_inline(b, f[i], fc);
			continue;
		}
		puts_(b, "[\n");
		for (const cJSON *c = f[i]->child; c; c = c->next) {
			put_indent(b, ind + 4);
			put_item(b, c, ind + 4, item_ctx(fc));
			puts_(b, c->next ? ",\n" : "\n");
		}
		put_indent(b, ind);
		puts_(b, "]");
	}
	puts_(b, " }");
	free(f);
}

static void put_item(struct buf *b, const cJSON *v, int ind, int ctx)
{
	if (cJSON_IsObject(v) && (ctx == X_STEP || ctx == X_ROW))
		put_entry(b, v, ind, ctx);
	else
		put_inline(b, v, ctx);
}

char *step_write(const cJSON *step)
{
	struct buf b = { 0 };
	put_item(&b, step, 0, X_STEP);
	return done(&b);
}

char *scene_write(const cJSON *root)
{
	struct buf b = { 0 };
	if (!cJSON_IsObject(root) || !root->child) {
		put_inline(&b, root, X_ANY);
		puts_(&b, "\n");
		return done(&b);
	}
	int n;
	const cJSON **f = ordered(root, X_TOP, &n);
	puts_(&b, "{\n");
	for (int i = 0; i < n; i++) {
		const char *key = f[i]->string ? f[i]->string : "";
		int fc = obj_ctx(f[i], field_ctx(X_TOP, NULL, key));
		put_indent(&b, 2);
		put_string(&b, key);
		puts_(&b, ": ");
		if (fc == X_CAST && f[i]->child) {
			puts_(&b, "{\n");
			for (const cJSON *c = f[i]->child; c; c = c->next) {
				put_indent(&b, 4);
				put_string(&b, c->string ? c->string : "");
				puts_(&b, ": ");
				put_inline(&b, c, X_CAST_ENTRY);
				puts_(&b, c->next ? ",\n" : "\n");
			}
			put_indent(&b, 2);
			puts_(&b, "}");
		} else if (fc == X_STEPS && f[i]->child) {
			puts_(&b, "[\n");
			for (const cJSON *c = f[i]->child; c; c = c->next) {
				put_indent(&b, 4);
				put_item(&b, c, 4, X_STEP);
				puts_(&b, c->next ? ",\n" : "\n");
			}
			put_indent(&b, 2);
			puts_(&b, "]");
		} else {
			put_inline(&b, f[i], fc);
		}
		puts_(&b, i + 1 < n ? ",\n" : "\n");
	}
	puts_(&b, "}\n");
	free(f);
	return done(&b);
}

/* ---- the same values ---- */

bool json_same(const cJSON *a, const cJSON *b)
{
	if (!a || !b)
		return a == b;
	int ta = a->type & 0xFF, tb = b->type & 0xFF;
	if (ta != tb)
		return false;
	if (cJSON_IsNumber(a))
		return a->valuedouble == b->valuedouble;
	if (cJSON_IsString(a))
		return !strcmp(a->valuestring ? a->valuestring : "", b->valuestring ? b->valuestring : "");
	if (cJSON_IsArray(a)) {
		const cJSON *x = a->child, *y = b->child;
		for (; x && y; x = x->next, y = y->next)
			if (!json_same(x, y))
				return false;
		return !x && !y;
	}
	if (cJSON_IsObject(a)) {
		int na = 0, nb = 0;
		for (const cJSON *x = a->child; x; x = x->next)
			na++;
		for (const cJSON *y = b->child; y; y = y->next)
			nb++;
		if (na != nb)
			return false;
		for (const cJSON *x = a->child; x; x = x->next) {
			const cJSON *y = cJSON_GetObjectItemCaseSensitive(b, x->string ? x->string : "");
			if (!y || !json_same(x, y))
				return false;
		}
		return true;
	}
	return true;
}
