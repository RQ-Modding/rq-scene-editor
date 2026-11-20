# RQ Scene Editor: a window to write RQ MOD's scene files (a mod's scenes as JSON) for Rance Quest
# Magnum, without Lua and without typing JSON. Built on Windows with MSYS2 MINGW64; see README.md.
#
# Alone:           meson setup build && ninja -C build
#                  (Rance Browser and libsys4 come as the submodules in subprojects/)
# Inside RQ MOD:   the engine calls subproject('rq-scene-editor'), and meson shares one copy of
#                  Rance Browser and of libsys4 with the engine's own.
#
# It reads the game through Rance Browser's readers (the rance_core library): the game folder, its
# CGs, music and sounds, RQ MOD's names, Dear ImGui (cimgui) on SDL2 and OpenGL 3.
project('rq-scene-editor', 'c', 'cpp',
        version : '0.5.0',
        meson_version : '>= 0.59',
        default_options : ['c_std=c11', 'cpp_std=c++17', 'warning_level=1'])
add_project_arguments('-D_DEFAULT_SOURCE', language : 'c')

rance_core_dep = subproject('rance-browser').get_variable('rance_core_dep')

c_warnings = ['-Wno-unused-parameter', '-Wno-sign-compare', '-Wno-missing-field-initializers',
              '-Wno-format-truncation']
editor_version = meson.project_version()
build_note = get_option('build_note')

executable('rq-scene-editor',
    ['src/main.c', 'src/look.c', 'src/json.c', 'src/doc.c', 'src/mods.c', 'src/ui.c',
     'src/steps.c', 'src/win.c', 'src/check.c', 'src/media.c', 'src/stage.c', 'src/fields.c',
     'src/fx.c', 'src/play.c', 'src/worldmap.c', 'src/flash.c', 'src/mixer.c', 'src/picker.c'],
    c_args : c_warnings + [
              '-DEDITOR_VERSION="' + editor_version + '"',
              '-DEDITOR_BUILD_NOTE="' + build_note + '"'],
    include_directories : include_directories('src'),
    dependencies : rance_core_dep,
    link_args : ['-static', '-lstdc++', '-lcomdlg32', '-lole32', '-lshell32', '-luuid'],
    win_subsystem : 'windows',
    install : true)
