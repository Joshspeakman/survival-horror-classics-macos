#!/bin/bash
# build_ffmpeg.sh <out/ffmpeg>: a small ffmpeg and ffprobe (FFmpeg 7.1, LGPL 2.1: no GPL parts, no external libraries)
# with only what this project asks of them, for the downloaded app (recomp/setup/suite.json "helpers"; the player's Mac
# has no Homebrew): setup converts Resident Evil's Cinepak and Resident Evil 2's Indeo movies to MPEG-1
# (tools/convert_movies.sh), and the Mods window's Restore from Disc reads PlayStation STR movies and XA audio and the PC
# releases' clips (tools/psx_import.py). ffprobe is written beside ffmpeg. The source is FFmpeg's own release tarball,
# checked against its SHA-256; the build is cached in ~/ProjectBuilds/_toolkit/ffmpeg-7.1 (its configure line is in
# its ffmpeg -buildconf). The LGPL's source offer: https://ffmpeg.org/releases/ffmpeg-7.1.tar.xz with this script.
set -euo pipefail
OUT="${1:?usage: build_ffmpeg.sh <out/ffmpeg>}"
VER=7.1; SHA=40973d44970dbc83ef302b0609f2e74982be2d85916dd2ee7472d30678a7abe6
CACHE="$HOME/ProjectBuilds/_toolkit/ffmpeg-$VER"
DEC=cinepak,indeo2,indeo3,indeo4,indeo5,msvideo1,mdec,mpeg1video,mpeg2video,rawvideo,pcm_s16le,pcm_u8,pcm_s16be,adpcm_ms,adpcm_ima_wav,adpcm_xa,mp2,mp3,vorbis,flac
if [ ! -x "$CACHE/bin/ffmpeg" ] || [ ! -x "$CACHE/bin/ffprobe" ]; then
  W="$(mktemp -d)"; trap 'rm -rf "$W"' EXIT
  SRC="$CACHE.tar.xz"                                               # the source, kept for rebuilds (and the LGPL's offer)
  [ -f "$SRC" ] || { curl -fsSL -o "$SRC.part" "https://ffmpeg.org/releases/ffmpeg-$VER.tar.xz" && mv "$SRC.part" "$SRC"; }
  [ "$(shasum -a 256 "$SRC" | cut -d' ' -f1)" = "$SHA" ] || { echo "build_ffmpeg: $SRC isn't FFmpeg $VER's release tarball"; rm -f "$SRC"; exit 1; }
  tar -xf "$SRC" -C "$W"
  (cd "$W/ffmpeg-$VER" && ./configure --prefix="$CACHE" --cc=clang --arch=arm64 --extra-cflags="-mmacosx-version-min=13.0" --extra-ldflags="-mmacosx-version-min=13.0" \
     --disable-everything --disable-autodetect --disable-doc --disable-debug --disable-network --disable-ffplay --enable-static --disable-shared \
     --enable-ffmpeg --enable-ffprobe --enable-protocol=file,pipe \
     --enable-demuxer=avi,wav,str,ogg,mp3,mpegps,mpegvideo,flac,pcm_s16le --enable-decoder="$DEC" \
     --enable-encoder=mpeg1video,mp2,pcm_s16le,rawvideo --enable-muxer=mpeg1system,s16le,wav,rawvideo,null \
     --enable-parser=mpegvideo,mpegaudio,flac,vorbis --enable-filter=scale,fps,format,aformat,aresample,null,anull,setpts,asetpts,trim,atrim \
     --enable-swscale --enable-swresample > "$W/configure.log" 2>&1 || { tail -20 "$W/configure.log"; exit 1; }
   make -j"$(getconf _NPROCESSORS_ONLN)" > "$W/make.log" 2>&1 || { tail -20 "$W/make.log"; exit 1; }
   rm -rf "$CACHE"; make install > /dev/null)
fi
mkdir -p "$(dirname "$OUT")"; cp "$CACHE/bin/ffmpeg" "$OUT"; cp "$CACHE/bin/ffprobe" "$(dirname "$OUT")/ffprobe"
