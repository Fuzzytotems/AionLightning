#!/bin/sh
# Compiles geoengine/src into OUTDIR the way the original jar was built (javac, debug info on), but
# with javac --release 8 (the oldest target current JDKs support; the jar itself is Java 6 bytecode).
# The jME3 stubs are compiled into a separate directory and are only on the class path, so OUTDIR
# holds exactly the aionjHungary.* classes of geoEngine-0.1.jar.
#
# usage: build.sh OUTDIR
set -e
HERE=$(cd "$(dirname "$0")" && pwd)
ROOT=$(cd "$HERE/.." && pwd)
REPO=$(cd "$ROOT/.." && pwd)
OUT=${1:?usage: build.sh OUTDIR}
STUBS="$OUT.jme3-stubs"
rm -rf "$OUT" "$STUBS"
mkdir -p "$OUT" "$STUBS"
javac --release 8 -nowarn -encoding UTF-8 -d "$STUBS" $(find "$ROOT/jme3-stubs/src" -name '*.java')
javac --release 8 -g -nowarn -Xlint:-options -encoding UTF-8 \
    -cp "$STUBS:$REPO/gameserver/lib/log4j-1.2.16.jar" -d "$OUT" $(find "$ROOT/src" -name '*.java')
echo "compiled $(find "$OUT" -name '*.class' | wc -l) classes into $OUT"
