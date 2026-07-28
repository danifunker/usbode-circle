#!/bin/bash
#
# generate-os-sublist.sh - fill in os-sublist.json from freshly built images.
#
# os-sublist.json in the repository root is a template: every value that
# changes from build to build is a @PLACEHOLDER@. This script computes those
# values from the .img.xz files that `make images-dist` just produced and
# writes the finished list to --output. `make release` runs it automatically
# and the result is published as a release asset, which is what makes
#
#   https://github.com/danifunker/usbode-circle/releases/latest/download/os-sublist.json
#
# directly usable as an rpi-imager repository:
#
#   rpi-imager --repo <that url>
#
# The image URLs written into the list use the same /releases/latest/download/
# form on purpose. Stable releases are cut by retagging the CI build-NNN
# prerelease to vX.Y.Z, and retagging changes the /releases/download/<tag>/
# path, so a tag-pinned URL would 404 the moment a build is promoted. The
# latest/download/ form follows the promotion, and because the list and the
# images it points at are assets of the same release, the checksums always
# match what is served.
#
# Usage:
#   scripts/generate-os-sublist.sh --img32 <file.img.xz> --img64 <file.img.xz> \
#       --output <file> [--template <file>] [--url-base <url>] [--date <YYYY-MM-DD>]

set -euo pipefail

TEMPLATE="os-sublist.json"
URL_BASE="https://github.com/danifunker/usbode-circle/releases/latest/download"
RELEASE_DATE="$(date -u +%Y-%m-%d)"
IMG32=""
IMG64=""
OUTPUT=""

usage() {
    echo "Usage: $0 --img32 <file.img.xz> --img64 <file.img.xz> --output <file>"
    echo "          [--template <file>]   (default: $TEMPLATE)"
    echo "          [--url-base <url>]    (default: $URL_BASE)"
    echo "          [--date <YYYY-MM-DD>] (default: today, UTC)"
    exit 1
}

while [ $# -gt 0 ]; do
    case "$1" in
        --img32)    IMG32="$2"; shift 2 ;;
        --img64)    IMG64="$2"; shift 2 ;;
        --output)   OUTPUT="$2"; shift 2 ;;
        --template) TEMPLATE="$2"; shift 2 ;;
        --url-base) URL_BASE="${2%/}"; shift 2 ;;
        --date)     RELEASE_DATE="$2"; shift 2 ;;
        -h|--help)  usage ;;
        *) echo "Error: unknown argument '$1'" >&2; usage ;;
    esac
done

[ -n "$IMG32" ]  || { echo "Error: --img32 is required" >&2; usage; }
[ -n "$IMG64" ]  || { echo "Error: --img64 is required" >&2; usage; }
[ -n "$OUTPUT" ] || { echo "Error: --output is required" >&2; usage; }

for f in "$TEMPLATE" "$IMG32" "$IMG64"; do
    [ -f "$f" ] || { echo "Error: no such file: $f" >&2; exit 1; }
    [ -s "$f" ] || { echo "Error: file is empty: $f" >&2; exit 1; }
done

command -v xz >/dev/null || { echo "Error: xz not found" >&2; exit 1; }

# sha256sum on Linux, shasum on macOS.
sha256_stream() {
    if command -v sha256sum >/dev/null; then
        sha256sum | cut -d' ' -f1
    else
        shasum -a 256 | cut -d' ' -f1
    fi
}

# Uncompressed size straight out of the .xz index - no need to decompress.
uncompressed_size() {
    xz --robot -l "$1" | awk -F'\t' '$1 == "totals" { print $5 }'
}

# Escape what sed treats as special in an s||| replacement.
sed_escape() {
    printf '%s' "$1" | sed -e 's/[\\&|]/\\&/g'
}

# Everything rpi-imager needs for one image entry, from the compressed file:
# size and checksum of the download itself, plus size and checksum of the
# .img it expands to (rpi-imager verifies the expanded image as it writes).
declare -a URL EXTRACT_SIZE EXTRACT_SHA DOWNLOAD_SIZE DOWNLOAD_SHA
measure() { # $1 = index (0 = 32-bit, 1 = 64-bit), $2 = path to .img.xz
    local i="$1" img="$2"
    echo "Measuring $img ..."

    URL[$i]="$URL_BASE/$(basename "$img")"
    DOWNLOAD_SIZE[$i]=$(wc -c < "$img" | tr -d '[:space:]')
    DOWNLOAD_SHA[$i]=$(sha256_stream < "$img")
    EXTRACT_SIZE[$i]=$(uncompressed_size "$img")
    EXTRACT_SHA[$i]=$(xz -dc "$img" | sha256_stream)

    if [ -z "${EXTRACT_SIZE[$i]}" ]; then
        echo "Error: could not read uncompressed size from $img" >&2
        exit 1
    fi

    echo "  url                   ${URL[$i]}"
    echo "  image_download_size   ${DOWNLOAD_SIZE[$i]}"
    echo "  image_download_sha256 ${DOWNLOAD_SHA[$i]}"
    echo "  extract_size          ${EXTRACT_SIZE[$i]}"
    echo "  extract_sha256        ${EXTRACT_SHA[$i]}"
}

measure 0 "$IMG32"
measure 1 "$IMG64"

mkdir -p "$(dirname "$OUTPUT")"
TMP="$OUTPUT.tmp"
trap 'rm -f "$TMP"' EXIT

# The numeric fields are quoted in the template so it stays valid JSON; the
# quotes are part of the match and are dropped here, leaving a JSON number.
sed \
    -e "s|@RELEASE_DATE@|$(sed_escape "$RELEASE_DATE")|g" \
    -e "s|@URL_32@|$(sed_escape "${URL[0]}")|g" \
    -e "s|@URL_64@|$(sed_escape "${URL[1]}")|g" \
    -e "s|@EXTRACT_SHA256_32@|$(sed_escape "${EXTRACT_SHA[0]}")|g" \
    -e "s|@EXTRACT_SHA256_64@|$(sed_escape "${EXTRACT_SHA[1]}")|g" \
    -e "s|@IMAGE_DOWNLOAD_SHA256_32@|$(sed_escape "${DOWNLOAD_SHA[0]}")|g" \
    -e "s|@IMAGE_DOWNLOAD_SHA256_64@|$(sed_escape "${DOWNLOAD_SHA[1]}")|g" \
    -e "s|\"@EXTRACT_SIZE_32@\"|$(sed_escape "${EXTRACT_SIZE[0]}")|g" \
    -e "s|\"@EXTRACT_SIZE_64@\"|$(sed_escape "${EXTRACT_SIZE[1]}")|g" \
    -e "s|\"@IMAGE_DOWNLOAD_SIZE_32@\"|$(sed_escape "${DOWNLOAD_SIZE[0]}")|g" \
    -e "s|\"@IMAGE_DOWNLOAD_SIZE_64@\"|$(sed_escape "${DOWNLOAD_SIZE[1]}")|g" \
    "$TEMPLATE" > "$TMP"

# A placeholder that survived means the template grew a field this script
# does not know how to fill. Fail rather than publish a broken list.
if grep -o '@[A-Z0-9_]\{1,\}@' "$TMP" | sort -u | grep .; then
    echo "Error: unsubstituted placeholders left in $TEMPLATE (listed above)" >&2
    exit 1
fi

if command -v python3 >/dev/null; then
    python3 -c 'import json,sys; json.load(open(sys.argv[1]))' "$TMP" \
        || { echo "Error: generated file is not valid JSON" >&2; exit 1; }
fi

mv "$TMP" "$OUTPUT"
trap - EXIT
echo "Wrote $OUTPUT"
