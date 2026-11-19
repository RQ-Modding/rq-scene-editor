/* The two looks and the fonts.
 *
 * Copandon, the default: dark green and gold, from the game's own colours. Parchment: a light look
 * for long writing. The top bar's switch changes the look at once (ImGui's colours are a table) and
 * the settings file keeps it. The game's own pictures look the same in both. */
#include <stdio.h>
#include <string.h>

#include "system4.h"
#include "system4/file.h"

#include "editor.h"

int LOOK = LOOK_COPANDON;
const char *const look_keys[LOOK_COUNT] = { "copandon", "parchment" };
const char *const look_names[LOOK_COUNT] = { "Copandon", "Parchment" };

ImFont *F_UI, *F_BOLD, *F_GAME, *F_MONO;

#define RGB(x) (((uint32_t)(x) << 8) | 0xFF)
#define RGBA(r, g, b, a) (((uint32_t)(r) << 24) | ((uint32_t)(g) << 16) | ((uint32_t)(b) << 8) | (uint32_t)((a) * 255 + 0.5))

static const uint32_t palette[LOOK_COUNT][C_COUNT] = {
	[LOOK_COPANDON] = {
		[C_PAGE] = RGB(0x0b100d), [C_BG] = RGB(0x101813), [C_PANEL] = RGB(0x142019), [C_CHILD] = RGB(0x18261e),
		[C_RAISE] = RGB(0x203128), [C_BORDER] = RGB(0x2b3d32), [C_LINE] = RGB(0x22332a),
		[C_TEXT] = RGB(0xece5cf), [C_DIM] = RGB(0x9ba591), [C_FAINT] = RGB(0x677160),
		[C_ACCENT] = RGB(0xcfa64c), [C_ACCENT_HI] = RGB(0xe0bb63), [C_ACCENT_INK] = RGB(0x1c1505),
		[C_BTN] = RGB(0x22342a), [C_BTN_HI] = RGB(0x2c4436), [C_FIELD] = RGB(0x0d1510), [C_FIELD_B] = RGB(0x2d4034),
		[C_SEL] = RGBA(207, 166, 76, 0.15), [C_SEL_BAR] = RGB(0xcfa64c), [C_HOVER] = RGBA(236, 229, 207, 0.05),
		[C_TITLE] = RGB(0x0a0f0c),
		[C_BAD] = RGB(0xe0664f), [C_WARN] = RGB(0xe3ab4c), [C_OK] = RGB(0x8cc46f), [C_PLAY] = RGB(0x7fc4d6),
		[C_T_LINE] = RGB(0x6fb8c9), [C_T_PIC] = RGB(0xb195e0), [C_T_SND] = RGB(0xe0a15a), [C_T_FLOW] = RGB(0x9acb6e),
		[C_T_DATA] = RGB(0xe08aa0), [C_T_NOTE] = RGB(0x8a927f),
		[C_MODAL_DIM] = RGBA(3, 6, 4, 0.62),
	},
	[LOOK_PARCHMENT] = {
		[C_PAGE] = RGB(0xcfc3a6), [C_BG] = RGB(0xe8dec5), [C_PANEL] = RGB(0xefe6d0), [C_CHILD] = RGB(0xf6f0e1),
		[C_RAISE] = RGB(0xe3d7b9), [C_BORDER] = RGB(0xc7b68d), [C_LINE] = RGB(0xddd0b0),
		[C_TEXT] = RGB(0x2a2216), [C_DIM] = RGB(0x6c6049), [C_FAINT] = RGB(0x998b6c),
		[C_ACCENT] = RGB(0x2f6a45), [C_ACCENT_HI] = RGB(0x387c51), [C_ACCENT_INK] = RGB(0xf6f0e1),
		[C_BTN] = RGB(0xe0d3b1), [C_BTN_HI] = RGB(0xd6c69e), [C_FIELD] = RGB(0xfbf7ec), [C_FIELD_B] = RGB(0xc9b88f),
		[C_SEL] = RGBA(47, 106, 69, 0.14), [C_SEL_BAR] = RGB(0x2f6a45), [C_HOVER] = RGBA(42, 34, 22, 0.05),
		[C_TITLE] = RGB(0x3a3122),
		[C_BAD] = RGB(0xb23a26), [C_WARN] = RGB(0xa66a10), [C_OK] = RGB(0x2f7a33), [C_PLAY] = RGB(0x1f6f8c),
		[C_T_LINE] = RGB(0x1f6f8c), [C_T_PIC] = RGB(0x6b4aa8), [C_T_SND] = RGB(0xa3581a), [C_T_FLOW] = RGB(0x3b7a2a),
		[C_T_DATA] = RGB(0xa33a5a), [C_T_NOTE] = RGB(0x7d7462),
		[C_MODAL_DIM] = RGBA(40, 32, 18, 0.45),
	},
};

int look_find(const char *key)
{
	for (int i = 0; key && i < LOOK_COUNT; i++)
		if (!strcmp(key, look_keys[i]))
			return i;
	return -1;
}

