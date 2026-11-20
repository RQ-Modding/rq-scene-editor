/* The stage in textures of the game's 1024 by 768, and the game's transitions between two of them.
 *
 * While the stage plays, a frame of it is drawn as Dear ImGui draws the window, but into a texture of
 * the game's size (a draw list of the editor's own, rendered by Dear ImGui's OpenGL backend into a
 * framebuffer after the window's frame is built), and a transition mixes two such textures, the
 * screen before and the screen after, as RQ MOD's engine mixes them. The effects are xsystem4's
 * (its src/effect.c and shaders/effects, Copyright (C) 2019 Nunuhara Cabbage, GPL-2.0-or-later):
 * its shaders as they are (fx_shaders.h), and the effects it draws in C written here as shaders of
 * the same maths. A number it has not written plays as its blur crossfade (51), as in the engine.
 *
 * The textures hold the game's screen top row first (v = 0 at the top), as the engine's do: the draw
 * list's points are turned upside down before they are rendered. */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <SDL.h>
#include <SDL_opengl.h>

#include "system4.h"

#include "editor.h"
#include "fx_shaders.h"

/* ---- the effects by number ---- */

static const char FX_HEAD[] =
	"#version 130\n"
	"uniform sampler2D tex;\n"      /* the screen after */
	"uniform sampler2D old;\n"      /* the screen before */
	"uniform vec2 resolution;\n"
	"uniform float progress;\n"
	"in vec2 tex_coord;\n"
	"out vec4 frag_color;\n";

/* The effects xsystem4 draws in C (effect.c), as shaders. */
static const char FX_FADEOUT[] =
	"void main() { frag_color = vec4(texture(old, tex_coord).rgb * (1.0 - progress), 1.0); }\n";
static const char FX_FADEIN[] =
	"void main() { frag_color = vec4(texture(tex, tex_coord).rgb * progress, 1.0); }\n";
static const char FX_WHITEOUT[] =
	"void main() { frag_color = vec4(mix(vec3(1.0), texture(old, tex_coord).rgb, 1.0 - progress), 1.0); }\n";
static const char FX_WHITEIN[] =
	"void main() { frag_color = vec4(mix(vec3(1.0), texture(tex, tex_coord).rgb, progress), 1.0); }\n";
/* The old screen turned a whole turn and made smaller to nothing, on black (gfx_copy_rot_zoom). */
static const char FX_ROTATE_OUT[] =
	"void main() {\n"
	"    vec2 c = resolution * 0.5;\n"
	"    vec2 d = tex_coord * resolution - c;\n"
	"    float m = 1.0 - progress;\n"
	"    float a = radians(progress * 360.0);\n"
	"    vec3 col = vec3(0.0);\n"
	"    if (m > 0.0) {\n"
	"        vec2 s = c + mat2(cos(a), sin(a), -sin(a), cos(a)) * d / m;\n"
	"        if (s.x >= 0.0 && s.y >= 0.0 && s.x < resolution.x && s.y < resolution.y)\n"
	"            col = texture(old, s / resolution).rgb;\n"
	"    }\n"
	"    frag_color = vec4(col, 1.0);\n"
	"}\n";
/* A television switched off: the picture squeezed to a line across the middle, then the line to
 * nothing (effect_tv_switch); switched on is the same backwards with the new screen. */
#define FX_TV_FN \
	"vec3 tv(sampler2D src, float rate) {\n" \
	"    vec2 p = tex_coord * resolution;\n" \
	"    if (rate < 0.5) {\n" \
	"        float h = max(1.0, floor(resolution.y * (1.0 - rate * 2.0)));\n" \
	"        float top = floor(resolution.y * rate);\n" \
	"        if (p.y < top || p.y >= top + h) return vec3(0.0);\n" \
	"        return texture(src, vec2(tex_coord.x, (p.y - top) / h)).rgb;\n" \
	"    }\n" \
	"    rate -= 0.5;\n" \
	"    float w = floor(resolution.x * (1.0 - rate * 2.0));\n" \
	"    float left = floor(resolution.x * rate);\n" \
	"    float y = floor(resolution.y / 2.0);\n" \
	"    if (w < 1.0 || p.x < left || p.x >= left + w || p.y < y || p.y >= y + 1.0) return vec3(0.0);\n" \
	"    return texture(src, vec2((p.x - left) / w, 0.5)).rgb;\n" \
	"}\n"
