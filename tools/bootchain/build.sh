#!/usr/bin/env bash
# Build Biscuit's boot chain add-ons for amonet v2 (README.md here):
#  - kaeru with the A/B slot fix (kaeru issue #102)
#  - a TWRP zip that writes it to expdb, and zips that restore amonet's
#    original kaeru and TWRP
#
#   tools/bootchain/build.sh /path/to/amonet-biscuit-v2.0.0.zip
#
# Needs git, python3 and docker (kaeru's own build image).
# Work and output: $BOOTCHAIN_WORK (default ./bootchain-work), output in its out/.
set -euo pipefail

HERE="$(cd "$(dirname "$0")" && pwd)"
WORK="$(mkdir -p "${BOOTCHAIN_WORK:-bootchain-work}" && cd "${BOOTCHAIN_WORK:-bootchain-work}" && pwd)"
OUT="$WORK/out"

AMONET_ZIP_SHA256="98297293701082bc7272efe077f941c56fc7b6e1f27ef6f2e93b6e4c6fc7b62d"
AMONET_TWRP_SHA256="1dc701a1d931adf37bb55c5b438989b40136a297579a83cf0ee514eac0f165a9"
AMONET_KAERU_SHA256="eaa3b053f9326d06ac81294c17449a3436df36f8e59e8b8bf2bf4b521f8c5ecc"

# Fire OS 6.5.7.4 OTA, pinned by sha256; its lk is the LK that kaeru patches
OTA_URL="https://d1s31zyz7dcc2d.cloudfront.net/2026/8/3/f49aaff7-dd63-4d9c-9e9a-c17498267de5/update-kindle-biscuit_puffin-NS6574_user_7623_0013121734532.bin"
OTA_SHA256="64ab6d2dd85f8093abdd62c275d229c7e9fdd68e4d46892b48bdbd1d100d46d8"
OTA="$WORK/downloads/$(basename "$OTA_URL")"
LK_SHA256="a993322de3c26b709f3b13fb0ab58c106649136acf8dc7ba571e60fcc94b6d86"

# kaeru 7673aa2 (what amonet v2.0.0's kaeru reports) plus the #102 fix
KAERU_REPO="https://github.com/meiser79/kaeru.git"
KAERU_COMMIT="2674337"
KAERU_BRANCH="biscuit-7673aa2-issue102"

die() { echo "ERROR: $*" >&2; exit 1; }
check_sha() { [[ "$(sha256sum "$1" | cut -d' ' -f1)" == "$2" ]] || die "$1: unexpected sha256"; }
fetch_commit() { # repo branch commit dir
  [[ -d "$4/.git" ]] || git clone -q --filter=blob:none --no-checkout -b "$2" "$1" "$4"
  git -C "$4" fetch -q origin "$2"
  git -C "$4" -c advice.detachedHead=false checkout -q --force "$3"
}

[[ $# -eq 1 ]] || die "usage: $0 /path/to/amonet-biscuit-v2.0.0.zip"
AMONET_ZIP="$1"
check_sha "$AMONET_ZIP" "$AMONET_ZIP_SHA256"
mkdir -p "$WORK" "$OUT"

echo "== amonet v2.0.0 images"
unzip -p "$AMONET_ZIP" amonet/bin/twrp.img > "$WORK/amonet-twrp.img"
unzip -p "$AMONET_ZIP" amonet/bin/biscuit-kaeru.bin > "$WORK/amonet-kaeru.bin"
check_sha "$WORK/amonet-twrp.img" "$AMONET_TWRP_SHA256"
check_sha "$WORK/amonet-kaeru.bin" "$AMONET_KAERU_SHA256"

echo "== Fire OS 6.5.7.4 lk"
if [[ ! -f "$OTA" ]]; then
  mkdir -p "$(dirname "$OTA")"
  curl -fL --retry 3 -o "$OTA.partial" "$OTA_URL"
  mv "$OTA.partial" "$OTA"
fi
check_sha "$OTA" "$OTA_SHA256"
python3 -I "$HERE/extract-fireos6-payload.py" "$OTA" lk "$WORK/lk.img"
check_sha "$WORK/lk.img" "$LK_SHA256"

echo "== kaeru $KAERU_COMMIT"
fetch_commit "$KAERU_REPO" "$KAERU_BRANCH" "$KAERU_COMMIT" "$WORK/kaeru"
docker build -q -t biscuit-kaeru-builder:latest "$WORK/kaeru" >/dev/null
cp "$WORK/lk.img" "$WORK/kaeru/lk-ns6574.img"
docker run --rm -u "$(id -u):$(id -g)" -e KAERU_GIT_SHA="$KAERU_COMMIT" \
  -v "$WORK/kaeru:/work" -w /work biscuit-kaeru-builder:latest \
  ./build.sh biscuit lk-ns6574.img > "$WORK/kaeru-build.log" 2>&1 || { tail -30 "$WORK/kaeru-build.log"; die "kaeru build failed"; }
cp "$WORK/kaeru/biscuit-kaeru.bin" "$WORK/biscuit-kaeru-issue102.bin"
grep -aq "2.0.0-g$KAERU_COMMIT" "$WORK/biscuit-kaeru-issue102.bin" || die "kaeru version string missing"

echo "== zips"
python3 -I "$HERE/make-flash-zip.py" expdb "$WORK/biscuit-kaeru-issue102.bin" "$OUT/biscuit-kaeru-issue102.zip"
python3 -I "$HERE/make-flash-zip.py" recovery "$WORK/amonet-twrp.img" "$OUT/restore-amonet-twrp.zip"
python3 -I "$HERE/make-flash-zip.py" expdb "$WORK/amonet-kaeru.bin" "$OUT/restore-amonet-kaeru.zip"
(cd "$OUT" && sha256sum ./*.zip > SHA256SUMS && cat SHA256SUMS)
echo "Install: $HERE/README.md"
