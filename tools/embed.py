# Turns a file into a C array, so the editor carries it inside the exe.
# Usage: embed.py IN OUT.c NAME [DEPFILE]  ->  const char NAME[]; const unsigned NAME_len;
# With DEPFILE it also writes a make-style dependency file naming IN, so the build runs again when IN
# changes even though IN may sit outside the project (RQ MOD passes its own scene_check.lua).
import sys


def depfile_path(p):
    return p.replace("\\", "/").replace(" ", "\\ ")


src, out, name = sys.argv[1], sys.argv[2], sys.argv[3]
dep = sys.argv[4] if len(sys.argv) > 4 else None
data = open(src, "rb").read()
with open(out, "w", encoding="utf-8", newline="\n") as f:
    f.write("/* generated from %s at compile, do not edit */\n" % src.replace("\\", "/").split("/")[-1])
    f.write("const unsigned %s_len = %d;\n" % (name, len(data)))
    f.write("const char %s[] = {\n" % name)
    for i in range(0, len(data), 16):
        f.write("\t" + ", ".join("0x%02x" % b for b in data[i:i + 16]) + ",\n")
    f.write("\t0\n};\n")
if dep:
    with open(dep, "w", encoding="utf-8", newline="\n") as f:
        f.write("%s: %s\n" % (depfile_path(out), depfile_path(src)))
