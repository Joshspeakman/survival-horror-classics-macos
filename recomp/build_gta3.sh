#!/bin/bash
# build_gta3.sh <GTAIII folder> [outdir]
# Recompile the user's own Steam copy of Grand Theft Auto III (the Mac release, app 12230: the Windows gta3.exe inside
# TransGaming's Cider wrapper, Grand Theft Auto 3.app/Contents/Resources/transgaming/c_drive/Program Files/Rockstar
# Games/GTAIII) into C and link it with the native runtime (tools/build_runtime.sh). The Steam wrapper (SteamStub 2.1)
# is undone on a copy in the build folder (tools/steamstub21.py); everything generated stays local (work/ is ignored).
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
GAME="$(cd "${1:?usage: build_gta3.sh <GTAIII folder> [outdir]}" && pwd)"
sha() { if command -v shasum >/dev/null; then shasum -a 256 "$1"; else sha256sum "$1"; fi | cut -d' ' -f1; }
EXE_NAME=gta3.exe; SHA=7fd2749072b1d1b04737b44d7d8173c2c00cd419bb8f1983c0067578e6229267   # Steam 12230 (Mac, Cider 7713), gta3.exe 1.1, 2010
[ -f "$GAME/$EXE_NAME" ] || { echo "no gta3.exe in $GAME"; exit 1; }
[ "$(sha "$GAME/$EXE_NAME")" = "$SHA" ] || { echo "not the supported Steam gta3.exe"; exit 1; }
OUT="${2:-$HERE/work/gta3}"
PY="$HERE/.venv/bin/python"
[ -x "$PY" ] || { python3 -m venv "$HERE/.venv" && "$HERE/.venv/bin/pip" install -q capstone==5.0.7 unicorn==2.1.4; }
mkdir -p "$OUT"; OUT="$(cd "$OUT" && pwd)"
python3 "$HERE/tools/compiler_compat.py" "${CC:-clang}" "$OUT/compiler-flags.txt"
"$PY" "$HERE/tools/steamstub21.py" "$GAME/$EXE_NAME" "$OUT/$EXE_NAME"; EXE="$OUT/$EXE_NAME"
if [[ -n "${GTA3_RELINK:-}" && -d "$OUT/obj" ]]; then            # runtime changes only: keep the lifted objects
  echo "== runtime and link (GTA3_RELINK)"; exec "$HERE/tools/build_runtime.sh" "$OUT"
fi
echo "== analysis"
"$PY" "$HERE/tools/analyze.py" "$EXE" "$OUT/analysis.json" > "$OUT/analysis.log"
sed -n '/^functions /p; /^decode failures:/p; /^jump tables /p; /^SEH:/p' "$OUT/analysis.log"
echo "== lifting"
"$PY" "$HERE/tools/lift.py" "$EXE" "$OUT/analysis.json" "$OUT" --game gta3 | grep -Ev '^unsupported: \{\}$'
"$PY" "$HERE/tools/link_tables.py" "$OUT" "="
echo "== compiling"
rm -rf "$OUT/obj"; mkdir -p "$OUT/obj"
JOBS="${GTA3_BUILD_JOBS:-$(getconf _NPROCESSORS_ONLN)}"
printf '%s\0' "$OUT"/lift_*.c "$OUT/fntab.c" | xargs -0 -n 1 -P "$JOBS" "$HERE/tools/cc1.sh" "$OUT"
echo "== $(ls "$OUT/obj" | wc -l | tr -d ' ') object files; runtime and link"
"$HERE/tools/build_runtime.sh" "$OUT"
