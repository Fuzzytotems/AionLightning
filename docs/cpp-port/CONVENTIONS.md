# AionLightning Java → C++ port: conventions

This document is the single source of truth for how Java code in this repository
is translated to C++. Every translated file must follow it. When the rules here and
a generated header disagree, **the generated header wins** (it was produced
mechanically from javac's attributed AST and is what every other translation unit
compiles against). If a rule is missing or wrong, fix this document, don't improvise
per file.

The port is done in three layers:

1. **`jlang`**: a small C++20 runtime that provides the JDK/third-party APIs the code
   uses (strings, collections, concurrency, time, I/O, NIO sockets, JDBC-style SQL,
   log4j-style logging, XML/JAXB support, crypto…), with Java names and Java semantics.
   Translated code calls `jlang` wherever the Java called the JDK.
2. **`tools/cppgen`**: a javac-based generator that emits, for every Java type, the
   C++ header (complete declaration) and a `.cpp` skeleton (every method stubbed with
   its original Java body in a comment), plus mechanical code: enum tables, JAXB
   unmarshalling binders, `@Property` config binders, the class registry used in place of
   reflection and script loading, and CMake source lists.
3. **Body translation**: the Java method bodies in the `.cpp` skeletons are translated
   to C++ one file at a time by hand/agents, following this document, then compiled.

---------------------------------------------------------------------------------------

## 1. Toolchain and build

* C++20 (`-std=c++20`, **not** `gnu++20`, so `linux`/`unix` are not macros).
* GCC 13 is the primary compiler. Clang 18 must also compile the code (it is stricter
  about some things and catches `"literal" + int` pointer arithmetic).
* Required flags (set by CMake for every target):
  `-fwrapv` (Java integer overflow wraps), `-fno-strict-aliasing`,
  `-fnon-call-exceptions` (null dereferences become `jlang::NullPointerException`),
  `-pthread`.
* Dependencies: Boehm GC (`bdw-gc`, threads enabled), MariaDB Connector/C
  (`libmariadb`), pugixml, OpenSSL (`libcrypto`), zlib, PCRE2 (`libpcre2-8`, for
  java.util.regex). Only `jlang/src/*.cpp`
  includes their headers. Translated code includes `jlang` headers only.
* CMake + Ninja. Root `CMakeLists.txt`; one library target per module (`jlang`,
  `commons`, `geoengine`) and one executable per server (`loginserver`, `chatserver`,
  `gameserver`). Scripts are compiled into the gameserver executable.

## 2. Memory model: garbage collected, like Java

The whole program runs on the Boehm conservative garbage collector.

* The global `operator new`/`new[]` allocate from the GC heap (`GC_MALLOC`).
  `operator delete` is a no-op. **Never write `delete`** in translated code.
  Objects die when unreachable, exactly as in Java. Cycles are fine.
* Java objects are held by **raw pointers** (`Player*`). There is no `shared_ptr`,
  `unique_ptr` or ownership reasoning anywhere in translated code.
* All threads must be `jlang::Thread`s (or threads started by `jlang` executors),
  which are registered with the collector. **Never** use `std::thread`, `std::async`,
  `std::jthread`, or raw `pthread_create`.
* Never keep a GC pointer only in memory the collector cannot see: `malloc`ed
  memory, `thread_local` variables, OS handles or kernel buffers.
* Destructors are never run for collected objects. Release OS resources (files,
  sockets, DB connections) explicitly, as the Java code already does with
  `close()`.
* `jlang::Object` is the root of every translated class (Java `java.lang.Object`).

## 3. Files, namespaces, names

### 3.1 Layout

```
CMakeLists.txt
jlang/include/jlang/*.h          runtime headers, included as <jlang/X.h>
jlang/src/*.cpp                  runtime implementation
jlang/tests/                     runtime unit tests
commons/src/org/openaion/commons/**/X.{h,cpp}      next to X.java
loginserver/src/org/openaion/loginserver/**/X.{h,cpp}
chatserver/src/com/aionengine/chatserver/**/X.{h,cpp}
gameserver/src/org/openaion/gameserver/**/X.{h,cpp}
gameserver/data/scripts/system/handlers/**/X.{h,cpp}   quest/admin/user command scripts
gameserver/data/scripts/system/database/**/X.{h,cpp}   mysql5 DAO scripts
geoengine/src/aionjHungary/geoEngine/**/X.{h,cpp}      port of the binary geoEngine jar
tools/cppgen/                    the generator (Java)
```

