#!/bin/bash
# install.sh [--games gta3,vc,sa] [--steam-dir DIR] [--gta3-dir D] [--vc-dir D] [--sa-dir D] [--app-dir DIR]
#            [--branch main|nightly] [--no-downloads] [--in-place]
#
# Builds Grand Theft Auto III, Vice City and San Andreas natively for Apple Silicon from your own Steam copies and
# installs them as one app, "GTA Classics.app" ("GTA Classics Nightly.app" with --branch nightly), in ~/Applications.
# Its launch window has a card per game; Play starts the chosen one in the same process (Game Mode stays on).
#
# The games are the Mac Steam releases (TransGaming's wrapper around the Windows game: GTA III 12230, San Andreas
# 12250), found under --steam-dir (default ~/Library/Application Support/Steam/steamapps/common), or already imported
# by an earlier install, or given with --gta3-dir / --vc-dir / --sa-dir (the folder holding gta3.exe, gta-vc.exe or
# gta_sa.exe). Vice City is the Windows release (app 12110): the Mac release's gta-vc.exe carries SecuROM with online
# activation, which this project does not touch. Download it from Steam's console (steam://open/console):
# "download_depot 12110 12111"; the depot folder Steam reports is found by default. --games picks which to build (default: every game found); a game already in
# the app and not named this time is kept as it is. A game whose port isn't in this checkout yet is skipped.
# Each game is copied into its data folder (~/Games/GTA3Native, GTAVCNative, GTASANative: game-files/<folder>, an
# APFS clone, no extra space), so the Steam copy can go afterwards; --in-place reads the found folders instead.
# Saves and settings go to the data folder (drive_c/Users/player/Documents/...). The launch window's art comes from
# the games' Steam store pages (recomp/tools/gta_art.py, into ~/Games/GTAClassics/art; --no-downloads writes the
# titles instead); the app's icon is GTA III's Mac release's own. The app's updater (Updates in the launch window)
# rebuilds every installed game from the branch it follows. Needs clang and python3 (the Xcode command line tools);
# the recompiler installs capstone with pip into recomp/.venv.
#   development: --skip-build assembles the app from the last builds in recomp/work/<gta3|gtavc|gtasa> instead.
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
STEAM="$HOME/Library/Application Support/Steam/steamapps/common"
WANT=""; DIR_gta3=""; DIR_vc=""; DIR_sa=""; APPS=""; NODL=""; U_BRANCH=main; IN_PLACE=""; SKIP_BUILD=""
while [ $# -gt 0 ]; do
  case "$1" in
    --games) WANT="$(echo "$2" | tr ',' ' ')"; shift 2;;
    --steam-dir) STEAM="$2"; shift 2;;
    --gta3-dir) DIR_gta3="$2"; shift 2;; --vc-dir) DIR_vc="$2"; shift 2;; --sa-dir) DIR_sa="$2"; shift 2;;
    --app-dir) APPS="$2"; shift 2;;
    --branch) U_BRANCH="$2"; shift 2;;
    --no-downloads) NODL=--no-downloads; shift;;
    --in-place) IN_PLACE=1; shift;;
    --skip-build) SKIP_BUILD=1; shift;;
    --game-dir) DIR_gta3="$2"; WANT="${WANT:-gta3}"; shift 2;;   # the single-game GTA III installer's (its apps' updaters)
    --data-dir) echo "   (--data-dir is no longer used: the data folders are ~/Games/GTA3Native, GTAVCNative, GTASANative)"; shift 2;;
    -h|--help) sed -n '2,22p' "$0" | cut -c3-; exit 0;;
    *) echo "unknown option $1 (see install.sh --help)"; exit 1;;
  esac
done
[[ "$U_BRANCH" == main || "$U_BRANCH" == nightly ]] || { echo "--branch must be main or nightly"; exit 1; }
[[ $(uname -s) == Darwin ]] || { echo "the GTA ports are macOS only for now"; exit 1; }
for g in $WANT; do case "$g" in gta3|vc|sa) ;; *) echo "--games: unknown game $g (gta3, vc, sa)"; exit 1;; esac; done

