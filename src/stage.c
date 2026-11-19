/* The stage sketch: the scene as the game shows it at a step, drawn small above the step's fields.
 *
 * The steps are followed from the file's start to the selected one (into the branch that holds it;
 * a branch passed on the way is not entered, a goto not followed): each picture step sets its
 * layer, a line goes into the message box. The box, the face's frame and the name plate are drawn
 * where the game puts them on its 1024 by 768 screen, and a page is laid out in the game's font as
 * the game lays it out: words that do not fit go to the next line, the lines after an opening
 * quote start under its first letter, and a page holds 4 lines (a 5th is drawn past the box, here
 * in red). Play shows the truth; this is a sketch. */
#include <float.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "system4.h"

#include "editor.h"

/* The width a line of the box takes before the game breaks it, at 1024 by 768. Measured on the
 * game's shots of a long line: the first line held 611 pixels and not 715 with the next word, the
 * third (under the quote) 681 and not 740. */
#define BOX_TEXT_W 700.0f

/* Dear ImGui sizes a font by its whole height, which is 1.362 times the em of the game's font: its
 * sizes here are the game's pixel sizes times this. */
#define GAME_EM 1.362f

/* ---- following the steps ---- */

static const cJSON *get(const cJSON *o, const char *k)
{
	return cJSON_IsObject(o) ? cJSON_GetObjectItemCaseSensitive(o, k) : NULL;
}