Include roots are the Java source roots (`commons/src`, `loginserver/src`,
`gameserver/src`, `gameserver/data/scripts/system/handlers`,
`gameserver/data/scripts/system/database`, `chatserver/src`, `geoengine/src`) plus
`jlang/include`. Includes are always written from a root:
`#include "org/openaion/gameserver/model/Race.h"`, `#include <jlang/String.h>`.

### 3.2 Namespaces

Java package `a.b.c` ↔ C++ namespace `a::b::c`, one to one, including script
packages (`quest.sanctum` → `quest::sanctum`, `mysql5` → `mysql5`).
The runtime lives in `namespace jlang`.

* Headers: never `using namespace`; refer to other types fully qualified
  (the generator does this).
* `.cpp` files: `using namespace jlang;` is allowed. Java imports become
  **using-declarations** (`using org::openaion::gameserver::model::Race;`), never
  using-directives for codebase namespaces. The generator emits them.

### 3.3 Types and files

* A top-level Java type `a.b.C` → C++ `a::b::C`, declared in `a/b/C.h`, defined in
  `a/b/C.cpp`.
* A **nested** Java type (static nested class, inner class, nested enum, nested
  interface) `a.b.C.D` is hoisted to namespace scope as `a::b::C_D`, declared in
  `a/b/C_D.h` / `a/b/C_D.cpp`. Deeper nesting: `C_D_E`. The outer class gets
  `using D = C_D;` so both `C::D` and `C_D` work.
  The outer and nested classes are mutual `friend`s (Java lets them see each other's
  private members).
* **Inner (non-static) classes** get an explicit outer pointer: a member
  `Outer* this_0` and an extra **first** constructor parameter `Outer* this_0`.
  Java `new Inner(a)` in `Outer` → `new Outer_Inner(this, a)`;
  `outer.new Inner(a)` → `new Outer_Inner(outer, a)`; `Outer.this.f` and
  implicit outer member access → `this_0->f`.
* **Anonymous and local classes** are not hoisted by the generator. Translate them
  in the `.cpp`:
  - For single-abstract-method interfaces use the lambda adapter the header provides:
    `jlang::Runnable::of([=, this]() { ... })`, `ObjectFilter::of(...)`,
    `jlang::Comparator<T>::of(...)`. Every SAM interface in the codebase gets a
    generated `template<class F> static I* of(F f)`.
  - Otherwise write a named class in an anonymous namespace at the top of the `.cpp`,
    named after javac's binary name (`Outer$1` → `Outer_1`, `Outer$1Local` →
    `Outer_1Local`). Captured locals and the outer `this` become constructor
    parameters/members (`Outer* this_0`).
  - Java captures are by value (effectively final) → lambdas capture `[=, this]`
    (never `[&]` for anything that outlives the call, such as scheduled tasks).

### 3.4 Identifier collisions

Java identifiers that are C++ keywords, or macros from the standard/system headers,
get a trailing underscore **everywhere** (declaration and every use):
`and and_eq asm auto bitand bitor bool char8_t char16_t char32_t compl concept
consteval constexpr constinit const_cast co_await co_return co_yield decltype delete
dynamic_cast explicit export extern friend inline mutable namespace noexcept not
not_eq nullptr operator or or_eq register reinterpret_cast requires signed sizeof
static_assert static_cast struct template thread_local typedef typeid typename union
unsigned using virtual wchar_t xor xor_eq NULL EOF errno assert stdin stdout stderr
unix linux major minor DOMAIN INFINITY NAN BUFSIZ` (the authoritative list is
`tools/cppgen/.../Names.java`). Example: a field named `delete` → `delete_`.

Java allows a field and a method with the same name in one class; C++ does not.
The **field** is renamed with a trailing underscore (`level` → `level_`) and every
access to it (also from other classes) uses the new name. The header shows it.

## 4. Type mapping

