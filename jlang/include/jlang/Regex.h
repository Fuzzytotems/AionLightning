// jlang/Regex.h - java.util.regex.Pattern / Matcher / PatternSyntaxException.
//
//   java.util.regex.Pattern -> jlang::Pattern*     java.util.regex.Matcher -> jlang::Matcher*
//
// Java regex syntax is translated to PCRE2 (UTF-8 mode) and matched with Java's Matcher
// semantics: matches() must consume the whole region (with backtracking), lookingAt() is
// anchored at the region start, find() continues after the previous match (one character
// further after an empty match), replaceAll/replaceFirst/appendReplacement expand "$n",
// "${name}" and "\x" exactly like Java (same exceptions), split() follows Pattern.split (no
// leading empty string for a zero-width match at 0, trailing empty strings removed for
// limit 0).
//
// Translation covers the Java dialect: '.' '^' '$' '\Z' with Java's line terminators and the
// UNIX_LINES/MULTILINE/DOTALL flags (also inline (?d)(?m)(?s)), nested character classes
// with union [a[b]] and intersection [a&&[^b]] (negation applies to the whole class, Java 9+),
// POSIX \p{Alpha}... (ASCII), Unicode categories/scripts \p{Lu} \p{IsLatin} \p{L}, \Q..\E,
// \uXXXX, \x{h..h}, \0ooo, named groups, possessive/lazy quantifiers, lookaround, atomic
// groups, back references (a reference to a group that does not exist never matches, as in
// Java), a curly quantifier directly after another quantifier (Java applies it to an empty
// node), COMMENTS mode and LITERAL.
//
// INDICES ARE UTF-8 BYTE OFFSETS, consistent with jlang::String (start(), end(), region(),
// find(int)). For ASCII text they equal Java's.
//
// Deviations: CASE_INSENSITIVE also folds non-ASCII letters (Java needs UNICODE_CASE for
// that); \b{g}, canonical equivalence (CANON_EQ) and Unicode block names (\p{InGreek} is
// matched as the script Greek) are approximations; lookbehind must have a bounded length per
// alternative (PCRE2 rule). Pattern compilation errors are reported as PatternSyntaxException
// with Java's description where the translator detects them, PCRE2's otherwise.
#pragma once

#include <jlang/Array.h>
#include <jlang/Exceptions.h>
#include <jlang/Object.h>
#include <jlang/String.h>

#include <cstdint>
#include <vector>

namespace jlang {

class Matcher;

// java.util.regex.PatternSyntaxException
class PatternSyntaxException : public IllegalArgumentException {
public:
    PatternSyntaxException(const String& desc, const String& regex, int32_t index)
        : IllegalArgumentException(desc), desc_(desc), pattern_(regex), index_(index) {}
    String getDescription() { return *desc_; }
    int32_t getIndex() { return index_; }
    String getPattern() { return *pattern_; }
    // "desc near index n\npattern\n    ^"
    String getMessage() override;
    String className() const override { return String("java.util.regex.PatternSyntaxException"); }
    JLANG_THROWABLE(PatternSyntaxException)

private:
    detail::Pinned<String> desc_;
    detail::Pinned<String> pattern_;
    int32_t index_;
};

// java.util.regex.Pattern (immutable, thread safe)
class Pattern final : public virtual Object {
public:
    static constexpr int32_t UNIX_LINES = 0x01;
    static constexpr int32_t CASE_INSENSITIVE = 0x02;
    static constexpr int32_t COMMENTS = 0x04;
    static constexpr int32_t MULTILINE = 0x08;
    static constexpr int32_t LITERAL = 0x10;
    static constexpr int32_t DOTALL = 0x20;
    static constexpr int32_t UNICODE_CASE = 0x40;
    static constexpr int32_t CANON_EQ = 0x80;
    static constexpr int32_t UNICODE_CHARACTER_CLASS = 0x100;

    static Pattern* compile(const String& regex);
    static Pattern* compile(const String& regex, int32_t flags);
    // Pattern.matches(regex, input) == compile(regex).matcher(input).matches()
    static bool matches(const String& regex, const String& input);
    // "\Q...\E" quoting (Java's algorithm, handles embedded "\E").
    static String quote(const String& s);

    Matcher* matcher(const String& input);
    String pattern() { return pattern_; }
    int32_t flags() { return flags_; }
    String toString() override { return pattern_; }
    Array<String>* split(const String& input);
    Array<String>* split(const String& input, int32_t limit);

    // jlang internals
    int32_t groupCount() const noexcept { return groupCount_; }
    int32_t groupIndex(const String& name);  // -1 if no such named group
    void* code() const noexcept { return code_; }
    // The translated PCRE2 pattern (for tests/diagnostics).
    String translated() { return translated_; }

private:
    Pattern(const String& regex, int32_t flags);
    String pattern_;
    int32_t flags_;
    String translated_;
    void* code_ = nullptr;  // pcre2_code_8 (GC allocated)
    int32_t groupCount_ = 0;
    std::vector<std::pair<std::string, int32_t>> names_;
};

// java.util.regex.Matcher (not thread safe, like Java)
class Matcher final : public virtual Object {
public:
    Pattern* pattern() { return pattern_; }
    Matcher* usePattern(Pattern* newPattern);
    Matcher* reset();
    Matcher* reset(const String& input);
    Matcher* region(int32_t start, int32_t end);
    int32_t regionStart() { return from_; }
    int32_t regionEnd() { return to_; }

    bool matches();
    bool lookingAt();
    bool find();
    bool find(int32_t start);  // resets, then searches from start (IndexOutOfBoundsException)
    bool hitEnd() { return hitEnd_; }

    // IllegalStateException("No match found") without a current match;
    // IndexOutOfBoundsException("No group n"); IllegalArgumentException for unknown names.
    String group();
    String group(int32_t group);
    String group(const String& name);
    int32_t groupCount() { return pattern_->groupCount(); }
    int32_t start();
    int32_t start(int32_t group);
    int32_t start(const String& name);
    int32_t end();
    int32_t end(int32_t group);
    int32_t end(const String& name);

    String replaceAll(const String& replacement);
    String replaceFirst(const String& replacement);
    Matcher* appendReplacement(StringBuilder* sb, const String& replacement);
    StringBuilder* appendTail(StringBuilder* sb);
    static String quoteReplacement(const String& s);

    // "java.util.regex.Matcher[pattern=p region=0,5 lastmatch=abc]"
    String toString() override;

    Matcher(Pattern* p, const String& input);  // use Pattern::matcher

private:
    bool search(int32_t from, int mode);  // mode: 0 find, 1 lookingAt, 2 matches
    void clearGroups();
    void ensureMatch();
    String expandReplacement(const String& replacement);
    int32_t nextCharBoundary(int32_t i);

    Pattern* pattern_;
    String text_;
    int32_t from_ = 0;
    int32_t to_ = 0;
    int32_t first_ = -1;  // start of the last match (-1: none)
    int32_t last_ = 0;    // end of the last match
    int32_t lastAppend_ = 0;
    bool hitEnd_ = false;
    std::vector<int32_t> groups_;  // 2 * (groupCount + 1) byte offsets, -1 = unset
    void* matchData_ = nullptr;    // pcre2_match_data_8 (GC allocated)
};

}  // namespace jlang
