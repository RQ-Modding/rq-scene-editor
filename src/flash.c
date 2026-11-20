/* A small player of the game's Flash files, enough for the world map's place tag: the timeline's
 * display list by depth (place, move, remove), sprites of one object, shapes filled with a bitmap,
 * a place's matrix and the colour transform's multiply terms, at the file's frame rate, looping.
 *
 * After RQ MOD's engine: xsystem4's src/parts/flash.c (Copyright (C) 2023 kichikuou) and
 * src/parts/render.c (Copyright (C) 2019 Nunuhara Cabbage), both GPL-2.0-or-later; on Rance Browser's
 * reader of the files (swf.h). Drawn with Dear ImGui: a shape is its rectangle with the whole bitmap
 * on it, turned by the matrices; the colour transform's add terms and the blend modes other than
 * normal are left out (Dear ImGui's drawing has no place for them). */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "system4.h"

#include "editor.h"
#include "swf.h"

#define MAX_CHARS 256
#define MAX_OBJS 64

struct fobj {
	int depth, id;
	float m[6];          /* x' = m0 x + m2 y + m4, y' = m1 x + m3 y + m5 */
	float mul[4];        /* r, g, b, a */
};

struct fchar {
	int id;
	struct swf_tag *tag;
	unsigned tex;        /* a bitmap's */
	int w, h;
	bool has_obj;        /* a sprite's one object */
	struct fobj obj;
};

struct flash {
	char base[128];
	uint8_t *raw;
	struct swf *swf;
	struct fchar chars[MAX_CHARS];
	int nchars;
	struct fobj list[MAX_OBJS];
	int nlist;
	int frame;           /* frames shown so far */
	struct swf_tag *tag; /* the next tag of the timeline */
	bool ended;
	bool failed;
};

static struct flash files[4];

static struct fchar *char_of(struct flash *f, int id)
{
	for (int i = 0; i < f->nchars; i++)
		if (f->chars[i].id == id)
			return &f->chars[i];
	return NULL;
}

static struct fchar *define(struct flash *f, int id, struct swf_tag *t)
{
	struct fchar *c = char_of(f, id);
	if (c)
		return c;
	if (f->nchars >= MAX_CHARS)
		return NULL;
	c = &f->chars[f->nchars++];
	memset(c, 0, sizeof *c);
	c->id = id;
	c->tag = t;
	return c;
}

static void obj_set(struct fobj *o, const struct swf_tag_place_object *p)
{
	if (p->flags & PLACE_FLAG_HAS_CHARACTER) {
		o->id = p->character_id;
		o->m[0] = 1; o->m[1] = 0; o->m[2] = 0; o->m[3] = 1; o->m[4] = 0; o->m[5] = 0;
		o->mul[0] = o->mul[1] = o->mul[2] = o->mul[3] = 1;
	}
	if (p->flags & PLACE_FLAG_HAS_MATRIX) {
		o->m[0] = fixed32_to_float(p->matrix.scale_x);
		o->m[1] = fixed32_to_float(p->matrix.rotate_skew_0);
		o->m[2] = fixed32_to_float(p->matrix.rotate_skew_1);
		o->m[3] = fixed32_to_float(p->matrix.scale_y);
		o->m[4] = twips_to_float(p->matrix.translate_x);
		o->m[5] = twips_to_float(p->matrix.translate_y);
	}
	if (p->flags & PLACE_FLAG_HAS_COLOR_TRANSFORM)
		for (int i = 0; i < 4; i++)
			o->mul[i] = fixed16_to_float(p->color_transform.mult_terms[i]);
}

static void place(struct flash *f, const struct swf_tag_place_object *p)
{
	int i = 0;
	while (i < f->nlist && f->list[i].depth < p->depth)
		i++;
	if (i < f->nlist && f->list[i].depth == p->depth) {
		obj_set(&f->list[i], p);
		return;
	}
	if (!(p->flags & PLACE_FLAG_HAS_CHARACTER) || f->nlist >= MAX_OBJS)
		return;
	memmove(&f->list[i + 1], &f->list[i], (size_t)(f->nlist - i) * sizeof f->list[0]);
	f->nlist++;
	memset(&f->list[i], 0, sizeof f->list[i]);
	f->list[i].depth = p->depth;
	obj_set(&f->list[i], p);
}

