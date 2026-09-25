// jlang/IO.h - java.io (files, byte streams, readers/writers), java.util.Properties,
// java.util.ResourceBundle/PropertyResourceBundle, java.util.zip (ZipOutputStream, ZipEntry,
// CRC32) and Apache commons-io (FileUtils, IOUtils, LineIterator, FileFilterUtils,
// HiddenFileFilter).
//
// Mapping notes (see tools/cppgen/jdkmap.tsv):
//   * file.delete() -> file->delete_()        (C++ keyword, CONVENTIONS §3.4)
//   * FileFilterUtils.and(a, b)/or(a, b) -> FileFilterUtils::and_(a, b)/or_(a, b)
//   * HiddenFileFilter.VISIBLE / HIDDEN -> FileFilterUtils::VISIBLE() / HIDDEN()
//   * IOFileFilter -> jlang::FileFilter (accept(File*) and accept(File* dir, String name))
//   * java.io.FilterInputStream / DataInput / DataOutput -> InputStream / DataInputStream /
//     DataOutputStream; PropertyResourceBundle -> ResourceBundle
//   * java.io.PrintStream is the core class (<jlang/System.h>: System.out/err and
//     new PrintStream(fileName)); new PrintStream(OutputStream) -> new jlang::OutputStreamPrintStream(out)
//   * Collection<File> results (FileUtils.listFiles) -> jlang::List<File*>*
//
// Semantics follow the JDK: exceptions and messages ("<path> (No such file or directory)"),
// EOF conventions (-1), BufferedReader.readLine line terminators (\n, \r, \r\n), Java's
// modified UTF-8 in DataInput/DataOutput, byte order (big endian), Properties.load/store
// syntax (exactly java.util.Properties: ISO-8859-1 for streams, escapes, continuation lines,
// comments), directory listing order (the OS order, like File.list()).
// The default charset is UTF-8. Strings passed to Writers are UTF-8 jlang::Strings; their
// UTF-16 units are what Java would see. write(String, off, len) takes jlang::String indices.
// Streams are not closed by the collector: close them explicitly, as the Java code does.
#pragma once

#include <jlang/Collections.h>
#include <jlang/IOBase.h>
#include <jlang/System.h>
#include <jlang/Util.h>

#include <cstdint>
#include <string>
#include <utility>

namespace jlang {

class File;
class InputStream;
class OutputStream;
class Reader;
class Writer;
class PrintWriter;

// =======================================================================================
// java.io.FileFilter (also commons-io IOFileFilter) and java.io.FilenameFilter
class FileFilter : public virtual Object {
public:
    virtual bool accept(File* pathname) = 0;
    // IOFileFilter.accept(File dir, String name): default accept(new File(dir, name)).
    virtual bool accept(File* dir, const String& name);
    // Lambda adapter: FileFilter::of([](jlang::File* f) { return ...; })
    template<class F> static FileFilter* of(F f);
};

class FilenameFilter : public virtual Object {
public:
    virtual bool accept(File* dir, const String& name) = 0;
    template<class F> static FilenameFilter* of(F f);
};

namespace detail {
template<class F>
class FileFilterLambda final : public virtual FileFilter {
public:
    explicit FileFilterLambda(F f) : f_(std::move(f)) {}
    using FileFilter::accept;
    bool accept(File* pathname) override { return f_(pathname); }

private:
    F f_;
};
template<class F>
class FilenameFilterLambda final : public virtual FilenameFilter {
public:
    explicit FilenameFilterLambda(F f) : f_(std::move(f)) {}
    bool accept(File* dir, const String& name) override { return f_(dir, name); }

private:
    F f_;
};
}  // namespace detail

template<class F>
FileFilter* FileFilter::of(F f) {
    return new detail::FileFilterLambda<F>(std::move(f));
}
template<class F>
FilenameFilter* FilenameFilter::of(F f) {
    return new detail::FilenameFilterLambda<F>(std::move(f));
}

// =======================================================================================
// java.io.File (UNIX file system semantics of the JDK)
class File : public virtual Object, public virtual Comparable<File*> {
public:
    static inline const String separator = String("/");
    static constexpr char16_t separatorChar = u'/';
    static inline const String pathSeparator = String(":");
    static constexpr char16_t pathSeparatorChar = u':';

    // NullPointerException for a null pathname/child, like Java. Paths are normalized
    // (duplicate and trailing separators removed).
    explicit File(const String& pathname);
    File(const String& parent, const String& child);
    File(File* parent, const String& child);

    String getName();
    String getParent();       // null if none
    File* getParentFile();    // null if none
    String getPath() { return path_; }
    bool isAbsolute() { return prefixLength_ != 0; }
    String getAbsolutePath();  // resolved against System.getProperty("user.dir")
    File* getAbsoluteFile();
    String getCanonicalPath();  // IOException on failure
    File* getCanonicalFile();
    // jlang helper for Java's file.toURI().getPath(): absolute path, '/' appended for
    // directories (percent-decoding is the identity here).
    String toURIPath();

