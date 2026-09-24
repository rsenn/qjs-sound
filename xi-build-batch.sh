#!/usr/bin/env bash
#
# Find candidate single-instrument sample files on disk via `locate` and
# batch-run xi-build.js on each, producing FastTracker/MilkyTracker .xi
# instruments.
#
# Filtering is filename/path-based only (no audio analysis before build):
#   - extension must be one of the formats libsndfile (xi-build.js's SndFile
#     backend) can actually read
#   - path must NOT match a drum/loop/fx/vocal/mix exclude pattern
#   - path must match an instrument/pitched-sample include pattern, unless -a
#
# Usage:
#   xi-build-batch.sh [-o outdir] [-a] [-n]
#
#   -o outdir   where to write .xi files (default: ./.tmp/xi-batch)
#   -a          disable the include filter (still applies the exclude filter)
#   -n          dry run: list candidates and exit, build nothing
#
# Requires: locate (mlocate/plocate, already updated -- run `updatedb` first
# if results look stale), qjsm, and xi-build.js in this directory.

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
XI_BUILD="$SCRIPT_DIR/xi-build.js"
QUICKJS_MODULE_DIR="${QUICKJS_MODULE_PATH:-$SCRIPT_DIR/build/x86_64-linux-debug}"

OUT_DIR="$SCRIPT_DIR/.tmp/xi-batch"
INCLUDE_FILTER=1
DRY_RUN=0

while getopts "o:an" opt; do
  case "$opt" in
    o) OUT_DIR="$OPTARG" ;;
    a) INCLUDE_FILTER=0 ;;
    n) DRY_RUN=1 ;;
    *) echo "usage: $0 [-o outdir] [-a] [-n]" >&2; exit 1 ;;
  esac
done

mkdir -p "$OUT_DIR"

# Every extension libsndfile (this project's `sndfile` module) can open --
# actual decode success for the compressed ones (flac/ogg) still depends on
# how the system's libsndfile was built, so failures there just get logged,
# not filtered out up front.
EXT_PATTERN='\.\(wav\|wave\|aif\|aiff\|au\|snd\|flac\|ogg\|oga\|caf\|w64\)$'

# Path/filename hints for a single pitched instrument sample: note names
# (c3, gs4, bb2...), instrument family names, or generic
# sample/sustain/patch/instrument wording.
INCLUDE_RE='(^|[/_ .-])(piano|grand|upright|epiano|rhodes|wurli|organ|violin|viola|cello|contrabass|strings?|brass|trumpet|trombone|tuba|horn|sax(ophone)?|flute|clarinet|oboe|bassoon|guitar|acoustic|nylon|bass|synth|pad|lead|pluck|choir|harp|marimba|xylophone|vibraphone|kalimba|bell|celesta|mellotron|instrument|patch|sample|sustain|susp?|oneshot|one[_-]shot|note|[a-g]s?[0-8])([/_ .-]|$)'

# Path/filename hints this is NOT a single instrument sample: drum hits,
# loops, fx, spoken word, or full mixes.
EXCLUDE_RE='(loop|drum|kick|snare|hat|hihat|perc|fx|sfx|impact|riser|whoosh|dialog|speech|beat|break|ride|crash|tom[0-9]?|clap|click|metronome|vocal|vox|dialogue|song|mix(down)?|master|full[_ -]?mix|demo|jingle|stinger|announce)'

echo "searching for candidate instrument samples..." >&2

candidates="$OUT_DIR/candidates.txt"
if [ "$INCLUDE_FILTER" -eq 1 ]; then
  locate -i -e -r "$EXT_PATTERN" \
    | grep -viE "$EXCLUDE_RE" \
    | grep -iE "$INCLUDE_RE" \
    | sort -u > "$candidates"
else
  locate -i -e -r "$EXT_PATTERN" \
    | grep -viE "$EXCLUDE_RE" \
    | sort -u > "$candidates"
fi

total=$(wc -l < "$candidates")
echo "found $total candidate file(s) -> $candidates" >&2

if [ "$DRY_RUN" -eq 1 ]; then
  cat "$candidates"
  exit 0
fi

echo "building .xi files into $OUT_DIR" >&2

ok=0 fail=0 skip=0
log="$OUT_DIR/build.log"
: > "$log"

while IFS= read -r wav; do
  [ -f "$wav" ] || { skip=$((skip + 1)); continue; }

  # Skip files too small to be a real note (silence/click) or too large to
  # plausibly be a single-instrument sample (full mixes/songs).
  size=$(stat -c%s -- "$wav" 2>/dev/null || echo 0)
  if [ "$size" -lt 1024 ] || [ "$size" -gt $((50 * 1024 * 1024)) ]; then
    skip=$((skip + 1))
    continue
  fi

  base="$(basename "$wav")"
  # Path-hashed suffix, since candidates from different directories often
  # share a basename (e.g. two "c3.wav" from different sample packs).
  hash="$(printf '%s' "$wav" | md5sum | cut -c1-8)"
  out="$OUT_DIR/${base%.*}__$hash.xi"

  echo "-> $wav" >&2
  {
    echo "=== $wav ==="
    if QUICKJS_MODULE_PATH="$QUICKJS_MODULE_DIR" qjsm "$XI_BUILD" "$wav" "$out"; then
      echo "OK -> $out"
    else
      echo "FAILED"
    fi
  } >> "$log" 2>&1

  if [ -f "$out" ]; then
    ok=$((ok + 1))
  else
    echo "   failed (see $log)" >&2
    fail=$((fail + 1))
  fi
done < "$candidates"

echo "done: $ok built, $fail failed, $skip skipped -> $OUT_DIR" >&2