| Java | C++ |
|---|---|
| `boolean` `byte` `short` `char` `int` `long` `float` `double` | `bool` `int8_t` `int16_t` `char16_t` `int32_t` `int64_t` `float` `double` |
| `String` | `jlang::String` (value type, nullable, see §8) |
| `Object` | `jlang::Object*` |
| codebase or JDK **class/interface** `C` | `C*` (pointer) |
| **enum** `E` (codebase) | `E` (value class, see §7) |
| JDK enum (`TimeUnit`, `ByteOrder`…) | the `jlang` value class of the same name |
| `Integer` etc. in a **non-generic** position (field, param, return, local) | `std::optional<int32_t>` etc. (null = `std::nullopt`) |
| `Integer` etc. as a **type argument** of a JDK generic | the primitive (`jlang::List<int32_t>*`) |
| `Integer` etc. **as an Object** (stored in `Object`, `Object...`) | boxed object `jlang::Integer*` via `jlang::box(x)` |
| `T[]` | `jlang::Array<T'>*` where `T'` is the mapped element type |
| JDK collections (§9) | pointer to `jlang` collection, e.g. `jlang::List<Player*>*` |
| `Map.Entry<K,V>` | `jlang::Entry<K,V>` (value) |
| codebase generic type `G<X>`, `G<?>`, raw `G` | `G*` (**erased**, §6) |
| type variable `T` of a codebase generic | its erasure: bound's mapping (`T extends Creature` → `Creature*`), else `jlang::Object*` |
| wildcard `? extends B` / `? super B` / `?` in a JDK generic | `B'` / `B'` / `jlang::Object*` |
| `Class<?>` | `jlang::Class*` |
| `java.util.regex.Pattern` | `jlang::Pattern*` |

Type arguments of `jlang` templates are always full mapped C++ types, including the
`*`: `jlang::Map<int32_t, jlang::List<Item*>*>*`.

## 5. Classes

### 5.1 Inheritance

```cpp
class Foo : public Base, public virtual Iface1, public virtual Iface2 { ... };  // Java class
class Top : public virtual jlang::Object, public virtual Iface { ... };           // no superclass
class Iface : public virtual jlang::Object, public virtual SuperIface { ... };    // Java interface
```

* Superclass: `public` non-virtual. Interfaces and `jlang::Object`: `public virtual`.
  There is exactly one `jlang::Object` subobject per object.
* Casts and `instanceof` always go through `dynamic_cast` wrappers:
  `x instanceof T` → `jlang::instanceof<T>(x)`; `(T) x` → `jlang::cast<T>(x)`
  (throws `jlang::ClassCastException` like Java; null stays null). Primitive casts
  are `static_cast`.
* Access: `private` members stay `private`. Everything else (`public`, `protected`,
  package-private) becomes `public`, because C++ has no package access.
* Java `final` classes/methods → C++ `final` where it is legal.

### 5.2 Methods

* Non-private, non-static instance methods are `virtual`; overrides are marked
  `override`. Abstract methods are `= 0`. Private methods are non-virtual.
* If a subclass declares a method name that also has other overloads in a superclass,
  the generator adds `using Base::name;` (C++ would otherwise hide them).
* If a class implements an interface method with an implementation inherited
  from its superclass, the generator emits a forwarding override.
* **Covariant families.** When some override narrows a method's return type
  (explicitly, or because a subclass binds an erased type variable), the family is
  emitted as:
  ```cpp
  // root class
  virtual CreatureController* getController_impl();          // the Java body
  CreatureController* getController() { return getController_impl(); }
  // subclass that narrows
  CreatureController* getController_impl() override;        // Java body (if overridden)
  PlayerController* getController();                         // non-virtual, defined in .cpp
  ```
  Callers always call the Java name (`getController()`). A Java `super.m()` call on
  such a family must call `Base::m_impl()` directly, never `Base::m()`.
  The generated header marks these families with a comment.
* Varargs `T... xs` → parameter `jlang::Array<T'>* xs`. The generator also emits a
  variadic forwarding overload so call sites can pass the elements directly, as in
  Java: `new SM_SYSTEM_MESSAGE(1300000, jlang::box(name), jlang::box(5))`. For
  `Object...` wrap each argument with `jlang::box(...)`.
