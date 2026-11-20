/* The stage: the game's screen at a step, drawn from the game's own pieces at the game's places.
 *
 * The steps are followed from the file's start to the selected one (into the branch that holds it;
 * a branch passed on the way is not entered, a goto not followed): each picture step sets its
 * layer as the game's call does (a scene CG, black and white clear the others; a set puts the
 * game's mask wallpaper in), a line goes into the message box. Everything is drawn at the game's
 * 1024 by 768, scaled: each layer at its part's place, by the parts' order (`画面表示.jaf`); the
 * message frame of the line's kind with its name plate, the face in its frame, the click mark and
 * AUTO, SKIP and CONFIG (`メッセージ枠.jaf`, `メッセージボタン.jaf`); a choice as the game's menu
 * (`T_Select`). A page is laid out in the game's font as the game lays it out: words that do not
 * fit go to the next line, an opening quote hangs left of where the lines start, and a line past
 * the box's last is drawn in red. Still, the stage shows where each step ends; while it plays
 * (play.c), the same drawing takes what moves: a slide, a shake, the letters coming in, the click
 * mark's turn, a white flash. */
#include <float.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "system4.h"

#include "editor.h"

/* The width a line of a frame-1 box takes from where its text starts before the game breaks it,
 * in the window's measure of the game's font at BOX_TEXT_SIZE (an opening quote hangs left of it).
 * Measured on the game's shots of a long line: the first line held 611 and not 715 with the next
 * word from its quote, the third 681 and not 740 under the quote's 15. */
#define BOX_TEXT_W 685.0f

/* Where the game's text starts against the place its frame registers (31,18 in frame 1, 43,7 in
 * the E frames), in the window's measure: on the game's shots, a line's top at 586 draws its ink
 * from 594, as Dear ImGui draws it from 3 lower. */
#define TEXT_DX 0
#define TEXT_DY (-3)

/* Text the game draws at a size of its own, in the window's sizes. */
#define GAME_SIZE_K (BOX_TEXT_SIZE / 40.0f)

/* ---- following the steps ---- */

static const cJSON *get(const cJSON *o, const char *k)
{
	return cJSON_IsObject(o) ? cJSON_GetObjectItemCaseSensitive(o, k) : NULL;
}

/* The game's 画面_クリア: every picture, the screen's colour and the face (顔クリア). */
static void clear_layers(struct stage *s)
{
	for (int i = 0; i < L_COUNT; i++)
		s->layer[i] = NULL;
	s->mask_cg = 0;
	s->screen = 0;
	s->face_off = true;
}

void stage_apply(struct stage *s, const cJSON *st)
{
	const char *kind = step_kind(st);
	if (!kind)
		return;
	const cJSON *v = get(st, kind);
	if (!strcmp(kind, "background") || !strcmp(kind, "background_in")) {
		s->layer[L_BG] = v;
	} else if (!strcmp(kind, "background_out")) {
		s->layer[L_BG] = NULL;
	} else if (!strcmp(kind, "wallpaper")) {
		s->layer[L_WALL] = v;
		s->screen = 0;
	} else if (!strcmp(kind, "mask_wallpaper")) {
		s->layer[L_MASK] = v;
		s->mask_cg = 0;
	} else if (!strcmp(kind, "portrait")) {
		s->layer[L_PORTRAIT] = v;
	} else if (!strcmp(kind, "mini")) {
		s->layer[L_MINI] = v;
	} else if (!strcmp(kind, "cg")) {
		/* イベントＣＧ表示 clears the screen first. */
		clear_layers(s);
		s->layer[L_CG] = v;
	} else if (!strcmp(kind, "set")) {
		/* セット表示: the four layers (none where 0 or left out), no scene CG unless the set
		 * names one, and the game's mask wallpaper 5550; the message box and the face cleared. */
		static const char *const keys[] = { "wallpaper", "background", "portrait", "mini", "cg" };
		static const int layers[] = { L_WALL, L_BG, L_PORTRAIT, L_MINI, L_CG };
		for (int i = 0; i < 5; i++)
			s->layer[layers[i]] = get(v, keys[i]);
		s->layer[L_MASK] = NULL;
		s->mask_cg = 5550;
		if (s->layer[L_WALL])
			s->screen = 0;
		s->line = NULL;
		s->face_off = true;
	} else if (!strcmp(kind, "black") || !strcmp(kind, "white")) {
		/* 黒画面, 白画面: the screen cleared to one colour. */
		clear_layers(s);
		s->screen = kind[0] == 'b' ? 'b' : 'w';
	} else if (!strcmp(kind, "clear")) {
		clear_layers(s);
		s->line = NULL;
	} else if (!strcmp(kind, "say") || !strcmp(kind, "think") || !strcmp(kind, "narrate")) {
		s->line = st;
		s->face_off = false;
	} else if (!strcmp(kind, "clear_text")) {
		s->line = NULL;
	} else if (!strcmp(kind, "speed")) {
		s->speed = cJSON_IsNumber(v) && v->valuedouble >= 0 ? (int)v->valuedouble : -1;
	} else if (!strcmp(kind, "music")) {
		s->music = cJSON_IsNumber(v) ? (int)v->valuedouble : 0;
	} else if (!strcmp(kind, "world_map")) {
		/* 地図点滅: after the map's click the screen and the box are cleared. */
		clear_layers(s);
		s->line = NULL;
	}
}

