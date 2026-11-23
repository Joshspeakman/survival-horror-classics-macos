#!/bin/bash
# build_re.sh <re1|re2|re2c|re3|re3m|dc|dc2> <game folder> [outdir]
# Recompile one program of the player's own GOG copy of Resident Evil (ResidentEvil.exe), Resident Evil 2 (LeonU.exe,
# ClaireU.exe), Resident Evil 3: Nemesis (ResidentEvil3.exe, RE3_MERCE.exe) or Dino Crisis (DINO.exe) into C and link
# it with the native runtime (tools/build_runtime.sh). The GOG executables carry no wrapper or copy protection: the
# build copies the program as it is. GOG's ddraw.dll, dinput.dll and dshow.dll are not recompiled (the runtime takes
# their place). Everything generated stays local (work/ is ignored). RE_RELINK=1: runtime changes only.
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
ID="${1:?usage: build_re.sh <re1|re2|re2c|re3|re3m|dc> <game folder> [outdir]}"
GAME="$(cd "${2:?usage: build_re.sh <id> <game folder> [outdir]}" && pwd)"
case "$ID" in
  re1)  EXE_NAME=ResidentEvil.exe;  SHA=205bd6fed6e080a3 ;;
  re2)  EXE_NAME=LeonU.exe;         SHA=5cef4bea4d864d76 ;;
  re2c) EXE_NAME=ClaireU.exe;       SHA=79e09de3ab921260 ;;
  re3)  EXE_NAME=ResidentEvil3.exe; SHA=f6ab367f757e6ee7 ;;
  re3m) EXE_NAME=RE3_MERCE.exe;     SHA=e409166c7d70c40a ;;
  dc)   EXE_NAME=DINO.exe;          SHA=e51128312935eb3e ;;
  dc2)  EXE_NAME=Dino2.exe;         SHA=45499e03a5cfb52f ;;
  *) echo "unknown game $ID"; exit 1 ;;
esac
sha() { if command -v shasum >/dev/null; then shasum -a 256 "$1"; else sha256sum "$1"; fi | cut -c1-16; }
[ -f "$GAME/$EXE_NAME" ] || { echo "no $EXE_NAME in $GAME"; exit 1; }
[ "$(sha "$GAME/$EXE_NAME")" = "$SHA" ] || { echo "not the supported GOG $EXE_NAME (SHA-256 starts $SHA)"; exit 1; }
OUT="${3:-$HERE/work/$ID}"
PY="$HERE/.venv/bin/python"
[ -x "$PY" ] || { python3 -m venv "$HERE/.venv" && "$HERE/.venv/bin/pip" install -q capstone==5.0.7 unicorn==2.1.4; }
mkdir -p "$OUT"; OUT="$(cd "$OUT" && pwd)"
python3 "$HERE/tools/compiler_compat.py" "${CC:-clang}" "$OUT/compiler-flags.txt"
cp "$GAME/$EXE_NAME" "$OUT/$EXE_NAME"; chmod u+w "$OUT/$EXE_NAME"; EXE="$OUT/$EXE_NAME"
if [[ -n "${RE_RELINK:-}" && -d "$OUT/obj" ]]; then            # runtime changes only: keep the lifted objects
  echo "== runtime and link (RE_RELINK)"; exec "$HERE/tools/build_runtime.sh" "$OUT"
fi
# Resident Evil: its task switcher's entry points (reached by jumps only) are functions of their own, so the runtime can
# take them over (runtime/win32/re1_tasks.c: a host thread per task instead of switching stacks)
[ "$ID" = re1 ] && echo '[4675420, 4675452, 4675484, 4675496]' > "$OUT/observed_targets.json"
echo "== analysis"
"$PY" "$HERE/tools/analyze.py" "$EXE" "$OUT/analysis.json" > "$OUT/analysis.log"
sed -n '/^functions /p; /^decode failures:/p; /^jump tables /p; /^SEH:/p' "$OUT/analysis.log"
echo "== lifting"
"$PY" "$HERE/tools/lift.py" "$EXE" "$OUT/analysis.json" "$OUT" --game "$ID" | grep -Ev '^unsupported: \{\}$'
"$PY" "$HERE/tools/link_tables.py" "$OUT" "="
echo "== compiling"
rm -rf "$OUT/obj"; mkdir -p "$OUT/obj"
JOBS="${RE_BUILD_JOBS:-$(getconf _NPROCESSORS_ONLN)}"
printf '%s\0' "$OUT"/lift_*.c "$OUT/fntab.c" | xargs -0 -n 1 -P "$JOBS" "$HERE/tools/cc1.sh" "$OUT"
echo "== $(ls "$OUT/obj" | wc -l | tr -d ' ') object files; runtime and link"
"$HERE/tools/build_runtime.sh" "$OUT"
