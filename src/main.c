/* rq-scene-editor.exe: the window and the command line.
 *
 *   rq-scene-editor [--game DIR] [--mod ID] [--mod-dir DIR] [--file REL] [--scale F]
 *        the window, on a game folder's mods (--mod-dir: a mod's folder outside the game's mods)
 *   rq-scene-editor --check [--dir DIR | --game DIR [--mod ID]] [--out FILE]
 *        each scene file read as RQ MOD reads it, written in the editor's layout and read again;
 *        the results as JSON
 *   rq-scene-editor --format FILE...
 *        the files written in the editor's layout (as the window's Save writes them)
 *   rq-scene-editor --shot FILE.png --game DIR [--mod ID | --mod-dir DIR] [--file REL]
 *                   [--select PLACE] [--look copandon|parchment] [--size WxH] [--scale F]
 *        the window drawn offscreen and saved (the docs' pictures, the checks)
 *
 * The game folder: --game, else the folder above the program's own (RQ MOD puts it in the game's
 * xsystem4 folder), else the program's own folder, else the last one used, else a picker. */
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <SDL_opengl.h>

#include "system4.h"
#include "system4/file.h"

#include "editor.h"

SDL_Window *g_window;

static struct {
	const char *game, *mod, *mod_dir, *file, *select, *dir, *out, *selftest;
	bool check, mixer_check;
	const char *dos[64];   /* --do actions, in order */
	int ndo;
	int format_at;   /* argv index of the first file for --format, or 0 */
	const char *shot_path;
	int look;
	int w, h;
	bool size_given;
	float scale;
} A = { .look = -1, .w = 1440, .h = 900 };

static void usage(void)
{
	win_attach_console();
	printf(APP_NAME " " EDITOR_VERSION ": RQ MOD's scene files for Rance Quest Magnum.\n\n"
	       "  " APP_EXE " [--game DIR] [--mod ID] [--mod-dir DIR] [--file REL] [--scale F]\n"
	       "  " APP_EXE " --check [--dir DIR | --game DIR [--mod ID]] [--out FILE]\n"
	       "      each scene file read as RQ MOD reads it, written in the editor's layout and read\n"
	       "      again: the same values, the same text twice. Exit 1 when one does not round-trip.\n"
	       "      Each file's problems from RQ MOD's own checker (the game's names with --game) and\n"
	       "      the editor's warnings that need no font.\n"
	       "  " APP_EXE " --format FILE...\n"
	       "      the files written in the editor's layout\n"
	       "  " APP_EXE " --shot FILE.png --game DIR [--mod ID | --mod-dir DIR] [--file REL]\n"
	       "      [--select PLACE] [--look copandon|parchment] [--size WxH] [--scale F] [--do ACTION]...\n"
	       "      --do: what a button does, before the shot: select=PLACE, add=KIND, apply=JSON,\n"
	       "      type=JSON, copy, delete, up, down, undo, redo, tab=steps|cast,\n"
	       "      modal=unsaved|layout|changed|new, fold (nothing is saved); the stage: play (the\n"
	       "      step's motion), playon (Play from here), clock=MS (its clock held), click,\n"
	       "      pick=ROW, answer=then|else, preview=EFFECT (a list's row hovered), print (what\n"
	       "      plays, on stdout); the branches: cond=OP:PATH[:ARG] (the selected if's or row's\n"
	       "      condition: add, group, del, not, kind, mode, value), does=on|steps|goto:LABEL|\n"
	       "      end:VALUE (the selected row), label=NAME (its go-tos with it), printstep,\n"
	       "      printfile; the bottom panel: panel=problems|log|logall, printproblems,\n"
	       "      goproblem=N (the Nth problem clicked), logfile=PATH (another file as the game's log),\n"
	       "      printlog, loglink=N (the log's Nth line clicked); frame: the actions after it wait\n"
	       "      for a frame drawn\n"
	       "  " APP_EXE " --selftest FILE\n"
	       "      edits, undo, saving and changes on disk, tried on FILE (a copy: it is written)\n"
	       "  " APP_EXE " --mixer-check [--game DIR]\n"
	       "      Play from here's sounds mixed without a sound device: two at once, a track's fade\n"
	       "      and its loop\n"
	       "  --mod-dir DIR: a mod's folder outside the game's mods folder\n"
	       "  --select PLACE: a step's place, such as steps[4].then[0]\n"
	       "  --scale F: the size of the text and the layout, 1 to 4 (default: the display's;\n"
	       "      1 for --shot)\n");
}