static const char FX_TV_OFF[] = FX_TV_FN "void main() { frag_color = vec4(tv(old, progress), 1.0); }\n";
static const char FX_TV_ON[] = FX_TV_FN "void main() { frag_color = vec4(tv(tex, 1.0 - progress), 1.0); }\n";
/* The new screen stretched in from the left over the old one squeezed to the right, and back. */
static const char FX_ZOOM_LR[] =
	"void main() {\n"
	"    float x = floor(resolution.x * progress + 0.5);\n"
	"    float p = tex_coord.x * resolution.x;\n"
	"    vec3 col = p < x ? texture(tex, vec2(p / x, tex_coord.y)).rgb\n"
	"                     : texture(old, vec2((p - x) / (resolution.x - x), tex_coord.y)).rgb;\n"
	"    frag_color = vec4(col, 1.0);\n"
	"}\n";
static const char FX_ZOOM_RL[] =
	"void main() {\n"
	"    float x = resolution.x - floor(resolution.x * progress + 0.5);\n"
	"    float p = tex_coord.x * resolution.x;\n"
	"    vec3 col = p >= x ? texture(tex, vec2((p - x) / (resolution.x - x), tex_coord.y)).rgb\n"
	"                      : texture(old, vec2(p / x, tex_coord.y)).rgb;\n"
	"    frag_color = vec4(col, 1.0);\n"
	"}\n";
/* The old screen grown from the middle (to 17 times) as it fades over the new one. */
static const char FX_ZOOM_IN[] =
	"void main() {\n"
	"    float scale = 1.0 + pow(progress, 4.0) * 16.0;\n"
	"    vec2 c = resolution * 0.5;\n"
	"    vec2 s = c + (tex_coord * resolution - c) / scale;\n"
	"    vec3 col = mix(texture(tex, tex_coord).rgb, texture(old, s / resolution).rgb, 1.0 - progress);\n"
	"    frag_color = vec4(col, 1.0);\n"
	"}\n";

/* The game's スプライトズーム (set's effects 7001 and 7002): the screen filled with the CG's top
 * left colour, but where the CG, stretched `mag` times about the middle, is clear. */
static const char FX_SHAPE[] =
	"#version 130\n"
	"uniform sampler2D tex;\n"      /* the screen under it */
	"uniform sampler2D shape;\n"
	"uniform vec2 resolution;\n"
	"uniform vec2 shape_size;\n"
	"uniform float mag;\n"
	"in vec2 tex_coord;\n"
	"out vec4 frag_color;\n"
	"void main() {\n"
	"    vec3 back = texture(shape, vec2(0.5) / shape_size).rgb;\n"
	"    vec2 size = floor(shape_size * mag);\n"
	"    vec2 at = floor(resolution / 2.0) - floor(size / 2.0);\n"
	"    vec2 p = tex_coord * resolution - at;\n"
	"    float a = 1.0;\n"
	"    if (size.x >= 1.0 && size.y >= 1.0 && p.x >= 0.0 && p.y >= 0.0 && p.x < size.x && p.y < size.y)\n"
	"        a = texture(shape, p / size).a;\n"
	"    frag_color = vec4(mix(texture(tex, tex_coord).rgb, back, a), 1.0);\n"
	"}\n";

struct effect_def {
	const char *name;
	const char *src;      /* NULL: xsystem4 has not written it (it plays as 51) */
};

