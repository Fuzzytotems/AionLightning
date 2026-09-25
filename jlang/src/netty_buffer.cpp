// jlang/src/netty_buffer.cpp - org.jboss.netty.buffer (ChannelBuffer, ChannelBuffers,
// HeapChannelBufferFactory), ported from Netty 3.2.0.BETA1.
#include "netty_internal.h"

#include <cstring>

namespace jlang::netty {

using ::jlang::Array;
using ::jlang::ByteOrder;
using ::jlang::String;

namespace {

[[noreturn]] void throwIOOBE() { throw ::jlang::IndexOutOfBoundsException(); }

[[noreturn]] void throwAIOOBE(int64_t index) {
    throw ::jlang::ArrayIndexOutOfBoundsException(static_cast<int32_t>(index));
}

// System.arraycopy bounds check for one side.
inline void checkArrayRange(Array<int8_t>* a, int32_t index, int32_t length) {
    if (a == nullptr) throw ::jlang::NullPointerException();
    if (index < 0) throwAIOOBE(index);
    if (length < 0) throwAIOOBE(length);
    if (static_cast<int64_t>(index) + length > a->length) throwAIOOBE(static_cast<int64_t>(index) + length);
}

inline int32_t floatBits(float v) {
    int32_t r;
    std::memcpy(&r, &v, 4);
    return r;
}
inline float bitsFloat(int32_t v) {
    float r;
    std::memcpy(&r, &v, 4);
    return r;
}
inline int64_t doubleBits(double v) {
    int64_t r;
    std::memcpy(&r, &v, 8);
    return r;
}
inline double bitsDouble(int64_t v) {
    double r;
    std::memcpy(&r, &v, 8);
    return r;
}

}  // namespace

// =======================================================================================
// HeapChannelBufferFactory
// =======================================================================================

HeapChannelBufferFactory::HeapChannelBufferFactory() : littleEndian_(false) {}

HeapChannelBufferFactory::HeapChannelBufferFactory(ByteOrder defaultOrder) {
    if (detail::isNullOrder(defaultOrder)) throw ::jlang::NullPointerException("defaultOrder");
    littleEndian_ = detail::isLittle(defaultOrder);
}

ChannelBufferFactory* HeapChannelBufferFactory::getInstance() { return getInstance(detail::bigEndian()); }

ChannelBufferFactory* HeapChannelBufferFactory::getInstance(ByteOrder endianness) {
    static HeapChannelBufferFactory* const INSTANCE_BE = new HeapChannelBufferFactory(detail::bigEndian());
    static HeapChannelBufferFactory* const INSTANCE_LE = new HeapChannelBufferFactory(detail::littleEndian());
    if (detail::isNullOrder(endianness)) throw ::jlang::NullPointerException("endianness");
    return detail::isLittle(endianness) ? INSTANCE_LE : INSTANCE_BE;
}

ChannelBuffer* HeapChannelBufferFactory::getBuffer(int32_t capacity) {
    return getBuffer(getDefaultOrder(), capacity);
}

ChannelBuffer* HeapChannelBufferFactory::getBuffer(ByteOrder order, int32_t capacity) {
    return ChannelBuffers::buffer(order, capacity);
}

ChannelBuffer* HeapChannelBufferFactory::getBuffer(Array<int8_t>* array, int32_t offset, int32_t length) {
    return getBuffer(getDefaultOrder(), array, offset, length);
}

ChannelBuffer* HeapChannelBufferFactory::getBuffer(ByteOrder order, Array<int8_t>* array, int32_t offset,
                                                   int32_t length) {
    return ChannelBuffers::wrappedBuffer(order, array, offset, length);
}

ByteOrder HeapChannelBufferFactory::getDefaultOrder() {
    return littleEndian_ ? detail::littleEndian() : detail::bigEndian();
}

// =======================================================================================
// ChannelBuffer: construction
// =======================================================================================

ChannelBuffer* ChannelBuffer::newHeap(bool littleEndian, Array<int8_t>* array, int32_t readerIndex,
                                      int32_t writerIndex) {
    if (array == nullptr) throw ::jlang::NullPointerException("array");
    ChannelBuffer* b = new ChannelBuffer(Kind::HEAP);
    b->littleEndian_ = littleEndian;
    b->array_ = array;
    b->setIndex(readerIndex, writerIndex);
    return b;
}

ChannelBuffer* ChannelBuffer::newDynamic(ByteOrder endianness, int32_t estimatedLength,
                                         ChannelBufferFactory* factory) {
    if (estimatedLength < 0) {
        throw ::jlang::IllegalArgumentException(::jlang::str("estimatedLength: ", estimatedLength));
    }
    if (detail::isNullOrder(endianness)) throw ::jlang::NullPointerException("endianness");
    if (factory == nullptr) throw ::jlang::NullPointerException("factory");
    ChannelBuffer* b = new ChannelBuffer(Kind::DYNAMIC);
    b->factory_ = factory;
    b->littleEndian_ = detail::isLittle(endianness);
    b->buffer_ = factory->getBuffer(b->order(), estimatedLength);
    return b;
}

ChannelBuffer* ChannelBuffer::newSliced(ChannelBuffer* buffer, int32_t index, int32_t length) {
    if (index < 0 || index > buffer->capacity()) throwIOOBE();
    if (static_cast<int64_t>(index) + length > buffer->capacity()) throwIOOBE();
    ChannelBuffer* b = new ChannelBuffer(Kind::SLICED);
    b->buffer_ = buffer;
    b->adjustment_ = index;
    b->length_ = length;
    b->writerIndex(length);
    return b;
}

ChannelBuffer* ChannelBuffer::newTruncated(ChannelBuffer* buffer, int32_t length) {
    if (length > buffer->capacity()) throwIOOBE();
    ChannelBuffer* b = new ChannelBuffer(Kind::TRUNCATED);
    b->buffer_ = buffer;
    b->length_ = length;
    b->writerIndex(length);
    return b;
}

ChannelBuffer* ChannelBuffer::newDuplicated(ChannelBuffer* buffer) {
    ChannelBuffer* b = new ChannelBuffer(Kind::DUPLICATED);
    b->buffer_ = buffer;
    b->setIndex(buffer->readerIndex(), buffer->writerIndex());
    return b;
}

// =======================================================================================
// ChannelBuffer: capacity, order, raw access
// =======================================================================================

int32_t ChannelBuffer::capacity() {
    switch (kind_) {
    case Kind::HEAP: return array_->length;
    case Kind::DYNAMIC: return buffer_->capacity();
    case Kind::SLICED:
    case Kind::TRUNCATED: return length_;
    case Kind::DUPLICATED: return buffer_->capacity();
    }
    return 0;
}

bool ChannelBuffer::isLittleEndian() {
    switch (kind_) {
    case Kind::HEAP:
    case Kind::DYNAMIC: return littleEndian_;
    default: return buffer_->isLittleEndian();
    }
}

ByteOrder ChannelBuffer::order() { return isLittleEndian() ? detail::littleEndian() : detail::bigEndian(); }

ChannelBufferFactory* ChannelBuffer::factory() {
    switch (kind_) {
    case Kind::HEAP: return HeapChannelBufferFactory::getInstance(order());
    case Kind::DYNAMIC: return factory_;
    default: return buffer_->factory();
    }
}

void ChannelBuffer::checkIndex(int32_t index, int32_t length) {
    if (length < 0) throw ::jlang::IllegalArgumentException(::jlang::str("length is negative: ", length));
    if (index < 0 && kind_ == Kind::SLICED) throwIOOBE();
    if (static_cast<int64_t>(index) + length > capacity()) throwIOOBE();
}

int8_t* ChannelBuffer::rawBytes(int32_t index, int32_t length) {
    switch (kind_) {
    case Kind::HEAP:
        if (index < 0) throwAIOOBE(index);
        if (length < 0) throwAIOOBE(length);
        if (static_cast<int64_t>(index) + length > array_->length) {
            throwAIOOBE(index > array_->length ? index : array_->length);
        }
        return array_->data() + index;
    case Kind::DYNAMIC: return buffer_->rawBytes(index, length);
    case Kind::SLICED:
        checkIndex(index, length);
        return buffer_->rawBytes(index + adjustment_, length);
    case Kind::TRUNCATED:
        checkIndex(index, length);
        return buffer_->rawBytes(index, length);
    case Kind::DUPLICATED: return buffer_->rawBytes(index, length);
    }
    throwIOOBE();
}

uint64_t ChannelBuffer::getN(int32_t index, int n) {
    const uint8_t* p = reinterpret_cast<const uint8_t*>(rawBytes(index, n));
    uint64_t v = 0;
    if (isLittleEndian()) {
        for (int i = n - 1; i >= 0; --i) v = (v << 8) | p[i];
    } else {
        for (int i = 0; i < n; ++i) v = (v << 8) | p[i];
    }
    return v;
}

void ChannelBuffer::setN(int32_t index, int n, uint64_t v) {
    uint8_t* p = reinterpret_cast<uint8_t*>(rawBytes(index, n));
    if (isLittleEndian()) {
        for (int i = 0; i < n; ++i) p[i] = static_cast<uint8_t>(v >> (8 * i));
    } else {
        for (int i = 0; i < n; ++i) p[i] = static_cast<uint8_t>(v >> (8 * (n - 1 - i)));
    }
}

// =======================================================================================
// ChannelBuffer: indexes
// =======================================================================================

void ChannelBuffer::readerIndex(int32_t readerIndex) {
    if (readerIndex < 0 || readerIndex > writerIndex_) throwIOOBE();
    readerIndex_ = readerIndex;
}

void ChannelBuffer::writerIndex(int32_t writerIndex) {
    if (writerIndex < readerIndex_ || writerIndex > capacity()) throwIOOBE();
    writerIndex_ = writerIndex;
}

void ChannelBuffer::setIndex(int32_t readerIndex, int32_t writerIndex) {
    if (readerIndex < 0 || readerIndex > writerIndex || writerIndex > capacity()) throwIOOBE();
    readerIndex_ = readerIndex;
    writerIndex_ = writerIndex;
}

void ChannelBuffer::discardReadBytes() {
    if (readerIndex_ == 0) return;
    setBytes(0, this, readerIndex_, writerIndex_ - readerIndex_);
    writerIndex_ -= readerIndex_;
    markedReaderIndex_ = std::max(markedReaderIndex_ - readerIndex_, 0);
    markedWriterIndex_ = std::max(markedWriterIndex_ - readerIndex_, 0);
    readerIndex_ = 0;
}

void ChannelBuffer::ensureWritableBytes(int32_t minWritableBytes) {
    if (kind_ != Kind::DYNAMIC) {
        if (minWritableBytes > writableBytes()) throwIOOBE();
        return;
    }
    // DynamicChannelBuffer.ensureWritableBytes
    if (minWritableBytes <= writableBytes()) return;
    int64_t newCapacity = capacity() == 0 ? 1 : capacity();
    int64_t minNewCapacity = static_cast<int64_t>(writerIndex_) + minWritableBytes;
    while (newCapacity < minNewCapacity) newCapacity <<= 1;
    if (newCapacity > INT32_MAX) {
        if (minNewCapacity > INT32_MAX) throwIOOBE();
        newCapacity = INT32_MAX;
    }
    ChannelBuffer* newBuffer = factory()->getBuffer(order(), static_cast<int32_t>(newCapacity));
    newBuffer->writeBytes(buffer_, 0, writerIndex_);
    buffer_ = newBuffer;
}

void ChannelBuffer::checkReadableBytes(int32_t minimumReadableBytes) {
    if (readableBytes() < minimumReadableBytes) throwIOOBE();
}

// =======================================================================================
// ChannelBuffer: get / set
// =======================================================================================

int8_t ChannelBuffer::getByte(int32_t index) { return *rawBytes(index, 1); }
int16_t ChannelBuffer::getShort(int32_t index) { return static_cast<int16_t>(getN(index, 2)); }
int32_t ChannelBuffer::getUnsignedMedium(int32_t index) { return static_cast<int32_t>(getN(index, 3)); }
int32_t ChannelBuffer::getMedium(int32_t index) {
    int32_t value = getUnsignedMedium(index);
    if ((value & 0x800000) != 0) value |= static_cast<int32_t>(0xff000000u);
    return value;
}
int32_t ChannelBuffer::getInt(int32_t index) { return static_cast<int32_t>(getN(index, 4)); }
int64_t ChannelBuffer::getLong(int32_t index) { return static_cast<int64_t>(getN(index, 8)); }
float ChannelBuffer::getFloat(int32_t index) { return bitsFloat(getInt(index)); }
double ChannelBuffer::getDouble(int32_t index) { return bitsDouble(getLong(index)); }

void ChannelBuffer::getBytes(int32_t index, ChannelBuffer* dst) { getBytes(index, dst, dst->writableBytes()); }

void ChannelBuffer::getBytes(int32_t index, ChannelBuffer* dst, int32_t length) {
    if (length > dst->writableBytes()) throwIOOBE();
    getBytes(index, dst, dst->writerIndex(), length);
    dst->writerIndex(dst->writerIndex() + length);
}

void ChannelBuffer::getBytes(int32_t index, ChannelBuffer* dst, int32_t dstIndex, int32_t length) {
    if (dst == nullptr) throw ::jlang::NullPointerException();
    int8_t* s = rawBytes(index, length);
    int8_t* d = dst->rawBytes(dstIndex, length);
    if (length > 0) std::memmove(d, s, static_cast<size_t>(length));
}

void ChannelBuffer::getBytes(int32_t index, Array<int8_t>* dst) {
    if (dst == nullptr) throw ::jlang::NullPointerException();
    getBytes(index, dst, 0, dst->length);
}

void ChannelBuffer::getBytes(int32_t index, Array<int8_t>* dst, int32_t dstIndex, int32_t length) {
    int8_t* s = rawBytes(index, length);
    checkArrayRange(dst, dstIndex, length);
    if (length > 0) std::memmove(dst->data() + dstIndex, s, static_cast<size_t>(length));
}

void ChannelBuffer::setByte(int32_t index, int32_t value) { *rawBytes(index, 1) = static_cast<int8_t>(value); }
void ChannelBuffer::setShort(int32_t index, int32_t value) { setN(index, 2, static_cast<uint32_t>(value)); }
void ChannelBuffer::setMedium(int32_t index, int32_t value) { setN(index, 3, static_cast<uint32_t>(value)); }
void ChannelBuffer::setInt(int32_t index, int32_t value) { setN(index, 4, static_cast<uint32_t>(value)); }
void ChannelBuffer::setLong(int32_t index, int64_t value) { setN(index, 8, static_cast<uint64_t>(value)); }
void ChannelBuffer::setFloat(int32_t index, float value) { setInt(index, floatBits(value)); }
void ChannelBuffer::setDouble(int32_t index, double value) { setLong(index, doubleBits(value)); }

void ChannelBuffer::setBytes(int32_t index, ChannelBuffer* src) { setBytes(index, src, src->readableBytes()); }

void ChannelBuffer::setBytes(int32_t index, ChannelBuffer* src, int32_t length) {
    if (length > src->readableBytes()) throwIOOBE();
    setBytes(index, src, src->readerIndex(), length);
    src->readerIndex(src->readerIndex() + length);
}

void ChannelBuffer::setBytes(int32_t index, ChannelBuffer* src, int32_t srcIndex, int32_t length) {
    if (src == nullptr) throw ::jlang::NullPointerException();
    int8_t* s = src->rawBytes(srcIndex, length);
    int8_t* d = rawBytes(index, length);
    if (length > 0) std::memmove(d, s, static_cast<size_t>(length));
}

void ChannelBuffer::setBytes(int32_t index, Array<int8_t>* src) {
    if (src == nullptr) throw ::jlang::NullPointerException();
    setBytes(index, src, 0, src->length);
}

void ChannelBuffer::setBytes(int32_t index, Array<int8_t>* src, int32_t srcIndex, int32_t length) {
    checkArrayRange(src, srcIndex, length);
    int8_t* d = rawBytes(index, length);
    if (length > 0) std::memmove(d, src->data() + srcIndex, static_cast<size_t>(length));
}

void ChannelBuffer::setZero(int32_t index, int32_t length) {
    if (length == 0) return;
    if (length < 0) throw ::jlang::IllegalArgumentException("length must be 0 or greater than 0.");
    int8_t* d = rawBytes(index, length);
    std::memset(d, 0, static_cast<size_t>(length));
}

// =======================================================================================
// ChannelBuffer: relative reads
// =======================================================================================

int8_t ChannelBuffer::readByte() {
    if (readerIndex_ == writerIndex_) throwIOOBE();
    int32_t i = readerIndex_++;
    return getByte(i);
}

int16_t ChannelBuffer::readShort() {
    checkReadableBytes(2);
    int16_t v = getShort(readerIndex_);
    readerIndex_ += 2;
    return v;
}

int32_t ChannelBuffer::readMedium() {
    int32_t value = readUnsignedMedium();
    if ((value & 0x800000) != 0) value |= static_cast<int32_t>(0xff000000u);
    return value;
}

int32_t ChannelBuffer::readUnsignedMedium() {
    checkReadableBytes(3);
    int32_t v = getUnsignedMedium(readerIndex_);
    readerIndex_ += 3;
    return v;
}

int32_t ChannelBuffer::readInt() {
    checkReadableBytes(4);
    int32_t v = getInt(readerIndex_);
    readerIndex_ += 4;
    return v;
}

int64_t ChannelBuffer::readLong() {
    checkReadableBytes(8);
    int64_t v = getLong(readerIndex_);
    readerIndex_ += 8;
    return v;
}

float ChannelBuffer::readFloat() { return bitsFloat(readInt()); }
double ChannelBuffer::readDouble() { return bitsDouble(readLong()); }

ChannelBuffer* ChannelBuffer::readBytes(int32_t length) {
    checkReadableBytes(length);
    if (length == 0) return ChannelBuffers::emptyBuffer();
    ChannelBuffer* buf = factory()->getBuffer(order(), length);
    buf->writeBytes(this, readerIndex_, length);
    readerIndex_ += length;
    return buf;
}

ChannelBuffer* ChannelBuffer::readSlice(int32_t length) {
    ChannelBuffer* s = slice(readerIndex_, length);
    readerIndex_ += length;
    return s;
}

void ChannelBuffer::readBytes(Array<int8_t>* dst) {
    if (dst == nullptr) throw ::jlang::NullPointerException();
    readBytes(dst, 0, dst->length);
}

void ChannelBuffer::readBytes(Array<int8_t>* dst, int32_t dstIndex, int32_t length) {
    checkReadableBytes(length);
    getBytes(readerIndex_, dst, dstIndex, length);
    readerIndex_ += length;
}

void ChannelBuffer::readBytes(ChannelBuffer* dst) { readBytes(dst, dst->writableBytes()); }

void ChannelBuffer::readBytes(ChannelBuffer* dst, int32_t length) {
    if (length > dst->writableBytes()) throwIOOBE();
    readBytes(dst, dst->writerIndex(), length);
    dst->writerIndex(dst->writerIndex() + length);
}

void ChannelBuffer::readBytes(ChannelBuffer* dst, int32_t dstIndex, int32_t length) {
    checkReadableBytes(length);
    getBytes(readerIndex_, dst, dstIndex, length);
    readerIndex_ += length;
}

void ChannelBuffer::skipBytes(int32_t length) {
    int32_t newReaderIndex = readerIndex_ + length;
    if (newReaderIndex > writerIndex_) throwIOOBE();
    readerIndex_ = newReaderIndex;
}

// =======================================================================================
// ChannelBuffer: relative writes (dynamic buffers grow first, as DynamicChannelBuffer does)
// =======================================================================================

void ChannelBuffer::writeByte(int32_t value) {
    if (kind_ == Kind::DYNAMIC) ensureWritableBytes(1);
    int32_t i = writerIndex_++;  // Java: setByte(writerIndex++, value)
    setByte(i, value);
}

void ChannelBuffer::writeShort(int32_t value) {
    if (kind_ == Kind::DYNAMIC) ensureWritableBytes(2);
    setShort(writerIndex_, value);
    writerIndex_ += 2;
}

void ChannelBuffer::writeMedium(int32_t value) {
    if (kind_ == Kind::DYNAMIC) ensureWritableBytes(3);
    setMedium(writerIndex_, value);
    writerIndex_ += 3;
}

void ChannelBuffer::writeInt(int32_t value) {
    if (kind_ == Kind::DYNAMIC) ensureWritableBytes(4);
    setInt(writerIndex_, value);
    writerIndex_ += 4;
}

void ChannelBuffer::writeLong(int64_t value) {
    if (kind_ == Kind::DYNAMIC) ensureWritableBytes(8);
    setLong(writerIndex_, value);
    writerIndex_ += 8;
}

void ChannelBuffer::writeFloat(float value) { writeInt(floatBits(value)); }
void ChannelBuffer::writeDouble(double value) { writeLong(doubleBits(value)); }

void ChannelBuffer::writeBytes(Array<int8_t>* src) {
    if (src == nullptr) throw ::jlang::NullPointerException();
    writeBytes(src, 0, src->length);
}

void ChannelBuffer::writeBytes(Array<int8_t>* src, int32_t srcIndex, int32_t length) {
    if (kind_ == Kind::DYNAMIC) ensureWritableBytes(length);
    setBytes(writerIndex_, src, srcIndex, length);
    writerIndex_ += length;
}

void ChannelBuffer::writeBytes(ChannelBuffer* src) { writeBytes(src, src->readableBytes()); }

void ChannelBuffer::writeBytes(ChannelBuffer* src, int32_t length) {
    if (length > src->readableBytes()) throwIOOBE();
    writeBytes(src, src->readerIndex(), length);
    src->readerIndex(src->readerIndex() + length);
}

void ChannelBuffer::writeBytes(ChannelBuffer* src, int32_t srcIndex, int32_t length) {
    if (kind_ == Kind::DYNAMIC) ensureWritableBytes(length);
    setBytes(writerIndex_, src, srcIndex, length);
    writerIndex_ += length;
}

void ChannelBuffer::writeZero(int32_t length) {
    if (length == 0) return;
    if (length < 0) throw ::jlang::IllegalArgumentException("length must be 0 or greater than 0.");
    if (kind_ == Kind::DYNAMIC) ensureWritableBytes(length);
    int32_t nLong = static_cast<int32_t>(static_cast<uint32_t>(length) >> 3);
    int32_t nBytes = length & 7;
    for (int32_t i = nLong; i > 0; i--) writeLong(0);
    if (nBytes == 4) {
        writeInt(0);
    } else if (nBytes < 4) {
        for (int32_t i = nBytes; i > 0; i--) writeByte(0);
    } else {
        writeInt(0);
        for (int32_t i = nBytes - 4; i > 0; i--) writeByte(0);
    }
}

// =======================================================================================
// ChannelBuffer: search
// =======================================================================================

int32_t ChannelBuffer::indexOf(int32_t fromIndex, int32_t toIndex, int8_t value) {
    return ChannelBuffers::indexOf(this, fromIndex, toIndex, value);
}

int32_t ChannelBuffer::bytesBefore(int8_t value) { return bytesBefore(readerIndex_, readableBytes(), value); }

int32_t ChannelBuffer::bytesBefore(int32_t length, int8_t value) {
    checkReadableBytes(length);
    return bytesBefore(readerIndex_, length, value);
}

int32_t ChannelBuffer::bytesBefore(int32_t index, int32_t length, int8_t value) {
    int32_t endIndex = indexOf(index, index + length, value);
    if (endIndex < 0) return -1;
    return endIndex - index;
}

// =======================================================================================
// ChannelBuffer: copies and views
// =======================================================================================

ChannelBuffer* ChannelBuffer::copy() { return copy(readerIndex_, readableBytes()); }

ChannelBuffer* ChannelBuffer::copy(int32_t index, int32_t length) {
    switch (kind_) {
    case Kind::HEAP: {
        if (index < 0 || length < 0 || static_cast<int64_t>(index) + length > array_->length) throwIOOBE();
        Array<int8_t>* copied = new Array<int8_t>(length);
        if (length > 0) std::memcpy(copied->data(), array_->data() + index, static_cast<size_t>(length));
        return newHeap(littleEndian_, copied, 0, length);
    }
    case Kind::DYNAMIC: {
        ChannelBuffer* copied = newDynamic(order(), std::max(length, 64), factory());
        copied->buffer_ = buffer_->copy(index, length);
        copied->setIndex(0, length);
        return copied;
    }
    case Kind::SLICED:
        checkIndex(index, length);
        return buffer_->copy(index + adjustment_, length);
    case Kind::TRUNCATED:
        checkIndex(index, length);
        return buffer_->copy(index, length);
    case Kind::DUPLICATED: return buffer_->copy(index, length);
    }
    throwIOOBE();
}

ChannelBuffer* ChannelBuffer::slice() { return slice(readerIndex_, readableBytes()); }

ChannelBuffer* ChannelBuffer::slice(int32_t index, int32_t length) {
    switch (kind_) {
    case Kind::HEAP:
        if (index == 0) {
            if (length == 0) return ChannelBuffers::emptyBuffer();
            if (length == array_->length) return duplicate();
            return newTruncated(this, length);
        }
        if (length == 0) return ChannelBuffers::emptyBuffer();
        return newSliced(this, index, length);
    case Kind::DYNAMIC:
        if (index == 0) {
            if (length == 0) return ChannelBuffers::emptyBuffer();
            return newTruncated(this, length);
        }
        if (length == 0) return ChannelBuffers::emptyBuffer();
        return newSliced(this, index, length);
    case Kind::SLICED:
        checkIndex(index, length);
        if (length == 0) return ChannelBuffers::emptyBuffer();
        return newSliced(buffer_, index + adjustment_, length);
    case Kind::TRUNCATED:
        checkIndex(index, length);
        if (length == 0) return ChannelBuffers::emptyBuffer();
        return buffer_->slice(index, length);
    case Kind::DUPLICATED: return buffer_->slice(index, length);
    }
    throwIOOBE();
}

ChannelBuffer* ChannelBuffer::duplicate() {
    switch (kind_) {
    case Kind::HEAP: return newHeap(littleEndian_, array_, readerIndex_, writerIndex_);
    case Kind::SLICED: {
        ChannelBuffer* d = newSliced(buffer_, adjustment_, length_);
        d->setIndex(readerIndex_, writerIndex_);
        return d;
    }
    case Kind::TRUNCATED: {
        ChannelBuffer* d = newTruncated(buffer_, length_);
        d->setIndex(readerIndex_, writerIndex_);
        return d;
    }
    case Kind::DYNAMIC:
    case Kind::DUPLICATED: return newDuplicated(this);
    }
    throwIOOBE();
}

Array<int8_t>* ChannelBuffer::array() {
    switch (kind_) {
    case Kind::HEAP: return array_;
    default: return buffer_->array();
    }
}

int32_t ChannelBuffer::arrayOffset() {
    switch (kind_) {
    case Kind::HEAP: return 0;
    case Kind::SLICED: return buffer_->arrayOffset() + adjustment_;
    default: return buffer_->arrayOffset();
    }
}

// =======================================================================================
// ChannelBuffer: Object
// =======================================================================================

bool ChannelBuffer::equals(::jlang::Object* o) {
    ChannelBuffer* that = dynamic_cast<ChannelBuffer*>(o);
    if (that == nullptr) return false;
    return ChannelBuffers::equals(this, that);
}

int32_t ChannelBuffer::hashCode() { return ChannelBuffers::hashCode(this); }

int32_t ChannelBuffer::compareTo(ChannelBuffer* that) { return ChannelBuffers::compare(this, that); }

String ChannelBuffer::toString() {
    const char* name = "";
    switch (kind_) {
    case Kind::HEAP: name = littleEndian_ ? "LittleEndianHeapChannelBuffer" : "BigEndianHeapChannelBuffer"; break;
    case Kind::DYNAMIC: name = "DynamicChannelBuffer"; break;
    case Kind::SLICED: name = "SlicedChannelBuffer"; break;
    case Kind::TRUNCATED: name = "TruncatedChannelBuffer"; break;
    case Kind::DUPLICATED: name = "DuplicatedChannelBuffer"; break;
    }
    return ::jlang::str(name, "(ridx=", readerIndex_, ", widx=", writerIndex_, ", cap=", capacity(), ")");
}

// =======================================================================================
// ChannelBuffers
// =======================================================================================

ChannelBuffer* ChannelBuffers::emptyBuffer() {
    static ChannelBuffer* const empty = ChannelBuffer::newHeap(false, new Array<int8_t>(0), 0, 0);
    return empty;
}

ChannelBuffer* const ChannelBuffers::EMPTY_BUFFER = ChannelBuffers::emptyBuffer();

ChannelBuffer* ChannelBuffers::buffer(int32_t capacity) { return buffer(detail::bigEndian(), capacity); }

ChannelBuffer* ChannelBuffers::buffer(ByteOrder endianness, int32_t capacity) {
    if (detail::isNullOrder(endianness)) throw ::jlang::NullPointerException("endianness");
    if (capacity == 0) return emptyBuffer();
    // new byte[capacity] (NegativeArraySizeException for a negative capacity)
    if (capacity < 0) throw ::jlang::NegativeArraySizeException(::jlang::str(capacity));
    return ChannelBuffer::newHeap(detail::isLittle(endianness), new Array<int8_t>(capacity), 0, 0);
}

ChannelBuffer* ChannelBuffers::dynamicBuffer() { return dynamicBuffer(detail::bigEndian(), 256); }

ChannelBuffer* ChannelBuffers::dynamicBuffer(int32_t estimatedLength) {
    return dynamicBuffer(detail::bigEndian(), estimatedLength);
}

ChannelBuffer* ChannelBuffers::dynamicBuffer(ByteOrder endianness, int32_t estimatedLength) {
    if (detail::isNullOrder(endianness)) throw ::jlang::NullPointerException("endianness");
    return ChannelBuffer::newDynamic(endianness, estimatedLength, HeapChannelBufferFactory::getInstance(endianness));
}

ChannelBuffer* ChannelBuffers::dynamicBuffer(ChannelBufferFactory* factory) {
    if (factory == nullptr) throw ::jlang::NullPointerException("factory");
    return ChannelBuffer::newDynamic(factory->getDefaultOrder(), 256, factory);
}

ChannelBuffer* ChannelBuffers::dynamicBuffer(int32_t estimatedLength, ChannelBufferFactory* factory) {
    if (factory == nullptr) throw ::jlang::NullPointerException("factory");
    return ChannelBuffer::newDynamic(factory->getDefaultOrder(), estimatedLength, factory);
}

ChannelBuffer* ChannelBuffers::dynamicBuffer(ByteOrder endianness, int32_t estimatedLength,
                                             ChannelBufferFactory* factory) {
    return ChannelBuffer::newDynamic(endianness, estimatedLength, factory);
}

ChannelBuffer* ChannelBuffers::wrappedBuffer(Array<int8_t>* array) { return wrappedBuffer(detail::bigEndian(), array); }

ChannelBuffer* ChannelBuffers::wrappedBuffer(ByteOrder endianness, Array<int8_t>* array) {
    if (detail::isNullOrder(endianness)) throw ::jlang::NullPointerException("endianness");
    if (array == nullptr) throw ::jlang::NullPointerException("array");
    if (array->length == 0) return emptyBuffer();
    return ChannelBuffer::newHeap(detail::isLittle(endianness), array, 0, array->length);
}

ChannelBuffer* ChannelBuffers::wrappedBuffer(Array<int8_t>* array, int32_t offset, int32_t length) {
    return wrappedBuffer(detail::bigEndian(), array, offset, length);
}

ChannelBuffer* ChannelBuffers::wrappedBuffer(ByteOrder endianness, Array<int8_t>* array, int32_t offset,
                                             int32_t length) {
    if (detail::isNullOrder(endianness)) throw ::jlang::NullPointerException("endianness");
    if (array == nullptr) throw ::jlang::NullPointerException("array");
    if (offset == 0) {
        if (length == array->length) return wrappedBuffer(endianness, array);
        if (length == 0) return emptyBuffer();
        return ChannelBuffer::newTruncated(wrappedBuffer(endianness, array), length);
    }
    if (length == 0) return emptyBuffer();
    return ChannelBuffer::newSliced(wrappedBuffer(endianness, array), offset, length);
}

ChannelBuffer* ChannelBuffers::wrappedBuffer(ChannelBuffer* buffer) {
    if (buffer->readable()) return buffer->slice();
    return emptyBuffer();
}

ChannelBuffer* ChannelBuffers::copiedBuffer(Array<int8_t>* array) { return copiedBuffer(detail::bigEndian(), array); }

ChannelBuffer* ChannelBuffers::copiedBuffer(ByteOrder endianness, Array<int8_t>* array) {
    if (detail::isNullOrder(endianness)) throw ::jlang::NullPointerException("endianness");
    if (array == nullptr) throw ::jlang::NullPointerException("array");
    if (array->length == 0) return emptyBuffer();
    Array<int8_t>* copied = new Array<int8_t>(array->length);
    std::memcpy(copied->data(), array->data(), static_cast<size_t>(array->length));
    return ChannelBuffer::newHeap(detail::isLittle(endianness), copied, 0, copied->length);
}

ChannelBuffer* ChannelBuffers::copiedBuffer(Array<int8_t>* array, int32_t offset, int32_t length) {
    return copiedBuffer(detail::bigEndian(), array, offset, length);
}

ChannelBuffer* ChannelBuffers::copiedBuffer(ByteOrder endianness, Array<int8_t>* array, int32_t offset,
                                            int32_t length) {
    if (detail::isNullOrder(endianness)) throw ::jlang::NullPointerException("endianness");
    if (array == nullptr) throw ::jlang::NullPointerException("array");
    if (length == 0) return emptyBuffer();
    checkArrayRange(array, offset, length);
    Array<int8_t>* copied = new Array<int8_t>(length);
    std::memcpy(copied->data(), array->data() + offset, static_cast<size_t>(length));
    return ChannelBuffer::newHeap(detail::isLittle(endianness), copied, 0, length);
}

ChannelBuffer* ChannelBuffers::copiedBuffer(ChannelBuffer* buffer) {
    if (buffer->readable()) return buffer->copy();
    return emptyBuffer();
}

String ChannelBuffers::hexDump(ChannelBuffer* buffer) {
    return hexDump(buffer, buffer->readerIndex(), buffer->readableBytes());
}

String ChannelBuffers::hexDump(ChannelBuffer* buffer, int32_t fromIndex, int32_t length) {
    if (length < 0) throw ::jlang::IllegalArgumentException(::jlang::str("length: ", length));
    if (length == 0) return String("");
    static const char DIGITS[] = "0123456789abcdef";
    std::string out;
    out.reserve(static_cast<size_t>(length) * 2);
    int32_t endIndex = fromIndex + length;
    for (int32_t i = fromIndex; i < endIndex; i++) {
        int32_t b = buffer->getUnsignedByte(i);
        out.push_back(DIGITS[(b >> 4) & 0x0F]);
        out.push_back(DIGITS[b & 0x0F]);
    }
    return String(std::move(out));
}

int32_t ChannelBuffers::hashCode(ChannelBuffer* buffer) {
    const int32_t aLen = buffer->readableBytes();
    const int32_t intCount = static_cast<int32_t>(static_cast<uint32_t>(aLen) >> 2);
    const int32_t byteCount = aLen & 3;
    int32_t hashCode = 1;
    int32_t arrayIndex = buffer->readerIndex();
    bool be = !buffer->isLittleEndian();
    for (int32_t i = intCount; i > 0; i--) {
        int32_t v = buffer->getInt(arrayIndex);
        hashCode = 31 * hashCode + (be ? v : swapInt(v));
        arrayIndex += 4;
    }
    for (int32_t i = byteCount; i > 0; i--) hashCode = 31 * hashCode + buffer->getByte(arrayIndex++);
    if (hashCode == 0) hashCode = 1;
    return hashCode;
}

bool ChannelBuffers::equals(ChannelBuffer* bufferA, ChannelBuffer* bufferB) {
    const int32_t aLen = bufferA->readableBytes();
    if (aLen != bufferB->readableBytes()) return false;
    const int32_t longCount = static_cast<int32_t>(static_cast<uint32_t>(aLen) >> 3);
    const int32_t byteCount = aLen & 7;
    int32_t aIndex = bufferA->readerIndex();
    int32_t bIndex = bufferB->readerIndex();
    bool sameOrder = bufferA->isLittleEndian() == bufferB->isLittleEndian();
    for (int32_t i = longCount; i > 0; i--) {
        int64_t b = bufferB->getLong(bIndex);
        if (bufferA->getLong(aIndex) != (sameOrder ? b : swapLong(b))) return false;
        aIndex += 8;
        bIndex += 8;
    }
    for (int32_t i = byteCount; i > 0; i--) {
        if (bufferA->getByte(aIndex) != bufferB->getByte(bIndex)) return false;
        aIndex++;
        bIndex++;
    }
    return true;
}

int32_t ChannelBuffers::compare(ChannelBuffer* bufferA, ChannelBuffer* bufferB) {
    const int32_t aLen = bufferA->readableBytes();
    const int32_t bLen = bufferB->readableBytes();
    const int32_t minLength = std::min(aLen, bLen);
    const int32_t uintCount = static_cast<int32_t>(static_cast<uint32_t>(minLength) >> 2);
    const int32_t byteCount = minLength & 3;
    int32_t aIndex = bufferA->readerIndex();
    int32_t bIndex = bufferB->readerIndex();
    bool sameOrder = bufferA->isLittleEndian() == bufferB->isLittleEndian();
    for (int32_t i = uintCount; i > 0; i--) {
        int64_t va = bufferA->getUnsignedInt(aIndex);
        int64_t vb = sameOrder ? bufferB->getUnsignedInt(bIndex)
                               : (swapInt(bufferB->getInt(bIndex)) & INT64_C(0xFFFFFFFF));
        if (va > vb) return 1;
        if (va < vb) return -1;
        aIndex += 4;
        bIndex += 4;
    }
    for (int32_t i = byteCount; i > 0; i--) {
        int8_t va = bufferA->getByte(aIndex);
        int8_t vb = bufferB->getByte(bIndex);
        if (va > vb) return 1;
        if (va < vb) return -1;
        aIndex++;
        bIndex++;
    }
    return aLen - bLen;
}

int32_t ChannelBuffers::indexOf(ChannelBuffer* buffer, int32_t fromIndex, int32_t toIndex, int8_t value) {
    if (fromIndex <= toIndex) {
        fromIndex = std::max(fromIndex, 0);
        if (fromIndex >= toIndex || buffer->capacity() == 0) return -1;
        for (int32_t i = fromIndex; i < toIndex; i++) {
            if (buffer->getByte(i) == value) return i;
        }
        return -1;
    }
    fromIndex = std::min(fromIndex, buffer->capacity());
    if (fromIndex < 0 || buffer->capacity() == 0) return -1;
    for (int32_t i = fromIndex - 1; i >= toIndex; i--) {
        if (buffer->getByte(i) == value) return i;
    }
    return -1;
}

int16_t ChannelBuffers::swapShort(int16_t value) {
    uint16_t v = static_cast<uint16_t>(value);
    return static_cast<int16_t>(static_cast<uint16_t>((v << 8) | (v >> 8)));
}

int32_t ChannelBuffers::swapMedium(int32_t value) {
    return ((value << 16) & 0xff0000) | (value & 0xff00) | (static_cast<int32_t>(static_cast<uint32_t>(value) >> 16) & 0xff);
}

int32_t ChannelBuffers::swapInt(int32_t value) {
    return static_cast<int32_t>(__builtin_bswap32(static_cast<uint32_t>(value)));
}

int64_t ChannelBuffers::swapLong(int64_t value) {
    return static_cast<int64_t>(__builtin_bswap64(static_cast<uint64_t>(value)));
}

}  // namespace jlang::netty
