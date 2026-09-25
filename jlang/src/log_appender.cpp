// log4j emulation, part 3: appenders (AppenderSkeleton, WriterAppender, ConsoleAppender,
// FileAppender, RollingFileAppender), the error handler and the log4j varia filters.
// See <jlang/Log.h>.
#include <jlang/Log.h>

#include "log_internal.h"

#include <cerrno>
#include <cstring>

#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

namespace jlang::log4j {

namespace {

constexpr int32_t WRITE_FAILURE = 1;
constexpr int32_t FLUSH_FAILURE = 2;
constexpr int32_t FILE_OPEN_FAILURE = 4;

bool fileExists(const std::string& p) {
    struct stat st{};
    return ::stat(p.c_str(), &st) == 0;
}

int64_t fileLength(const std::string& p) {
    struct stat st{};
    return ::stat(p.c_str(), &st) == 0 ? static_cast<int64_t>(st.st_size) : 0;
}

// java.io.File.getParent(): null when the path has no separator.
std::string parentOf(const std::string& p) {
    size_t s = p.find_last_of('/');
    if (s == std::string::npos) return std::string();
    if (s == 0) return "/";
    return p.substr(0, s);
}

// java.io.File.mkdirs()
bool mkdirs(const std::string& dir) {
    if (dir.empty()) return false;
    if (fileExists(dir)) return false;
    std::string parent = parentOf(dir);
    if (!parent.empty() && !fileExists(parent)) mkdirs(parent);
    return ::mkdir(dir.c_str(), 0777) == 0;
}

// new FileOutputStream(fileName, append) plus log4j's "create the parent directory" retry.
std::FILE* openLogFile(const String& fileName, bool append) {
    const char* mode = append ? "ae" : "we";
    std::FILE* f = std::fopen(fileName.c_str(), mode);
    if (f != nullptr) return f;
    int err = errno;
    if (err == ENOENT) {
        std::string parent = parentOf(fileName);
        if (!parent.empty() && !fileExists(parent) && mkdirs(parent)) {
            f = std::fopen(fileName.c_str(), mode);
            if (f != nullptr) return f;
            err = errno;
        }
    }
    throw FileNotFoundException(str(fileName, " (", std::strerror(err), ")"));
}

bool isUtf8(const String& enc) {
    if (enc == nullptr || enc.isEmpty()) return true;
    String u = enc.trim().toUpperCase();
    return u.equals("UTF-8") || u.equals("UTF8");
}

}  // namespace

// =======================================================================================
// OnlyOnceErrorHandler

void OnlyOnceErrorHandler::error(String message, Throwable* e, int32_t errorCode) {
    (void)errorCode;
    if (firstTime_.exchange(false)) LogLog::error(message, e);
}

void OnlyOnceErrorHandler::error(String message) {
    if (firstTime_.exchange(false)) LogLog::error(message);
}

// =======================================================================================
// AppenderSkeleton

AppenderSkeleton::AppenderSkeleton() : errorHandler(new OnlyOnceErrorHandler()) {}

AppenderSkeleton::AppenderSkeleton(bool isActive) : AppenderSkeleton() { (void)isActive; }

void AppenderSkeleton::addFilter(Filter* newFilter) {
    if (headFilter == nullptr) {
        headFilter = tailFilter = newFilter;
    } else {
        tailFilter->setNext(newFilter);
        tailFilter = newFilter;
    }
}

void AppenderSkeleton::clearFilters() { headFilter = tailFilter = nullptr; }

bool AppenderSkeleton::isAsSevereAsThreshold(Level* priority) {
    return threshold == nullptr || (priority != nullptr && priority->isGreaterOrEqual(threshold));
}

void AppenderSkeleton::setErrorHandler(ErrorHandler* eh) {
    JSYNC(this) {
        if (eh == nullptr) LogLog::warn(String("You have tried to set a null error-handler."));
        else errorHandler = eh;
    }
}

void AppenderSkeleton::doAppend(LoggingEvent* event) {
    JSYNC(this) {
        if (closed) {
            LogLog::error(str("Attempted to append to closed appender named [", name, "]."));
            return;
        }
        if (!isAsSevereAsThreshold(event->getLevel())) return;
        for (Filter* f = headFilter; f != nullptr;) {
            int32_t d = f->decide(event);
            if (d == Filter::DENY) return;
            if (d == Filter::ACCEPT) break;
            f = f->getNext();  // NEUTRAL (and any other value)
        }
        append(event);
    }
}

bool AppenderSkeleton::setOption(String name, String value) {
    if (optionIs(name, "threshold")) {
        setThreshold(toLevelOption(value));
        return true;
    }
    if (optionIs(name, "name")) {
        setName(value);
        return true;
    }
    return OptionHandler::setOption(name, value);
}

// =======================================================================================
// WriterAppender

WriterAppender::WriterAppender() {}

bool WriterAppender::setOption(String name, String value) {
    if (optionIs(name, "immediateFlush")) {
        setImmediateFlush(toBoolOption(value));
        return true;
    }
    if (optionIs(name, "encoding")) {
        setEncoding(value);
        return true;
    }
    return AppenderSkeleton::setOption(name, value);
}

void WriterAppender::setOutput(std::FILE* stream, bool owned) {
    file_ = stream;
    owned_ = owned;
    printStream_ = nullptr;
    count_ = 0;
    writeErrorReported_ = false;
}

void WriterAppender::setOutput(PrintStream* stream) {
    file_ = nullptr;
    owned_ = false;
    printStream_ = stream;
    count_ = 0;
    writeErrorReported_ = false;
}

void WriterAppender::write(const String& s) {
    if (s == nullptr) return;
    std::string converted;
    const std::string* data = &s;
    if (!isUtf8(encoding)) {
        try {
            Array<int8_t>* b = s.getBytes(encoding.trim());
            converted.assign(reinterpret_cast<const char*>(b->data()), static_cast<size_t>(b->length));
            data = &converted;
        } catch (Exception&) {
            // unsupported encoding: keep UTF-8 (log4j warns once when creating the writer)
        }
    }
    if (printStream_ != nullptr) {
        printStream_->print(String(*data));
    } else if (file_ != nullptr) {
        if (!data->empty() && std::fwrite(data->data(), 1, data->size(), file_) != data->size()) {
            if (!writeErrorReported_) {
                writeErrorReported_ = true;
                IOException e(String(std::strerror(errno)));
                errorHandler->error(str("Failed to write [", s, "]."), &e, WRITE_FAILURE);
            }
            return;
        }
    } else {
        return;
    }
    count_ += static_cast<int64_t>(data->size());
}

void WriterAppender::flush() {
    if (printStream_ != nullptr) {
        printStream_->flush();
    } else if (file_ != nullptr && std::fflush(file_) != 0) {
        IOException e(String(std::strerror(errno)));
        errorHandler->error(String("Failed to flush writer,"), &e, FLUSH_FAILURE);
    }
}

void WriterAppender::append(LoggingEvent* event) {
    if (!checkEntryConditions()) return;
    subAppend(event);
}

bool WriterAppender::checkEntryConditions() {
    if (closed) {
        LogLog::warn(String("Not allowed to write to a closed appender."));
        return false;
    }
    if (!hasOutput()) {
        errorHandler->error(str("No output stream or file set for the appender named [", name, "]."));
        return false;
    }
    if (layout == nullptr) {
        errorHandler->error(str("No layout set for the appender named [", name, "]."));
        return false;
    }
    return true;
}

void WriterAppender::subAppend(LoggingEvent* event) {
    write(layout->format(event));
    if (layout->ignoresThrowable()) {
        Array<String>* s = event->getThrowableStrRep();
        if (s != nullptr) {
            for (const String& line : *s) {
                write(line);
                write(Layout::LINE_SEP);
            }
        }
    }
    if (shouldFlush(event)) flush();
}

void WriterAppender::close() {
    JSYNC(this) {
        if (closed) return;
        closed = true;
        writeFooter();
        reset();
    }
}

void WriterAppender::reset() {
    closeWriter();
    file_ = nullptr;
    printStream_ = nullptr;
}

void WriterAppender::closeWriter() {
    if (file_ != nullptr) {
        if (owned_) {
            if (std::fclose(file_) != 0) {
                IOException e(String(std::strerror(errno)));
                LogLog::error(String("Could not close log file"), e);
            }
        } else {
            std::fflush(file_);
        }
        file_ = nullptr;
    }
    if (printStream_ != nullptr) printStream_->flush();
    printStream_ = nullptr;
}

void WriterAppender::writeHeader() {
    if (layout != nullptr) {
        String h = layout->getHeader();
        if (h != nullptr && hasOutput()) write(h);
    }
}

void WriterAppender::writeFooter() {
    if (layout != nullptr) {
        String f = layout->getFooter();
        if (f != nullptr && hasOutput()) {
            write(f);
            flush();
        }
    }
}

// =======================================================================================
// ConsoleAppender

ConsoleAppender::ConsoleAppender() {}

ConsoleAppender::ConsoleAppender(Layout* layout) : ConsoleAppender(layout, SYSTEM_OUT) {}

ConsoleAppender::ConsoleAppender(Layout* layout, const String& target) {
    setLayout(layout);
    setTarget(target);
    activateOptions();
}

void ConsoleAppender::setTarget(const String& value) {
    String v = value.trim();
    if (SYSTEM_OUT.equalsIgnoreCase(v)) {
        target_ = SYSTEM_OUT;
    } else if (SYSTEM_ERR.equalsIgnoreCase(v)) {
        target_ = SYSTEM_ERR;
    } else {
        LogLog::warn(str("[", value, "] should be System.out or System.err."));
        LogLog::warn(String("Using previously set target, System.out by default."));
    }
}

namespace {
PrintStream* consoleStream(const String& target) {
    bool err = target.equals(ConsoleAppender::SYSTEM_ERR);
    PrintStream* ps = err ? System::err : System::out;
    if (ps == nullptr) ps = err ? ::jlang::detail::stderrStream() : ::jlang::detail::stdoutStream();
    return ps;
}
}  // namespace

void ConsoleAppender::activateOptions() {
    setOutput(consoleStream(target_));
    WriterAppender::activateOptions();
}

void ConsoleAppender::subAppend(LoggingEvent* event) {
    if (follow_) {
        PrintStream* cur = consoleStream(target_);
        int64_t c = getCount();
        setOutput(cur);
        setCount(c);
    }
    WriterAppender::subAppend(event);
}

bool ConsoleAppender::setOption(String name, String value) {
    if (optionIs(name, "target")) {
        setTarget(value);
        return true;
    }
    if (optionIs(name, "follow")) {
        setFollow(toBoolOption(value));
        return true;
    }
    return WriterAppender::setOption(name, value);
}

// =======================================================================================
// FileAppender

FileAppender::FileAppender() {}

FileAppender::FileAppender(Layout* layout, const String& filename, bool append, bool bufferedIO, int32_t bufferSize) {
    this->layout = layout;
    FileAppender::setFile(filename, append, bufferedIO, bufferSize);
}

FileAppender::FileAppender(Layout* layout, const String& filename, bool append) {
    this->layout = layout;
    FileAppender::setFile(filename, append, false, bufferSize);
}

FileAppender::FileAppender(Layout* layout, const String& filename) : FileAppender(layout, filename, true) {}

void FileAppender::setFile(String file) {
    // Trim spaces from both ends. The user probably does not want trailing spaces in file names.
    fileName = file.trim();
}

void FileAppender::setBufferedIO(bool bufferedIO) {
    this->bufferedIO = bufferedIO;
    if (bufferedIO) immediateFlush = false;
}

void FileAppender::setFile(String name, bool append, bool bufferedIO, int32_t bufferSize) {
    // (by value: reset() below clears this->fileName, which a caller may pass)
    JSYNC(this) {
        LogLog::debug(str("setFile called: ", name, ", ", append));
        if (bufferedIO) setImmediateFlush(false);
        reset();
        std::FILE* f = openLogFile(name, append);
        if (bufferedIO) {
            if (bufferSize <= 0) {
                std::fclose(f);
                throw IllegalArgumentException(String("Buffer size <= 0"));
            }
            std::setvbuf(f, nullptr, _IOFBF, static_cast<size_t>(bufferSize));
        }
        setOutput(f, true);
        this->fileName = name;
        this->fileAppend = append;
        this->bufferedIO = bufferedIO;
        this->bufferSize = bufferSize;
        writeHeader();
        LogLog::debug(String("setFile ended"));
    }
}

void FileAppender::activateOptions() {
    if (fileName != nullptr) {
        const String f = fileName;
        try {
            setFile(f, fileAppend, bufferedIO, bufferSize);
        } catch (IOException& e) {
            errorHandler->error(str("setFile(", f, ",", fileAppend, ") call failed."), &e, FILE_OPEN_FAILURE);
        }
    } else {
        LogLog::warn(str("File option not set for appender [", name, "]."));
        LogLog::warn(String("Are you using FileAppender instead of ConsoleAppender?"));
    }
}

bool FileAppender::setOption(String name, String value) {
    if (optionIs(name, "file")) {
        setFile(value);
        return true;
    }
    if (optionIs(name, "append")) {
        setAppend(toBoolOption(value));
        return true;
    }
    if (optionIs(name, "bufferedIO")) {
        setBufferedIO(toBoolOption(value));
        return true;
    }
    if (optionIs(name, "bufferSize")) {
        setBufferSize(toIntOption(value));
        return true;
    }
    return WriterAppender::setOption(name, value);
}

void FileAppender::closeFile() { closeWriter(); }

void FileAppender::reset() {
    closeFile();
    fileName = nullptr;
    WriterAppender::reset();
}

// =======================================================================================
// RollingFileAppender

RollingFileAppender::RollingFileAppender() {}

RollingFileAppender::RollingFileAppender(Layout* layout, const String& filename, bool append) {
    this->layout = layout;
    RollingFileAppender::setFile(filename, append, false, bufferSize);
}

RollingFileAppender::RollingFileAppender(Layout* layout, const String& filename)
    : RollingFileAppender(layout, filename, true) {}

void RollingFileAppender::setMaxFileSize(const String& value) {
    maxFileSize = impl::toFileSize(value, maxFileSize + 1);
}

void RollingFileAppender::setFile(String name, bool append, bool bufferedIO, int32_t bufferSize) {
    JSYNC(this) {
        FileAppender::setFile(name, append, bufferedIO, bufferSize);
        if (append) setCount(fileLength(name));
    }
}

void RollingFileAppender::rollOver() {
    JSYNC(this) {
        if (hasOutput()) {
            int64_t size = getCount();
            LogLog::debug(str("rolling over count=", size));
            // if the operation fails, do not roll again until maxFileSize more bytes are written
            nextRollover_ = size + maxFileSize;
        }
        LogLog::debug(str("maxBackupIndex=", maxBackupIndex));
        const String base = fileName;
        bool renameSucceeded = true;
        if (maxBackupIndex > 0) {
            std::string oldest = str(base, ".", maxBackupIndex);
            if (fileExists(oldest)) renameSucceeded = std::remove(oldest.c_str()) == 0;
            for (int32_t i = maxBackupIndex - 1; i >= 1 && renameSucceeded; i--) {
                std::string file = str(base, ".", i);
                if (fileExists(file)) {
                    std::string target = str(base, ".", i + 1);
                    LogLog::debug(str("Renaming file ", file, " to ", target));
                    renameSucceeded = std::rename(file.c_str(), target.c_str()) == 0;
                }
            }
            if (renameSucceeded) {
                std::string target = str(base, ".", 1);
                closeFile();
                LogLog::debug(str("Renaming file ", base, " to ", target));
                renameSucceeded = std::rename(base.c_str(), target.c_str()) == 0;
                if (!renameSucceeded) {
                    try {
                        setFile(base, true, bufferedIO, bufferSize);
                    } catch (IOException& e) {
                        LogLog::error(str("setFile(", base, ", true) call failed."), e);
                    }
                }
            }
        }
        if (renameSucceeded) {
            try {
                // This also closes the file (multiple closes are safe).
                setFile(base, false, bufferedIO, bufferSize);
                nextRollover_ = 0;
            } catch (IOException& e) {
                LogLog::error(str("setFile(", base, ", false) call failed."), e);
            }
        }
    }
}

bool RollingFileAppender::setOption(String name, String value) {
    if (optionIs(name, "maxFileSize")) {
        setMaxFileSize(value);
        return true;
    }
    if (optionIs(name, "maximumFileSize")) {
        setMaximumFileSize(toLongOption(value));
        return true;
    }
    if (optionIs(name, "maxBackupIndex")) {
        setMaxBackupIndex(toIntOption(value));
        return true;
    }
    return FileAppender::setOption(name, value);
}

void RollingFileAppender::subAppend(LoggingEvent* event) {
    FileAppender::subAppend(event);
    if (fileName != nullptr && hasOutput()) {
        int64_t size = getCount();
        if (size >= maxFileSize && size >= nextRollover_) rollOver();
    }
}

// =======================================================================================
// Filters (org.apache.log4j.varia)

int32_t LevelMatchFilter::decide(LoggingEvent* event) {
    if (levelToMatch_ == nullptr) return NEUTRAL;
    if (levelToMatch_->equals(event->getLevel())) return acceptOnMatch_ ? ACCEPT : DENY;
    return NEUTRAL;
}

void LevelMatchFilter::setLevelToMatch(const String& level) { levelToMatch_ = impl::toLevel(level, nullptr); }

String LevelMatchFilter::getLevelToMatch() {
    return levelToMatch_ == nullptr ? String(nullptr) : levelToMatch_->toString();
}

bool LevelMatchFilter::setOption(String name, String value) {
    if (optionIs(name, "levelToMatch")) {
        setLevelToMatch(value);
        return true;
    }
    if (optionIs(name, "acceptOnMatch")) {
        setAcceptOnMatch(toBoolOption(value));
        return true;
    }
    return Filter::setOption(name, value);
}

int32_t LevelRangeFilter::decide(LoggingEvent* event) {
    if (levelMin_ != nullptr && !event->getLevel()->isGreaterOrEqual(levelMin_)) return DENY;
    if (levelMax_ != nullptr && event->getLevel()->toInt() > levelMax_->toInt()) return DENY;
    return acceptOnMatch_ ? ACCEPT : NEUTRAL;
}

bool LevelRangeFilter::setOption(String name, String value) {
    if (optionIs(name, "levelMin")) {
        setLevelMin(toLevelOption(value));
        return true;
    }
    if (optionIs(name, "levelMax")) {
        setLevelMax(toLevelOption(value));
        return true;
    }
    if (optionIs(name, "acceptOnMatch")) {
        setAcceptOnMatch(toBoolOption(value));
        return true;
    }
    return Filter::setOption(name, value);
}

int32_t StringMatchFilter::decide(LoggingEvent* event) {
    String msg = event->getRenderedMessage();
    if (msg == nullptr || stringToMatch_ == nullptr) return NEUTRAL;
    if (msg.indexOf(stringToMatch_) == -1) return NEUTRAL;
    return acceptOnMatch_ ? ACCEPT : DENY;
}

bool StringMatchFilter::setOption(String name, String value) {
    if (optionIs(name, "stringToMatch")) {
        setStringToMatch(value);
        return true;
    }
    if (optionIs(name, "acceptOnMatch")) {
        setAcceptOnMatch(toBoolOption(value));
        return true;
    }
    return Filter::setOption(name, value);
}

}  // namespace jlang::log4j
