#!/bin/sh
# Package built plug-ins into a release archive.
#   tools/package.sh <build dir> <platform: macos|windows|linux> <output .zip or .tar.gz>
set -e
BUILD=$1; PLATFORM=$2; OUT=$3
case $OUT in /*) ;; *) OUT="$PWD/$OUT" ;; esac
ROOT=$(cd "$(dirname "$0")/.." && pwd)
STAGE=$(mktemp -d)/SQ8L-X
mkdir -p "$STAGE"
if [ "$PLATFORM" = macos ]; then
    cp -R "$BUILD/bin/SQ8L-X.vst" "$BUILD/bin/SQ8L-X.vst3" "$BUILD/bin/SQ8L-X.component" "$BUILD/bin/SQ8L-X.clap" "$STAGE/"
    # Ad-hoc signature over the whole bundle (the linker only signs the binary). Not a
    # Developer ID: downloaded copies still need the quarantine flag removed.
    for b in SQ8L-X.vst SQ8L-X.vst3 SQ8L-X.component SQ8L-X.clap; do
        codesign --force --sign - --timestamp=none "$STAGE/$b"
        codesign --verify --strict "$STAGE/$b"
    done
elif [ "$PLATFORM" = linux ]; then
    cp -R "$BUILD/bin/SQ8L-X.so" "$BUILD/bin/SQ8L-X.vst3" "$BUILD/bin/SQ8L-X.clap" "$BUILD/bin/SQ8L-X.lv2" "$STAGE/"
    ${STRIP:-strip} "$STAGE/SQ8L-X.so" "$STAGE"/SQ8L-X.vst3/Contents/*-linux/SQ8L-X.so "$STAGE/SQ8L-X.clap" \
        "$STAGE/SQ8L-X.lv2/SQ8L-X.so"
else
    cp -R "$BUILD/bin/SQ8L-X.dll" "$BUILD/bin/SQ8L-X.vst3" "$BUILD/bin/SQ8L-X.clap" "$STAGE/"
    ${STRIP:-x86_64-w64-mingw32-strip} "$STAGE/SQ8L-X.dll" "$STAGE/SQ8L-X.vst3/Contents/x86_64-win/SQ8L-X.vst3" "$STAGE/SQ8L-X.clap"
fi
cp "$ROOT/README.md" "$ROOT/THIRD_PARTY_NOTICES.md" "$ROOT/LICENSE.md" "$STAGE/"
case $OUT in
    *.tar.gz) (cd "$(dirname "$STAGE")" && tar czf "$OUT" SQ8L-X) ;;
    *) (cd "$(dirname "$STAGE")" && zip -qr -X "$OUT" SQ8L-X) ;;
esac
echo "wrote $OUT"