static void on_error(const char *msg)
{
	if (A.check || A.shot_path || A.format_at || A.selftest || A.mixer_check) {
		fprintf(stderr, "%s\n", msg);
		return;
	}
	win_message(APP_NAME, msg);
}

static void parent_of(char *dir)
{
	char *a = strrchr(dir, '/'), *b = strrchr(dir, '\\');
	char *s = a > b ? a : b;
	if (s)
		*s = 0;
}

static bool pick_game(char *out, size_t n)
{
	while (win_pick_folder("Pick Rance Quest Magnum's folder (the one with System40.ini)", out, n)) {
		if (game_dir_ok(out))
			return true;
		win_message(APP_NAME, "That folder is not the game's: it needs System40.ini and the game's CG archive. "
		            "Pick Rance Quest Magnum's own folder.");
	}
	return false;
}

static bool find_game(char *out, size_t n)
{
	if (A.game) {
		snprintf(out, n, "%s", A.game);
		return game_dir_ok(out);
	}
	char dir[1024];
	if (win_exe_dir(dir, sizeof dir)) {
		char up[1024];
		snprintf(up, sizeof up, "%s", dir);
		parent_of(up);
		if (game_dir_ok(up)) {
			snprintf(out, n, "%s", up);
			return true;
		}
		if (game_dir_ok(dir)) {
			snprintf(out, n, "%s", dir);
			return true;
		}
	}
	const char *last = settings_get("game");
	if (last && game_dir_ok(last)) {
		snprintf(out, n, "%s", last);
		return true;
	}
	if (A.check || A.shot_path)
		return false;
	return pick_game(out, n);
}

static void parse_args(int argc, char **argv)
{
	for (int i = 1; i < argc; i++) {
		const char *a = argv[i];
		if (!strcmp(a, "--game") && i + 1 < argc) {
			A.game = argv[++i];
		} else if (!strcmp(a, "--mod") && i + 1 < argc) {
			A.mod = argv[++i];
		} else if (!strcmp(a, "--mod-dir") && i + 1 < argc) {
			A.mod_dir = argv[++i];
		} else if (!strcmp(a, "--file") && i + 1 < argc) {
			A.file = argv[++i];
		} else if (!strcmp(a, "--select") && i + 1 < argc) {
			A.select = argv[++i];
		} else if (!strcmp(a, "--do") && i + 1 < argc) {
			if (A.ndo < (int)(sizeof A.dos / sizeof A.dos[0]))
				A.dos[A.ndo++] = argv[++i];
			else
				i++;
		} else if (!strcmp(a, "--selftest") && i + 1 < argc) {
			A.selftest = argv[++i];
		} else if (!strcmp(a, "--check")) {
			A.check = true;
		} else if (!strcmp(a, "--mixer-check")) {
			A.mixer_check = true;
		} else if (!strcmp(a, "--dir") && i + 1 < argc) {
			A.dir = argv[++i];
		} else if (!strcmp(a, "--out") && i + 1 < argc) {
			A.out = argv[++i];
		} else if (!strcmp(a, "--format") && i + 1 < argc) {
			A.format_at = i + 1;
			return;
		} else if (!strcmp(a, "--shot") && i + 1 < argc) {
			A.shot_path = argv[++i];
		} else if (!strcmp(a, "--look") && i + 1 < argc) {
			A.look = look_find(argv[++i]);
			if (A.look < 0) {
				usage();
				exit(2);
			}
		} else if (!strcmp(a, "--size") && i + 1 < argc) {
			sscanf(argv[++i], "%dx%d", &A.w, &A.h);
			A.size_given = true;
		} else if (!strcmp(a, "--scale") && i + 1 < argc) {
			A.scale = (float)atof(argv[++i]);
			if (A.scale < 1 || A.scale > 4)
				A.scale = 0;
		} else if (!strcmp(a, "--help") || !strcmp(a, "-h")) {
			usage();
			exit(0);
		} else {
			usage();
			exit(2);
		}
	}
}

