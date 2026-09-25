// jlang/System.h - java.lang.System, java.lang.Runtime, java.lang.Math (and StrictMath),
// java.util.Random and a minimal java.io.PrintStream (System.out / System.err).
//
// Part of the jlang core: translated code includes <jlang/jlang.h>.
#pragma once

#include <jlang/Exceptions.h>
#include <jlang/Object.h>
#include <jlang/Runtime.h>
#include <jlang/String.h>

#if __has_include(<jlang/Array.h>)
#include <jlang/Array.h>
#define JLANG_HAVE_ARRAY_H 1
#endif

#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <limits>
#include <type_traits>
#include <vector>

namespace jlang {

class Thread;

// ---------------------------------------------------------------------------------------
// java.io.PrintStream (minimal: System.out, System.err, new PrintStream(fileName)).
// print/println accept anything jlang::String concatenation accepts; printf/format use
// jlang::String::format. Output is UTF-8. Methods lock the stream's monitor like Java's
// synchronized PrintStream. Subclasses (the java.io layer) override writeBytes/flush/close.
class PrintStream : public virtual Object {
public:
    // Opens (truncates) a file. FileNotFoundException on failure.
    explicit PrintStream(const String& fileName);
    // Wraps a stdio stream (not closed by close() unless ownsFile).
    PrintStream(std::FILE* file, bool autoFlush, bool ownsFile = false);

    template<class T>
        requires detail::Concatenable<T>
    void print(const T& v) {
        std::string s;
        detail::appendValue(s, v);
        writeString(s, false);
    }
    void println() { writeString(std::string(), true); }
    template<class T>
        requires detail::Concatenable<T>
    void println(const T& v) {
        std::string s;
        detail::appendValue(s, v);
        writeString(s, true);
    }
    // print(char[]) / println(char[]): the characters (not "[C@hash")
    void print(Array<char16_t>* chars);
    void println(Array<char16_t>* chars);
    template<class... A>
    PrintStream* printf(const String& fmt, const A&... args) {
        writeString(String::format(fmt, args...), false);
        return this;
    }
    template<class... A>
    PrintStream* format(const String& fmt, const A&... args) {
        return printf(fmt, args...);
    }
    PrintStream* append(const String& s) {
        writeString(s, false);
        return this;
    }
    PrintStream* append(char16_t c) {
        print(c);
        return this;
    }
    // write(int b): one byte.
    virtual void write(int32_t b);
    virtual void flush();
    virtual void close();
    bool checkError() { return error_; }

protected:
    PrintStream() {}
    // Raw output of UTF-8 bytes (called with the monitor held).
    virtual void writeBytes(const char* data, size_t n);
    void setError() { error_ = true; }

private:
    void writeString(const std::string& s, bool newline);
    std::FILE* file_ = nullptr;
    bool autoFlush_ = false;
    bool owns_ = false;
    bool error_ = false;
};

namespace detail {
PrintStream* stdoutStream();
PrintStream* stderrStream();
}  // namespace detail

// ---------------------------------------------------------------------------------------
class System {
public:
    System() = delete;

    static inline PrintStream* out = detail::stdoutStream();
    static inline PrintStream* err = detail::stderrStream();
    static void setOut(PrintStream* s) { out = s; }
    static void setErr(PrintStream* s) { err = s; }

    static int64_t currentTimeMillis() noexcept;
    static int64_t nanoTime() noexcept;

#ifdef JLANG_HAVE_ARRAY_H
    // System.arraycopy: see jlang::arraycopy in <jlang/Array.h>.
    template<class S, class D>
    static void arraycopy(Array<S>* src, int32_t srcPos, Array<D>* dest, int32_t destPos, int32_t length) {
        ::jlang::arraycopy(src, srcPos, dest, destPos, length);
    }
#endif

    // Runs the shutdown hooks (Runtime.addShutdownHook) once, flushes stdio and terminates
    // the process without running C++ static destructors (other threads keep running until
    // then, as in Java).
    [[noreturn]] static void exit(int32_t status);
    static void gc();
    static void runFinalization() {}
    static int32_t identityHashCode(Object* o) noexcept { return ::jlang::identityHashCode(o); }