* `const` is not used on methods or objects (Java has no const). The only `const`s are
  constants (§5.4).
* Checked-exception `throws` clauses are dropped.

### 5.3 Constructors and fields

* Every field gets a default member initializer with the Java default value in the
  header (`= 0`, `= false`, `= nullptr`, null `String`, null enum, `std::nullopt`).
  Simple Java field initializers (literals, constants, `null`, empty `jlang`
  collections, `new jlang::Array<T>(n)` with constant n) are emitted by the generator
  as default member initializers. Anything else goes into
  `void _init_fields();`, called at the start of every constructor body that does
  not delegate to `this(...)`. This keeps Java's order: superclass constructor,
  then field initializers and instance initializer blocks, then constructor body.
* Java `super(args)` → base initializer `: Base(args)`. Java `this(args)` →
  delegating constructor `: Foo(args)`.
* If a Java class has no constructor, the generator emits the implicit one.
* **Virtual calls in constructors differ.** In Java a constructor calling an
  overridable method dispatches to the subclass; in C++ it calls the current class's
  version. The same holds for `getClass()`. The generator marks such constructors
  with `// WARNING: virtual call in constructor` and the translator must preserve the
  Java behavior (usually by moving the call out of the constructor or computing the
  value lazily).
* Java `volatile` fields → `std::atomic<T>` (reads and writes keep Java syntax).
* Java `final` instance fields are **not** `const`.

### 5.4 Static members

* `static final` compile-time constants (javac constant values) → `static constexpr`
  in the header; `String` constants → `static inline const jlang::String X = "...";`.
* Other static fields → `static inline` in the header with the Java default value.
  The generator translates simple initializers there directly: literals, `null`,
  empty `jlang` collections, `jlang::Logger::getLogger(...)`. Anything else is
  initialized in the `.cpp` inside the generated `static void _clinit();`.
* Java `static { ... }` blocks go into `_clinit()` as well. The generator emits
  `_clinit()` and a registration `static const bool _clinit_done = (X::_clinit(), true);`
  in the `.cpp` (dynamic initialization before `main`).
  **Do not** construct heavyweight objects, read config, or touch other classes'
  non-constant statics in `_clinit()`. Make them lazy instead.
* **Singletons.** The Java `SingletonHolder` idiom → a function-local static inside
  `getInstance()`:
  `static Foo* getInstance() { static Foo* instance = new Foo(); return instance; }`.
  It is lazy and thread-safe, like the Java holder class. The generated
  `Foo_SingletonHolder` class is then left empty or deleted.

## 6. Generics of the codebase: erasure

Generic classes and interfaces declared in this repository are **not** C++ templates.
They are erased the way javac erases them: each type variable becomes its bound, so
`CreatureGameStats<T extends Creature>` becomes class `CreatureGameStats` using
`Creature*`, and `ObjectFilter<T>` becomes `ObjectFilter` with `jlang::Object*`.
All parameterizations (`CreatureGameStats<Player>`, `CreatureGameStats<?>`) are the
same C++ type.

* Where javac inserts a checkcast (e.g. `PlayerGameStats.getOwner()` used as `Player`),
  the translator inserts `jlang::cast<Player>(...)` (or `static_cast` when the type is
  certain and complete).
* Subclasses that bind a type variable (`class PlayerController extends
  CreatureController<Player>`) get narrowed non-virtual accessors for methods
  returning the variable (see covariant families, §5.2).
* An override whose Java parameter type is a binding of a type variable (e.g.
  `acceptObject(Player p)` overriding `acceptObject(T)`) keeps the **erased**
  parameter type in C++, and the body starts with the cast, which the generator
  inserts:
  `bool acceptObject(jlang::Object* p_) override { Player* p = jlang::cast<Player>(p_); ...`
* Generic **methods** (`<T> T foo(Class<T>)`) are erased too, unless they are
  static/private/final and clearly need a template. The header shows which.

JDK generics (collections, `Comparable<T>`, `Comparator<T>`, `Callable<V>`,
`Future<V>`…) **are** templates in `jlang`.

## 7. Enums