void stage_start(struct stage *s, const cJSON *root)
{
	memset(s, 0, sizeof *s);
	s->cast = get(root, "cast");
	s->speed = -1;
}

static bool take(const char **p, const char *word, int *num)
{
	size_t n = strlen(word);
	if (strncmp(*p, word, n))
		return false;
	const char *q = *p + n;
	if (*q < '0' || *q > '9')
		return false;
	long v = 0;
	while (*q >= '0' && *q <= '9' && v < 1000000)
		v = v * 10 + (*q++ - '0');
	if (*q != ']')
		return false;
	*num = (int)v;
	*p = q + 1;
	return true;
}

static void walk(const cJSON *root, const char *place, struct stage *s, bool with_step)
{
	stage_start(s, root);
	const cJSON *list = get(root, "steps");
	const char *p = place ? place : "";
	int k;
	if (!cJSON_IsArray(list) || !take(&p, "steps[", &k))
		return;
	for (;;) {
		const cJSON *st = list->child;
		for (int i = 0; st && i < k; i++, st = st->next)
			stage_apply(s, st);
		if (!st)
			return;
		if (!*p) {
			if (!with_step)
				return;
			stage_apply(s, st);
			const char *kind = step_kind(st);
			if (kind && !strcmp(kind, "choice"))
				s->choice = st;
			return;
		}
		int r;
		const cJSON *next = NULL;
		if (take(&p, ".then[", &k))
			next = get(st, "then");
		else if (take(&p, ".else[", &k))
			next = get(st, "else");
		else if (take(&p, ".choice[", &r) && take(&p, ".steps[", &k)) {
			const cJSON *rows = get(st, "choice");
			next = get(cJSON_IsArray(rows) ? cJSON_GetArrayItem(rows, r) : NULL, "steps");
		}
		if (!cJSON_IsArray(next))
			return;
		list = next;
	}
}

void stage_walk(const cJSON *root, const char *place, struct stage *s)
{
	walk(root, place, s, true);
}

void stage_walk_before(const cJSON *root, const char *place, struct stage *s)
{
	walk(root, place, s, false);
}

/* ---- a line's parts ---- */

const cJSON *line_text(const cJSON *line)
{
	const char *kind = step_kind(line);
	if (!kind)
		return NULL;
	if (!strcmp(kind, "narrate"))
		return get(line, "narrate");
	if (!strcmp(kind, "say") || !strcmp(kind, "think"))
		return get(line, "text");
	return NULL;
}

int line_pages(const cJSON *line)
{
	const cJSON *t = line_text(line);
	if (cJSON_IsString(t))
		return 1;
	if (cJSON_IsArray(t))
		return cJSON_GetArraySize(t);
	return 0;
}

const char *line_page(const cJSON *line, int page)
{
	const cJSON *t = line_text(line);
	if (cJSON_IsString(t))
		return page == 0 ? t->valuestring : NULL;
	const cJSON *e = cJSON_IsArray(t) ? cJSON_GetArrayItem(t, page) : NULL;
	return cJSON_IsString(e) ? e->valuestring : NULL;
}

const cJSON *line_face(const cJSON *line, const cJSON *cast)
{
	const char *kind = step_kind(line);
	if (!kind || (strcmp(kind, "say") && strcmp(kind, "think")))
		return NULL;
	if (cJSON_IsTrue(get(line, "hide_face")))
		return NULL;
	const cJSON *f = get(line, kind);
	if (cJSON_IsString(f) && !strchr(f->valuestring, '.')) {
		const cJSON *e = get(cast, f->valuestring);
		return get(e, "face");
	}
	return f;
}

