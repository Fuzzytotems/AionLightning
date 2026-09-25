// java.lang.System, Runtime, Math, java.util.Random and the minimal PrintStream.
#include <jlang/Array.h>
#include <jlang/Boxes.h>
#include <jlang/Exceptions.h>
#include <jlang/Runtime.h>
#include <jlang/String.h>
#include <jlang/System.h>
#include <jlang/Util.h>

#include <algorithm>
#include <bit>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <mutex>
#include <pwd.h>
#include <sys/utsname.h>
#include <thread>
#include <unistd.h>
#include <vector>

namespace jlang {

// ---------------------------------------------------------------------------------------
// PrintStream

PrintStream::PrintStream(const String& fileName) : autoFlush_(false), owns_(true) {
    file_ = std::fopen(std::string(fileName).c_str(), "w");
    if (file_ == nullptr) {
        throw FileNotFoundException(str(fileName, " (", std::strerror(errno), ")"));
    }
}

PrintStream::PrintStream(std::FILE* file, bool autoFlush, bool ownsFile)
    : file_(file), autoFlush_(autoFlush), owns_(ownsFile) {}

void PrintStream::writeBytes(const char* data, size_t n) {
    if (file_ == nullptr) {
        setError();
        return;
    }
    if (n > 0 && std::fwrite(data, 1, n, file_) != n) setError();
}

void PrintStream::writeString(const std::string& s, bool newline) {
    MonitorGuard guard(this);
    if (newline) {
        std::string line;
        line.reserve(s.size() + 1);
        line.append(s).push_back('\n');
        writeBytes(line.data(), line.size());
        if (autoFlush_) flush();
    } else {
        writeBytes(s.data(), s.size());
        if (autoFlush_ && s.find('\n') != std::string::npos) flush();
    }
}

void PrintStream::print(Array<char16_t>* chars) {
    if (chars == nullptr) throw NullPointerException();
    writeString(String(chars), false);
}

void PrintStream::println(Array<char16_t>* chars) {
    if (chars == nullptr) throw NullPointerException();
    writeString(String(chars), true);
}

void PrintStream::write(int32_t b) {
    MonitorGuard guard(this);
    char c = static_cast<char>(b);
    writeBytes(&c, 1);
    if (autoFlush_ && c == '\n') flush();
}

void PrintStream::flush() {
    if (file_ != nullptr) std::fflush(file_);
}

void PrintStream::close() {
    MonitorGuard guard(this);
    if (file_ == nullptr) return;
    std::fflush(file_);
    if (owns_) std::fclose(file_);
    file_ = nullptr;
}

namespace detail {
PrintStream* stdoutStream() {
    static PrintStream* s = new PrintStream(stdout, true, false);
    return s;
}
PrintStream* stderrStream() {
    static PrintStream* s = new PrintStream(stderr, true, false);
    return s;
}
}  // namespace detail

// ---------------------------------------------------------------------------------------
// System

namespace {

struct Properties {
    std::mutex mu;
    std::map<std::string, std::string> values;
    bool defaultsLoaded = false;
};

Properties& props() {
    static Properties* p = new Properties();
    return *p;
}

void loadDefaults(Properties& p) {
    if (p.defaultsLoaded) return;
    p.defaultsLoaded = true;
    auto set = [&](const char* k, const std::string& v) { p.values.emplace(k, v); };
    char cwd[4096];
    if (getcwd(cwd, sizeof cwd) != nullptr) set("user.dir", cwd);
    const char* home = std::getenv("HOME");
    const char* user = std::getenv("USER");
    struct passwd* pw = getpwuid(getuid());
    if (home == nullptr && pw != nullptr) home = pw->pw_dir;
    if (user == nullptr && pw != nullptr) user = pw->pw_name;
    set("user.home", home != nullptr ? home : "/");
    set("user.name", user != nullptr ? user : "unknown");
    struct utsname un;
    if (uname(&un) == 0) {
        set("os.name", un.sysname);
        set("os.version", un.release);
        std::string arch = un.machine;
        set("os.arch", arch == "x86_64" ? std::string("amd64") : arch);
    } else {
        set("os.name", "Linux");
    }
    set("java.version", "cpp");
    set("java.vendor", "jlang");
    set("java.vm.name", "jlang (C++ runtime)");
    set("java.vm.version", "1");
    set("java.home", "/");
    set("java.class.path", "");
    set("java.io.tmpdir", "/tmp");
    set("file.encoding", "UTF-8");
    set("sun.jnu.encoding", "UTF-8");
    set("line.separator", "\n");
    set("file.separator", "/");
    set("path.separator", ":");
}

}  // namespace

namespace detail {
void initSystemProperties(int argc, char** argv) {
    Properties& p = props();
    std::lock_guard<std::mutex> lock(p.mu);
    loadDefaults(p);
    for (int i = 1; i < argc; i++) {
        if (argv[i] == nullptr) continue;
        std::string a = argv[i];
        if (a.size() > 2 && a[0] == '-' && a[1] == 'D') {
            size_t eq = a.find('=');
            if (eq == std::string::npos) {
                p.values[a.substr(2)] = "";
            } else {
                p.values[a.substr(2, eq - 2)] = a.substr(eq + 1);
            }
        }
    }
}
}  // namespace detail

int64_t System::currentTimeMillis() noexcept {
    return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch())
        .count();
}

