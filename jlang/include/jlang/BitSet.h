// jlang/BitSet.h - java.util.BitSet (JDK 6/7 algorithms: same length(), size(), hashCode(),
// toString() "{1, 3, 5}" and exceptions). Not synchronized, like Java's.
//
// Java's and/or/xor are C++ keywords: they are named and_/or_/xor_ (CONVENTIONS §3.4).
//
// Part of the jlang core: translated code includes <jlang/jlang.h>, never this header directly.
#pragma once

#include <jlang/Array.h>
#include <jlang/CollectionsCore.h>

namespace jlang {

class BitSet final : public virtual Object {
public:
    BitSet();                        // 64 bits
    explicit BitSet(int32_t nbits);  // NegativeArraySizeException("nbits < 0: -1")

    bool get(int32_t bitIndex);
    BitSet* get(int32_t fromIndex, int32_t toIndex);
    void set(int32_t bitIndex);
    void set(int32_t bitIndex, bool value);
    void set(int32_t fromIndex, int32_t toIndex);
    void set(int32_t fromIndex, int32_t toIndex, bool value);
    void clear(int32_t bitIndex);
    void clear(int32_t fromIndex, int32_t toIndex);
    void clear();
    void flip(int32_t bitIndex);
    void flip(int32_t fromIndex, int32_t toIndex);

    int32_t nextSetBit(int32_t fromIndex);    // -1 if none
    int32_t nextClearBit(int32_t fromIndex);
    int32_t previousSetBit(int32_t fromIndex);    // Java 7; -1 if none
    int32_t previousClearBit(int32_t fromIndex);  // Java 7; -1 if none

    int32_t length();       // highest set bit + 1
    int32_t size();         // bits of space in use (a multiple of 64)
    bool isEmpty();
    int32_t cardinality();  // number of set bits
    bool intersects(BitSet* set);

    void and_(BitSet* set);
    void or_(BitSet* set);
    void xor_(BitSet* set);
    void andNot(BitSet* set);

    Array<int64_t>* toLongArray();
    Array<int8_t>* toByteArray();

    bool equals(Object* obj) override;
    int32_t hashCode() override;
    String toString() override;  // "{0, 2, 5}"
    BitSet* clone() override;

private:
    static int32_t wordIndex(int32_t bitIndex) { return bitIndex >> 6; }
    void recalculateWordsInUse();
    void ensureCapacity(int32_t wordsRequired);
    void expandTo(int32_t wordIndex);
    void trimToSize();

    detail::Vec<int64_t> words_;
    int32_t wordsInUse_ = 0;
    bool sizeIsSticky_ = false;
};

}  // namespace jlang
