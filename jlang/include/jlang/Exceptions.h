// jlang/Exceptions.h - java.lang.Throwable and the JDK exception hierarchy.
//
// Part of the jlang core: translated code includes <jlang/jlang.h>.
//
// Usage (CONVENTIONS §12): throw by value, catch by reference.
//   throw jlang::IllegalStateException("x");
//   catch (jlang::Exception& e) { log->error("failed", e); }
//   throw jlang::RuntimeException("wrapped", e);     // cause = copy of e
//   throw jlang::RuntimeException(e);                // Java new RuntimeException(e)
//   throw jlang::RuntimeException(&e);               // same, pointer form
//   throw;                                           // rethrow the caught exception as is
//
// Memory: the C++ runtime allocates thrown exception objects with malloc, which the collector
// does not scan. Throwable therefore keeps its message, cause and stack in a separate GC cell
// that is uncollectable while the Throwable lives outside the GC heap (freed by the
// destructor) and an ordinary collectable cell when the Throwable itself is a GC object
// (created with `new`, e.g. a stored cause). Subclasses that add fields holding GC pointers or
// Strings must wrap them in jlang::detail::Pinned<T> for the same reason.
//
// CONTRACT for every Throwable subclass (JDK ones below, and every code base exception):
//   * put JLANG_THROWABLE(ClassName) in the class body. It defines copyThrowable() (a GC copy
//     with the exact dynamic type, used for getCause()) and rethrow() (throws *this by its
//     dynamic type). Without it, stored causes slice to the nearest ancestor that has it.
//   * toString()/getClass()->getName() use the Java FQN derived from the C++ name
//     (org::openaion::X -> org.openaion.X; jlang exceptions map to java.* names). A class may
//     override `jlang::String className() const` when its Java name differs (nested classes
//     are registered with their '$' name by the class registry, which className() honours).
//   * Constructors mirror Java: (), (String), (String, const Throwable&), (String, Throwable*),
//     (const Throwable&)/(Throwable*) — the last two wrap: message = cause.toString().
#pragma once

#include <jlang/String.h>

#include <exception>
#include <new>
#include <type_traits>
#include <utility>

namespace jlang {

class StackTraceElement;
class PrintStream;

namespace detail {

void* gcAllocCell(size_t n, bool uncollectable);
void gcFreeCell(void* p) noexcept;

// Holds a T in a separately allocated GC cell that the collector scans and keeps alive for as
// long as the owner lives, wherever the owner is (malloc'ed exception storage, stack, static
// storage, GC heap). Copying copies the T into a new cell.
template<class T>
class Pinned {
public:
    Pinned() : Pinned(T()) {}
    explicit Pinned(const T& v) {
        allocate();
        new (p_) T(v);
    }
    Pinned(const Pinned& o) : Pinned(*o.p_) {}
    Pinned& operator=(const Pinned& o) {
        if (this != &o) *p_ = *o.p_;
        return *this;
    }
    Pinned& operator=(const T& v) {
        *p_ = v;
        return *this;
    }
    ~Pinned() {
        if (uncollectable_) {
            p_->~T();
            gcFreeCell(p_);
        }
    }
    T& get() noexcept { return *p_; }
    const T& get() const noexcept { return *p_; }
    T* operator->() noexcept { return p_; }
    const T* operator->() const noexcept { return p_; }
    T& operator*() noexcept { return *p_; }
    const T& operator*() const noexcept { return *p_; }

private:
    void allocate() {
        uncollectable_ = !inGcHeap(this);
        p_ = static_cast<T*>(gcAllocCell(sizeof(T), uncollectable_));
    }
    T* p_ = nullptr;
    bool uncollectable_ = true;
};

struct ThrowableData;

}  // namespace detail

// Defines the per-class virtuals every Throwable subclass needs (see the contract above).
#define JLANG_THROWABLE(Name)                                                             \
public:                                                                                   \
    ::jlang::Throwable* copyThrowable() const override { return new Name(*this); }        \
    [[noreturn]] void rethrow() const override { throw *this; }

// ---------------------------------------------------------------------------------------
class Throwable : public std::exception, public virtual Object {
public:
    struct WrapTag {};