int64_t System::nanoTime() noexcept {
    return std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch())
        .count();
}

void System::gc() { gc::collect(); }

String System::getProperty(const String& key) {
    if (key.isNull()) throw NullPointerException(String("key can't be null"));
    if (key.isEmpty()) throw IllegalArgumentException(String("key can't be empty"));
    Properties& p = props();
    std::lock_guard<std::mutex> lock(p.mu);
    loadDefaults(p);
    auto it = p.values.find(key);
    return it == p.values.end() ? String() : String(it->second);
}

String System::getProperty(const String& key, const String& def) {
    String v = getProperty(key);
    return v.isNull() ? def : v;
}

String System::setProperty(const String& key, const String& value) {
    if (key.isNull()) throw NullPointerException(String("key can't be null"));
    if (key.isEmpty()) throw IllegalArgumentException(String("key can't be empty"));
    if (value.isNull()) throw NullPointerException();
    Properties& p = props();
    std::lock_guard<std::mutex> lock(p.mu);
    loadDefaults(p);
    String old;
    auto it = p.values.find(key);
    if (it != p.values.end()) old = String(it->second);
    p.values[key] = std::string(value);
    return old;
}

String System::clearProperty(const String& key) {
    Properties& p = props();
    std::lock_guard<std::mutex> lock(p.mu);
    loadDefaults(p);
    String old;
    auto it = p.values.find(key);
    if (it != p.values.end()) {
        old = String(it->second);
        p.values.erase(it);
    }
    return old;
}

String System::getenv(const String& name) {
    if (name.isNull()) throw NullPointerException();
    const char* v = std::getenv(std::string(name).c_str());
    return v == nullptr ? String() : String(v);
}

void System::exit(int32_t status) {
    Runtime::getRuntime()->runShutdownHooks();
    std::fflush(nullptr);
    std::_Exit(status);
}

// ---------------------------------------------------------------------------------------
// Runtime

namespace {
struct Hooks {
    std::mutex mu;
    std::vector<Runnable*> hooks;
    bool started = false;
};
Hooks& hooks() {
    static Hooks* h = new Hooks();
    return *h;
}
}  // namespace

Runtime* Runtime::getRuntime() {
    static Runtime* r = new Runtime();
    return r;
}

int32_t Runtime::availableProcessors() {
    unsigned n = std::thread::hardware_concurrency();
    return n == 0 ? 1 : static_cast<int32_t>(n);
}

