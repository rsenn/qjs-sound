#!/bin/sh
# Builds qjs-sound-web.wasm: STK + SndObj DSP, no device I/O. Usage: build.sh [outdir]
# EMCC=... picks the compiler; EM_CACHE=<writable dir> when the emsdk cache is read-only.
cd "$(dirname "$0")" || exit 1
EMCC=${EMCC:-em++}
OUT=${1:-.}
STK=../third_party/stk
SND=../third_party/sndobj

# only the DSP classes the exports use (the linker pulls their dependencies in)
STK_SRC="$STK/src/Stk.cpp $STK/src/BiQuad.cpp $STK/src/Plucked.cpp $STK/src/DelayA.cpp $STK/src/DelayL.cpp
 $STK/src/OneZero.cpp $STK/src/OnePole.cpp $STK/src/Noise.cpp"
SND_SRC="$SND/src/SndObj.cpp $SND/src/Filter.cpp $SND/src/Reson.cpp"

# standalone: one .wasm with no JS loader, instantiated by hand inside the AudioWorklet
exec "$EMCC" qjs-sound-web.cpp $STK_SRC $SND_SRC \
  -I "$STK/include" -I "$SND/include/SndObj" -I "$SND/src" \
  -sSTANDALONE_WASM -sALLOW_MEMORY_GROWTH -sINITIAL_MEMORY=2MB --no-entry \
  -O2 -w -o "$OUT/qjs-sound-web.wasm"
