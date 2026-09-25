// Throwable hierarchy, causes, messages under GC pressure, JFINALLY/JASSERT, and the
// NullPointerException / ArithmeticException fault handlers.
#include "jtest.h"

#include <atomic>
#include <cstdio>
#include <fstream>
#include <sstream>

using jlang::String;

namespace org::openaion::commons::database::dao {
// A code base exception as the generator emits it.
class DAOException : public jlang::RuntimeException {
public:
    DAOException() {}
    explicit DAOException(const String& message) : jlang::RuntimeException(message) {}
    DAOException(const String& message, jlang::Throwable* cause) : jlang::RuntimeException(message, cause) {}
    explicit DAOException(jlang::Throwable* cause) : jlang::RuntimeException(cause) {}
    JLANG_THROWABLE(DAOException)
};
}  // namespace org::openaion::commons::database::dao

using org::openaion::commons::database::dao::DAOException;

JTEST(Exceptions_ToStringAndMessages) {
    jlang::IllegalStateException e("bad state");
    JCHECK_EQ(e.getMessage(), String("bad state"));
    JCHECK_EQ(e.getLocalizedMessage(), String("bad state"));
    JCHECK_EQ(e.toString(), String("java.lang.IllegalStateException: bad state"));
    JCHECK_EQ(String(e.what()), String("java.lang.IllegalStateException: bad state"));
    jlang::RuntimeException none;
    JCHECK(none.getMessage().isNull());
    JCHECK_EQ(none.toString(), String("java.lang.RuntimeException"));
    DAOException dao("db down");
    JCHECK_EQ(dao.toString(), String("org.openaion.commons.database.dao.DAOException: db down"));
    JCHECK_EQ(jlang::NoSuchElementException().toString(), String("java.util.NoSuchElementException"));
    JCHECK_EQ(jlang::IOException("x").toString(), String("java.io.IOException: x"));
    JCHECK_EQ(jlang::SQLException("q").toString(), String("java.sql.SQLException: q"));
    JCHECK_EQ(jlang::ArrayIndexOutOfBoundsException(5, 3).getMessage(), String("Index 5 out of bounds for length 3"));
    JCHECK_EQ(jlang::StringIndexOutOfBoundsException(7).getMessage(), String("String index out of range: 7"));
    jlang::SQLException sql("dup", "23000", 1062);
    JCHECK_EQ(sql.getErrorCode(), 1062);
    JCHECK_EQ(sql.getSQLState(), String("23000"));
    JCHECK(jlang::SQLException("x").getSQLState().isNull());
    jlang::ParseException pe("Unparseable date", 4);
    JCHECK_EQ(pe.getErrorOffset(), 4);
    JCHECK_EQ(jlang::AssertionError().toString(), String("java.lang.AssertionError"));
}

JTEST(Exceptions_Hierarchy) {
    auto catchesAs = [](auto thrower) -> String {
        try {
            thrower();
        } catch (jlang::IllegalArgumentException&) {
            return "IAE";
        } catch (jlang::RuntimeException&) {
            return "RTE";
        } catch (jlang::IOException&) {
            return "IOE";
        } catch (jlang::Exception&) {
            return "EX";
        } catch (jlang::Error&) {
            return "ERR";
        } catch (jlang::Throwable&) {
            return "T";
        }
        return "none";
    };
    JCHECK_EQ(catchesAs([] { throw jlang::NumberFormatException("x"); }), String("IAE"));
    JCHECK_EQ(catchesAs([] { throw jlang::NullPointerException(); }), String("RTE"));
    JCHECK_EQ(catchesAs([] { throw jlang::CancellationException(); }), String("RTE"));
    JCHECK_EQ(catchesAs([] { throw jlang::FileNotFoundException("f"); }), String("IOE"));
    JCHECK_EQ(catchesAs([] { throw jlang::ClosedChannelException(); }), String("IOE"));
    JCHECK_EQ(catchesAs([] { throw jlang::SQLException("s"); }), String("EX"));
    JCHECK_EQ(catchesAs([] { throw jlang::InterruptedException(); }), String("EX"));
    JCHECK_EQ(catchesAs([] { throw jlang::ClassNotFoundException("c"); }), String("EX"));
    JCHECK_EQ(catchesAs([] { throw jlang::NoSuchAlgorithmException("a"); }), String("EX"));
    JCHECK_EQ(catchesAs([] { throw jlang::OutOfMemoryError(); }), String("ERR"));
    JCHECK_EQ(catchesAs([] { throw jlang::AbstractMethodError(); }), String("ERR"));
    JCHECK_EQ(catchesAs([] { throw jlang::Throwable("t"); }), String("T"));
    JCHECK_EQ(catchesAs([] { throw DAOException("d"); }), String("RTE"));
    // std::exception interop
    try {
        throw jlang::IllegalStateException("std");
    } catch (std::exception& e) {
        JCHECK_EQ(String(e.what()), String("java.lang.IllegalStateException: std"));
    }
}