const char *line_plate(const cJSON *line, const cJSON *cast, bool *game)
{
	if (game)
		*game = false;
	const char *kind = step_kind(line);
	if (!kind || (strcmp(kind, "say") && strcmp(kind, "think")))
		return NULL;
	const cJSON *n = get(line, "name");
	if (cJSON_IsString(n))
		return n->valuestring;
	const cJSON *f = get(line, kind);
	if (cJSON_IsString(f) && !strchr(f->valuestring, '.')) {
		const cJSON *e = get(cast, f->valuestring);
		const cJSON *cn = get(e, "name");
		if (cJSON_IsString(cn))
			return cn->valuestring;
		f = get(e, "face");
	}
	if (cJSON_IsNumber(f)) {
		const char *who = media_face_name((int)f->valuedouble);
		if (who && game)
			*game = true;
		return who;
	}
	return NULL;
}

/* ---- the game's message frames ---- */

/* The frames the game registers (メッセージ枠登録) and what goes with each. */
struct frame {
	int cg, x, y;     /* the frame's CG at x, y (z 8101) */
	int tx, ty;       /* where the frame's text is registered, from the frame */
	int text_w;       /* the text's width the game registers */
	int lines;        /* the lines its box holds */
	int r, g, b;      /* the text's colour */
	bool edge;        /* a 1 pixel black edge round each letter */
	int plate;        /* the name plate: 1 at 216,526, 2 at 3,596, 0 none */
	int mark;         /* the click mark: 1 at 960,709, 2 at 930,725 */
	int buttons;      /* AUTO, SKIP, CONFIG at x 966: 1 from y 600 every 40, 2 from 656 every 30 */
};

static const struct frame frames[9] = {
	[1] = { 50011, 216, 568, 31, 18, 772, 4, 255, 255, 255, false, 1, 1, 1 },
	[2] = { 50012, 216, 568, 31, 18, 772, 4, 255, 255, 224, false, 1, 1, 1 },
	[3] = { 50013, 216, 568, 31, 18, 772, 4, 255, 255, 255, false, 0, 1, 1 },
	[4] = { 50013, 216, 568, 31, 13, 772, 4, 255, 255, 255, true, 0, 1, 1 },
	[6] = { 50016, 3, 638, 43, 7, 974, 3, 255, 255, 255, false, 2, 2, 2 },
	[7] = { 50017, 3, 638, 43, 7, 974, 3, 255, 255, 224, false, 2, 2, 2 },
	[8] = { 50018, 3, 638, 43, 7, 974, 3, 255, 255, 255, false, 0, 2, 2 },
};

int line_frame(const cJSON *line)
{
	const char *kind = step_kind(line);
	if (!kind)
		return 0;
	bool page = cJSON_IsTrue(get(line, "page"));
	if (!strcmp(kind, "say"))
		return page ? 6 : 1;
	if (!strcmp(kind, "think"))
		return page ? 7 : 2;
	if (!strcmp(kind, "narrate"))
		return cJSON_IsTrue(get(line, "paper")) ? 4 : page ? 8 : 3;
	return 0;
}

int line_limit(const cJSON *line)
{
	int f = line_frame(line);
	return f ? frames[f].lines : 4;
}

/* The width a line of the frame takes before the game breaks it: frame 1's, measured, in the
 * proportion of the widths the game registers. */
static float frame_room(const struct frame *f)
{
	return BOX_TEXT_W * (float)f->text_w / 772.0f;
}

/* ---- laying out a page ---- */

#define MAX_LAID 32

struct laid {
	int n;
	const char *a[MAX_LAID], *b[MAX_LAID];   /* each line's text */
	float indent;                            /* the opening quote's width: it hangs left of the text */
};

static float tw(const char *a, const char *b)
{
	return ImFont_CalcTextSizeA(F_GAME, BOX_TEXT_SIZE, FLT_MAX, 0, a, b, NULL).x;
}

static int utf8_len(const char *p)
{
	unsigned char c = (unsigned char)*p;
	if (c < 0x80)
		return 1;
	if (c >= 0xF0)
		return 4;
	if (c >= 0xE0)
		return 3;
	if (c >= 0xC0)
		return 2;
	return 1;
}

/* An opening quote: the game hangs it left of where the text starts, so every line of the page
 * starts at the same place. */