    bool canRead();
    bool canWrite();
    bool canExecute();
    bool exists();
    bool isDirectory();
    bool isFile();
    bool isHidden();  // name starts with '.'
    int64_t lastModified();  // ms since the epoch, 0 if it does not exist
    int64_t length();        // 0 if it does not exist
    int64_t getTotalSpace();
    int64_t getFreeSpace();
    int64_t getUsableSpace();

    bool createNewFile();  // IOException for errors other than "exists"
    bool delete_();        // Java delete()
    void deleteOnExit();
    Array<String>* list();  // null if not a directory / I/O error; OS order
    Array<String>* list(FilenameFilter* filter);
    Array<File*>* listFiles();
    Array<File*>* listFiles(FilenameFilter* filter);
    Array<File*>* listFiles(FileFilter* filter);
    bool mkdir();
    bool mkdirs();
    bool renameTo(File* dest);
    bool setLastModified(int64_t time);  // IllegalArgumentException if negative
    bool setReadOnly();
    bool setWritable(bool writable, bool ownerOnly = true);
    bool setReadable(bool readable, bool ownerOnly = true);
    bool setExecutable(bool executable, bool ownerOnly = true);

    static File* createTempFile(const String& prefix, const String& suffix);
    static File* createTempFile(const String& prefix, const String& suffix, File* directory);
    static Array<File*>* listRoots();