/* The engine's names (effect.h), in words, and what draws each. */
static const struct effect_def effects[EFFECT_LAST + 1] = {
	[1] = { "Crossfade", FX_CROSSFADE },
	[2] = { "Fade out to black", FX_FADEOUT },
	[3] = { "Fade in from black", FX_FADEIN },
	[4] = { "Fade out to white", FX_WHITEOUT },
	[5] = { "Fade in from white", FX_WHITEIN },
	[6] = { "Crossfade through a mosaic", FX_MOSAIC },
	[7] = { "Blinds, downwards", FX_BLIND_DOWN },
	[8] = { "Blinds, left to right", FX_BLIND_LR },
	[9] = { "Blinds, down and left to right", NULL },
	[10] = { "Zoom, blend and blur", NULL },
	[11] = { "Crossfade through a sideways blur", FX_LINEAR_BLUR },
	[12] = { "Crossfade from the top down", FX_CROSSFADE_UP_DOWN },
	[13] = { "Crossfade from the bottom up", FX_CROSSFADE_DOWN_UP },
	[14] = { "Pentagram, inside out", NULL },
	[15] = { "Pentagram, outside in", NULL },
	[16] = { "Hexagram, inside out", NULL },
	[17] = { "Hexagram, outside in", NULL },
	[18] = { "Crossfade through an alpha map", NULL },
	[19] = { "Vertical bars and blur", NULL },
	[20] = { "Turn away to nothing", FX_ROTATE_OUT },
	[21] = { "Turn in", NULL },
	[22] = { "Turn away clockwise", NULL },
	[23] = { "Turn in clockwise", NULL },
	[24] = { "Dissolve in blocks", NULL },
	[25] = { "Polygons turning on Y", NULL },
	[26] = { "Polygons turning on Y, clockwise", NULL },
	[27] = { "Shake the screen", NULL },
	[28] = { "Polygons turning on X, clockwise", NULL },
	[29] = { "Polygons turning on X", NULL },
	[30] = { "Turn, zoom, blend and blur", NULL },
	[31] = { "Crossfade in a zigzag", FX_ZIGZAG_CROSSFADE },
	[32] = { "Television off", FX_TV_OFF },
	[33] = { "Television on", FX_TV_ON },
	[34] = { "Polygons bursting", NULL },
	[35] = { "Crossfade through noise", FX_NOISE_CROSSFADE },
	[36] = { "Turn the page", FX_TURN_PAGE },
	[37] = { "Crossfade through sepia noise", FX_SEPIA_NOISE_CROSSFADE },
	[38] = { "Crumpled paper pulled away", NULL },
	[39] = { "Horizontal zigzag", NULL },
	[40] = { "Sideways blur, HD", NULL },
	[41] = { "Vertical bars and blur, HD", NULL },
	[42] = { "Crossfade through an alpha map, 2", NULL },
	[43] = { "Stretch in from the left", FX_ZOOM_LR },
	[44] = { "Stretch in from the right", FX_ZOOM_RL },
	[45] = { "Crossfade from the left", FX_CROSSFADE_LR },
	[46] = { "Crossfade from the right", NULL },
	[47] = { "Pixels bursting", NULL },
	[48] = { "Zoom into the old screen", FX_ZOOM_IN },
	[49] = { "Pixels dropping", NULL },
	[50] = { "Blur away, then the new screen", FX_BLUR_FADEOUT },
	[51] = { "Crossfade through a blur", FX_BLUR_CROSSFADE },
	[52] = { "Two turns, zoom, blend and blur", NULL },
	[55] = { "Waves scrolling across", FX_VWAVE_SCROLL_CROSSFADE },
	[60] = { "Waves", FX_VWAVE_CROSSFADE },
};

const char *effect_name(int no)
{
	return no >= 1 && no <= EFFECT_LAST ? effects[no].name : NULL;
}

bool effect_written(int no)
{
	return no >= 1 && no <= EFFECT_LAST && effects[no].src;
}

int effect_played(int no)
{
	if (no == 27)
		return 27;
	if (no < 1 || no > EFFECT_LAST)
		return 0;
	return effects[no].src ? no : 51;
}

/* ---- OpenGL past 1.1, through SDL ---- */

