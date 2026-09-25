// jlang/src/netty_internal.h - private helpers shared by the jlang/src/netty*.cpp files.
#pragma once

#include <jlang/Netty.h>

#include <initializer_list>

namespace jlang::netty::detail {

// ---- byte order helpers (jlang::ByteOrder is a value class from <jlang/Nio.h>)
inline ::jlang::ByteOrder bigEndian() { return ::jlang::ByteOrder::BIG_ENDIAN; }
inline ::jlang::ByteOrder littleEndian() { return ::jlang::ByteOrder::LITTLE_ENDIAN; }
inline bool isLittle(::jlang::ByteOrder o) { return o == ::jlang::ByteOrder::LITTLE_ENDIAN; }
inline bool isNullOrder(::jlang::ByteOrder o) { return o == nullptr; }

// ---- boxed values
::jlang::Object* boxBool(bool v);
::jlang::Object* boxInt(int32_t v);
bool toBoolean(::jlang::Object* v);   // Netty ConversionUtil.toBoolean
int32_t toInt(::jlang::Object* v);    // Netty ConversionUtil.toInt
bool isTrue(::jlang::Object* v);      // Boolean.TRUE.equals(v)
bool isFalse(::jlang::Object* v);     // Boolean.FALSE.equals(v)

// ---- logging (Netty's InternalLogger; goes to the jlang logger of the given Java class name)
void logWarn(const char* loggerName, const ::jlang::String& msg, ::jlang::Throwable* t = nullptr);
void logDebug(const char* loggerName, const ::jlang::String& msg, ::jlang::Throwable* t = nullptr);

// ---- exceptions: a GC copy of a caught exception
::jlang::Throwable* copyOf(const ::jlang::Throwable& t);
::jlang::Throwable* copyOf(const std::exception& e);

// ---- I/O thread flag (IoWorkerRunnable.IN_IO_THREAD)
bool inIoThread();
void setInIoThread(bool v);

// Monotonic nanoseconds (System.nanoTime()).
int64_t nanoTime();

// ---- executors (org.jboss.netty.util.internal.ExecutorUtil)
// shutdownNow() + awaitTermination(100 ms) until terminated, for every ExecutorService.
void terminateExecutors(std::initializer_list<::jlang::Executor*> executors);
bool isShutdown(::jlang::Executor* executor);

}  // namespace jlang::netty::detail