JTEST(Exceptions_Causes) {
    try {
        try {
            throw jlang::SQLException("inner", "HY000", 7);
        } catch (jlang::SQLException& e) {
            throw DAOException("outer", &e);
        }
    } catch (jlang::RuntimeException& e) {
        JCHECK_EQ(e.getMessage(), String("outer"));
        jlang::Throwable* cause = e.getCause();
        JCHECK(cause != nullptr);
        // the stored cause keeps its dynamic type
        auto* sql = dynamic_cast<jlang::SQLException*>(cause);
        JCHECK(sql != nullptr);
        if (sql) {
            JCHECK_EQ(sql->getErrorCode(), 7);
            JCHECK_EQ(sql->getSQLState(), String("HY000"));
        }
        JCHECK_EQ(cause->toString(), String("java.sql.SQLException: inner"));
        JCHECK(jlang::instanceof<jlang::SQLException>(cause));
        JCHECK(jlang::instanceof<jlang::Throwable>(static_cast<jlang::Object*>(cause)));
    }
    // Java new X(cause): message = cause.toString()
    try {
        try {
            throw jlang::IllegalArgumentException("arg");
        } catch (jlang::IllegalArgumentException& e) {
            throw jlang::RuntimeException(e);
        }
    } catch (jlang::RuntimeException& e) {
        JCHECK_EQ(e.getMessage(), String("java.lang.IllegalArgumentException: arg"));
        JCHECK(dynamic_cast<jlang::IllegalArgumentException*>(e.getCause()) != nullptr);
        JCHECK(dynamic_cast<jlang::IllegalArgumentException*>(&e) == nullptr);  // wrapped, not copied
    }
    // subclass of the constructed type as cause: wraps (Java semantics), not a slicing copy
    try {
        try {
            throw jlang::NumberFormatException("nfe");
        } catch (jlang::NumberFormatException& e) {
            throw jlang::IllegalArgumentException(e);
        }
    } catch (jlang::IllegalArgumentException& e) {
        JCHECK(dynamic_cast<jlang::NumberFormatException*>(&e) == nullptr);
        JCHECK(dynamic_cast<jlang::NumberFormatException*>(e.getCause()) != nullptr);
    }
    // pointer form and null cause
    jlang::IOException io("io");
    jlang::Error err(&io);
    JCHECK_EQ(err.getMessage(), String("java.io.IOException: io"));
    jlang::Error nul(static_cast<jlang::Throwable*>(nullptr));
    JCHECK(nul.getMessage().isNull());
    JCHECK(nul.getCause() == nullptr);
    jlang::RuntimeException withMsg("m", io);
    JCHECK_EQ(withMsg.getCause()->getMessage(), String("io"));
    // initCause
    jlang::Exception ex("x");
    ex.initCause(&io);
    JCHECK_EQ(ex.getCause()->getMessage(), String("io"));
    JCHECK_THROWS(jlang::IllegalStateException, ex.initCause(&io));
    // a cause that is already a GC object is shared, like Java
    jlang::Throwable* shared = ex.getCause();
    jlang::Exception ex2("y", shared);
    JCHECK_EQ(ex2.getCause(), shared);
    // copies share state; rethrow keeps the dynamic type
    jlang::Throwable* stored = jlang::FileNotFoundException("file.txt").copyThrowable();
    try {
        stored->rethrow();
    } catch (jlang::FileNotFoundException& e) {
        JCHECK_EQ(e.getMessage(), String("file.txt"));
    }
    // self-catching copy: Exception(const Exception&) copies
    jlang::Exception orig("orig");
    jlang::Exception copy(orig);
    JCHECK_EQ(copy.getMessage(), String("orig"));
    JCHECK(copy.getCause() == nullptr);
}