    int32_t compareTo(File* pathname) override;
    bool equals(Object* obj) override;
    int32_t hashCode() override;
    String toString() override { return path_; }

private:
    struct Raw {};
    File(Raw, const String& normalizedPath);
    String path_;
    int32_t prefixLength_ = 0;
};

// =======================================================================================
// Byte streams

// java.io.InputStream
class InputStream : public virtual Object, public virtual Closeable {
public:
    virtual int32_t read() = 0;
    virtual int32_t read(Array<int8_t>* b);
    virtual int32_t read(Array<int8_t>* b, int32_t off, int32_t len);
    virtual int64_t skip(int64_t n);
    virtual int32_t available() { return 0; }
    void close() override {}
    virtual void mark(int32_t readlimit) { (void)readlimit; }
    virtual void reset();  // IOException("mark/reset not supported")
    virtual bool markSupported() { return false; }
    // Java 9+
    virtual Array<int8_t>* readAllBytes();
    virtual Array<int8_t>* readNBytes(int32_t len);
    virtual int32_t readNBytes(Array<int8_t>* b, int32_t off, int32_t len);
    virtual void skipNBytes(int64_t n);
    virtual int64_t transferTo(OutputStream* out);
    static InputStream* nullInputStream();
};

// java.io.OutputStream
class OutputStream : public virtual Object, public virtual Closeable, public virtual Flushable {
public:
    virtual void write(int32_t b) = 0;
    virtual void write(Array<int8_t>* b);
    virtual void write(Array<int8_t>* b, int32_t off, int32_t len);
    void flush() override {}
    void close() override {}
    static OutputStream* nullOutputStream();
};

// java.io.FilterInputStream (the Java type maps to InputStream in declarations)
class FilterInputStream : public InputStream {
public:
    using InputStream::read;
    int32_t read() override;
    int32_t read(Array<int8_t>* b) override;
    int32_t read(Array<int8_t>* b, int32_t off, int32_t len) override;
    int64_t skip(int64_t n) override;
    int32_t available() override;
    void close() override;
    void mark(int32_t readlimit) override;
    void reset() override;
    bool markSupported() override;

protected:
    explicit FilterInputStream(InputStream* in) : in(in) {}
    InputStream* in;
};

// java.io.FilterOutputStream
class FilterOutputStream : public OutputStream {
public:
    explicit FilterOutputStream(OutputStream* out) : out(out) {}
    using OutputStream::write;
    void write(int32_t b) override;
    void write(Array<int8_t>* b) override;
    void write(Array<int8_t>* b, int32_t off, int32_t len) override;
    void flush() override;
    void close() override;  // flush, then close out (flush exception rethrown after closing)

protected:
    OutputStream* out;
    bool closed_ = false;
};

// java.io.FileInputStream
class FileInputStream : public InputStream {
public:
    explicit FileInputStream(File* file);        // FileNotFoundException
    explicit FileInputStream(const String& name);
    using InputStream::read;
    int32_t read() override;
    int32_t read(Array<int8_t>* b, int32_t off, int32_t len) override;
    int64_t skip(int64_t n) override;
    int32_t available() override;
    void close() override;
    // jlang: the underlying POSIX descriptor (-1 once closed).
    int fd() const noexcept { return fd_; }

private:
    void open(const String& path);
    int fd_ = -1;
    String path_;
};

// java.io.FileOutputStream
class FileOutputStream : public OutputStream {
public:
    explicit FileOutputStream(File* file, bool append = false);  // FileNotFoundException
    explicit FileOutputStream(const String& name, bool append = false);
    FileOutputStream(const char* name, bool append = false) : FileOutputStream(String(name), append) {}
    using OutputStream::write;
    void write(int32_t b) override;
    void write(Array<int8_t>* b, int32_t off, int32_t len) override;
    void close() override;
    int fd() const noexcept { return fd_; }
    // jlang: raw write of n bytes (IOException on failure).
    void writeRaw(const void* p, size_t n);

private:
    void open(const String& path, bool append);
    int fd_ = -1;
    String path_;
};

// java.io.BufferedInputStream (the JDK algorithm, mark/reset included)
class BufferedInputStream : public FilterInputStream {
public:
    explicit BufferedInputStream(InputStream* in, int32_t size = 8192);  // IllegalArgumentException if size <= 0
    using FilterInputStream::read;
    int32_t read() override;
    int32_t read(Array<int8_t>* b, int32_t off, int32_t len) override;
    int64_t skip(int64_t n) override;
    int32_t available() override;
    void mark(int32_t readlimit) override;
    void reset() override;
    bool markSupported() override { return true; }
    void close() override;

protected:
    Array<int8_t>* buf;
    int32_t count = 0;
    int32_t pos = 0;
    int32_t markpos = -1;
    int32_t marklimit = 0;

private:
    void fill();
    int32_t read1(Array<int8_t>* b, int32_t off, int32_t len);
    Array<int8_t>* getBufIfOpen();
    InputStream* getInIfOpen();
};

// java.io.BufferedOutputStream
class BufferedOutputStream : public FilterOutputStream {
public:
    explicit BufferedOutputStream(OutputStream* out, int32_t size = 8192);
    using FilterOutputStream::write;
    void write(int32_t b) override;
    void write(Array<int8_t>* b, int32_t off, int32_t len) override;
    void flush() override;

protected:
    Array<int8_t>* buf;
    int32_t count = 0;

private:
    void flushBuffer();
};

// java.io.ByteArrayInputStream
class ByteArrayInputStream : public InputStream {
public:
    explicit ByteArrayInputStream(Array<int8_t>* buf);
    ByteArrayInputStream(Array<int8_t>* buf, int32_t offset, int32_t length);
    using InputStream::read;
    int32_t read() override;
    int32_t read(Array<int8_t>* b, int32_t off, int32_t len) override;
    int64_t skip(int64_t n) override;
    int32_t available() override { return count - pos; }
    bool markSupported() override { return true; }
    void mark(int32_t readAheadLimit) override;
    void reset() override { pos = mark_; }
    void close() override {}
    Array<int8_t>* readAllBytes() override;
    int64_t transferTo(OutputStream* out) override;

protected:
    Array<int8_t>* buf;
    int32_t pos = 0;
    int32_t mark_ = 0;
    int32_t count = 0;
};

// java.io.ByteArrayOutputStream
class ByteArrayOutputStream : public OutputStream {
public:
    ByteArrayOutputStream() : ByteArrayOutputStream(32) {}
    explicit ByteArrayOutputStream(int32_t size);  // IllegalArgumentException("Negative initial size: n")
    using OutputStream::write;
    void write(int32_t b) override;
    void write(Array<int8_t>* b, int32_t off, int32_t len) override;
    void writeBytes(Array<int8_t>* b);
    void writeTo(OutputStream* out);
    void reset() { count = 0; }
    Array<int8_t>* toByteArray();
    int32_t size() { return count; }
    String toString() override;  // decoded with the default charset (UTF-8)
    String toString(const String& charsetName);
    String toString(Charset* charset);
    void close() override {}

protected:
    Array<int8_t>* buf;
    int32_t count = 0;

private:
    void ensureCapacity(int32_t minCapacity);
};

// java.io.DataInputStream (also java.io.DataInput): big-endian, modified UTF-8.
class DataInputStream : public FilterInputStream {
public:
    explicit DataInputStream(InputStream* in) : FilterInputStream(in) {}
    using FilterInputStream::read;
    int32_t read(Array<int8_t>* b) override;
    int32_t read(Array<int8_t>* b, int32_t off, int32_t len) override;
    void readFully(Array<int8_t>* b);  // EOFException
    void readFully(Array<int8_t>* b, int32_t off, int32_t len);
    int32_t skipBytes(int32_t n);
    bool readBoolean();
    int8_t readByte();
    int32_t readUnsignedByte();
    int16_t readShort();
    int32_t readUnsignedShort();
    char16_t readChar();
    int32_t readInt();
    int64_t readLong();
    float readFloat();
    double readDouble();
    String readLine();  // deprecated in Java: bytes as ISO-8859-1 chars
    String readUTF();   // UTFDataFormatException for malformed input
    static String readUTF(DataInputStream* in);
};

// java.io.DataOutputStream (also java.io.DataOutput)
class DataOutputStream : public FilterOutputStream {
public:
    explicit DataOutputStream(OutputStream* out) : FilterOutputStream(out) {}
    using FilterOutputStream::write;
    void write(int32_t b) override;
    void write(Array<int8_t>* b, int32_t off, int32_t len) override;
    void flush() override;
    void writeBoolean(bool v);
    void writeByte(int32_t v);
    void writeShort(int32_t v);
    void writeChar(int32_t v);
    void writeInt(int32_t v);
    void writeLong(int64_t v);
    void writeFloat(float v);
    void writeDouble(double v);
    void writeBytes(const String& s);  // low byte of each UTF-16 unit
    void writeChars(const String& s);
    void writeUTF(const String& s);    // UTFDataFormatException("encoded string too long: n bytes")
    int32_t size() { return written; }

protected:
    int32_t written = 0;

private:
    void incCount(int32_t v);
};

// java.io.PrintStream over an OutputStream (Java new PrintStream(OutputStream[, autoFlush[,
// encoding]]) / new PrintStream(File)). The PrintStream class itself is the core's.
class OutputStreamPrintStream : public PrintStream, public virtual Closeable, public virtual Flushable {
public:
    explicit OutputStreamPrintStream(OutputStream* out, bool autoFlush = false);
    OutputStreamPrintStream(OutputStream* out, bool autoFlush, const String& encoding);  // UnsupportedEncodingException
    explicit OutputStreamPrintStream(File* file);  // FileNotFoundException
    void write(int32_t b) override;
    void write(Array<int8_t>* buf, int32_t off, int32_t len);
    void flush() override;
    void close() override;

protected:
    void writeBytes(const char* data, size_t n) override;

private:
    OutputStream* out_;
    Charset* charset_;
    bool autoFlush_;
    bool closing_ = false;
};

// =======================================================================================
// Character streams

// java.io.Reader
class Reader : public virtual Object, public virtual Closeable {
public:
    virtual int32_t read();  // a UTF-16 code unit, or -1
    virtual int32_t read(Array<char16_t>* cbuf);
    virtual int32_t read(Array<char16_t>* cbuf, int32_t off, int32_t len) = 0;
    virtual int32_t read(CharBuffer* target);
    virtual int64_t skip(int64_t n);
    virtual bool ready() { return false; }
    virtual bool markSupported() { return false; }
    virtual void mark(int32_t readAheadLimit);  // IOException("mark() not supported")
    virtual void reset();                       // IOException("reset() not supported")
    void close() override = 0;
    virtual int64_t transferTo(Writer* out);
    static Reader* nullReader();

protected:
    Reader() : lock(this) {}
    explicit Reader(Object* lock) : lock(lock) {}
    Object* lock;
};

// java.io.InputStreamReader (charset decoding with JDK replacement rules)
class InputStreamReader : public Reader {
public:
    explicit InputStreamReader(InputStream* in);  // default charset (UTF-8)
    InputStreamReader(InputStream* in, const String& charsetName);  // UnsupportedEncodingException
    InputStreamReader(InputStream* in, const char* charsetName) : InputStreamReader(in, String(charsetName)) {}
    InputStreamReader(InputStream* in, Charset* cs);
    String getEncoding();  // historical name ("UTF8"), null once closed
    using Reader::read;
    int32_t read() override;
    int32_t read(Array<char16_t>* cbuf, int32_t off, int32_t len) override;
    bool ready() override;
    void close() override;

private:
    bool fillChars();  // false at end of input
    InputStream* in_;
    Charset* cs_;
    detail::CharDecoderState dec_;
    std::u16string pending_;
    size_t pendingPos_ = 0;
    Array<int8_t>* bytes_;
    bool eof_ = false;
    bool closed_ = false;
};

// java.io.FileReader (default charset UTF-8)
class FileReader : public InputStreamReader {
public:
    explicit FileReader(File* file);
    explicit FileReader(const String& fileName);
    FileReader(File* file, Charset* cs);
    FileReader(const String& fileName, Charset* cs);
};

// java.io.BufferedReader
class BufferedReader : public Reader {
public:
    explicit BufferedReader(Reader* in, int32_t sz = 8192);  // IllegalArgumentException if sz <= 0
    using Reader::read;
    int32_t read() override;
    int32_t read(Array<char16_t>* cbuf, int32_t off, int32_t len) override;
    // The next line without its terminator (\n, \r or \r\n), or null at end of stream.
    String readLine();
    int64_t skip(int64_t n) override;
    bool ready() override;
    bool markSupported() override { return true; }
    void mark(int32_t readAheadLimit) override;
    void reset() override;
    void close() override;
    // Java 8 lines(): all remaining lines as a list (no streams in jlang).
    List<String>* lines();

private:
    void ensureOpen();
    void fill();
    int32_t read1(Array<char16_t>* cbuf, int32_t off, int32_t len);
    Reader* in_;
    Array<char16_t>* cb_;
    int32_t nChars_ = 0;
    int32_t nextChar_ = 0;
    int32_t markedChar_ = -1;  // UNMARKED; -2 = INVALIDATED
    int32_t readAheadLimit_ = 0;
    bool skipLF_ = false;
    bool markedSkipLF_ = false;
};

// java.io.StringReader
class StringReader : public Reader {
public:
    explicit StringReader(const String& s);
    using Reader::read;
    int32_t read() override;
    int32_t read(Array<char16_t>* cbuf, int32_t off, int32_t len) override;
    int64_t skip(int64_t n) override;
    bool ready() override;
    bool markSupported() override { return true; }
    void mark(int32_t readAheadLimit) override;
    void reset() override;
    void close() override { closed_ = true; }

private:
    std::u16string s_;
    size_t next_ = 0;
    size_t mark_ = 0;
    bool closed_ = false;
};

// java.io.Writer
class Writer : public virtual Object, public virtual Closeable, public virtual Flushable {
public:
    virtual void write(int32_t c);  // the low 16 bits (a UTF-16 code unit)
    virtual void write(Array<char16_t>* cbuf);
    virtual void write(Array<char16_t>* cbuf, int32_t off, int32_t len) = 0;
    virtual void write(const String& str);
    virtual void write(const String& str, int32_t off, int32_t len);  // jlang::String indices
    void write(const char* str) { write(String(str)); }
    virtual Writer* append(const String& csq);
    virtual Writer* append(const String& csq, int32_t start, int32_t end);
    virtual Writer* append(char16_t c);
    void flush() override = 0;
    void close() override = 0;
    static Writer* nullWriter();

protected:
    Writer() : lock(this) {}
    explicit Writer(Object* lock) : lock(lock) {}
    // Writes UTF-16 code units (subclasses override for speed; default goes through
    // write(Array<char16_t>*, int, int)).
    virtual void writeUnits(const char16_t* p, int32_t n);
    Object* lock;
};

// java.io.OutputStreamWriter
class OutputStreamWriter : public Writer {
public:
    explicit OutputStreamWriter(OutputStream* out);
    OutputStreamWriter(OutputStream* out, const String& charsetName);  // UnsupportedEncodingException
    OutputStreamWriter(OutputStream* out, const char* charsetName) : OutputStreamWriter(out, String(charsetName)) {}
    OutputStreamWriter(OutputStream* out, Charset* cs);
    String getEncoding();
    using Writer::write;
    void write(int32_t c) override;
    void write(Array<char16_t>* cbuf, int32_t off, int32_t len) override;
    void write(const String& str) override;
    void flush() override;
    void close() override;

protected:
    void writeUnits(const char16_t* p, int32_t n) override;

private:
    void ensureOpen();
    void flushBytes();
    OutputStream* out_;
    Charset* cs_;
    detail::CharEncoderState enc_;
    std::string bytes_;
    bool closed_ = false;
};

// java.io.FileWriter (default charset UTF-8)
class FileWriter : public OutputStreamWriter {
public:
    explicit FileWriter(File* file, bool append = false);
    explicit FileWriter(const String& fileName, bool append = false);
    FileWriter(const char* fileName, bool append = false) : FileWriter(String(fileName), append) {}
    FileWriter(File* file, Charset* cs, bool append = false);
};

// java.io.BufferedWriter
class BufferedWriter : public Writer {
public:
    explicit BufferedWriter(Writer* out, int32_t sz = 8192);  // IllegalArgumentException if sz <= 0
    using Writer::write;
    void write(int32_t c) override;
    void write(Array<char16_t>* cbuf, int32_t off, int32_t len) override;
    void write(const String& s) override;
    void newLine();  // System.lineSeparator() ("\n")
    void flush() override;
    void close() override;

protected:
    void writeUnits(const char16_t* p, int32_t n) override;

private:
    void ensureOpen();
    void flushBuffer();
    Writer* out_;
    Array<char16_t>* cb_;
    int32_t nChars_;
    int32_t nextChar_ = 0;
};

// java.io.StringWriter
class StringWriter : public Writer {
public:
    StringWriter() {}
    explicit StringWriter(int32_t initialSize);
    using Writer::write;
    void write(int32_t c) override;
    void write(Array<char16_t>* cbuf, int32_t off, int32_t len) override;
    void write(const String& str) override;
    StringBuilder* getBuffer() { return buf_; }
    String toString() override { return buf_->toString(); }
    void flush() override {}
    void close() override {}

protected:
    void writeUnits(const char16_t* p, int32_t n) override;

private:
    StringBuilder* buf_ = new StringBuilder();
};

// java.io.PrintWriter (never throws IOException: checkError() reports failures)
class PrintWriter : public Writer {
public:
    explicit PrintWriter(Writer* out, bool autoFlush = false);
    explicit PrintWriter(OutputStream* out, bool autoFlush = false);
    explicit PrintWriter(const String& fileName);  // FileNotFoundException
    PrintWriter(const String& fileName, const String& csn);
    explicit PrintWriter(File* file);
    PrintWriter(File* file, const String& csn);

