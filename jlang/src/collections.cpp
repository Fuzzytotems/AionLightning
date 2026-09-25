// Non-template parts of the jlang collections: GC storage, exception helpers with Java's
// messages, Array::toString, Collections' shared Random and java.util.BitSet.
#include <jlang/jlang.h>

#include <atomic>
#include <bit>
#include <cstdio>
#include <cstdlib>
#include <cxxabi.h>
#include <string>
#include <typeinfo>

namespace jlang::detail {

// ---------------------------------------------------------------------------------------
// GC storage
void* collMalloc(std::size_t n) {
    return ::operator new(n);  // GC_MALLOC: scanned, zero-filled (see gc_new.cpp)
}

void* collMallocAtomic(std::size_t n) {
    void* p = gc::allocAtomic(n);
    if (p == nullptr) throw std::bad_alloc();
    return p;
}

// ---------------------------------------------------------------------------------------
// Object subobject of a pointer to an incomplete polymorphic class (Itanium C++ ABI).
//
// Every polymorphic subobject starts with a vptr; the vtable holds, at [-1], the type_info of
// the complete object and, at [-2], the offset from this subobject to the complete object.
// The type_info's __do_upcast (the catch-clause matching of the C++ runtime) then finds the
// unique Object subobject of the complete object. The displacement only depends on the vtable
// (dynamic type + subobject), so it is cached per vptr in a small lock-free table.
namespace {
struct VtDelta {
    const void* vptr;
    std::ptrdiff_t delta;
};
constexpr std::size_t kVtCacheSize = 4096;  // power of two
constexpr std::size_t kVtProbe = 32;
std::atomic<VtDelta*> g_vtCache[kVtCacheSize];

inline std::size_t vtHome(const void* vptr) {
    const uint64_t a = static_cast<uint64_t>(reinterpret_cast<uintptr_t>(vptr));
    return static_cast<std::size_t>((a * UINT64_C(0x9E3779B97F4A7C15)) >> 52) & (kVtCacheSize - 1);
}
}  // namespace

Object* objectOfIncomplete(const void* p) {
    const void* vptr = *static_cast<const void* const*>(p);
    std::size_t i = vtHome(vptr);
    for (std::size_t n = 0; n < kVtProbe; n++, i = (i + 1) & (kVtCacheSize - 1)) {
        const VtDelta* e = g_vtCache[i].load(std::memory_order_acquire);
        if (e == nullptr) break;
        if (e->vptr == vptr) {
            return static_cast<Object*>(const_cast<void*>(static_cast<const void*>(static_cast<const char*>(p) + e->delta)));
        }
    }
    const auto* vtable = static_cast<const std::ptrdiff_t*>(vptr);
    const std::ptrdiff_t offsetToTop = vtable[-2];
    const auto* dynamicType = static_cast<const std::type_info* const*>(vptr)[-1];
    // Upcast from the complete object to its jlang::Object base, the way a thrown object is
    // matched against `catch (jlang::Object&)` (handles virtual bases).
    void* obj = const_cast<char*>(static_cast<const char*>(p) + offsetToTop);
    if (!dynamicType->__do_upcast(static_cast<const abi::__class_type_info*>(&typeid(Object)), &obj)) obj = nullptr;
    if (obj == nullptr) {
        throw ClassCastException(String(std::string(dynamicType->name()) + " is not a jlang::Object"));
    }
    // Cache (plain malloc: the entry holds no GC pointers and lives forever).
    auto* entry = static_cast<VtDelta*>(std::malloc(sizeof(VtDelta)));
    if (entry != nullptr) {
        entry->vptr = vptr;
        entry->delta = static_cast<const char*>(obj) - static_cast<const char*>(p);
        i = vtHome(vptr);
        for (std::size_t n = 0; n < kVtProbe; n++, i = (i + 1) & (kVtCacheSize - 1)) {
            VtDelta* expected = nullptr;
            if (g_vtCache[i].compare_exchange_strong(expected, entry, std::memory_order_acq_rel)) break;
            if (expected->vptr == vptr) {
                std::free(entry);
                break;
            }
        }
    }
    return static_cast<Object*>(obj);
}

// ---------------------------------------------------------------------------------------
// Exceptions
void throwIndexOutOfBounds(int32_t index, int32_t size) {
    throw IndexOutOfBoundsException(String("Index: " + std::to_string(index) + ", Size: " + std::to_string(size)));
}
void throwIndexOutOfBoundsMsg(const std::string& msg) { throw IndexOutOfBoundsException(String(msg)); }
void throwArrayIndexOutOfBounds(int32_t index) { throw ArrayIndexOutOfBoundsException(String(std::to_string(index))); }
void throwArrayIndexOutOfBoundsMsg(const std::string& msg) { throw ArrayIndexOutOfBoundsException(String(msg)); }
void throwArrayIndexOutOfBoundsNoMsg() { throw ArrayIndexOutOfBoundsException(); }
void throwNegativeArraySize(int32_t n) { throw NegativeArraySizeException(String(std::to_string(n))); }
void throwNoSuchElement() { throw NoSuchElementException(); }
void throwNoSuchElementMsg(const std::string& msg) { throw NoSuchElementException(String(msg)); }
void throwIllegalState() { throw IllegalStateException(); }
void throwIllegalStateMsg(const std::string& msg) { throw IllegalStateException(String(msg)); }
void throwIllegalArgument(const std::string& msg) { throw IllegalArgumentException(String(msg)); }
void throwIllegalArgumentNoMsg() { throw IllegalArgumentException(); }
void throwUnsupportedOperation() { throw UnsupportedOperationException(); }
void throwUnsupportedOperationMsg(const std::string& msg) { throw UnsupportedOperationException(String(msg)); }
void throwNullPointer() { throw NullPointerException(); }
void throwClassCastNotComparable(Object* o) {
    throw ClassCastException(o->getClass()->getName() + " cannot be cast to java.lang.Comparable");
}
void throwClassCastMsg(const std::string& msg) { throw ClassCastException(String(msg)); }
void throwTroveIndex(int32_t index) { throw ArrayIndexOutOfBoundsException(index); }
void throwViewIndexRemoval() {
    throw UnsupportedOperationException(String(
        "remove(int index) on a map view (keySet()/values()/entrySet()): Java's views have no index "
        "removal; use removeObject(x) for remove(Object)"));
}

// ---------------------------------------------------------------------------------------
String arrayToString(const char* typeCode, int32_t identityHash) {
    char buf[16];
    std::snprintf(buf, sizeof buf, "%x", static_cast<unsigned>(identityHash));
    return String(std::string(typeCode) + "@" + buf);
}

Random* collectionsRandom() {
    static Random* const r = new Random();
    return r;
}

}  // namespace jlang::detail

