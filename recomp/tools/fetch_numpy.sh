#!/bin/bash
# fetch_numpy.sh <folder>: numpy's wheel for the toolkit's Python (3.12, arm64), for the downloaded app's Restore from Disc
# (recomp/setup/suite.json "helpers"; tools/psx_import.py needs numpy): the Mods window installs it from the app, with no
# download, into the Python it unpacks the first time (~/Games/SurvivalHorrorClassics/.python). Cached in
# ~/ProjectBuilds/_toolkit/wheels. numpy is BSD-licensed.
set -euo pipefail
OUT="${1:?usage: fetch_numpy.sh <folder>}"; VER=2.1.3; CACHE="$HOME/ProjectBuilds/_toolkit/wheels"
mkdir -p "$CACHE" "$OUT"
ls "$CACHE"/numpy-$VER-cp312-*macosx*arm64.whl >/dev/null 2>&1 ||
  python3 -m pip download -q --only-binary=:all: --platform macosx_14_0_arm64 --python-version 3.12 --implementation cp --no-deps "numpy==$VER" -d "$CACHE"
cp "$CACHE"/numpy-$VER-cp312-*macosx*arm64.whl "$OUT/"