A Java enum `E` becomes a value class with the same name (JDK enums likewise live in
`jlang`):

```cpp
class Race final {
public:
    enum class Value : int32_t { _NULL = -1, ELYOS, ASMODIANS, /*...*/ };
    static const Race ELYOS, ASMODIANS /*...*/;       // constexpr constants of type Race
    constexpr Race() = default;                        // null
    constexpr Race(std::nullptr_t);                    // null
    constexpr operator Value() const;                  // enables switch/case
    int32_t ordinal() const; jlang::String name() const; jlang::String toString() const;
    static jlang::Array<Race>* values(); static Race valueOf(const jlang::String&);
    int32_t compareTo(Race o) const; bool equals(Race o) const; int32_t hashCode() const;
    // Java members (fields live in a per-constant table in the .cpp):
    int32_t getRaceId();
    ...
};
```

* Use: `Race r = Race::ELYOS;` `auto r = Race::ELYOS;` (type `Race`).
  `r == Race::ELYOS`, `r == nullptr`, `r.getRaceId()` (**dot**, it is a value),
  `switch (r) { case Race::ELYOS: ... }`, `Race::values()`, `r.name()`, `r.ordinal()`.
* Hash/equality work out of the box for `jlang` maps/sets. `EnumMap<E,V>` →
  `jlang::TreeMap<E,V>*` (ordinal order, like Java). `EnumSet<E>` → `jlang::TreeSet<E>*`.
* Per-constant data (Java constructor arguments and fields) is stored in a static table
  in the `.cpp` indexed by ordinal. The generator fills it when the constructor
  arguments are literals or constants. Otherwise it leaves a TODO with the Java text.
  Mutable enum fields are kept in the table (non-const).
* Constant-specific class bodies (`A { @Override int f() {...} }`) → the method
  switches on `*this`.
* `@XmlEnumValue` → generated `static E fromXml(const jlang::String&)` and
  `jlang::String toXml() const`.
* An enum that implements an interface → the generator emits the interface methods
  as members; where a pointer to the interface is needed, use `jlang::box(e)`
  (a boxed `jlang::EnumBox<E>` that implements the interface by forwarding).

## 8. Strings

`jlang::String` derives from `std::string` (UTF-8) and has Java's methods:
`length() equals() equalsIgnoreCase() startsWith() endsWith() indexOf() lastIndexOf()
substring(b[,e]) charAt() toLowerCase() toUpperCase() trim() split(regex) replace()
replaceAll() contains() isEmpty() compareTo() compareToIgnoreCase() hashCode()
matches() format(...) valueOf(...) toCharArray() getBytes(charset)`, and more.

* **Nullable.** A default-constructed `jlang::String` is **null** (Java field default).
  `s == nullptr` / `s != nullptr` test it, `s = nullptr` assigns null. `""` is a
  non-null empty string. Calling methods on a null string behaves like an empty string
  (no NPE). Keep Java's null checks as written.
* **Concatenation.** Java `"a" + x + y` → `jlang::str("a", x, y)` or
  `jlang::String("a") + x + y`. **Never** write `"literal" + number`: that is
  pointer arithmetic in C++. `operator+`/`+=` on `jlang::String` accept `String`,
  `const char*`, all arithmetic types (formatted like Java: `1.0f` → `"1.0"`),
  `char16_t` (appended as a character), `bool` (`"true"`), enums (name),
  `jlang::Object*` (`toString()` or `"null"`).
* `char` literals → `u'x'` (`char16_t`). Strings are UTF-8 internally; packet
  `writeS`/`readS` convert to and from UTF-16LE.
* `String.format(fmt, args...)` → `jlang::String::format(fmt, args...)`
  (Java format syntax).
* `StringBuilder`/`StringBuffer` → `jlang::StringBuilder*` (Java API; `append`
  returns `this`).
* Java `==` between strings (reference equality) → use `.equals()` semantics
  unless comparing with `null`.

## 9. Collections

All collections are GC objects held by pointer, created with `new`, with Java method
names (`add get set remove size isEmpty contains clear addAll indexOf iterator
put get containsKey containsValue remove keySet values entrySet putAll ...`).
They also expose STL-style iteration.

