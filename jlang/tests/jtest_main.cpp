// main() of the jlang_tests executable (see jtest.h).
#include "jtest.h"

#include <cstring>
#include <exception>

namespace jtest {

std::vector<TestCase>& registry() {
    static std::vector<TestCase>* r = new std::vector<TestCase>();
    return *r;
}

static int g_failures = 0;
static const char* g_current = "";

void fail(const char* file, int line, const std::string& msg) {
    g_failures++;
    std::fprintf(stderr, "  FAIL [%s] %s:%d: %s\n", g_current, file, line, msg.c_str());
}

}  // namespace jtest

int main(int argc, char** argv) {
    jlang::init(argc, argv);
    const char* filter = argc > 1 ? argv[1] : nullptr;
    int run = 0, failedTests = 0;
    for (auto& t : jtest::registry()) {
        if (filter != nullptr && std::strstr(t.name, filter) == nullptr) continue;
        jtest::g_current = t.name;
        int before = jtest::g_failures;
        std::fprintf(stderr, "[ RUN  ] %s\n", t.name);
        try {
            t.fn();
        } catch (jlang::Throwable& e) {
            jtest::fail(t.file, t.line, std::string("uncaught ") + e.what());
        } catch (std::exception& e) {
            jtest::fail(t.file, t.line, std::string("uncaught std::exception: ") + e.what());
        } catch (...) {
            jtest::fail(t.file, t.line, "uncaught unknown exception");
        }
        run++;
        bool ok = jtest::g_failures == before;
        if (!ok) failedTests++;
        std::fprintf(stderr, "[ %s ] %s\n", ok ? " OK " : "FAIL", t.name);
    }
    std::fprintf(stderr, "%d tests run, %d failed (%d failed checks)\n", run, failedTests, jtest::g_failures);
    return failedTests == 0 && run > 0 ? 0 : 1;
}
