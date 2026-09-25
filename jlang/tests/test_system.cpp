// System, Runtime, Math, Random, PrintStream, Util helpers.
#include "jtest.h"

#include <cmath>
#include <cstdio>
#include <fstream>
#include <limits>
#include <sstream>
#include <fcntl.h>
#include <sys/wait.h>
#include <unistd.h>

namespace jlang::detail {
void initSystemProperties(int argc, char** argv);  // System.cpp (called by jlang::init)
}

using jlang::Math;
using jlang::String;
using jlang::System;

JTEST(System_Time) {
    int64_t ms = System::currentTimeMillis();
    JCHECK(ms > INT64_C(1600000000000));  // after 2020
    int64_t a = System::nanoTime();
    int64_t b = System::nanoTime();
    JCHECK(b >= a);
}

JTEST(System_Properties) {
    JCHECK_EQ(System::getProperty("java.version"), String("cpp"));
    JCHECK_EQ(System::getProperty("line.separator"), String("\n"));
    JCHECK_EQ(System::lineSeparator(), String("\n"));
    JCHECK_EQ(System::getProperty("file.separator"), String("/"));
    JCHECK(!System::getProperty("user.dir").isNull());
    JCHECK(!System::getProperty("os.name").isNull());
    JCHECK(System::getProperty("no.such.property").isNull());
    JCHECK_EQ(System::getProperty("no.such.property", "dflt"), String("dflt"));
    JCHECK(System::setProperty("jlang.test.key", "v1").isNull());
    JCHECK_EQ(System::setProperty("jlang.test.key", "v2"), String("v1"));
    JCHECK_EQ(System::getProperty("jlang.test.key"), String("v2"));
    JCHECK_EQ(System::clearProperty("jlang.test.key"), String("v2"));
    JCHECK(System::getProperty("jlang.test.key").isNull());
    JCHECK_THROWS(jlang::IllegalArgumentException, System::getProperty(""));
    JCHECK_THROWS(jlang::NullPointerException, System::getProperty(String()));
    JCHECK(!System::getenv("PATH").isNull());
    JCHECK(System::getenv("JLANG_SURELY_UNSET_VARIABLE").isNull());
    // -D arguments are parsed by jlang::init
    char a0[] = "prog";
    char a1[] = "-Djlang.from.argv=hello";
    char a2[] = "-Djlang.flag";
    char* argv[] = {a0, a1, a2, nullptr};
    jlang::detail::initSystemProperties(3, argv);
    JCHECK_EQ(System::getProperty("jlang.from.argv"), String("hello"));
    JCHECK_EQ(System::getProperty("jlang.flag"), String(""));
}

JTEST(Runtime_Basics) {
    jlang::Runtime* rt = jlang::Runtime::getRuntime();
    JCHECK_EQ(rt, jlang::Runtime::getRuntime());
    JCHECK(rt->availableProcessors() >= 1);
    JCHECK(rt->totalMemory() > 0);
    JCHECK(rt->freeMemory() >= 0);
    JCHECK(rt->maxMemory() >= rt->totalMemory() / 4);
    int64_t before = jlang::gc::collections();
    System::gc();
    JCHECK(jlang::gc::collections() > before);
    jlang::Runnable* hook = jlang::Runnable::of([] {});
    rt->addShutdownHook(hook);
    JCHECK_THROWS(jlang::IllegalArgumentException, rt->addShutdownHook(hook));
    JCHECK(rt->removeShutdownHook(hook));
    JCHECK(!rt->removeShutdownHook(hook));
    JCHECK_THROWS(jlang::NullPointerException, rt->addShutdownHook(static_cast<jlang::Runnable*>(nullptr)));
}

JTEST(Runtime_ExitRunsHooks) {
    // System.exit runs the hooks, then terminates with the status: check it in a child process.
    int fds[2];
    JCHECK(pipe(fds) == 0);
    std::fflush(nullptr);
    pid_t pid = fork();
    if (pid == 0) {
        close(fds[0]);
        int devnull = open("/dev/null", O_WRONLY);
        if (devnull >= 0) dup2(devnull, 2);  // the throwing hook prints a stack trace
        int fd = fds[1];
        jlang::Runtime::getRuntime()->addShutdownHook(jlang::Runnable::of([fd] {
            ssize_t w = write(fd, "hook1;", 6);
            (void)w;
        }));
        jlang::Runtime::getRuntime()->addShutdownHook(jlang::Runnable::of([fd] {
            ssize_t w = write(fd, "hook2;", 6);
            (void)w;
            throw jlang::IllegalStateException("hooks may throw");
        }));
        System::exit(3);
    }
    close(fds[1]);
    char buf[64] = {0};
    ssize_t total = 0, n;
    while ((n = read(fds[0], buf + total, sizeof buf - 1 - static_cast<size_t>(total))) > 0) total += n;
    close(fds[0]);
    int status = 0;
    waitpid(pid, &status, 0);
    JCHECK(WIFEXITED(status));
    JCHECK_EQ(WEXITSTATUS(status), 3);
    JCHECK_EQ(std::string(buf), std::string("hook1;hook2;"));
}

