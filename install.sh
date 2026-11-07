#!/bin/bash
# install.sh [--games re1,re2,re3,dc] [--games-root DIR] [--installers DIR] [--app-dir DIR] [--branch main|nightly]
#            [--skip-build]
#
# Builds Resident Evil (1997), Resident Evil 2, Resident Evil 3: Nemesis and Dino Crisis natively for Apple Silicon from
# your own GOG copies and installs them as one app, "Survival Horror Classics.app" ("... Nightly.app" with --branch
# nightly), in ~/Applications. Its launch window has a card per game; Play starts the chosen one.
#
# The games stay where they are: <games-root>/<Game>Native/game-files/<title> (default games root: the Expansion Pak
# SD card, /Volumes/Expansion Pak/Games). A game not there yet is unpacked from its GOG installer (setup_*.exe and its
# .bin parts, in --installers, default /Volumes/Expansion Pak/GOG Installers) with innoextract (Homebrew). Resident
# Evil's and Resident Evil 2's movies (Cinepak, Indeo) get MPEG-1 copies beside them (recomp/tools/convert_movies.sh,
# ffmpeg from Homebrew while it runs). Saves and settings: ~/Games/SurvivalHorrorClassics/<version>. --games picks
# which to build (default: every game found); a game already in the app and not named this time is kept as it is.
# The app's updater (Updates in the launch window) rebuilds every installed game from the branch it follows.
# Needs clang and python3 (the Xcode command line tools); the recompiler installs capstone with pip into recomp/.venv.
#   development: --skip-build assembles the app from the last builds in recomp/work/<id> instead.
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
GROOT="/Volumes/Expansion Pak/Games"; INST="/Volumes/Expansion Pak/GOG Installers"
WANT=""; APPS=""; U_BRANCH=main; SKIP_BUILD=""
while [ $# -gt 0 ]; do
  case "$1" in
    --games) WANT="$(echo "$2" | tr ',' ' ')"; shift 2;;
    --games-root) GROOT="$2"; shift 2;;
    --installers) INST="$2"; shift 2;;
    --app-dir) APPS="$2"; shift 2;;
    --branch) U_BRANCH="$2"; shift 2;;
    --skip-build) SKIP_BUILD=1; shift;;
    -h|--help) sed -n '2,20p' "$0" | cut -c3-; exit 0;;
    *) echo "unknown option $1"; exit 1;;
  esac
done
[ -n "$APPS" ] || APPS="$HOME/Applications"
NAME="Survival Horror Classics"; ID=com.joshspeakman.survivalhorrorclassics
[[ "$U_BRANCH" == nightly ]] && { NAME="$NAME Nightly"; ID="$ID.nightly"; }
A="$APPS/$NAME.app"; HOMEDIR="$HOME/Games/SurvivalHorrorClassics"; mkdir -p "$HOMEDIR"
export PATH="/opt/homebrew/bin:/usr/local/bin:$PATH"
pl() { /usr/libexec/PlistBuddy -c "Print :$2" "$1" 2>/dev/null || true; }

# game: native folder, title folder, installer pattern, versions (id:binary:exe)
game_info() {
  case "$1" in
    re1) G_DIR=ResidentEvilNative;  G_TITLE="Resident Evil";   G_SETUP="setup_resident_evil_1.0*"; G_VERS="re1:ResidentEvil:ResidentEvil.exe";;
    re2) G_DIR=ResidentEvil2Native; G_TITLE="Resident Evil 2";  G_SETUP="setup_resident_evil_2_*";  G_VERS="re2:ResidentEvil2Leon:LeonU.exe re2c:ResidentEvil2Claire:ClaireU.exe";;
    re3) G_DIR=ResidentEvil3Native; G_TITLE="Resident Evil 3";  G_SETUP="setup_resident_evil_3_*";  G_VERS="re3:ResidentEvil3:ResidentEvil3.exe re3m:ResidentEvil3Mercenaries:RE3_MERCE.exe";;
    dc)  G_DIR=DinoCrisisNative;    G_TITLE="Dino Crisis";      G_SETUP="setup_dino_crisis_*";      G_VERS="dc:DinoCrisis:DINO.exe";;
  esac
  G_GAME="$GROOT/$G_DIR/game-files/$G_TITLE"
}
ALL="re1 re2 re3 dc"; [ -n "$WANT" ] || WANT="$ALL"
DONE=""
for g in $WANT; do
  game_info "$g"
  if [ ! -d "$G_GAME" ]; then
    setup="$(ls "$INST"/$G_SETUP.exe 2>/dev/null | head -1 || true)"
    if [ -n "$setup" ] && command -v innoextract >/dev/null; then
      echo "== $G_TITLE: unpacking $(basename "$setup")"
      mkdir -p "$G_GAME"; innoextract -q -s -e --exclude-temp -d "$G_GAME" "$setup"
    else echo "== $G_TITLE: not found in $G_GAME (and no installer in $INST): skipped"; continue; fi
  fi
  if [ "$g" = re1 ] || [ "$g" = re2 ]; then
    "$HERE/recomp/tools/convert_movies.sh" "$G_GAME" || echo "   ($G_TITLE's movies were not converted: they will be skipped)"
  fi
  for v in $G_VERS; do
    vid="${v%%:*}"
    if [ -z "$SKIP_BUILD" ]; then
      echo "== building $G_TITLE ($vid)"
      LOGF="$HOMEDIR/build-$vid.log"
      "$HERE/recomp/build_re.sh" "$vid" "$G_GAME" > "$LOGF" 2>&1 || { echo "   failed: see $LOGF"; continue; }
    fi
    [ -x "$HERE/recomp/work/$vid/app/$(echo "$v" | cut -d: -f2)" ] && DONE="$DONE $vid"
  done