static PFNGLGENFRAMEBUFFERSPROC p_gen_fb;
static PFNGLBINDFRAMEBUFFERPROC p_bind_fb;
static PFNGLFRAMEBUFFERTEXTURE2DPROC p_fb_tex;
static PFNGLCHECKFRAMEBUFFERSTATUSPROC p_fb_status;
static PFNGLCREATESHADERPROC p_create_shader;
static PFNGLSHADERSOURCEPROC p_shader_source;
static PFNGLCOMPILESHADERPROC p_compile_shader;
static PFNGLGETSHADERIVPROC p_get_shader;
static PFNGLGETSHADERINFOLOGPROC p_shader_log;
static PFNGLDELETESHADERPROC p_delete_shader;
static PFNGLCREATEPROGRAMPROC p_create_program;
static PFNGLATTACHSHADERPROC p_attach;
static PFNGLBINDATTRIBLOCATIONPROC p_bind_attrib;
static PFNGLBINDFRAGDATALOCATIONPROC p_bind_frag;
static PFNGLLINKPROGRAMPROC p_link;
static PFNGLGETPROGRAMIVPROC p_get_program;
static PFNGLGETPROGRAMINFOLOGPROC p_program_log;
static PFNGLUSEPROGRAMPROC p_use;
static PFNGLGETUNIFORMLOCATIONPROC p_uniform_at;
static PFNGLUNIFORM1IPROC p_u1i;
static PFNGLUNIFORM1FPROC p_u1f;
static PFNGLUNIFORM2FPROC p_u2f;
static PFNGLACTIVETEXTUREPROC p_active_tex;
static PFNGLGENBUFFERSPROC p_gen_buffers;
static PFNGLBINDBUFFERPROC p_bind_buffer;
static PFNGLBUFFERDATAPROC p_buffer_data;
static PFNGLGENVERTEXARRAYSPROC p_gen_vao;
static PFNGLBINDVERTEXARRAYPROC p_bind_vao;
static PFNGLENABLEVERTEXATTRIBARRAYPROC p_enable_attrib;
static PFNGLVERTEXATTRIBPOINTERPROC p_attrib_ptr;

static bool load_gl(void)
{
#define L(var, type, name) if (!(var = (type)SDL_GL_GetProcAddress(name))) return false
	L(p_gen_fb, PFNGLGENFRAMEBUFFERSPROC, "glGenFramebuffers");
	L(p_bind_fb, PFNGLBINDFRAMEBUFFERPROC, "glBindFramebuffer");
	L(p_fb_tex, PFNGLFRAMEBUFFERTEXTURE2DPROC, "glFramebufferTexture2D");
	L(p_fb_status, PFNGLCHECKFRAMEBUFFERSTATUSPROC, "glCheckFramebufferStatus");
	L(p_create_shader, PFNGLCREATESHADERPROC, "glCreateShader");
	L(p_shader_source, PFNGLSHADERSOURCEPROC, "glShaderSource");
	L(p_compile_shader, PFNGLCOMPILESHADERPROC, "glCompileShader");
	L(p_get_shader, PFNGLGETSHADERIVPROC, "glGetShaderiv");
	L(p_shader_log, PFNGLGETSHADERINFOLOGPROC, "glGetShaderInfoLog");
	L(p_delete_shader, PFNGLDELETESHADERPROC, "glDeleteShader");
	L(p_create_program, PFNGLCREATEPROGRAMPROC, "glCreateProgram");
	L(p_attach, PFNGLATTACHSHADERPROC, "glAttachShader");
	L(p_bind_attrib, PFNGLBINDATTRIBLOCATIONPROC, "glBindAttribLocation");
	L(p_bind_frag, PFNGLBINDFRAGDATALOCATIONPROC, "glBindFragDataLocation");
	L(p_link, PFNGLLINKPROGRAMPROC, "glLinkProgram");
	L(p_get_program, PFNGLGETPROGRAMIVPROC, "glGetProgramiv");
	L(p_program_log, PFNGLGETPROGRAMINFOLOGPROC, "glGetProgramInfoLog");
	L(p_use, PFNGLUSEPROGRAMPROC, "glUseProgram");
	L(p_uniform_at, PFNGLGETUNIFORMLOCATIONPROC, "glGetUniformLocation");
	L(p_u1i, PFNGLUNIFORM1IPROC, "glUniform1i");
	L(p_u1f, PFNGLUNIFORM1FPROC, "glUniform1f");
	L(p_u2f, PFNGLUNIFORM2FPROC, "glUniform2f");
	L(p_active_tex, PFNGLACTIVETEXTUREPROC, "glActiveTexture");
	L(p_gen_buffers, PFNGLGENBUFFERSPROC, "glGenBuffers");
	L(p_bind_buffer, PFNGLBINDBUFFERPROC, "glBindBuffer");
	L(p_buffer_data, PFNGLBUFFERDATAPROC, "glBufferData");
	L(p_gen_vao, PFNGLGENVERTEXARRAYSPROC, "glGenVertexArrays");
	L(p_bind_vao, PFNGLBINDVERTEXARRAYPROC, "glBindVertexArray");
	L(p_enable_attrib, PFNGLENABLEVERTEXATTRIBARRAYPROC, "glEnableVertexAttribArray");
	L(p_attrib_ptr, PFNGLVERTEXATTRIBPOINTERPROC, "glVertexAttribPointer");
#undef L
	return true;
}

