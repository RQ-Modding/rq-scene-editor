/* The running game's pipe (\\.\pipe\rqmod), spoken as RQ MOD's rqmod-ctl speaks it: one command a
 * connection, a line with its words, and the answer read to its end ("ok", "err ...", or what the
 * command gives). The talking happens on a thread of its own, so the window never waits for the
 * game: a command is queued with pipe_send and its answer taken with pipe_take, in order.
 *
 * The game serves one connection at a time and reads at most 511 bytes of a command: a longer one
 * is refused here. While another program talks to it (rqmod-ctl, an agent), the editor waits its
 * turn a little. */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <SDL.h>

#include "editor.h"

#define QMAX 16
#define LINE_MAX_GAME 511     /* the game's read: 512 bytes with its end */
#define ANSWER_MAX (256 * 1024)

struct req {
	int id, tag;
	char line[LINE_MAX_GAME + 1];
};

static SDL_mutex *mx;
static SDL_cond *cv;
static struct req queue[QMAX];
static int nqueue;
static struct pipe_answer done[QMAX];
static int ndone;
static int next_id, busy_id;
static wchar_t pipe_name[128] = L"\\\\.\\pipe\\rqmod";

/* One command: PIPE_ANSWER with the text, PIPE_NO_GAME when no game has the pipe, PIPE_NO_ANSWER
 * when it stayed busy or the talk broke. */
static int talk(const char *line, char **out)
{
	*out = NULL;
	HANDLE h = INVALID_HANDLE_VALUE;
	for (int tries = 0; tries < 12; tries++) {
		h = CreateFileW(pipe_name, GENERIC_READ | GENERIC_WRITE, 0, NULL, OPEN_EXISTING, 0, NULL);
		if (h != INVALID_HANDLE_VALUE)
			break;
		DWORD e = GetLastError();
		if (e != ERROR_PIPE_BUSY)
			return PIPE_NO_GAME;
		WaitNamedPipeW(pipe_name, 250);   /* another program's turn: wait for the next instance */
	}
	if (h == INVALID_HANDLE_VALUE)
		return PIPE_NO_ANSWER;
	char msg[LINE_MAX_GAME + 2];
	int n = snprintf(msg, sizeof msg, "%s\n", line);
	DWORD w = 0;
	if (n <= 0 || n > LINE_MAX_GAME || !WriteFile(h, msg, (DWORD)n, &w, NULL) || w != (DWORD)n) {
		CloseHandle(h);
		return PIPE_NO_ANSWER;
	}
	size_t cap = 4096, got = 0;
	char *buf = malloc(cap);
	for (;;) {
		if (got + 1 >= cap) {
			if (cap >= ANSWER_MAX)
				break;
			cap *= 2;
			buf = realloc(buf, cap);
		}
		DWORD r = 0;
		if (!ReadFile(h, buf + got, (DWORD)(cap - 1 - got), &r, NULL) || r == 0)
			break;
		got += r;
	}
	CloseHandle(h);
	buf[got] = 0;
	/* The answer without its last line end. */
	while (got && (buf[got - 1] == '\n' || buf[got - 1] == '\r'))
		buf[--got] = 0;
	*out = buf;
	return got ? PIPE_ANSWER : PIPE_NO_ANSWER;
}

static int worker(void *unused)
{
	(void)unused;
	for (;;) {
		SDL_LockMutex(mx);
		while (!nqueue)
			SDL_CondWait(cv, mx);
		struct req r = queue[0];
		memmove(queue, queue + 1, (size_t)(nqueue - 1) * sizeof *queue);
		nqueue--;
		busy_id = r.id;
		SDL_UnlockMutex(mx);

		char *text = NULL;
		int how = talk(r.line, &text);

		SDL_LockMutex(mx);
		if (ndone == QMAX) {   /* nobody took the oldest: dropped */
			free(done[0].text);
			memmove(done, done + 1, (size_t)(QMAX - 1) * sizeof *done);
			ndone--;
		}
		done[ndone].id = r.id;
		done[ndone].tag = r.tag;
		done[ndone].how = how;
		done[ndone].text = text;
		ndone++;
		busy_id = 0;
		SDL_UnlockMutex(mx);
	}
	return 0;
}

void pipe_set_name(const char *name)
{
	char full[200];
	snprintf(full, sizeof full, "\\\\.\\pipe\\%s", name);
	MultiByteToWideChar(CP_UTF8, 0, full, -1, pipe_name, (int)(sizeof pipe_name / sizeof pipe_name[0]));
}

int pipe_send(int tag, const char *line)
{
	if (!line || strlen(line) > LINE_MAX_GAME - 1)
		return 0;
	if (!mx) {
		mx = SDL_CreateMutex();
		cv = SDL_CreateCond();
		SDL_Thread *t = SDL_CreateThread(worker, "pipe", NULL);
		if (!mx || !cv || !t)
			return 0;
		SDL_DetachThread(t);
	}
	SDL_LockMutex(mx);
	int id = 0;
	if (nqueue < QMAX) {
		id = ++next_id;
		queue[nqueue].id = id;
		queue[nqueue].tag = tag;
		snprintf(queue[nqueue].line, sizeof queue[nqueue].line, "%s", line);
		nqueue++;
		SDL_CondSignal(cv);
	}
	SDL_UnlockMutex(mx);
	return id;
}

bool pipe_take(struct pipe_answer *out)
{
	if (!mx)
		return false;
	SDL_LockMutex(mx);
	bool has = ndone > 0;
	if (has) {
		*out = done[0];
		memmove(done, done + 1, (size_t)(ndone - 1) * sizeof *done);
		ndone--;
	}
	SDL_UnlockMutex(mx);
	return has;
}

int pipe_waiting(void)
{
	if (!mx)
		return 0;
	SDL_LockMutex(mx);
	int n = nqueue + (busy_id ? 1 : 0);
	SDL_UnlockMutex(mx);
	return n;
}

size_t pipe_line_max(void)
{
	return LINE_MAX_GAME - 1;
}