done
[ -n "$DONE" ] || { echo "nothing built"; exit 1; }

# the app: the launcher, each version's binary and executable, the art (GOG's icons), the list of games
STAGE="$(mktemp -d "$APPS/.shc.XXXXXX" 2>/dev/null || { mkdir -p "$APPS"; mktemp -d "$APPS/.shc.XXXXXX"; })/$NAME.app"
mkdir -p "$STAGE/Contents/MacOS" "$STAGE/Contents/Resources/art"
OLD="$A/Contents/Resources/games.plist"
clang -O2 -fobjc-arc -Wall -Wno-unused-function -framework Cocoa -framework CoreImage -framework GameController -o "$STAGE/Contents/MacOS/SurvivalHorrorClassics" "$HERE/recomp/host/horror_launcher.m" "$HERE/recomp/host/horror_mods.m"
U_BUILT="$(git -C "$HERE" rev-parse HEAD 2>/dev/null || echo unknown)"
G="$STAGE/Contents/Resources/games.plist"; plutil -create xml1 "$G"
INSTALLED=""
for g in $ALL; do
  game_info "$g"
  for v in $G_VERS; do
    vid="${v%%:*}"; bin="$(echo "$v" | cut -d: -f2)"; exe="$(echo "$v" | cut -d: -f3)"; data="$HOMEDIR/$vid"
    case " $DONE " in
      *" $vid "*) W="$HERE/recomp/work/$vid"; cp "$W/app/$bin" "$STAGE/Contents/MacOS/"; cp "$W/$exe" "$STAGE/Contents/Resources/$vid-$exe"   # your copy, this Mac only
                  for f in shaders.metal dsr_snapshot.bin; do cp "$W/app/$f" "$STAGE/Contents/MacOS/"; done; game="$G_GAME"; built="$U_BUILT";;
      *) [ -f "$OLD" ] && [ -n "$(pl "$OLD" "$vid.bin")" ] || continue                                  # kept from the app as it was
         cp "$A/Contents/MacOS/$bin" "$STAGE/Contents/MacOS/"; cp "$A/Contents/Resources/$vid-$exe" "$STAGE/Contents/Resources/"
         for f in shaders.metal dsr_snapshot.bin; do [ -f "$STAGE/Contents/MacOS/$f" ] || cp "$A/Contents/MacOS/$f" "$STAGE/Contents/MacOS/"; done
         game="$(pl "$OLD" "$vid.game")"; built="$(pl "$OLD" "$vid.built")";;
    esac
    mkdir -p "$data"
    plutil -insert "$vid" -dictionary "$G"; plutil -insert "$vid.bin" -string "$bin" "$G"; plutil -insert "$vid.exe" -string "$vid-$exe" "$G"
    plutil -insert "$vid.game" -string "$game" "$G"; plutil -insert "$vid.data" -string "$data" "$G"; plutil -insert "$vid.built" -string "$built" "$G"
    INSTALLED="$INSTALLED $vid"
  done
  # the card's art: the game's own GOG icon (goggame-*.ico in its folder), when the folder is reachable
  ico="$(ls "$G_GAME"/goggame-*.ico "$G_GAME"/app/goggame-*.ico 2>/dev/null | head -1 || true)"
  if [ -n "$ico" ]; then sips -s format png "$ico" --out "$STAGE/Contents/Resources/art/$g.png" >/dev/null 2>&1 || true
  elif [ -f "$A/Contents/Resources/art/$g.png" ]; then cp "$A/Contents/Resources/art/$g.png" "$STAGE/Contents/Resources/art/"; fi
  # the launch window's backdrop and card picture: the game's own title screen, captured from it here (a few seconds in
  # test mode: a background window, muted); GOG's store banner from the game's folder (app/webcache.zip) as a fallback
  ART="$STAGE/Contents/Resources/art"
  if [ -d "$G_GAME" ] && [ -x "$STAGE/Contents/MacOS/$(echo "$G_VERS" | cut -d' ' -f1 | cut -d: -f2)" ]; then
    case "$g" in re2) frames="600 800 1000"; args=-mode0;; re1) frames="760 800 840"; args="";; re3) frames="1180 1230 1280"; args="";; *) frames="1500 1700 1900"; args="";; esac
    ART_FRAMES="$frames" "$HERE/recomp/tools/capture_art.sh" "$STAGE/Contents/MacOS" "$STAGE/Contents/Resources" "$ART" "$g" "$G_GAME" -- $args || true
    wc="$G_GAME/app/webcache.zip"
    if [ -f "$wc" ]; then
      T="$(mktemp -d)"; unzip -q -o "$wc" -d "$T" 2>/dev/null || true
      b="$(ls -S "$T"/*.jpg 2>/dev/null | grep -v logo | head -1 || true)"; [ -n "$b" ] && cp "$b" "$ART/$g-banner.jpg"
      rm -rf "$T"
    fi
  fi
  for f in "$g-title.png" "$g-banner.jpg"; do [ -f "$ART/$f" ] || [ ! -f "$A/Contents/Resources/art/$f" ] || cp "$A/Contents/Resources/art/$f" "$ART/"; done