int64_t Runtime::totalMemory() { return gc::heapSize(); }
int64_t Runtime::freeMemory() { return gc::freeBytes(); }
int64_t Runtime::maxMemory() { return gc::maxHeapSize(); }
void Runtime::gc() { gc::collect(); }

void Runtime::halt(int32_t status) { std::_Exit(status); }

void Runtime::addShutdownHook(Runnable* hook) {
    if (hook == nullptr) throw NullPointerException();
    Hooks& h = hooks();
    std::lock_guard<std::mutex> lock(h.mu);
    if (h.started) throw IllegalStateException(String("Shutdown in progress"));
    if (std::find(h.hooks.begin(), h.hooks.end(), hook) != h.hooks.end()) {
        throw IllegalArgumentException(String("Hook previously registered"));
    }
    h.hooks.push_back(hook);
}

bool Runtime::removeShutdownHook(Runnable* hook) {
    if (hook == nullptr) throw NullPointerException();
    Hooks& h = hooks();
    std::lock_guard<std::mutex> lock(h.mu);
    if (h.started) throw IllegalStateException(String("Shutdown in progress"));
    auto it = std::find(h.hooks.begin(), h.hooks.end(), hook);
    if (it == h.hooks.end()) return false;
    h.hooks.erase(it);
    return true;
}

void Runtime::runShutdownHooks() {
    Hooks& h = hooks();
    std::vector<Runnable*> toRun;
    {
        std::lock_guard<std::mutex> lock(h.mu);
        if (h.started) return;
        h.started = true;
        toRun = h.hooks;
    }
    for (Runnable* r : toRun) {
        try {
            r->run();
        } catch (Throwable& t) {
            std::fprintf(stderr, "Exception in shutdown hook: ");
            t.printStackTrace();
        } catch (std::exception& e) {
            std::fprintf(stderr, "Exception in shutdown hook: %s\n", e.what());
        } catch (...) {
            std::fprintf(stderr, "Unknown exception in shutdown hook\n");
        }
    }
}

// ---------------------------------------------------------------------------------------
// Math

float Math::max(float a, float b) noexcept { return Float::max(a, b); }
double Math::max(double a, double b) noexcept { return Double::max(a, b); }
float Math::min(float a, float b) noexcept { return Float::min(a, b); }
double Math::min(double a, double b) noexcept { return Double::min(a, b); }

// Java's StrictMath.cbrt (fdlibm s_cbrt.c, which Math.cbrt uses): glibc's cbrt differs in the
// last bit for some inputs (cbrt(27.0) == 3.0000000000000004).
double Math::cbrt(double x) noexcept {
    constexpr uint32_t B1 = 715094163, B2 = 696219795;
    constexpr double C = 5.42857142857142815906e-01, D = -7.05306122448979611050e-01,
                     E = 1.41428571428571436819e+00, F = 1.60714285714285720630e+00,
                     G = 3.57142857142857150787e-01;
    auto hi = [](double v) { return static_cast<uint32_t>(std::bit_cast<uint64_t>(v) >> 32); };
    auto lo = [](double v) { return static_cast<uint32_t>(std::bit_cast<uint64_t>(v)); };
    auto make = [](uint32_t h, uint32_t l) { return std::bit_cast<double>((static_cast<uint64_t>(h) << 32) | l); };
    uint32_t hx = hi(x);
    const uint32_t sign = hx & 0x80000000u;
    hx ^= sign;
    if (hx >= 0x7ff00000u) return x + x;       // NaN, Infinity
    if ((hx | lo(x)) == 0) return x;          // +-0
    x = make(hx, lo(x));                      // |x|
    double t = 0.0;
    if (hx < 0x00100000u) {                   // subnormal
        t = make(0x43500000u, 0);             // 2^54
        t *= x;
        t = make(hi(t) / 3 + B2, lo(t));
    } else {
        t = make(hx / 3 + B1, 0);
    }
    double r = t * t / x;
    double s = C + r * t;
    t *= G + F / (s + E + D / s);
    t = make(hi(t) + 1, 0);
    s = t * t;
    r = x / s;
    double w = t + t;
    r = (r - t) / (w + r);
    t = t + t * r;
    return make(hi(t) | sign, lo(t));
}