ImVec4_c colv(int c)
{
	uint32_t x = palette[LOOK][c];
	return v4((float)((x >> 24) & 0xFF) / 255.0f, (float)((x >> 16) & 0xFF) / 255.0f,
	          (float)((x >> 8) & 0xFF) / 255.0f, (float)(x & 0xFF) / 255.0f);
}

ImU32 col(int c)
{
	return igColorConvertFloat4ToU32(colv(c));
}

ImU32 col_a(int c, float alpha)
{
	ImVec4_c v = colv(c);
	v.w *= alpha;
	return igColorConvertFloat4ToU32(v);
}

static ImVec4_c clear(void)
{
	return v4(0, 0, 0, 0);
}

static ImVec4_c with_alpha(int c, float a)
{
	ImVec4_c v = colv(c);
	v.w = a;
	return v;
}

void look_apply(int look)
{
	if (look < 0 || look >= LOOK_COUNT)
		look = LOOK_COPANDON;
	LOOK = look;
	/* From ImGui's defaults each time, so a new scale does not multiply the last one. */
	ImGuiStyle *fresh = ImGuiStyle_ImGuiStyle();
	*igGetStyle() = *fresh;
	ImGuiStyle_destroy(fresh);
	ImGuiStyle *s = igGetStyle();
	ImVec4_c *c = s->Colors;
	c[ImGuiCol_Text] = colv(C_TEXT);
	c[ImGuiCol_TextDisabled] = colv(C_FAINT);
	c[ImGuiCol_WindowBg] = colv(C_BG);
	c[ImGuiCol_ChildBg] = clear();
	c[ImGuiCol_PopupBg] = colv(C_PANEL);
	c[ImGuiCol_Border] = colv(C_BORDER);
	c[ImGuiCol_BorderShadow] = clear();
	c[ImGuiCol_FrameBg] = colv(C_FIELD);
	c[ImGuiCol_FrameBgHovered] = colv(C_FIELD);
	c[ImGuiCol_FrameBgActive] = colv(C_FIELD);
	c[ImGuiCol_TitleBg] = colv(C_RAISE);   /* a dialog's head in the raised colour, so its title reads in both looks */
	c[ImGuiCol_TitleBgActive] = colv(C_RAISE);
	c[ImGuiCol_TitleBgCollapsed] = colv(C_RAISE);
	c[ImGuiCol_MenuBarBg] = colv(C_BG);
	c[ImGuiCol_ScrollbarBg] = clear();
	c[ImGuiCol_ScrollbarGrab] = colv(C_BTN_HI);
	c[ImGuiCol_ScrollbarGrabHovered] = colv(C_BORDER);
	c[ImGuiCol_ScrollbarGrabActive] = colv(C_FAINT);
	c[ImGuiCol_CheckMark] = colv(C_ACCENT);
	c[ImGuiCol_CheckboxSelectedBg] = colv(C_FIELD);
	c[ImGuiCol_SliderGrab] = colv(C_ACCENT);
	c[ImGuiCol_SliderGrabActive] = colv(C_ACCENT_HI);
	c[ImGuiCol_Button] = colv(C_BTN);
	c[ImGuiCol_ButtonHovered] = colv(C_BTN_HI);
	c[ImGuiCol_ButtonActive] = colv(C_RAISE);
	c[ImGuiCol_Header] = colv(C_SEL);
	c[ImGuiCol_HeaderHovered] = colv(C_HOVER);
	c[ImGuiCol_HeaderActive] = colv(C_SEL);
	c[ImGuiCol_Separator] = colv(C_LINE);
	c[ImGuiCol_SeparatorHovered] = colv(C_ACCENT);
	c[ImGuiCol_SeparatorActive] = colv(C_ACCENT);
	c[ImGuiCol_ResizeGrip] = clear();
	c[ImGuiCol_ResizeGripHovered] = colv(C_ACCENT);
	c[ImGuiCol_ResizeGripActive] = colv(C_ACCENT);
	c[ImGuiCol_InputTextCursor] = colv(C_TEXT);
	c[ImGuiCol_Tab] = clear();
	c[ImGuiCol_TabHovered] = colv(C_BTN_HI);
	c[ImGuiCol_TabSelected] = colv(C_RAISE);
	c[ImGuiCol_TabSelectedOverline] = colv(C_ACCENT);
	c[ImGuiCol_TabDimmed] = clear();
	c[ImGuiCol_TabDimmedSelected] = colv(C_RAISE);
	c[ImGuiCol_TabDimmedSelectedOverline] = clear();
	c[ImGuiCol_TableHeaderBg] = colv(C_CHILD);
	c[ImGuiCol_TableBorderStrong] = colv(C_BORDER);
	c[ImGuiCol_TableBorderLight] = colv(C_LINE);
	c[ImGuiCol_TableRowBg] = clear();
	c[ImGuiCol_TableRowBgAlt] = colv(C_HOVER);
	c[ImGuiCol_TextLink] = colv(C_ACCENT);
	c[ImGuiCol_TextSelectedBg] = with_alpha(C_ACCENT, 0.35f);
	c[ImGuiCol_TreeLines] = colv(C_LINE);
	c[ImGuiCol_DragDropTarget] = colv(C_ACCENT);
	c[ImGuiCol_UnsavedMarker] = colv(C_ACCENT);
	c[ImGuiCol_NavCursor] = colv(C_ACCENT);
	c[ImGuiCol_NavWindowingHighlight] = colv(C_ACCENT);
	c[ImGuiCol_NavWindowingDimBg] = colv(C_MODAL_DIM);
	c[ImGuiCol_ModalWindowDimBg] = colv(C_MODAL_DIM);

	s->WindowRounding = 0;
	s->ChildRounding = 4;
	s->FrameRounding = 4;
	s->PopupRounding = 6;
	s->GrabRounding = 4;
	s->TabRounding = 4;
	s->ScrollbarRounding = 6;
	s->WindowBorderSize = 0;
	s->PopupBorderSize = 1;
	s->FrameBorderSize = 1;
	s->ChildBorderSize = 1;
	s->WindowPadding = v2(12, 10);
	s->FramePadding = v2(9, 5);
	s->ItemSpacing = v2(8, 6);
	s->ItemInnerSpacing = v2(6, 4);
	s->ScrollbarSize = 12;
	s->TabBarBorderSize = 1;
	s->SeparatorTextBorderSize = 1;
	s->SeparatorTextPadding = v2(0, 3);
	s->DisabledAlpha = 0.4f;   /* a button that does nothing now reads as one in both looks */
	ImGuiStyle_ScaleAllSizes(s, CORE.scale);
	s->FontScaleDpi = CORE.scale;
}