# each game: its name, the binary the build makes, its executable, data folder, folder name, Steam bundle, build id
game_info() {
  case "$1" in
    gta3) G_NAME="Grand Theft Auto III"; G_BIN=GTA3; G_EXE=gta3.exe; G_DATA="$HOME/Games/GTA3Native"; G_FOLDER=GTAIII
          G_STEAM="grand theft auto 3/Grand Theft Auto 3.app"; G_BUILD=gta3;;
    vc)   G_NAME="Grand Theft Auto: Vice City"; G_BIN=GTAVC; G_EXE=gta-vc.exe; G_DATA="$HOME/Games/GTAVCNative"; G_FOLDER="Grand Theft Auto Vice City"
          G_STEAM="grand theft auto - vice city/Grand Theft Auto - Vice City.app"; G_BUILD=gtavc;;
    sa)   G_NAME="Grand Theft Auto: San Andreas"; G_BIN=GTASA; G_EXE=gta_sa.exe; G_DATA="$HOME/Games/GTASANative"; G_FOLDER="GTA San Andreas"
          G_STEAM="grand theft auto - san andreas/Grand Theft Auto - San Andreas.app"; G_BUILD=gtasa;;
  esac
  G_STEAM_DIR="$STEAM/$G_STEAM/Contents/Resources/transgaming/c_drive/Program Files/Rockstar Games/$G_FOLDER"
  G_IMPORTED="$G_DATA/game-files/$G_FOLDER"
  G_DEPOT=""   # Vice City: the Windows depot Steam's console downloads (the Mac copy's exe is SecuROM'd: never used)
  [ "$1" = vc ] && G_DEPOT="$HOME/Library/Application Support/Steam/Steam.AppBundle/Steam/Contents/MacOS/steamapps/content/app_12110/depot_12111"
}
given_dir() { case "$1" in gta3) echo "$DIR_gta3";; vc) echo "$DIR_vc";; sa) echo "$DIR_sa";; esac; }
find_game() {   # find_game <id>: the game folder (an explicit one, else the imported copy, else Steam's), or nothing
  game_info "$1"; local d; d="$(given_dir "$1")"
  if [ -n "$d" ]; then if [ -f "$d/$G_EXE" ]; then (cd "$d" && pwd); fi
  elif [ -f "$G_IMPORTED/$G_EXE" ]; then echo "$G_IMPORTED"
  elif [ -n "$G_DEPOT" ] && [ -f "$G_DEPOT/$G_EXE" ]; then echo "$G_DEPOT"
  elif [ -z "$G_DEPOT" ] && [ -f "$G_STEAM_DIR/$G_EXE" ]; then echo "$G_STEAM_DIR"; fi
  return 0
}
if [ -z "$WANT" ]; then
  for g in gta3 vc sa; do [ -n "$(find_game $g)" ] && WANT="$WANT $g"; done
  [ -n "$WANT" ] || { echo "no GTA III, Vice City or San Andreas found under $STEAM (use --steam-dir or --gta3-dir/--vc-dir/--sa-dir)"; exit 1; }
  echo "== found:$WANT"
fi

NAME="GTA Classics"; ID=com.joshspeakman.gta-classics
[[ "$U_BRANCH" == nightly ]] && { NAME="$NAME Nightly"; ID=$ID.nightly; }
APPS="${APPS:-$HOME/Applications}"; A="$APPS/$NAME.app"; CLASSICS="$HOME/Games/GTAClassics"
copy() { cp -cRL "$1" "$2" 2>/dev/null || cp -RL "$1" "$2"; }   # an APFS clone where it can be