/* ---- the textures ---- */

struct canvas {
	GLuint fbo, tex;
	ImDrawList *dl;
};

static struct canvas canvases[FX_SLOTS];
static bool ready, failed;
static GLuint quad_vao, quad_vbo;

enum { OP_DRAW, OP_MIX, OP_SHAPE };

struct op {
	int kind, out, a, b, effect;
	float p;
	unsigned shape_tex;
	int shape_w, shape_h;
};

static struct op ops[32];
static int nops;

struct prog {
	GLuint id;
	bool tried;
	GLint tex, old, res, progress, shape, shape_size, mag;
};

static struct prog progs[EFFECT_LAST + 1], shape_prog;

static bool init(void)
{
	if (ready || failed)
		return ready;
	failed = true;
	if (!load_gl())
		return false;
	for (int i = 0; i < FX_SLOTS; i++) {
		struct canvas *c = &canvases[i];
		glGenTextures(1, &c->tex);
		glBindTexture(GL_TEXTURE_2D, c->tex);
		glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, FX_W, FX_H, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
		glBindTexture(GL_TEXTURE_2D, 0);
		GLint prev = 0;
		glGetIntegerv(GL_FRAMEBUFFER_BINDING, &prev);
		p_gen_fb(1, &c->fbo);
		p_bind_fb(GL_FRAMEBUFFER, c->fbo);
		p_fb_tex(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, c->tex, 0);
		bool ok = p_fb_status(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;
		p_bind_fb(GL_FRAMEBUFFER, (GLuint)prev);
		if (!ok)
			return false;
		c->dl = ImDrawList_ImDrawList(igGetDrawListSharedData());
	}
	static const float quad[] = { 0, 0, 1, 0, 0, 1, 1, 1 };
	p_gen_vao(1, &quad_vao);
	p_bind_vao(quad_vao);
	p_gen_buffers(1, &quad_vbo);
	p_bind_buffer(GL_ARRAY_BUFFER, quad_vbo);
	p_buffer_data(GL_ARRAY_BUFFER, sizeof quad, quad, GL_STATIC_DRAW);
	p_enable_attrib(0);
	p_attrib_ptr(0, 2, GL_FLOAT, GL_FALSE, 0, NULL);
	p_bind_vao(0);
	p_bind_buffer(GL_ARRAY_BUFFER, 0);
	ready = true;
	failed = false;
	return true;
}

bool fx_ok(void)
{
	return init();
}

unsigned fx_texture(int slot)
{
	return slot >= 0 && slot < FX_SLOTS && ready ? canvases[slot].tex : 0;
}

static GLuint compile(GLenum type, const char *a, const char *b)
{
	GLuint s = p_create_shader(type);
	const char *src[2] = { a, b };
	p_shader_source(s, b ? 2 : 1, src, NULL);
	p_compile_shader(s);
	GLint ok = 0;
	p_get_shader(s, GL_COMPILE_STATUS, &ok);
	if (!ok) {
		char log[1024] = "";
		p_shader_log(s, sizeof log, NULL, log);
		fprintf(stderr, APP_EXE ": a transition's shader did not compile: %s\n", log);
		p_delete_shader(s);
		return 0;
	}
	return s;
}

static const char VERTEX[] =
	"#version 130\n"
	"in vec2 pos;\n"
	"out vec2 tex_coord;\n"
	"void main() { tex_coord = pos; gl_Position = vec4(pos * 2.0 - 1.0, 0.0, 1.0); }\n";

static bool link(struct prog *pr, const char *head, const char *body)
{
	pr->tried = true;
	GLuint v = compile(GL_VERTEX_SHADER, VERTEX, NULL), f = compile(GL_FRAGMENT_SHADER, head, body);
	if (!v || !f)
		return false;
	GLuint p = p_create_program();
	p_attach(p, v);
	p_attach(p, f);
	p_bind_attrib(p, 0, "pos");
	p_bind_frag(p, 0, "frag_color");
	p_link(p);
	p_delete_shader(v);
	p_delete_shader(f);
	GLint ok = 0;
	p_get_program(p, GL_LINK_STATUS, &ok);
	if (!ok) {
		char log[1024] = "";
		p_program_log(p, sizeof log, NULL, log);
		fprintf(stderr, APP_EXE ": a transition's shader did not link: %s\n", log);
		return false;
	}
	pr->id = p;
	pr->tex = p_uniform_at(p, "tex");
	pr->old = p_uniform_at(p, "old");
	pr->res = p_uniform_at(p, "resolution");
	pr->progress = p_uniform_at(p, "progress");
	pr->shape = p_uniform_at(p, "shape");
	pr->shape_size = p_uniform_at(p, "shape_size");
	pr->mag = p_uniform_at(p, "mag");
	return true;
}

static struct prog *program(int effect)
{
	int no = effect_played(effect);
	if (no <= 0 || no == 27)
		no = 1;
	struct prog *pr = &progs[no];
	if (!pr->tried)
		link(pr, FX_HEAD, effects[no].src);
	return pr->id ? pr : NULL;
}

/* ---- what the frame asks for ---- */

ImDrawList *fx_draw(int slot)
{
	if (slot < 0 || slot >= FX_SLOTS || !init() || nops >= (int)(sizeof ops / sizeof ops[0]))
		return NULL;
	struct canvas *c = &canvases[slot];
	ImDrawList__ResetForNewFrame(c->dl);
	ImDrawList_PushTexture(c->dl, igGetIO_Nil()->Fonts->TexRef);
	ImDrawList_PushClipRect(c->dl, v2(0, 0), v2(FX_W, FX_H), false);
	ops[nops++] = (struct op){ .kind = OP_DRAW, .out = slot };
	return c->dl;
}

void fx_mix(int out, int effect, int old, int new, float p)
{
	if (!ready || nops >= (int)(sizeof ops / sizeof ops[0]))
		return;
	ops[nops++] = (struct op){ .kind = OP_MIX, .out = out, .a = old, .b = new, .effect = effect,
	                           .p = p < 0 ? 0 : p > 1 ? 1 : p };
}

void fx_shape(int out, int under, unsigned shape_tex, int w, int h, float mag)
{
	if (!ready || !shape_tex || w <= 0 || h <= 0 || nops >= (int)(sizeof ops / sizeof ops[0]))
		return;
	ops[nops++] = (struct op){ .kind = OP_SHAPE, .out = out, .a = under, .shape_tex = shape_tex,
	                           .shape_w = w, .shape_h = h, .p = mag };
}

/* ---- after the window's frame ---- */

static void render_list(struct canvas *c)
{
	ImDrawList *dl = c->dl;
	/* Upside down: the backend draws its first row at the top of the framebuffer, the last row
	 * of the texture; the game's top row goes to the texture's first. */
	for (int i = 0; i < dl->VtxBuffer.Size; i++)
		dl->VtxBuffer.Data[i].pos.y = FX_H - dl->VtxBuffer.Data[i].pos.y;
	for (int i = 0; i < dl->CmdBuffer.Size; i++) {
		ImVec4_c *r = &dl->CmdBuffer.Data[i].ClipRect;
		float y1 = r->y, y2 = r->w;
		r->y = FX_H - y2;
		r->w = FX_H - y1;
	}
	ImDrawList *lists[1] = { dl };
	ImDrawData dd;
	memset(&dd, 0, sizeof dd);
	dd.Valid = true;
	dd.CmdLists.Size = dd.CmdLists.Capacity = 1;
	dd.CmdLists.Data = lists;
	dd.TotalVtxCount = dl->VtxBuffer.Size;
	dd.TotalIdxCount = dl->IdxBuffer.Size;
	dd.DisplayPos = v2(0, 0);
	dd.DisplaySize = v2(FX_W, FX_H);
	dd.FramebufferScale = v2(1, 1);
	dd.Textures = &igGetPlatformIO_Nil()->Textures;
	p_bind_fb(GL_FRAMEBUFFER, c->fbo);
	glViewport(0, 0, FX_W, FX_H);
	glDisable(GL_SCISSOR_TEST);
	glClearColor(0, 0, 0, 1);
	glClear(GL_COLOR_BUFFER_BIT);
	ImGui_ImplOpenGL3_RenderDrawData(&dd);
}

static void bind_tex(int unit, GLuint tex)
{
	p_active_tex(GL_TEXTURE0 + unit);
	glBindTexture(GL_TEXTURE_2D, tex);
}

static void render_pass(const struct op *o)
{
	struct prog *pr;
	if (o->kind == OP_SHAPE) {
		if (!shape_prog.tried)
			link(&shape_prog, FX_SHAPE, NULL);
		pr = shape_prog.id ? &shape_prog : NULL;
	} else {
		pr = program(o->effect);
	}
	if (!pr)
		return;
	p_bind_fb(GL_FRAMEBUFFER, canvases[o->out].fbo);
	glViewport(0, 0, FX_W, FX_H);
	glDisable(GL_SCISSOR_TEST);
	glDisable(GL_BLEND);
	glDisable(GL_CULL_FACE);
	glDisable(GL_DEPTH_TEST);
	p_use(pr->id);
	if (o->kind == OP_SHAPE) {
		bind_tex(0, canvases[o->a].tex);
		bind_tex(1, o->shape_tex);
		p_u1i(pr->tex, 0);
		p_u1i(pr->shape, 1);
		p_u2f(pr->shape_size, (float)o->shape_w, (float)o->shape_h);
		p_u1f(pr->mag, o->p);
	} else {
		bind_tex(0, canvases[o->b].tex);
		bind_tex(1, canvases[o->a].tex);
		p_u1i(pr->tex, 0);
		p_u1i(pr->old, 1);
		p_u1f(pr->progress, o->p);
	}
	p_u2f(pr->res, FX_W, FX_H);
	p_bind_vao(quad_vao);
	glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
	p_bind_vao(0);
	p_use(0);
	bind_tex(1, 0);
	bind_tex(0, 0);
}

void fx_render(void)
{
	if (!ready) {
		nops = 0;
		return;
	}
	GLint prev_fb = 0, vp[4];
	glGetIntegerv(GL_FRAMEBUFFER_BINDING, &prev_fb);
	glGetIntegerv(GL_VIEWPORT, vp);
	for (int i = 0; i < nops; i++) {
		if (ops[i].kind == OP_DRAW)
			render_list(&canvases[ops[i].out]);
		else
			render_pass(&ops[i]);
	}
	nops = 0;
	p_bind_fb(GL_FRAMEBUFFER, (GLuint)prev_fb);
	glViewport(vp[0], vp[1], vp[2], vp[3]);
}

void fx_shutdown(void)
{
	for (int i = 0; i < FX_SLOTS; i++) {
		if (canvases[i].dl)
			ImDrawList_destroy(canvases[i].dl);
		canvases[i].dl = NULL;
	}
	ready = false;
	failed = true;
}