JTEST(Math_JavaSemantics) {
    JCHECK_EQ(Math::abs(INT32_MIN), INT32_MIN);
    JCHECK_EQ(Math::abs(int64_t{-5}), 5);
    JCHECK_EQ(Math::abs(-2.5f), 2.5f);
    JCHECK_EQ(Math::abs(static_cast<int16_t>(-3)), 3);
    JCHECK_EQ(Math::max(1, 2), 2);
    JCHECK_EQ(Math::max(1, int64_t{5}), 5);
    JCHECK_EQ(Math::min(2.0f, 1), 1.0f);
    JCHECK(std::isnan(Math::max(1.0, std::nan(""))));
    JCHECK(std::isnan(Math::min(std::nanf(""), 1.0f)));
    JCHECK(std::signbit(Math::min(0.0, -0.0)));
    JCHECK(!std::signbit(Math::max(-0.0, 0.0)));
    JCHECK(std::signbit(Math::min(-0.0f, 0.0f)));
    JCHECK_EQ(Math::round(2.5f), 3);
    JCHECK_EQ(Math::round(-2.5), INT64_C(-2));
    JCHECK_EQ(Math::round(7), 7);  // int argument -> round(float)
    JCHECK_EQ(Math::floor(-1.5), -2.0);
    JCHECK_EQ(Math::ceil(-1.5), -1.0);
    JCHECK_EQ(Math::rint(2.5), 2.0);
    JCHECK_EQ(Math::signum(-3.0), -1.0);
    JCHECK(std::signbit(Math::signum(-0.0)));
    JCHECK_EQ(Math::toDegrees(Math::PI), 180.0);
    JCHECK_EQ(Math::toRadians(180.0), Math::PI);
    JCHECK(std::isnan(Math::pow(1.0, std::nan(""))));
    JCHECK(std::isnan(Math::pow(1.0, std::numeric_limits<double>::infinity())));
    JCHECK_EQ(Math::pow(2.0, 10.0), 1024.0);
    JCHECK_EQ(Math::sqrt(16.0), 4.0);
    JCHECK_EQ(Math::cbrt(27.0), 3.0);
    JCHECK_EQ(Math::hypot(3.0, 4.0), 5.0);
    JCHECK_EQ(Math::log10(1000.0), 3.0);
    JCHECK_EQ(Math::atan2(1.0, 1.0), Math::PI / 4);
    JCHECK_EQ(Math::floorDiv(-7, 2), -4);
    JCHECK_EQ(Math::floorMod(-7, 2), 1);
    JCHECK_EQ(Math::floorMod(int64_t{7}, int64_t{-2}), -1);
    JCHECK_THROWS(jlang::ArithmeticException, Math::addExact(INT32_MAX, 1));
    JCHECK_THROWS(jlang::ArithmeticException, Math::toIntExact(INT64_C(1) << 40));
    JCHECK_EQ(Math::ulp(1.0), std::numeric_limits<double>::epsilon());
    for (int i = 0; i < 1000; i++) {
        double r = Math::random();
        JCHECK(r >= 0.0 && r < 1.0);
    }
    JCHECK_EQ(jlang::StrictMath::sqrt(4.0), 2.0);
}

JTEST(Util_ShiftsAndCasts) {
    JCHECK_EQ(jlang::ushr(-1, 28), 15);
    JCHECK_EQ(jlang::ushr(-1, 60), 15);          // count masked with & 31 -> 28
    JCHECK_EQ(jlang::ushr(int64_t{-1}, 60), 15);
    JCHECK_EQ(jlang::ushr(int64_t{-1}, 124), 15);  // & 63
    JCHECK_EQ(jlang::ushr(static_cast<int8_t>(-1), 28), 15);  // byte promoted to int
    JCHECK_EQ(jlang::ushr(static_cast<char16_t>(0xFFFF), 4), 0xFFF);
    JCHECK_EQ(jlang::ushr(8, 0), 8);
    JCHECK_EQ(jlang::shl(1, 33), 2);
    JCHECK_EQ(jlang::shr(-16, 34), -4);
    JCHECK_EQ(jlang::d2i(1e20), INT32_MAX);
    JCHECK_EQ(jlang::d2i(-1e20), INT32_MIN);
    JCHECK_EQ(jlang::d2i(std::nan("")), 0);
    JCHECK_EQ(jlang::d2i(-2.9), -2);
    JCHECK_EQ(jlang::d2l(1e30), INT64_MAX);
    JCHECK_EQ(jlang::d2l(-1e30), INT64_MIN);
    JCHECK_EQ(jlang::f2i(std::nanf("")), 0);
    JCHECK_EQ(jlang::f2l(3.9f), 3);
    JCHECK_EQ(jlang::f2i(3e9f), INT32_MAX);
    JCHECK_EQ(jlang::d2i(2147483647.5), INT32_MAX);
    JCHECK_EQ(jlang::d2c(65.7), u'A');
}