| Java (declared or constructed type) | C++ |
|---|---|
| `List`, `ArrayList`, `LinkedList` used as list, `Vector`, `FastList`, `CopyOnWriteArrayList`, `Collection`, `Iterable`, `AbstractList` | `jlang::List<E>*` |
| `Set`, `HashSet`, `LinkedHashSet`, `FastSet` | `jlang::Set<E>*` (insertion ordered) |
| `TreeSet`, `SortedSet`, `EnumSet` | `jlang::TreeSet<E>*` |
| `Map`, `HashMap`, `LinkedHashMap`, `FastMap`, `THashMap`, `Hashtable`, `ConcurrentHashMap`, `ConcurrentMap`, `IdentityHashMap` | `jlang::Map<K,V>*` (insertion ordered) |
| `TreeMap`, `SortedMap`, `NavigableMap`, `EnumMap` | `jlang::TreeMap<K,V>*` |
| `Queue`, `Deque`, `ArrayDeque`, `LinkedList` used as queue, `ConcurrentLinkedQueue`, `LinkedBlockingQueue`, `BlockingQueue` | `jlang::Deque<E>*` |
| `PriorityQueue` | `jlang::PriorityQueue<E>*` |
| `Iterator<E>` / `ListIterator<E>` | `jlang::Iterator<E>*` |
| `TIntObjectHashMap<V>`, `TIntIntHashMap`, `TIntArrayList`, `THashSet` | `jlang::TIntObjectHashMap<V>*` etc. (subclasses exposing the Trove method names) |

* **Thread safety.** Java concurrent/synchronized collections map to the same class
  with its internal lock enabled: `new ConcurrentHashMap<>()` →
  `new jlang::ConcurrentHashMap<K,V>()` (a `jlang::Map` subclass that only enables the
  lock), `new FastMap<K,V>().shared()` → `(new jlang::Map<K,V>())->shared()`,
  `Collections.synchronizedList(l)` → `jlang::Collections::synchronizedList(l)`.
* **Iteration is robust.** `for (X x : list)` → `for (X* x : *list)`. List iteration
  is index based: it never crashes when the list is modified during the loop, where
  Java would throw `ConcurrentModificationException`. Iterating a locked collection
  iterates a snapshot. Map and set views (`keySet() values() entrySet()`) return
  **snapshots** (`jlang::List<...>*`). To remove while iterating, use
  `it = coll->iterator(); while (it->hasNext()) { ... it->remove(); }`, which works on
  every `jlang` collection.
* `map->get(k)` returns the mapped value or the Java default (`nullptr` for
  pointers, `0` for numbers, null `String`). Java code that relies on
  `Integer v = map.get(k); if (v == null)` must use `containsKey` or
  `map->getOptional(k)`.
* `for (Map.Entry<K,V> e : m.entrySet())` →
  `for (auto& e : *m->entrySet()) { e.getKey(); e.getValue(); }`.
* Hashing/equality: arithmetic, `jlang::String` and enums hash by value. Pointers to
  `jlang::Object` use the virtual `hashCode()`/`equals()`, which default to identity,
  as in Java.
* `Collections.*`, `Arrays.*` → `jlang::Collections::*`, `jlang::Arrays::*`.
  `unmodifiableX(c)` returns `c` itself.

## 10. Arrays

`jlang::Array<T>` is a fixed-length GC object: `arr->length`, `(*arr)[i]`
(bounds-checked, throws `ArrayIndexOutOfBoundsException`), `arr->begin()/end()`,
`arr->data()`.

* `new int[n]` → `new jlang::Array<int32_t>(n)` (zero-initialized, like Java).
* `new int[]{1,2}` / `{1,2}` initializers → `jlang::Array<int32_t>::of({1, 2})`.
* `new int[a][b]` → `jlang::Array<int32_t>::newMatrix(a, b)`
  (returns `jlang::Array<jlang::Array<int32_t>*>*`).
* `System.arraycopy`, `Arrays.fill/copyOf/sort/asList/equals` → `jlang` equivalents.
* `for (int v : arr)` → `for (int32_t v : *arr)`.

## 11. Expressions and statements