namespace {
void churnGarbage() {
    // allocate lots of short-lived objects and strings to encourage reuse of freed memory
    for (int i = 0; i < 20000; i++) {
        String s = jlang::str("garbage-", i, "-", std::string(40, 'x'));
        auto* sb = new jlang::StringBuilder(s);
        sb->append(i);
    }
}

[[noreturn]] void throwWithBigMessage(int depth) {
    if (depth > 0) {
        // frames with cleanups (String destructors) between the throw and the catch
        String local = jlang::str("frame", depth, std::string(100, 'z'));
        (void)local;
        throwWithBigMessage(depth - 1);
    }
    String msg = jlang::str("message that must survive a collection: ", std::string(300, 'm'));
    jlang::RuntimeException cause(jlang::str("cause ", std::string(200, 'c')));
    throw jlang::IllegalStateException(msg, cause);
}

struct CollectOnUnwind {
    ~CollectOnUnwind() {
        jlang::gc::collect();
        churnGarbage();
        jlang::gc::collect();
    }
};
}  // namespace

JTEST(Exceptions_MessageSurvivesGC) {
    for (int round = 0; round < 5; round++) {
        try {
            CollectOnUnwind guard;  // forces full collections while the exception is in flight
            throwWithBigMessage(5);
        } catch (jlang::IllegalStateException& e) {
            jlang::gc::collect();
            churnGarbage();
            jlang::gc::collect();
            String m = e.getMessage();
            JCHECK(m.startsWith("message that must survive a collection: "));
            JCHECK_EQ(m.length(), 40 + 300);
            JCHECK(m.endsWith(std::string(300, 'm')));
            JCHECK(e.getCause() != nullptr);
            JCHECK_EQ(e.getCause()->getMessage(), jlang::str("cause ", std::string(200, 'c')));
            JCHECK(String(e.what()).startsWith("java.lang.IllegalStateException: message that must"));
        }
    }
    // exception objects held only by std::exception_ptr
    std::exception_ptr ptr;
    try {
        throw jlang::SQLException(jlang::str("stored ", std::string(500, 's')), "S1000", 42);
    } catch (...) {
        ptr = std::current_exception();
    }
    for (int i = 0; i < 3; i++) {
        churnGarbage();
        jlang::gc::collect();
    }
    try {
        std::rethrow_exception(ptr);
    } catch (jlang::SQLException& e) {
        JCHECK_EQ(e.getMessage().length(), 507);
        JCHECK_EQ(e.getSQLState(), String("S1000"));
    }
}

JTEST(Exceptions_StackTrace) {
    String path = jlang::str("/tmp/jlang_trace_", jlang::System::nanoTime(), ".txt");
    {
        auto* out = new jlang::PrintStream(path);
        try {
            try {
                throw jlang::IOException("disk");
            } catch (jlang::IOException& e) {
                throw DAOException("wrapped", &e);
            }
        } catch (jlang::Throwable& t) {
            t.printStackTrace(out);
            JCHECK(!t.getStackTraceText().empty());
        }
        out->close();
    }
    std::ifstream in(std::string(path).c_str());
    std::stringstream ss;
    ss << in.rdbuf();
    std::string text = ss.str();
    std::remove(std::string(path).c_str());
    JCHECK(text.rfind("org.openaion.commons.database.dao.DAOException: wrapped\n", 0) == 0);
    JCHECK(text.find("\tat ") != std::string::npos);
    JCHECK(text.find("Caused by: java.io.IOException: disk") != std::string::npos);
}

JTEST(Finally_And_Assert) {
    std::vector<int> order;
    {
        JFINALLY { order.push_back(3); };
        JFINALLY { order.push_back(2); };
        order.push_back(1);
    }
    JCHECK((order == std::vector<int>{1, 2, 3}));
    int value = 0;
    try {
        JFINALLY { value = 10; };
        throw jlang::RuntimeException("boom");
    } catch (jlang::RuntimeException&) {
        JCHECK_EQ(value, 10);
    }
    // finally that throws during normal exit propagates
    JCHECK_THROWS(jlang::IllegalStateException, [] {
        JFINALLY { throw jlang::IllegalStateException("from finally"); };
    }());
    // ... and is swallowed while another exception unwinds
    try {
        JFINALLY { throw jlang::IllegalStateException("swallowed"); };
        throw jlang::IOException("original");
    } catch (jlang::IOException& e) {
        JCHECK_EQ(e.getMessage(), String("original"));
    } catch (...) {
        JCHECK(false);
    }
    // finally sees the latest values of locals
    int x = 1;
    int seen = 0;
    {
        JFINALLY { seen = x; };
        x = 7;
    }
    JCHECK_EQ(seen, 7);
    int evaluated = 0;
    JASSERT(++evaluated > 0);
#ifdef JLANG_ASSERTS
    JCHECK_EQ(evaluated, 1);
    JCHECK_THROWS(jlang::AssertionError, [] { JASSERT(1 == 2); }());
#else
    JCHECK_EQ(evaluated, 0);  // not evaluated, like a disabled Java assert
#endif
}