static void apply(struct stage *s, const cJSON *st)
{
	const char *kind = step_kind(st);
	if (!kind)
		return;
	const cJSON *v = get(st, kind);
	if (!strcmp(kind, "background") || !strcmp(kind, "background_in")) {
		s->layer[L_BG] = v;
		s->screen = 0;
	} else if (!strcmp(kind, "background_out")) {
		s->layer[L_BG] = NULL;
	} else if (!strcmp(kind, "wallpaper")) {
		s->layer[L_WALL] = v;
		s->screen = 0;
	} else if (!strcmp(kind, "mask_wallpaper")) {
		s->layer[L_MASK] = v;
	} else if (!strcmp(kind, "portrait")) {
		s->layer[L_PORTRAIT] = v;
	} else if (!strcmp(kind, "mini")) {
		s->layer[L_MINI] = v;
	} else if (!strcmp(kind, "cg")) {
		s->layer[L_CG] = v;
		s->screen = 0;
	} else if (!strcmp(kind, "set")) {
		static const char *const keys[] = { "wallpaper", "background", "portrait", "mini", "cg" };
		static const int layers[] = { L_WALL, L_BG, L_PORTRAIT, L_MINI, L_CG };
		for (int i = 0; i < 5; i++)
			s->layer[layers[i]] = get(v, keys[i]);
		s->screen = 0;
	} else if (!strcmp(kind, "black")) {
		s->screen = 'b';
	} else if (!strcmp(kind, "white")) {
		s->screen = 'w';
	} else if (!strcmp(kind, "clear")) {
		for (int i = 0; i < L_COUNT; i++)
			s->layer[i] = NULL;
		s->screen = 0;
		s->line = NULL;
	} else if (!strcmp(kind, "say") || !strcmp(kind, "think") || !strcmp(kind, "narrate")) {
		s->line = st;
	} else if (!strcmp(kind, "clear_text")) {
		s->line = NULL;
	}
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

void stage_walk(const cJSON *root, const char *place, struct stage *s)
{
	memset(s, 0, sizeof *s);
	s->cast = get(root, "cast");
	const cJSON *list = get(root, "steps");
	const char *p = place ? place : "";
	int k;
	if (!cJSON_IsArray(list) || !take(&p, "steps[", &k))
		return;
	for (;;) {
		const cJSON *st = list->child;
		for (int i = 0; st && i < k; i++, st = st->next)
			apply(s, st);
		if (!st)
			return;
		if (!*p) {
			apply(s, st);
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

/* ---- laying out a page ---- */

#define MAX_LAID 32

struct laid {
	int n;
	const char *a[MAX_LAID], *b[MAX_LAID];   /* each line's text */
	float indent;                            /* the lines after the first start this far in */
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

/* An opening quote: the game starts the next lines under the letter after it. */
static int opening_quote(const char *s)
{
	static const char *const q[] = { "\xE2\x80\x9C", "\"", "\xE3\x80\x8C", "\xE3\x80\x8E", "\xE2\x80\x98", "(", "\xEF\xBC\x88" };
	for (size_t i = 0; i < sizeof q / sizeof q[0]; i++)
		if (!strncmp(s, q[i], strlen(q[i])))
			return (int)strlen(q[i]);
	return 0;
}

static void lay(const char *page, struct laid *L)
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
			float room = BOX_TEXT_W - (L->n ? L->indent : 0);
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

int page_lines(const char *page)
{
	if (!page)
		return 0;
	struct laid L;
	lay(page, &L);
	return L.n;
}

/* ---- drawing ---- */

static ImU32 rgba(int r, int g, int b, float a)
{
	return IM_COL32(r, g, b, (int)(a * 255 + 0.5f));
}

/* A picture into a box: cover (fills it, cut to the top when top is set) or contain. */
static void picture_in(ImDrawList *dl, const cJSON *v, V2 a, V2 b, bool cover, bool top, float round)
{
	const struct picture *p = media_picture(v);
	if (p->state != PIC_READY)
		return;
	float bw = b.x - a.x, bh = b.y - a.y;
	float sx = bw / p->w, sy = bh / p->h;
	float s = cover ? fmaxf(sx, sy) : fminf(sx, sy);
	float dw = p->w * s, dh = p->h * s;
	if (cover) {
		/* The part of the picture inside the box, in UVs. */
		float u0 = (dw - bw) / 2 / dw, u1 = 1 - u0;
		float v0 = top ? 0 : (dh - bh) / 2 / dh, v1 = v0 + bh / dh;
		ImDrawList_AddImageRounded(dl, texref(p->tex), a, b, v2(u0, v0), v2(u1, v1), IM_COL32_WHITE, round, 0);
	} else {
		V2 c = v2(a.x + (bw - dw) / 2, a.y + (bh - dh) / 2);
		ImDrawList_AddImage(dl, texref(p->tex), c, v2(c.x + dw, c.y + dh), v2(0, 0), v2(1, 1), IM_COL32_WHITE);
	}
}

/* A picture at the game's own size (k: screen pixels per game pixel), centred at x, its bottom at y. */
static void picture_at(ImDrawList *dl, const cJSON *v, float x, float y, float k, bool centre_y)
{
	const struct picture *p = media_picture(v);
	if (p->state != PIC_READY)
		return;
	float dw = p->w * k, dh = p->h * k;
	V2 a = v2(x - dw / 2, centre_y ? y - dh / 2 : y - dh);
	ImDrawList_AddImage(dl, texref(p->tex), a, v2(a.x + dw, a.y + dh), v2(0, 0), v2(1, 1), IM_COL32_WHITE);
}

static void frame_rect(ImDrawList *dl, V2 a, V2 b, ImU32 c, float th, float round)
{
	ImDrawList_AddRect(dl, v2(a.x - th / 2, a.y - th / 2), v2(b.x + th / 2, b.y + th / 2), c, round, th, 0);
}

static void choice_rows(ImDrawList *dl, const cJSON *choice, V2 o, float w, float H)
{
	const cJSON *rows = get(choice, "choice");
	float x0 = o.x + w * 0.17f, x1 = o.x + w * 0.83f;
	float y = o.y + H * 0.15f;
	float size = w * 0.026f * GAME_EM, rh = w * 0.052f, gap = w * 0.012f;
	if (!cJSON_IsArray(rows)) {
		const char *msg = "Rows from the mod's function";
		float mw = ImFont_CalcTextSizeA(F_GAME, size, FLT_MAX, 0, msg, NULL, NULL).x;
		ImDrawList_AddRectFilled(dl, v2(x0, y), v2(x1, y + rh), rgba(14, 30, 24, 0.9f), w * 0.006f, 0);
		ImDrawList_AddText_FontPtr(dl, F_GAME, size, v2((x0 + x1 - mw) / 2, y + (rh - size) / 2), rgba(255, 255, 255, 0.7f),
		                           msg, NULL, 0, NULL);
		return;
	}
	for (const cJSON *r = rows->child; r && y < o.y + H * 0.7f; r = r->next) {
		if (cJSON_IsString(r) && !strcmp(r->valuestring, "-")) {
			ImDrawList_AddRectFilled(dl, v2(x0 + (x1 - x0) * 0.1f, y), v2(x1 - (x1 - x0) * 0.1f, y + w * 0.003f),
			                         rgba(183, 178, 143, 0.6f), 0, 0);
			y += w * 0.003f + gap;
			continue;
		}
		const char *text = cJSON_IsString(r) ? r->valuestring : NULL;
		const cJSON *t = get(r, "text");
		if (cJSON_IsString(t))
			text = t->valuestring;
		ImU32 ink = rgba(255, 255, 255, 1);
		const cJSON *cr = get(r, "r"), *cg = get(r, "g"), *cb = get(r, "b");
		if (cJSON_IsNumber(cr) || cJSON_IsNumber(cg) || cJSON_IsNumber(cb))
			ink = IM_COL32(cJSON_IsNumber(cr) ? (int)cr->valuedouble : 255, cJSON_IsNumber(cg) ? (int)cg->valuedouble : 255,
			               cJSON_IsNumber(cb) ? (int)cb->valuedouble : 255, 255);
		ImDrawList_AddRectFilled(dl, v2(x0, y), v2(x1, y + rh), rgba(14, 30, 24, 0.9f), w * 0.006f, 0);
		frame_rect(dl, v2(x0, y), v2(x1, y + rh), rgba(183, 178, 143, 1), fmaxf(1, w * 0.0025f), w * 0.006f);
		if (text) {
			float tw2 = ImFont_CalcTextSizeA(F_GAME, size, FLT_MAX, 0, text, NULL, NULL).x;
			float tx = (x0 + x1 - tw2) / 2;
			if (tx < x0 + size)
				tx = x0 + size;
			ImDrawList_PushClipRect(dl, v2(x0, y), v2(x1, y + rh), true);
			ImDrawList_AddText_FontPtr(dl, F_GAME, size, v2(tx, y + (rh - size) / 2), ink, text, NULL, 0, NULL);
			ImDrawList_PopClipRect(dl);
		}
		if (get(r, "if")) {
			float ms = w * 0.015f;
			const char *m = "if";
			float mw = ImFont_CalcTextSizeA(F_BOLD, ms, FLT_MAX, 0, m, NULL, NULL).x;
			V2 ma = v2(x1 - w * 0.012f - mw - w * 0.016f, y + (rh - ms * 1.6f) / 2);
			V2 mb = v2(x1 - w * 0.012f, ma.y + ms * 1.6f);
			frame_rect(dl, ma, mb, rgba(201, 232, 176, 1), fmaxf(1, w * 0.0015f), w * 0.004f);
			ImDrawList_AddText_FontPtr(dl, F_BOLD, ms, v2(ma.x + w * 0.008f, ma.y + ms * 0.3f), rgba(201, 232, 176, 1), m, NULL, 0, NULL);
		}
		y += rh + gap;
	}
}

void stage_draw(const struct stage *s, int page, float w)
{
	ImDrawList *dl = igGetWindowDrawList();
	V2 o = igGetCursorScreenPos();
	o.x = floorf(o.x);
	o.y = floorf(o.y);
	float H = floorf(w * 0.75f);
	float k = w / 1024.0f;
	V2 end = v2(o.x + w, o.y + H);
	ImDrawList_PushClipRect(dl, o, end, true);

	/* The wall: the wallpaper, else the game's green. */
	ImDrawList_AddRectFilledMultiColor(dl, o, end, rgba(0x2b, 0x4a, 0x37, 1), rgba(0x1d, 0x33, 0x26, 1),
	                                   rgba(0x0f, 0x1d, 0x15, 1), rgba(0x1d, 0x33, 0x26, 1));
	if (s->layer[L_WALL])
		picture_in(dl, s->layer[L_WALL], o, end, true, false, 0);

	/* The background in its frame. */
	V2 ba = v2(o.x + w * 0.022f, o.y + H * 0.065f), bb = v2(ba.x + w * 0.956f, ba.y + H * 0.58f);
	if (s->layer[L_BG]) {
		frame_rect(dl, ba, bb, rgba(0x3b, 0x4a, 0x3a, 1), w * 0.009f, w * 0.015f);
		frame_rect(dl, ba, bb, rgba(0xb7, 0xb2, 0x8f, 1), w * 0.005f, w * 0.015f);
		picture_in(dl, s->layer[L_BG], ba, bb, true, false, w * 0.015f);
	}
	if (s->layer[L_MINI])
		picture_at(dl, s->layer[L_MINI], (ba.x + bb.x) / 2, (ba.y + bb.y) / 2, k, true);
	if (s->layer[L_PORTRAIT])
		picture_at(dl, s->layer[L_PORTRAIT], o.x + w / 2, o.y + H * 0.745f, k, false);
	if (s->layer[L_CG])
		picture_in(dl, s->layer[L_CG], o, end, false, false, 0);
	if (s->screen)
		ImDrawList_AddRectFilled(dl, o, end, s->screen == 'w' ? rgba(250, 250, 250, 0.96f) : rgba(0, 0, 0, 0.96f), 0, 0);

	/* The message box, the face and the plate. */
	const char *kind = s->line ? step_kind(s->line) : NULL;
	bool narr = kind && !strcmp(kind, "narrate");
	bool paper = narr && cJSON_IsTrue(get(s->line, "paper"));
	V2 xa = v2(o.x + w * 0.213f, o.y + H * 0.745f), xb = v2(xa.x + w * 0.725f, xa.y + H * 0.23f);
	ImDrawList_AddRectFilled(dl, xa, xb, paper ? rgba(0xe9, 0xdf, 0xc4, 0.95f) : rgba(22, 48, 46, 0.86f), w * 0.006f, 0);
	frame_rect(dl, xa, xb, rgba(0x8d, 0x8a, 0x70, 1), fmaxf(1, w * 0.0025f), w * 0.006f);
	static const char *const side[] = { "AUTO", "SKIP", "CONFIG" };
	float ss = w * 0.0145f;
	for (int i = 0; i < 3; i++)
		ImDrawList_AddText_FontPtr(dl, F_BOLD, ss, v2(o.x + w * 0.946f, o.y + H * 0.77f + i * (ss + w * 0.013f)),
		                           rgba(0xbf, 0xe9, 0xb6, 1), side[i], NULL, 0, NULL);
	if (s->line && !narr) {
		const cJSON *face = line_face(s->line, s->cast);
		if (face) {
			V2 fa = v2(o.x + w * 0.012f, o.y + H * 0.682f), fb = v2(fa.x + w * 0.191f, fa.y + H * 0.302f);
			frame_rect(dl, fa, fb, rgba(0x2d, 0x24, 0x14, 1), w * 0.008f, w * 0.006f);
			frame_rect(dl, fa, fb, rgba(0xb5, 0x9a, 0x5c, 1), w * 0.0045f, w * 0.006f);
			ImDrawList_AddRectFilled(dl, fa, fb, rgba(0x1d, 0x2a, 0x22, 1), w * 0.006f, 0);
			picture_in(dl, face, fa, fb, true, true, w * 0.006f);
		}
		bool game = false;
		const char *plate = s->line_name ? s->line_name : line_plate(s->line, s->cast, &game);
		V2 pa = v2(o.x + w * 0.213f, o.y + H * 0.688f), pb = v2(pa.x + w * 0.213f, pa.y + H * 0.049f);
		ImDrawList_AddRectFilled(dl, pa, pb, rgba(18, 40, 32, 0.88f), w * 0.005f, 0);
		frame_rect(dl, pa, pb, rgba(0x8d, 0x8a, 0x70, 1), fmaxf(1, w * 0.0025f), w * 0.005f);
		if (plate) {
			float ps = w * 0.0195f * GAME_EM;   /* the game's plate: 20 pixels */
			float pw = ImFont_CalcTextSizeA(F_GAME, ps, FLT_MAX, 0, plate, NULL, NULL).x;
			ImDrawList_PushClipRect(dl, pa, pb, true);
			ImDrawList_AddText_FontPtr(dl, F_GAME, ps, v2((pa.x + pb.x - pw) / 2, pa.y + (pb.y - pa.y - ps) / 2),
			                           rgba(0xf1, 0xe2, 0x7a, game ? 0.75f : 1), plate, NULL, 0, NULL);
			ImDrawList_PopClipRect(dl);
		}
	}

	/* The page. */
	const char *text = s->line ? line_page(s->line, page) : NULL;
	if (text) {
		struct laid L;
		lay(text, &L);
		float size = BOX_TEXT_SIZE * k, lh = BOX_LINE_H * k;
		float tx = o.x + w * 0.227f, ty = o.y + H * 0.762f;
		ImU32 ink = paper ? rgba(0x2a, 0x22, 0x16, 1) : rgba(255, 255, 255, 1);
		for (int i = 0; i < L.n; i++) {
			bool over = i >= 4;
			V2 at = v2(tx + (i ? L.indent * k : 0), ty + i * lh);
			if (!paper && !over)
				ImDrawList_AddText_FontPtr(dl, F_GAME, size, v2(at.x, at.y + k * 2), rgba(0, 0, 0, 0.6f), L.a[i], L.b[i], 0, NULL);
			ImDrawList_AddText_FontPtr(dl, F_GAME, size, at, over ? rgba(0xff, 0x8a, 0x73, 1) : ink, L.a[i], L.b[i], 0, NULL);
			if (over) {
				float lw = tw(L.a[i], L.b[i]) * k;
				float uy = at.y + size * 0.86f;   /* under the baseline */
				ImDrawList_AddLine(dl, v2(at.x, uy), v2(at.x + lw, uy), rgba(0xff, 0x8a, 0x73, 1), fmaxf(1, k * 2));
			}
		}
	}
	if (s->choice)
		choice_rows(dl, s->choice, o, w, H);
	ImDrawList_PopClipRect(dl);
	ImDrawList_AddRect(dl, o, end, col(C_BORDER), S(4), S(1), 0);
	igDummy(v2(w, H));
}
