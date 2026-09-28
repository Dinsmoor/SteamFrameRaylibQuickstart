#!/usr/bin/env bash
# get-speech.sh - fetch and build the optional speech recognizer: whisper.cpp
# plus our shim (tools/speech/sfq_speech.c) as one libsfq_speech.so, and the
# smallest English Whisper model. None of it is in the repo (docs/AUDIO.md).
#
#   scripts/get-speech.sh           for this machine  -> build/host-speech/libsfq_speech.so
#   scripts/get-speech.sh frame     for the headset   -> build/frame-speech/libsfq_speech.so
#                                   (inside the Steam Runtime SDK container, like make frame)
#
# The model goes to external/speech/ggml-tiny.en-q5_1.bin (31 MB): "tiny",
# English only, 5-bit quantized -- the smallest Whisper there is, and plenty
# for a handful of command words. make package copies both next to the app.
set -euo pipefail
ROOT=$(cd "$(dirname "$0")/.." && pwd)
cd "$ROOT"
TARGET=${1:-host}
WHISPER_TAG=${WHISPER_TAG:-v1.9.4}
MODEL=${SPEECH_MODEL:-ggml-tiny.en-q5_1.bin}
SRC=external/whisper.cpp
OUT=build/$TARGET-speech

mkdir -p external/speech
if [ ! -d "$SRC" ]; then
  git clone --depth 1 --branch "$WHISPER_TAG" https://github.com/ggml-org/whisper.cpp "$SRC"
fi
if [ ! -f "external/speech/$MODEL" ]; then
  curl -L --fail -o "external/speech/$MODEL.part" "https://huggingface.co/ggerganov/whisper.cpp/resolve/main/$MODEL"
  mv "external/speech/$MODEL.part" "external/speech/$MODEL"
fi

build() {   # run inside the target's environment
  cmake -S "$SRC" -B "$OUT/whisper" -DCMAKE_BUILD_TYPE=Release -DBUILD_SHARED_LIBS=OFF \
        -DWHISPER_BUILD_TESTS=OFF -DWHISPER_BUILD_EXAMPLES=OFF -DWHISPER_BUILD_SERVER=OFF \
        -DGGML_OPENMP=OFF -DGGML_NATIVE=OFF -DCMAKE_POSITION_INDEPENDENT_CODE=ON >/dev/null
  cmake --build "$OUT/whisper" -j"$(nproc)" >/dev/null
  # one shared library: our three functions, whisper and ggml linked in
  libs=$(find "$OUT/whisper" -name 'libwhisper.a' -o -name 'libggml*.a' | sort -r | tr '\n' ' ')
  # the shim is C (compiled as C, so its three names stay unmangled); whisper is C++
  cc -c -fPIC -O2 -o "$OUT/sfq_speech.o" tools/speech/sfq_speech.c -I"$SRC/include" -I"$SRC/ggml/include"
  c++ -shared -fPIC -O2 -o "$OUT/libsfq_speech.so" "$OUT/sfq_speech.o" -Wl,--start-group $libs -Wl,--end-group -lpthread -lm
  echo "built $OUT/libsfq_speech.so"
}

if [ "$TARGET" = host ]; then
  build
elif [ "$TARGET" = frame ]; then
  SDK=${FRAME_SDK:-registry.gitlab.steamos.cloud/steamrt/steamrt4/sdk/arm64:latest}
  # as you, not root (like scripts/frame-build.sh), so the files stay yours
  docker run --rm -v "$ROOT:$ROOT" -w "$ROOT" -u "$(id -u):$(id -g)" -e HOME=/tmp "$SDK" \
    bash -c "$(declare -f build); SRC=$SRC OUT=$OUT build"
else
  echo "usage: scripts/get-speech.sh [host|frame]"; exit 2
fi
echo "model: external/speech/$MODEL"
