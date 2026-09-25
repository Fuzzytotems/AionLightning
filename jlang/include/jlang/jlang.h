// jlang/jlang.h - the jlang core: every header whose tools/cppgen/jdkmap.tsv rows say "core",
// plus forward declarations of the other jlang classes (jlang/fwd.h).
//
// Every generated/translated header includes this one. Service headers (<jlang/Thread.h>,
// <jlang/IO.h>, <jlang/Sql.h>, ...) are included only where their types are used.
#pragma once

#include <jlang/fwd.h>

#include <jlang/Object.h>
#include <jlang/String.h>
#include <jlang/Exceptions.h>
#include <jlang/Boxes.h>
#include <jlang/Util.h>
#include <jlang/Runtime.h>
#include <jlang/System.h>

// Collections, arrays and friends (List, Map, Set, Array, Iterator, Comparator, Arrays,
// Collections, BitSet, Trove types, ArrayUtils).
#if __has_include(<jlang/Array.h>)
#include <jlang/Array.h>
#endif
#if __has_include(<jlang/Collections.h>)
#include <jlang/Collections.h>
#endif