    // print/println: Java string conversion of any value jlang::String concatenation accepts
    // (numbers like Java, char16_t as a character, Object* via toString(), "null"...).
    template<class T>
        requires detail::Concatenable<T>
    void print(const T& v) {
        std::string s;
        detail::appendValue(s, v);
        printRaw(s, false);
    }
    void print(Array<char16_t>* s);
    void println() { printRaw(std::string(), true); }
    template<class T>
        requires detail::Concatenable<T>
    void println(const T& v) {
        std::string s;
        detail::appendValue(s, v);
        printRaw(s, true);
    }
    void println(Array<char16_t>* s);
    template<class... A>
    PrintWriter* printf(const String& fmt, const A&... args) {
        formatRaw(String::format(fmt, args...));
        return this;
    }
    template<class... A>
    PrintWriter* format(const String& fmt, const A&... args) {
        formatRaw(String::format(fmt, args...));
        return this;
    }
    PrintWriter* append(const String& csq) override;
    PrintWriter* append(const String& csq, int32_t start, int32_t end) override;
    PrintWriter* append(char16_t c) override;

    using Writer::write;
    void write(int32_t c) override;
    void write(Array<char16_t>* buf, int32_t off, int32_t len) override;
    void write(const String& s) override;
    void write(const String& s, int32_t off, int32_t len) override;
    void flush() override;
    void close() override;
    bool checkError();

protected:
    void setError() { trouble_ = true; }
    void clearError() { trouble_ = false; }
    void writeUnits(const char16_t* p, int32_t n) override;

private:
    void printRaw(const std::string& utf8, bool newline);
    void formatRaw(const String& s);
    Writer* out_;
    bool autoFlush_;
    bool trouble_ = false;
};

// =======================================================================================
// java.util.Properties (Hashtable<Object,Object> of Strings; insertion ordered here)
class Properties : public virtual Object {
public:
    Properties() : Properties(nullptr) {}
    explicit Properties(Properties* defaults);