    Throwable();
    explicit Throwable(const String& message);
    Throwable(const String& message, const Throwable& cause);
    Throwable(const String& message, Throwable* cause);
    explicit Throwable(Throwable* cause);
    Throwable(const Throwable& cause, WrapTag);
    // Java `new X(cause)` for a cause of any other exception type (the copy constructor
    // handles an argument of exactly this class).
    template<class E>
        requires(std::is_base_of_v<Throwable, E> && !std::is_same_v<E, Throwable>)
    explicit Throwable(const E& cause) : Throwable(static_cast<const Throwable&>(cause), WrapTag{}) {}
    Throwable(const Throwable& o);
    Throwable& operator=(const Throwable& o);
    ~Throwable() override;

    virtual String getMessage();
    virtual String getLocalizedMessage();
    // The cause (a GC copy with the original dynamic type), or nullptr.
    virtual Throwable* getCause();
    // Java initCause: sets the cause once; IllegalStateException if already set.
    Throwable* initCause(Throwable* cause);
    // "java.lang.IllegalStateException: msg" / "a.b.MyException"
    String toString() override;
    // Prints toString(), the captured stack ("\tat ...") and the cause chain to System.err.
    void printStackTrace();
    void printStackTrace(PrintStream* s);
    // Captured frames as text ("function+offset (binary)"), innermost first.
    std::vector<String> getStackTraceText();
    Throwable* fillInStackTrace();

    // The Java class name used by toString(). Default: getClass()->getName().
    virtual String className() const;

    // std::exception::what(): toString() as UTF-8 (valid while this object lives).
    const char* what() const noexcept override;