namespace {
// MTRandom-like subclass: next(bits) is virtual and drives every other method.
class CountingRandom : public jlang::Random {
public:
    explicit CountingRandom(int64_t seed) : jlang::Random(seed) {}
    int32_t calls = 0;
    int32_t constant = 0x12345678;

protected:
    int32_t next(int32_t bits) override {
        calls++;
        return jlang::ushr(constant, 32 - bits);
    }
};
}  // namespace

JTEST(Random_Behaviour) {
    jlang::Random a(7), b(7);
    for (int i = 0; i < 100; i++) JCHECK_EQ(a.nextInt(), b.nextInt());
    a.setSeed(99);
    jlang::Random c(99);
    JCHECK_EQ(a.nextLong(), c.nextLong());
    JCHECK_THROWS(jlang::IllegalArgumentException, a.nextInt(0));
    JCHECK_THROWS(jlang::IllegalArgumentException, a.nextInt(-5));
    for (int i = 0; i < 1000; i++) {
        int32_t v = a.nextInt(10);
        JCHECK(v >= 0 && v < 10);
        int32_t w = a.nextInt(-5, 5);
        JCHECK(w >= -5 && w < 5);
        float f = a.nextFloat();
        JCHECK(f >= 0.0f && f < 1.0f);
    }
    jlang::Random d1, d2;  // default seeds differ
    JCHECK(d1.nextLong() != d2.nextLong());
    CountingRandom cr(1);
    int32_t v = cr.nextInt(1 << 10);
    JCHECK_EQ(cr.calls, 1);
    JCHECK_EQ(v, static_cast<int32_t>((int64_t{1} << 10) * jlang::ushr(0x12345678, 1) >> 31));
    cr.nextDouble();
    JCHECK_EQ(cr.calls, 3);
}

JTEST(PrintStream_Output) {
    String path = jlang::str("/tmp/jlang_ps_", System::nanoTime(), ".txt");
    auto* ps = new jlang::PrintStream(path);
    ps->print("a");
    ps->print(1);
    ps->print(u'b');
    ps->println(2.5);
    ps->println();
    ps->println(static_cast<jlang::Object*>(nullptr));
    ps->printf("%05d|%s%n", 42, "x");
    ps->format("%.1f", 0.25)->append(String("!"));
    ps->write('\n');
    ps->flush();
    JCHECK(!ps->checkError());
    ps->close();
    std::ifstream in(std::string(path).c_str());
    std::stringstream ss;
    ss << in.rdbuf();
    std::remove(std::string(path).c_str());
    JCHECK_EQ(ss.str(), std::string("a1b2.5\n\nnull\n00042|x\n0.3!\n"));
    JCHECK_THROWS(jlang::FileNotFoundException, new jlang::PrintStream("/nonexistent-dir/x/y.txt"));
    // System.out / System.err exist before main and are PrintStreams
    JCHECK(System::out != nullptr);
    JCHECK(System::err != nullptr);
    JCHECK_EQ(System::out->getClass()->getName(), String("java.io.PrintStream"));
}

JTEST(Math_CbrtMatchesStrictMath) {
    // values from java.lang.StrictMath.cbrt
    JCHECK_EQ(Math::cbrt(27.0), 3.0);
    JCHECK_EQ(Math::cbrt(-8.0), -2.0);
    JCHECK_EQ(Math::cbrt(1000.0), 10.0);
    JCHECK_EQ(Math::cbrt(2.0), 1.2599210498948732);
    JCHECK_EQ(Math::cbrt(0.001), 0.1);
    JCHECK(std::signbit(Math::cbrt(-0.0)));
    JCHECK(std::isnan(Math::cbrt(std::nan(""))));
    JCHECK_EQ(Math::cbrt(4.9e-324), 1.7031839360032603E-108);
}

JTEST(PrintStream_CharArray) {
    String path = jlang::str("/tmp/jlang_ps2_", System::nanoTime(), ".txt");
    auto* ps = new jlang::PrintStream(path);
    ps->print(String("abé").toCharArray());
    ps->println(String("cd").toCharArray());
    ps->close();
    std::ifstream in(std::string(path).c_str());
    std::stringstream ss;
    ss << in.rdbuf();
    std::remove(std::string(path).c_str());
    JCHECK_EQ(ss.str(), std::string("abécd\n"));
}
