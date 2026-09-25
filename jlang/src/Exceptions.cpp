// java.lang.Throwable and friends.
#include <jlang/Boxes.h>
#include <jlang/Exceptions.h>
#include <jlang/String.h>
#include <jlang/System.h>
#include <jlang/Util.h>

#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cxxabi.h>
#include <execinfo.h>
#include <functional>
#include <mutex>
#include <string>
#include <vector>

namespace jlang {

namespace detail {

constexpr int kMaxFrames = 48;

struct ThrowableData {
    String message;              // null = no message
    Throwable* cause = nullptr;  // GC object
    bool causeSet = false;
    int32_t nframes = 0;
    void* frames[kMaxFrames] = {};
    bool fromFault = false;  // frames start at the faulting instruction
    std::mutex whatMu;
    std::string what;
};

std::atomic<bool> g_captureStackTraces{true};

namespace {
thread_local void* tl_faultPc = nullptr;
}
// Set by the fault signal handler right before it throws NullPointerException /
// ArithmeticException: the stack trace then starts at the faulting instruction.
void setFaultPc(void* pc) noexcept { tl_faultPc = pc; }

}  // namespace detail

// ---------------------------------------------------------------------------------------
// constructors

Throwable::Throwable() : data_(new detail::ThrowableData()) { captureStack(); }

Throwable::Throwable(const String& message) : data_(new detail::ThrowableData()) {
    (*data_)->message = message;
    captureStack();
}

Throwable::Throwable(const String& message, const Throwable& cause) : data_(new detail::ThrowableData()) {
    (*data_)->message = message;
    setCauseFrom(&cause);
    captureStack();
}

Throwable::Throwable(const String& message, Throwable* cause) : data_(new detail::ThrowableData()) {
    (*data_)->message = message;
    setCauseFrom(cause);
    captureStack();
}

Throwable::Throwable(Throwable* cause) : data_(new detail::ThrowableData()) {
    if (cause != nullptr) (*data_)->message = cause->toString();
    setCauseFrom(cause);
    captureStack();
}

Throwable::Throwable(const Throwable& cause, WrapTag) : data_(new detail::ThrowableData()) {
    (*data_)->message = const_cast<Throwable&>(cause).toString();
    setCauseFrom(&cause);
    captureStack();
}

// Copies share the (immutable) data block.
Throwable::Throwable(const Throwable& o) : Object(o), std::exception(o), data_(o.data_) {}

Throwable& Throwable::operator=(const Throwable& o) {
    if (this != &o) data_ = o.data_;
    return *this;
}

Throwable::~Throwable() = default;

void Throwable::setCauseFrom(const Throwable* cause) {
    detail::ThrowableData* d = *data_;
    d->causeSet = true;
    if (cause == nullptr) {
        d->cause = nullptr;
    } else if (detail::inGcHeap(cause)) {
        // Already a GC object (e.g. obtained from getCause()): share it like Java does.
        d->cause = const_cast<Throwable*>(cause);
    } else {
        // Exception storage / stack: keep a GC copy with the same dynamic type.
        d->cause = cause->copyThrowable();
    }
}

void Throwable::captureStack() {
    detail::ThrowableData* d = *data_;
    if (!detail::g_captureStackTraces.load(std::memory_order_relaxed)) {
        d->nframes = 0;
        return;
    }
    d->nframes = backtrace(d->frames, detail::kMaxFrames);
    void* faultPc = detail::tl_faultPc;
    if (faultPc != nullptr) {
        detail::tl_faultPc = nullptr;
        for (int i = 0; i < d->nframes; i++) {
            auto f = reinterpret_cast<uintptr_t>(d->frames[i]);
            auto pc = reinterpret_cast<uintptr_t>(faultPc);
            if (f == pc || f == pc + 1) {
                std::memmove(d->frames, d->frames + i, sizeof(void*) * static_cast<size_t>(d->nframes - i));
                d->nframes -= i;
                d->fromFault = true;
                break;
            }
        }
    }
}

Throwable* Throwable::fillInStackTrace() {
    captureStack();
    return this;
}

// ---------------------------------------------------------------------------------------
// accessors

String Throwable::getMessage() { return (*data_)->message; }

String Throwable::getLocalizedMessage() { return getMessage(); }

Throwable* Throwable::getCause() { return (*data_)->cause; }

Throwable* Throwable::initCause(Throwable* cause) {
    detail::ThrowableData* d = *data_;
    if (d->causeSet) throw IllegalStateException(str("Can't overwrite cause with ", cause, " of ", this));
    if (cause == this) throw IllegalArgumentException(String("Self-causation not permitted"));
    setCauseFrom(cause);
    return this;
}

void Throwable::setMessage(const String& message) { (*data_)->message = message; }

String Throwable::className() const { return const_cast<Throwable*>(this)->getClass()->getName(); }

String Throwable::toString() {
    String name = className();
    String msg = getLocalizedMessage();
    if (msg.isNull()) return name;
    return str(name, ": ", msg);
}

const char* Throwable::what() const noexcept {
    detail::ThrowableData* d = *data_;
    try {
        std::lock_guard<std::mutex> lock(d->whatMu);
        if (d->what.empty()) d->what = std::string(const_cast<Throwable*>(this)->toString());
        return d->what.c_str();
    } catch (...) {
        return "java.lang.Throwable";
    }
}

// ---------------------------------------------------------------------------------------
// stack traces

namespace {

// "binary(mangled+0x1f) [0x4005d3]" -> "demangled+0x1f (binary)"
std::string prettyFrame(const char* sym) {
    std::string s(sym);
    size_t open = s.find('(');
    size_t plus = s.find('+', open == std::string::npos ? 0 : open);
    size_t close = s.find(')', open == std::string::npos ? 0 : open);
    if (open == std::string::npos || close == std::string::npos) return s;
    std::string binary = s.substr(0, open);
    size_t slash = binary.find_last_of('/');
    if (slash != std::string::npos) binary = binary.substr(slash + 1);
    std::string mangled, offset;
    if (plus != std::string::npos && plus < close) {
        mangled = s.substr(open + 1, plus - open - 1);
        offset = s.substr(plus, close - plus);
    } else {
        mangled = s.substr(open + 1, close - open - 1);
    }
    std::string name = mangled;
    if (!mangled.empty()) {
        int status = 0;
        char* dem = abi::__cxa_demangle(mangled.c_str(), nullptr, nullptr, &status);
        if (status == 0 && dem != nullptr) name = dem;
        std::free(dem);
    } else if (!offset.empty()) {
        name = "??";  // local symbol: binary+offset (use addr2line -e binary offset)
    } else {
        size_t lb = s.find('[');
        name = lb != std::string::npos ? s.substr(lb) : std::string("??");
    }
    return name + offset + " (" + binary + ")";
}

// A constructor frame of an exception class ("a::b::FooException::FooException(...)").
bool isExceptionCtorFrame(const std::string& f) {
    size_t paren = f.find('(');
    std::string name = f.substr(0, paren);
    size_t p2 = name.rfind("::");
    if (p2 == std::string::npos) return false;
    std::string last = name.substr(p2 + 2);
    std::string rest = name.substr(0, p2);
    size_t p1 = rest.rfind("::");
    std::string cls = p1 == std::string::npos ? rest : rest.substr(p1 + 2);
    if (cls != last) return false;
    auto endsWith = [&](const char* suf) {
        size_t n = std::strlen(suf);
        return cls.size() >= n && cls.compare(cls.size() - n, n, suf) == 0;
    };
    return endsWith("Exception") || endsWith("Error") || endsWith("Throwable");
}

bool isInternalFrame(const std::string& f) {
    return f.find("jlang::Throwable::") != std::string::npos || f.rfind("backtrace", 0) == 0 ||
           isExceptionCtorFrame(f);
}

}  // namespace

std::vector<String> Throwable::getStackTraceText() {
    detail::ThrowableData* d = *data_;
    std::vector<String> out;
    if (d->nframes <= 0) return out;
    char** syms = backtrace_symbols(d->frames, d->nframes);
    if (syms == nullptr) return out;
    std::vector<std::string> frames;
    for (int i = 0; i < d->nframes; i++) frames.push_back(prettyFrame(syms[i]));
    std::free(syms);
    size_t start = 0;
    // Raised by the fault handler (null dereference / division by zero): the frames already
    // start at the faulting function.
    if (!d->fromFault) {
        while (start < frames.size() && isInternalFrame(frames[start])) start++;
    }
    for (size_t i = start; i < frames.size(); i++) out.push_back(String(frames[i]));
    return out;
}

namespace {
void printTrace(Throwable* t, const std::function<void(const String&)>& line) {
    std::vector<Throwable*> seen;
    const char* prefix = "";
    while (t != nullptr) {
        bool loop = false;
        for (Throwable* s : seen) loop = loop || s == t;
        if (loop) {
            line(str(prefix, "[CIRCULAR REFERENCE: ", t->toString(), "]"));
            break;
        }
        seen.push_back(t);
        line(str(prefix, t->toString()));
        for (const String& f : t->getStackTraceText()) line(str("\tat ", f));
        t = t->getCause();
        prefix = "Caused by: ";
    }
}
}  // namespace

void Throwable::printStackTrace() { printStackTrace(System::err); }

void Throwable::printStackTrace(PrintStream* s) {
    if (s == nullptr) s = System::err;
    JSYNC(s) {
        printTrace(this, [s](const String& l) { s->println(l); });
    }
}

// ---------------------------------------------------------------------------------------

ArrayIndexOutOfBoundsException::ArrayIndexOutOfBoundsException(int32_t index, int32_t length)
    : IndexOutOfBoundsException(str("Index ", index, " out of bounds for length ", length)) {}

ArrayIndexOutOfBoundsException::ArrayIndexOutOfBoundsException(int32_t index)
    : IndexOutOfBoundsException(str("Array index out of range: ", index)) {}

StringIndexOutOfBoundsException::StringIndexOutOfBoundsException(int32_t index)
    : IndexOutOfBoundsException(str("String index out of range: ", index)) {}

String StackTraceElement::toString() {
    if (file_.isNull()) return str(cls_, ".", method_, "(Unknown Source)");
    if (line_ >= 0) return str(cls_, ".", method_, "(", file_, ":", line_, ")");
    return str(cls_, ".", method_, "(", file_, ")");
}

namespace detail {

void throwNumberFormatForInput(const String& s) {
    throw NumberFormatException(str("For input string: \"", s, "\""));
}

void throwNumberFormatForInput(const String& s, int32_t radix) {
    if (radix == 10) throwNumberFormatForInput(s);
    throw NumberFormatException(str("For input string: \"", s, "\" under radix ", radix));
}

void assertionFailed(const char* expr, const char* file, int line) {
    throw AssertionError(str(expr, " (", file, ":", line, ")"));
}

void assertionFailed(const char*, const char*, int, const String& message) { throw AssertionError(message); }

void throwDivideByZero() { throw ArithmeticException(String("/ by zero")); }

}  // namespace detail

}  // namespace jlang
