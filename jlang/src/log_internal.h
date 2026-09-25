// Private helpers shared by the log4j emulation sources (jlang/src/log*.cpp).
#pragma once

#include <jlang/Log.h>

#include <cstdint>
#include <string>
#include <vector>

namespace jlang::log4j::impl {

// "void org::openaion::X::run()" -> cls "org.openaion.X", method "run".
void parseFunctionName(const char* fn, String& cls, String& method);

// A Throwable that may be kept after the logging call returns: the object itself when it lives
// on the GC heap, otherwise a GC copy (caught exceptions live in C++ exception storage).
Throwable* retainThrowable(Throwable* t);

// Java printStackTrace() lines (toString, "\tat" frames, "Caused by:" chain with "... n more").
void renderThrowable(Throwable* t, std::vector<String>& out);

// OptionConverter.toLevel(value, default): "NULL" -> nullptr, "level#class" -> default + warning.
Level* toLevel(const String& value, Level* defaultValue);

// OptionConverter.toBoolean(value, default)
bool toBoolean(const String& value, bool defaultValue);

// OptionConverter.toFileSize(value, default): "10MB", "500KB", "1GB", "1234".
int64_t toFileSize(const String& value, int64_t defaultValue);

// OptionConverter.substVars with System properties (IllegalArgumentException for a "${" without "}").
String substVars(const String& val);

// OptionConverter.convertSpecialChars: \\n \\r \\t \\f \\b \\" \\' \\\\ escapes.
String convertSpecialChars(const String& s);

// Name of the calling thread (Thread.currentThread().getName()).
String currentThreadName();

// ---------------------------------------------------------------------------------------
// java.text.SimpleDateFormat subset used by %d (US English symbols, Gregorian calendar,
// first day of week Sunday, minimal days in first week 1). Throws IllegalArgumentException
// for an illegal pattern (like SimpleDateFormat's constructor).
class DateFormatter {
public:
    explicit DateFormatter(const std::string& pattern);
    // Formats epoch milliseconds in the local time zone, or with a fixed offset (minutes east
    // of UTC) and zone name when setFixedZone was called.
    void format(int64_t millis, std::string& out) const;
    void setFixedZone(int32_t offsetMinutes, const std::string& name);

private:
    struct Token {
        char letter;  // 0 = literal
        int count;
        std::string text;
    };
    std::vector<Token> tokens_;
    bool fixedZone_ = false;
    int32_t fixedOffset_ = 0;
    std::string fixedName_;
};

}  // namespace jlang::log4j::impl