double Math::pow(double a, double b) noexcept {
    // Java: NaN exponent -> NaN (C gives 1 for pow(1, NaN)); |a| == 1 with infinite b -> NaN.
    if (b != b) return b;
    if (std::isinf(b) && std::fabs(a) == 1.0) return std::numeric_limits<double>::quiet_NaN();
    return std::pow(a, b);
}

int32_t Math::round(float a) noexcept {
    if (a != a) return 0;
    float f = std::floor(a);
    if (a - f >= 0.5f) f += 1.0f;
    return f2i(f);
}

int64_t Math::round(double a) noexcept {
    if (a != a) return 0;
    double f = std::floor(a);
    if (a - f >= 0.5) f += 1.0;
    return d2l(f);
}

double Math::signum(double a) noexcept {
    if (a != a || a == 0.0) return a;
    return a > 0 ? 1.0 : -1.0;
}

float Math::signum(float a) noexcept {
    if (a != a || a == 0.0f) return a;
    return a > 0 ? 1.0f : -1.0f;
}

double Math::ulp(double a) noexcept {
    if (a != a) return a;
    if (std::isinf(a)) return std::numeric_limits<double>::infinity();
    a = std::fabs(a);
    if (a == std::numeric_limits<double>::max()) return std::ldexp(1.0, 971);
    return std::nextafter(a, std::numeric_limits<double>::infinity()) - a;
}

float Math::ulp(float a) noexcept {
    if (a != a) return a;
    if (std::isinf(a)) return std::numeric_limits<float>::infinity();
    a = std::fabs(a);
    if (a == std::numeric_limits<float>::max()) return std::ldexp(1.0f, 104);
    return std::nextafter(a, std::numeric_limits<float>::infinity()) - a;
}

double Math::random() {
    static Random* rnd = new Random();
    return rnd->nextDouble();
}

int32_t Math::floorDiv(int32_t x, int32_t y) {
    int32_t q = idiv(x, y);
    if ((x ^ y) < 0 && q * y != x) q--;
    return q;
}
int64_t Math::floorDiv(int64_t x, int64_t y) {
    int64_t q = idiv(x, y);
    if ((x ^ y) < 0 && q * y != x) q--;
    return q;
}
int32_t Math::floorMod(int32_t x, int32_t y) { return x - floorDiv(x, y) * y; }
int64_t Math::floorMod(int64_t x, int64_t y) { return x - floorDiv(x, y) * y; }

int32_t Math::addExact(int32_t a, int32_t b) {
    int32_t r;
    if (__builtin_add_overflow(a, b, &r)) throw ArithmeticException(String("integer overflow"));
    return r;
}
int64_t Math::addExact(int64_t a, int64_t b) {
    int64_t r;
    if (__builtin_add_overflow(a, b, &r)) throw ArithmeticException(String("long overflow"));
    return r;
}
int32_t Math::multiplyExact(int32_t a, int32_t b) {
    int32_t r;
    if (__builtin_mul_overflow(a, b, &r)) throw ArithmeticException(String("integer overflow"));
    return r;
}
int64_t Math::multiplyExact(int64_t a, int64_t b) {
    int64_t r;
    if (__builtin_mul_overflow(a, b, &r)) throw ArithmeticException(String("long overflow"));
    return r;
}
int32_t Math::toIntExact(int64_t v) {
    if (v != static_cast<int32_t>(v)) throw ArithmeticException(String("integer overflow"));
    return static_cast<int32_t>(v);
}

// ---------------------------------------------------------------------------------------
// Random (java.util.Random)