DONE=""   # the games built (or, with --skip-build, taken from recomp/work) this time; their folders in GAME_<id>
for g in $WANT; do
  game_info $g; GAME="$(find_game $g)"
  [ -n "$GAME" ] || { W="$(given_dir $g)"; echo "== $G_NAME: no $G_EXE in ${W:-$G_IMPORTED or $G_STEAM_DIR}: skipped"; continue; }
  [ -x "$HERE/recomp/build_$G_BUILD.sh" ] || { echo "== $G_NAME: not ported in this checkout yet (no recomp/build_$G_BUILD.sh): skipped"; continue; }
  OUT="$HERE/recomp/work/$G_BUILD"
  if [ -n "$SKIP_BUILD" ] && { [ ! -x "$OUT/app/$G_BIN" ] || [ ! -f "$OUT/$G_EXE" ]; }; then
    echo "== $G_NAME: --skip-build but no build in $OUT (app/$G_BIN, $G_EXE): skipped"; continue
  fi
  mkdir -p "$G_DATA"
  # the Mac release's wrapper: the app bundle around the game folder (its icon) and its c_drive (GTA III's radio and
  # cutscene audio in c_drive/Audio, San Andreas's C runtime DLLs in c_drive/windows/system32); Steam's copy is used
  # when the game comes from the data folder
  SRC="$GAME"; [ "$GAME" = "$G_IMPORTED" ] && [ -d "$G_STEAM_DIR" ] && SRC="$G_STEAM_DIR"
  WRAP_RES="$(cd "$SRC/../../../../.." 2>/dev/null && pwd || true)"; CDRIVE="$(cd "$SRC/../../.." 2>/dev/null && pwd || true)"
  ICNS="$(ls "$WRAP_RES"/*.icns 2>/dev/null | head -1 || true)"
  [ -n "$ICNS" ] && { mkdir -p "$G_DATA/art"; cp "$ICNS" "$G_DATA/art/icon.icns"; }
  if [ -z "$IN_PLACE" ]; then case "$GAME/" in "$G_DATA"/*) ;; *)
    GF="$G_IMPORTED"; echo "== copying $G_NAME from $GAME into $GF"
    mkdir -p "$G_DATA/game-files"; rm -rf "$GF.part"; copy "$GAME" "$GF.part"
    rm -rf "$GF"; mv "$GF.part" "$GF"
    echo "== $G_NAME is read from $GF: $GAME is no longer needed (delete it to free the space)"; GAME="$GF";;
  esac; fi
  if [ $g = gta3 ]; then   # the radio stations and cutscene audio: the game asks for them on its "CD" (C:\Audio, the game's audio/)
    if [ -n "$CDRIVE" ] && [ -f "$CDRIVE/Audio/HEAD.WAV" ] && [ ! -f "$GAME/audio/HEAD.WAV" ]; then
      echo "== copying the radio and cutscene audio from $CDRIVE/Audio"
      mkdir -p "$GAME/audio"; for f in "$CDRIVE/Audio"/*; do [ -e "$GAME/audio/$(basename "$f")" ] || copy "$f" "$GAME/audio/"; done
    fi
    [ -f "$GAME/audio/HEAD.WAV" ] || echo "   (no radio audio found: the game will ask for its second disc; copy c_drive/Audio into $GAME/audio)"
  fi
  if [ $g = sa ]; then     # the Visual C++ 7.1 runtime the game loads (recompiled with it) from beside gta_sa.exe
    for d in msvcr71.dll msvcp71.dll; do
      [ -f "$GAME/$d" ] || { [ -f "$CDRIVE/windows/system32/$d" ] && copy "$CDRIVE/windows/system32/$d" "$GAME/$d"; } || true
      [ -f "$GAME/$d" ] || echo "   (no $d beside the game or in the wrapper's windows/system32: the game won't start)"
    done
  fi
  if [ -z "$SKIP_BUILD" ]; then
    echo "== recompiling $G_NAME from $GAME (a few minutes)"
    "$HERE/recomp/build_$G_BUILD.sh" "$GAME" "$OUT"
  fi
  [ -x "$OUT/app/$G_BIN" ] && [ -f "$OUT/$G_EXE" ] || { echo "== $G_NAME: the build made no $OUT/app/$G_BIN"; exit 1; }
  eval "GAME_$g=\$GAME"; DONE="$DONE $g"
done

# ---------------------------------------------------------------- the app: staged beside, then swapped in
U_BUILT="$(git -C "$HERE" rev-parse HEAD 2>/dev/null || true)"
OLD_GAMES="$A/Contents/Resources/games.plist"
pl() { plutil -extract "$2" raw -o - "$1" 2>/dev/null || true; }   # pl <plist> <key.path>: a string, or nothing
KEEP=""   # games in the installed app that weren't rebuilt now: carried over as they are
for g in gta3 vc sa; do
  case " $DONE " in *" $g "*) continue;; esac
  game_info $g; [ -f "$OLD_GAMES" ] && [ -n "$(pl "$OLD_GAMES" $g.bin)" ] && [ -x "$A/Contents/MacOS/$G_BIN" ] && KEEP="$KEEP $g"
done
[ -n "$DONE$KEEP" ] || { echo "nothing to install: no game was built (see above)"; exit 1; }
echo "== installing $A (built now:${DONE:- none}; kept:${KEEP:- none})"
STAGE="$HERE/build/stage/$NAME.app"; rm -rf "$STAGE"; mkdir -p "$STAGE/Contents/MacOS" "$STAGE/Contents/Resources"
clang -O2 -fobjc-arc -Wall -framework Cocoa -o "$STAGE/Contents/MacOS/GTAClassics" "$HERE/recomp/host/classics_launcher.m"
G="$STAGE/Contents/Resources/games.plist"; plutil -create xml1 "$G"
for g in gta3 vc sa; do
  game_info $g
  case " $DONE " in
    *" $g "*) OUT="$HERE/recomp/work/$G_BUILD"; eval "GAME=\$GAME_$g"; BUILT="$U_BUILT"
              cp "$OUT/app/$G_BIN" "$STAGE/Contents/MacOS/"; cp "$OUT/$G_EXE" "$STAGE/Contents/Resources/"   # your copy, this Mac only
              for f in shaders.metal dsr_snapshot.bin; do cp "$OUT/app/$f" "$STAGE/Contents/MacOS/"; done;;
    *) case " $KEEP " in *" $g "*) ;; *) continue;; esac
       GAME="$(pl "$OLD_GAMES" $g.game)"; G_DATA="$(pl "$OLD_GAMES" $g.data)"; BUILT="$(pl "$OLD_GAMES" $g.built)"
       cp "$A/Contents/MacOS/$G_BIN" "$STAGE/Contents/MacOS/"; cp "$A/Contents/Resources/$G_EXE" "$STAGE/Contents/Resources/"
       for f in shaders.metal dsr_snapshot.bin; do [ -f "$STAGE/Contents/MacOS/$f" ] || cp "$A/Contents/MacOS/$f" "$STAGE/Contents/MacOS/"; done;;
  esac
  plutil -insert $g -dictionary "$G"
  plutil -insert $g.name -string "$G_NAME" "$G"; plutil -insert $g.bin -string "$G_BIN" "$G"; plutil -insert $g.exe -string "$G_EXE" "$G"
  plutil -insert $g.game -string "$GAME" "$G"; plutil -insert $g.data -string "$G_DATA" "$G"; plutil -insert $g.built -string "$BUILT" "$G"
  INSTALLED="${INSTALLED:-} $g"
done
# the launch window's art (tools/gta_art.py) for all three games, installed or not (a game's card shows it either way)
for g in gta3 vc sa; do
  if python3 -I "$HERE/recomp/tools/gta_art.py" $g "$CLASSICS/art/$g" $NODL >/dev/null 2>&1 && ls "$CLASSICS/art/$g/"*.jpg >/dev/null 2>&1; then
    mkdir -p "$STAGE/Contents/Resources/art/$g"; cp "$CLASSICS/art/$g/"*.jpg "$CLASSICS/art/$g/"*.png "$STAGE/Contents/Resources/art/$g/" 2>/dev/null || true
  else echo "   (no art for $g's card: the launch window writes its title)"; fi
done
# the icon: GTA III's Mac release's (kept in its data folder's art/), else Vice City's or San Andreas's, else the
# Windows executable's
ICON=""; for g in gta3 vc sa; do game_info $g; [ -z "$ICON" ] && [ -f "$G_DATA/art/icon.icns" ] && ICON="$G_DATA/art/icon.icns"; done
if [ -n "$ICON" ]; then cp "$ICON" "$STAGE/Contents/Resources/icon.icns"
else
  I="$(mktemp -d)"; set -- $INSTALLED; game_info $1
  if python3 "$HERE/recomp/tools/extract_icon.py" "$STAGE/Contents/Resources/$G_EXE" "$I/g.ico" 2>/dev/null && sips -s format png "$I/g.ico" --out "$I/g.png" >/dev/null 2>&1; then
    sips -z 512 512 "$I/g.png" --out "$I/g512.png" >/dev/null && sips -s format icns "$I/g512.png" --out "$STAGE/Contents/Resources/icon.icns" >/dev/null 2>&1 || true
  fi
  rm -rf "$I"
fi
# updates from the launch window: the commit this app was built from and the checkout it was installed from; the
# updater rebuilds every game in the app (each from the folder it was installed from) from the branch it follows
U_WORK=stable; [[ "$U_BRANCH" == nightly ]] && U_WORK=nightly
U_REPO="$(git -C "$HERE" rev-parse --path-format=absolute --git-common-dir 2>/dev/null || true)"; U_REPO="${U_REPO%/.git}"
if [ -n "$U_REPO" ] && [ -n "$U_BUILT" ] && git -C "$U_REPO" remote get-url origin >/dev/null 2>&1; then
  U_ARGS="--games $(echo $INSTALLED | tr ' ' ',')"; U_INPLACE=""
  for g in $INSTALLED; do
    game_info $g; GAME="$(pl "$G" $g.game)"; U_ARGS="$U_ARGS --$g-dir $(printf '%q' "$GAME")"
    case "$GAME/" in "$(pl "$G" $g.data)"/*) ;; *) U_INPLACE=" --in-place";; esac
  done
  cat > "$STAGE/Contents/MacOS/update" <<UPDATE
#!/bin/bash
# $NAME updater (generated by install.sh): "update --check" prints the newest commit of $U_BRANCH on GitHub;
# "update" fetches it into a worktree of the checkout this app was installed from and rebuilds the app from it
set -euo pipefail
REPO=$(printf '%q' "$U_REPO"); BRANCH=$U_BRANCH; W="\$REPO/build/$U_WORK"
export GIT_TERMINAL_PROMPT=0 PATH="/opt/homebrew/bin:/usr/local/bin:\$PATH"   # never wait for a password prompt
# a private repository: git signs in through gh with the repository owner's account (gh's active one may differ)
OWNER="\$(git -C "\$REPO" remote get-url origin | sed -E 's#.*github.com[:/]([^/]+)/.*#\1#')"
[ -n "\${GH_TOKEN:-}" ] || GH_TOKEN="\$(gh auth token --user "\$OWNER" 2>/dev/null || true)"
[ -z "\$GH_TOKEN" ] || export GH_TOKEN
if [ "\${1:-}" = --check ]; then                          # GitHub's newest, unless this app already has it: a build
  R="\$(git -C "\$REPO" ls-remote origin "refs/heads/\$BRANCH" | cut -f1)"   # from a newer local commit is never offered an older one
  if [ -n "\$R" ] && git -C "\$REPO" merge-base --is-ancestor "\$R" $U_BUILT 2>/dev/null; then echo $U_BUILT; else echo "\$R"; fi
  exit 0
fi
echo "== fetching \$BRANCH from GitHub"
git -C "\$REPO" fetch -q origin "\$BRANCH"
git -C "\$REPO" worktree prune
if [ -e "\$W/.git" ]; then git -C "\$W" checkout -q -f --detach "origin/\$BRANCH"
else mkdir -p "\$REPO/build"; git -C "\$REPO" worktree add -q -f --detach "\$W" "origin/\$BRANCH"; fi
echo "== \$BRANCH: \$(git -C "\$W" log -1 --format='%h %s')"
[ -e "\$W/recomp/.venv" ] || [ ! -e "\$REPO/recomp/.venv" ] || ln -s "\$(readlink "\$REPO/recomp/.venv" 2>/dev/null || echo "\$REPO/recomp/.venv")" "\$W/recomp/.venv"   # share the tools
exec "\$W/install.sh" $U_ARGS --app-dir $(printf '%q' "$APPS") --branch $U_BRANCH$U_INPLACE $NODL
UPDATE
  chmod +x "$STAGE/Contents/MacOS/update"
fi
cat > "$STAGE/Contents/Info.plist" <<PLIST
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0"><dict>
  <key>CFBundleName</key><string>$NAME</string>
  <key>CFBundleDisplayName</key><string>$NAME</string>
  <key>CFBundleIdentifier</key><string>$ID</string>
  <key>CFBundleExecutable</key><string>GTAClassics</string>
  <key>CFBundleIconFile</key><string>icon</string>
  <key>CFBundlePackageType</key><string>APPL</string>
  <key>CFBundleShortVersionString</key><string>${U_BUILT:0:7}</string>
  <key>CFBundleVersion</key><string>${U_BUILT:0:7}</string>
  <key>LSApplicationCategoryType</key><string>public.app-category.action-games</string>
  <key>GCSupportsGameMode</key><true/>
  <key>LSMinimumSystemVersion</key><string>13.0</string>
  <key>NSHighResolutionCapable</key><true/>
  <key>GTACBranch</key><string>$U_BRANCH</string>
  <key>GTACBuilt</key><string>$U_BUILT</string>
</dict></plist>
PLIST
codesign --force --deep --sign - "$STAGE" >/dev/null 2>&1 || true
mkdir -p "$APPS"; rm -rf "$A"; mv "$STAGE" "$A"
touch "$A"                                                    # Finder and the Dock pick up the new icon
L=""; for g in $INSTALLED; do game_info $g; L="${L:+$L, }$G_NAME"; done
echo "== done: $A ($L)"