// ---------------------------------------------------------------------------------------
// fault handlers (need -fnon-call-exceptions: GCC only; clang does not implement it)

#if !defined(__clang__)
// External linkage so the frames have dynamic symbols (named in stack traces).
namespace npe_test {
struct Node : public virtual jlang::Object {
    int32_t value = 1;
    Node* next = nullptr;
    virtual int32_t get() { return value; }
};
__attribute__((noinline)) int32_t readField(Node* n) { return n->value; }
__attribute__((noinline)) int32_t callVirtual(Node* n) { return n->get(); }
__attribute__((noinline)) int32_t walk(Node* n) {
    int32_t sum = 0;
    for (int i = 0; i < 10; i++) {
        sum += n->value;
        n = n->next;
    }
    return sum;
}
__attribute__((noinline)) int32_t divide(int32_t a, int32_t b) { return a / b; }
__attribute__((noinline)) int64_t divideLong(int64_t a, int64_t b) { return a % b; }
Node* volatile g_null = nullptr;

int32_t npeRound() {
    int32_t caught = 0;
    for (int i = 0; i < 200; i++) {
        try {
            readField(g_null);
        } catch (jlang::NullPointerException&) {
            caught++;
        }
        try {
            callVirtual(g_null);
        } catch (jlang::NullPointerException& e) {
            if (e.getMessage().isNull()) caught++;
        }
        try {
            Node* a = new Node();
            a->next = new Node();
            walk(a);  // runs off the end of the list
        } catch (jlang::RuntimeException&) {
            caught++;
        }
        try {
            String s = jlang::str("v", g_null->value);  // field read inside an expression with cleanups
            (void)s;
        } catch (jlang::Exception&) {
            caught++;
        }
    }
    return caught;
}
}  // namespace npe_test
using namespace npe_test;

JTEST(Faults_NullPointerException) {
    JCHECK_EQ(npeRound(), 800);
    // from a secondary thread registered with the collector
    std::atomic<int32_t> threadCaught{0};
    struct Arg {
        std::atomic<int32_t>* out;
    };
    uint64_t t = jlang::gc::startNativeThread(
        [](void* p) { static_cast<Arg*>(p)->out->store(npeRound()); }, new Arg{&threadCaught});
    jlang::gc::joinNativeThread(t);
    JCHECK_EQ(threadCaught.load(), 800);
    // stack trace starts at the faulting function
    try {
        callVirtual(g_null);
    } catch (jlang::NullPointerException& e) {
        auto frames = e.getStackTraceText();
        JCHECK(!frames.empty());
        if (!frames.empty()) JCHECK(frames[0].contains("callVirtual"), frames[0]);
    }
}

JTEST(Faults_ArithmeticException) {
    int32_t caught = 0;
    for (int i = 0; i < 100; i++) {
        try {
            divide(i, 0);
        } catch (jlang::ArithmeticException& e) {
            if (e.getMessage() == "/ by zero") caught++;
        }
        try {
            divideLong(i, 0);
        } catch (jlang::ArithmeticException&) {
            caught++;
        }
    }
    JCHECK_EQ(caught, 200);
    JCHECK_EQ(divide(7, 2), 3);
}
#endif

JTEST(Exceptions_IntegerDivisionHelpers) {
    JCHECK_THROWS(jlang::ArithmeticException, jlang::idiv(1, 0));
    JCHECK_THROWS(jlang::ArithmeticException, jlang::irem(int64_t{1}, int64_t{0}));
    JCHECK_EQ(jlang::idiv(INT32_MIN, -1), INT32_MIN);  // Java: overflow wraps
    JCHECK_EQ(jlang::irem(INT32_MIN, -1), 0);
    JCHECK_EQ(jlang::idiv(-7, 2), -3);
    JCHECK_EQ(jlang::irem(-7, 2), -1);
}