namespace {
constexpr int64_t kMultiplier = 0x5DEECE66DLL;
constexpr int64_t kAddend = 0xBLL;
constexpr int64_t kMask = (int64_t{1} << 48) - 1;
constexpr double kDoubleUnit = 1.0 / static_cast<double>(int64_t{1} << 53);

std::atomic<int64_t> g_seedUniquifier{8682522807148012LL};

int64_t seedUniquifier() {
    for (;;) {
        int64_t current = g_seedUniquifier.load();
        int64_t next = current * 1181783497276652981LL;
        if (g_seedUniquifier.compare_exchange_weak(current, next)) return next;
    }
}
}  // namespace

int64_t Random::initialScramble(int64_t seed) noexcept { return (seed ^ kMultiplier) & kMask; }

Random::Random() : Random(seedUniquifier() ^ System::nanoTime()) {}

Random::Random(int64_t seed) { seed_.store(initialScramble(seed)); }

void Random::setSeed(int64_t seed) {
    MonitorGuard guard(this);
    seed_.store(initialScramble(seed));
    haveNextNextGaussian_ = false;
}

int32_t Random::next(int32_t bits) {
    int64_t oldseed, nextseed;
    do {
        oldseed = seed_.load();
        nextseed = (oldseed * kMultiplier + kAddend) & kMask;
    } while (!seed_.compare_exchange_weak(oldseed, nextseed));
    return static_cast<int32_t>(ushr(nextseed, 48 - bits));
}

int32_t Random::nextInt() { return next(32); }

int32_t Random::nextInt(int32_t bound) {
    if (bound <= 0) throw IllegalArgumentException(String("bound must be positive"));
    int32_t r = next(31);
    int32_t m = bound - 1;
    if ((bound & m) == 0) {
        r = static_cast<int32_t>((static_cast<int64_t>(bound) * static_cast<int64_t>(r)) >> 31);
    } else {
        for (int32_t u = r; u - (r = u % bound) + m < 0; u = next(31)) {
        }
    }
    return r;
}

int32_t Random::nextInt(int32_t origin, int32_t bound) {
    if (origin >= bound) throw IllegalArgumentException(String("bound must be greater than origin"));
    // java.util.random.RandomSupport.boundedNextInt
    int32_t r = nextInt();
    const int32_t n = bound - origin, m = n - 1;
    if ((n & m) == 0) {
        r = (r & m) + origin;
    } else if (n > 0) {
        for (int32_t u = ushr(r, 1); u + m - (r = u % n) < 0; u = ushr(nextInt(), 1)) {
        }
        r += origin;
    } else {
        while (r < origin || r >= bound) r = nextInt();
    }
    return r;
}

int64_t Random::nextLong() { return (static_cast<int64_t>(next(32)) << 32) + next(32); }

bool Random::nextBoolean() { return next(1) != 0; }

float Random::nextFloat() { return static_cast<float>(next(24)) / static_cast<float>(1 << 24); }

double Random::nextDouble() {
    return static_cast<double>((static_cast<int64_t>(next(26)) << 27) + next(27)) * kDoubleUnit;
}

double Random::nextGaussian() {
    MonitorGuard guard(this);
    if (haveNextNextGaussian_) {
        haveNextNextGaussian_ = false;
        return nextNextGaussian_;
    }
    double v1, v2, s;
    do {
        v1 = 2 * nextDouble() - 1;
        v2 = 2 * nextDouble() - 1;
        s = v1 * v1 + v2 * v2;
    } while (s >= 1 || s == 0);
    double multiplier = std::sqrt(-2 * std::log(s) / s);
    nextNextGaussian_ = v2 * multiplier;
    haveNextNextGaussian_ = true;
    return v1 * multiplier;
}

void Random::nextBytes(Array<int8_t>* bytes) {
    if (bytes == nullptr) throw NullPointerException();
    for (int32_t i = 0, len = bytes->length; i < len;) {
        for (int32_t rnd = nextInt(), n = std::min(len - i, 4); n-- > 0; rnd >>= 8) {
            bytes->data()[i++] = static_cast<int8_t>(rnd);
        }
    }
}

}  // namespace jlang
