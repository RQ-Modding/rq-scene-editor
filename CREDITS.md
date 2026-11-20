# Credits

RQ Scene Editor is GPL-2.0-or-later (`COPYING`). It is built from these, each under its own license:

| What | By | License | Where |
| --- | --- | --- | --- |
| [Rance Browser](https://github.com/RQ-Modding/rance-browser)'s readers, the `rance_core` library | RQ Modding | GPL-2.0-or-later | `subprojects/rance-browser` (submodule): the game folder, its archives, pictures, music and sounds, RQ MOD's names |
| [xsystem4](https://github.com/nunuhara/xsystem4)'s transitions (RQ MOD's engine) | Nunuhara Cabbage | GPL-2.0-or-later | `src/fx_shaders.h` (its effects' shaders, copied) and `src/fx.c` (the effects it draws in C, written again as shaders of the same maths) |
| xsystem4's Flash player | kichikuou (`parts/flash.c`), Nunuhara Cabbage (`parts/render.c`) | GPL-2.0-or-later | `src/flash.c`, a small player after them for the world map's tag |
| [libsys4](https://github.com/nunuhara/libsys4) | Nunuhara Cabbage and contributors | GPL-2.0-or-later | `subprojects/libsys4` (submodule): the game's archives and picture formats, files with UTF-8 names |
| RQ MOD's scene checker | RQ Modding | GPL-2.0-or-later | `data/scene_check.lua`, a copy of [RQ MOD](https://github.com/RQ-Modding/RQ-Mod)'s `src/lua/scene_check.lua` (inside RQ MOD the engine's own file is built in), run as the game runs it |
| [Lua 5.4.7](https://www.lua.org/) | Lua.org, PUC-Rio | MIT | `third_party/lua` (unchanged, `README.md` there): runs the scene checker |
| [cJSON](https://github.com/DaveGamble/cJSON) 1.7.12 | Dave Gamble and contributors | MIT | through `rance_core`: the scene files, read with the same cJSON as RQ MOD's loader |
| [Dear ImGui 1.92.9](https://github.com/ocornut/imgui) | Omar Cornut and contributors | MIT | through `rance_core` |
| [cimgui 1.92.9b](https://github.com/cimgui/cimgui) | Stephan Dilly and contributors | MIT | through `rance_core` |
| [SDL2](https://www.libsdl.org/) | Sam Lantinga and contributors | zlib | linked in |
| [libsndfile](https://libsndfile.github.io/libsndfile/), with libogg, libvorbis, FLAC, Opus, mpg123 and LAME | their authors | LGPL-2.1-or-later and others | linked in, through `rance_core` |
| libjpeg-turbo, libwebp, libpng, zlib, libdeflate | their authors | BSD-style, zlib, MIT | linked in, through libsys4 |

The release zip has each library's license in `licenses/`. MSYS2 installs no license file for
libvorbis and LAME, so this repository carries them in `licenses/`.

How the editor reads a scene file (a byte-order mark skipped, the size limit, a field given twice,
the depth limit and the messages) follows RQ MOD's loader, so that a file the game refuses is refused
here with the same words.