done
# the app's icon: Resident Evil's card art, else the first there is
for g in re1 re2 re3 dc; do
  p="$STAGE/Contents/Resources/art/$g.png"; [ -f "$p" ] || continue
  sips -z 512 512 "$p" --out "$STAGE/Contents/Resources/icon512.png" >/dev/null 2>&1 && sips -s format icns "$STAGE/Contents/Resources/icon512.png" --out "$STAGE/Contents/Resources/icon.icns" >/dev/null 2>&1 || true
  rm -f "$STAGE/Contents/Resources/icon512.png"; break
done
# updates from the launch window: a worktree of this checkout at the branch it follows, then this installer again
U_WORK=stable; [[ "$U_BRANCH" == nightly ]] && U_WORK=nightly
U_REPO="$(git -C "$HERE" rev-parse --path-format=absolute --git-common-dir 2>/dev/null || true)"; U_REPO="${U_REPO%/.git}"
if [ -n "$U_REPO" ] && git -C "$U_REPO" remote get-url origin >/dev/null 2>&1; then
  U_GAMES=""
  for g in $ALL; do game_info "$g"; for v in $G_VERS; do
    if [[ " $INSTALLED " == *" ${v%%:*} "* ]]; then U_GAMES="${U_GAMES:+$U_GAMES,}$g"; break; fi
  done; done
  cat > "$STAGE/Contents/MacOS/update" <<UPDATE
#!/bin/bash
# $NAME updater (generated by install.sh): fetches $U_BRANCH into a worktree of the checkout this app was installed
# from and installs again from it (the games are rebuilt)
set -euo pipefail
REPO=$(printf '%q' "$U_REPO"); BRANCH=$U_BRANCH; W="\$REPO/build/$U_WORK"
export GIT_TERMINAL_PROMPT=0 PATH="/opt/homebrew/bin:/usr/local/bin:\$PATH"
OWNER="\$(git -C "\$REPO" remote get-url origin | sed -E 's#.*github.com[:/]([^/]+)/.*#\1#')"
[ -n "\${GH_TOKEN:-}" ] || GH_TOKEN="\$(gh auth token --user "\$OWNER" 2>/dev/null || true)"; [ -z "\$GH_TOKEN" ] || export GH_TOKEN
if [ "\${1:-}" = --check ]; then git -C "\$REPO" ls-remote origin "refs/heads/\$BRANCH" | cut -f1; exit 0; fi
git -C "\$REPO" fetch -q origin "\$BRANCH"; git -C "\$REPO" worktree prune
if [ -e "\$W/.git" ]; then git -C "\$W" checkout -q -f --detach "origin/\$BRANCH"
else mkdir -p "\$REPO/build"; git -C "\$REPO" worktree add -q -f --detach "\$W" "origin/\$BRANCH"; fi
[ -e "\$W/recomp/.venv" ] || [ ! -e "\$REPO/recomp/.venv" ] || ln -s "\$(readlink "\$REPO/recomp/.venv" 2>/dev/null || echo "\$REPO/recomp/.venv")" "\$W/recomp/.venv"
exec "\$W/install.sh" --games $U_GAMES --games-root $(printf '%q' "$GROOT") --installers $(printf '%q' "$INST") --app-dir $(printf '%q' "$APPS") --branch $U_BRANCH > $(printf '%q' "$HOMEDIR/update.log") 2>&1
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
  <key>CFBundleExecutable</key><string>SurvivalHorrorClassics</string>
  <key>CFBundleIconFile</key><string>icon</string>
  <key>CFBundlePackageType</key><string>APPL</string>
  <key>CFBundleShortVersionString</key><string>${U_BUILT:0:7}</string>
  <key>CFBundleVersion</key><string>${U_BUILT:0:7}</string>
  <key>LSApplicationCategoryType</key><string>public.app-category.action-games</string>
  <key>GCSupportsGameMode</key><true/>
  <key>LSMinimumSystemVersion</key><string>13.0</string>
  <key>NSHighResolutionCapable</key><true/>
  <key>SHCBranch</key><string>$U_BRANCH</string>
  <key>SHCBuilt</key><string>$U_BUILT</string>
</dict></plist>
PLIST
codesign --force --deep --sign - "$STAGE" >/dev/null 2>&1 || true
mkdir -p "$APPS"; rm -rf "$A"; mv "$STAGE" "$A"; rmdir "$(dirname "$STAGE")" 2>/dev/null || true
touch "$A"
echo "== done: $A (${INSTALLED# })"