    // Searches this table, then the defaults (recursively). null when absent.
    String getProperty(const String& key);
    String getProperty(const String& key, const String& defaultValue);
    // Returns the previous value (null if none).
    String setProperty(const String& key, const String& value);
    // Hashtable methods (this table only, no defaults)
    String get(const String& key);
    String put(const String& key, const String& value);
    String remove(const String& key);
    bool containsKey(const String& key);
    bool containsValue(const String& value);
    bool contains(const String& value) { return containsValue(value); }
    int32_t size();
    bool isEmpty() { return size() == 0; }
    void clear();
    void putAll(Properties* t);
    void putAll(Map<String, String>* t);
    Set<String>* keySet();             // snapshot of this table's keys
    List<String>* values();            // snapshot
    List<Entry<String, String>>* entrySet();  // snapshot
    Iterator<String>* keys();
    // Keys of this table and the defaults.
    Set<String>* stringPropertyNames();
    Iterator<String>* propertyNames();

    // java.util.Properties.load: InputStream bytes are ISO-8859-1; '\' escapes (\t \n \r \f
    // \uXXXX, others literal), line continuations, '#'/'!' comments, '=' ':' or whitespace
    // separators, leading whitespace ignored. IllegalArgumentException("Malformed \uxxxx
    // encoding.") like Java.
    void load(InputStream* inStream);
    void load(Reader* reader);
    // Writes "#comments", "#<date>", then key=value lines (escaped like Java; the stream form
    // uses ISO-8859-1 and \uXXXX for other characters). Order: insertion order.
    void store(Writer* writer, const String& comments);
    void store(OutputStream* out, const String& comments);
    void list(PrintStream* out);
    void list(PrintWriter* out);

