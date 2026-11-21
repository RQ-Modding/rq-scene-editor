/* The game's Flash files on the stage (the world map's place tag), played by Rance Browser's Flash
 * player (fplay_* in core.h): the timeline, the actions and the drawing as RQ MOD's engine has them,
 * at the game's 30 frames a second. The game's T_PartsFlash asks for frame 30 x seconds + 1 and,
 * past the last frame of a looping file, goes back to frame 1 with its clock started again. A frame
 * is drawn with OpenGL into the player's texture, the colours multiplied by the alpha, and put on
 * the stage as the engine blends it. */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "system4.h"

#include "editor.h"
#include "swf_check.h"

#define FILES 4

static struct {
	char base[128];
	struct fplay *p;
	bool failed;   /* not in the game, or one the game would not play: not asked again */
} files[FILES];

static struct fplay *open_file(const char *base)
{
	int slot = -1;
	for (int i = 0; i < FILES; i++) {
		if (files[i].base[0] && !strcmp(files[i].base, base))
			return files[i].failed ? NULL : files[i].p;
		if (!files[i].base[0] && slot < 0)
			slot = i;
	}
	if (slot < 0)
		return NULL;
	snprintf(files[slot].base, sizeof files[slot].base, "%s", base);
	files[slot].failed = true;
	for (int i = 0; i < G.nflash; i++) {
		if (strcmp(G.flash[i].base, base))
			continue;
		size_t size = 0;
		uint8_t *raw = game_read_flash(&G.h, i, &size);
		if (raw) {
			struct swfc c;
			files[slot].p = fplay_open(raw, size, &c, NULL);
			swfc_free(&c);
			free(raw);
		}
		break;
	}
	if (!files[slot].p)
		return NULL;
	files[slot].failed = false;
	return files[slot].p;
}

bool flash_draw(ImDrawList *dl, V2 o, float k, const char *base, float x, float y, double ms)
{
	struct fplay *p = open_file(base);
	if (!p)
		return false;
	int frames = fplay_frames(p) > 0 ? fplay_frames(p) : 1;
	/* Round and round at 30 a second: the first frame again after the last. */
	double loop = frames * 1000.0 / 30;
	int frame = (int)(30 * fmod(ms > 0 ? ms : 0, loop) / 1000) + 1;
	fplay_seek(p, frame < frames ? frame : frames);
	unsigned tex = fplay_render(p, k, FPLAY_CLEAR);
	if (!tex)
		return false;
	float w = (float)fplay_width(p), h = (float)fplay_height(p);
	fplay_image(dl, tex, v2(o.x + x * k, o.y + y * k), v2(o.x + (x + w) * k, o.y + (y + h) * k));
	return true;
}