static int opening_quote(const char *s)
{
	static const char *const q[] = { "\xE2\x80\x9C", "\"", "\xE3\x80\x8C", "\xE3\x80\x8E", "\xE2\x80\x98", "(", "\xEF\xBC\x88" };
	for (size_t i = 0; i < sizeof q / sizeof q[0]; i++)
		if (!strncmp(s, q[i], strlen(q[i])))
			return (int)strlen(q[i]);
	return 0;
}

static void lay(const char *page, float width, struct laid *L)
{
	memset(L, 0, sizeof *L);
	int q = opening_quote(page);
	L->indent = q ? tw(page, page + q) : 0;
	const char *p = page;
	while (L->n < MAX_LAID) {
		const char *nl = strchr(p, '\n');
		const char *end = nl ? nl : p + strlen(p);
		/* One paragraph: as many words as fit on each line. */
		const char *a = p;
		do {
			float room = width + (L->n ? 0 : L->indent);
			const char *fit = a, *c = a;
			while (c < end) {
				const char *w = c;
				while (w < end && *w == ' ')
					w++;
				const char *we = w;
				while (we < end && *we != ' ')
					we++;
				if (tw(a, we) <= room) {
					fit = c = we;
					continue;
				}
				if (fit == a) {
					/* A word longer than the line: cut where it stops fitting. */
					const char *x = a;
					while (x < we && tw(a, x + utf8_len(x)) <= room)
						x += utf8_len(x);
					fit = x > a ? x : a + utf8_len(a);
				}
				break;
			}
			if (fit == a && a < end)
				fit = end;
			L->a[L->n] = a;
			L->b[L->n] = fit;
			L->n++;
			a = fit;
			while (a < end && *a == ' ')
				a++;
		} while (a < end && L->n < MAX_LAID);
		if (!nl)
			break;
		p = nl + 1;
	}
}

/* A page as the game shows it: its box draws a typed apostrophe as a closing quote (’), as the
 * game's shots show it ("game’s"), though the font's own apostrophe is straight. In a buffer of
 * its own, kept until the next call. */
static const char *as_shown(const char *page)
{
	static char *buf;
	static size_t cap;
	size_t n = 0;
	for (const char *p = page; *p; p++)
		n += *p == '\'' ? 3 : 1;
	if (n + 1 > cap) {
		cap = n + 64;
		buf = realloc(buf, cap);
	}
	char *o = buf;
	for (const char *p = page; *p; p++) {
		if (*p == '\'') {
			memcpy(o, "\xE2\x80\x99", 3);
			o += 3;
		} else {
			*o++ = *p;
		}
	}
	*o = 0;
	return buf;
}

/* The letters a page shows, as laid out (the spaces where a line breaks are not shown). */
static int letters_of(const struct laid *L)
{
	int n = 0;
	for (int i = 0; i < L->n; i++)
		for (const char *c = L->a[i]; c < L->b[i]; c += utf8_len(c))
			n++;
	return n;
}

/* The game's text speed: 字速度's milliseconds a letter, or the Config's middle speed. */
#define PLAYER_SPEED 23
#define LETTER_FADE 200.0f
static int letter_ms(const struct stage *s)
{
	return s->speed >= 0 ? s->speed : PLAYER_SPEED;
}

static ImU32 bad_ink;

int page_lines(const cJSON *line, const char *page)
{
	if (!page)
		return 0;
	int f = line_frame(line);
	struct laid L;
	lay(as_shown(page), frame_room(&frames[f ? f : 1]), &L);
	return L.n;
}

/* ---- drawing at the game's places ---- */

struct pen {
	ImDrawList *dl;
	V2 o;      /* the screen's top left */
	float k;   /* window pixels to a game pixel */
};

static V2 at(const struct pen *P, float x, float y)
{
	return v2(P->o.x + x * P->k, P->o.y + y * P->k);
}

static ImU32 rgba(int r, int g, int b, float a)
{
	return IM_COL32(r, g, b, (int)(a * 255 + 0.5f));
}

/* A picture at the game's place x, y, at its own size. */
static void pic(const struct pen *P, const struct picture *p, float x, float y)
{
	if (p->state != PIC_READY)
		return;
	ImDrawList_AddImage(P->dl, texref(p->tex), at(P, x, y), at(P, x + p->w, y + p->h), v2(0, 0), v2(1, 1), IM_COL32_WHITE);
}