    // Polymorphic copy into the GC heap / polymorphic throw (see JLANG_THROWABLE).
    virtual Throwable* copyThrowable() const { return new Throwable(*this); }
    [[noreturn]] virtual void rethrow() const { throw *this; }

protected:
    // For subclasses whose getMessage() is computed (e.g. from extra fields).
    void setMessage(const String& message);

private:
    void captureStack();
    void setCauseFrom(const Throwable* cause);
    detail::Pinned<detail::ThrowableData*> data_;
};

// Declares a JDK exception class `Name : Base` with Java's constructors.
#define JLANG_DECLARE_EXCEPTION(Name, Base)                                                  \
    class Name : public Base {                                                               \
    public:                                                                                  \
        Name() : Base() {}                                                                   \
        explicit Name(const ::jlang::String& message) : Base(message) {}                     \
        explicit Name(const char* message) : Base(::jlang::String(message)) {}               \
        Name(const ::jlang::String& message, const ::jlang::Throwable& cause)                \
            : Base(message, cause) {}                                                        \
        Name(const ::jlang::String& message, ::jlang::Throwable* cause) : Base(message, cause) {} \
        explicit Name(::jlang::Throwable* cause) : Base(cause) {}                            \
        Name(const ::jlang::Throwable& cause, ::jlang::Throwable::WrapTag t) : Base(cause, t) {} \
        template<class E>                                                                    \
            requires(std::is_base_of_v<::jlang::Throwable, E> && !std::is_same_v<E, Name>)  \
        explicit Name(const E& cause)                                                        \
            : Base(static_cast<const ::jlang::Throwable&>(cause), ::jlang::Throwable::WrapTag{}) {} \
        JLANG_THROWABLE(Name)                                                                \
    }

// ---- java.lang
JLANG_DECLARE_EXCEPTION(Exception, Throwable);
JLANG_DECLARE_EXCEPTION(Error, Throwable);
JLANG_DECLARE_EXCEPTION(RuntimeException, Exception);
JLANG_DECLARE_EXCEPTION(IllegalArgumentException, RuntimeException);
JLANG_DECLARE_EXCEPTION(IllegalStateException, RuntimeException);
JLANG_DECLARE_EXCEPTION(NullPointerException, RuntimeException);
JLANG_DECLARE_EXCEPTION(ClassCastException, RuntimeException);
JLANG_DECLARE_EXCEPTION(ArithmeticException, RuntimeException);
JLANG_DECLARE_EXCEPTION(IndexOutOfBoundsException, RuntimeException);
JLANG_DECLARE_EXCEPTION(NegativeArraySizeException, RuntimeException);
JLANG_DECLARE_EXCEPTION(UnsupportedOperationException, RuntimeException);
JLANG_DECLARE_EXCEPTION(NumberFormatException, IllegalArgumentException);
JLANG_DECLARE_EXCEPTION(SecurityException, RuntimeException);
JLANG_DECLARE_EXCEPTION(IllegalMonitorStateException, RuntimeException);
JLANG_DECLARE_EXCEPTION(ArrayStoreException, RuntimeException);
JLANG_DECLARE_EXCEPTION(InterruptedException, Exception);
JLANG_DECLARE_EXCEPTION(CloneNotSupportedException, Exception);
JLANG_DECLARE_EXCEPTION(ReflectiveOperationException, Exception);
JLANG_DECLARE_EXCEPTION(ClassNotFoundException, ReflectiveOperationException);
JLANG_DECLARE_EXCEPTION(InstantiationException, ReflectiveOperationException);
JLANG_DECLARE_EXCEPTION(IllegalAccessException, ReflectiveOperationException);
JLANG_DECLARE_EXCEPTION(NoSuchFieldException, ReflectiveOperationException);
JLANG_DECLARE_EXCEPTION(NoSuchMethodException, ReflectiveOperationException);
JLANG_DECLARE_EXCEPTION(VirtualMachineError, Error);
JLANG_DECLARE_EXCEPTION(InternalError, VirtualMachineError);
JLANG_DECLARE_EXCEPTION(OutOfMemoryError, VirtualMachineError);
JLANG_DECLARE_EXCEPTION(StackOverflowError, VirtualMachineError);
JLANG_DECLARE_EXCEPTION(LinkageError, Error);
JLANG_DECLARE_EXCEPTION(IncompatibleClassChangeError, LinkageError);
JLANG_DECLARE_EXCEPTION(AbstractMethodError, IncompatibleClassChangeError);
JLANG_DECLARE_EXCEPTION(ExceptionInInitializerError, LinkageError);
JLANG_DECLARE_EXCEPTION(AssertionError, Error);

// IndexOutOfBounds family: also Java's (int index) constructors.
class ArrayIndexOutOfBoundsException : public IndexOutOfBoundsException {
public:
    ArrayIndexOutOfBoundsException() {}
    explicit ArrayIndexOutOfBoundsException(const String& message) : IndexOutOfBoundsException(message) {}
    explicit ArrayIndexOutOfBoundsException(const char* message)
        : IndexOutOfBoundsException(String(message)) {}
    // "Index 5 out of bounds for length 3" (Java 11+ wording)
    ArrayIndexOutOfBoundsException(int32_t index, int32_t length);
    explicit ArrayIndexOutOfBoundsException(int32_t index);  // "Array index out of range: 5"
    JLANG_THROWABLE(ArrayIndexOutOfBoundsException)
};
class StringIndexOutOfBoundsException : public IndexOutOfBoundsException {
public:
    StringIndexOutOfBoundsException() {}
    explicit StringIndexOutOfBoundsException(const String& message) : IndexOutOfBoundsException(message) {}
    explicit StringIndexOutOfBoundsException(const char* message)
        : IndexOutOfBoundsException(String(message)) {}
    explicit StringIndexOutOfBoundsException(int32_t index);  // "String index out of range: 5"
    JLANG_THROWABLE(StringIndexOutOfBoundsException)
};

// ---- java.util / java.util.concurrent
JLANG_DECLARE_EXCEPTION(NoSuchElementException, RuntimeException);
JLANG_DECLARE_EXCEPTION(ConcurrentModificationException, RuntimeException);
JLANG_DECLARE_EXCEPTION(ExecutionException, Exception);
JLANG_DECLARE_EXCEPTION(TimeoutException, Exception);
JLANG_DECLARE_EXCEPTION(RejectedExecutionException, RuntimeException);
JLANG_DECLARE_EXCEPTION(CancellationException, IllegalStateException);
JLANG_DECLARE_EXCEPTION(BrokenBarrierException, Exception);

// ---- java.io / java.nio / java.net
JLANG_DECLARE_EXCEPTION(IOException, Exception);
JLANG_DECLARE_EXCEPTION(FileNotFoundException, IOException);
JLANG_DECLARE_EXCEPTION(UnsupportedEncodingException, IOException);
JLANG_DECLARE_EXCEPTION(EOFException, IOException);
JLANG_DECLARE_EXCEPTION(UncheckedIOException, RuntimeException);
JLANG_DECLARE_EXCEPTION(BufferUnderflowException, RuntimeException);
JLANG_DECLARE_EXCEPTION(BufferOverflowException, RuntimeException);
JLANG_DECLARE_EXCEPTION(ClosedChannelException, IOException);
JLANG_DECLARE_EXCEPTION(CancelledKeyException, IllegalStateException);
JLANG_DECLARE_EXCEPTION(UnknownHostException, IOException);
JLANG_DECLARE_EXCEPTION(SocketException, IOException);
JLANG_DECLARE_EXCEPTION(SocketTimeoutException, IOException);

// ---- java.security
JLANG_DECLARE_EXCEPTION(GeneralSecurityException, Exception);
JLANG_DECLARE_EXCEPTION(NoSuchAlgorithmException, GeneralSecurityException);

// ---- java.sql.SQLException (reason, SQLState, vendor error code)
class SQLException : public Exception {
public:
    SQLException() {}
    explicit SQLException(const String& reason) : Exception(reason) {}
    explicit SQLException(const char* reason) : Exception(String(reason)) {}
    SQLException(const String& reason, const String& sqlState) : Exception(reason), state_(sqlState) {}
    SQLException(const String& reason, const String& sqlState, int32_t vendorCode)
        : Exception(reason), state_(sqlState), code_(vendorCode) {}
    SQLException(const String& reason, const Throwable& cause) : Exception(reason, cause) {}
    SQLException(const String& reason, Throwable* cause) : Exception(reason, cause) {}
    SQLException(const String& reason, const String& sqlState, int32_t vendorCode, Throwable* cause)
        : Exception(reason, cause), state_(sqlState), code_(vendorCode) {}
    explicit SQLException(Throwable* cause) : Exception(cause) {}
    template<class E>
        requires(std::is_base_of_v<Throwable, E> && !std::is_same_v<E, SQLException>)
    explicit SQLException(const E& cause) : Exception(static_cast<const Throwable&>(cause), WrapTag{}) {}
    int32_t getErrorCode() { return code_; }
    String getSQLState() { return *state_; }
    JLANG_THROWABLE(SQLException)

private:
    detail::Pinned<String> state_;
    int32_t code_ = 0;
};

// ---- java.text.ParseException (message, errorOffset)
class ParseException : public Exception {
public:
    ParseException(const String& message, int32_t errorOffset) : Exception(message), offset_(errorOffset) {}
    int32_t getErrorOffset() { return offset_; }
    JLANG_THROWABLE(ParseException)

private:
    int32_t offset_ = 0;
};

// ---- java.lang.StackTraceElement (minimal)
class StackTraceElement : public virtual Object {
public:
    StackTraceElement(const String& declaringClass, const String& methodName, const String& fileName,
                      int32_t lineNumber)
        : cls_(declaringClass), method_(methodName), file_(fileName), line_(lineNumber) {}
    String getClassName() { return cls_; }
    String getMethodName() { return method_; }
    String getFileName() { return file_; }
    int32_t getLineNumber() { return line_; }
    String toString() override;

private:
    String cls_, method_, file_;
    int32_t line_;
};

// NumberFormatException factories with Java's messages.
namespace detail {
[[noreturn]] void throwNumberFormatForInput(const String& s);             // For input string: "s"
[[noreturn]] void throwNumberFormatForInput(const String& s, int32_t radix);  // ... under radix r
}  // namespace detail

}  // namespace jlang
