/* Windows: the folder dialog, the program's own folder, a console for --check, the settings file
 * beside the program (rq-scene-editor.json: the game folder, the look, the last mod and file), a
 * file moved over another for saving, and starting the program again on another game. */
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <shlobj.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <SDL_syswm.h>

#include "system4.h"
#include "system4/file.h"

#include "editor.h"

extern SDL_Window *g_window;

static cJSON *settings;
static char settings_path[1100];

static wchar_t *wide(const char *s)
{
	int n = MultiByteToWideChar(CP_UTF8, 0, s, -1, NULL, 0);
	wchar_t *w = xmalloc((n > 0 ? n : 1) * sizeof(wchar_t));
	if (n <= 0 || !MultiByteToWideChar(CP_UTF8, 0, s, -1, w, n))
		w[0] = 0;
	return w;
}

static void narrow(const wchar_t *w, char *out, size_t n)
{
	if (!WideCharToMultiByte(CP_UTF8, 0, w, -1, out, (int)n, NULL, NULL))
		out[0] = 0;
}

static HWND owner(void)
{
	SDL_SysWMinfo info;
	SDL_VERSION(&info.version);
	if (g_window && SDL_GetWindowWMInfo(g_window, &info))
		return info.info.win.window;
	return NULL;
}

bool win_exe_dir(char *out, size_t n)
{
	wchar_t w[MAX_PATH * 2];
	DWORD len = GetModuleFileNameW(NULL, w, MAX_PATH * 2);
	if (!len || len >= MAX_PATH * 2)
		return false;
	wchar_t *slash = wcsrchr(w, L'\\');
	if (slash)
		*slash = 0;
	narrow(w, out, n);
	for (char *p = out; *p; p++)
		if (*p == '\\')
			*p = '/';
	return out[0] != 0;
}

void win_init(void)
{
	CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
	char dir[1024];
	if (!win_exe_dir(dir, sizeof dir))
		return;
	snprintf(settings_path, sizeof settings_path, "%s/rq-scene-editor.json", dir);
	size_t size = 0;
	uint8_t *raw = file_read(settings_path, &size);
	if (raw) {
		char *text = xmalloc(size + 1);
		memcpy(text, raw, size);
		text[size] = 0;
		settings = cJSON_Parse(text);
		free(text);
		free(raw);
	}
	if (!cJSON_IsObject(settings)) {
		cJSON_Delete(settings);
		settings = cJSON_CreateObject();
	}
}

bool win_relaunch(const char *game_dir)
{
	wchar_t exe[MAX_PATH * 2];
	DWORD len = GetModuleFileNameW(NULL, exe, MAX_PATH * 2);
	if (!len || len >= MAX_PATH * 2)
		return false;
	wchar_t *dir = wide(game_dir);
	size_t n = wcslen(exe) + wcslen(dir) + 32;
	wchar_t *cmd = xmalloc(n * sizeof(wchar_t));
	_snwprintf(cmd, n, L"\"%ls\" --game \"%ls\"", exe, dir);
	STARTUPINFOW si = { .cb = sizeof si };
	PROCESS_INFORMATION pi;
	bool ok = CreateProcessW(exe, cmd, NULL, NULL, FALSE, 0, NULL, NULL, &si, &pi);
	if (ok) {
		CloseHandle(pi.hProcess);
		CloseHandle(pi.hThread);
	}
	free(cmd);
	free(dir);
	return ok;
}

bool win_replace(const char *from, const char *to)
{
	wchar_t *a = wide(from), *b = wide(to);
	bool ok = MoveFileExW(a, b, MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != 0;
	free(a);
	free(b);
	return ok;
}

bool win_stat(const char *path, long long *mtime, long long *size)
{
	wchar_t *w = wide(path);
	WIN32_FILE_ATTRIBUTE_DATA a;
	bool ok = GetFileAttributesExW(w, GetFileExInfoStandard, &a) != 0;
	free(w);
	if (!ok)
		return false;
	*mtime = (long long)(((unsigned long long)a.ftLastWriteTime.dwHighDateTime << 32) | a.ftLastWriteTime.dwLowDateTime);
	*size = (long long)(((unsigned long long)a.nFileSizeHigh << 32) | a.nFileSizeLow);
	return true;
}

const char *settings_get(const char *key)
{
	cJSON *v = cJSON_GetObjectItemCaseSensitive(settings, key);
	return cJSON_IsString(v) ? v->valuestring : NULL;
}

void settings_set(const char *key, const char *value)
{
	if (!settings || !settings_path[0])
		return;
	const char *old = settings_get(key);
	if (old && value && !strcmp(old, value))
		return;
	cJSON_DeleteItemFromObjectCaseSensitive(settings, key);
	if (value)
		cJSON_AddStringToObject(settings, key, value);
	char *text = cJSON_Print(settings);
	if (text) {
		file_write(settings_path, (uint8_t *)text, strlen(text));
		free(text);
	}
}

/* The settings as a whole, for a value that is more than a text (the values for Play). */
cJSON *settings_tree(void)
{
	return settings_path[0] ? settings : NULL;
}

void settings_save(void)
{
	if (!settings || !settings_path[0])
		return;
	char *text = cJSON_Print(settings);
	if (text) {
		file_write(settings_path, (uint8_t *)text, strlen(text));
		free(text);
	}
}

static int CALLBACK folder_cb(HWND hwnd, UINT msg, LPARAM lp, LPARAM data)
{
	(void)lp;
	if (msg == BFFM_INITIALIZED && data)
		SendMessageW(hwnd, BFFM_SETSELECTIONW, TRUE, data);
	return 0;
}

bool win_pick_folder(const char *title, char *out, size_t n)
{
	wchar_t *wt = wide(title);
	const char *last = settings_get("folder");
	wchar_t *wl = last ? wide(last) : NULL;
	BROWSEINFOW bi = { 0 };
	bi.hwndOwner = owner();
	bi.lpszTitle = wt;
	bi.ulFlags = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE;
	bi.lpfn = folder_cb;
	bi.lParam = (LPARAM)wl;
	PIDLIST_ABSOLUTE pidl = SHBrowseForFolderW(&bi);
	bool ok = false;
	if (pidl) {
		wchar_t path[MAX_PATH * 2];
		if (SHGetPathFromIDListW(pidl, path)) {
			narrow(path, out, n);
			ok = out[0] != 0;
		}
		CoTaskMemFree(pidl);
	}
	free(wt);
	free(wl);
	if (ok)
		settings_set("folder", out);
	return ok;
}

void win_message(const char *title, const char *text)
{
	wchar_t *wt = wide(title), *wx = wide(text);
	MessageBoxW(owner(), wx, wt, MB_OK | MB_ICONINFORMATION);
	free(wt);
	free(wx);
}

/* --check prints to the console it was started from, unless its output already goes somewhere. */
void win_attach_console(void)
{
	HANDLE h = GetStdHandle(STD_OUTPUT_HANDLE);
	if (h && h != INVALID_HANDLE_VALUE && GetFileType(h) != FILE_TYPE_UNKNOWN)
		return;
	if (AttachConsole(ATTACH_PARENT_PROCESS)) {
		freopen("CONOUT$", "w", stdout);
		freopen("CONOUT$", "w", stderr);
	}
}

const char *path_shown(const char *path, char *out, size_t n)
{
	snprintf(out, n, "%s", path ? path : "");
	for (char *c = out; *c; c++)
		if (*c == '/')
			*c = '\\';
	size_t k = strlen(out);
	while (k > 3 && out[k - 1] == '\\')
		out[--k] = 0;
	return out;
}