/* Part of a picture (sx, sy, sw, sh in its pixels) at the game's place x, y. */
static void pic_part(const struct pen *P, const struct picture *p, float sx, float sy, float sw, float sh, float x, float y)
{
	if (p->state != PIC_READY || p->w <= 0 || p->h <= 0)
		return;
	V2 uv0 = v2(sx / p->w, sy / p->h), uv1 = v2((sx + sw) / p->w, (sy + sh) / p->h);
	ImDrawList_AddImage(P->dl, texref(p->tex), at(P, x, y), at(P, x + sw, y + sh), uv0, uv1, IM_COL32_WHITE);
}

static void layer(const struct pen *P, const cJSON *v, float x, float y)
{
	if (v)
		pic(P, media_picture(v), x, y);
}

/* Text in the game's font at the game's size `size`, its top left at x, y, `space` game pixels
 * between letters (the game's 字間隔), drawn twice a pixel apart for the game's bold. */
static float text_width(float size, float space, const char *s)
{
	float w = 0;
	int n = 0;
	for (const char *c = s; *c; c += utf8_len(c), n++)
		w += ImFont_CalcTextSizeA(F_GAME, size * GAME_SIZE_K, FLT_MAX, 0, c, c + utf8_len(c), NULL).x;
	return w + (n > 1 ? (n - 1) * space : 0);
}

static void game_text(const struct pen *P, float size, float space, bool bold, float x, float y, ImU32 ink, const char *s)
{
	float fs = size * GAME_SIZE_K;
	for (const char *c = s; *c; c += utf8_len(c)) {
		const char *e = c + utf8_len(c);
		ImDrawList_AddText_FontPtr(P->dl, F_GAME, fs * P->k, at(P, x, y), ink, c, e, 0, NULL);
		if (bold)
			ImDrawList_AddText_FontPtr(P->dl, F_GAME, fs * P->k, at(P, x + 1, y), ink, c, e, 0, NULL);
		x += ImFont_CalcTextSizeA(F_GAME, fs, FLT_MAX, 0, c, e, NULL).x + space;
	}
}

/* The name plate (T_MessageName::createSp): CG 50007, the name centred on it in size 26, made
 * smaller down to 12 until it fits 212 pixels, yellow, bold, its letters 3 closer, which with its
 * bold measures 1.25 closer here ("Magic" 51 pixels wide on the game's shots). */
#define PLATE_SPACE (-1.25f)
static void plate(const struct pen *P, int type, const char *name)
{
	float px = type == 2 ? 3 : 216, py = type == 2 ? 596 : 526;
	const struct picture *p = media_cg(50007);
	pic(P, p, px, py);
	if (!name || !name[0] || p->state != PIC_READY)
		return;
	float size = 26;
	while (size > 12 && text_width(size, PLATE_SPACE, name) > 212)
		size -= 1;
	float w = text_width(size, PLATE_SPACE, name);
	float h = size * GAME_SIZE_K;
	ImDrawList_PushClipRect(P->dl, at(P, px, py), at(P, px + p->w, py + p->h), true);
	game_text(P, size, PLATE_SPACE, true, px + (p->w - w) / 2, py + (p->h - h) / 2, IM_COL32(255, 245, 100, 255), name);
	ImDrawList_PopClipRect(P->dl);
}

