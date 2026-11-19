/* The game's pictures, music and names for the window.
 *
 * Small pictures in the list come from Rance Browser's thumbnail thread. The stage sketch draws whole
 * pictures: a CG of the game or a picture in the mod's folder (PNG, WebP, JPEG), read on the window's
 * thread a few a frame and kept while they are drawn. rq.ids gives a face's character and a track's
 * name; a track or a sound plays through Rance Browser's player. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "system4.h"
#include "system4/cg.h"
#include "system4/file.h"

#include "editor.h"

#define CACHE 48
#define LOADS_PER_FRAME 3

struct slot {
	bool used;
	int no;              /* a CG number, when path is "" */
	char path[600];      /* a picture in the mod's folder */
	struct picture p;
	unsigned last;
};

static struct slot cache[CACHE];
static unsigned frame_no;
static int loads_left = LOADS_PER_FRAME;
static bool waiting;     /* a picture wanted this frame is read next frame */
static bool thumbs_on;
static char mod_dir[1024];

struct face_of { int cg; const char *name; };
static struct face_of faces[512];
static int nfaces;

static int face_cmp(const void *a, const void *b)
{
	return ((const struct face_of *)a)->cg - ((const struct face_of *)b)->cg;
}

void media_init(bool thumbs)
{
	thumbs_on = thumbs;
	if (thumbs)
		pics_init();
	nfaces = 0;
	for (int k = 0; k < G.nkinds; k++) {
		if (strcmp(G.kinds[k].kind, "chara"))
			continue;
		for (int i = 0; i < G.kinds[k].n && nfaces < (int)(sizeof faces / sizeof faces[0]); i++) {
			struct id_entry *e = &G.kinds[k].e[i];
			if (e->cg && e->use && !strcmp(e->use, "face")) {
				faces[nfaces].cg = e->cg;
				faces[nfaces].name = e->name;
				nfaces++;
			}
		}
	}
	qsort(faces, (size_t)nfaces, sizeof *faces, face_cmp);
}

void media_frame(void)
{
	frame_no++;
	loads_left = LOADS_PER_FRAME;
	waiting = false;
	if (thumbs_on)
		pics_frame();
	/* A picture not drawn for a few seconds goes. */
	for (int i = 0; i < CACHE; i++) {
		struct slot *s = &cache[i];
		if (s->used && s->last + 600 < frame_no) {
			tex_free(s->p.tex);
			memset(s, 0, sizeof *s);
		}
	}
}

bool media_idle(void)
{
	return !waiting && (!thumbs_on || pics_idle());
}

void media_set_mod(const char *dir)
{
	if (strcmp(mod_dir, dir ? dir : "")) {
		for (int i = 0; i < CACHE; i++) {
			if (cache[i].used && cache[i].path[0]) {
				tex_free(cache[i].p.tex);
				memset(&cache[i], 0, sizeof cache[i]);
			}
		}
	}
	snprintf(mod_dir, sizeof mod_dir, "%s", dir ? dir : "");
}

const char *media_mod_dir(void)
{
	return mod_dir;
}

static void load(struct slot *s)
{
	struct cg *cg = NULL;
	if (s->path[0]) {
		char full[1700];
		snprintf(full, sizeof full, "%s/%s", mod_dir, s->path);
		size_t n = 0;
		uint8_t *raw = file_read(full, &n);
		if (raw && n)
			cg = cg_load_buffer(raw, n);
		free(raw);
	} else {
		int i = game_find_cg(s->no);
		if (i >= 0)
			cg = game_load_cg(&G.h, i);
	}
	if (cg && cg->pixels && cg->metrics.w > 0 && cg->metrics.h > 0) {
		s->p.tex = tex_from_rgba(cg->pixels, cg->metrics.w, cg->metrics.h);
		s->p.w = cg->metrics.w;
		s->p.h = cg->metrics.h;
	}
	s->p.state = s->p.tex ? PIC_READY : PIC_FAILED;
	if (cg)
		cg_free(cg);
}