/* The display's scale, 1 at 100 % (96 DPI). */
static float display_scale(int display)
{
	float s = ImGui_ImplSDL2_GetContentScaleForDisplay(display < 0 ? 0 : display);
	return s >= 1 && s <= 4 ? s : 1;
}

/* ---- --shot: draw into a framebuffer of our own and save it ---- */

static PFNGLGENFRAMEBUFFERSPROC p_gen_fb;
static PFNGLBINDFRAMEBUFFERPROC p_bind_fb;
static PFNGLFRAMEBUFFERTEXTURE2DPROC p_fb_tex;
static PFNGLCHECKFRAMEBUFFERSTATUSPROC p_fb_status;

static GLuint shot_fbo(int w, int h)
{
	p_gen_fb = (PFNGLGENFRAMEBUFFERSPROC)SDL_GL_GetProcAddress("glGenFramebuffers");
	p_bind_fb = (PFNGLBINDFRAMEBUFFERPROC)SDL_GL_GetProcAddress("glBindFramebuffer");
	p_fb_tex = (PFNGLFRAMEBUFFERTEXTURE2DPROC)SDL_GL_GetProcAddress("glFramebufferTexture2D");
	p_fb_status = (PFNGLCHECKFRAMEBUFFERSTATUSPROC)SDL_GL_GetProcAddress("glCheckFramebufferStatus");
	if (!p_gen_fb || !p_bind_fb || !p_fb_tex || !p_fb_status)
		return 0;
	GLuint tex, fbo;
	glGenTextures(1, &tex);
	glBindTexture(GL_TEXTURE_2D, tex);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
	glBindTexture(GL_TEXTURE_2D, 0);
	p_gen_fb(1, &fbo);
	p_bind_fb(GL_FRAMEBUFFER, fbo);
	p_fb_tex(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, tex, 0);
	bool ok = p_fb_status(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;
	p_bind_fb(GL_FRAMEBUFFER, 0);
	return ok ? fbo : 0;
}

static bool shot_save(int w, int h, const char *path)
{
	uint8_t *px = xmalloc((size_t)w * h * 4), *row = xmalloc((size_t)w * 4);
	glPixelStorei(GL_PACK_ALIGNMENT, 1);
	glReadPixels(0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, px);
	for (int y = 0; y < h / 2; y++) {
		memcpy(row, px + (size_t)y * w * 4, (size_t)w * 4);
		memcpy(px + (size_t)y * w * 4, px + (size_t)(h - 1 - y) * w * 4, (size_t)w * 4);
		memcpy(px + (size_t)(h - 1 - y) * w * 4, row, (size_t)w * 4);
	}
	for (size_t i = 0; i < (size_t)w * h; i++)
		px[i * 4 + 3] = 255;
	bool ok = save_png(path, px, w, h);
	free(px);
	free(row);
	return ok;
}

/* ---- main ---- */

static void on_abort(int sig)
{
	(void)sig;
	fputs(APP_EXE ": stopped by a failed check\n", stderr);
	fflush(stderr);
	_Exit(3);
}

static void core_message(const char *text)
{
	ui_status("%s", text);
}

/* The --do actions from `from` up to a "frame" (the rest wait for a frame drawn); where the next
 * ones start. */
static int run_dos(int from, int *failed)
{
	for (int i = from; i < A.ndo; i++) {
		if (!strcmp(A.dos[i], "frame"))
			return i + 1;
		if (!ui_do(A.dos[i])) {
			fprintf(stderr, APP_EXE ": --do %s did nothing\n", A.dos[i]);
			(*failed)++;
		}
	}
	return A.ndo;
}

int main(int argc, char **argv)
{
	parse_args(argc, argv);
	if (A.check || A.format_at || A.selftest || A.mixer_check)
		win_attach_console();
	/* Nobody watches these runs: a failed check goes to the console, not to a dialog. */
	if (A.check || A.format_at || A.shot_path || A.selftest || A.mixer_check) {
		_set_error_mode(_OUT_TO_STDERR);
		signal(SIGABRT, on_abort);
	}
	sys_silent = true;
	sys_error_handler = on_error;
	win_init();
	CORE.status = core_message;
	CORE.scale = 1;

	if (A.selftest)
		return run_selftest(A.selftest);

	if (A.format_at)
		return run_format(argc, argv, A.format_at);
	char dir[1024] = "";
	if (A.check) {
		/* The game's names for the checker: with --dir only when --game names the game. */
		bool have_game = (!A.dir || A.game) && find_game(dir, sizeof dir);
		if (have_game && !game_open(dir))
			fprintf(stderr, APP_EXE ": the game's names are not checked: %s\n", G.errors);
		if (A.dir)
			return run_check(A.dir, NULL, NULL, A.out);
		if (!have_game) {
			fprintf(stderr, APP_EXE ": no game folder (use --game DIR or --dir DIR)\n");
			return 1;
		}
		return run_check(NULL, dir, A.mod, A.out);
	}
	if (A.mixer_check) {
		if (!find_game(dir, sizeof dir) || !game_open(dir)) {
			fprintf(stderr, APP_EXE ": no game folder (use --game DIR) %s\n", G.errors);
			return 1;
		}
		return mixer_check();
	}
	if (!find_game(dir, sizeof dir)) {
		if (A.shot_path)
			fprintf(stderr, APP_EXE ": no game folder (use --game DIR)\n");
		return 1;
	}

	SDL_SetHint(SDL_HINT_WINDOWS_DPI_AWARENESS, "permonitorv2");
	/* No sound device for --shot: nothing plays there. */
	if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER | (A.shot_path ? 0 : SDL_INIT_AUDIO)) != 0) {
		on_error(SDL_GetError());
		return 1;
	}
	if (!game_open(dir)) {
		char msg[4200];
		snprintf(msg, sizeof msg, "Could not open the game in %s: %s", dir, G.errors);
		on_error(msg);
		return 1;
	}
	if (!G.rq) {
		char msg[1400];
		snprintf(msg, sizeof msg, APP_NAME " writes scenes for Rance Quest Magnum and RQ MOD; %s in %s is another "
		         "game. Pick Rance Quest Magnum's folder.", G.title, dir);
		on_error(msg);
		if (!A.shot_path && !A.game) {
			char other[1024];
			if (pick_game(other, sizeof other)) {
				settings_set("game", other);
				win_relaunch(other);
			}
		}
		return 1;
	}
	if (!A.shot_path)
		settings_set("game", dir);

	SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
	SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);
	SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
	Uint32 flags = SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI;
	if (A.shot_path)
		flags |= SDL_WINDOW_HIDDEN;
	int ww = A.w, wh = A.h;
	if (A.shot_path) {
		CORE.scale = A.scale > 0 ? A.scale : 1;
	} else {
		CORE.scale = A.scale > 0 ? A.scale : display_scale(0);
		if (!A.size_given) {
			ww = (int)(A.w * CORE.scale);
			wh = (int)(A.h * CORE.scale);
		}
		SDL_Rect r;
		if (SDL_GetDisplayUsableBounds(0, &r) == 0) {
			if (ww > r.w - 40) ww = r.w - 40;
			if (wh > r.h - 60) wh = r.h - 60;
		}
	}
	g_window = SDL_CreateWindow(APP_NAME, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, ww, wh, flags);
	if (!g_window) {
		on_error(SDL_GetError());
		return 1;
	}
	SDL_SetWindowMinimumSize(g_window, 1000, 600);
	SDL_GLContext gl = SDL_GL_CreateContext(g_window);
	if (!gl) {
		on_error(SDL_GetError());
		return 1;
	}
	SDL_GL_MakeCurrent(g_window, gl);
	SDL_GL_SetSwapInterval(A.shot_path ? 0 : 1);

	igCreateContext(NULL);
	ImGuiIO *io = igGetIO_Nil();
	io->IniFilename = NULL;
	int look = A.look >= 0 ? A.look : A.shot_path ? LOOK_COPANDON : look_find(settings_get("look"));
	look_apply(look < 0 ? LOOK_COPANDON : look);
	fonts_load(G.font);
	problems_box = true;   /* the box's rules need the fonts: the window checks them, --check does not */
	ImGui_ImplSDL2_InitForOpenGL(g_window, gl);
	ImGui_ImplOpenGL3_Init("#version 130");

	media_init(true);
	UI.shot = A.shot_path != NULL;
	ui_init(dir, A.mod_dir, A.mod, A.file, A.select);
	int failed_dos = 0;
	int next_do = run_dos(0, &failed_dos);
	unsigned last_do_frame = 0;

	GLuint fbo = 0;
	if (A.shot_path) {
		fbo = shot_fbo(A.w, A.h);
		if (!fbo) {
			fprintf(stderr, APP_EXE ": no framebuffer for --shot\n");
			return 1;
		}
	}

	char title[1600] = "", want_title[1600];
	int code = 0;
	for (bool run = true; run; UI.frame++) {
		SDL_Event ev;
		while (SDL_PollEvent(&ev)) {
			ImGui_ImplSDL2_ProcessEvent(&ev);
			bool close = ev.type == SDL_QUIT
			          || (ev.type == SDL_WINDOWEVENT && ev.window.event == SDL_WINDOWEVENT_CLOSE);
			if (close && ui_wants_quit())
				run = false;
			if (!A.shot_path && !A.scale && ev.type == SDL_WINDOWEVENT && ev.window.event == SDL_WINDOWEVENT_DISPLAY_CHANGED) {
				float s = display_scale(ev.window.data1);
				if (s != CORE.scale) {
					CORE.scale = s;
					look_apply(LOOK);
				}
			}
		}
		ImGui_ImplOpenGL3_NewFrame();
		ImGui_ImplSDL2_NewFrame();
		igNewFrame();
		media_frame();
		ui_frame();
		igRender();
		/* The stage's textures, when it plays, before the window that shows them. */
		fx_render();
		if (UI.quit)
			run = false;
		ui_title(want_title, sizeof want_title);
		if (strcmp(title, want_title)) {
			snprintf(title, sizeof title, "%s", want_title);
			SDL_SetWindowTitle(g_window, title);
		}
		if (fbo)
			p_bind_fb(GL_FRAMEBUFFER, fbo);
		glViewport(0, 0, (int)(io->DisplaySize.x * io->DisplayFramebufferScale.x), (int)(io->DisplaySize.y * io->DisplayFramebufferScale.y));
		ImVec4_c bg = colv(C_BG);
		glClearColor(bg.x, bg.y, bg.z, 1);
		glClear(GL_COLOR_BUFFER_BIT);
		ImGui_ImplOpenGL3_RenderDrawData(igGetDrawData());
		if (fbo) {
			/* The shot waits for the pictures it shows (a few seconds at most). */
			if (UI.frame > 12 && (media_idle() || UI.frame > 900) && next_do < A.ndo) {
				next_do = run_dos(next_do, &failed_dos);   /* a frame drawn: the next actions */
				last_do_frame = UI.frame;
			} else if (UI.frame > 12 && UI.frame > last_do_frame + 3 && (media_idle() || UI.frame > 900)) {
				/* A few frames after the last actions: Dear ImGui shows a tab or a scroll they set
				 * a frame later. */
				code = shot_save(A.w, A.h, A.shot_path) ? 0 : 1;
				if (code)
					fprintf(stderr, APP_EXE ": could not write %s\n", A.shot_path);
				else if (failed_dos)
					code = 1;
				run = false;
			}
			p_bind_fb(GL_FRAMEBUFFER, 0);
			SDL_Delay(4);
		} else {
			SDL_GL_SwapWindow(g_window);
			if (SDL_GetWindowFlags(g_window) & SDL_WINDOW_MINIMIZED)
				SDL_Delay(50);
		}
	}
	fx_shutdown();
	mixer_shutdown();
	ImGui_ImplOpenGL3_Shutdown();
	ImGui_ImplSDL2_Shutdown();
	igDestroyContext(NULL);
	SDL_GL_DeleteContext(gl);
	SDL_DestroyWindow(g_window);
	SDL_Quit();
	_Exit(code);
}