static void message(const struct pen *P, const struct stage *s, int page)
{
	/* No frame until a line (and none after clear_text, the game's 枠消去). */
	if (!s->line)
		return;
	int fno = line_frame(s->line);
	if (!fno)
		fno = 1;
	const struct frame *f = &frames[fno];
	const char *kind = s->line ? step_kind(s->line) : NULL;
	bool talk = kind && (!strcmp(kind, "say") || !strcmp(kind, "think"));

	/* The frame (z 8101), the plate (8102), the face (8201) in its frame (8211), the buttons (8301). */
	pic(P, media_cg(f->cg), f->x, f->y);
	if (talk && f->plate) {
		bool game = false;
		const char *name = s->line_name ? s->line_name : line_plate(s->line, s->cast, &game);
		if (name && name[0])
			plate(P, f->plate, name);
	}
	/* The face shows in frames 1 and 2 only: the game draws none in the E frames. */
	const cJSON *face = talk && fno <= 2 && !s->face_off ? line_face(s->line, s->cast) : NULL;
	if (face) {
		layer(P, face, 12 + s->m.face_dx, 523 + s->m.face_dy);
		pic(P, media_cg(50006), 0, 511);
	}
	static const int button_cg[3] = { 50033, 50036, 50039 };
	for (int i = 0; i < 3; i++)
		pic(P, media_cg(button_cg[i]), 966, f->buttons == 2 ? 656 + 30 * i : 600 + 40 * i);

	/* The page; while a choice shows, the game's box stays with its plate and no text. */
	const char *text = s->line && !s->choice ? line_page(s->line, page) : NULL;
	if (!text)
		return;
	text = as_shown(text);
	struct laid L;
	lay(text, frame_room(f), &L);
	float k = P->k, size = BOX_TEXT_SIZE * k;
	float tx = f->x + f->tx + TEXT_DX, ty = f->y + f->ty + TEXT_DY;
	bad_ink = rgba(0xff, 0x8a, 0x73, 1);
	/* While the stage plays, each letter comes in one after another, fading in. */
	float ms = s->m.text_ms, every = (float)letter_ms(s);
	bool partial = s->m.playing && ms >= 0 && every > 0 && ms < letters_of(&L) * every - every + LETTER_FADE;
	int n = 0;
	for (int i = 0; i < L.n; i++) {
		bool over = i >= f->lines;
		ImU32 ink = over ? bad_ink : IM_COL32(f->r, f->g, f->b, 255);
		V2 a = at(P, tx - (i ? 0 : L.indent), ty + i * BOX_LINE_H);
		static const int d[8][2] = { { -1, -1 }, { 0, -1 }, { 1, -1 }, { -1, 0 }, { 1, 0 }, { -1, 1 }, { 0, 1 }, { 1, 1 } };
		if (!partial) {
			if (f->edge && !over)
				for (int j = 0; j < 8; j++)
					ImDrawList_AddText_FontPtr(P->dl, F_GAME, size, v2(a.x + d[j][0] * k, a.y + d[j][1] * k), IM_COL32(0, 0, 0, 255),
					                           L.a[i], L.b[i], 0, NULL);
			ImDrawList_AddText_FontPtr(P->dl, F_GAME, size, a, ink, L.a[i], L.b[i], 0, NULL);
		} else {
			for (const char *c = L.a[i]; c < L.b[i]; c += utf8_len(c), n++) {
				float alpha = (ms - n * every) / LETTER_FADE;
				if (alpha <= 0)
					break;
				if (alpha > 1)
					alpha = 1;
				const char *e = c + utf8_len(c);
				V2 q = v2(a.x + tw(L.a[i], c) * k, a.y);
				if (f->edge && !over)
					for (int j = 0; j < 8; j++)
						ImDrawList_AddText_FontPtr(P->dl, F_GAME, size, v2(q.x + d[j][0] * k, q.y + d[j][1] * k), rgba(0, 0, 0, alpha),
						                           c, e, 0, NULL);
				ImDrawList_AddText_FontPtr(P->dl, F_GAME, size, q, (ink & 0x00FFFFFFu) | ((ImU32)(alpha * 255 + 0.5f) << 24), c, e, 0, NULL);
			}
		}
		if (over && !partial) {
			/* Past the box: the game draws it there; the editor marks it. */
			float uy = a.y + size * 0.86f;
			ImDrawList_AddLine(P->dl, v2(a.x, uy), v2(a.x + tw(L.a[i], L.b[i]) * k, uy), bad_ink, fmaxf(1, k * 2));
		}
	}
	/* The click mark, after the page: five pictures, 200 ms each, round and round while it waits
	 * (still, the first). */
	if (L.n <= f->lines && !partial && !s->m.no_mark) {
		int no = 50021;
		if (s->m.playing && ms >= 0) {
			float since = ms - (every > 0 ? letters_of(&L) * every - every + LETTER_FADE : 0);
			no += since > 0 ? ((int)(since / 200)) % 5 : 0;
		}
		pic(P, media_cg(no), f->mark == 2 ? 930 : 960, f->mark == 2 ? 725 : 709);
	}
}

int stage_letters(const cJSON *line, const char *page)
{
	if (!page)
		return 0;
	int f = line_frame(line);
	struct laid L;
	lay(as_shown(page), frame_room(&frames[f ? f : 1]), &L);
	return letters_of(&L);
}

float stage_page_ms(const struct stage *s, int page)
{
	int every = letter_ms(s);
	const char *text = s->line ? line_page(s->line, page) : NULL;
	int n = stage_letters(s->line, text);
	return every > 0 && n > 0 ? (float)((n - 1) * every) + LETTER_FADE : 0;
}

