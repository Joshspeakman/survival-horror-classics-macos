#!/bin/bash
# build_gtasa.sh <GTA San Andreas folder> [outdir]
# Recompile the user's own Steam copy of Grand Theft Auto: San Andreas (the Mac release, app 12250: the Windows
# gta_sa.exe 3.0 inside TransGaming's Cider wrapper, Grand Theft Auto - San Andreas.app/Contents/Resources/transgaming/
# c_drive/Program Files/Rockstar Games/GTA San Andreas) into C and link it with the native runtime. The Steam wrapper
# (SteamStub 2.1) is undone on a copy in the build folder (tools/steamstub21.py). The DLLs it loads are recompiled with
# it as further images: the Visual C++ 7.1 runtime (msvcr71.dll, msvcp71.dll: the wrapper's windows/system32, copied
# beside the game by install.sh) and the game's Ogg Vorbis and EAX DLLs. Everything generated stays local (work/).
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
GAME="$(cd "${1:?usage: build_gtasa.sh <GTA San Andreas folder> [outdir]}" && pwd)"
sha() { if command -v shasum >/dev/null; then shasum -a 256 "$1"; else sha256sum "$1"; fi | cut -d' ' -f1; }
EXE_NAME=gta_sa.exe; SHA=$(cat "$HERE/tools/gtasa.sha256" 2>/dev/null || true)
[ -f "$GAME/$EXE_NAME" ] || { echo "no gta_sa.exe in $GAME"; exit 1; }
[ -z "$SHA" ] || [ "$(sha "$GAME/$EXE_NAME")" = "$SHA" ] || { echo "not the supported Steam gta_sa.exe (3.0, Mac Steam 12250)"; exit 1; }
# the C runtime DLLs: beside the game (install.sh puts them there) or in the wrapper's system32
for d in msvcr71.dll msvcp71.dll; do
  [ -f "$GAME/$d" ] || { W="$GAME/../../../windows/system32/$d"; [ -f "$W" ] || { echo "no $d beside the game or in the wrapper"; exit 1; }; }
done
OUT="${2:-$HERE/work/gtasa}"
PY="$HERE/.venv/bin/python"
[ -x "$PY" ] || { python3 -m venv "$HERE/.venv" && "$HERE/.venv/bin/pip" install -q capstone==5.0.7 unicorn==2.1.4; }
mkdir -p "$OUT"; OUT="$(cd "$OUT" && pwd)"
python3 "$HERE/tools/compiler_compat.py" "${CC:-clang}" "$OUT/compiler-flags.txt"
"$PY" "$HERE/tools/steamstub21.py" "$GAME/$EXE_NAME" "$OUT/$EXE_NAME"; EXE="$OUT/$EXE_NAME"
if [[ -n "${GTA_RELINK:-}" && -d "$OUT/obj" ]]; then              # runtime changes only: keep the lifted objects
  echo "== runtime and link (GTA_RELINK)"; exec "$HERE/tools/build_runtime.sh" "$OUT"
fi
echo "== analysis"
"$PY" "$HERE/tools/analyze.py" "$EXE" "$OUT/analysis.json" > "$OUT/analysis.log"
sed -n '/^functions /p; /^decode failures:/p; /^jump tables /p; /^SEH:/p' "$OUT/analysis.log"
echo "== lifting"
"$PY" "$HERE/tools/lift.py" "$EXE" "$OUT/analysis.json" "$OUT" --game gtasa | grep -Ev '^unsupported: \{\}$'
# the DLLs, each at its own address: the Vorbis and EAX DLLs all want 0x10000000, and msvcp71 (0x7c3c0000) would sit
# inside msvcr71 (0x7c360000-0x7c3cb000), so those are lifted from copies relocated (tools/rebase_pe.py); loader.c
# applies the same relocations to the originals at run time
NIMP=$("$PY" -c 'import json,sys; print(len(json.load(open(sys.argv[1]))["imports"]))' "$OUT/image.json")
IMGS=("=")
for spec in msvcr71.dll msvcp71.dll@0x14000000 vorbisfile.dll vorbis.dll@0x11000000 ogg.dll@0x12000000 eax.dll@0x13000000; do
  rel=${spec%@*}; tag=$(echo "$rel" | tr . _ | tr A-Z a-z); src="$GAME/$rel"
  [ -f "$src" ] || src="$GAME/../../../windows/system32/$rel"
  [ -f "$src" ] || { echo "missing $rel"; exit 1; }
  if [[ $spec == *@* ]]; then "$PY" "$HERE/tools/rebase_pe.py" "$src" "$OUT/$rel" "${spec#*@}"; src="$OUT/$rel"; fi
  "$PY" "$HERE/tools/analyze.py" "$src" "$OUT/$tag.json" | head -1
  "$PY" "$HERE/tools/lift.py" "$src" "$OUT/$tag.json" "$OUT" --tag "$tag" --imp-base "$NIMP" | grep -E 'lifted|internal|unsupported'
  NIMP=$((NIMP + $("$PY" -c 'import json,sys; print(len(json.load(open(sys.argv[1]))["imports"]))' "$OUT/${tag}_image.json")))
  IMGS+=("$tag=$rel")
done
"$PY" "$HERE/tools/link_tables.py" "$OUT" "${IMGS[@]}"
echo "== compiling"
rm -rf "$OUT/obj"; mkdir -p "$OUT/obj"
JOBS="${GTA_BUILD_JOBS:-$(getconf _NPROCESSORS_ONLN)}"
printf '%s\0' "$OUT"/lift_*.c "$OUT"/*_lift_*.c "$OUT/fntab.c" | xargs -0 -n 1 -P "$JOBS" "$HERE/tools/cc1.sh" "$OUT"
echo "== $(ls "$OUT/obj" | wc -l | tr -d ' ') object files; runtime and link"
"$HERE/tools/build_runtime.sh" "$OUT"
