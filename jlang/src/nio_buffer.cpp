// jlang/NioBuffer.cpp - java.nio.ByteOrder, ByteBuffer, CharBuffer (OpenJDK 21 semantics).
#include <jlang/Nio.h>

#include <cstring>
#include <string>

namespace jlang {

// =======================================================================================
// exceptions

namespace detail {

void throwBufferUnderflow() { throw BufferUnderflowException(); }
void throwBufferOverflow() { throw BufferOverflowException(); }
void throwReadOnlyBuffer() { throw ReadOnlyBufferException(); }
void throwBufferIndex(int32_t index, int32_t limit) {
    throw IndexOutOfBoundsException(str("Index ", index, " out of bounds for length ", limit));
}
void throwBufferIndexNoMsg() { throw IndexOutOfBoundsException(); }
void throwFromIndexSize(int32_t from, int32_t size, int32_t length) {
    throw IndexOutOfBoundsException(
        str("Range [", from, ", ", from, " + ", size, ") out of bounds for length ", length));
}

// Objects.checkFromIndexSize
static inline void checkFromIndexSize(int32_t from, int32_t size, int32_t length) {
    if ((length | from | size) < 0 || size > length - from) throwFromIndexSize(from, size, length);
}

}  // namespace detail

// =======================================================================================
// ByteOrder

ByteOrder ByteOrder::nativeOrder() noexcept {
#if defined(__BYTE_ORDER__) && __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
    return BIG_ENDIAN;
#else
    return LITTLE_ENDIAN;
#endif
}

String ByteOrder::toString() const {
    if (v_ == Value::BIG) return String("BIG_ENDIAN");
    if (v_ == Value::LITTLE) return String("LITTLE_ENDIAN");
    return String("null");
}

// =======================================================================================
// ByteBuffer

ByteBuffer::ByteBuffer(Array<int8_t>* hb, int32_t mark, int32_t pos, int32_t lim, int32_t cap,
                       int32_t offset, bool readOnly, bool direct)
    : hb_(hb), offset_(offset), readOnly_(readOnly), direct_(direct) {
    // java.nio.Buffer(int mark, int pos, int lim, int cap)
    if (cap < 0) throw IllegalArgumentException(str("capacity < 0: (", cap, " < 0)"));
    capacity_ = cap;
    limit(lim);
    position(pos);
    if (mark >= 0) {
        if (mark > pos) throw IllegalArgumentException(str("mark > position: (", mark, " > ", pos, ")"));
        mark_ = mark;
    }
}

ByteBuffer* ByteBuffer::allocate(int32_t capacity) {
    if (capacity < 0) throw IllegalArgumentException(str("capacity < 0: (", capacity, " < 0)"));
    return new ByteBuffer(new Array<int8_t>(capacity), -1, 0, capacity, capacity, 0, false, false);
}

ByteBuffer* ByteBuffer::allocateDirect(int32_t capacity) {
    if (capacity < 0) throw IllegalArgumentException(str("capacity < 0: (", capacity, " < 0)"));
    return new ByteBuffer(new Array<int8_t>(capacity), -1, 0, capacity, capacity, 0, false, true);
}

ByteBuffer* ByteBuffer::wrap(Array<int8_t>* array) {
    if (array == nullptr) throw NullPointerException();
    return new ByteBuffer(array, -1, 0, array->length, array->length, 0, false, false);
}

ByteBuffer* ByteBuffer::wrap(Array<int8_t>* array, int32_t offset, int32_t length) {
    if (array == nullptr) throw NullPointerException();
    try {
        return new ByteBuffer(array, -1, offset, offset + length, array->length, 0, false, false);
    } catch (IllegalArgumentException&) {
        throw IndexOutOfBoundsException();
    }
}

void ByteBuffer::badPosition(int32_t newPosition) {
    if (newPosition > limit_)
        throw IllegalArgumentException(str("newPosition > limit: (", newPosition, " > ", limit_, ")"));
    throw IllegalArgumentException(str("newPosition < 0: (", newPosition, " < 0)"));
}

void ByteBuffer::badLimit(int32_t newLimit) {
    if (newLimit > capacity_)
        throw IllegalArgumentException(str("newLimit > capacity: (", newLimit, " > ", capacity_, ")"));
    throw IllegalArgumentException(str("newLimit < 0: (", newLimit, " < 0)"));
}

ByteBuffer* ByteBuffer::reset() {
    int32_t m = mark_;
    if (m < 0) throw InvalidMarkException();
    position_ = m;
    return this;
}

Array<int8_t>* ByteBuffer::array() {
    if (hb_ == nullptr) throw UnsupportedOperationException();
    if (readOnly_) throw ReadOnlyBufferException();
    return hb_;
}

int32_t ByteBuffer::arrayOffset() {
    if (hb_ == nullptr) throw UnsupportedOperationException();
    if (readOnly_) throw ReadOnlyBufferException();
    return offset_;
}

ByteBuffer* ByteBuffer::slice() {
    int32_t pos = position_;
    int32_t lim = limit_;
    int32_t rem = pos <= lim ? lim - pos : 0;
    return new ByteBuffer(hb_, -1, 0, rem, rem, pos + offset_, readOnly_, direct_);
}

ByteBuffer* ByteBuffer::slice(int32_t index, int32_t length) {
    detail::checkFromIndexSize(index, length, limit_);
    return new ByteBuffer(hb_, -1, 0, length, length, index + offset_, readOnly_, direct_);
}

ByteBuffer* ByteBuffer::duplicate() {
    return new ByteBuffer(hb_, mark_, position_, limit_, capacity_, offset_, readOnly_, direct_);
}

ByteBuffer* ByteBuffer::asReadOnlyBuffer() {
    return new ByteBuffer(hb_, mark_, position_, limit_, capacity_, offset_, true, direct_);
}

ByteBuffer* ByteBuffer::compact() {
    checkWritable();
    int32_t pos = position_;
    int32_t lim = limit_;
    int32_t rem = pos <= lim ? lim - pos : 0;
    if (rem > 0) std::memmove(base(), base() + pos, static_cast<size_t>(rem));
    position(rem);
    limit(capacity_);
    mark_ = -1;
    return this;
}

CharBuffer* ByteBuffer::asCharBuffer() {
    int32_t pos = position_;
    int32_t size = (limit_ - pos) >> 1;
    return new CharBuffer(nullptr, this, -1, 0, size, size, pos, readOnly_, bigEndian_);
}

ByteBuffer* ByteBuffer::get(Array<int8_t>* dst) {
    if (dst == nullptr) throw NullPointerException();
    return get(dst, 0, dst->length);
}

ByteBuffer* ByteBuffer::get(Array<int8_t>* dst, int32_t offset, int32_t length) {
    if (dst == nullptr) throw NullPointerException();
    detail::checkFromIndexSize(offset, length, dst->length);
    int32_t pos = position_;
    if (length > limit_ - pos) detail::throwBufferUnderflow();
    if (length > 0) std::memmove(dst->data() + offset, base() + pos, static_cast<size_t>(length));
    position(pos + length);
    return this;
}

ByteBuffer* ByteBuffer::get(int32_t index, Array<int8_t>* dst) {
    if (dst == nullptr) throw NullPointerException();
    return get(index, dst, 0, dst->length);
}

ByteBuffer* ByteBuffer::get(int32_t index, Array<int8_t>* dst, int32_t offset, int32_t length) {
    if (dst == nullptr) throw NullPointerException();
    detail::checkFromIndexSize(index, length, limit_);
    detail::checkFromIndexSize(offset, length, dst->length);
    if (length > 0) std::memmove(dst->data() + offset, base() + index, static_cast<size_t>(length));
    return this;
}

ByteBuffer* ByteBuffer::put(Array<int8_t>* src) {
    if (src == nullptr) throw NullPointerException();
    return put(src, 0, src->length);
}

ByteBuffer* ByteBuffer::put(Array<int8_t>* src, int32_t offset, int32_t length) {
    if (src == nullptr) throw NullPointerException();
    checkWritable();
    detail::checkFromIndexSize(offset, length, src->length);
    int32_t pos = position_;
    if (length > limit_ - pos) detail::throwBufferOverflow();
    if (length > 0) std::memmove(base() + pos, src->data() + offset, static_cast<size_t>(length));
    position(pos + length);
    return this;
}

ByteBuffer* ByteBuffer::put(int32_t index, Array<int8_t>* src) {
    if (src == nullptr) throw NullPointerException();
    return put(index, src, 0, src->length);
}

ByteBuffer* ByteBuffer::put(int32_t index, Array<int8_t>* src, int32_t offset, int32_t length) {
    if (src == nullptr) throw NullPointerException();
    checkWritable();
    detail::checkFromIndexSize(index, length, limit_);
    detail::checkFromIndexSize(offset, length, src->length);
    if (length > 0) std::memmove(base() + index, src->data() + offset, static_cast<size_t>(length));
    return this;
}

ByteBuffer* ByteBuffer::put(ByteBuffer* src) {
    if (src == nullptr) throw NullPointerException();
    if (src == this) throw IllegalArgumentException(String("The source buffer is this buffer"));
    checkWritable();
    int32_t srcPos = src->position_;
    int32_t srcLim = src->limit_;
    int32_t srcRem = srcPos <= srcLim ? srcLim - srcPos : 0;
    int32_t pos = position_;
    int32_t lim = limit_;
    int32_t rem = pos <= lim ? lim - pos : 0;
    if (srcRem > rem) detail::throwBufferOverflow();
    if (srcRem > 0) std::memmove(base() + pos, src->base() + srcPos, static_cast<size_t>(srcRem));
    position(pos + srcRem);
    src->position(srcPos + srcRem);
    return this;
}

ByteBuffer* ByteBuffer::put(int32_t index, ByteBuffer* src, int32_t offset, int32_t length) {
    if (src == nullptr) throw NullPointerException();
    detail::checkFromIndexSize(index, length, limit_);
    detail::checkFromIndexSize(offset, length, src->limit_);
    checkWritable();
    if (length > 0) std::memmove(base() + index, src->base() + offset, static_cast<size_t>(length));
    return this;
}

int32_t ByteBuffer::mismatch(ByteBuffer* that) {
    int32_t thisPos = position_;
    int32_t thisRem = limit_ - thisPos;
    int32_t thatPos = that->position_;
    int32_t thatRem = that->limit_ - thatPos;
    int32_t length = thisRem < thatRem ? thisRem : thatRem;
    if (length < 0) return -1;
    const int8_t* a = base() + thisPos;
    const int8_t* b = that->base() + thatPos;
    for (int32_t i = 0; i < length; i++)
        if (a[i] != b[i]) return i;
    return thisRem == thatRem ? -1 : length;
}

bool ByteBuffer::equals(Object* o) {
    if (o == static_cast<Object*>(this)) return true;
    ByteBuffer* that = dynamic_cast<ByteBuffer*>(o);
    if (that == nullptr) return false;
    int32_t thisPos = position_;
    int32_t thisRem = limit_ - thisPos;
    int32_t thatPos = that->position_;
    int32_t thatRem = that->limit_ - thatPos;
    if (thisRem < 0 || thisRem != thatRem) return false;
    return std::memcmp(base() + thisPos, that->base() + thatPos, static_cast<size_t>(thisRem)) == 0;
}

int32_t ByteBuffer::hashCode() {
    int32_t h = 1;
    int32_t p = position_;
    for (int32_t i = limit_ - 1; i >= p; i--)
        h = static_cast<int32_t>(31u * static_cast<uint32_t>(h) + static_cast<uint32_t>(static_cast<int32_t>(base()[i])));
    return h;
}

int32_t ByteBuffer::compareTo(ByteBuffer* that) {
    if (that == nullptr) throw NullPointerException();
    int32_t thisPos = position_;
    int32_t thisRem = limit_ - thisPos;
    int32_t thatPos = that->position_;
    int32_t thatRem = that->limit_ - thatPos;
    int32_t length = thisRem < thatRem ? thisRem : thatRem;
    if (length < 0) return -1;
    const int8_t* a = base() + thisPos;
    const int8_t* b = that->base() + thatPos;
    for (int32_t i = 0; i < length; i++)
        if (a[i] != b[i]) return static_cast<int32_t>(a[i]) - static_cast<int32_t>(b[i]);
    return thisRem - thatRem;
}

String ByteBuffer::toString() {
    const char* cls = direct_ ? (readOnly_ ? "java.nio.DirectByteBufferR" : "java.nio.DirectByteBuffer")
                              : (readOnly_ ? "java.nio.HeapByteBufferR" : "java.nio.HeapByteBuffer");
    return str(cls, "[pos=", position_, " lim=", limit_, " cap=", capacity_, "]");
}

// =======================================================================================
// CharBuffer

CharBuffer::CharBuffer(Array<char16_t>* hb, ByteBuffer* bb, int32_t mark, int32_t pos, int32_t lim,
                       int32_t cap, int32_t offset, bool readOnly, bool bigEndian)
    : hb_(hb), bb_(bb), offset_(offset), readOnly_(readOnly), bigEndian_(bigEndian) {
    if (cap < 0) throw IllegalArgumentException(str("capacity < 0: (", cap, " < 0)"));
    capacity_ = cap;
    limit(lim);
    position(pos);
    if (mark >= 0) {
        if (mark > pos) throw IllegalArgumentException(str("mark > position: (", mark, " > ", pos, ")"));
        mark_ = mark;
    }
}

CharBuffer* CharBuffer::allocate(int32_t capacity) {
    if (capacity < 0) throw IllegalArgumentException(str("capacity < 0: (", capacity, " < 0)"));
    return new CharBuffer(new Array<char16_t>(capacity), nullptr, -1, 0, capacity, capacity, 0, false, true);
}

CharBuffer* CharBuffer::wrap(Array<char16_t>* array) {
    if (array == nullptr) throw NullPointerException();
    return new CharBuffer(array, nullptr, -1, 0, array->length, array->length, 0, false, true);
}

CharBuffer* CharBuffer::wrap(Array<char16_t>* array, int32_t offset, int32_t length) {
    if (array == nullptr) throw NullPointerException();
    try {
        return new CharBuffer(array, nullptr, -1, offset, offset + length, array->length, 0, false, true);
    } catch (IllegalArgumentException&) {
        throw IndexOutOfBoundsException();
    }
}

CharBuffer* CharBuffer::wrap(const String& s) {
    std::u16string u = s.toUtf16();
    auto* a = new Array<char16_t>(static_cast<int32_t>(u.size()));
    if (!u.empty()) std::memcpy(a->data(), u.data(), u.size() * sizeof(char16_t));
    return new CharBuffer(a, nullptr, -1, 0, a->length, a->length, 0, true, true);
}

CharBuffer* CharBuffer::position(int32_t newPosition) {
    if (newPosition > limit_ || newPosition < 0) {
        if (newPosition > limit_)
            throw IllegalArgumentException(str("newPosition > limit: (", newPosition, " > ", limit_, ")"));
        throw IllegalArgumentException(str("newPosition < 0: (", newPosition, " < 0)"));
    }
    if (mark_ > newPosition) mark_ = -1;
    position_ = newPosition;
    return this;
}

CharBuffer* CharBuffer::limit(int32_t newLimit) {
    if (newLimit > capacity_ || newLimit < 0) {
        if (newLimit > capacity_)
            throw IllegalArgumentException(str("newLimit > capacity: (", newLimit, " > ", capacity_, ")"));
        throw IllegalArgumentException(str("newLimit < 0: (", newLimit, " < 0)"));
    }
    limit_ = newLimit;
    if (position_ > newLimit) position_ = newLimit;
    if (mark_ > newLimit) mark_ = -1;
    return this;
}

CharBuffer* CharBuffer::reset() {
    if (mark_ < 0) throw InvalidMarkException();
    position_ = mark_;
    return this;
}

Array<char16_t>* CharBuffer::array() {
    if (hb_ == nullptr) throw UnsupportedOperationException();
    if (readOnly_) throw ReadOnlyBufferException();
    return hb_;
}

int32_t CharBuffer::arrayOffset() {
    if (hb_ == nullptr) throw UnsupportedOperationException();
    if (readOnly_) throw ReadOnlyBufferException();
    return offset_;
}

ByteOrder CharBuffer::order() const noexcept {
    if (hb_ != nullptr) return ByteOrder::nativeOrder();
    return bigEndian_ ? ByteOrder::BIG_ENDIAN : ByteOrder::LITTLE_ENDIAN;
}

char16_t CharBuffer::load(int32_t i) {
    if (hb_ != nullptr) return hb_->data()[offset_ + i];
    const int8_t* p = bb_->rawBase() + offset_ + 2 * i;
    return static_cast<char16_t>(bigEndian_ ? detail::loadBE<uint16_t>(p) : detail::loadLE<uint16_t>(p));
}

void CharBuffer::store(int32_t i, char16_t c) {
    if (hb_ != nullptr) {
        hb_->data()[offset_ + i] = c;
        return;
    }
    int8_t* p = bb_->rawBase() + offset_ + 2 * i;
    if (bigEndian_) detail::storeBE<uint16_t>(p, static_cast<uint16_t>(c));
    else detail::storeLE<uint16_t>(p, static_cast<uint16_t>(c));
}

int32_t CharBuffer::nextGetIndex() {
    if (position_ >= limit_) detail::throwBufferUnderflow();
    return position_++;
}

int32_t CharBuffer::nextPutIndex() {
    if (position_ >= limit_) detail::throwBufferOverflow();
    return position_++;
}

int32_t CharBuffer::checkIndex(int32_t i) {
    if (static_cast<uint32_t>(i) >= static_cast<uint32_t>(limit_)) detail::throwBufferIndex(i, limit_);
    return i;
}

char16_t CharBuffer::get() { return load(nextGetIndex()); }
char16_t CharBuffer::get(int32_t index) { return load(checkIndex(index)); }

CharBuffer* CharBuffer::get(Array<char16_t>* dst) {
    if (dst == nullptr) throw NullPointerException();
    return get(dst, 0, dst->length);
}

CharBuffer* CharBuffer::get(Array<char16_t>* dst, int32_t offset, int32_t length) {
    if (dst == nullptr) throw NullPointerException();
    detail::checkFromIndexSize(offset, length, dst->length);
    if (length > remaining()) detail::throwBufferUnderflow();
    for (int32_t i = 0; i < length; i++) dst->data()[offset + i] = load(position_ + i);
    position_ += length;
    return this;
}

CharBuffer* CharBuffer::put(char16_t c) {
    if (readOnly_) detail::throwReadOnlyBuffer();
    store(nextPutIndex(), c);
    return this;
}

CharBuffer* CharBuffer::put(int32_t index, char16_t c) {
    if (readOnly_) detail::throwReadOnlyBuffer();
    store(checkIndex(index), c);
    return this;
}

CharBuffer* CharBuffer::put(Array<char16_t>* src) {
    if (src == nullptr) throw NullPointerException();
    return put(src, 0, src->length);
}

CharBuffer* CharBuffer::put(Array<char16_t>* src, int32_t offset, int32_t length) {
    if (src == nullptr) throw NullPointerException();
    if (readOnly_) detail::throwReadOnlyBuffer();
    detail::checkFromIndexSize(offset, length, src->length);
    if (length > remaining()) detail::throwBufferOverflow();
    for (int32_t i = 0; i < length; i++) store(position_ + i, src->data()[offset + i]);
    position_ += length;
    return this;
}

CharBuffer* CharBuffer::put(const String& src) {
    std::u16string u = src.toUtf16();
    if (readOnly_) detail::throwReadOnlyBuffer();
    int32_t n = static_cast<int32_t>(u.size());
    if (n > remaining()) detail::throwBufferOverflow();
    for (int32_t i = 0; i < n; i++) store(position_ + i, u[static_cast<size_t>(i)]);
    position_ += n;
    return this;
}

CharBuffer* CharBuffer::put(const String& src, int32_t start, int32_t end) {
    std::u16string u = src.toUtf16();
    int32_t len = static_cast<int32_t>(u.size());
    if (start < 0 || start > end || end > len)
        throw IndexOutOfBoundsException(str("Range [", start, ", ", end, ") out of bounds for length ", len));
    if (readOnly_) detail::throwReadOnlyBuffer();
    if (end - start > remaining()) detail::throwBufferOverflow();
    for (int32_t i = start; i < end; i++) store(position_++, u[static_cast<size_t>(i)]);
    return this;
}

CharBuffer* CharBuffer::put(CharBuffer* src) {
    if (src == nullptr) throw NullPointerException();
    if (src == this) throw IllegalArgumentException(String("The source buffer is this buffer"));
    if (readOnly_) detail::throwReadOnlyBuffer();
    int32_t n = src->remaining();
    if (n > remaining()) detail::throwBufferOverflow();
    for (int32_t i = 0; i < n; i++) put(src->get());
    return this;
}

CharBuffer* CharBuffer::slice() {
    int32_t pos = position_;
    int32_t rem = remaining();
    if (hb_ != nullptr) return new CharBuffer(hb_, nullptr, -1, 0, rem, rem, offset_ + pos, readOnly_, bigEndian_);
    return new CharBuffer(nullptr, bb_, -1, 0, rem, rem, offset_ + 2 * pos, readOnly_, bigEndian_);
}

CharBuffer* CharBuffer::duplicate() {
    return new CharBuffer(hb_, bb_, mark_, position_, limit_, capacity_, offset_, readOnly_, bigEndian_);
}

CharBuffer* CharBuffer::compact() {
    if (readOnly_) detail::throwReadOnlyBuffer();
    int32_t pos = position_;
    int32_t rem = remaining();
    for (int32_t i = 0; i < rem; i++) store(i, load(pos + i));
    position(rem);
    limit(capacity_);
    mark_ = -1;
    return this;
}

char16_t CharBuffer::charAt(int32_t index) {
    if (index < 0 || index >= remaining()) detail::throwBufferIndex(index, remaining());
    return load(position_ + index);
}

String CharBuffer::toString() {
    std::u16string u;
    u.reserve(static_cast<size_t>(remaining()));
    for (int32_t i = position_; i < limit_; i++) u.push_back(load(i));
    return String::fromUtf16(u);
}

bool CharBuffer::equals(Object* o) {
    if (o == static_cast<Object*>(this)) return true;
    CharBuffer* that = dynamic_cast<CharBuffer*>(o);
    if (that == nullptr) return false;
    int32_t n = remaining();
    if (n != that->remaining()) return false;
    for (int32_t i = 0; i < n; i++)
        if (load(position_ + i) != that->load(that->position_ + i)) return false;
    return true;
}

int32_t CharBuffer::hashCode() {
    int32_t h = 1;
    for (int32_t i = limit_ - 1; i >= position_; i--)
        h = static_cast<int32_t>(31u * static_cast<uint32_t>(h) + static_cast<uint32_t>(load(i)));
    return h;
}

}  // namespace jlang