static void remove_at(struct flash *f, int depth)
{
	for (int i = 0; i < f->nlist; i++) {
		if (f->list[i].depth == depth) {
			memmove(&f->list[i], &f->list[i + 1], (size_t)(f->nlist - i - 1) * sizeof f->list[0]);
			f->nlist--;
			return;
		}
	}
}

static void rewind_file(struct flash *f)
{
	f->frame = 0;
	f->tag = f->swf ? f->swf->tags : NULL;
	f->nlist = 0;
	f->ended = false;
}

/* The timeline up to `frame` frames shown (parts_flash_seek). */
static void seek(struct flash *f, int frame, int depth)
{
	if (frame < f->frame)
		rewind_file(f);
	struct swf_tag *t;
	for (t = f->tag; t && f->frame < frame; t = t->next) {
		switch (t->type) {
		case TAG_END:
			f->ended = true;
			break;
		case TAG_SHOW_FRAME:
			f->frame++;
			break;
		case TAG_DEFINE_BITS_LOSSLESS:
		case TAG_DEFINE_BITS_LOSSLESS2: {
			struct swf_tag_define_bits_lossless *b = (struct swf_tag_define_bits_lossless *)t;
			struct fchar *c = define(f, b->character_id, t);
			if (c && !c->tex) {
				c->tex = tex_from_rgba(b->data, b->width, b->height);
				c->w = b->width;
				c->h = b->height;
			}
			break;
		}
		case TAG_DEFINE_SHAPE:
			define(f, ((struct swf_tag_define_shape *)t)->shape_id, t);
			break;
		case TAG_DEFINE_SPRITE: {
			struct swf_tag_define_sprite *sp = (struct swf_tag_define_sprite *)t;
			struct fchar *c = define(f, sp->sprite_id, t);
			if (c && !c->has_obj) {
				for (struct swf_tag *s = sp->tags; s && s->type != TAG_END; s = s->next) {
					if (s->type == TAG_PLACE_OBJECT2 || s->type == TAG_PLACE_OBJECT3) {
						memset(&c->obj, 0, sizeof c->obj);
						obj_set(&c->obj, (struct swf_tag_place_object *)s);
						c->has_obj = true;
						break;
					}
				}
			}
			break;
		}
		case TAG_DO_ACTION: {
			int go = -1;
			for (struct swf_action *a = ((struct swf_tag_do_action *)t)->actions; a && a->code != ACTION_END; a = a->next) {
				if (a->code == ACTION_STOP)
					f->ended = true;
				else if (a->code == ACTION_PLAY)
					f->ended = false;
				else if (a->code == ACTION_GOTO_FRAME)
					go = ((struct swf_action_goto_frame *)a)->frame;
			}
			if (go >= 0 && depth < 4) {
				f->tag = t->next;
				seek(f, go < 1 ? 1 : go, depth + 1);
				return;
			}
			break;
		}
		case TAG_PLACE_OBJECT2:
		case TAG_PLACE_OBJECT3:
			place(f, (struct swf_tag_place_object *)t);
			break;
		case TAG_REMOVE_OBJECT2:
			remove_at(f, ((struct swf_tag_remove_object2 *)t)->depth);
			break;
		default:
			break;
		}
	}
	f->tag = t;
}

static struct flash *open_file(const char *base)
{
	for (int i = 0; i < 4; i++)
		if (!strcmp(files[i].base, base))
			return files[i].failed ? NULL : &files[i];
	struct flash *f = NULL;
	for (int i = 0; i < 4 && !f; i++)
		if (!files[i].base[0])
			f = &files[i];
	if (!f)
		return NULL;
	memset(f, 0, sizeof *f);
	snprintf(f->base, sizeof f->base, "%s", base);
	f->failed = true;
	for (int i = 0; i < G.nflash; i++) {
		if (strcmp(G.flash[i].base, base))
			continue;
		size_t size = 0;
		f->raw = game_read_flash(&G.h, i, &size);
		if (f->raw)
			f->swf = aff_load(f->raw, size);
		break;
	}
	if (!f->swf)
		return NULL;
	f->failed = false;
	rewind_file(f);
	return f;
}

