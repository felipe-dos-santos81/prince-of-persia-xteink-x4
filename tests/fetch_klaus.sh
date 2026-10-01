#!/bin/sh
# Download Klaus Dormann's 6502 functional test binary (GPL-3, test fixture
# only, never committed or shipped).
set -eu

URL="https://raw.githubusercontent.com/Klaus2m5/6502_65C02_functional_tests/master/bin_files/6502_functional_test.bin"
SHA256="fa12bfc761e6f9057e4cc01a665a7b800ff01ae91f598af1e39a1201d01953fd"
OUT="$(dirname "$0")/data/6502_functional_test.bin"

mkdir -p "$(dirname "$OUT")"
curl -fsSL -o "$OUT.tmp" "$URL"
ACTUAL="$(shasum -a 256 "$OUT.tmp" | cut -d' ' -f1)"
if [ "$ACTUAL" != "$SHA256" ]; then
    echo "hash mismatch for $URL" >&2
    echo "  expected $SHA256" >&2
    echo "  got      $ACTUAL" >&2
    rm -f "$OUT.tmp"
    exit 1
fi
mv "$OUT.tmp" "$OUT"
echo "wrote $OUT"
