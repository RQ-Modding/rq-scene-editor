# Lua

Lua 5.4.7 from [lua.org](https://www.lua.org/), unchanged, built into the editor in one piece
(`onelua.c`) to run RQ MOD's scene checker. Lua's licence (MIT) is at the end of `lua.h`.

The files are the ones RQ MOD's engine carries, without its one change to the virtual machine (a
tail call into another file's function), which only the loader's mods need.
