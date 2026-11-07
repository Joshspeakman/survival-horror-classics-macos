#!/bin/bash
# build_app.sh <outdir> : link the recompiled game, the runtime and the macOS host into an app executable (run
# build_gta3.sh first). Produces <outdir>/app/GTA3 (gta3.exe; DeltaForce/DeltaForce2 for Df.exe/Df2.exe) with shaders.metal and
# dsr_snapshot.bin beside it. On Linux, tools/build_linux.sh makes the SDL/OpenGL build instead.
set -euo pipefail
HERE="$(cd "$(dirname "$0")/.." && pwd)"; ROOT="$(cd "$HERE/.." && pwd)"; OUT="$1"; APP="$OUT/app"
if [[ $(uname -s) == Linux ]]; then exec "$HERE/tools/build_linux.sh" "$OUT"; fi
NAME=DungeonSiegeNative; EDITION_FLAGS=()
[[ -f "$OUT/Df.exe" ]] && { NAME=DeltaForce; EDITION_FLAGS=(-DDF_EDITION -DDF1_EDITION); }
[[ -f "$OUT/Df2.exe" ]] && { NAME=DeltaForce2; EDITION_FLAGS=(-DDF_EDITION -DDF2_EDITION); }
[[ -f "$OUT/gta3.exe" ]] && { NAME=GTA3; EDITION_FLAGS=(-DGTA3_EDITION); }
[[ -f "$OUT/gta-vc.exe" ]] && { NAME=GTAVC; EDITION_FLAGS=(-DGTAVC_EDITION); }
[[ -f "$OUT/gta_sa.exe" ]] && { NAME=GTASA; EDITION_FLAGS=(-DGTASA_EDITION); }
[[ -f "$OUT/ResidentEvil.exe" ]] && { NAME=ResidentEvil; EDITION_FLAGS=(-DRE1_EDITION); }
[[ -f "$OUT/LeonU.exe" ]] && { NAME=ResidentEvil2Leon; EDITION_FLAGS=(-DRE2_EDITION); }
[[ -f "$OUT/ClaireU.exe" ]] && { NAME=ResidentEvil2Claire; EDITION_FLAGS=(-DRE2_EDITION -DRE2_CLAIRE); }
[[ -f "$OUT/ResidentEvil3.exe" ]] && { NAME=ResidentEvil3; EDITION_FLAGS=(-DRE3_EDITION); }
[[ -f "$OUT/RE3_MERCE.exe" ]] && { NAME=ResidentEvil3Mercenaries; EDITION_FLAGS=(-DRE3_EDITION -DRE3_MERCE); }
[[ -f "$OUT/DINO.exe" ]] && { NAME=DinoCrisis; EDITION_FLAGS=(-DDC_EDITION); }
SAN=(); [[ -n "${DF_SANITIZE:-}" ]] && SAN=(-fsanitize="$DF_SANITIZE" -fno-omit-frame-pointer)   # development (build_runtime.sh)
mkdir -p "$APP" "$OUT/obj/host"
BUILD="$(git -C "$HERE" describe --always --dirty 2>/dev/null || echo unknown)"
clang -c -O2 -fobjc-arc -Wall -Wno-unused-function ${SAN[@]+"${SAN[@]}"} "${EDITION_FLAGS[@]}" -I "$HERE/runtime" -DDS_BUILD="\"$BUILD\"" -o "$OUT/obj/host/main.o" "$HERE/host/main.m"
clang -c -O2 -fobjc-arc -Wall -Wno-unused-function -o "$OUT/obj/host/renderer.o" "$ROOT/src/renderer/renderer.m"
LAUNCHER=launcher.m; [[ -f "$OUT/gta3.exe" || -f "$OUT/gta-vc.exe" || -f "$OUT/gta_sa.exe" ]] && LAUNCHER=gta3_launcher.m   # the GTA games: their launch window
clang -c -O2 -fobjc-arc -Wall -Wno-unused-function "${EDITION_FLAGS[@]}" -o "$OUT/obj/host/launcher.o" "$HERE/host/$LAUNCHER"
MOJO=(); [[ -d "$OUT/obj/rt/mojo" ]] && MOJO=("$OUT"/obj/rt/mojo/*.o)
clang ${SAN[@]+"${SAN[@]}"} -o "$APP/$NAME" "$OUT"/obj/*.o "$OUT"/obj/rt/*.o "${MOJO[@]}" "$OUT"/obj/host/*.o \
  -framework AppKit -framework GameController -framework Metal -framework QuartzCore -framework CoreText -framework CoreGraphics -framework CoreFoundation -framework AudioToolbox -framework AVFoundation -framework CoreMedia -lz
cp "$ROOT/src/renderer/shaders.metal" "$ROOT/src/dsr/dsr_snapshot.bin" "$APP/"
echo "$APP/$NAME"
