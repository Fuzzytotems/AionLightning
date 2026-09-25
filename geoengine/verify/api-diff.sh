#!/bin/sh
# Plain 'javap -p' comparison of every class in geoEngine-0.1.jar against a directory of recompiled
# classes.  Prints the diff; synthetic members that only newer javac versions generate ($values() in
# enums, the switch-map holder VertexBuffer$1, final flag of the anonymous TempVars$1) show up here and
# are classified by bcdiff.py.
#
# usage: api-diff.sh CLASSES_DIR [JAR]
set -e
HERE=$(cd "$(dirname "$0")" && pwd)
REPO=$(cd "$HERE/../.." && pwd)
NEW=${1:?usage: api-diff.sh CLASSES_DIR [JAR]}
JAR=${2:-$REPO/gameserver/lib/geoEngine-0.1.jar}
TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT
(cd "$TMP" && unzip -q "$JAR")
list() { (cd "$1" && find aionjHungary -name '*.class' | sed 's/\.class$//' | tr / . | sort); }
javap -p -classpath "$TMP" $(list "$TMP") 2>/dev/null > "$TMP/jar.txt"
javap -p -classpath "$NEW" $(list "$NEW") 2>/dev/null > "$TMP/new.txt"
if diff "$TMP/jar.txt" "$TMP/new.txt"; then echo "javap -p: identical"; fi
exit 0