    // Properties: user.dir, user.home, user.name, os.name, os.arch, os.version,
    // java.version ("cpp"), java.vendor, java.vm.name, java.io.tmpdir, file.encoding,
    // line.separator, file.separator, path.separator, plus -Dkey=value arguments given to
    // jlang::init and setProperty.
    static String getProperty(const String& key);
    static String getProperty(const String& key, const String& def);
    static String setProperty(const String& key, const String& value);  // returns the old value
    static String clearProperty(const String& key);
    static String getenv(const String& name);  // null if unset
    static String lineSeparator() { return String("\n"); }
};

// ---------------------------------------------------------------------------------------
// java.lang.Runtime
class Runtime final : public virtual Object {
public:
    static Runtime* getRuntime();

    int32_t availableProcessors();
    int64_t totalMemory();  // current collector heap size
    int64_t freeMemory();   // free bytes in the heap
    int64_t maxMemory();    // see jlang::gc::maxHeapSize
    void gc();
    void runFinalization() {}
    [[noreturn]] void exit(int32_t status) { System::exit(status); }
    // halt: terminate immediately (no hooks, no flushing).
    [[noreturn]] void halt(int32_t status);

    // Shutdown hooks run once, sequentially in registration order, on the thread that calls
    // System.exit (the JVM starts them as concurrent threads). A jlang::Thread is a Runnable
    // (its run() is called). IllegalArgumentException if already registered,
    // IllegalStateException once shutdown has begun.
    void addShutdownHook(Runnable* hook);
    bool removeShutdownHook(Runnable* hook);
    // java.lang.Thread hooks (usable with an incomplete jlang::Thread; defined with
    // <jlang/Thread.h>): the thread's run() is called.
    void addShutdownHook(Thread* hook);
    bool removeShutdownHook(Thread* hook);
    // Runs the hooks now if they have not run yet (used by System.exit; exposed for servers
    // that terminate through other paths).
    void runShutdownHooks();

private:
    Runtime() {}
};

// ---------------------------------------------------------------------------------------
// java.lang.Math / StrictMath (Java semantics for NaN, -0.0, rounding and overflow).
class Math {
public:
    Math() = delete;
    static constexpr double PI = 3.141592653589793;
    static constexpr double E = 2.718281828459045;

    static int32_t abs(int32_t a) noexcept { return a < 0 ? -a : a; }  // abs(MIN_VALUE) == MIN_VALUE
    static int64_t abs(int64_t a) noexcept { return a < 0 ? -a : a; }
    static float abs(float a) noexcept { return std::fabs(a); }
    static double abs(double a) noexcept { return std::fabs(a); }

    static int32_t max(int32_t a, int32_t b) noexcept { return a >= b ? a : b; }
    static int64_t max(int64_t a, int64_t b) noexcept { return a >= b ? a : b; }
    static float max(float a, float b) noexcept;
    static double max(double a, double b) noexcept;
    static int32_t min(int32_t a, int32_t b) noexcept { return a <= b ? a : b; }
    static int64_t min(int64_t a, int64_t b) noexcept { return a <= b ? a : b; }
    static float min(float a, float b) noexcept;
    static double min(double a, double b) noexcept;
    // Mixed argument types promote like Java (int+long -> long, int+float -> float, ...).
    template<class A, class B>
        requires(std::is_arithmetic_v<A> && std::is_arithmetic_v<B> && !std::is_same_v<A, B>)
    static auto max(A a, B b) noexcept {
        using C = decltype(a + b);
        return max(static_cast<C>(a), static_cast<C>(b));
    }
    template<class A, class B>
        requires(std::is_arithmetic_v<A> && std::is_arithmetic_v<B> && !std::is_same_v<A, B>)
    static auto min(A a, B b) noexcept {
        using C = decltype(a + b);
        return min(static_cast<C>(a), static_cast<C>(b));
    }
    // int16_t/int8_t/char16_t arguments promote to int like in Java.
    template<class A>
        requires(std::is_integral_v<A> && sizeof(A) < 4)
    static int32_t abs(A a) noexcept { return abs(static_cast<int32_t>(a)); }
    template<class A>
        requires(std::is_integral_v<A> && sizeof(A) < 4)
    static int32_t max(A a, A b) noexcept { return max(static_cast<int32_t>(a), static_cast<int32_t>(b)); }
    template<class A>
        requires(std::is_integral_v<A> && sizeof(A) < 4)
    static int32_t min(A a, A b) noexcept { return min(static_cast<int32_t>(a), static_cast<int32_t>(b)); }

