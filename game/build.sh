#!/bin/sh
# Build the Prince of Persia 3.5" disk image from Jordan Mechner's source,
# using Adam Green's build fork inside a 32-bit Linux container.
#
#   game/build.sh                  unpatched repository source (hash-checked)
#   game/build.sh --release-patch  patched to match the retail 3.5" release
#
# Output: game/out/PrinceOfPersia_3.5.hdv (819,200 bytes, never committed).
set -eu

REPO="https://github.com/adamgreen/Prince-of-Persia-Apple-II.git"
COMMIT="ec78dbfd51013ba349cda8c51c3ce0595fe75342"
EXPECTED_SHA256="21428d817130b9f69673266f9bca361bb3d28286b4a84ffe6524abe844da7080"
IMAGE_NAME="PrinceOfPersia_3.5.hdv"

HERE="$(cd "$(dirname "$0")" && pwd)"
WORK="$HERE/work"
OUT="$HERE/out"

MAKE_ARGS="clean all"
CHECK_HASH=1
case "${1:-}" in
    "") ;;
    --release-patch) MAKE_ARGS="clean all RELEASE_PATCH=1"; CHECK_HASH=0 ;;
    *) echo "usage: build.sh [--release-patch]" >&2; exit 2 ;;
esac

if [ ! -d "$WORK/.git" ]; then
    git clone --quiet "$REPO" "$WORK"
fi
# Start from pristine sources every time: the release patch edits files in
# place and a previous build leaves objects behind.
git -C "$WORK" checkout --quiet --force "$COMMIT"
git -C "$WORK" clean -fdxq

docker build --quiet --platform linux/386 -t pop-build "$HERE" >/dev/null
# shellcheck disable=SC2086
docker run --rm --platform linux/386 -v "$WORK":/src -w /src pop-build make $MAKE_ARGS

mkdir -p "$OUT"
cp "$WORK/$IMAGE_NAME" "$OUT/$IMAGE_NAME"

ACTUAL_SHA256="$(shasum -a 256 "$OUT/$IMAGE_NAME" | cut -d' ' -f1)"
echo "built $OUT/$IMAGE_NAME"
echo "sha256 $ACTUAL_SHA256"
if [ "$CHECK_HASH" = 1 ] && [ "$ACTUAL_SHA256" != "$EXPECTED_SHA256" ]; then
    echo "error: image hash does not match the expected build" >&2
    echo "  expected $EXPECTED_SHA256" >&2
    echo "  got      $ACTUAL_SHA256" >&2
    exit 1
fi