    bool equals(Object* o) override;
    int32_t hashCode() override;
    String toString() override;  // {k1=v1, k2=v2}

protected:
    Properties* defaults;

private:
    template<class Src>
    void load0(Src& src);
    void store0(Writer* bw, const String& comments, bool escUnicode);
    void enumerate(Set<String>* out);
    Map<String, String>* map_;
};

// java.util.ResourceBundle / PropertyResourceBundle (property files only)
class ResourceBundle : public virtual Object {
public:
    // PropertyResourceBundle(InputStream) (ISO-8859-1 per Java 6 / UTF-8 per Java 9+: UTF-8
    // with ISO-8859-1 fallback on malformed input) and PropertyResourceBundle(Reader).
    explicit ResourceBundle(InputStream* stream);
    explicit ResourceBundle(Reader* reader);
    // Loads "<baseName with '.' -> '/'>.properties" (then "<baseName>.properties" as given)
    // relative to the working directory. MissingResourceException when not found.
    static ResourceBundle* getBundle(const String& baseName);

    // MissingResourceException("Can't find resource for bundle ..., key k") when absent
    // (after the parent chain).
    String getString(const String& key);
    Object* getObject(const String& key);
    Array<String>* getStringArray(const String& key);
    bool containsKey(const String& key);
    Iterator<String>* getKeys();
    Set<String>* keySet();
    Object* handleGetObject(const String& key);  // nullptr if absent (this bundle only)
    void setParent(ResourceBundle* parent) { parent_ = parent; }
    ResourceBundle* getParent() { return parent_; }
    String getBaseBundleName() { return baseName_; }

protected:
    ResourceBundle() : props_(new Properties()) {}
    Properties* props_;
    ResourceBundle* parent_ = nullptr;
    String baseName_;
};
using PropertyResourceBundle = ResourceBundle;

// =======================================================================================
// java.util.zip

// java.util.zip.CRC32 (zlib)
class CRC32 : public virtual Object {
public:
    void update(int32_t b);
    void update(Array<int8_t>* b);
    void update(Array<int8_t>* b, int32_t off, int32_t len);
    void update(ByteBuffer* buffer);
    int64_t getValue() { return static_cast<int64_t>(crc_); }
    void reset() { crc_ = 0; }
    // jlang helper
    void updateRaw(const void* p, size_t n);

private:
    uint32_t crc_ = 0;
};

// java.util.zip.ZipEntry
class ZipEntry : public virtual Object {
public:
    static constexpr int32_t STORED = 0;
    static constexpr int32_t DEFLATED = 8;

