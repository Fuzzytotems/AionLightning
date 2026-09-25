// jlang/src/netty_util.cpp - shared helpers of the Netty port (logging through log4j).
#include "netty_internal.h"

#include <jlang/Log.h>

namespace jlang::netty::detail {

// Netty logs through its InternalLogger (java.util.logging, routed to log4j by the
// servers); here directly to the jlang log4j logger of the Netty class.
void logWarn(const char* loggerName, const ::jlang::String& msg, ::jlang::Throwable* t) {
    ::jlang::Logger* log = ::jlang::Logger::getLogger(loggerName);
    if (t != nullptr) {
        log->warn(msg, t);
    } else {
        log->warn(msg);
    }
}

void logDebug(const char* loggerName, const ::jlang::String& msg, ::jlang::Throwable* t) {
    ::jlang::Logger* log = ::jlang::Logger::getLogger(loggerName);
    if (!log->isDebugEnabled()) return;
    if (t != nullptr) {
        log->debug(msg, t);
    } else {
        log->debug(msg);
    }
}

}  // namespace jlang::netty::detail
