// jlang/src/xml_io.cpp - byte/char IO used by the XML runtime: files through POSIX, jlang
// streams/readers/writers through <jlang/IO.h>.
#include "xml_internal.h"

#include <cerrno>
#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#if __has_include(<jlang/IO.h>)
#include <jlang/IO.h>
#define JLANG_XML_HAVE_IO 1
#else
#define JLANG_XML_HAVE_IO 0
#endif

namespace jlang::xml::detail {

namespace {

// java.io.FileInputStream / FileOutputStream: "<path> (<strerror>)".
[[noreturn]] void throwOpenError(const std::string& path, int err) {
    std::string m = path + " (" + std::strerror(err) + ")";
    throw ::jlang::FileNotFoundException(String(m));
}

[[noreturn]] void throwIo(const std::string& what, int err) {
    std::string m = what + ": " + std::strerror(err);
    throw ::jlang::IOException(String(m));
}

struct Grow {
    char* data = nullptr;
    size_t size = 0, cap = 0;
    void reserve(size_t n) {
        if (n <= cap) return;
        size_t c = cap == 0 ? 65536 : cap;
        while (c < n) c *= 2;
        auto* p = static_cast<char*>(std::realloc(data, c + 1));
        if (p == nullptr) {
            std::free(data);
            throw std::bad_alloc();
        }
        data = p;
        cap = c;
    }
    void append(const char* p, size_t n) {
        reserve(size + n);
        std::memcpy(data + size, p, n);
        size += n;
    }
    Bytes finish() {
        reserve(size + 1);
        data[size] = 0;
        return Bytes{data, size};
    }
};

}  // namespace

Bytes readFile(const std::string& path) {
    int fd = ::open(path.c_str(), O_RDONLY | O_CLOEXEC);
    if (fd < 0) throwOpenError(path, errno);
    struct stat st;
    if (::fstat(fd, &st) != 0) {
        int e = errno;
        ::close(fd);
        throwIo(path, e);
    }
    if (S_ISDIR(st.st_mode)) {
        ::close(fd);
        throwOpenError(path, EISDIR);
    }
    Grow g;
    try {
        g.reserve(static_cast<size_t>(st.st_size > 0 ? st.st_size : 0));
        char buf[65536];
        for (;;) {
            ssize_t n = ::read(fd, buf, sizeof buf);
            if (n < 0) {
                if (errno == EINTR) continue;
                int e = errno;
                std::free(g.data);
                ::close(fd);
                throwIo(path, e);
            }
            if (n == 0) break;
            g.append(buf, static_cast<size_t>(n));
        }
    } catch (...) {
        ::close(fd);
        throw;
    }
    ::close(fd);
    return g.finish();
}

void writeFile(const std::string& path, std::string_view data) {
    int fd = ::open(path.c_str(), O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0666);
    if (fd < 0) throwOpenError(path, errno);
    size_t off = 0;
    while (off < data.size()) {
        ssize_t n = ::write(fd, data.data() + off, data.size() - off);
        if (n < 0) {
            if (errno == EINTR) continue;
            int e = errno;
            ::close(fd);
            throwIo(path, e);
        }
        off += static_cast<size_t>(n);
    }
    if (::close(fd) != 0) throwIo(path, errno);
}

#if JLANG_XML_HAVE_IO

std::string filePath(File* f) { return std::string(f->getPath()); }

}  // namespace jlang::xml::detail

namespace jlang::xml {
File* parseFile(std::string_view lexical) { return new File(String(trimXml(lexical))); }
}  // namespace jlang::xml

namespace jlang::xml::detail {

Bytes readStream(InputStream* in) {
    Grow g;
    auto* buf = new Array<int8_t>(65536);
    try {
        for (;;) {
            int32_t n = in->read(buf, 0, buf->length);
            if (n < 0) break;
            g.append(reinterpret_cast<const char*>(buf->data()), static_cast<size_t>(n));
        }
    } catch (...) {
        std::free(g.data);
        throw;
    }
    return g.finish();
}

Bytes readReader(Reader* r) {
    Grow g;
    auto* buf = new Array<char16_t>(32768);
    std::u16string pending;  // a high surrogate split across reads
    try {
        for (;;) {
            int32_t n = r->read(buf, 0, buf->length);
            if (n < 0) break;
            pending.append(buf->data(), static_cast<size_t>(n));
            size_t keep = 0;
            if (!pending.empty() && pending.back() >= 0xD800 && pending.back() <= 0xDBFF) keep = 1;
            String s = String::fromUtf16(pending.data(), pending.size() - keep);
            g.append(s.data(), s.size());
            pending.erase(0, pending.size() - keep);
        }
        if (!pending.empty()) {
            String s = String::fromUtf16(pending.data(), pending.size());
            g.append(s.data(), s.size());
        }
    } catch (...) {
        std::free(g.data);
        throw;
    }
    return g.finish();
}

void writeStream(OutputStream* os, std::string_view data) {
    auto* a = new Array<int8_t>(static_cast<int32_t>(data.size()));
    std::memcpy(a->data(), data.data(), data.size());
    os->write(a, 0, a->length);
    os->flush();
}

void writeWriter(::jlang::Writer* w, std::string_view data) {
    if (!data.empty()) w->write(String(data));
}

void flushWriter(::jlang::Writer* w) { w->flush(); }

#else  // <jlang/IO.h> not available yet

[[noreturn]] static void noIo() {
    throw ::jlang::UnsupportedOperationException("jlang::xml: <jlang/IO.h> is not available");
}
}  // namespace jlang::xml::detail
namespace jlang::xml {
File* parseFile(std::string_view) { detail::noIo(); }
}  // namespace jlang::xml
namespace jlang::xml::detail {
std::string filePath(File*) { noIo(); }
Bytes readStream(InputStream*) { noIo(); }
Bytes readReader(Reader*) { noIo(); }
void writeStream(OutputStream*, std::string_view) { noIo(); }
void writeWriter(::jlang::Writer*, std::string_view) { noIo(); }
void flushWriter(::jlang::Writer*) { noIo(); }

#endif

}  // namespace jlang::xml::detail
