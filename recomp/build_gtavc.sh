#!/bin/bash
# build_gtavc.sh <Vice City folder> [outdir]
# Recompile the user's own Steam copy of Grand Theft Auto: Vice City (the Windows release, app 12110, depot 12111:
# gta-vc.exe 1.1 for Steam; the Mac release's copy carries SecuROM with online activation and is not used) into C and
# link it with the native runtime (tools/build_runtime.sh). The Steam wrapper (SteamStub 2.1) is undone on a copy in
# the build folder (tools/steamstub21.py); everything generated stays local (work/ is ignored).
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
GAME="$(cd "${1:?usage: build_gtavc.sh <Vice City folder> [outdir]}" && pwd)"
sha() { if command -v shasum >/dev/null; then shasum -a 256 "$1"; else sha256sum "$1"; fi | cut -d' ' -f1; }
EXE_NAME=gta-vc.exe; SHA=8c2318d61bffaaff5e638449543dbf99ea932dca85f297c7190d2efd525b848f   # Steam 12110 (Windows, depot 12111)
[ -f "$GAME/$EXE_NAME" ] || { echo "no gta-vc.exe in $GAME"; exit 1; }
[ "$(sha "$GAME/$EXE_NAME")" = "$SHA" ] || { echo "not the supported Steam gta-vc.exe (Windows depot 12111)"; exit 1; }
OUT="${2:-$HERE/work/gtavc}"
PY="$HERE/.venv/bin/python"
[ -x "$PY" ] || { python3 -m venv "$HERE/.venv" && "$HERE/.venv/bin/pip" install -q capstone==5.0.7 unicorn==2.1.4; }
mkdir -p "$OUT"; OUT="$(cd "$OUT" && pwd)"
python3 "$HERE/tools/compiler_compat.py" "${CC:-clang}" "$OUT/compiler-flags.txt"
"$PY" "$HERE/tools/steamstub21.py" "$GAME/$EXE_NAME" "$OUT/$EXE_NAME"; EXE="$OUT/$EXE_NAME"
if [[ -n "${GTA_RELINK:-}" && -d "$OUT/obj" ]]; then            # runtime changes only: keep the lifted objects
  echo "== runtime and link (GTA_RELINK)"; exec "$HERE/tools/build_runtime.sh" "$OUT"
fi
echo "== analysis"
"$PY" "$HERE/tools/analyze.py" "$EXE" "$OUT/analysis.json" > "$OUT/analysis.log"
sed -n '/^functions /p; /^decode failures:/p; /^jump tables /p; /^SEH:/p' "$OUT/analysis.log"
echo "== lifting"
"$PY" "$HERE/tools/lift.py" "$EXE" "$OUT/analysis.json" "$OUT" --game gtavc | grep -Ev '^unsupported: \{\}$'
"$PY" "$HERE/tools/link_tables.py" "$OUT" "="
echo "== compiling"
rm -rf "$OUT/obj"; mkdir -p "$OUT/obj"
JOBS="${GTA_BUILD_JOBS:-$(getconf _NPROCESSORS_ONLN)}"
printf '%s\n' "$OUT"/lift_*.c "$OUT/fntab.c" | xargs -n 1 -P "$JOBS" "$HERE/tools/cc1.sh" "$OUT"
echo "== $(ls "$OUT/obj" | wc -l | tr -d ' ') object files; runtime and link"
"$HERE/tools/build_runtime.sh" "$OUT"
