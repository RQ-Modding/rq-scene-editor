/* The world map's tag, as the game's T_WorldMap draws it at a quest's start (地図点滅): the place
 * tag's Flash file looping at the point, and the place's name over it. */
#include <float.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "system4.h"

#include "editor.h"

/* The name: the game's bold 24, white, a black shadow a pixel down and right, centred in a box of
 * 216 by 32 at the tag's place, 22 to the right of a tag that opens right, 15 to the left of one that
 * opens left. The English game places these letters with its own tables (its 24 with letters 4
 * closer); here the font at Dear ImGui's size 23 with 3 between letters measures as the game's shots
 * do ("Cidade" 67 pixels wide, its letters 12 tall). The tag is the Flash file 場所名札左向き for a
 * point left of the middle (the tag opening right, pointing left at it), 場所名札右向き otherwise. */
#define NAME_PX 23.0f
#define NAME_SPACE 3.0f

static float name_width(float size, const char *name)
{
	float w = 0;
	for (const char *c = name; *c;) {
		const char *e = c + 1;
		while (*e && ((unsigned char)*e & 0xC0) == 0x80)
			e++;
		w += ImFont_CalcTextSizeA(F_GAME, size, FLT_MAX, 0, c, e, NULL).x;
		if (*e)
			w += NAME_SPACE;
		c = e;
	}
	return w;
}

static void name_draw(ImDrawList *dl, float size, V2 p, float k, const char *name, ImU32 col)
{
	for (const char *c = name; *c;) {
		const char *e = c + 1;
		while (*e && ((unsigned char)*e & 0xC0) == 0x80)
			e++;
		/* Bold: the letter twice, half a pixel apart. */
		ImDrawList_AddText_FontPtr(dl, F_GAME, size, p, col, c, e, 0, NULL);
		ImDrawList_AddText_FontPtr(dl, F_GAME, size, v2(p.x + 0.6f * k, p.y), col, c, e, 0, NULL);
		p.x += ImFont_CalcTextSizeA(F_GAME, size, FLT_MAX, 0, c, e, NULL).x + NAME_SPACE * k;
		c = e;
	}
}

void map_tag_paint(ImDrawList *dl, V2 o, float k, int x, int y, const char *name, double ms)
{
	bool left = x < 512;
	int tx = left ? x : x - 244, ty = y - 34;
	if (tx < 0) tx = 0;
	if (tx > 780) tx = 780;
	if (ty < 0) ty = 0;
	if (ty > 734) ty = 734;
	flash_draw(dl, o, k, left ? "場所名札左向き" : "場所名札右向き", (float)tx, (float)ty, ms);
	float bx = (float)(left ? tx + 22 : tx - 15), by = (float)ty;
	float size = NAME_PX;
	float w = name_width(size, name);
	V2 p = v2(o.x + (bx + (216 - w) / 2) * k, o.y + (by + (32 - size) / 2) * k);
	ImDrawList_PushClipRect(dl, v2(o.x + bx * k, o.y + by * k), v2(o.x + (bx + 216) * k, o.y + (by + 32) * k), true);
	name_draw(dl, size * k, v2(p.x + k, p.y + k), k, name, IM_COL32(0, 0, 0, 255));
	name_draw(dl, size * k, p, k, name, IM_COL32_WHITE);
	ImDrawList_PopClipRect(dl);
}