* Member access: pointer types use `->` (`player->getName()`); value types (`String`,
  enums, `std::optional`, `jlang::Entry`) use `.`; statics use `::`
  (`World::getInstance()`, `Race::ELYOS`, `GSConfig::SERVER_NAME`).
* `null` → `nullptr`.
* `>>>` → `jlang::ushr(x, n)` (int and long overloads). `>>` and `<<` as written. If
  the shift count can be ≥ the width, mask it (`n & 31`) like Java does.
* Integer `/` and `%` behave like Java. With `-fwrapv`, overflow wraps like Java.
  Division by zero crashes in C++; guard it where the Java code relied on
  `ArithmeticException`.
* Casting float/double to int: `static_cast<int32_t>(x)`. Where the value can be NaN
  or out of range, use `jlang::d2i(x)`/`jlang::d2l(x)`, which use Java's saturating
  semantics.
* Java compound assignments narrow implicitly (`byteVar += 5`); the same is true in
  C++. Char arithmetic works on `char16_t`.
* `x.equals(y)` on objects → `x->equals(y)`; on strings → `x.equals(y)`;
  on enums/numbers → `==`.
* Switch on strings → `if/else if` chain with `.equals()`.
* Labeled `break`/`continue` → `goto` to a label placed right after the loop, or
  inside the loop body before its closing brace.
* Enhanced `for` over arrays and collections, see §9 and §10.
* `synchronized (x) { ... }` → `JSYNC(x) { ... }` (a reentrant monitor on any
  `jlang::Object`). A `synchronized` method wraps its body in `JSYNC(this) { ... }`;
  a `static synchronized` method uses `JSYNC(jlang::Class::of<Foo>())`.
  `x.wait()`, `x.notify()`, `x.notifyAll()` → `x->wait()`, `x->notify()`,
  `x->notifyAll()`.
* `assert` → `JASSERT(cond)` (disabled unless `-DJLANG_ASSERTS`).
* `X.class` → `jlang::Class::of<X>()`; `obj.getClass()` → `obj->getClass()`;
  `getClass().getSimpleName()` → `obj->getClass()->getSimpleName()`.
* `System.currentTimeMillis()` → `jlang::System::currentTimeMillis()`, and likewise
  for the rest of `java.lang` (`Math`, `Integer.parseInt`, `Thread.sleep`…).
* **Evaluation order.** Java evaluates operands and arguments strictly left to right.
  C++ does not for function arguments or most binary operators. Whenever two or more
  sub-expressions have side effects that interact (the classic case is packet reads:
  `new Foo(readD(), readS(), readC())`, `readH() + readH()`), hoist them into locals
  **in Java order** first:
  `auto a = readD(); auto b = readS(); auto c = readC(); new Foo(a, b, c);`.
  (Braced initializer lists are ordered in C++ too, but use locals anyway for clarity.)
* **Integer literals.** Java `int` hex/octal literals above `0x7FFFFFFF` are negative
  ints; in C++ they are unsigned. Write them as `static_cast<int32_t>(0x85A308D3u)`
  (or declare such tables `uint32_t` if every use is bit-level). Long literals `123L`
  → `INT64_C(123)`; `0xFF000000` used as an int in a long expression must stay a
  **sign-extended** int: `static_cast<int64_t>(static_cast<int32_t>(0xFF000000u))`.
  Float literals `1.5f` stay; `1.5` is double as in Java.
* Byte arithmetic: Java `byte` is signed. `b & 0xFF` works the same in C++ with
  `int8_t` (promotion to int sign-extends, as in Java).
* `hashCode()` default is an identity hash (stable per object, 32-bit), like the JVM's.

## 12. Exceptions

* `jlang::Throwable` derives from `std::exception` and `jlang::Object`, with Java's
  hierarchy below it (`Exception`, `RuntimeException`, `NullPointerException`,
  `IllegalArgumentException`, `IOException`, `SQLException`…).
  Codebase exception classes derive from these.
* Throw by value, catch by reference:
  `throw new IllegalStateException("x")` → `throw jlang::IllegalStateException("x");`
  `catch (Exception e)` → `catch (jlang::Exception& e)`,
  `catch (Throwable t)` → `catch (jlang::Throwable& t)`.
  `e.getMessage()` → `e.getMessage()`. Logging a throwable: `log->error(msg, e)`.
