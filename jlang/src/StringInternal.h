// Internal helpers shared by the jlang string implementation files (not installed).
#pragma once

#include <jlang/String.h>

#include <cstddef>
#include <cstdint>
#include <regex>
#include <string>
#include <vector>

namespace jlang {

namespace utf {
// Decodes the code point at byte i (advancing i). Invalid sequences give U+FFFD (1 byte).
// Encoded surrogates (WTF-8, used for lone UTF-16 surrogates) decode to themselves.
char32_t decode(const char* p, size_t n, size_t& i) noexcept;
void encode(std::string& out, char32_t cp);
void appendUnit(std::string& out, char16_t c);
bool isAscii(const std::string& s) noexcept;
int32_t utf16Length(const char* p, size_t n) noexcept;
}  // namespace utf

namespace detail {
const std::regex& compiledRegex(const String& javaRegex);
void appendJavaReplacement(std::string& out, const std::smatch& m, const std::string& repl);
std::vector<String> splitToVector(const String& s, const String& regex, int32_t limit);
}  // namespace detail

}  // namespace jlang