    static double sqrt(double a) noexcept { return std::sqrt(a); }
    static double cbrt(double a) noexcept;  // out of line: clang constant-folds std::cbrt inexactly
    static double pow(double a, double b) noexcept;
    static double exp(double a) noexcept { return std::exp(a); }
    static double expm1(double a) noexcept { return std::expm1(a); }
    static double log(double a) noexcept { return std::log(a); }
    static double log10(double a) noexcept { return std::log10(a); }
    static double log1p(double a) noexcept { return std::log1p(a); }
    static double sin(double a) noexcept { return std::sin(a); }
    static double cos(double a) noexcept { return std::cos(a); }
    static double tan(double a) noexcept { return std::tan(a); }
    static double asin(double a) noexcept { return std::asin(a); }
    static double acos(double a) noexcept { return std::acos(a); }
    static double atan(double a) noexcept { return std::atan(a); }
    static double atan2(double y, double x) noexcept { return std::atan2(y, x); }
    static double sinh(double a) noexcept { return std::sinh(a); }
    static double cosh(double a) noexcept { return std::cosh(a); }
    static double tanh(double a) noexcept { return std::tanh(a); }
    static double hypot(double x, double y) noexcept { return std::hypot(x, y); }
    static double toRadians(double deg) noexcept { return deg * 0.017453292519943295; }
    static double toDegrees(double rad) noexcept { return rad * 57.29577951308232; }
    static double floor(double a) noexcept { return std::floor(a); }
    static double ceil(double a) noexcept { return std::ceil(a); }
    static double rint(double a) noexcept { return std::nearbyint(a); }  // ties to even
    static int32_t round(float a) noexcept;   // floor(a + 1/2) exactly; NaN -> 0; saturating
    static int64_t round(double a) noexcept;
    // Java picks round(float) for int/long arguments (widening to float).
    template<class A>
        requires std::is_integral_v<A>
    static int32_t round(A a) noexcept { return round(static_cast<float>(a)); }
    static double signum(double a) noexcept;
    static float signum(float a) noexcept;
    static double ulp(double a) noexcept;
    static float ulp(float a) noexcept;
    static double IEEEremainder(double a, double b) noexcept { return std::remainder(a, b); }
    static double random();  // uniform [0, 1) from a shared java.util.Random
    static int32_t floorDiv(int32_t x, int32_t y);
    static int64_t floorDiv(int64_t x, int64_t y);
    static int32_t floorMod(int32_t x, int32_t y);
    static int64_t floorMod(int64_t x, int64_t y);
    static int32_t addExact(int32_t a, int32_t b);
    static int64_t addExact(int64_t a, int64_t b);
    static int32_t multiplyExact(int32_t a, int32_t b);
    static int64_t multiplyExact(int64_t a, int64_t b);
    static int32_t toIntExact(int64_t v);
};
using StrictMath = Math;

// ---------------------------------------------------------------------------------------
// java.util.Random: the exact 48-bit LCG and derived algorithms of the JDK (same sequences
// for the same seed). Thread-safe like Java's (atomic seed; nextGaussian synchronized).
//
// Subclasses (MTRandom) override next(bits) and setSeed(seed). NOTE: Java's Random(long)
// constructor calls the overridden setSeed of the subclass; in C++ the base constructor calls
// Random::setSeed. A subclass must therefore initialize its own state in its own constructor.
class Random : public virtual Object {
public:
    Random();                      // seed = seedUniquifier() ^ System.nanoTime()
    explicit Random(int64_t seed);

    virtual void setSeed(int64_t seed);
    virtual int32_t nextInt();
    virtual int32_t nextInt(int32_t bound);              // IllegalArgumentException if bound <= 0
    virtual int32_t nextInt(int32_t origin, int32_t bound);  // Java 17 bounded range
    virtual int64_t nextLong();
    virtual bool nextBoolean();
    virtual float nextFloat();
    virtual double nextDouble();
    virtual double nextGaussian();
#ifdef JLANG_HAVE_ARRAY_H
    virtual void nextBytes(Array<int8_t>* bytes);
#endif

protected:
    virtual int32_t next(int32_t bits);

private:
    static int64_t initialScramble(int64_t seed) noexcept;
    std::atomic<int64_t> seed_{0};
    double nextNextGaussian_ = 0;
    bool haveNextNextGaussian_ = false;
};

}  // namespace jlang