* Null dereferences raise `jlang::NullPointerException` (through a SIGSEGV handler
  and `-fnon-call-exceptions`), so Java code that catches `Exception` around failing
  code keeps working.
* `finally` → scope guard at the start of the protected block:
  ```cpp
  { JFINALLY { DatabaseFactory::close(con); };
    ...try body...
  }
  ```
  Combined with catches: `try { JFINALLY {...}; body } catch (jlang::SQLException& e) {...}`
  only if Java's order allows it. When the finally block must run **after** the catch
  block, as in Java, put the guard in an outer block:
  `{ JFINALLY {...}; try { body } catch (...) { handler } }`.
  A finally block must not throw.
* `throws` clauses are dropped.

## 13. Reflection, annotations, scripts, configuration, JAXB

Nothing uses runtime reflection. The generator emits the metadata instead:

* **Class registry.** Every named class registers a `jlang::Class` with name, simple
  name, superclass, interfaces, abstract/interface flags, annotations (name → string
  values), and a factory for the public no-arg constructor. `jlang::Class::forName`,
  `cls->newInstance()`, `cls->isAssignableFrom()`, `cls->getAnnotation("QuestHandler")`
  work on it.
* **Scripts** (`gameserver/data/scripts`) are compiled into the binary. Their classes
  are registered with the script root directory they came from, so the C++ script
  loader can honor `handlers.xml`, `database.xml` and `quest_handlers.xml` and run the
  same class-listener logic as the Java loader (instantiate non-abstract classes of a
  given base type and register them).
* **`@Property` configuration.** For every class with `@Property` fields the generator
  emits `static void _configure(jlang::Properties* props)`. It applies the key, the
  default value and the type conversion the Java `PropertyTransformer`s would.
  `ConfigurableProcessor.process(Foo.class, props)` becomes a registry call.
* **JAXB.** For every JAXB-bound class the generator emits
  `void _jaxbUnmarshal(jlang::xml::Element* e, jlang::xml::JaxbContext* ctx)`
  (attributes, elements, lists, `@XmlElements` polymorphism, `@XmlIDREF`, enum values,
  defaults, and `afterUnmarshal` callbacks), plus `_jaxbMarshal` for marshalled classes.
  The exact generated-member contract is in `docs/cpp-port/JAXB.md`. Call sites keep the
  Java shape: `JAXBContext.newInstance(StaticData.class)` →
  `jlang::xml::JAXBContext::newInstance<StaticData>()`, and a cast of the unmarshal result
  `(NpcData) un.unmarshal(f)` → `un->unmarshal<NpcData>(f)`. Hand-written code never
  parses XML for JAXB classes.

## 14. Logging

`org.apache.log4j.Logger` → `jlang::Logger*`:
`private static final Logger log = Logger.getLogger(Foo.class);` →
`static inline jlang::Logger* log = jlang::Logger::getLogger("org.openaion...Foo");`.
Methods: `debug info warn error fatal (msg[, throwable])`, `isDebugEnabled()`,
`isInfoEnabled()`. Configured from `config/log4j.xml` (console and file appenders,
logger levels).

## 15. Translation checklist (per file)

1. Read the Java file and the generated `.h`/`.cpp` skeleton. **Do not change the
   public API in the header** (signatures, fields, bases). You may add private helper
   methods to your own class, and fix a mistake in your own header only if it is
   clearly wrong. Record why in a `// PORT:` comment.
2. Replace every `// TODO(port)` stub body with the C++ translation of the Java body
   shown next to it. Keep comments. Keep the structure and names, so the Java and C++
   can be compared line by line.
3. Add `#include`s for every codebase class whose members you use (the skeleton
   already includes the ones javac saw).
4. Compile the file (`tools/port/check.sh path/to/X.cpp`) with GCC, and with Clang.
   No errors. Fix warnings that point at real problems.
5. Re-read your translation against the Java for semantic differences:
   string concatenation, integer/float division, `>>>`, map `get` returning null,
   virtual calls in constructors, `==` on strings, iteration while modifying,
   `finally`, `super.m()` on covariant families, char vs int overloads.
