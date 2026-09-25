# jlang — the C++ runtime of the AionLightning port

`jlang` provides the JDK / third-party APIs used by the Java code base, with Java names and
Java semantics, for the C++20 translation (see `docs/cpp-port/CONVENTIONS.md`).

## Layout

```
jlang/include/jlang/jlang.h     umbrella: the core headers + fwd.h (included by all generated code)
jlang/include/jlang/fwd.h       forward declarations of every non-core jlang class (from jdkmap.tsv)
  Object.h      Object, Class (+ registry), cast/instanceof, JSYNC/MonitorGuard, Runnable
  String.h      String (nullable UTF-8 value type), str(), String::format, StringBuilder,
                StringCharacterIterator, StringUtils
  Exceptions.h  Throwable and the JDK exception hierarchy, JLANG_THROWABLE, detail::Pinned
  Boxes.h       Number, Integer, Long, Short, Byte, Float, Double, Boolean, Character, StringBox
  Util.h        JFINALLY, JASSERT, ushr/shl/shr, d2i/d2l/f2i/f2l, idiv/irem, Hash/Equal,
                hashCodeOf/equalsOf, box/unbox, EnumBox
  System.h      System (out/err, properties, time, exit), Runtime, Math/StrictMath, Random,
                PrintStream
  Runtime.h     jlang::init, jlang::gc (thread registration, stats), jlang::sync (interrupts)
  Array.h, Collections.h, ...   arrays and collections (core, collections part)
  Thread.h, IO.h, Nio.h, Sql.h, Xml.h, Log.h, ...   service headers (include where used)
jlang/src/*.cpp                 implementation (only place that includes gc.h, mariadb, pugixml,
                                openssl, zlib)
jlang/tests/*.cpp               unit tests, one executable `jlang_tests` (harness: jtest.h)
jlang/tests/java/JavaRef.java   generates tests/java_ref_data.inc (JDK reference outputs)
jlang/pch/jlang_pch.cpp         owner of the shared precompiled header (jlang_use_pch)
```

## Build and test

```
cmake -S . -B build -G Ninja                  # RelWithDebInfo by default
cmake --build build
ctest --test-dir build --output-on-failure    # or: build/jlang_tests [name-filter]
cmake -S . -B build-clang -G Ninja -DCMAKE_CXX_COMPILER=clang++
tools/port/check.sh [--clang] path/to/File.cpp|File.h   # compile one translation unit
```

Flags (every target, through jlang's PUBLIC usage requirements): `-std=c++20 -fwrapv
-fno-strict-aliasing -fnon-call-exceptions -pthread`, plus `-fno-delete-dead-exceptions` with GCC.
Options: `-DJLANG_ASSERTS=ON` (enable `JASSERT`), `-DAION_USE_PCH=OFF`.

Regenerate the JDK reference data after changing `JavaRef.java`:
`cd jlang/tests/java && javac -d /tmp/jr JavaRef.java && java -cp /tmp/jr JavaRef > ../java_ref_data.inc`

## Memory model

* Global `operator new`/`new[]` (all variants) allocate collectable, scanned, zeroed memory
  from the Boehm collector (`GC_MALLOC`); `operator delete` is a no-op. The collector is
  initialized by the first allocation; `jlang::init(argc, argv)` must still be the first
  statement of `main()` (fault handlers, properties).
* Java objects are raw pointers deriving (virtually) from `jlang::Object`. Unreachable objects,
  including cycles, are reclaimed; destructors of collected objects never run.
* Every thread touching GC pointers must be registered with the collector: use `jlang::Thread`,
  the executors, or `jlang::gc::startNativeThread` / `jlang::gc::ThreadRegistration`.
* GC pointers must not live only in memory the collector cannot see (malloc, `thread_local`,
  kernel buffers). Thrown exception objects live in malloc'ed memory: `Throwable` keeps its data
  in a pinned (uncollectable while outside the GC heap) cell; exception subclasses with extra
  GC-pointer/String fields use `jlang::detail::Pinned<T>`.
* Monitors (`JSYNC`, `wait`/`notify`) are created lazily per object.

## Runtime notes

* Null dereferences become `jlang::NullPointerException` and integer division by zero
  `jlang::ArithmeticException` through signal handlers. This needs `-fnon-call-exceptions`, which
  only GCC implements: with clang such faults terminate the process (clang builds are for
  compile checking). Faults inside libraries (libc, libstdc++) are not converted.
* `jlang::String` indices are UTF-8 code units (identical to Java for ASCII);
  `hashCode()`, `toCharArray()`, `getBytes()`, `compareTo()` are exact for any text.
* `Class` names come from the demangled C++ type (`a::b::C` → `a.b.C`, jlang types → `java.*`)
  until the generated class registry sets the exact Java metadata (`Class::registerClass`).