    explicit ZipEntry(const String& name);  // NullPointerException / IllegalArgumentException("entry name too long")
    explicit ZipEntry(ZipEntry* e);
    String getName() { return name_; }
    void setTime(int64_t time) { time_ = time; }
    int64_t getTime() { return time_; }
    void setSize(int64_t size);  // IllegalArgumentException("invalid entry size")
    int64_t getSize() { return size_; }
    int64_t getCompressedSize() { return csize_; }
    void setCompressedSize(int64_t csize) { csize_ = csize; }
    void setCrc(int64_t crc);    // IllegalArgumentException("invalid entry crc-32")
    int64_t getCrc() { return crc_; }
    void setMethod(int32_t method);  // IllegalArgumentException("invalid compression method")
    int32_t getMethod() { return method_; }
    void setExtra(Array<int8_t>* extra);
    Array<int8_t>* getExtra() { return extra_; }
    void setComment(const String& comment) { comment_ = comment; }
    String getComment() { return comment_; }
    bool isDirectory() { return name_.endsWith(String("/")); }
    String toString() override { return name_; }
    int32_t hashCode() override { return name_.hashCode(); }
    ZipEntry* clone() override { return new ZipEntry(this); }

private:
    friend class ZipOutputStream;
    String name_;
    int64_t time_ = -1;
    int64_t crc_ = -1;
    int64_t size_ = -1;
    int64_t csize_ = -1;
    int32_t method_ = -1;
    int32_t flag_ = 0;
    Array<int8_t>* extra_ = nullptr;
    String comment_;
};

// java.util.zip.ZipOutputStream (deflate via zlib; data descriptors like the JDK; UTF-8 names)
class ZipOutputStream : public FilterOutputStream {
public:
    static constexpr int32_t STORED = ZipEntry::STORED;
    static constexpr int32_t DEFLATED = ZipEntry::DEFLATED;

    explicit ZipOutputStream(OutputStream* out);
    ZipOutputStream(OutputStream* out, Charset* charset);
    void setComment(const String& comment);
    void setMethod(int32_t method);  // IllegalArgumentException("invalid compression method")
    void setLevel(int32_t level);    // -1..9, IllegalArgumentException("invalid compression level")
    // ZipException: duplicate entry, STORED without size/crc, unsupported method...
    void putNextEntry(ZipEntry* e);
    void closeEntry();
    using FilterOutputStream::write;
    void write(int32_t b) override;
    void write(Array<int8_t>* b, int32_t off, int32_t len) override;
    void finish();
    void close() override;

private:
    struct XEntry;
    void ensureOpen();
    void writeRaw(const void* p, size_t n);
    void writeShort(int32_t v);
    void writeInt(int64_t v);
    void writeLOC(ZipEntry* e);
    void writeEXT(ZipEntry* e);
    void deflateInput(const int8_t* p, size_t n, bool finish);
    Charset* charset_;
    std::vector<ZipEntry*> entries_;
    std::vector<int64_t> offsets_;
    std::vector<std::string> names_;
    ZipEntry* current_ = nullptr;
    int64_t currentOffset_ = 0;
    int64_t written_ = 0;
    int64_t locoff_ = 0;
    int32_t method_ = DEFLATED;
    int32_t level_ = -1;
    bool finished_ = false;
    bool zclosed_ = false;
    String comment_;
    CRC32* crc_ = new CRC32();
    void* zs_ = nullptr;  // z_stream (GC allocated)
    int64_t bytesRead_ = 0;
    int64_t bytesWritten_ = 0;
};

// =======================================================================================
// Apache commons-io

// org.apache.commons.io.LineIterator
class LineIterator : public virtual Iterator<String>, public virtual Closeable {
public:
    explicit LineIterator(Reader* reader);  // IllegalArgumentException("Reader must not be null")
    // IllegalStateException wrapping an IOException (the iterator is closed first).
    bool hasNext() override;
    String next() override { return nextLine(); }
    String nextLine();  // NoSuchElementException("No more lines")
    void close() override;
    void remove() override;  // UnsupportedOperationException("Remove unsupported on LineIterator")
    static void closeQuietly(LineIterator* iterator);

protected:
    virtual bool isValidLine(const String& line) {
        (void)line;
        return true;
    }

private:
    BufferedReader* bufferedReader_;
    String cachedLine_;
    bool finished_ = false;
};

// org.apache.commons.io.FileUtils
class FileUtils {
public:
    FileUtils() = delete;
    static constexpr int64_t ONE_KB = 1024;
    static constexpr int64_t ONE_MB = ONE_KB * ONE_KB;
    static constexpr int64_t ONE_GB = ONE_KB * ONE_MB;

