#!/usr/bin/env bash
# Behaviour check for the plain C++ modules (protocol, recipe library, cloud crypto, emoji).
# Builds each golden test twice, against a reference commit and against the working
# tree, and compares the output. Any difference means behaviour changed.
#
#   test_native/run.sh [reference commit, default HEAD]
#
# Needs g++, ArduinoJson (ARDUINOJSON=dir with src/) and an mbedTLS build for the
# crypto test (MBEDTLS_INC=include dir, MBEDTLS_LIB=libmbedcrypto.a; skipped if unset).
set -euo pipefail
cd "$(dirname "$0")/.."
REF=${1:-HEAD}
WORK=$(mktemp -d)
trap 'rm -rf "$WORK"' EXIT
git archive "$REF" src include | tar -x -C "$WORK"
JSON=${ARDUINOJSON:?set ARDUINOJSON to the ArduinoJson folder}/src
fail=0

run() {  # name, test file, sources...
  local name=$1 test=$2
  shift 2
  for side in ref new; do
    local root=. extra=()
    [ "$side" = ref ] && root=$WORK
    local srcs=()
    for s in "$@"; do srcs+=("$root/$s"); done
    [ -n "${MBEDTLS_INC:-}" ] && extra=(-I "$MBEDTLS_INC")
    g++ -O1 -w -std=c++17 -I "$root/include" -I "$JSON" "${extra[@]}" "$test" "${srcs[@]}" ${LIBS:-} -o "$WORK/$name.$side"
    "$WORK/$name.$side" >"$WORK/$name.$side.txt"
  done
  if cmp -s "$WORK/$name.ref.txt" "$WORK/$name.new.txt"; then
    echo "same      $name ($(wc -l <"$WORK/$name.new.txt") results)"
  else
    echo "CHANGED   $name"
    diff "$WORK/$name.ref.txt" "$WORK/$name.new.txt" | head -${DIFF_LINES:-20}
    fail=1
  fi
}

run protocol test_native/protocol_golden.cpp src/xbloom_protocol.cpp
run library test_native/library_golden.cpp src/library.cpp src/xbloom_protocol.cpp
if [ -n "${MBEDTLS_INC:-}" ] && [ -n "${MBEDTLS_LIB:-}" ]; then
  LIBS=$MBEDTLS_LIB run crypto test_native/crypto_golden.cpp src/cloud_crypto.cpp
else
  echo "skipped   crypto (set MBEDTLS_INC and MBEDTLS_LIB)"
fi
# Emoji: a direct check (no reference build). Needs LVGL's headers (LVGL=folder).
if [ -n "${LVGL:-}" ]; then
  gcc -O1 -w -c src/emoji_data.c -o "$WORK/emoji_data.o"
  g++ -O1 -w -std=c++17 -DLV_CONF_INCLUDE_SIMPLE -I include -I "$LVGL" -I "$LVGL/.." test_native/emoji_test.cpp src/emoji.cpp \
    "$WORK/emoji_data.o" -o "$WORK/emoji"
  if out=$("$WORK/emoji"); then echo "ok        emoji ($out)"; else echo "FAILED    emoji"; echo "$out" | head -20; fail=1; fi
else
  echo "skipped   emoji (set LVGL to the LVGL folder)"
fi
exit $fail