static V2 apply(const float *m, float x, float y)
{
	return v2(m[0] * x + m[2] * y + m[4], m[1] * x + m[3] * y + m[5]);
}

static void mul_m(const float *a, const float *b, float *out)
{
	/* out = a * b: b first, then a. */
	float r[6] = {
		a[0] * b[0] + a[2] * b[1], a[1] * b[0] + a[3] * b[1],
		a[0] * b[2] + a[2] * b[3], a[1] * b[2] + a[3] * b[3],
		a[0] * b[4] + a[2] * b[5] + a[4], a[1] * b[4] + a[3] * b[5] + a[5],
	};
	memcpy(out, r, sizeof r);
}

static void draw_shape(struct flash *f, ImDrawList *dl, V2 o, float k, float x, float y, const float *m, const float *mul,
                       const struct swf_tag_define_shape *s)
{
	struct fchar *b = char_of(f, s->fill_style.bitmap_id);
	if (!b || !b->tex)
		return;
	float x0 = twips_to_float(s->bounds.x_min), x1 = twips_to_float(s->bounds.x_max);
	float y0 = twips_to_float(s->bounds.y_min), y1 = twips_to_float(s->bounds.y_max);
	V2 c[4] = { apply(m, x0, y0), apply(m, x1, y0), apply(m, x1, y1), apply(m, x0, y1) };
	for (int i = 0; i < 4; i++)
		c[i] = v2(o.x + (x + c[i].x) * k, o.y + (y + c[i].y) * k);
	float r = fminf(fmaxf(mul[0], 0), 1), g = fminf(fmaxf(mul[1], 0), 1), bl = fminf(fmaxf(mul[2], 0), 1), a = fminf(fmaxf(mul[3], 0), 1);
	ImU32 col = IM_COL32((int)(r * 255), (int)(g * 255), (int)(bl * 255), (int)(a * 255));
	ImDrawList_AddImageQuad(dl, texref(b->tex), c[0], c[1], c[2], c[3], v2(0, 0), v2(1, 0), v2(1, 1), v2(0, 1), col);
}

bool flash_draw(ImDrawList *dl, V2 o, float k, const char *base, float x, float y, double ms)
{
	struct flash *f = open_file(base);
	if (!f)
		return false;
	float fps = f->swf->frame_rate / 256.0f;
	int count = f->swf->frame_count > 0 ? f->swf->frame_count : 1;
	int frame = fps > 0 ? (int)(ms * fps / 1000.0) : 0;
	/* Round and round: the first frame again after the last. */
	frame = frame % count + 1;
	seek(f, frame, 0);
	for (int i = 0; i < f->nlist; i++) {
		struct fobj *ob = &f->list[i];
		struct fchar *c = char_of(f, ob->id);
		if (!c)
			continue;
		if (c->tag->type == TAG_DEFINE_SHAPE) {
			draw_shape(f, dl, o, k, x, y, ob->m, ob->mul, (struct swf_tag_define_shape *)c->tag);
		} else if (c->tag->type == TAG_DEFINE_SPRITE && c->has_obj) {
			struct fchar *child = char_of(f, c->obj.id);
			if (!child || child->tag->type != TAG_DEFINE_SHAPE)
				continue;
			float m[6], mul[4];
			mul_m(ob->m, c->obj.m, m);
			for (int j = 0; j < 4; j++)
				mul[j] = ob->mul[j] * c->obj.mul[j];
			draw_shape(f, dl, o, k, x, y, m, mul, (struct swf_tag_define_shape *)child->tag);
		}
	}
	return true;
}