/* ---- the fonts ---- */

static const char *ui_fonts[] = {
	"C:/Windows/Fonts/segoeui.ttf",
	"C:/Windows/Fonts/arial.ttf",
};

static const char *bold_fonts[] = {
	"C:/Windows/Fonts/seguisb.ttf",
	"C:/Windows/Fonts/segoeuib.ttf",
	"C:/Windows/Fonts/arialbd.ttf",
};

static const char *mono_fonts[] = {
	"C:/Windows/Fonts/consola.ttf",
	"C:/Windows/Fonts/cour.ttf",
};

/* Arrows and marks (↵ ◆ ↪ ▾ ☾ ✓) the text fonts lack. */
static const char *symbol_fonts[] = {
	"C:/Windows/Fonts/seguisym.ttf",
};

/* Japanese, for a game command's name or a line written in Japanese. */
static const char *jp_fonts[] = {
	"C:/Windows/Fonts/YuGothM.ttc",
	"C:/Windows/Fonts/meiryo.ttc",
	"C:/Windows/Fonts/msgothic.ttc",
};

static ImFont *first_of(const char *const *paths, size_t n, float size)
{
	ImGuiIO *io = igGetIO_Nil();
	for (size_t i = 0; i < n; i++)
		if (file_exists(paths[i]))
			return ImFontAtlas_AddFontFromFileTTF(io->Fonts, paths[i], size, NULL, NULL);
	return NULL;
}

static void merge_first(const char *const *paths, size_t n, float size)
{
	ImGuiIO *io = igGetIO_Nil();
	for (size_t i = 0; i < n; i++) {
		if (!file_exists(paths[i]))
			continue;
		ImFontConfig *cfg = ImFontConfig_ImFontConfig();
		cfg->MergeMode = true;
		ImFontAtlas_AddFontFromFileTTF(io->Fonts, paths[i], size, cfg, NULL);
		ImFontConfig_destroy(cfg);
		return;
	}
}

#define COUNT(a) (sizeof (a) / sizeof (a)[0])

void fonts_load(const char *game_font)
{
	ImGuiIO *io = igGetIO_Nil();
	F_UI = first_of(ui_fonts, COUNT(ui_fonts), UI_SIZE);
	if (!F_UI)
		F_UI = ImFontAtlas_AddFontDefault(io->Fonts, NULL);
	merge_first(symbol_fonts, COUNT(symbol_fonts), UI_SIZE);
	merge_first(jp_fonts, COUNT(jp_fonts), UI_SIZE);

	F_BOLD = first_of(bold_fonts, COUNT(bold_fonts), UI_SIZE);
	if (F_BOLD)
		merge_first(symbol_fonts, COUNT(symbol_fonts), UI_SIZE);
	else
		F_BOLD = F_UI;

	F_MONO = first_of(mono_fonts, COUNT(mono_fonts), MONO_SIZE);
	if (F_MONO) {
		merge_first(symbol_fonts, COUNT(symbol_fonts), MONO_SIZE);
		merge_first(jp_fonts, COUNT(jp_fonts), MONO_SIZE);
	} else {
		F_MONO = F_UI;
	}

	/* The game's font as it is: a letter it cannot draw shows as the game shows it. */
	F_GAME = NULL;
	if (game_font && game_font[0] && file_exists(game_font))
		F_GAME = ImFontAtlas_AddFontFromFileTTF(io->Fonts, game_font, GAME_SIZE, NULL, NULL);
	if (!F_GAME)
		F_GAME = F_UI;
	io->FontDefault = F_UI;
}
