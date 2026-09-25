// jlang/IOZip.cpp - java.util.zip.CRC32, ZipEntry and ZipOutputStream on zlib (raw deflate,
// JDK header layout: data descriptor when sizes/CRC are unknown, UTF-8 name flag).
#include <jlang/IO.h>
#include <jlang/Nio.h>

#include <zlib.h>

#include <cstring>
#include <ctime>
#include <string>

namespace jlang {

// =======================================================================================
// CRC32

void CRC32::updateRaw(const void* p, size_t n) {
    const Bytef* b = static_cast<const Bytef*>(p);
    while (n > 0) {
        uInt chunk = n > 0x40000000u ? 0x40000000u : static_cast<uInt>(n);
        crc_ = static_cast<uint32_t>(::crc32(crc_, b, chunk));
        b += chunk;
        n -= chunk;
    }
}

void CRC32::update(int32_t b) {
    uint8_t v = static_cast<uint8_t>(b);
    updateRaw(&v, 1);
}

void CRC32::update(Array<int8_t>* b) {
    if (b == nullptr) throw NullPointerException();
    updateRaw(b->data(), static_cast<size_t>(b->length));
}

void CRC32::update(Array<int8_t>* b, int32_t off, int32_t len) {
    if (b == nullptr) throw NullPointerException();
    if (off < 0 || len < 0 || off > b->length - len) throw ArrayIndexOutOfBoundsException();
    updateRaw(b->data() + off, static_cast<size_t>(len));
}

void CRC32::update(ByteBuffer* buffer) {
    int32_t pos = buffer->position();
    int32_t rem = buffer->remaining();
    if (rem <= 0) return;
    updateRaw(buffer->rawBase() + pos, static_cast<size_t>(rem));
    buffer->position(pos + rem);
}

// =======================================================================================
// ZipEntry

ZipEntry::ZipEntry(const String& name) {
    if (name.isNull()) throw NullPointerException(String("name"));
    if (name.length() > 0xFFFF) throw IllegalArgumentException(String("entry name too long"));
    name_ = name;
}

ZipEntry::ZipEntry(ZipEntry* e)
    : name_(e->name_), time_(e->time_), crc_(e->crc_), size_(e->size_), csize_(e->csize_), method_(e->method_),
      flag_(e->flag_), extra_(e->extra_), comment_(e->comment_) {}

void ZipEntry::setSize(int64_t size) {
    if (size < 0) throw IllegalArgumentException(String("invalid entry size"));
    size_ = size;
}

void ZipEntry::setCrc(int64_t crc) {
    if (crc < 0 || crc > INT64_C(0xFFFFFFFF)) throw IllegalArgumentException(String("invalid entry crc-32"));
    crc_ = crc;
}

void ZipEntry::setMethod(int32_t method) {
    if (method != STORED && method != DEFLATED) throw IllegalArgumentException(String("invalid compression method"));
    method_ = method;
}

void ZipEntry::setExtra(Array<int8_t>* extra) {
    if (extra != nullptr && extra->length > 0xFFFF) throw IllegalArgumentException(String("invalid extra field length"));
    extra_ = extra;
}

// =======================================================================================
// ZipOutputStream

namespace {

constexpr uint32_t LOCSIG = 0x04034b50;
constexpr uint32_t EXTSIG = 0x08074b50;
constexpr uint32_t CENSIG = 0x02014b50;
constexpr uint32_t ENDSIG = 0x06054b50;
constexpr int32_t USE_UTF8 = 0x800;

voidpf zAlloc(voidpf, uInt items, uInt size) { return detail::collMalloc(static_cast<size_t>(items) * size); }
void zFree(voidpf, voidpf) {}

int64_t javaToDosTime(int64_t time) {
    std::time_t t = static_cast<std::time_t>(time / 1000);
    std::tm tm;
    localtime_r(&t, &tm);
    int year = tm.tm_year + 1900 - 1980;
    if (year < 0) return (1 << 21) | (1 << 16);
    return (static_cast<int64_t>(year) << 25 | static_cast<int64_t>(tm.tm_mon + 1) << 21 |
            static_cast<int64_t>(tm.tm_mday) << 16 | static_cast<int64_t>(tm.tm_hour) << 11 |
            static_cast<int64_t>(tm.tm_min) << 5 | static_cast<int64_t>(tm.tm_sec) >> 1) &
           INT64_C(0xffffffff);
}

std::string hex(int64_t v) {
    char buf[32];
    std::snprintf(buf, sizeof buf, "%llx", static_cast<unsigned long long>(v));
    return buf;
}

}  // namespace

ZipOutputStream::ZipOutputStream(OutputStream* out) : ZipOutputStream(out, StandardCharsets::UTF_8()) {}

ZipOutputStream::ZipOutputStream(OutputStream* out, Charset* charset) : FilterOutputStream(out), charset_(charset) {
    if (out == nullptr) throw NullPointerException(String("out is null"));
    if (charset == nullptr) throw NullPointerException(String("charset is null"));
    auto* zs = static_cast<z_stream*>(detail::collMalloc(sizeof(z_stream)));
    zs->zalloc = zAlloc;
    zs->zfree = zFree;
    zs->opaque = nullptr;
    if (::deflateInit2(zs, Z_DEFAULT_COMPRESSION, Z_DEFLATED, -MAX_WBITS, 8, Z_DEFAULT_STRATEGY) != Z_OK)
        throw InternalError(String("deflateInit2 failed"));
    zs_ = zs;
}

void ZipOutputStream::ensureOpen() {
    if (zclosed_) throw IOException(String("Stream closed"));
}

void ZipOutputStream::setComment(const String& comment) {
    if (!comment.isNull() && charset_->encodeToArray(comment)->length > 0xffff)
        throw IllegalArgumentException(String("ZIP file comment too long."));
    comment_ = comment;
}

void ZipOutputStream::setMethod(int32_t method) {
    if (method != DEFLATED && method != STORED) throw IllegalArgumentException(String("invalid compression method"));
    method_ = method;
}

void ZipOutputStream::setLevel(int32_t level) {
    if ((level < 0 || level > 9) && level != -1) throw IllegalArgumentException(String("invalid compression level"));
    level_ = level;
    ::deflateParams(static_cast<z_stream*>(zs_), level, Z_DEFAULT_STRATEGY);
}

void ZipOutputStream::writeRaw(const void* p, size_t n) {
    if (n == 0) return;
    auto* a = new Array<int8_t>(static_cast<int32_t>(n));
    std::memcpy(a->data(), p, n);
    out->write(a, 0, a->length);
    written_ += static_cast<int64_t>(n);
}

void ZipOutputStream::writeShort(int32_t v) {
    uint8_t b[2] = {static_cast<uint8_t>(v & 0xff), static_cast<uint8_t>((v >> 8) & 0xff)};
    writeRaw(b, 2);
}

void ZipOutputStream::writeInt(int64_t v) {
    uint8_t b[4] = {static_cast<uint8_t>(v & 0xff), static_cast<uint8_t>((v >> 8) & 0xff),
                    static_cast<uint8_t>((v >> 16) & 0xff), static_cast<uint8_t>((v >> 24) & 0xff)};
    writeRaw(b, 4);
}

void ZipOutputStream::writeLOC(ZipEntry* e) {
    std::string name = detail::encodeString(charset_->kind(), e->name_);
    int32_t version = e->method_ == DEFLATED ? 20 : 10;
    writeInt(LOCSIG);
    writeShort(version);
    writeShort(e->flag_);
    writeShort(e->method_);
    writeInt(javaToDosTime(e->time_));
    if ((e->flag_ & 8) == 8) {
        writeInt(0);
        writeInt(0);
        writeInt(0);
    } else {
        writeInt(e->crc_);
        writeInt(e->csize_);
        writeInt(e->size_);
    }
    int32_t elen = e->extra_ != nullptr ? e->extra_->length : 0;
    writeShort(static_cast<int32_t>(name.size()));
    writeShort(elen);
    writeRaw(name.data(), name.size());
    if (elen > 0) writeRaw(e->extra_->data(), static_cast<size_t>(elen));
    locoff_ = written_;
}

void ZipOutputStream::writeEXT(ZipEntry* e) {
    writeInt(EXTSIG);
    writeInt(e->crc_);
    writeInt(e->csize_);
    writeInt(e->size_);
}

void ZipOutputStream::putNextEntry(ZipEntry* e) {
    if (e == nullptr) throw NullPointerException();
    ensureOpen();
    if (current_ != nullptr) closeEntry();
    if (e->time_ == -1) e->setTime(System::currentTimeMillis());
    if (e->method_ == -1) e->method_ = method_;
    e->flag_ = 0;
    switch (e->method_) {
        case DEFLATED:
            if (e->size_ == -1 || e->csize_ == -1 || e->crc_ == -1) e->flag_ = 8;
            break;
        case STORED:
            if (e->size_ == -1) {
                e->size_ = e->csize_;
            } else if (e->csize_ == -1) {
                e->csize_ = e->size_;
            } else if (e->size_ != e->csize_) {
                throw ZipException(String("STORED entry where compressed != uncompressed size"));
            }
            if (e->size_ == -1 || e->crc_ == -1)
                throw ZipException(String("STORED entry missing size, compressed size, or crc-32"));
            break;
        default:
            throw ZipException(String("unsupported compression method"));
    }
    std::string n = std::string(e->name_);
    for (const auto& x : names_)
        if (x == n) throw ZipException(str("duplicate entry: ", e->name_));
    names_.push_back(n);
    if (charset_->kind() == Charset::UTF_8_KIND) e->flag_ |= USE_UTF8;
    current_ = e;
    currentOffset_ = written_;
    entries_.push_back(e);
    offsets_.push_back(written_);
    writeLOC(e);
    crc_->reset();
    bytesRead_ = 0;
    bytesWritten_ = 0;
}

void ZipOutputStream::deflateInput(const int8_t* p, size_t n, bool finish) {
    auto* zs = static_cast<z_stream*>(zs_);
    zs->next_in = reinterpret_cast<Bytef*>(const_cast<int8_t*>(p));
    zs->avail_in = static_cast<uInt>(n);
    unsigned char buf[8192];
    for (;;) {
        zs->next_out = buf;
        zs->avail_out = sizeof buf;
        int rc = ::deflate(zs, finish ? Z_FINISH : Z_NO_FLUSH);
        if (rc == Z_STREAM_ERROR) throw ZipException(String("deflate failed"));
        size_t have = sizeof buf - zs->avail_out;
        if (have > 0) {
            writeRaw(buf, have);
            bytesWritten_ += static_cast<int64_t>(have);
        }
        if (finish) {
            if (rc == Z_STREAM_END) break;
        } else if (zs->avail_in == 0 && zs->avail_out != 0) {
            break;
        }
    }
}

void ZipOutputStream::write(int32_t b) {
    int8_t v = static_cast<int8_t>(b);
    auto* a = new Array<int8_t>(1);
    a->data()[0] = v;
    write(a, 0, 1);
}

void ZipOutputStream::write(Array<int8_t>* b, int32_t off, int32_t len) {
    ensureOpen();
    if (b == nullptr) throw NullPointerException();
    if (off < 0 || len < 0 || off > b->length - len) throw IndexOutOfBoundsException();
    if (len == 0) return;
    if (current_ == nullptr) throw ZipException(String("no current ZIP entry"));
    ZipEntry* e = current_;
    switch (e->method_) {
        case DEFLATED:
            bytesRead_ += len;
            deflateInput(b->data() + off, static_cast<size_t>(len), false);
            break;
        case STORED:
            written_ += len;
            if (written_ - locoff_ > e->size_) throw ZipException(String("attempt to write past end of STORED entry"));
            written_ -= len;
            writeRaw(b->data() + off, static_cast<size_t>(len));
            break;
        default:
            throw ZipException(String("invalid compression method"));
    }
    crc_->update(b, off, len);
}

void ZipOutputStream::closeEntry() {
    ensureOpen();
    if (current_ == nullptr) return;
    ZipEntry* e = current_;
    switch (e->method_) {
        case DEFLATED: {
            deflateInput(nullptr, 0, true);
            if ((e->flag_ & 8) == 0) {
                if (e->size_ != bytesRead_)
                    throw ZipException(str("invalid entry size (expected ", e->size_, " but got ", bytesRead_, " bytes)"));
                if (e->csize_ != bytesWritten_)
                    throw ZipException(
                        str("invalid entry compressed size (expected ", e->csize_, " but got ", bytesWritten_, " bytes)"));
                if (e->crc_ != crc_->getValue())
                    throw ZipException(String("invalid entry CRC-32 (expected 0x" + hex(e->crc_) + " but got 0x" +
                                              hex(crc_->getValue()) + ")"));
            } else {
                e->size_ = bytesRead_;
                e->csize_ = bytesWritten_;
                e->crc_ = crc_->getValue();
                writeEXT(e);
            }
            ::deflateReset(static_cast<z_stream*>(zs_));
            break;
        }
        case STORED:
            if (e->size_ != written_ - locoff_)
                throw ZipException(str("invalid entry size (expected ", e->size_, " but got ", written_ - locoff_, " bytes)"));
            if (e->crc_ != crc_->getValue())
                throw ZipException(String("invalid entry crc-32 (expected 0x" + hex(e->crc_) + " but got 0x" +
                                          hex(crc_->getValue()) + ")"));
            break;
        default:
            throw ZipException(String("invalid compression method"));
    }
    crc_->reset();
    current_ = nullptr;
}

void ZipOutputStream::finish() {
    ensureOpen();
    if (finished_) return;
    if (current_ != nullptr) closeEntry();
    int64_t off = written_;
    for (size_t i = 0; i < entries_.size(); i++) {
        ZipEntry* e = entries_[i];
        std::string name = detail::encodeString(charset_->kind(), e->name_);
        std::string comment = e->comment_.isNull() ? std::string() : detail::encodeString(charset_->kind(), e->comment_);
        int32_t version = e->method_ == DEFLATED ? 20 : 10;
        int32_t elen = e->extra_ != nullptr ? e->extra_->length : 0;
        writeInt(CENSIG);
        writeShort(version);
        writeShort(version);
        writeShort(e->flag_);
        writeShort(e->method_);
        writeInt(javaToDosTime(e->time_));
        writeInt(e->crc_);
        writeInt(e->csize_);
        writeInt(e->size_);
        writeShort(static_cast<int32_t>(name.size()));
        writeShort(elen);
        writeShort(static_cast<int32_t>(comment.size()));
        writeShort(0);  // starting disk number
        writeShort(0);  // internal file attributes
        writeInt(0);    // external file attributes
        writeInt(offsets_[i]);
        writeRaw(name.data(), name.size());
        if (elen > 0) writeRaw(e->extra_->data(), static_cast<size_t>(elen));
        writeRaw(comment.data(), comment.size());
    }
    int64_t len = written_ - off;
    std::string comment = comment_.isNull() ? std::string() : detail::encodeString(charset_->kind(), comment_);
    writeInt(ENDSIG);
    writeShort(0);
    writeShort(0);
    writeShort(static_cast<int32_t>(entries_.size()));
    writeShort(static_cast<int32_t>(entries_.size()));
    writeInt(len);
    writeInt(off);
    writeShort(static_cast<int32_t>(comment.size()));
    writeRaw(comment.data(), comment.size());
    finished_ = true;
}

void ZipOutputStream::close() {
    if (zclosed_) return;
    try {
        finish();
    } catch (...) {
        zclosed_ = true;
        try {
            out->close();
        } catch (...) {
        }
        throw;
    }
    zclosed_ = true;
    ::deflateEnd(static_cast<z_stream*>(zs_));
    out->close();
}

}  // namespace jlang
