// jlang/IOFile.cpp - java.io.File (UnixFileSystem semantics), FileFilter adapters and the
// commons-io FileUtils / FileFilterUtils.
#include <jlang/IO.h>

#include <cerrno>
#include <climits>
#include <cstdlib>
#include <cstring>
#include <dirent.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <sys/time.h>
#include <sys/types.h>
#include <unistd.h>

#include <mutex>
#include <string>
#include <vector>

namespace jlang {

namespace {

String errnoString(int err) {
    char buf[256];
    return String(strerror_r(err, buf, sizeof buf));
}

// UnixFileSystem.normalize: collapse duplicate slashes, drop a trailing slash (not "/").
std::string normalize(const std::string& p) {
    std::string r;
    r.reserve(p.size());
    char prev = 0;
    for (char c : p) {
        if (c == '/' && prev == '/') continue;
        r.push_back(c);
        prev = c;
    }
    if (r.size() > 1 && r.back() == '/') r.pop_back();
    return r;
}

// UnixFileSystem.resolve(parent, child) (both normalized)
std::string resolve(const std::string& parent, const std::string& child) {
    if (child.empty()) return parent;
    if (child[0] == '/') {
        if (parent == "/") return child;
        return parent + child;
    }
    if (parent == "/") return parent + child;
    return parent + "/" + child;
}

int32_t prefixLength(const std::string& p) { return (!p.empty() && p[0] == '/') ? 1 : 0; }

bool statPath(const std::string& p, struct stat& st) { return !p.empty() && ::stat(p.c_str(), &st) == 0; }

std::string userDir() {
    String d = System::getProperty(String("user.dir"));
    if (!d.isNull() && !d.isEmpty()) return std::string(d);
    char buf[PATH_MAX];
    if (::getcwd(buf, sizeof buf) != nullptr) return std::string(buf);
    return std::string("/");
}

// Collapses "." and ".." textually on an absolute path.
std::string collapse(const std::string& abs) {
    std::vector<std::string> parts;
    size_t i = 0;
    while (i < abs.size()) {
        size_t j = abs.find('/', i);
        if (j == std::string::npos) j = abs.size();
        std::string seg = abs.substr(i, j - i);
        if (seg.empty() || seg == ".") {
        } else if (seg == "..") {
            if (!parts.empty()) parts.pop_back();
        } else {
            parts.push_back(seg);
        }
        i = j + 1;
    }
    std::string r;
    for (const auto& s : parts) r += "/" + s;
    return r.empty() ? std::string("/") : r;
}

std::string canonicalize(const std::string& abs) {
    std::string c = collapse(abs);
    char buf[PATH_MAX];
    if (::realpath(c.c_str(), buf) != nullptr) return std::string(buf);
    // Resolve the longest existing prefix, append the rest.
    std::string rest;
    std::string head = c;
    while (head.size() > 1) {
        size_t slash = head.rfind('/');
        std::string tail = head.substr(slash + 1);
        head = slash == 0 ? std::string("/") : head.substr(0, slash);
        rest = rest.empty() ? tail : tail + "/" + rest;
        if (::realpath(head.c_str(), buf) != nullptr) {
            std::string h(buf);
            return h == "/" ? "/" + rest : h + "/" + rest;
        }
    }
    return c;
}

}  // namespace

// =======================================================================================
// FileFilter

bool FileFilter::accept(File* dir, const String& name) { return accept(new File(dir, name)); }

// =======================================================================================
// File

File::File(const String& pathname) {
    if (pathname.isNull()) throw NullPointerException();
    path_ = String(normalize(pathname));
    prefixLength_ = prefixLength(path_);
}

File::File(const String& parent, const String& child) {
    if (child.isNull()) throw NullPointerException();
    if (!parent.isNull()) {
        if (parent.isEmpty()) path_ = String(resolve("/", normalize(child)));
        else path_ = String(resolve(normalize(parent), normalize(child)));
    } else {
        path_ = String(normalize(child));
    }
    prefixLength_ = prefixLength(path_);
}

File::File(File* parent, const String& child) {
    if (child.isNull()) throw NullPointerException();
    if (parent != nullptr) {
        if (parent->path_.isEmpty()) path_ = String(resolve("/", normalize(child)));
        else path_ = String(resolve(parent->path_, normalize(child)));
    } else {
        path_ = String(normalize(child));
    }
    prefixLength_ = prefixLength(path_);
}

File::File(Raw, const String& normalizedPath) : path_(normalizedPath), prefixLength_(prefixLength(normalizedPath)) {}

String File::getName() {
    const std::string& p = path_;
    size_t index = p.rfind('/');
    if (index == std::string::npos || static_cast<int32_t>(index) < prefixLength_)
        return String(p.substr(static_cast<size_t>(prefixLength_)));
    return String(p.substr(index + 1));
}

String File::getParent() {
    const std::string& p = path_;
    size_t index = p.rfind('/');
    if (index == std::string::npos || static_cast<int32_t>(index) < prefixLength_) {
        if (prefixLength_ > 0 && static_cast<int32_t>(p.size()) > prefixLength_)
            return String(p.substr(0, static_cast<size_t>(prefixLength_)));
        return String();
    }
    return String(p.substr(0, index));
}

File* File::getParentFile() {
    String p = getParent();
    if (p.isNull()) return nullptr;
    return new File(Raw{}, p);
}

String File::getAbsolutePath() {
    if (isAbsolute()) return path_;
    return String(resolve(normalize(userDir()), path_));
}

File* File::getAbsoluteFile() { return new File(Raw{}, getAbsolutePath()); }

String File::getCanonicalPath() { return String(canonicalize(std::string(getAbsolutePath()))); }

File* File::getCanonicalFile() { return new File(Raw{}, getCanonicalPath()); }

String File::toURIPath() {
    std::string p = std::string(getAbsolutePath());
    if (p.empty() || p.back() != '/') {
        if (isDirectory()) p.push_back('/');
    }
    return String(p);
}

bool File::canRead() { return !path_.isEmpty() && ::access(path_.c_str(), R_OK) == 0; }
bool File::canWrite() { return !path_.isEmpty() && ::access(path_.c_str(), W_OK) == 0; }
bool File::canExecute() { return !path_.isEmpty() && ::access(path_.c_str(), X_OK) == 0; }

bool File::exists() {
    struct stat st;
    return statPath(path_, st);
}

bool File::isDirectory() {
    struct stat st;
    return statPath(path_, st) && S_ISDIR(st.st_mode);
}

bool File::isFile() {
    struct stat st;
    return statPath(path_, st) && S_ISREG(st.st_mode);
}

bool File::isHidden() { return getName().startsWith(String(".")); }

int64_t File::lastModified() {
    struct stat st;
    if (!statPath(path_, st)) return 0;
    return static_cast<int64_t>(st.st_mtim.tv_sec) * 1000 + st.st_mtim.tv_nsec / 1000000;
}

int64_t File::length() {
    struct stat st;
    if (!statPath(path_, st)) return 0;
    return static_cast<int64_t>(st.st_size);
}

int64_t File::getTotalSpace() {
    struct statvfs s;
    if (path_.isEmpty() || ::statvfs(path_.c_str(), &s) != 0) return 0;
    return static_cast<int64_t>(s.f_blocks) * static_cast<int64_t>(s.f_frsize);
}

int64_t File::getFreeSpace() {
    struct statvfs s;
    if (path_.isEmpty() || ::statvfs(path_.c_str(), &s) != 0) return 0;
    return static_cast<int64_t>(s.f_bfree) * static_cast<int64_t>(s.f_frsize);
}

int64_t File::getUsableSpace() {
    struct statvfs s;
    if (path_.isEmpty() || ::statvfs(path_.c_str(), &s) != 0) return 0;
    return static_cast<int64_t>(s.f_bavail) * static_cast<int64_t>(s.f_frsize);
}

bool File::createNewFile() {
    int fd = ::open(path_.c_str(), O_RDWR | O_CREAT | O_EXCL | O_CLOEXEC, 0666);
    if (fd < 0) {
        int err = errno;
        if (err == EEXIST) return false;
        throw IOException(errnoString(err));
    }
    ::close(fd);
    return true;
}

bool File::delete_() {
    if (path_.isEmpty()) return false;
    if (::unlink(path_.c_str()) == 0) return true;
    if (errno == EISDIR || errno == EPERM) return ::rmdir(path_.c_str()) == 0;
    return false;
}

namespace {
std::mutex& exitListLock() {
    static std::mutex* m = new std::mutex();
    return *m;
}
std::vector<std::string>* exitList = nullptr;
void deleteAtExit() {
    std::lock_guard<std::mutex> g(exitListLock());
    if (exitList == nullptr) return;
    for (auto it = exitList->rbegin(); it != exitList->rend(); ++it) {
        if (::unlink(it->c_str()) != 0) ::rmdir(it->c_str());
    }
}
}  // namespace

void File::deleteOnExit() {
    std::lock_guard<std::mutex> g(exitListLock());
    if (exitList == nullptr) {
        exitList = new std::vector<std::string>();
        std::atexit(deleteAtExit);
        Runtime::getRuntime()->addShutdownHook(Runnable::of([]() { deleteAtExit(); }));
    }
    exitList->push_back(std::string(path_));
}

Array<String>* File::list() {
    if (path_.isEmpty()) return nullptr;
    DIR* d = ::opendir(path_.c_str());
    if (d == nullptr) return nullptr;
    std::vector<String> names;
    for (;;) {
        errno = 0;
        dirent* e = ::readdir(d);
        if (e == nullptr) break;
        if (std::strcmp(e->d_name, ".") == 0 || std::strcmp(e->d_name, "..") == 0) continue;
        names.emplace_back(e->d_name);
    }
    ::closedir(d);
    auto* r = new Array<String>(static_cast<int32_t>(names.size()));
    for (size_t i = 0; i < names.size(); i++) (*r)[static_cast<int32_t>(i)] = names[i];
    return r;
}

Array<String>* File::list(FilenameFilter* filter) {
    Array<String>* names = list();
    if (names == nullptr || filter == nullptr) return names;
    std::vector<String> v;
    for (const String& n : *names)
        if (filter->accept(this, n)) v.push_back(n);
    auto* r = new Array<String>(static_cast<int32_t>(v.size()));
    for (size_t i = 0; i < v.size(); i++) (*r)[static_cast<int32_t>(i)] = v[i];
    return r;
}

Array<File*>* File::listFiles() {
    Array<String>* names = list();
    if (names == nullptr) return nullptr;
    auto* r = new Array<File*>(names->length);
    for (int32_t i = 0; i < names->length; i++)
        (*r)[i] = new File(Raw{}, String(resolve(path_, (*names)[i])));
    return r;
}

Array<File*>* File::listFiles(FilenameFilter* filter) {
    Array<String>* names = list();
    if (names == nullptr) return nullptr;
    std::vector<File*> v;
    for (const String& n : *names)
        if (filter == nullptr || filter->accept(this, n)) v.push_back(new File(Raw{}, String(resolve(path_, n))));
    auto* r = new Array<File*>(static_cast<int32_t>(v.size()));
    for (size_t i = 0; i < v.size(); i++) (*r)[static_cast<int32_t>(i)] = v[i];
    return r;
}

Array<File*>* File::listFiles(FileFilter* filter) {
    Array<String>* names = list();
    if (names == nullptr) return nullptr;
    std::vector<File*> v;
    for (const String& n : *names) {
        File* f = new File(Raw{}, String(resolve(path_, n)));
        if (filter == nullptr || filter->accept(f)) v.push_back(f);
    }
    auto* r = new Array<File*>(static_cast<int32_t>(v.size()));
    for (size_t i = 0; i < v.size(); i++) (*r)[static_cast<int32_t>(i)] = v[i];
    return r;
}

bool File::mkdir() { return !path_.isEmpty() && ::mkdir(path_.c_str(), 0777) == 0; }

bool File::mkdirs() {
    if (exists()) return false;
    if (mkdir()) return true;
    File* canonFile = nullptr;
    try {
        canonFile = getCanonicalFile();
    } catch (IOException&) {
        return false;
    }
    File* parent = canonFile->getParentFile();
    return parent != nullptr && (parent->mkdirs() || parent->exists()) && canonFile->mkdir();
}

bool File::renameTo(File* dest) {
    if (dest == nullptr) throw NullPointerException();
    if (path_.isEmpty() || dest->path_.isEmpty()) return false;
    return ::rename(path_.c_str(), dest->path_.c_str()) == 0;
}

bool File::setLastModified(int64_t time) {
    if (time < 0) throw IllegalArgumentException(String("Negative time"));
    struct timeval tv[2];
    struct stat st;
    if (!statPath(path_, st)) return false;
    tv[0].tv_sec = st.st_atim.tv_sec;
    tv[0].tv_usec = st.st_atim.tv_nsec / 1000;
    tv[1].tv_sec = static_cast<time_t>(time / 1000);
    tv[1].tv_usec = static_cast<suseconds_t>((time % 1000) * 1000);
    return ::utimes(path_.c_str(), tv) == 0;
}

namespace {
bool changeMode(const String& path, mode_t bits, bool set) {
    struct stat st;
    if (!statPath(path, st)) return false;
    mode_t m = st.st_mode & 07777;
    m = set ? (m | bits) : (m & ~bits);
    return ::chmod(path.c_str(), m) == 0;
}
}  // namespace

bool File::setReadOnly() { return changeMode(path_, S_IWUSR | S_IWGRP | S_IWOTH, false); }
bool File::setWritable(bool writable, bool ownerOnly) {
    return changeMode(path_, ownerOnly ? S_IWUSR : (S_IWUSR | S_IWGRP | S_IWOTH), writable);
}
bool File::setReadable(bool readable, bool ownerOnly) {
    return changeMode(path_, ownerOnly ? S_IRUSR : (S_IRUSR | S_IRGRP | S_IROTH), readable);
}
bool File::setExecutable(bool executable, bool ownerOnly) {
    return changeMode(path_, ownerOnly ? S_IXUSR : (S_IXUSR | S_IXGRP | S_IXOTH), executable);
}

File* File::createTempFile(const String& prefix, const String& suffix) { return createTempFile(prefix, suffix, nullptr); }

File* File::createTempFile(const String& prefix, const String& suffix, File* directory) {
    if (prefix.isNull()) throw NullPointerException();
    if (prefix.length() < 3) throw IllegalArgumentException(String("Prefix string \"") + prefix + "\" too short: length must be at least 3");
    String suf = suffix.isNull() ? String(".tmp") : suffix;
    std::string dir;
    if (directory != nullptr) {
        dir = std::string(directory->getPath());
    } else {
        String t = System::getProperty(String("java.io.tmpdir"));
        dir = t.isNull() || t.isEmpty() ? std::string("/tmp") : std::string(t);
    }
    std::string tmpl = resolve(normalize(dir), std::string(prefix) + "XXXXXX" + std::string(suf));
    std::vector<char> buf(tmpl.begin(), tmpl.end());
    buf.push_back('\0');
    int fd = ::mkostemps(buf.data(), static_cast<int>(suf.size()), O_CLOEXEC);
    if (fd < 0) throw IOException(errnoString(errno));
    ::close(fd);
    return new File(String(buf.data()));
}

Array<File*>* File::listRoots() {
    auto* r = new Array<File*>(1);
    (*r)[0] = new File(String("/"));
    return r;
}

int32_t File::compareTo(File* pathname) {
    if (pathname == nullptr) throw NullPointerException();
    return path_.compareTo(pathname->path_);
}

bool File::equals(Object* obj) {
    File* f = dynamic_cast<File*>(obj);
    return f != nullptr && compareTo(f) == 0;
}

int32_t File::hashCode() { return path_.hashCode() ^ 1234321; }

// =======================================================================================
// FileFilterUtils (commons-io filters)

namespace {

class AndFilter final : public virtual FileFilter {
public:
    explicit AndFilter(std::vector<FileFilter*> fs) : fs_(std::move(fs)) {}
    bool accept(File* f) override {
        if (fs_.empty()) return false;
        for (FileFilter* x : fs_)
            if (!x->accept(f)) return false;
        return true;
    }
    bool accept(File* dir, const String& name) override {
        if (fs_.empty()) return false;
        for (FileFilter* x : fs_)
            if (!x->accept(dir, name)) return false;
        return true;
    }

private:
    std::vector<FileFilter*> fs_;
};

class OrFilter final : public virtual FileFilter {
public:
    explicit OrFilter(std::vector<FileFilter*> fs) : fs_(std::move(fs)) {}
    bool accept(File* f) override {
        for (FileFilter* x : fs_)
            if (x->accept(f)) return true;
        return false;
    }
    bool accept(File* dir, const String& name) override {
        for (FileFilter* x : fs_)
            if (x->accept(dir, name)) return true;
        return false;
    }

private:
    std::vector<FileFilter*> fs_;
};

class NotFilter final : public virtual FileFilter {
public:
    explicit NotFilter(FileFilter* f) : f_(f) {}
    bool accept(File* f) override { return !f_->accept(f); }
    bool accept(File* dir, const String& name) override { return !f_->accept(dir, name); }

private:
    FileFilter* f_;
};

// Name-based filters: accept(File) uses file->getName(); accept(dir, name) uses name.
class NamePredicateFilter final : public virtual FileFilter {
public:
    enum Kind { PREFIX, SUFFIX, NAME };
    NamePredicateFilter(Kind k, std::vector<String> vals) : k_(k), vals_(std::move(vals)) {}
    bool accept(File* f) override { return test(f->getName()); }
    bool accept(File* dir, const String& name) override {
        (void)dir;
        return test(name);
    }

private:
    bool test(const String& name) {
        for (const String& v : vals_) {
            switch (k_) {
                case PREFIX:
                    if (name.startsWith(v)) return true;
                    break;
                case SUFFIX:
                    if (name.endsWith(v)) return true;
                    break;
                case NAME:
                    if (name.equals(v)) return true;
                    break;
            }
        }
        return false;
    }
    Kind k_;
    std::vector<String> vals_;
};

class ConstFilter final : public virtual FileFilter {
public:
    explicit ConstFilter(bool v) : v_(v) {}
    bool accept(File*) override { return v_; }
    bool accept(File*, const String&) override { return v_; }

private:
    bool v_;
};

template<class F>
class FileOnlyPredicate final : public virtual FileFilter {
public:
    explicit FileOnlyPredicate(F f) : f_(std::move(f)) {}
    using FileFilter::accept;
    bool accept(File* f) override { return f_(f); }

private:
    F f_;
};

template<class F>
FileFilter* filePredicate(F f) {
    return new FileOnlyPredicate<F>(std::move(f));
}

}  // namespace

FileFilter* FileFilterUtils::and_(FileFilter* a, FileFilter* b) {
    if (a == nullptr || b == nullptr) throw IllegalArgumentException(String("The filter must not be null"));
    return new AndFilter({a, b});
}

FileFilter* FileFilterUtils::and_(Array<FileFilter*>* filters) {
    if (filters == nullptr) throw IllegalArgumentException(String("The filters must not be null"));
    std::vector<FileFilter*> v;
    for (FileFilter* f : *filters) {
        if (f == nullptr) throw IllegalArgumentException(String("The filter must not be null"));
        v.push_back(f);
    }
    return new AndFilter(std::move(v));
}

FileFilter* FileFilterUtils::or_(FileFilter* a, FileFilter* b) {
    if (a == nullptr || b == nullptr) throw IllegalArgumentException(String("The filter must not be null"));
    return new OrFilter({a, b});
}

FileFilter* FileFilterUtils::or_(Array<FileFilter*>* filters) {
    if (filters == nullptr) throw IllegalArgumentException(String("The filters must not be null"));
    std::vector<FileFilter*> v;
    for (FileFilter* f : *filters) {
        if (f == nullptr) throw IllegalArgumentException(String("The filter must not be null"));
        v.push_back(f);
    }
    return new OrFilter(std::move(v));
}

FileFilter* FileFilterUtils::notFileFilter(FileFilter* filter) {
    if (filter == nullptr) throw IllegalArgumentException(String("The filter must not be null"));
    return new NotFilter(filter);
}

FileFilter* FileFilterUtils::prefixFileFilter(const String& prefix) {
    if (prefix.isNull()) throw IllegalArgumentException(String("The prefix must not be null"));
    return new NamePredicateFilter(NamePredicateFilter::PREFIX, {prefix});
}

FileFilter* FileFilterUtils::suffixFileFilter(const String& suffix) {
    if (suffix.isNull()) throw IllegalArgumentException(String("The suffix must not be null"));
    return new NamePredicateFilter(NamePredicateFilter::SUFFIX, {suffix});
}

FileFilter* FileFilterUtils::suffixFileFilter(Array<String>* suffixes) {
    if (suffixes == nullptr) throw IllegalArgumentException(String("The array of suffixes must not be null"));
    std::vector<String> v(suffixes->begin(), suffixes->end());
    return new NamePredicateFilter(NamePredicateFilter::SUFFIX, std::move(v));
}

FileFilter* FileFilterUtils::nameFileFilter(const String& name) {
    if (name.isNull()) throw IllegalArgumentException(String("The wildcard must not be null"));
    return new NamePredicateFilter(NamePredicateFilter::NAME, {name});
}

FileFilter* FileFilterUtils::directoryFileFilter() {
    static FileFilter* f = filePredicate([](File* file) { return file->isDirectory(); });
    return f;
}

FileFilter* FileFilterUtils::fileFileFilter() {
    static FileFilter* f = filePredicate([](File* file) { return file->isFile(); });
    return f;
}

FileFilter* FileFilterUtils::trueFileFilter() {
    static FileFilter* f = new ConstFilter(true);
    return f;
}

FileFilter* FileFilterUtils::falseFileFilter() {
    static FileFilter* f = new ConstFilter(false);
    return f;
}

FileFilter* FileFilterUtils::ageFileFilter(int64_t cutoff, bool acceptOlder) {
    return filePredicate([cutoff, acceptOlder](File* file) {
        bool newer = FileUtils::isFileNewer(file, cutoff);
        return acceptOlder ? !newer : newer;
    });
}

FileFilter* FileFilterUtils::sizeFileFilter(int64_t threshold, bool acceptLarger) {
    if (threshold < 0) throw IllegalArgumentException(String("The size must be non-negative"));
    return filePredicate([threshold, acceptLarger](File* file) {
        bool smaller = file->length() < threshold;
        return acceptLarger ? !smaller : smaller;
    });
}

FileFilter* FileFilterUtils::makeSVNAware(FileFilter* filter) {
    static FileFilter* svn = notFileFilter(and_(directoryFileFilter(), nameFileFilter(String(".svn"))));
    if (filter == nullptr) return svn;
    return and_(filter, svn);
}

FileFilter* FileFilterUtils::makeCVSAware(FileFilter* filter) {
    static FileFilter* cvs = notFileFilter(and_(directoryFileFilter(), nameFileFilter(String("CVS"))));
    if (filter == nullptr) return cvs;
    return and_(filter, cvs);
}

FileFilter* FileFilterUtils::makeDirectoryOnly(FileFilter* filter) {
    if (filter == nullptr) return directoryFileFilter();
    return and_(directoryFileFilter(), filter);
}

FileFilter* FileFilterUtils::makeFileOnly(FileFilter* filter) {
    if (filter == nullptr) return fileFileFilter();
    return and_(fileFileFilter(), filter);
}

FileFilter* FileFilterUtils::VISIBLE() {
    static FileFilter* f = filePredicate([](File* file) { return !file->isHidden(); });
    return f;
}

FileFilter* FileFilterUtils::HIDDEN() {
    static FileFilter* f = filePredicate([](File* file) { return file->isHidden(); });
    return f;
}

// =======================================================================================
// FileUtils

FileInputStream* FileUtils::openInputStream(File* file) {
    if (file->exists()) {
        if (file->isDirectory()) throw IOException(str("File '", file, "' exists but is a directory"));
        if (!file->canRead()) throw IOException(str("File '", file, "' cannot be read"));
    } else {
        throw FileNotFoundException(str("File '", file, "' does not exist"));
    }
    return new FileInputStream(file);
}

FileOutputStream* FileUtils::openOutputStream(File* file, bool append) {
    if (file->exists()) {
        if (file->isDirectory()) throw IOException(str("File '", file, "' exists but is a directory"));
        if (!file->canWrite()) throw IOException(str("File '", file, "' cannot be written to"));
    } else {
        File* parent = file->getParentFile();
        if (parent != nullptr && !parent->exists()) {
            if (!parent->mkdirs()) throw IOException(str("File '", file, "' could not be created"));
        }
    }
    return new FileOutputStream(file, append);
}

namespace {
void innerListFiles(List<File*>* files, File* directory, FileFilter* filter) {
    Array<File*>* found = directory->listFiles(filter);
    if (found != nullptr) {
        for (File* file : *found) {
            if (file->isDirectory()) innerListFiles(files, file, filter);
            else files->add(file);
        }
    }
}
}  // namespace

List<File*>* FileUtils::listFiles(File* directory, FileFilter* fileFilter, FileFilter* dirFilter) {
    if (!directory->isDirectory()) throw IllegalArgumentException(String("Parameter 'directory' is not a directory"));
    if (fileFilter == nullptr) throw NullPointerException(String("Parameter 'fileFilter' is null"));
    FileFilter* effFileFilter =
        FileFilterUtils::and_(fileFilter, FileFilterUtils::notFileFilter(FileFilterUtils::directoryFileFilter()));
    FileFilter* effDirFilter = dirFilter == nullptr ? FileFilterUtils::falseFileFilter()
                                                    : FileFilterUtils::and_(dirFilter, FileFilterUtils::directoryFileFilter());
    auto* files = new List<File*>();
    innerListFiles(files, directory, FileFilterUtils::or_(effFileFilter, effDirFilter));
    return files;
}

List<File*>* FileUtils::listFiles(File* directory, Array<String>* extensions, bool recursive) {
    FileFilter* filter;
    if (extensions == nullptr) {
        filter = FileFilterUtils::trueFileFilter();
    } else {
        auto* suffixes = new Array<String>(extensions->length);
        for (int32_t i = 0; i < extensions->length; i++) (*suffixes)[i] = str(".", (*extensions)[i]);
        filter = FileFilterUtils::suffixFileFilter(suffixes);
    }
    return listFiles(directory, filter, recursive ? FileFilterUtils::trueFileFilter() : FileFilterUtils::falseFileFilter());
}

Array<int8_t>* FileUtils::readFileToByteArray(File* file) {
    FileInputStream* in = openInputStream(file);
    Array<int8_t>* r;
    {
        JFINALLY { IOUtils::closeQuietly(in); };
        r = in->readAllBytes();
    }
    return r;
}

String FileUtils::readFileToString(File* file) { return readFileToString(file, String()); }

String FileUtils::readFileToString(File* file, const String& encoding) {
    Array<int8_t>* bytes = readFileToByteArray(file);
    Charset* cs = encoding.isNull() ? Charset::defaultCharset() : Charset::forNameIO(encoding);
    return cs->decodeToString(bytes);
}

List<String>* FileUtils::readLines(File* file) { return readLines(file, String()); }

List<String>* FileUtils::readLines(File* file, const String& encoding) {
    FileInputStream* in = openInputStream(file);
    List<String>* r;
    {
        JFINALLY { IOUtils::closeQuietly(in); };
        r = IOUtils::readLines(in, encoding);
    }
    return r;
}

LineIterator* FileUtils::lineIterator(File* file) { return lineIterator(file, String()); }

LineIterator* FileUtils::lineIterator(File* file, const String& encoding) {
    InputStream* in = nullptr;
    try {
        in = openInputStream(file);
        return IOUtils::lineIterator(in, encoding);
    } catch (IOException&) {
        IOUtils::closeQuietly(in);
        throw;
    } catch (RuntimeException&) {
        IOUtils::closeQuietly(in);
        throw;
    }
}

void FileUtils::writeStringToFile(File* file, const String& data) { writeStringToFile(file, data, String(), false); }

void FileUtils::writeStringToFile(File* file, const String& data, const String& encoding) {
    writeStringToFile(file, data, encoding, false);
}

void FileUtils::writeStringToFile(File* file, const String& data, const String& encoding, bool append) {
    OutputStream* out = openOutputStream(file, append);
    {
        JFINALLY { IOUtils::closeQuietly(out); };
        if (!data.isNull()) {
            Charset* cs = encoding.isNull() ? Charset::defaultCharset() : Charset::forNameIO(encoding);
            out->write(cs->encodeToArray(data));
        }
    }
}

void FileUtils::writeByteArrayToFile(File* file, Array<int8_t>* data) {
    OutputStream* out = openOutputStream(file, false);
    {
        JFINALLY { IOUtils::closeQuietly(out); };
        out->write(data);
    }
}

void FileUtils::writeLines(File* file, List<String>* lines) {
    OutputStream* out = openOutputStream(file, false);
    {
        JFINALLY { IOUtils::closeQuietly(out); };
        if (lines != nullptr) {
            std::string all;
            for (const String& l : *lines) {
                if (!l.isNull()) all += std::string(l);
                all += "\n";
            }
            auto* a = new Array<int8_t>(static_cast<int32_t>(all.size()));
            if (!all.empty()) std::memcpy(a->data(), all.data(), all.size());
            out->write(a);
        }
    }
}

int64_t FileUtils::checksumCRC32(File* file) {
    auto* crc = new CRC32();
    FileInputStream* in = openInputStream(file);
    {
        JFINALLY { IOUtils::closeQuietly(in); };
        auto* buf = new Array<int8_t>(65536);
        for (;;) {
            int32_t n = in->read(buf, 0, buf->length);
            if (n < 0) break;
            crc->update(buf, 0, n);
        }
    }
    return crc->getValue();
}

bool FileUtils::isFileOlder(File* file, int64_t timeMillis) {
    if (file == nullptr) throw IllegalArgumentException(String("No specified file"));
    if (!file->exists()) return false;
    return file->lastModified() < timeMillis;
}

bool FileUtils::isFileOlder(File* file, File* reference) {
    if (reference == nullptr) throw IllegalArgumentException(String("No specified reference file"));
    if (!reference->exists())
        throw IllegalArgumentException(str("The reference file '", reference, "' doesn't exist"));
    return isFileOlder(file, reference->lastModified());
}

bool FileUtils::isFileNewer(File* file, int64_t timeMillis) {
    if (file == nullptr) throw IllegalArgumentException(String("No specified file"));
    if (!file->exists()) return false;
    return file->lastModified() > timeMillis;
}

bool FileUtils::isFileNewer(File* file, File* reference) {
    if (reference == nullptr) throw IllegalArgumentException(String("No specified reference file"));
    if (!reference->exists())
        throw IllegalArgumentException(str("The reference file '", reference, "' doesn't exist"));
    return isFileNewer(file, reference->lastModified());
}

bool FileUtils::deleteQuietly(File* file) {
    if (file == nullptr) return false;
    try {
        if (file->isDirectory()) cleanDirectory(file);
    } catch (Exception&) {
    }
    try {
        return file->delete_();
    } catch (Exception&) {
        return false;
    }
}

void FileUtils::cleanDirectory(File* directory) {
    if (!directory->exists()) throw IllegalArgumentException(str(directory, " does not exist"));
    if (!directory->isDirectory()) throw IllegalArgumentException(str(directory, " is not a directory"));
    Array<File*>* files = directory->listFiles();
    if (files == nullptr) throw IOException(str("Failed to list contents of ", directory));
    IOException* exception = nullptr;
    for (File* file : *files) {
        try {
            forceDelete(file);
        } catch (IOException& e) {
            exception = static_cast<IOException*>(e.copyThrowable());
        }
    }
    if (exception != nullptr) exception->rethrow();
}

void FileUtils::deleteDirectory(File* directory) {
    if (!directory->exists()) return;
    cleanDirectory(directory);
    if (!directory->delete_()) throw IOException(str("Unable to delete directory ", directory, "."));
}

void FileUtils::forceDelete(File* file) {
    if (file->isDirectory()) {
        deleteDirectory(file);
    } else {
        bool filePresent = file->exists();
        if (!file->delete_()) {
            if (!filePresent) throw FileNotFoundException(str("File does not exist: ", file));
            throw IOException(str("Unable to delete file: ", file));
        }
    }
}

void FileUtils::forceMkdir(File* directory) {
    if (directory->exists()) {
        if (!directory->isDirectory())
            throw IOException(str("File ", directory, " exists and is not a directory. Unable to create directory."));
    } else {
        if (!directory->mkdirs()) {
            if (!directory->isDirectory()) throw IOException(str("Unable to create directory ", directory));
        }
    }
}

void FileUtils::copyFile(File* srcFile, File* destFile) {
    if (srcFile == nullptr) throw NullPointerException(String("Source must not be null"));
    if (destFile == nullptr) throw NullPointerException(String("Destination must not be null"));
    if (!srcFile->exists()) throw FileNotFoundException(str("Source '", srcFile, "' does not exist"));
    if (srcFile->isDirectory()) throw IOException(str("Source '", srcFile, "' exists but is a directory"));
    if (srcFile->getCanonicalPath().equals(destFile->getCanonicalPath()))
        throw IOException(str("Source '", srcFile, "' and destination '", destFile, "' are the same"));
    File* parent = destFile->getParentFile();
    if (parent != nullptr && !parent->exists() && !parent->mkdirs())
        throw IOException(str("Destination '", parent, "' directory cannot be created"));
    FileInputStream* in = new FileInputStream(srcFile);
    {
        JFINALLY { IOUtils::closeQuietly(in); };
        FileOutputStream* out = new FileOutputStream(destFile);
        JFINALLY { IOUtils::closeQuietly(out); };
        IOUtils::copyLarge(in, out);
    }
    if (srcFile->length() != destFile->length())
        throw IOException(str("Failed to copy full contents from '", srcFile, "' to '", destFile, "'"));
    destFile->setLastModified(srcFile->lastModified());
}

void FileUtils::moveFile(File* srcFile, File* destFile) {
    if (srcFile == nullptr) throw NullPointerException(String("Source must not be null"));
    if (destFile == nullptr) throw NullPointerException(String("Destination must not be null"));
    if (!srcFile->exists()) throw FileNotFoundException(str("Source '", srcFile, "' does not exist"));
    if (srcFile->isDirectory()) throw IOException(str("Source '", srcFile, "' is a directory"));
    if (destFile->exists()) throw IOException(str("Destination '", destFile, "' already exists"));
    if (destFile->isDirectory()) throw IOException(str("Destination '", destFile, "' is a directory"));
    if (!srcFile->renameTo(destFile)) {
        copyFile(srcFile, destFile);
        if (!srcFile->delete_()) {
            deleteQuietly(destFile);
            throw IOException(str("Failed to delete original file '", srcFile, "' after copy to '", destFile, "'"));
        }
    }
}

void FileUtils::touch(File* file) {
    if (!file->exists()) {
        OutputStream* out = openOutputStream(file);
        IOUtils::closeQuietly(out);
    }
    if (!file->setLastModified(System::currentTimeMillis()))
        throw IOException(str("Unable to set the last modification time for ", file));
}

int64_t FileUtils::sizeOf(File* file) {
    if (!file->exists()) throw IllegalArgumentException(str(file, " does not exist"));
    if (!file->isDirectory()) return file->length();
    int64_t size = 0;
    Array<File*>* files = file->listFiles();
    if (files == nullptr) return 0;
    for (File* f : *files) size += sizeOf(f);
    return size;
}

}  // namespace jlang