/* A picture value of a scene file: a CG number, or a path in the mod's folder. */
const struct picture *media_picture(const cJSON *v)
{
	static struct picture none = { 0, 0, 0, PIC_FAILED };
	int no = -1;
	const char *path = NULL;
	if (cJSON_IsNumber(v) && v->valuedouble >= 0)
		no = (int)v->valuedouble;
	else if (cJSON_IsString(v) && v->valuestring[0] && strchr(v->valuestring, '.'))
		path = v->valuestring;
	else
		return &none;
	if (no == 0)
		return &none;
	struct slot *free_slot = NULL, *old = NULL;
	for (int i = 0; i < CACHE; i++) {
		struct slot *s = &cache[i];
		if (!s->used) {
			if (!free_slot)
				free_slot = s;
			continue;
		}
		if (path ? !strcmp(s->path, path) : (!s->path[0] && s->no == no)) {
			s->last = frame_no;
			return &s->p;
		}
		if (!old || s->last < old->last)
			old = s;
	}
	static struct picture later = { 0, 0, 0, PIC_LOADING };
	if (loads_left <= 0) {
		waiting = true;
		return &later;
	}
	struct slot *s = free_slot;
	if (!s) {
		s = old;
		tex_free(s->p.tex);
		memset(s, 0, sizeof *s);
	}
	s->used = true;
	s->no = no;
	snprintf(s->path, sizeof s->path, "%s", path ? path : "");
	s->last = frame_no;
	loads_left--;
	load(s);
	return &s->p;
}

struct thumb *media_thumb(int no)
{
	static struct thumb none = { 0, 0, 0, 3, 0 };
	if (!thumbs_on || no <= 0)
		return &none;
	int i = game_find_cg(no);
	return i >= 0 ? pics_thumb(PK_CG, i) : &none;
}

bool media_cg_exists(int no)
{
	return no > 0 && game_find_cg(no) >= 0;
}

/* The character whose face this is: a character's face, or one of its expressions. A face on a
 * round ten starts a run of expressions in the CGs that follow it without a gap in that ten (Rance:
 * 2000, his expressions 2001 to 2006); elsewhere each number is a face of its own (the 2800s). */
const char *media_face_owner(int face)
{
	for (int i = 0; i < nfaces && faces[i].cg <= face; i++)
		if (faces[i].cg == face)
			return faces[i].name;
	for (int i = 0; i < nfaces && faces[i].cg < face; i++) {
		int base = faces[i].cg;
		if (base % 10 || face / 10 != base / 10)
			continue;
		bool run = true;
		for (int no = base + 1; no <= face && run; no++)
			run = game_find_cg(no) >= 0;
		if (run)
			return faces[i].name;
	}
	return NULL;
}

/* The name the game's plate shows for a face: the first word of RQ MOD's name for its character
 * ("Magic" for Magic The Gandhi, as the game's own plate reads), or NULL. */
const char *media_face_name(int face)
{
	static char ring[4][64];
	static int at;
	const char *who = media_face_owner(face);
	if (!who)
		return NULL;
	char *out = ring[at++ & 3];
	size_t n = strcspn(who, " ");
	snprintf(out, sizeof ring[0], "%.*s", (int)(n < sizeof ring[0] ? n : sizeof ring[0] - 1), who);
	return out;
}

const char *media_music_name(int no)
{
	for (int i = 0; i < G.nmusic; i++)
		if (G.music[i].no == no)
			return G.music[i].name;
	return NULL;
}

/* ---- playing ---- */

static int playing_kind = -1, playing_no = -1;

static struct audio_entry *audio_of(int kind, int no)
{
	struct audio_entry *list = kind == SOUND_MUSIC ? G.music : G.sounds;
	int n = kind == SOUND_MUSIC ? G.nmusic : G.nsounds;
	for (int i = 0; i < n; i++)
		if (list[i].no == no)
			return &list[i];
	return NULL;
}

bool media_playing(int kind, int no)
{
	return playing_kind == kind && playing_no == no && player_playing();
}

bool media_can_play(int kind, int no)
{
	return audio_of(kind, no) != NULL;
}

void media_play(int kind, int no)
{
	if (media_playing(kind, no)) {
		player_stop();
		playing_kind = playing_no = -1;
		return;
	}
	struct audio_entry *a = audio_of(kind, no);
	if (!a) {
		ui_error("%s %d is not in the game", kind == SOUND_MUSIC ? "Music" : "Sound", no);
		return;
	}
	size_t size = 0;
	uint8_t *data = game_read_audio(kind == SOUND_MUSIC ? G.h.ba : G.h.wa, no, &size);
	if (!data) {
		ui_error("Could not read %s %d", kind == SOUND_MUSIC ? "music" : "sound", no);
		return;
	}
	char title[300];
	snprintf(title, sizeof title, "%s %d", kind == SOUND_MUSIC ? "music" : "sound", no);
	if (player_load(data, size, title)) {
		player_set_loop(kind == SOUND_MUSIC && a->has_loop, a->loop_start, a->loop_end);
		player_play();
		playing_kind = kind;
		playing_no = no;
	}
	free(data);
}

void media_stop(void)
{
	if (playing_kind >= 0)
		player_stop();
	playing_kind = playing_no = -1;
}