namespace jlang {

// ---------------------------------------------------------------------------------------
// java.util.BitSet (port of the JDK 6/7 implementation)
namespace {
constexpr int32_t kBitsPerWord = 64;
constexpr uint64_t kWordMask = ~UINT64_C(0);

inline uint64_t shl(uint64_t v, int32_t n) { return v << (n & 63); }
inline uint64_t ushr(uint64_t v, int32_t n) { return v >> (n & 63); }

void checkIndex(int32_t bitIndex) {
    if (bitIndex < 0) throw IndexOutOfBoundsException(String("bitIndex < 0: " + std::to_string(bitIndex)));
}
void checkFrom(int32_t fromIndex) {
    if (fromIndex < 0) throw IndexOutOfBoundsException(String("fromIndex < 0: " + std::to_string(fromIndex)));
}
void checkRange(int32_t fromIndex, int32_t toIndex) {
    checkFrom(fromIndex);
    if (toIndex < 0) throw IndexOutOfBoundsException(String("toIndex < 0: " + std::to_string(toIndex)));
    if (fromIndex > toIndex) {
        throw IndexOutOfBoundsException(
            String("fromIndex: " + std::to_string(fromIndex) + " > toIndex: " + std::to_string(toIndex)));
    }
}
}  // namespace

BitSet::BitSet() : words_(1, 0) { sizeIsSticky_ = false; }

BitSet::BitSet(int32_t nbits) {
    if (nbits < 0) throw NegativeArraySizeException(String("nbits < 0: " + std::to_string(nbits)));
    words_.assign(static_cast<std::size_t>(wordIndex(nbits - 1) + 1), 0);
    sizeIsSticky_ = true;
}

void BitSet::recalculateWordsInUse() {
    int32_t i;
    for (i = wordsInUse_ - 1; i >= 0; i--) {
        if (words_[static_cast<std::size_t>(i)] != 0) break;
    }
    wordsInUse_ = i + 1;
}

void BitSet::ensureCapacity(int32_t wordsRequired) {
    const int32_t len = static_cast<int32_t>(words_.size());
    if (len < wordsRequired) {
        const int32_t request = std::max(2 * len, wordsRequired);
        words_.resize(static_cast<std::size_t>(request), 0);
        sizeIsSticky_ = false;
    }
}

void BitSet::expandTo(int32_t wordIndex) {
    const int32_t wordsRequired = wordIndex + 1;
    if (wordsInUse_ < wordsRequired) {
        ensureCapacity(wordsRequired);
        wordsInUse_ = wordsRequired;
    }
}

void BitSet::trimToSize() {
    if (wordsInUse_ != static_cast<int32_t>(words_.size())) words_.resize(static_cast<std::size_t>(wordsInUse_));
}

bool BitSet::get(int32_t bitIndex) {
    checkIndex(bitIndex);
    const int32_t wi = wordIndex(bitIndex);
    return wi < wordsInUse_ && (static_cast<uint64_t>(words_[static_cast<std::size_t>(wi)]) & shl(1, bitIndex)) != 0;
}

BitSet* BitSet::get(int32_t fromIndex, int32_t toIndex) {
    checkRange(fromIndex, toIndex);
    const int32_t len = length();
    if (len <= fromIndex || fromIndex == toIndex) return new BitSet(0);
    if (toIndex > len) toIndex = len;
    auto* result = new BitSet(toIndex - fromIndex);
    const int32_t targetWords = wordIndex(toIndex - fromIndex - 1) + 1;
    int32_t sourceIndex = wordIndex(fromIndex);
    const bool wordAligned = (fromIndex & 0x3f) == 0;
    auto w = [&](int32_t i) { return static_cast<uint64_t>(words_[static_cast<std::size_t>(i)]); };
    for (int32_t i = 0; i < targetWords - 1; i++, sourceIndex++) {
        result->words_[static_cast<std::size_t>(i)] = static_cast<int64_t>(
            wordAligned ? w(sourceIndex) : (ushr(w(sourceIndex), fromIndex) | shl(w(sourceIndex + 1), -fromIndex)));
    }
    const uint64_t lastWordMask = ushr(kWordMask, -toIndex);
    result->words_[static_cast<std::size_t>(targetWords - 1)] = static_cast<int64_t>(
        ((toIndex - 1) & 0x3f) < (fromIndex & 0x3f)
            ? (ushr(w(sourceIndex), fromIndex) | shl(w(sourceIndex + 1) & lastWordMask, -fromIndex))
            : ushr(w(sourceIndex) & lastWordMask, fromIndex));
    result->wordsInUse_ = targetWords;
    result->recalculateWordsInUse();
    return result;
}

void BitSet::set(int32_t bitIndex) {
    checkIndex(bitIndex);
    const int32_t wi = wordIndex(bitIndex);
    expandTo(wi);
    words_[static_cast<std::size_t>(wi)] |= static_cast<int64_t>(shl(1, bitIndex));
}

void BitSet::set(int32_t bitIndex, bool value) {
    if (value) set(bitIndex);
    else clear(bitIndex);
}

void BitSet::set(int32_t fromIndex, int32_t toIndex) {
    checkRange(fromIndex, toIndex);
    if (fromIndex == toIndex) return;
    const int32_t startWordIndex = wordIndex(fromIndex);
    const int32_t endWordIndex = wordIndex(toIndex - 1);
    expandTo(endWordIndex);
    const uint64_t firstWordMask = shl(kWordMask, fromIndex);
    const uint64_t lastWordMask = ushr(kWordMask, -toIndex);
    auto& w = words_;
    if (startWordIndex == endWordIndex) {
        w[static_cast<std::size_t>(startWordIndex)] |= static_cast<int64_t>(firstWordMask & lastWordMask);
    } else {
        w[static_cast<std::size_t>(startWordIndex)] |= static_cast<int64_t>(firstWordMask);
        for (int32_t i = startWordIndex + 1; i < endWordIndex; i++) w[static_cast<std::size_t>(i)] = -1;
        w[static_cast<std::size_t>(endWordIndex)] |= static_cast<int64_t>(lastWordMask);
    }
}

void BitSet::set(int32_t fromIndex, int32_t toIndex, bool value) {
    if (value) set(fromIndex, toIndex);
    else clear(fromIndex, toIndex);
}

void BitSet::clear(int32_t bitIndex) {
    checkIndex(bitIndex);
    const int32_t wi = wordIndex(bitIndex);
    if (wi >= wordsInUse_) return;
    words_[static_cast<std::size_t>(wi)] &= ~static_cast<int64_t>(shl(1, bitIndex));
    recalculateWordsInUse();
}

void BitSet::clear(int32_t fromIndex, int32_t toIndex) {
    checkRange(fromIndex, toIndex);
    if (fromIndex == toIndex) return;
    const int32_t startWordIndex = wordIndex(fromIndex);
    if (startWordIndex >= wordsInUse_) return;
    int32_t endWordIndex = wordIndex(toIndex - 1);
    if (endWordIndex >= wordsInUse_) {
        toIndex = length();
        endWordIndex = wordsInUse_ - 1;
    }
    const uint64_t firstWordMask = shl(kWordMask, fromIndex);
    const uint64_t lastWordMask = ushr(kWordMask, -toIndex);
    auto& w = words_;
    if (startWordIndex == endWordIndex) {
        w[static_cast<std::size_t>(startWordIndex)] &= ~static_cast<int64_t>(firstWordMask & lastWordMask);
    } else {
        w[static_cast<std::size_t>(startWordIndex)] &= ~static_cast<int64_t>(firstWordMask);
        for (int32_t i = startWordIndex + 1; i < endWordIndex; i++) w[static_cast<std::size_t>(i)] = 0;
        w[static_cast<std::size_t>(endWordIndex)] &= ~static_cast<int64_t>(lastWordMask);
    }
    recalculateWordsInUse();
}

void BitSet::clear() {
    while (wordsInUse_ > 0) words_[static_cast<std::size_t>(--wordsInUse_)] = 0;
}

void BitSet::flip(int32_t bitIndex) {
    checkIndex(bitIndex);
    const int32_t wi = wordIndex(bitIndex);
    expandTo(wi);
    words_[static_cast<std::size_t>(wi)] ^= static_cast<int64_t>(shl(1, bitIndex));
    recalculateWordsInUse();
}

void BitSet::flip(int32_t fromIndex, int32_t toIndex) {
    checkRange(fromIndex, toIndex);
    if (fromIndex == toIndex) return;
    const int32_t startWordIndex = wordIndex(fromIndex);
    const int32_t endWordIndex = wordIndex(toIndex - 1);
    expandTo(endWordIndex);
    const uint64_t firstWordMask = shl(kWordMask, fromIndex);
    const uint64_t lastWordMask = ushr(kWordMask, -toIndex);
    auto& w = words_;
    if (startWordIndex == endWordIndex) {
        w[static_cast<std::size_t>(startWordIndex)] ^= static_cast<int64_t>(firstWordMask & lastWordMask);
    } else {
        w[static_cast<std::size_t>(startWordIndex)] ^= static_cast<int64_t>(firstWordMask);
        for (int32_t i = startWordIndex + 1; i < endWordIndex; i++) w[static_cast<std::size_t>(i)] ^= -1;
        w[static_cast<std::size_t>(endWordIndex)] ^= static_cast<int64_t>(lastWordMask);
    }
    recalculateWordsInUse();
}

int32_t BitSet::nextSetBit(int32_t fromIndex) {
    checkFrom(fromIndex);
    int32_t u = wordIndex(fromIndex);
    if (u >= wordsInUse_) return -1;
    uint64_t word = static_cast<uint64_t>(words_[static_cast<std::size_t>(u)]) & shl(kWordMask, fromIndex);
    while (true) {
        if (word != 0) return u * kBitsPerWord + std::countr_zero(word);
        if (++u == wordsInUse_) return -1;
        word = static_cast<uint64_t>(words_[static_cast<std::size_t>(u)]);
    }
}

int32_t BitSet::nextClearBit(int32_t fromIndex) {
    checkFrom(fromIndex);
    int32_t u = wordIndex(fromIndex);
    if (u >= wordsInUse_) return fromIndex;
    uint64_t word = ~static_cast<uint64_t>(words_[static_cast<std::size_t>(u)]) & shl(kWordMask, fromIndex);
    while (true) {
        if (word != 0) return u * kBitsPerWord + std::countr_zero(word);
        if (++u == wordsInUse_) return wordsInUse_ * kBitsPerWord;
        word = ~static_cast<uint64_t>(words_[static_cast<std::size_t>(u)]);
    }
}

int32_t BitSet::previousSetBit(int32_t fromIndex) {
    if (fromIndex < 0) {
        if (fromIndex == -1) return -1;
        throw IndexOutOfBoundsException(String("fromIndex < -1: " + std::to_string(fromIndex)));
    }
    int32_t u = wordIndex(fromIndex);
    if (u >= wordsInUse_) return length() - 1;
    uint64_t word = static_cast<uint64_t>(words_[static_cast<std::size_t>(u)]) & ushr(kWordMask, -(fromIndex + 1));
    while (true) {
        if (word != 0) return (u + 1) * kBitsPerWord - 1 - std::countl_zero(word);
        if (u-- == 0) return -1;
        word = static_cast<uint64_t>(words_[static_cast<std::size_t>(u)]);
    }
}

int32_t BitSet::previousClearBit(int32_t fromIndex) {
    if (fromIndex < 0) {
        if (fromIndex == -1) return -1;
        throw IndexOutOfBoundsException(String("fromIndex < -1: " + std::to_string(fromIndex)));
    }
    int32_t u = wordIndex(fromIndex);
    if (u >= wordsInUse_) return fromIndex;
    uint64_t word = ~static_cast<uint64_t>(words_[static_cast<std::size_t>(u)]) & ushr(kWordMask, -(fromIndex + 1));
    while (true) {
        if (word != 0) return (u + 1) * kBitsPerWord - 1 - std::countl_zero(word);
        if (u-- == 0) return -1;
        word = ~static_cast<uint64_t>(words_[static_cast<std::size_t>(u)]);
    }
}

int32_t BitSet::length() {
    if (wordsInUse_ == 0) return 0;
    return kBitsPerWord * (wordsInUse_ - 1) +
           (kBitsPerWord - std::countl_zero(static_cast<uint64_t>(words_[static_cast<std::size_t>(wordsInUse_ - 1)])));
}

int32_t BitSet::size() { return static_cast<int32_t>(words_.size()) * kBitsPerWord; }

bool BitSet::isEmpty() { return wordsInUse_ == 0; }

int32_t BitSet::cardinality() {
    int32_t sum = 0;
    for (int32_t i = 0; i < wordsInUse_; i++) sum += std::popcount(static_cast<uint64_t>(words_[static_cast<std::size_t>(i)]));
    return sum;
}

bool BitSet::intersects(BitSet* set) {
    if (set == nullptr) detail::throwNullPointer();
    for (int32_t i = std::min(wordsInUse_, set->wordsInUse_) - 1; i >= 0; i--) {
        if ((words_[static_cast<std::size_t>(i)] & set->words_[static_cast<std::size_t>(i)]) != 0) return true;
    }
    return false;
}

void BitSet::and_(BitSet* set) {
    if (set == nullptr) detail::throwNullPointer();
    if (this == set) return;
    while (wordsInUse_ > set->wordsInUse_) words_[static_cast<std::size_t>(--wordsInUse_)] = 0;
    for (int32_t i = 0; i < wordsInUse_; i++) words_[static_cast<std::size_t>(i)] &= set->words_[static_cast<std::size_t>(i)];
    recalculateWordsInUse();
}

void BitSet::or_(BitSet* set) {
    if (set == nullptr) detail::throwNullPointer();
    if (this == set) return;
    const int32_t wordsInCommon = std::min(wordsInUse_, set->wordsInUse_);
    if (wordsInUse_ < set->wordsInUse_) {
        ensureCapacity(set->wordsInUse_);
        wordsInUse_ = set->wordsInUse_;
    }
    for (int32_t i = 0; i < wordsInCommon; i++) words_[static_cast<std::size_t>(i)] |= set->words_[static_cast<std::size_t>(i)];
    for (int32_t i = wordsInCommon; i < set->wordsInUse_; i++) words_[static_cast<std::size_t>(i)] = set->words_[static_cast<std::size_t>(i)];
}

void BitSet::xor_(BitSet* set) {
    if (set == nullptr) detail::throwNullPointer();
    const int32_t wordsInCommon = std::min(wordsInUse_, set->wordsInUse_);
    const int32_t otherInUse = set->wordsInUse_;
    if (wordsInUse_ < otherInUse) {
        ensureCapacity(otherInUse);
        wordsInUse_ = otherInUse;
    }
    for (int32_t i = 0; i < wordsInCommon; i++) words_[static_cast<std::size_t>(i)] ^= set->words_[static_cast<std::size_t>(i)];
    for (int32_t i = wordsInCommon; i < otherInUse; i++) words_[static_cast<std::size_t>(i)] = set->words_[static_cast<std::size_t>(i)];
    recalculateWordsInUse();
}

void BitSet::andNot(BitSet* set) {
    if (set == nullptr) detail::throwNullPointer();
    for (int32_t i = std::min(wordsInUse_, set->wordsInUse_) - 1; i >= 0; i--) {
        words_[static_cast<std::size_t>(i)] &= ~set->words_[static_cast<std::size_t>(i)];
    }
    recalculateWordsInUse();
}

Array<int64_t>* BitSet::toLongArray() {
    auto* a = new Array<int64_t>(wordsInUse_);
    std::copy(words_.begin(), words_.begin() + wordsInUse_, a->data());
    return a;
}

Array<int8_t>* BitSet::toByteArray() {
    const int32_t n = wordsInUse_;
    if (n == 0) return new Array<int8_t>(0);
    int32_t len = 8 * (n - 1);
    for (uint64_t x = static_cast<uint64_t>(words_[static_cast<std::size_t>(n - 1)]); x != 0; x >>= 8) len++;
    auto* bytes = new Array<int8_t>(len);
    int32_t pos = 0;
    for (int32_t i = 0; i < n - 1; i++) {
        uint64_t w = static_cast<uint64_t>(words_[static_cast<std::size_t>(i)]);
        for (int32_t b = 0; b < 8; b++, w >>= 8) bytes->data()[pos++] = static_cast<int8_t>(w & 0xff);
    }
    for (uint64_t x = static_cast<uint64_t>(words_[static_cast<std::size_t>(n - 1)]); x != 0; x >>= 8) {
        bytes->data()[pos++] = static_cast<int8_t>(x & 0xff);
    }
    return bytes;
}

bool BitSet::equals(Object* obj) {
    auto* set = dynamic_cast<BitSet*>(obj);
    if (set == nullptr) return false;
    if (set == this) return true;
    if (wordsInUse_ != set->wordsInUse_) return false;
    for (int32_t i = 0; i < wordsInUse_; i++) {
        if (words_[static_cast<std::size_t>(i)] != set->words_[static_cast<std::size_t>(i)]) return false;
    }
    return true;
}

int32_t BitSet::hashCode() {
    int64_t h = 1234;
    for (int32_t i = wordsInUse_; --i >= 0;) h ^= words_[static_cast<std::size_t>(i)] * static_cast<int64_t>(i + 1);
    return static_cast<int32_t>((h >> 32) ^ h);
}

String BitSet::toString() {
    std::string b;
    b.push_back('{');
    int32_t i = nextSetBit(0);
    if (i != -1) {
        b.append(std::to_string(i));
        for (i = nextSetBit(i + 1); i >= 0; i = nextSetBit(i + 1)) {
            const int32_t endOfRun = nextClearBit(i);
            do {
                b.append(", ");
                b.append(std::to_string(i));
            } while (++i < endOfRun);
        }
    }
    b.push_back('}');
    return String(std::move(b));
}

BitSet* BitSet::clone() {
    if (!sizeIsSticky_) trimToSize();
    auto* r = new BitSet(0);
    r->words_ = words_;
    r->wordsInUse_ = wordsInUse_;
    r->sizeIsSticky_ = sizeIsSticky_;
    return r;
}

}  // namespace jlang