/* ---- the choice menu (T_Select) ---- */

/* The space before a row, by the row above it (T_Select::getSpace, as RQ MOD's kit has it). */
static int menu_space(char kind, char prev)
{
	if (!prev)
		return 8;
	if (prev == 'b')
		return 6;
	if (prev == 't')
		return kind == 't' ? 0 : 8;
	return kind == 's' ? 1 : 6;
}

static const char *row_text(const cJSON *r)
{
	if (cJSON_IsString(r))
		return r->valuestring;
	const cJSON *t = get(r, "text");
	return cJSON_IsString(t) ? t->valuestring : "";
}

static bool is_sep(const cJSON *r)
{
	return cJSON_IsString(r) && !strcmp(r->valuestring, "-");
}

/* The menu as the game builds it: the base 10021 cut to the rows' height and its last 6 lines,
 * buttons 10022 (10029, taller, when fewer than 3 rows can be picked) at 18 in, separators 10023 at
 * 4 in; at the step's x, y, else the game's 300,360, moved up when it would run past the bottom. */
/* The menu's rows as last drawn, in game pixels, for a click on the stage. */
static struct { int n; int row[64]; float x[64], y[64], w[64], h[64]; } hits;

int stage_menu_hit(float gx, float gy)
{
	for (int i = 0; i < hits.n; i++)
		if (gx >= hits.x[i] && gx < hits.x[i] + hits.w[i] && gy >= hits.y[i] && gy < hits.y[i] + hits.h[i])
			return hits.row[i];
	return -1;
}

static void menu(const struct pen *P, const cJSON *choice)
{
	hits.n = 0;
	const cJSON *rows = get(choice, "choice");
	static cJSON *fn_row;
	if (!fn_row)
		fn_row = cJSON_CreateString("Rows from the mod's function");
	int n = cJSON_IsArray(rows) ? cJSON_GetArraySize(rows) : 1;
	int picks = 0;
	for (int i = 0; i < n; i++) {
		const cJSON *r = cJSON_IsArray(rows) ? cJSON_GetArrayItem(rows, i) : fn_row;
		if (!is_sep(r))
			picks++;
	}
	bool large = picks < 3;
	int bh = large ? 60 : 27;
	int h = 0;
	char prev = 0;
	for (int i = 0; i < n; i++) {
		const cJSON *r = cJSON_IsArray(rows) ? cJSON_GetArrayItem(rows, i) : fn_row;
		char kind = is_sep(r) ? 's' : 'b';
		h += menu_space(kind, prev) + (kind == 'b' ? bh : 7);
		prev = kind;
	}
	if (prev)
		h += menu_space(0, prev);
	if (h > 762)
		h = 762;
	const cJSON *jx = get(choice, "x"), *jy = get(choice, "y");
	int x = 300, y = 360;
	if (cJSON_IsNumber(jx)) {
		x = (int)jx->valuedouble;
		y = cJSON_IsNumber(jy) ? (int)jy->valuedouble : 360;
	}
	if (x < 0)
		x = (1024 - 376) / 2;
	if (y < 0)
		y = (768 - (h + 6 - 4)) / 2;
	else if (y > 768 - 6 - h)
		y = 768 - 6 - h > 0 ? 768 - 6 - h : 0;   /* RQ MOD moves a long menu up (2.56.0) */

	const struct picture *base = media_cg(10021);
	pic_part(P, base, 0, 0, 380, (float)h, (float)x, (float)y);
	pic_part(P, base, 0, 762, 380, 6, (float)x, (float)(y + h));
	const struct picture *btn = media_cg(large ? 10029 : 10022), *sep = media_cg(10023);
	float size = large ? 24 : 20;
	int at_y = 0;
	prev = 0;
	for (int i = 0; i < n; i++) {
		const cJSON *r = cJSON_IsArray(rows) ? cJSON_GetArrayItem(rows, i) : fn_row;
		char kind = is_sep(r) ? 's' : 'b';
		at_y += menu_space(kind, prev);
		prev = kind;
		if (at_y >= h)
			break;
		if (kind == 's') {
			pic(P, sep, (float)(x + 4), (float)(y + at_y));
			at_y += 7;
			continue;
		}
		pic_part(P, btn, 0, 0, 340, (float)bh, (float)(x + 18), (float)(y + at_y));
		if (hits.n < 64) {
			hits.row[hits.n] = i;
			hits.x[hits.n] = (float)(x + 18);
			hits.y[hits.n] = (float)(y + at_y);
			hits.w[hits.n] = 340;
			hits.h[hits.n] = (float)bh;
			hits.n++;
		}
		const char *text = row_text(r);
		int cr = 196, cg = 196, cb = 196;
		const cJSON *jr = get(r, "r"), *jg = get(r, "g"), *jb = get(r, "b");
		if (cJSON_IsNumber(jr) || cJSON_IsNumber(jg) || cJSON_IsNumber(jb)) {
			cr = cJSON_IsNumber(jr) ? (int)jr->valuedouble : 196;
			cg = cJSON_IsNumber(jg) ? (int)jg->valuedouble : 196;
			cb = cJSON_IsNumber(jb) ? (int)jb->valuedouble : 196;
		}
		float ts = size;
		while (ts > 12 && text_width(ts, 0, text) > 340 - 12)
			ts -= 1;
		float w = text_width(ts, 0, text);
		float th = ts * GAME_SIZE_K;
		game_text(P, ts, 0, false, x + 18 + (340 - w) / 2, y + at_y + (bh - th) / 2, IM_COL32(cr, cg, cb, 255), text);
		at_y += bh;
	}
}

