#!/usr/bin/env bash
# tools/port/check.sh - compile translated files one at a time with the project's flags.
#
#   tools/port/check.sh [--clang] [--pch] [-DNAME[=VALUE] ...] <file.cpp|file.h>...
#
# * .cpp: compiled (syntax + semantics, no object file kept) with -Wall -Wextra.
# * .h:   a translation unit that only includes the header is compiled (checks that the
#         header is self-contained).
# Include roots are the Java source roots plus jlang/include (CONVENTIONS §3.1). The two mysql5
# DAO script roots never go on the path together: files under loginserver/ get
# loginserver/data/scripts/system/database, everything else gameserver/data/scripts/system/database.
# Exit status: number of files that failed (0 = all good).
set -u

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
CXX="${CXX_GCC:-g++}"
EXTRA=()
FILES=()
for arg in "$@"; do
    case "$arg" in
        --clang) CXX="${CXX_CLANG:-clang++}" ;;
        --gcc) CXX="${CXX_GCC:-g++}" ;;
        -D*|-U*|-W*|-f*|-O*|-g*) EXTRA+=("$arg") ;;
        -h|--help) sed -n '2,12p' "${BASH_SOURCE[0]}"; exit 0 ;;
        *) FILES+=("$arg") ;;
    esac
done
if [ ${#FILES[@]} -eq 0 ]; then
    echo "usage: tools/port/check.sh [--clang] <file.cpp|file.h>..." >&2
    exit 2
fi

FLAGS=(-std=c++20 -fwrapv -fno-strict-aliasing -fnon-call-exceptions -pthread -Wall -Wextra
       -Wno-unused-parameter -fsyntax-only)

failed=0
tmpdir="$(mktemp -d)"
trap 'rm -rf "$tmpdir"' EXIT

case "$(basename "$CXX")" in
    *clang*) ;;
    *) FLAGS+=(-fno-delete-dead-exceptions) ;;  # see CMakeLists.txt (AION_REQUIRED_FLAGS)
esac

for f in "${FILES[@]}"; do
    if [ ! -f "$f" ]; then
        echo "check.sh: no such file: $f" >&2
        failed=$((failed + 1))
        continue
    fi
    abs="$(cd "$(dirname "$f")" && pwd)/$(basename "$f")"
    rel="${abs#"$ROOT"/}"
    INC=(-I"$ROOT/jlang/include" -I"$ROOT/commons/src" -I"$ROOT/loginserver/src"
         -I"$ROOT/chatserver/src" -I"$ROOT/gameserver/src"
         -I"$ROOT/gameserver/data/scripts/system/handlers" -I"$ROOT/geoengine/src")
    case "$rel" in
        loginserver/*) INC+=(-I"$ROOT/loginserver/data/scripts/system/database") ;;
        *) INC+=(-I"$ROOT/gameserver/data/scripts/system/database") ;;
    esac
    case "$abs" in
        *.h|*.hpp)
            tu="$tmpdir/header_check.cpp"
            printf '#include "%s"\n' "$abs" > "$tu"
            src="$tu" ;;
        *) src="$abs" ;;
    esac
    if "$CXX" "${FLAGS[@]}" "${EXTRA[@]}" "${INC[@]}" "$src"; then
        echo "OK   $rel ($(basename "$CXX"))"
    else
        echo "FAIL $rel ($(basename "$CXX"))"
        failed=$((failed + 1))
    fi
done
exit $failed
