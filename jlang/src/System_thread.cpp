// Runtime.addShutdownHook(Thread) / removeShutdownHook(Thread): kept apart from System.cpp so
// the core does not depend on <jlang/Thread.h> (a jlang::Thread is a Runnable).
#include <jlang/System.h>

#if __has_include(<jlang/Thread.h>)
#include <jlang/Thread.h>

namespace jlang {

void Runtime::addShutdownHook(Thread* hook) {
    if (hook == nullptr) throw NullPointerException();
    addShutdownHook(static_cast<Runnable*>(hook));
}

bool Runtime::removeShutdownHook(Thread* hook) {
    if (hook == nullptr) throw NullPointerException();
    return removeShutdownHook(static_cast<Runnable*>(hook));
}

}  // namespace jlang
#endif