    // FileNotFoundException("File 'x' does not exist") / IOException("File 'x' exists but is a
    // directory" | "File 'x' cannot be read").
    static FileInputStream* openInputStream(File* file);
    static FileOutputStream* openOutputStream(File* file, bool append = false);
    // Files (not directories) accepted by fileFilter; subdirectories recursed into when
    // dirFilter accepts them (null = no recursion). IllegalArgumentException("Parameter
    // 'directory' is not a directory"), NullPointerException("Parameter 'fileFilter' is null").
    static List<File*>* listFiles(File* directory, FileFilter* fileFilter, FileFilter* dirFilter);
    // extensions without the dot ("xml"); null = all files.
    static List<File*>* listFiles(File* directory, Array<String>* extensions, bool recursive);
    static String readFileToString(File* file);
    static String readFileToString(File* file, const String& encoding);
    static Array<int8_t>* readFileToByteArray(File* file);
    static List<String>* readLines(File* file);
    static List<String>* readLines(File* file, const String& encoding);
    static LineIterator* lineIterator(File* file);
    static LineIterator* lineIterator(File* file, const String& encoding);
    static void writeStringToFile(File* file, const String& data);
    static void writeStringToFile(File* file, const String& data, const String& encoding);
    static void writeStringToFile(File* file, const String& data, const String& encoding, bool append);
    static void writeByteArrayToFile(File* file, Array<int8_t>* data);
    static void writeLines(File* file, List<String>* lines);
    static int64_t checksumCRC32(File* file);
    static bool isFileOlder(File* file, int64_t timeMillis);  // IllegalArgumentException("No specified file")
    static bool isFileOlder(File* file, File* reference);
    static bool isFileNewer(File* file, int64_t timeMillis);
    static bool isFileNewer(File* file, File* reference);
    static bool deleteQuietly(File* file);
    static void deleteDirectory(File* directory);
    static void cleanDirectory(File* directory);
    static void forceDelete(File* file);
    static void forceMkdir(File* directory);
    static void copyFile(File* srcFile, File* destFile);
    static void moveFile(File* srcFile, File* destFile);
    static void touch(File* file);
    static int64_t sizeOf(File* file);
};

// org.apache.commons.io.IOUtils
class IOUtils {
public:
    IOUtils() = delete;
    // Closes, ignoring IOException (any stream, reader, writer, channel, PrintStream, ...).
    template<class T>
    static void closeQuietly(T* closeable) {
        if (closeable == nullptr) return;
        try {
            closeable->close();
        } catch (IOException&) {
        }
    }
    static void closeQuietly(std::nullptr_t) {}
    static Array<int8_t>* toByteArray(InputStream* input);
    static Array<int8_t>* toByteArray(Reader* input);
    static String toString(InputStream* input);
    static String toString(InputStream* input, const String& encoding);
    static String toString(Reader* input);
    static String toString(Array<int8_t>* input);
    static List<String>* readLines(InputStream* input);
    static List<String>* readLines(InputStream* input, const String& encoding);
    static List<String>* readLines(Reader* input);
    static LineIterator* lineIterator(InputStream* input, const String& encoding);
    static LineIterator* lineIterator(Reader* reader) { return new LineIterator(reader); }
    static int32_t copy(InputStream* input, OutputStream* output);  // -1 if > 2GB
    static int64_t copyLarge(InputStream* input, OutputStream* output);
    static int32_t copy(Reader* input, Writer* output);
    static void write(const String& data, OutputStream* output);
    static void write(const String& data, Writer* output);
    static void write(Array<int8_t>* data, OutputStream* output);
    static bool contentEquals(InputStream* input1, InputStream* input2);
};

// org.apache.commons.io.filefilter.FileFilterUtils (+ HiddenFileFilter.VISIBLE/HIDDEN)
class FileFilterUtils {
public:
    FileFilterUtils() = delete;
    static FileFilter* and_(FileFilter* a, FileFilter* b);
    template<class... F>
        requires(sizeof...(F) >= 1)
    static FileFilter* and_(FileFilter* a, FileFilter* b, F*... more) {
        return and_(and_(a, b), more...);
    }
    static FileFilter* and_(Array<FileFilter*>* filters);
    static FileFilter* or_(FileFilter* a, FileFilter* b);
    template<class... F>
        requires(sizeof...(F) >= 1)
    static FileFilter* or_(FileFilter* a, FileFilter* b, F*... more) {
        return or_(or_(a, b), more...);
    }
    static FileFilter* or_(Array<FileFilter*>* filters);
    static FileFilter* andFileFilter(FileFilter* a, FileFilter* b) { return and_(a, b); }
    static FileFilter* orFileFilter(FileFilter* a, FileFilter* b) { return or_(a, b); }
    static FileFilter* notFileFilter(FileFilter* filter);
    static FileFilter* prefixFileFilter(const String& prefix);
    static FileFilter* suffixFileFilter(const String& suffix);
    static FileFilter* suffixFileFilter(Array<String>* suffixes);
    static FileFilter* nameFileFilter(const String& name);
    static FileFilter* directoryFileFilter();
    static FileFilter* fileFileFilter();
    static FileFilter* trueFileFilter();
    static FileFilter* falseFileFilter();
    static FileFilter* ageFileFilter(int64_t cutoff, bool acceptOlder = true);
    static FileFilter* sizeFileFilter(int64_t threshold, bool acceptLarger = true);
    // makeSVNAware(f): f AND NOT (directory named ".svn"); null f = that filter alone.
    static FileFilter* makeSVNAware(FileFilter* filter);
    static FileFilter* makeCVSAware(FileFilter* filter);
    static FileFilter* makeDirectoryOnly(FileFilter* filter);
    static FileFilter* makeFileOnly(FileFilter* filter);
    // HiddenFileFilter.VISIBLE / HiddenFileFilter.HIDDEN
    static FileFilter* VISIBLE();
    static FileFilter* HIDDEN();
};

}  // namespace jlang
