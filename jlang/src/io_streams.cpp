// jlang/IOStreams.cpp - java.io byte streams (file, buffered, byte array, data), PrintStream
// over an OutputStream, readers/writers and commons-io IOUtils / LineIterator.
#include <jlang/IO.h>
#include <jlang/Nio.h>

#include <cerrno>
#include <climits>
#include <cstring>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <unistd.h>

#include <string>

namespace jlang {

namespace {

String errnoString(int err) {
    char buf[256];
    return String(strerror_r(err, buf, sizeof buf));
}

inline void checkFromIndexSize(int32_t off, int32_t len, int32_t length) {
    if ((length | off | len) < 0 || len > length - off)
        throw IndexOutOfBoundsException(str("Range [", off, ", ", off, " + ", len, ") out of bounds for length ", length));
}

inline void requireNonNull(const void* p) {
    if (p == nullptr) throw NullPointerException();
}

constexpr int32_t kMaxBufferSize = INT32_MAX - 8;

}  // namespace

// =======================================================================================
// InputStream

int32_t InputStream::read(Array<int8_t>* b) {
    requireNonNull(b);
    return read(b, 0, b->length);
}

int32_t InputStream::read(Array<int8_t>* b, int32_t off, int32_t len) {
    requireNonNull(b);
    checkFromIndexSize(off, len, b->length);
    if (len == 0) return 0;
    int32_t c = read();
    if (c == -1) return -1;
    b->data()[off] = static_cast<int8_t>(c);
    int32_t i = 1;
    try {
        for (; i < len; i++) {
            c = read();
            if (c == -1) break;
            b->data()[off + i] = static_cast<int8_t>(c);
        }
    } catch (IOException&) {
    }
    return i;
}

int64_t InputStream::skip(int64_t n) {
    int64_t remaining = n;
    if (n <= 0) return 0;
    int32_t size = static_cast<int32_t>(remaining < 2048 ? remaining : 2048);
    auto* skipBuffer = new Array<int8_t>(size);
    while (remaining > 0) {
        int32_t nr = read(skipBuffer, 0, static_cast<int32_t>(remaining < size ? remaining : size));
        if (nr < 0) break;
        remaining -= nr;
    }
    return n - remaining;
}

void InputStream::reset() { throw IOException(String("mark/reset not supported")); }

Array<int8_t>* InputStream::readAllBytes() { return readNBytes(INT32_MAX); }

Array<int8_t>* InputStream::readNBytes(int32_t len) {
    if (len < 0) throw IllegalArgumentException(String("len < 0"));
    std::string acc;
    auto* buf = new Array<int8_t>(len < 16384 ? (len > 0 ? len : 1) : 16384);
    int32_t remaining = len;
    while (remaining > 0) {
        int32_t n = read(buf, 0, remaining < buf->length ? remaining : buf->length);
        if (n < 0) break;
        acc.append(reinterpret_cast<const char*>(buf->data()), static_cast<size_t>(n));
        remaining -= n;
    }
    auto* r = new Array<int8_t>(static_cast<int32_t>(acc.size()));
    if (!acc.empty()) std::memcpy(r->data(), acc.data(), acc.size());
    return r;
}

int32_t InputStream::readNBytes(Array<int8_t>* b, int32_t off, int32_t len) {
    requireNonNull(b);
    checkFromIndexSize(off, len, b->length);
    int32_t n = 0;
    while (n < len) {
        int32_t count = read(b, off + n, len - n);
        if (count < 0) break;
        n += count;
    }
    return n;
}

void InputStream::skipNBytes(int64_t n) {
    while (n > 0) {
        int64_t ns = skip(n);
        if (ns > 0 && ns <= n) {
            n -= ns;
        } else if (ns == 0) {
            if (read() == -1) throw EOFException();
            n--;
        } else {
            throw IOException(String("Unable to skip exactly"));
        }
    }
}

int64_t InputStream::transferTo(OutputStream* out) {
    requireNonNull(out);
    int64_t transferred = 0;
    auto* buffer = new Array<int8_t>(16384);
    int32_t read;
    while ((read = this->read(buffer, 0, buffer->length)) >= 0) {
        out->write(buffer, 0, read);
        transferred += read;
    }
    return transferred;
}

namespace {
class NullInputStream final : public InputStream {
public:
    using InputStream::read;
    int32_t read() override {
        ensureOpen();
        return -1;
    }
    int32_t read(Array<int8_t>* b, int32_t off, int32_t len) override {
        requireNonNull(b);
        checkFromIndexSize(off, len, b->length);
        ensureOpen();
        return len == 0 ? 0 : -1;
    }
    int32_t available() override {
        ensureOpen();
        return 0;
    }
    void close() override { closed_ = true; }

private:
    void ensureOpen() {
        if (closed_) throw IOException(String("Stream closed"));
    }
    bool closed_ = false;
};
class NullOutputStream final : public OutputStream {
public:
    using OutputStream::write;
    void write(int32_t) override { ensureOpen(); }
    void write(Array<int8_t>* b, int32_t off, int32_t len) override {
        requireNonNull(b);
        checkFromIndexSize(off, len, b->length);
        ensureOpen();
    }
    void close() override { closed_ = true; }

private:
    void ensureOpen() {
        if (closed_) throw IOException(String("Stream closed"));
    }
    bool closed_ = false;
};
}  // namespace

InputStream* InputStream::nullInputStream() { return new NullInputStream(); }
OutputStream* OutputStream::nullOutputStream() { return new NullOutputStream(); }

// =======================================================================================
// OutputStream

void OutputStream::write(Array<int8_t>* b) {
    requireNonNull(b);
    write(b, 0, b->length);
}

void OutputStream::write(Array<int8_t>* b, int32_t off, int32_t len) {
    requireNonNull(b);
    checkFromIndexSize(off, len, b->length);
    for (int32_t i = 0; i < len; i++) write(static_cast<int32_t>(b->data()[off + i]));
}

// =======================================================================================
// FilterInputStream / FilterOutputStream

int32_t FilterInputStream::read() { return in->read(); }
int32_t FilterInputStream::read(Array<int8_t>* b) { return read(b, 0, b->length); }
int32_t FilterInputStream::read(Array<int8_t>* b, int32_t off, int32_t len) { return in->read(b, off, len); }
int64_t FilterInputStream::skip(int64_t n) { return in->skip(n); }
int32_t FilterInputStream::available() { return in->available(); }
void FilterInputStream::close() { in->close(); }
void FilterInputStream::mark(int32_t readlimit) { in->mark(readlimit); }
void FilterInputStream::reset() { in->reset(); }
bool FilterInputStream::markSupported() { return in->markSupported(); }

void FilterOutputStream::write(int32_t b) { out->write(b); }
void FilterOutputStream::write(Array<int8_t>* b) { write(b, 0, b->length); }
void FilterOutputStream::write(Array<int8_t>* b, int32_t off, int32_t len) {
    requireNonNull(b);
    checkFromIndexSize(off, len, b->length);
    for (int32_t i = 0; i < len; i++) write(static_cast<int32_t>(b->data()[off + i]));
}
void FilterOutputStream::flush() { out->flush(); }
void FilterOutputStream::close() {
    if (closed_) return;
    closed_ = true;
    Throwable* flushException = nullptr;
    try {
        flush();
    } catch (Throwable& e) {
        flushException = e.copyThrowable();
    }
    if (flushException == nullptr) {
        out->close();
    } else {
        try {
            out->close();
        } catch (Throwable&) {
        }
        flushException->rethrow();
    }
}

// =======================================================================================
// FileInputStream / FileOutputStream

FileInputStream::FileInputStream(File* file) {
    if (file == nullptr) throw NullPointerException();
    open(file->getPath());
}

FileInputStream::FileInputStream(const String& name) {
    if (name.isNull()) throw NullPointerException();
    open(name);
}

void FileInputStream::open(const String& path) {
    path_ = path;
    if (path.isEmpty()) throw FileNotFoundException(str(path, " (", errnoString(ENOENT), ")"));
    int fd;
    do {
        fd = ::open(path.c_str(), O_RDONLY | O_CLOEXEC);
    } while (fd < 0 && errno == EINTR);
    if (fd < 0) throw FileNotFoundException(str(path, " (", errnoString(errno), ")"));
    struct stat st;
    if (::fstat(fd, &st) == 0 && S_ISDIR(st.st_mode)) {
        ::close(fd);
        throw FileNotFoundException(str(path, " (", errnoString(EISDIR), ")"));
    }
    fd_ = fd;
}

int32_t FileInputStream::read() {
    if (fd_ < 0) throw IOException(String("Stream Closed"));
    uint8_t b;
    for (;;) {
        ssize_t n = ::read(fd_, &b, 1);
        if (n == 1) return b;
        if (n == 0) return -1;
        if (errno == EINTR) continue;
        throw IOException(errnoString(errno));
    }
}

int32_t FileInputStream::read(Array<int8_t>* b, int32_t off, int32_t len) {
    requireNonNull(b);
    checkFromIndexSize(off, len, b->length);
    if (len == 0) return 0;
    if (fd_ < 0) throw IOException(String("Stream Closed"));
    for (;;) {
        ssize_t n = ::read(fd_, b->data() + off, static_cast<size_t>(len));
        if (n > 0) return static_cast<int32_t>(n);
        if (n == 0) return -1;
        if (errno == EINTR) continue;
        throw IOException(errnoString(errno));
    }
}

int64_t FileInputStream::skip(int64_t n) {
    if (fd_ < 0) throw IOException(String("Stream Closed"));
    off_t cur = ::lseek(fd_, 0, SEEK_CUR);
    if (cur < 0) throw IOException(errnoString(errno));
    off_t end = ::lseek(fd_, static_cast<off_t>(n), SEEK_CUR);
    if (end < 0) throw IOException(errnoString(errno));
    return static_cast<int64_t>(end - cur);
}

int32_t FileInputStream::available() {
    if (fd_ < 0) throw IOException(String("Stream Closed"));
    struct stat st;
    if (::fstat(fd_, &st) == 0 && S_ISREG(st.st_mode)) {
        off_t cur = ::lseek(fd_, 0, SEEK_CUR);
        if (cur < 0) return 0;
        int64_t rem = static_cast<int64_t>(st.st_size) - static_cast<int64_t>(cur);
        if (rem < 0) return 0;
        return rem > INT32_MAX ? INT32_MAX : static_cast<int32_t>(rem);
    }
    int n = 0;
    if (::ioctl(fd_, FIONREAD, &n) == 0) return n;
    return 0;
}

void FileInputStream::close() {
    int fd = fd_;
    fd_ = -1;
    if (fd >= 0) ::close(fd);
}

FileOutputStream::FileOutputStream(File* file, bool append) {
    if (file == nullptr) throw NullPointerException();
    open(file->getPath(), append);
}

FileOutputStream::FileOutputStream(const String& name, bool append) {
    if (name.isNull()) throw NullPointerException();
    open(name, append);
}

void FileOutputStream::open(const String& path, bool append) {
    path_ = path;
    if (path.isEmpty()) throw FileNotFoundException(str(path, " (", errnoString(ENOENT), ")"));
    int flags = O_WRONLY | O_CREAT | O_CLOEXEC | (append ? O_APPEND : O_TRUNC);
    int fd;
    do {
        fd = ::open(path.c_str(), flags, 0666);
    } while (fd < 0 && errno == EINTR);
    if (fd < 0) throw FileNotFoundException(str(path, " (", errnoString(errno), ")"));
    fd_ = fd;
}

void FileOutputStream::writeRaw(const void* p, size_t n) {
    if (fd_ < 0) throw IOException(String("Stream Closed"));
    const char* c = static_cast<const char*>(p);
    while (n > 0) {
        ssize_t w = ::write(fd_, c, n);
        if (w < 0) {
            if (errno == EINTR) continue;
            throw IOException(errnoString(errno));
        }
        c += w;
        n -= static_cast<size_t>(w);
    }
}

void FileOutputStream::write(int32_t b) {
    int8_t v = static_cast<int8_t>(b);
    writeRaw(&v, 1);
}

void FileOutputStream::write(Array<int8_t>* b, int32_t off, int32_t len) {
    requireNonNull(b);
    checkFromIndexSize(off, len, b->length);
    writeRaw(b->data() + off, static_cast<size_t>(len));
}

void FileOutputStream::close() {
    int fd = fd_;
    fd_ = -1;
    if (fd >= 0 && ::close(fd) != 0 && errno != EINTR) throw IOException(errnoString(errno));
}

// =======================================================================================
// BufferedInputStream (JDK algorithm)

BufferedInputStream::BufferedInputStream(InputStream* in, int32_t size) : FilterInputStream(in) {
    if (size <= 0) throw IllegalArgumentException(String("Buffer size <= 0"));
    buf = new Array<int8_t>(size);
}

Array<int8_t>* BufferedInputStream::getBufIfOpen() {
    if (buf == nullptr) throw IOException(String("Stream closed"));
    return buf;
}

InputStream* BufferedInputStream::getInIfOpen() {
    if (in == nullptr) throw IOException(String("Stream closed"));
    return in;
}

void BufferedInputStream::fill() {
    Array<int8_t>* buffer = getBufIfOpen();
    if (markpos == -1) {
        pos = 0;  // no mark: throw away the buffer
    } else if (pos >= buffer->length) {  // no room left in buffer
        if (markpos > 0) {                // can throw away early part of the buffer
            int32_t sz = pos - markpos;
            std::memmove(buffer->data(), buffer->data() + markpos, static_cast<size_t>(sz));
            pos = sz;
            markpos = 0;
        } else if (buffer->length >= marklimit) {
            markpos = -1;  // buffer got too big, invalidate mark
            pos = 0;       // drop buffer contents
        } else {           // grow buffer
            int32_t nsz = pos <= kMaxBufferSize - pos ? pos * 2 : kMaxBufferSize;
            if (nsz > marklimit) nsz = marklimit;
            auto* nbuf = new Array<int8_t>(nsz);
            std::memcpy(nbuf->data(), buffer->data(), static_cast<size_t>(pos));
            buf = nbuf;
            buffer = nbuf;
        }
    }
    count = pos;
    int32_t n = getInIfOpen()->read(buffer, pos, buffer->length - pos);
    if (n > 0) count = n + pos;
}

int32_t BufferedInputStream::read() {
    if (pos >= count) {
        fill();
        if (pos >= count) return -1;
    }
    return static_cast<uint8_t>(getBufIfOpen()->data()[pos++]);
}

int32_t BufferedInputStream::read1(Array<int8_t>* b, int32_t off, int32_t len) {
    int32_t avail = count - pos;
    if (avail <= 0) {
        // Large reads without a mark bypass the buffer.
        if (len >= getBufIfOpen()->length && markpos == -1) return getInIfOpen()->read(b, off, len);
        fill();
        avail = count - pos;
        if (avail <= 0) return -1;
    }
    int32_t cnt = avail < len ? avail : len;
    std::memcpy(b->data() + off, getBufIfOpen()->data() + pos, static_cast<size_t>(cnt));
    pos += cnt;
    return cnt;
}

int32_t BufferedInputStream::read(Array<int8_t>* b, int32_t off, int32_t len) {
    getBufIfOpen();
    requireNonNull(b);
    if ((off | len | (off + len) | (b->length - (off + len))) < 0) throw IndexOutOfBoundsException();
    if (len == 0) return 0;
    int32_t n = 0;
    for (;;) {
        int32_t nread = read1(b, off + n, len - n);
        if (nread <= 0) return n == 0 ? nread : n;
        n += nread;
        if (n >= len) return n;
        InputStream* input = in;
        if (input != nullptr && input->available() <= 0) return n;
    }
}

int64_t BufferedInputStream::skip(int64_t n) {
    getBufIfOpen();
    if (n <= 0) return 0;
    int64_t avail = count - pos;
    if (avail <= 0) {
        if (markpos == -1) return getInIfOpen()->skip(n);
        fill();
        avail = count - pos;
        if (avail <= 0) return 0;
    }
    int64_t skipped = avail < n ? avail : n;
    pos += static_cast<int32_t>(skipped);
    return skipped;
}

int32_t BufferedInputStream::available() {
    int32_t n = count - pos;
    int32_t avail = getInIfOpen()->available();
    return n > (INT32_MAX - avail) ? INT32_MAX : n + avail;
}

void BufferedInputStream::mark(int32_t readlimit) {
    marklimit = readlimit;
    markpos = pos;
}

void BufferedInputStream::reset() {
    getBufIfOpen();
    if (markpos < 0) throw IOException(String("Resetting to invalid mark"));
    pos = markpos;
}

void BufferedInputStream::close() {
    Array<int8_t>* b = buf;
    buf = nullptr;
    InputStream* input = in;
    in = nullptr;
    if (b != nullptr && input != nullptr) input->close();
}

// =======================================================================================
// BufferedOutputStream

BufferedOutputStream::BufferedOutputStream(OutputStream* out, int32_t size) : FilterOutputStream(out) {
    if (size <= 0) throw IllegalArgumentException(String("Buffer size <= 0"));
    buf = new Array<int8_t>(size);
}

void BufferedOutputStream::flushBuffer() {
    if (count > 0) {
        out->write(buf, 0, count);
        count = 0;
    }
}

void BufferedOutputStream::write(int32_t b) {
    if (count >= buf->length) flushBuffer();
    buf->data()[count++] = static_cast<int8_t>(b);
}

void BufferedOutputStream::write(Array<int8_t>* b, int32_t off, int32_t len) {
    requireNonNull(b);
    checkFromIndexSize(off, len, b->length);
    if (len >= buf->length) {
        flushBuffer();
        out->write(b, off, len);
        return;
    }
    if (len > buf->length - count) flushBuffer();
    std::memcpy(buf->data() + count, b->data() + off, static_cast<size_t>(len));
    count += len;
}

void BufferedOutputStream::flush() {
    flushBuffer();
    out->flush();
}

// =======================================================================================
// ByteArrayInputStream / ByteArrayOutputStream

ByteArrayInputStream::ByteArrayInputStream(Array<int8_t>* b) : buf(b) {
    requireNonNull(b);
    count = b->length;
}

ByteArrayInputStream::ByteArrayInputStream(Array<int8_t>* b, int32_t offset, int32_t length)
    : buf(b), pos(offset), mark_(offset) {
    requireNonNull(b);
    int64_t c = static_cast<int64_t>(offset) + length;
    count = static_cast<int32_t>(c < b->length ? c : b->length);
}

int32_t ByteArrayInputStream::read() { return pos < count ? static_cast<uint8_t>(buf->data()[pos++]) : -1; }

int32_t ByteArrayInputStream::read(Array<int8_t>* b, int32_t off, int32_t len) {
    requireNonNull(b);
    checkFromIndexSize(off, len, b->length);
    if (pos >= count) return -1;
    int32_t avail = count - pos;
    if (len > avail) len = avail;
    if (len <= 0) return 0;
    std::memcpy(b->data() + off, buf->data() + pos, static_cast<size_t>(len));
    pos += len;
    return len;
}

int64_t ByteArrayInputStream::skip(int64_t n) {
    int64_t k = count - pos;
    if (n < k) k = n < 0 ? 0 : n;
    pos += static_cast<int32_t>(k);
    return k;
}

void ByteArrayInputStream::mark(int32_t) { mark_ = pos; }

Array<int8_t>* ByteArrayInputStream::readAllBytes() {
    int32_t n = count - pos;
    if (n < 0) n = 0;
    auto* r = new Array<int8_t>(n);
    if (n > 0) std::memcpy(r->data(), buf->data() + pos, static_cast<size_t>(n));
    pos = count;
    return r;
}

int64_t ByteArrayInputStream::transferTo(OutputStream* out) {
    int32_t len = count - pos;
    if (len > 0) out->write(buf, pos, len);
    pos = count;
    return len > 0 ? len : 0;
}

ByteArrayOutputStream::ByteArrayOutputStream(int32_t size) {
    if (size < 0) throw IllegalArgumentException(str("Negative initial size: ", size));
    buf = new Array<int8_t>(size);
}

void ByteArrayOutputStream::ensureCapacity(int32_t minCapacity) {
    if (minCapacity < 0) throw OutOfMemoryError();
    if (minCapacity <= buf->length) return;
    int64_t newCap = static_cast<int64_t>(buf->length) * 2;
    if (newCap < minCapacity) newCap = minCapacity;
    if (newCap > kMaxBufferSize) newCap = minCapacity > kMaxBufferSize ? minCapacity : kMaxBufferSize;
    auto* nb = new Array<int8_t>(static_cast<int32_t>(newCap));
    if (count > 0) std::memcpy(nb->data(), buf->data(), static_cast<size_t>(count));
    buf = nb;
}

void ByteArrayOutputStream::write(int32_t b) {
    ensureCapacity(count + 1);
    buf->data()[count++] = static_cast<int8_t>(b);
}

void ByteArrayOutputStream::write(Array<int8_t>* b, int32_t off, int32_t len) {
    requireNonNull(b);
    checkFromIndexSize(off, len, b->length);
    ensureCapacity(count + len);
    if (len > 0) std::memcpy(buf->data() + count, b->data() + off, static_cast<size_t>(len));
    count += len;
}

void ByteArrayOutputStream::writeBytes(Array<int8_t>* b) {
    requireNonNull(b);
    write(b, 0, b->length);
}

void ByteArrayOutputStream::writeTo(OutputStream* out) { out->write(buf, 0, count); }

Array<int8_t>* ByteArrayOutputStream::toByteArray() {
    auto* r = new Array<int8_t>(count);
    if (count > 0) std::memcpy(r->data(), buf->data(), static_cast<size_t>(count));
    return r;
}

String ByteArrayOutputStream::toString() { return Charset::defaultCharset()->decodeToString(buf->data(), count); }

String ByteArrayOutputStream::toString(const String& charsetName) {
    return Charset::forNameIO(charsetName)->decodeToString(buf->data(), count);
}

String ByteArrayOutputStream::toString(Charset* charset) {
    requireNonNull(charset);
    return charset->decodeToString(buf->data(), count);
}

// =======================================================================================
// DataInputStream / DataOutputStream

int32_t DataInputStream::read(Array<int8_t>* b) { return in->read(b, 0, b->length); }
int32_t DataInputStream::read(Array<int8_t>* b, int32_t off, int32_t len) { return in->read(b, off, len); }

void DataInputStream::readFully(Array<int8_t>* b) {
    requireNonNull(b);
    readFully(b, 0, b->length);
}

void DataInputStream::readFully(Array<int8_t>* b, int32_t off, int32_t len) {
    requireNonNull(b);
    checkFromIndexSize(off, len, b->length);
    int32_t n = 0;
    while (n < len) {
        int32_t count = in->read(b, off + n, len - n);
        if (count < 0) throw EOFException();
        n += count;
    }
}

int32_t DataInputStream::skipBytes(int32_t n) {
    int32_t total = 0;
    int32_t cur = 0;
    while (total < n && (cur = static_cast<int32_t>(in->skip(n - total))) > 0) total += cur;
    return total;
}

bool DataInputStream::readBoolean() { return readUnsignedByte() != 0; }

int8_t DataInputStream::readByte() { return static_cast<int8_t>(readUnsignedByte()); }

int32_t DataInputStream::readUnsignedByte() {
    int32_t ch = in->read();
    if (ch < 0) throw EOFException();
    return ch;
}

int16_t DataInputStream::readShort() { return static_cast<int16_t>(readUnsignedShort()); }

int32_t DataInputStream::readUnsignedShort() {
    int32_t ch1 = in->read();
    int32_t ch2 = in->read();
    if ((ch1 | ch2) < 0) throw EOFException();
    return (ch1 << 8) + ch2;
}

char16_t DataInputStream::readChar() { return static_cast<char16_t>(readUnsignedShort()); }

int32_t DataInputStream::readInt() {
    auto* b = new Array<int8_t>(4);
    readFully(b, 0, 4);
    return detail::loadBE<int32_t>(b->data());
}

int64_t DataInputStream::readLong() {
    auto* b = new Array<int8_t>(8);
    readFully(b, 0, 8);
    return detail::loadBE<int64_t>(b->data());
}

float DataInputStream::readFloat() {
    int32_t bits = readInt();
    float f;
    std::memcpy(&f, &bits, 4);
    return f;
}

double DataInputStream::readDouble() {
    int64_t bits = readLong();
    double d;
    std::memcpy(&d, &bits, 8);
    return d;
}

namespace {
// java.io.PushbackInputStream with a one-byte buffer (DataInputStream.readLine wraps its input
// in one to push back the byte after a '\r', like the JDK).
class PushbackInputStream1 final : public FilterInputStream {
public:
    explicit PushbackInputStream1(InputStream* in) : FilterInputStream(in) {}
    using FilterInputStream::read;
    int32_t read() override {
        if (pushed_ >= 0) {
            int32_t c = pushed_;
            pushed_ = -1;
            return c;
        }
        return in->read();
    }
    int32_t read(Array<int8_t>* b, int32_t off, int32_t len) override {
        requireNonNull(b);
        checkFromIndexSize(off, len, b->length);
        if (len == 0) return 0;
        if (pushed_ >= 0) {
            b->data()[off] = static_cast<int8_t>(pushed_);
            pushed_ = -1;
            if (len == 1) return 1;
            int32_t n = in->available() > 0 ? in->read(b, off + 1, len - 1) : 0;
            return n > 0 ? n + 1 : 1;
        }
        return in->read(b, off, len);
    }
    int64_t skip(int64_t n) override {
        if (n <= 0) return 0;
        if (pushed_ >= 0) {
            pushed_ = -1;
            return 1 + in->skip(n - 1);
        }
        return in->skip(n);
    }
    int32_t available() override {
        int32_t a = in->available();
        return pushed_ >= 0 ? (a == INT32_MAX ? a : a + 1) : a;
    }
    bool markSupported() override { return false; }
    void mark(int32_t) override {}
    void reset() override { throw IOException(String("mark/reset not supported")); }
    void unread(int32_t b) {
        if (pushed_ >= 0) throw IOException(String("Push back buffer is full"));
        pushed_ = b & 0xff;
    }

private:
    int32_t pushed_ = -1;
};
}  // namespace

String DataInputStream::readLine() {
    std::u16string line;
    int32_t c;
    for (;;) {
        c = in->read();
        if (c == -1 || c == '\n') break;
        if (c == '\r') {
            int32_t c2 = in->read();
            if (c2 != '\n' && c2 != -1) {
                auto* pb = dynamic_cast<PushbackInputStream1*>(in);
                if (pb == nullptr) {
                    pb = new PushbackInputStream1(in);
                    in = pb;
                }
                pb->unread(c2);
            }
            break;
        }
        line.push_back(static_cast<char16_t>(c));
    }
    if (c == -1 && line.empty()) return String();
    return String::fromUtf16(line);
}

String DataInputStream::readUTF() { return readUTF(this); }

String DataInputStream::readUTF(DataInputStream* in) {
    int32_t utflen = in->readUnsignedShort();
    auto* bytearr = new Array<int8_t>(utflen);
    in->readFully(bytearr, 0, utflen);
    std::u16string chars;
    chars.reserve(static_cast<size_t>(utflen));
    const uint8_t* b = reinterpret_cast<const uint8_t*>(bytearr->data());
    int32_t count = 0;
    while (count < utflen) {
        int32_t c = b[count];
        if (c > 127) break;
        count++;
        chars.push_back(static_cast<char16_t>(c));
    }
    while (count < utflen) {
        int32_t c = b[count];
        switch (c >> 4) {
            case 0: case 1: case 2: case 3: case 4: case 5: case 6: case 7:
                count++;
                chars.push_back(static_cast<char16_t>(c));
                break;
            case 12:
            case 13: {
                count += 2;
                if (count > utflen) throw UTFDataFormatException(String("malformed input: partial character at end"));
                int32_t char2 = b[count - 1];
                if ((char2 & 0xC0) != 0x80) throw UTFDataFormatException(str("malformed input around byte ", count));
                chars.push_back(static_cast<char16_t>(((c & 0x1F) << 6) | (char2 & 0x3F)));
                break;
            }
            case 14: {
                count += 3;
                if (count > utflen) throw UTFDataFormatException(String("malformed input: partial character at end"));
                int32_t char2 = b[count - 2];
                int32_t char3 = b[count - 1];
                if (((char2 & 0xC0) != 0x80) || ((char3 & 0xC0) != 0x80))
                    throw UTFDataFormatException(str("malformed input around byte ", count - 1));
                chars.push_back(static_cast<char16_t>(((c & 0x0F) << 12) | ((char2 & 0x3F) << 6) | (char3 & 0x3F)));
                break;
            }
            default:
                throw UTFDataFormatException(str("malformed input around byte ", count));
        }
    }
    return String::fromUtf16(chars);
}

void DataOutputStream::incCount(int32_t v) {
    int32_t t = written + v;
    if (t < 0) t = INT32_MAX;
    written = t;
}

void DataOutputStream::write(int32_t b) {
    out->write(b);
    incCount(1);
}

void DataOutputStream::write(Array<int8_t>* b, int32_t off, int32_t len) {
    out->write(b, off, len);
    incCount(len);
}

void DataOutputStream::flush() { out->flush(); }

void DataOutputStream::writeBoolean(bool v) {
    out->write(v ? 1 : 0);
    incCount(1);
}

void DataOutputStream::writeByte(int32_t v) {
    out->write(v);
    incCount(1);
}

void DataOutputStream::writeShort(int32_t v) {
    auto* b = new Array<int8_t>(2);
    detail::storeBE<int16_t>(b->data(), static_cast<int16_t>(v));
    out->write(b, 0, 2);
    incCount(2);
}

void DataOutputStream::writeChar(int32_t v) { writeShort(v); }

void DataOutputStream::writeInt(int32_t v) {
    auto* b = new Array<int8_t>(4);
    detail::storeBE<int32_t>(b->data(), v);
    out->write(b, 0, 4);
    incCount(4);
}

void DataOutputStream::writeLong(int64_t v) {
    auto* b = new Array<int8_t>(8);
    detail::storeBE<int64_t>(b->data(), v);
    out->write(b, 0, 8);
    incCount(8);
}

void DataOutputStream::writeFloat(float v) {
    int32_t bits;
    if (v != v) bits = 0x7fc00000;  // floatToIntBits (canonical NaN)
    else std::memcpy(&bits, &v, 4);
    writeInt(bits);
}

void DataOutputStream::writeDouble(double v) {
    int64_t bits;
    if (v != v) bits = INT64_C(0x7ff8000000000000);
    else std::memcpy(&bits, &v, 8);
    writeLong(bits);
}

void DataOutputStream::writeBytes(const String& s) {
    std::u16string u = s.toUtf16();
    auto* b = new Array<int8_t>(static_cast<int32_t>(u.size()));
    for (size_t i = 0; i < u.size(); i++) b->data()[i] = static_cast<int8_t>(u[i]);
    out->write(b, 0, b->length);
    incCount(b->length);
}

void DataOutputStream::writeChars(const String& s) {
    std::u16string u = s.toUtf16();
    auto* b = new Array<int8_t>(static_cast<int32_t>(u.size() * 2));
    for (size_t i = 0; i < u.size(); i++) detail::storeBE<uint16_t>(b->data() + 2 * i, u[i]);
    out->write(b, 0, b->length);
    incCount(b->length);
}

void DataOutputStream::writeUTF(const String& s) {
    std::u16string u = s.toUtf16();
    int64_t utflen = 0;
    for (char16_t c : u) {
        if (c >= 0x0001 && c <= 0x007F) utflen++;
        else if (c > 0x07FF) utflen += 3;
        else utflen += 2;
    }
    if (utflen > 65535 || utflen < static_cast<int64_t>(u.size()))
        throw UTFDataFormatException(str("encoded string too long: ", utflen, " bytes"));
    auto* b = new Array<int8_t>(static_cast<int32_t>(utflen + 2));
    int8_t* p = b->data();
    int32_t count = 0;
    p[count++] = static_cast<int8_t>((utflen >> 8) & 0xFF);
    p[count++] = static_cast<int8_t>(utflen & 0xFF);
    for (char16_t c : u) {
        if (c >= 0x0001 && c <= 0x007F) {
            p[count++] = static_cast<int8_t>(c);
        } else if (c > 0x07FF) {
            p[count++] = static_cast<int8_t>(0xE0 | ((c >> 12) & 0x0F));
            p[count++] = static_cast<int8_t>(0x80 | ((c >> 6) & 0x3F));
            p[count++] = static_cast<int8_t>(0x80 | (c & 0x3F));
        } else {
            p[count++] = static_cast<int8_t>(0xC0 | ((c >> 6) & 0x1F));
            p[count++] = static_cast<int8_t>(0x80 | (c & 0x3F));
        }
    }
    out->write(b, 0, count);
    incCount(count);
}

// =======================================================================================
// OutputStreamPrintStream

OutputStreamPrintStream::OutputStreamPrintStream(OutputStream* out, bool autoFlush)
    : out_(out), charset_(Charset::defaultCharset()), autoFlush_(autoFlush) {
    requireNonNull(out);
}

OutputStreamPrintStream::OutputStreamPrintStream(OutputStream* out, bool autoFlush, const String& encoding)
    : out_(out), charset_(Charset::forNameIO(encoding)), autoFlush_(autoFlush) {
    requireNonNull(out);
}

OutputStreamPrintStream::OutputStreamPrintStream(File* file)
    : out_(new FileOutputStream(file)), charset_(Charset::defaultCharset()), autoFlush_(false) {}

void OutputStreamPrintStream::writeBytes(const char* data, size_t n) {
    try {
        if (charset_->kind() == Charset::UTF_8_KIND) {
            auto* a = new Array<int8_t>(static_cast<int32_t>(n));
            if (n > 0) std::memcpy(a->data(), data, n);
            out_->write(a, 0, a->length);
        } else {
            out_->write(charset_->encodeToArray(String(data, n)));
        }
        if (autoFlush_ && std::memchr(data, '\n', n) != nullptr) out_->flush();
    } catch (IOException&) {
        setError();
    }
}

void OutputStreamPrintStream::write(int32_t b) {
    JSYNC(this) {
        try {
            out_->write(b);
            if (b == '\n' && autoFlush_) out_->flush();
        } catch (IOException&) {
            setError();
        }
    }
}

void OutputStreamPrintStream::write(Array<int8_t>* buf, int32_t off, int32_t len) {
    JSYNC(this) {
        try {
            out_->write(buf, off, len);
            if (autoFlush_) out_->flush();
        } catch (IOException&) {
            setError();
        }
    }
}

void OutputStreamPrintStream::flush() {
    JSYNC(this) {
        try {
            out_->flush();
        } catch (IOException&) {
            setError();
        }
    }
}

void OutputStreamPrintStream::close() {
    JSYNC(this) {
        if (!closing_) {
            closing_ = true;
            try {
                out_->close();
            } catch (IOException&) {
                setError();
            }
        }
    }
}

// =======================================================================================
// Reader

int32_t Reader::read() {
    auto* cb = new Array<char16_t>(1);
    if (read(cb, 0, 1) == -1) return -1;
    return cb->data()[0];
}

int32_t Reader::read(Array<char16_t>* cbuf) {
    requireNonNull(cbuf);
    return read(cbuf, 0, cbuf->length);
}

int32_t Reader::read(CharBuffer* target) {
    requireNonNull(target);
    int32_t len = target->remaining();
    auto* cbuf = new Array<char16_t>(len);
    int32_t n = read(cbuf, 0, len);
    if (n > 0) target->put(cbuf, 0, n);
    return n;
}

int64_t Reader::skip(int64_t n) {
    if (n < 0) throw IllegalArgumentException(String("skip value is negative"));
    int32_t nn = static_cast<int32_t>(n < 8192 ? n : 8192);
    JSYNC(lock) {
        auto* skipBuffer = new Array<char16_t>(nn > 0 ? nn : 1);
        int64_t r = n;
        while (r > 0) {
            int32_t nc = read(skipBuffer, 0, static_cast<int32_t>(r < nn ? r : nn));
            if (nc == -1) break;
            r -= nc;
        }
        return n - r;
    }
    return 0;
}

void Reader::mark(int32_t) { throw IOException(String("mark() not supported")); }
void Reader::reset() { throw IOException(String("reset() not supported")); }

int64_t Reader::transferTo(Writer* out) {
    requireNonNull(out);
    int64_t transferred = 0;
    auto* buffer = new Array<char16_t>(8192);
    int32_t nRead;
    while ((nRead = read(buffer, 0, 8192)) >= 0) {
        out->write(buffer, 0, nRead);
        transferred += nRead;
    }
    return transferred;
}

namespace {
class NullReader final : public Reader {
public:
    using Reader::read;
    int32_t read(Array<char16_t>* cbuf, int32_t off, int32_t len) override {
        requireNonNull(cbuf);
        checkFromIndexSize(off, len, cbuf->length);
        if (closed_) throw IOException(String("Stream closed"));
        return len == 0 ? 0 : -1;
    }
    void close() override { closed_ = true; }

private:
    bool closed_ = false;
};
class NullWriter final : public Writer {
public:
    using Writer::write;
    void write(Array<char16_t>* cbuf, int32_t off, int32_t len) override {
        requireNonNull(cbuf);
        checkFromIndexSize(off, len, cbuf->length);
        if (closed_) throw IOException(String("Stream closed"));
    }
    void flush() override {
        if (closed_) throw IOException(String("Stream closed"));
    }
    void close() override { closed_ = true; }

private:
    bool closed_ = false;
};
}  // namespace

Reader* Reader::nullReader() { return new NullReader(); }
Writer* Writer::nullWriter() { return new NullWriter(); }

// =======================================================================================
// InputStreamReader / FileReader

InputStreamReader::InputStreamReader(InputStream* in) : InputStreamReader(in, Charset::defaultCharset()) {}

InputStreamReader::InputStreamReader(InputStream* in, const String& charsetName)
    : Reader(in), in_(in), bytes_(new Array<int8_t>(8192)) {
    if (charsetName.isNull()) throw NullPointerException(String("charsetName"));
    cs_ = Charset::forNameIO(charsetName);
    dec_.kind = cs_->kind();
}

InputStreamReader::InputStreamReader(InputStream* in, Charset* cs)
    : Reader(in), in_(in), cs_(cs), bytes_(new Array<int8_t>(8192)) {
    if (in == nullptr || cs == nullptr) throw NullPointerException();
    dec_.kind = cs_->kind();
}

String InputStreamReader::getEncoding() {
    if (closed_) return String();
    return cs_->historicalName();
}

bool InputStreamReader::fillChars() {
    while (pendingPos_ >= pending_.size()) {
        pending_.clear();
        pendingPos_ = 0;
        if (eof_) return false;
        int32_t n = in_->read(bytes_, 0, bytes_->length);
        if (n < 0) {
            eof_ = true;
            dec_.decode(nullptr, 0, pending_, true);
            if (pending_.empty()) return false;
            return true;
        }
        dec_.decode(reinterpret_cast<const uint8_t*>(bytes_->data()), static_cast<size_t>(n), pending_, false);
    }
    return true;
}

int32_t InputStreamReader::read() {
    JSYNC(lock) {
        if (closed_) throw IOException(String("Stream closed"));
        if (!fillChars()) return -1;
        return pending_[pendingPos_++];
    }
    return -1;
}

int32_t InputStreamReader::read(Array<char16_t>* cbuf, int32_t off, int32_t len) {
    requireNonNull(cbuf);
    checkFromIndexSize(off, len, cbuf->length);
    JSYNC(lock) {
        if (closed_) throw IOException(String("Stream closed"));
        if (len == 0) return 0;
        if (!fillChars()) return -1;
        int32_t n = 0;
        while (n < len) {
            size_t avail = pending_.size() - pendingPos_;
            if (avail == 0) {
                if (n > 0 && in_->available() <= 0) break;
                if (!fillChars()) break;
                continue;
            }
            int32_t c = static_cast<int32_t>(avail < static_cast<size_t>(len - n) ? avail : static_cast<size_t>(len - n));
            std::memcpy(cbuf->data() + off + n, pending_.data() + pendingPos_, static_cast<size_t>(c) * sizeof(char16_t));
            pendingPos_ += static_cast<size_t>(c);
            n += c;
        }
        return n;
    }
    return -1;
}

bool InputStreamReader::ready() {
    JSYNC(lock) {
        if (closed_) throw IOException(String("Stream closed"));
        return pendingPos_ < pending_.size() || in_->available() > 0;
    }
    return false;
}

void InputStreamReader::close() {
    JSYNC(lock) {
        if (closed_) return;
        closed_ = true;
        in_->close();
    }
}

FileReader::FileReader(File* file) : InputStreamReader(new FileInputStream(file)) {}
FileReader::FileReader(const String& fileName) : InputStreamReader(new FileInputStream(fileName)) {}
FileReader::FileReader(File* file, Charset* cs) : InputStreamReader(new FileInputStream(file), cs) {}
FileReader::FileReader(const String& fileName, Charset* cs) : InputStreamReader(new FileInputStream(fileName), cs) {}

// =======================================================================================
// BufferedReader (JDK algorithm)

namespace {
constexpr int32_t kInvalidated = -2;
constexpr int32_t kUnmarked = -1;
}  // namespace

BufferedReader::BufferedReader(Reader* in, int32_t sz) : Reader(in), in_(in) {
    if (sz <= 0) throw IllegalArgumentException(String("Buffer size <= 0"));
    cb_ = new Array<char16_t>(sz);
}

void BufferedReader::ensureOpen() {
    if (in_ == nullptr) throw IOException(String("Stream closed"));
}

void BufferedReader::fill() {
    int32_t dst;
    if (markedChar_ <= kUnmarked) {
        dst = 0;
    } else {
        int32_t delta = nextChar_ - markedChar_;
        if (delta >= readAheadLimit_) {
            markedChar_ = kInvalidated;
            readAheadLimit_ = 0;
            dst = 0;
        } else {
            if (readAheadLimit_ <= cb_->length) {
                std::memmove(cb_->data(), cb_->data() + markedChar_, static_cast<size_t>(delta) * sizeof(char16_t));
                markedChar_ = 0;
                dst = delta;
            } else {
                auto* ncb = new Array<char16_t>(readAheadLimit_);
                std::memcpy(ncb->data(), cb_->data() + markedChar_, static_cast<size_t>(delta) * sizeof(char16_t));
                cb_ = ncb;
                markedChar_ = 0;
                dst = delta;
            }
            nextChar_ = nChars_ = delta;
        }
    }
    int32_t n;
    do {
        n = in_->read(cb_, dst, cb_->length - dst);
    } while (n == 0);
    if (n > 0) {
        nChars_ = dst + n;
        nextChar_ = dst;
    }
}

int32_t BufferedReader::read() {
    JSYNC(lock) {
        ensureOpen();
        for (;;) {
            if (nextChar_ >= nChars_) {
                fill();
                if (nextChar_ >= nChars_) return -1;
            }
            if (skipLF_) {
                skipLF_ = false;
                if (cb_->data()[nextChar_] == '\n') {
                    nextChar_++;
                    continue;
                }
            }
            return cb_->data()[nextChar_++];
        }
    }
    return -1;
}

int32_t BufferedReader::read1(Array<char16_t>* cbuf, int32_t off, int32_t len) {
    if (nextChar_ >= nChars_) {
        if (len >= cb_->length && markedChar_ <= kUnmarked && !skipLF_) return in_->read(cbuf, off, len);
        fill();
    }
    if (nextChar_ >= nChars_) return -1;
    if (skipLF_) {
        skipLF_ = false;
        if (cb_->data()[nextChar_] == '\n') {
            nextChar_++;
            if (nextChar_ >= nChars_) fill();
            if (nextChar_ >= nChars_) return -1;
        }
    }
    int32_t n = len < nChars_ - nextChar_ ? len : nChars_ - nextChar_;
    std::memcpy(cbuf->data() + off, cb_->data() + nextChar_, static_cast<size_t>(n) * sizeof(char16_t));
    nextChar_ += n;
    return n;
}

int32_t BufferedReader::read(Array<char16_t>* cbuf, int32_t off, int32_t len) {
    requireNonNull(cbuf);
    JSYNC(lock) {
        ensureOpen();
        checkFromIndexSize(off, len, cbuf->length);
        if (len == 0) return 0;
        int32_t n = read1(cbuf, off, len);
        if (n <= 0) return n;
        while (n < len && in_->ready()) {
            int32_t n1 = read1(cbuf, off + n, len - n);
            if (n1 <= 0) break;
            n += n1;
        }
        return n;
    }
    return -1;
}

String BufferedReader::readLine() {
    JSYNC(lock) {
        ensureOpen();
        std::u16string s;
        bool haveS = false;
        bool omitLF = skipLF_;
        for (;;) {
            if (nextChar_ >= nChars_) fill();
            if (nextChar_ >= nChars_) {  // EOF
                if (haveS && !s.empty()) return String::fromUtf16(s);
                if (haveS) return String("");
                return String();
            }
            bool eol = false;
            char16_t c = 0;
            int32_t i;
            if (omitLF && cb_->data()[nextChar_] == '\n') nextChar_++;
            skipLF_ = false;
            omitLF = false;
            for (i = nextChar_; i < nChars_; i++) {
                c = cb_->data()[i];
                if (c == '\n' || c == '\r') {
                    eol = true;
                    break;
                }
            }
            int32_t startChar = nextChar_;
            nextChar_ = i;
            if (eol) {
                s.append(cb_->data() + startChar, static_cast<size_t>(i - startChar));
                nextChar_++;
                if (c == '\r') skipLF_ = true;
                return String::fromUtf16(s);
            }
            s.append(cb_->data() + startChar, static_cast<size_t>(i - startChar));
            haveS = true;
        }
    }
    return String();
}

int64_t BufferedReader::skip(int64_t n) {
    if (n < 0) throw IllegalArgumentException(String("skip value is negative"));
    JSYNC(lock) {
        ensureOpen();
        int64_t r = n;
        while (r > 0) {
            if (nextChar_ >= nChars_) fill();
            if (nextChar_ >= nChars_) break;
            if (skipLF_) {
                skipLF_ = false;
                if (cb_->data()[nextChar_] == '\n') nextChar_++;
            }
            int64_t d = nChars_ - nextChar_;
            if (r <= d) {
                nextChar_ += static_cast<int32_t>(r);
                r = 0;
                break;
            }
            r -= d;
            nextChar_ = nChars_;
        }
        return n - r;
    }
    return 0;
}

bool BufferedReader::ready() {
    JSYNC(lock) {
        ensureOpen();
        if (skipLF_) {
            if (nextChar_ >= nChars_ && in_->ready()) fill();
            if (nextChar_ < nChars_) {
                if (cb_->data()[nextChar_] == '\n') nextChar_++;
                skipLF_ = false;
            }
        }
        return nextChar_ < nChars_ || in_->ready();
    }
    return false;
}

void BufferedReader::mark(int32_t readAheadLimit) {
    if (readAheadLimit < 0) throw IllegalArgumentException(String("Read-ahead limit < 0"));
    JSYNC(lock) {
        ensureOpen();
        readAheadLimit_ = readAheadLimit;
        markedChar_ = nextChar_;
        markedSkipLF_ = skipLF_;
    }
}

void BufferedReader::reset() {
    JSYNC(lock) {
        ensureOpen();
        if (markedChar_ < 0)
            throw IOException(String(markedChar_ == kInvalidated ? "Mark invalid" : "Stream not marked"));
        nextChar_ = markedChar_;
        skipLF_ = markedSkipLF_;
    }
}

void BufferedReader::close() {
    JSYNC(lock) {
        if (in_ == nullptr) return;
        Reader* r = in_;
        in_ = nullptr;
        cb_ = nullptr;
        r->close();
    }
}

List<String>* BufferedReader::lines() {
    auto* l = new List<String>();
    for (;;) {
        String s = readLine();
        if (s.isNull()) break;
        l->add(s);
    }
    return l;
}

// =======================================================================================
// StringReader

StringReader::StringReader(const String& s) : s_(s.toUtf16()) {}

int32_t StringReader::read() {
    JSYNC(lock) {
        if (closed_) throw IOException(String("Stream closed"));
        if (next_ >= s_.size()) return -1;
        return s_[next_++];
    }
    return -1;
}

int32_t StringReader::read(Array<char16_t>* cbuf, int32_t off, int32_t len) {
    requireNonNull(cbuf);
    JSYNC(lock) {
        if (closed_) throw IOException(String("Stream closed"));
        checkFromIndexSize(off, len, cbuf->length);
        if (len == 0) return 0;
        if (next_ >= s_.size()) return -1;
        size_t n = s_.size() - next_;
        if (n > static_cast<size_t>(len)) n = static_cast<size_t>(len);
        std::memcpy(cbuf->data() + off, s_.data() + next_, n * sizeof(char16_t));
        next_ += n;
        return static_cast<int32_t>(n);
    }
    return -1;
}

int64_t StringReader::skip(int64_t n) {
    JSYNC(lock) {
        if (closed_) throw IOException(String("Stream closed"));
        if (next_ >= s_.size()) return 0;
        int64_t lo = -static_cast<int64_t>(next_);
        int64_t hi = static_cast<int64_t>(s_.size() - next_);
        int64_t r = n < hi ? n : hi;
        if (r < lo) r = lo;
        next_ = static_cast<size_t>(static_cast<int64_t>(next_) + r);
        return r;
    }
    return 0;
}

bool StringReader::ready() {
    if (closed_) throw IOException(String("Stream closed"));
    return true;
}

void StringReader::mark(int32_t readAheadLimit) {
    if (readAheadLimit < 0) throw IllegalArgumentException(String("Read-ahead limit < 0"));
    if (closed_) throw IOException(String("Stream closed"));
    mark_ = next_;
}

void StringReader::reset() {
    if (closed_) throw IOException(String("Stream closed"));
    next_ = mark_;
}

// =======================================================================================
// Writer

void Writer::write(int32_t c) {
    char16_t ch = static_cast<char16_t>(c);
    writeUnits(&ch, 1);
}

void Writer::write(Array<char16_t>* cbuf) {
    requireNonNull(cbuf);
    write(cbuf, 0, cbuf->length);
}

void Writer::writeUnits(const char16_t* p, int32_t n) {
    auto* a = new Array<char16_t>(n);
    if (n > 0) std::memcpy(a->data(), p, static_cast<size_t>(n) * sizeof(char16_t));
    write(a, 0, n);
}

void Writer::write(const String& str) {
    if (str.isNull()) throw NullPointerException();
    std::u16string u = str.toUtf16();
    writeUnits(u.data(), static_cast<int32_t>(u.size()));
}

void Writer::write(const String& str, int32_t off, int32_t len) {
    if (str.isNull()) throw NullPointerException();
    checkFromIndexSize(off, len, str.length());
    write(str.substring(off, off + len));
}

Writer* Writer::append(const String& csq) {
    write(csq.isNull() ? String("null") : csq);
    return this;
}

Writer* Writer::append(const String& csq, int32_t start, int32_t end) {
    String s = csq.isNull() ? String("null") : csq;
    write(s.substring(start, end));
    return this;
}

Writer* Writer::append(char16_t c) {
    write(static_cast<int32_t>(c));
    return this;
}

// =======================================================================================
// OutputStreamWriter / FileWriter

OutputStreamWriter::OutputStreamWriter(OutputStream* out) : OutputStreamWriter(out, Charset::defaultCharset()) {}

OutputStreamWriter::OutputStreamWriter(OutputStream* out, const String& charsetName) : Writer(out), out_(out) {
    if (charsetName.isNull()) throw NullPointerException(String("charsetName"));
    cs_ = Charset::forNameIO(charsetName);
    enc_.kind = cs_->kind();
}

OutputStreamWriter::OutputStreamWriter(OutputStream* out, Charset* cs) : Writer(out), out_(out), cs_(cs) {
    if (out == nullptr || cs == nullptr) throw NullPointerException();
    enc_.kind = cs_->kind();
}

String OutputStreamWriter::getEncoding() {
    if (closed_) return String();
    return cs_->historicalName();
}

void OutputStreamWriter::ensureOpen() {
    if (closed_) throw IOException(String("Stream closed"));
}

void OutputStreamWriter::flushBytes() {
    if (bytes_.empty()) return;
    auto* a = new Array<int8_t>(static_cast<int32_t>(bytes_.size()));
    std::memcpy(a->data(), bytes_.data(), bytes_.size());
    bytes_.clear();
    out_->write(a, 0, a->length);
}

void OutputStreamWriter::writeUnits(const char16_t* p, int32_t n) {
    JSYNC(lock) {
        ensureOpen();
        enc_.encode(p, static_cast<size_t>(n), bytes_, false);
        if (bytes_.size() >= 8192) flushBytes();
    }
}

void OutputStreamWriter::write(int32_t c) {
    char16_t ch = static_cast<char16_t>(c);
    writeUnits(&ch, 1);
}

void OutputStreamWriter::write(Array<char16_t>* cbuf, int32_t off, int32_t len) {
    requireNonNull(cbuf);
    checkFromIndexSize(off, len, cbuf->length);
    if (len == 0) return;
    writeUnits(cbuf->data() + off, len);
}

void OutputStreamWriter::write(const String& str) {
    if (str.isNull()) throw NullPointerException();
    if (enc_.kind == Charset::UTF_8_KIND && enc_.pendingHigh == 0) {
        std::string bytes = detail::encodeString(Charset::UTF_8_KIND, str);
        JSYNC(lock) {
            ensureOpen();
            bytes_ += bytes;
            if (bytes_.size() >= 8192) flushBytes();
        }
        return;
    }
    Writer::write(str);
}

void OutputStreamWriter::flush() {
    JSYNC(lock) {
        ensureOpen();
        flushBytes();
        out_->flush();
    }
}

void OutputStreamWriter::close() {
    JSYNC(lock) {
        if (closed_) return;
        {
            JFINALLY {
                closed_ = true;
            };
            enc_.encode(nullptr, 0, bytes_, true);
            try {
                flushBytes();
                out_->flush();
            } catch (...) {
                try {
                    out_->close();
                } catch (...) {
                }
                throw;
            }
            out_->close();
        }
    }
}

FileWriter::FileWriter(File* file, bool append) : OutputStreamWriter(new FileOutputStream(file, append)) {}
FileWriter::FileWriter(const String& fileName, bool append) : OutputStreamWriter(new FileOutputStream(fileName, append)) {}
FileWriter::FileWriter(File* file, Charset* cs, bool append) : OutputStreamWriter(new FileOutputStream(file, append), cs) {}

// =======================================================================================
// BufferedWriter

BufferedWriter::BufferedWriter(Writer* out, int32_t sz) : Writer(out), out_(out) {
    if (sz <= 0) throw IllegalArgumentException(String("Buffer size <= 0"));
    cb_ = new Array<char16_t>(sz);
    nChars_ = sz;
}

void BufferedWriter::ensureOpen() {
    if (out_ == nullptr) throw IOException(String("Stream closed"));
}

void BufferedWriter::flushBuffer() {
    JSYNC(lock) {
        ensureOpen();
        if (nextChar_ == 0) return;
        out_->write(cb_, 0, nextChar_);
        nextChar_ = 0;
    }
}

void BufferedWriter::write(int32_t c) {
    JSYNC(lock) {
        ensureOpen();
        if (nextChar_ >= nChars_) flushBuffer();
        cb_->data()[nextChar_++] = static_cast<char16_t>(c);
    }
}

void BufferedWriter::write(Array<char16_t>* cbuf, int32_t off, int32_t len) {
    requireNonNull(cbuf);
    JSYNC(lock) {
        ensureOpen();
        checkFromIndexSize(off, len, cbuf->length);
        if (len == 0) return;
        if (len >= nChars_) {
            flushBuffer();
            out_->write(cbuf, off, len);
            return;
        }
        writeUnits(cbuf->data() + off, len);
    }
}

void BufferedWriter::writeUnits(const char16_t* p, int32_t n) {
    JSYNC(lock) {
        ensureOpen();
        int32_t b = 0;
        while (b < n) {
            int32_t d = nChars_ - nextChar_;
            if (n - b < d) d = n - b;
            std::memcpy(cb_->data() + nextChar_, p + b, static_cast<size_t>(d) * sizeof(char16_t));
            b += d;
            nextChar_ += d;
            if (nextChar_ >= nChars_) flushBuffer();
        }
    }
}

void BufferedWriter::write(const String& s) { Writer::write(s); }

void BufferedWriter::newLine() {
    String sep = System::lineSeparator();
    Writer::write(sep);
}

void BufferedWriter::flush() {
    JSYNC(lock) {
        flushBuffer();
        out_->flush();
    }
}

void BufferedWriter::close() {
    JSYNC(lock) {
        if (out_ == nullptr) return;
        Writer* w = out_;
        {
            JFINALLY {
                out_ = nullptr;
                cb_ = nullptr;
            };
            try {
                flushBuffer();
            } catch (...) {
                try {
                    w->close();
                } catch (...) {
                }
                throw;
            }
            w->close();
        }
    }
}

// =======================================================================================
// StringWriter

StringWriter::StringWriter(int32_t initialSize) {
    if (initialSize < 0) throw IllegalArgumentException(String("Negative buffer size"));
}

void StringWriter::write(int32_t c) { buf_->append(static_cast<char16_t>(c)); }

void StringWriter::write(Array<char16_t>* cbuf, int32_t off, int32_t len) {
    requireNonNull(cbuf);
    checkFromIndexSize(off, len, cbuf->length);
    buf_->append(cbuf, off, len);
}

void StringWriter::write(const String& str) {
    if (str.isNull()) throw NullPointerException();
    buf_->append(str);
}

void StringWriter::writeUnits(const char16_t* p, int32_t n) {
    buf_->append(String::fromUtf16(p, static_cast<size_t>(n)));
}

// =======================================================================================
// PrintWriter

PrintWriter::PrintWriter(Writer* out, bool autoFlush) : Writer(out), out_(out), autoFlush_(autoFlush) {
    requireNonNull(out);
}

PrintWriter::PrintWriter(OutputStream* out, bool autoFlush)
    : PrintWriter(static_cast<Writer*>(new BufferedWriter(new OutputStreamWriter(out))), autoFlush) {}

PrintWriter::PrintWriter(const String& fileName)
    : PrintWriter(static_cast<Writer*>(new BufferedWriter(new OutputStreamWriter(new FileOutputStream(fileName)))), false) {}

PrintWriter::PrintWriter(const String& fileName, const String& csn)
    : PrintWriter(static_cast<Writer*>(new BufferedWriter(new OutputStreamWriter(new FileOutputStream(fileName), csn))),
                  false) {}

PrintWriter::PrintWriter(File* file)
    : PrintWriter(static_cast<Writer*>(new BufferedWriter(new OutputStreamWriter(new FileOutputStream(file)))), false) {}

PrintWriter::PrintWriter(File* file, const String& csn)
    : PrintWriter(static_cast<Writer*>(new BufferedWriter(new OutputStreamWriter(new FileOutputStream(file), csn))),
                  false) {}

void PrintWriter::printRaw(const std::string& utf8, bool newline) {
    JSYNC(lock) {
        if (out_ == nullptr) {
            trouble_ = true;
            return;
        }
        try {
            if (!utf8.empty()) out_->write(String(utf8));
            if (newline) {
                out_->write(System::lineSeparator());
                if (autoFlush_) out_->flush();
            }
        } catch (InterruptedIOException&) {
            trouble_ = true;
        } catch (IOException&) {
            trouble_ = true;
        }
    }
}

void PrintWriter::formatRaw(const String& s) {
    printRaw(std::string(s), false);
    if (autoFlush_) {
        JSYNC(lock) {
            try {
                if (out_ != nullptr) out_->flush();
            } catch (IOException&) {
                trouble_ = true;
            }
        }
    }
}

void PrintWriter::print(Array<char16_t>* s) {
    requireNonNull(s);
    write(s, 0, s->length);
}

void PrintWriter::println(Array<char16_t>* s) {
    JSYNC(lock) {
        print(s);
        println();
    }
}

PrintWriter* PrintWriter::append(const String& csq) {
    write(csq.isNull() ? String("null") : csq);
    return this;
}

PrintWriter* PrintWriter::append(const String& csq, int32_t start, int32_t end) {
    String s = csq.isNull() ? String("null") : csq;
    write(s.substring(start, end));
    return this;
}

PrintWriter* PrintWriter::append(char16_t c) {
    write(static_cast<int32_t>(c));
    return this;
}

void PrintWriter::write(int32_t c) {
    JSYNC(lock) {
        if (out_ == nullptr) {
            trouble_ = true;
            return;
        }
        try {
            out_->write(c);
        } catch (IOException&) {
            trouble_ = true;
        }
    }
}

void PrintWriter::write(Array<char16_t>* buf, int32_t off, int32_t len) {
    JSYNC(lock) {
        if (out_ == nullptr) {
            trouble_ = true;
            return;
        }
        try {
            out_->write(buf, off, len);
        } catch (IOException&) {
            trouble_ = true;
        }
    }
}

void PrintWriter::writeUnits(const char16_t* p, int32_t n) { write(String::fromUtf16(p, static_cast<size_t>(n))); }

void PrintWriter::write(const String& s) {
    JSYNC(lock) {
        if (out_ == nullptr) {
            trouble_ = true;
            return;
        }
        try {
            out_->write(s);
        } catch (IOException&) {
            trouble_ = true;
        }
    }
}

void PrintWriter::write(const String& s, int32_t off, int32_t len) { write(s.substring(off, off + len)); }

void PrintWriter::flush() {
    JSYNC(lock) {
        if (out_ == nullptr) {
            trouble_ = true;
            return;
        }
        try {
            out_->flush();
        } catch (IOException&) {
            trouble_ = true;
        }
    }
}

void PrintWriter::close() {
    JSYNC(lock) {
        if (out_ == nullptr) return;
        try {
            out_->close();
        } catch (IOException&) {
            trouble_ = true;
        }
        out_ = nullptr;
    }
}

bool PrintWriter::checkError() {
    if (out_ != nullptr) flush();
    return trouble_;
}

// =======================================================================================
// LineIterator (commons-io)

LineIterator::LineIterator(Reader* reader) {
    if (reader == nullptr) throw IllegalArgumentException(String("Reader must not be null"));
    if (auto* br = dynamic_cast<BufferedReader*>(reader)) bufferedReader_ = br;
    else bufferedReader_ = new BufferedReader(reader);
}

bool LineIterator::hasNext() {
    if (!cachedLine_.isNull()) return true;
    if (finished_) return false;
    try {
        for (;;) {
            String line = bufferedReader_->readLine();
            if (line.isNull()) {
                finished_ = true;
                return false;
            }
            if (isValidLine(line)) {
                cachedLine_ = line;
                return true;
            }
        }
    } catch (IOException& ioe) {
        close();
        throw IllegalStateException(ioe.toString(), ioe);
    }
}

String LineIterator::nextLine() {
    if (!hasNext()) throw NoSuchElementException(String("No more lines"));
    String current = cachedLine_;
    cachedLine_ = String();
    return current;
}

void LineIterator::close() {
    finished_ = true;
    IOUtils::closeQuietly(bufferedReader_);
    cachedLine_ = String();
}

void LineIterator::remove() { throw UnsupportedOperationException(String("Remove unsupported on LineIterator")); }

void LineIterator::closeQuietly(LineIterator* iterator) {
    if (iterator != nullptr) iterator->close();
}

// =======================================================================================
// IOUtils (commons-io)

Array<int8_t>* IOUtils::toByteArray(InputStream* input) {
    auto* out = new ByteArrayOutputStream();
    copyLarge(input, out);
    return out->toByteArray();
}

Array<int8_t>* IOUtils::toByteArray(Reader* input) {
    auto* out = new ByteArrayOutputStream();
    auto* w = new OutputStreamWriter(out);
    copy(input, w);
    w->flush();
    return out->toByteArray();
}

String IOUtils::toString(InputStream* input) { return toString(input, String()); }

String IOUtils::toString(InputStream* input, const String& encoding) {
    Array<int8_t>* bytes = toByteArray(input);
    Charset* cs = encoding.isNull() ? Charset::defaultCharset() : Charset::forNameIO(encoding);
    return cs->decodeToString(bytes);
}

String IOUtils::toString(Reader* input) {
    auto* sw = new StringWriter();
    copy(input, sw);
    return sw->toString();
}

String IOUtils::toString(Array<int8_t>* input) { return Charset::defaultCharset()->decodeToString(input); }

List<String>* IOUtils::readLines(InputStream* input) { return readLines(input, String()); }

List<String>* IOUtils::readLines(InputStream* input, const String& encoding) {
    Reader* reader = encoding.isNull() ? new InputStreamReader(input) : new InputStreamReader(input, encoding);
    return readLines(reader);
}

List<String>* IOUtils::readLines(Reader* input) {
    auto* reader = new BufferedReader(input);
    auto* list = new List<String>();
    String line = reader->readLine();
    while (!line.isNull()) {
        list->add(line);
        line = reader->readLine();
    }
    return list;
}

LineIterator* IOUtils::lineIterator(InputStream* input, const String& encoding) {
    Reader* reader = encoding.isNull() ? new InputStreamReader(input) : new InputStreamReader(input, encoding);
    return new LineIterator(reader);
}

int32_t IOUtils::copy(InputStream* input, OutputStream* output) {
    int64_t count = copyLarge(input, output);
    if (count > INT32_MAX) return -1;
    return static_cast<int32_t>(count);
}

int64_t IOUtils::copyLarge(InputStream* input, OutputStream* output) {
    auto* buffer = new Array<int8_t>(4096);
    int64_t count = 0;
    int32_t n;
    while ((n = input->read(buffer, 0, buffer->length)) != -1) {
        output->write(buffer, 0, n);
        count += n;
    }
    return count;
}

int32_t IOUtils::copy(Reader* input, Writer* output) {
    auto* buffer = new Array<char16_t>(4096);
    int64_t count = 0;
    int32_t n;
    while ((n = input->read(buffer, 0, buffer->length)) != -1) {
        output->write(buffer, 0, n);
        count += n;
    }
    return count > INT32_MAX ? -1 : static_cast<int32_t>(count);
}

void IOUtils::write(const String& data, OutputStream* output) {
    if (!data.isNull()) output->write(Charset::defaultCharset()->encodeToArray(data));
}

void IOUtils::write(const String& data, Writer* output) {
    if (!data.isNull()) output->write(data);
}

void IOUtils::write(Array<int8_t>* data, OutputStream* output) {
    if (data != nullptr) output->write(data);
}

bool IOUtils::contentEquals(InputStream* input1, InputStream* input2) {
    InputStream* a = dynamic_cast<BufferedInputStream*>(input1) ? input1 : new BufferedInputStream(input1);
    InputStream* b = dynamic_cast<BufferedInputStream*>(input2) ? input2 : new BufferedInputStream(input2);
    int32_t ch = a->read();
    while (ch != -1) {
        int32_t ch2 = b->read();
        if (ch != ch2) return false;
        ch = a->read();
    }
    return b->read() == -1;
}

}  // namespace jlang