void stage_paint(ImDrawList *dl, V2 o, float k, const struct stage *s, int page)
{
	V2 end = v2(o.x + 1024 * k, o.y + 768 * k);
	ImDrawList_PushClipRect(dl, o, end, true);
	/* Under a shake the screen moves whole, black where it left. */
	ImDrawList_AddRectFilled(dl, o, end, IM_COL32(0, 0, 0, 255), 0, 0);
	struct pen P = { dl, v2(o.x + s->m.dx * k, o.y + s->m.dy * k), k };

	/* The screen's colour, then the layers by their z: the wallpaper (8001), the mask wallpaper
	 * (8011), the scene CG (8021), the background at 0,39 (8031), the small CG at 513,271 (8041),
	 * the standing picture (8051). */
	ImU32 base = s->screen == 'w' ? IM_COL32(255, 255, 255, 255) : s->screen == 'c' ? s->screen_col : IM_COL32(0, 0, 0, 255);
	ImDrawList_AddRectFilled(dl, at(&P, 0, 0), at(&P, 1024, 768), base, 0, 0);
	layer(&P, s->layer[L_WALL], 0, 0);
	if (s->layer[L_MASK])
		layer(&P, s->layer[L_MASK], 0, 0);
	else if (s->mask_cg > 0)
		pic(&P, media_cg(s->mask_cg), 0, 0);
	layer(&P, s->layer[L_CG], 0, 0);
	layer(&P, s->layer[L_BG], s->m.bg_x, 39);
	layer(&P, s->layer[L_MINI], 513, 271);
	layer(&P, s->layer[L_PORTRAIT], s->m.portrait_dx, s->m.portrait_dy);

	message(&P, s, page);
	if (s->choice)
		menu(&P, s->choice);
	if (s->m.white > 0)
		ImDrawList_AddRectFilled(dl, o, end, rgba(255, 255, 255, s->m.white > 1 ? 1 : s->m.white), 0, 0);
	ImDrawList_PopClipRect(dl);
}

/* The stage's place at the cursor, w wide: its top left. */
static V2 stage_place(float w, float *H)
{
	V2 o = igGetCursorScreenPos();
	o.x = floorf(o.x);
	o.y = floorf(o.y);
	*H = floorf(w * 0.75f);
	return o;
}

void stage_draw(const struct stage *s, int page, float w)
{
	ImDrawList *dl = igGetWindowDrawList();
	float H;
	V2 o = stage_place(w, &H);
	stage_paint(dl, o, w / 1024.0f, s, page);
	ImDrawList_AddRect(dl, o, v2(o.x + w, o.y + H), col(C_BORDER), S(4), S(1), 0);
	igDummy(v2(w, H));
}

void stage_draw_texture(unsigned tex, float w)
{
	ImDrawList *dl = igGetWindowDrawList();
	float H;
	V2 o = stage_place(w, &H);
	ImDrawList_AddImage(dl, texref(tex), o, v2(o.x + w, o.y + H), v2(0, 0), v2(1, 1), IM_COL32_WHITE);
	ImDrawList_AddRect(dl, o, v2(o.x + w, o.y + H), col(C_BORDER), S(4), S(1), 0);
	igDummy(v2(w, H));
}
