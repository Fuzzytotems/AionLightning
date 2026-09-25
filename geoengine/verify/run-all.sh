#!/bin/bash
# Reproduces every verification of the recovered geoEngine sources (see ../README.md):
#   1. build geoengine/src (javac --release 8 -g)
#   2. API + normalized-bytecode comparison against gameserver/lib/geoEngine-0.1.jar (bcdiff.py)
#   3. plain 'javap -p' diff (api-diff.sh)
#   4. compile commons + gameserver/src + gameserver/data/scripts against the recompiled classes and,
#      as a control, against the original jar; the two outputs must be byte-identical
#   5. differential getZ()/canSee() test on synthetic geodata, both implementations side by side
#
# usage: run-all.sh WORKDIR [queriesPerKind] [seeds...]
# Needs a JDK >= 11 (javac --release 8), python3, unzip.  Nothing is written outside WORKDIR.
set -e -o pipefail
HERE=$(cd "$(dirname "$0")" && pwd)
ROOT=$(cd "$HERE/.." && pwd)
REPO=$(cd "$ROOT/.." && pwd)
WORK=${1:?usage: run-all.sh WORKDIR [queriesPerKind] [seeds...]}
QUERIES=${2:-20000}
shift 2 2>/dev/null || shift $#
SEEDS=${*:-20110425 1 42}
JAR="$REPO/gameserver/lib/geoEngine-0.1.jar"
mkdir -p "$WORK"
WORK=$(cd "$WORK" && pwd)

echo "== 1. build"
"$HERE/build.sh" "$WORK/classes"

echo "== 2. API and bytecode comparison (bcdiff.py)"
rm -rf "$WORK/jar" && mkdir -p "$WORK/jar" && (cd "$WORK/jar" && unzip -q "$JAR")
python3 "$HERE/bcdiff.py" "$WORK/jar" "$WORK/classes" --code > "$WORK/bcdiff.txt" || true
sed -n '/^##### SUMMARY/,$p' "$WORK/bcdiff.txt"
grep -q '^##### UNEXPLAINED differences: 0$' "$WORK/bcdiff.txt"

echo "== 3. javap -p diff"
"$HERE/api-diff.sh" "$WORK/classes" "$JAR"

echo "== 4. gameserver source compatibility"
rm -rf "$WORK/commons" && mkdir -p "$WORK/commons"
javac --release 8 -nowarn -Xlint:-options -encoding UTF-8 -proc:none \
    -cp "$(ls "$REPO"/commons/lib/*.jar | tr '\n' ':')" -d "$WORK/commons" $(find "$REPO/commons/src" -name '*.java')
# NpcShoutsService.java has a varargs call that javac >= 8 rejects as ambiguous (independent of
# geoEngine); compile a patched copy of that one file.
mkdir -p "$WORK/patched"
sed 's/owner.getObjectId(), param);/owner.getObjectId(), new Object[]{param});/' \
    "$REPO/gameserver/src/org/openaion/gameserver/services/NpcShoutsService.java" > "$WORK/patched/NpcShoutsService.java"
find "$REPO/gameserver/src" "$REPO/gameserver/data/scripts" -name '*.java' \
    | grep -v 'org/openaion/gameserver/services/NpcShoutsService.java$' > "$WORK/gs-sources.txt"
echo "$WORK/patched/NpcShoutsService.java" >> "$WORK/gs-sources.txt"
LIBS=$(ls "$REPO"/gameserver/lib/*.jar | grep -v geoEngine | grep -v open-aion-commons | tr '\n' ':')
for v in recompiled jar; do
    if [ $v = recompiled ]; then G="$WORK/classes"; else G="$JAR"; fi
    rm -rf "$WORK/gs-$v" && mkdir -p "$WORK/gs-$v"
    # data/scripts contains ISO-8859-1 comments, so the whole tree is read as Latin-1
    javac --release 8 -nowarn -Xlint:-options -encoding ISO-8859-1 -proc:none -J-Xmx3g \
        -cp "$G:$LIBS$WORK/commons" -d "$WORK/gs-$v" @"$WORK/gs-sources.txt"
    echo "   against $v geoEngine: $(find "$WORK/gs-$v" -name '*.class' | wc -l) classes"
done
diff -r "$WORK/gs-recompiled" "$WORK/gs-jar" > /dev/null
echo "   gameserver class files are byte-identical in both builds"

echo "== 5. differential getZ/canSee test"
mkdir -p "$WORK/difftest"
javac -d "$WORK/difftest" "$HERE/GeoDiffTest.java"
for seed in $SEEDS; do
    echo "-- seed $seed"
    java -XX:-OmitStackTraceInFastThrow -cp "$WORK/difftest" GeoDiffTest "$JAR" "$WORK/classes" \
        "$WORK/geodata-$seed" "$QUERIES" "$seed" 2>/dev/null
done
echo "== all checks passed"
