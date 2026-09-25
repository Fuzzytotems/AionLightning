// Tests for <jlang/IO.h>: File, byte streams, readers/writers, Data streams, zip, commons-io.
#include "jtest.h"

#include <jlang/IO.h>
#include <jlang/Nio.h>

#include <zlib.h>

#include <cstring>
#include <string>
#include <unistd.h>

using namespace jlang;

namespace {

File* tempDir() {
    File* f = File::createTempFile(String("jlangio"), String(".d"));
    f->delete_();
    JCHECK(f->mkdir());
    return f;
}

void writeText(File* f, const String& s) {
    auto* w = new FileWriter(f);
    w->write(s);
    w->close();
}

std::string bytesToStd(Array<int8_t>* a) { return std::string(reinterpret_cast<const char*>(a->data()), static_cast<size_t>(a->length)); }

}  // namespace

JTEST(IOFilePaths) {
    auto* f = new File(String("a//b/c.txt/"));
    JCHECK_EQ(f->getPath(), String("a/b/c.txt"));
    JCHECK_EQ(f->getName(), String("c.txt"));
    JCHECK_EQ(f->getParent(), String("a/b"));
    JCHECK_EQ(f->getParentFile()->getName(), String("b"));
    JCHECK(!f->isAbsolute());
    JCHECK(f->getAbsolutePath().startsWith(String("/")));
    JCHECK(f->getAbsolutePath().endsWith(String("/a/b/c.txt")));
    JCHECK((new File(String("x")))->getParent().isNull());
    JCHECK_EQ((new File(String("/")))->getName(), String(""));
    JCHECK((new File(String("/")))->getParent().isNull());
    JCHECK_EQ((new File(String("/x")))->getParent(), String("/"));
    JCHECK_EQ((new File(String("/usr"), String("lib")))->getPath(), String("/usr/lib"));
    JCHECK_EQ((new File(String(""), String("lib")))->getPath(), String("/lib"));
    JCHECK_EQ((new File(static_cast<File*>(nullptr), String("lib")))->getPath(), String("lib"));
    JCHECK_EQ((new File(new File(String("/")), String("lib")))->getPath(), String("/lib"));
    JCHECK((new File(String("a")))->equals(new File(String("a/"))));
    JCHECK_EQ((new File(String("a")))->hashCode(), String("a").hashCode() ^ 1234321);
    JCHECK((new File(String(".hidden")))->isHidden());
    JCHECK_EQ(File::separator, String("/"));
    JCHECK_EQ(File::separatorChar, u'/');
    JCHECK_EQ((new File(String("/tmp/../tmp/./x/..")))->getCanonicalPath(), String("/tmp"));
    JCHECK_THROWS(NullPointerException, new File(String()));
}

JTEST(IOFileOps) {
    File* dir = tempDir();
    JCHECK(dir->isDirectory());
    File* f = new File(dir, String("t.txt"));
    JCHECK(!f->exists());
    JCHECK_EQ(f->length(), 0);
    JCHECK_EQ(f->lastModified(), 0);
    JCHECK(f->createNewFile());
    JCHECK(!f->createNewFile());
    JCHECK(f->isFile());
    writeText(f, String("hello"));
    JCHECK_EQ(f->length(), 5);
    JCHECK(f->lastModified() > 0);
    JCHECK(f->canRead());
    File* sub = new File(dir, String("x/y/z"));
    JCHECK(sub->mkdirs());
    JCHECK(!sub->mkdirs());
    JCHECK(!(new File(dir, String("q/r")))->mkdir());
    Array<String>* names = dir->list();
    JCHECK_EQ(names->length, 2);
    Array<File*>* files = dir->listFiles(FileFilter::of([](File* x) { return x->isFile(); }));
    JCHECK_EQ(files->length, 1);
    JCHECK_EQ((*files)[0]->getName(), String("t.txt"));
    Array<File*>* byName = dir->listFiles(FilenameFilter::of([](File*, const String& n) { return n.startsWith(String("x")); }));
    JCHECK_EQ(byName->length, 1);
    JCHECK((new File(dir, String("nope")))->list() == nullptr);
    File* renamed = new File(dir, String("u.txt"));
    JCHECK(f->renameTo(renamed));
    JCHECK(!f->exists());
    JCHECK(renamed->exists());
    // commons-io listFiles
    writeText(new File(dir, String("x/a.xml")), String("<a/>"));
    writeText(new File(dir, String("x/y/new.xml")), String("<b/>"));
    writeText(new File(dir, String("x/y/b.xml")), String("<b/>"));
    writeText(new File(dir, String("x/.c.xml")), String("<c/>"));
    (new File(dir, String("x/.svn")))->mkdir();
    writeText(new File(dir, String("x/.svn/d.xml")), String("<d/>"));
    List<File*>* xmls = FileUtils::listFiles(dir, Array<String>::of({String("xml")}), true);
    JCHECK_EQ(xmls->size(), 5);
    JCHECK_EQ(FileUtils::listFiles(dir, Array<String>::of({String("xml")}), false)->size(), 0);
    // XmlMerger/Reload filter: not prefix "new", suffix ".xml", visible; dirs: SVN-aware visible
    FileFilter* dirFilter = FileFilterUtils::makeSVNAware(FileFilterUtils::VISIBLE());
    List<File*>* merged = FileUtils::listFiles(
        dir,
        FileFilterUtils::and_(FileFilterUtils::and_(FileFilterUtils::notFileFilter(FileFilterUtils::prefixFileFilter(String("new"))),
                                                    FileFilterUtils::suffixFileFilter(String(".xml"))),
                              FileFilterUtils::VISIBLE()),
        dirFilter);
    JCHECK_EQ(merged->size(), 2);  // x/a.xml, x/y/b.xml
    JCHECK_THROWS(IllegalArgumentException, FileUtils::listFiles(renamed, FileFilterUtils::trueFileFilter(), nullptr));
    // DataLoader filter: and(and(not(name "new"), suffix ".txt"), VISIBLE)
    writeText(new File(dir, String("x/new")), String("n"));
    writeText(new File(dir, String("x/list.txt")), String("1\n2\r\n3"));
    List<File*>* txt = FileUtils::listFiles(
        new File(dir, String("x")),
        FileFilterUtils::and_(FileFilterUtils::and_(FileFilterUtils::notFileFilter(FileFilterUtils::nameFileFilter(String("new"))),
                                                    FileFilterUtils::suffixFileFilter(String(".txt"))),
                              FileFilterUtils::VISIBLE()),
        FileFilterUtils::VISIBLE());
    JCHECK_EQ(txt->size(), 1);
    LineIterator* it = FileUtils::lineIterator(txt->get(0));
    std::string lines;
    while (it->hasNext()) lines += std::string(it->nextLine()) + ";";
    LineIterator::closeQuietly(it);
    JCHECK_EQ(lines, std::string("1;2;3;"));
    JCHECK_THROWS(NoSuchElementException, it->nextLine());
    JCHECK_EQ(FileUtils::readLines(txt->get(0))->size(), 3);
    JCHECK_EQ(FileUtils::readFileToString(txt->get(0)), String("1\n2\r\n3"));
    // checksumCRC32 == zlib crc32
    File* crcFile = new File(dir, String("x/list.txt"));
    JCHECK_EQ(FileUtils::checksumCRC32(crcFile), static_cast<int64_t>(::crc32(0, reinterpret_cast<const Bytef*>("1\n2\r\n3"), 6)));
    JCHECK(FileUtils::isFileOlder(crcFile, System::currentTimeMillis() + 100000));
    JCHECK(!FileUtils::isFileOlder(new File(dir, String("missing")), System::currentTimeMillis()));
    JCHECK_THROWS(FileNotFoundException, FileUtils::openInputStream(new File(dir, String("missing"))));
    JCHECK_THROWS(IOException, FileUtils::openInputStream(dir));
    FileUtils::writeStringToFile(new File(dir, String("w/out.txt")), String("data\xC3\xA9"));
    JCHECK_EQ(FileUtils::readFileToString(new File(dir, String("w/out.txt"))), String("data\xC3\xA9"));
    JCHECK(FileUtils::deleteQuietly(dir));
    JCHECK(!dir->exists());
    JCHECK(!FileUtils::deleteQuietly(nullptr));
}

JTEST(IOByteStreams) {
    File* dir = tempDir();
    File* f = new File(dir, String("b.bin"));
    auto* out = new DataOutputStream(new BufferedOutputStream(new FileOutputStream(f)));
    out->writeInt(0x01020304);
    out->writeShort(-2);
    out->writeLong(INT64_C(-1234567890123));
    out->writeFloat(1.5f);
    out->writeDouble(-0.25);
    out->writeBoolean(true);
    out->writeByte(200);
    out->writeChar(0x4e2d);
    out->writeUTF(String("A\0B", 3));
    out->writeUTF(String("\xF0\x9F\x98\x80"));  // U+1F600: two surrogates, 3 bytes each
    JCHECK_EQ(out->size(), 4 + 2 + 8 + 4 + 8 + 1 + 1 + 2 + (2 + 4) + (2 + 6));
    out->close();
    JCHECK_EQ(f->length(), 44);
    auto* in = new DataInputStream(new BufferedInputStream(new FileInputStream(f)));
    JCHECK_EQ(in->readInt(), 0x01020304);
    JCHECK_EQ(in->readShort(), -2);
    JCHECK_EQ(in->readLong(), INT64_C(-1234567890123));
    JCHECK_EQ(in->readFloat(), 1.5f);
    JCHECK_EQ(in->readDouble(), -0.25);
    JCHECK(in->readBoolean());
    JCHECK_EQ(in->readUnsignedByte(), 200);
    JCHECK_EQ(in->readChar(), u'中');
    JCHECK_EQ(in->readUTF(), String("A\0B", 3));
    JCHECK_EQ(in->readUTF(), String("\xF0\x9F\x98\x80"));
    JCHECK_THROWS(EOFException, in->readInt());
    in->close();
    // raw bytes of the modified UTF-8 of "A\0B": 00 04 41 C0 80 42
    auto* fis = new FileInputStream(f);
    fis->skip(30);
    auto* raw = new Array<int8_t>(6);
    JCHECK_EQ(fis->read(raw), 6);
    JCHECK_EQ(bytesToStd(raw), std::string("\x00\x04\x41\xC0\x80\x42", 6));
    JCHECK_EQ(fis->available(), 8);
    fis->close();
    JCHECK_THROWS(IOException, fis->read());
    JCHECK_THROWS(FileNotFoundException, new FileInputStream(new File(dir, String("none"))));
    try {
        new FileInputStream(dir);
        JCHECK(false);
    } catch (FileNotFoundException& e) {
        JCHECK(e.getMessage().endsWith(String(" (Is a directory)")));
    }
    // ByteArray streams
    auto* bos = new ByteArrayOutputStream();
    bos->write(65);
    bos->write(Array<int8_t>::of({66, 67, 68}), 1, 2);
    JCHECK_EQ(bos->size(), 3);
    JCHECK_EQ(bos->toString(), String("ACD"));
    auto* bis = new ByteArrayInputStream(bos->toByteArray());
    JCHECK_EQ(bis->available(), 3);
    JCHECK(bis->markSupported());
    JCHECK_EQ(bis->read(), 65);
    bis->mark(0);
    JCHECK_EQ(bis->read(), 67);
    bis->reset();
    JCHECK_EQ(bis->read(), 67);
    JCHECK_EQ(bis->skip(10), 1);
    JCHECK_EQ(bis->read(), -1);
    JCHECK_EQ(bis->read(new Array<int8_t>(4), 0, 4), -1);
    // BufferedInputStream mark/reset over a large mark limit
    auto* big = new Array<int8_t>(100000);
    for (int32_t i = 0; i < big->length; i++) (*big)[i] = static_cast<int8_t>(i * 7);
    auto* buf = new BufferedInputStream(new ByteArrayInputStream(big), 16);
    buf->read();
    buf->mark(50000);
    auto* tmp = new Array<int8_t>(40000);
    JCHECK_EQ(buf->read(tmp), 40000);
    buf->reset();
    JCHECK_EQ(buf->read(), static_cast<uint8_t>(7));
    JCHECK_EQ(IOUtils::toByteArray(buf)->length, 100000 - 2);
    // HTMLCache idiom: new byte[bis.available()] + read(raw)
    writeText(f, String("<html>\xC3\xA9</html>"));
    auto* h = new BufferedInputStream(new FileInputStream(f));
    auto* all = new Array<int8_t>(h->available());
    JCHECK_EQ(h->read(all), all->length);
    IOUtils::closeQuietly(h);
    JCHECK_EQ(String(all, String("UTF-8")), String("<html>\xC3\xA9</html>"));
    // OutputStreamPrintStream
    auto* pbo = new ByteArrayOutputStream();
    auto* ps = new OutputStreamPrintStream(pbo);
    ps->print(1);
    ps->print(String(" x "));
    ps->println(2.5);
    ps->printf(String("%05d"), 42);
    ps->flush();
    JCHECK_EQ(pbo->toString(), String("1 x 2.5\n00042"));
    IOUtils::closeQuietly(ps);
    auto* core = new PrintStream(f->getPath());
    core->println(String("line"));
    IOUtils::closeQuietly(core);
    JCHECK_EQ(FileUtils::readFileToString(f), String("line\n"));
    FileUtils::deleteQuietly(dir);
}

JTEST(IOReadersWriters) {
    File* dir = tempDir();
    File* f = new File(dir, String("r.txt"));
    auto* bw = new BufferedWriter(new FileWriter(f, false));
    bw->write(String("=FortressErrorReport[Items:2]=\n"));
    bw->write(String("a\r\nb\rc"));
    bw->newLine();
    bw->write(u'中');
    bw->write(String("\xF0\x9F\x98\x80"));
    bw->close();
    bw->close();  // idempotent
    auto* br = new BufferedReader(new FileReader(f));
    JCHECK_EQ(br->readLine(), String("=FortressErrorReport[Items:2]="));
    JCHECK_EQ(br->readLine(), String("a"));
    JCHECK_EQ(br->readLine(), String("b"));
    JCHECK_EQ(br->readLine(), String("c"));
    JCHECK_EQ(br->read(), 0x4e2d);
    JCHECK_EQ(br->read(), 0xD83D);
    JCHECK_EQ(br->read(), 0xDE00);
    JCHECK(br->readLine().isNull());
    br->close();
    // append mode + OutputStreamWriter encoding
    auto* w = new FileWriter(f, true);
    w->write(String("tail"));
    w->close();
    JCHECK(FileUtils::readFileToString(f).endsWith(String("tail")));
    auto* osw = new OutputStreamWriter(new FileOutputStream(f), String("UTF-16LE"));
    JCHECK_EQ(osw->getEncoding(), String("UnicodeLittleUnmarked"));
    osw->write(String("hi\xC3\xA9"));
    osw->close();
    JCHECK_EQ(f->length(), 6);
    auto* isr = new InputStreamReader(new FileInputStream(f), String("UTF-16LE"));
    auto* cb = new Array<char16_t>(10);
    JCHECK_EQ(isr->read(cb, 0, 10), 3);
    JCHECK_EQ((*cb)[2], u'é');
    JCHECK_EQ(isr->read(), -1);
    isr->close();
    JCHECK(isr->getEncoding().isNull());
    JCHECK_THROWS(UnsupportedEncodingException, new InputStreamReader(new ByteArrayInputStream(new Array<int8_t>(0)), String("x-none")));
    // readLine edge cases
    auto* sr = new BufferedReader(new StringReader(String("\n\r\nx\r")));
    JCHECK_EQ(sr->readLine(), String(""));
    JCHECK_EQ(sr->readLine(), String(""));
    JCHECK_EQ(sr->readLine(), String("x"));
    JCHECK(sr->readLine().isNull());
    // UTF-8 decoding across buffer boundaries (InputStreamReader reads 8192-byte blocks)
    std::string s;
    for (int i = 0; i < 5000; i++) s += "\xE4\xB8\xAD";  // 15000 bytes
    auto* a = new Array<int8_t>(static_cast<int32_t>(s.size()));
    std::memcpy(a->data(), s.data(), s.size());
    JCHECK_EQ(IOUtils::toString(new InputStreamReader(new ByteArrayInputStream(a))), String(s));
    // PrintWriter
    auto* sw = new StringWriter();
    auto* pw = new PrintWriter(sw);
    pw->print(String("v="));
    pw->println(3);
    pw->printf(String("%s-%d"), String("x"), 7);
    pw->print(true);
    pw->print(u'c');
    pw->print(static_cast<Object*>(nullptr));
    pw->flush();
    JCHECK_EQ(sw->toString(), String("v=3\nx-7truecnull"));
    JCHECK(!pw->checkError());
    pw->close();
    pw->println(1);
    JCHECK(pw->checkError());
    // IOUtils
    JCHECK_EQ(IOUtils::toString(new ByteArrayInputStream(Array<int8_t>::of({104, 105}))), String("hi"));
    JCHECK_EQ(IOUtils::readLines(new StringReader(String("a\nb")))->size(), 2);
    IOUtils::closeQuietly(nullptr);
    IOUtils::closeQuietly(static_cast<Reader*>(nullptr));
    FileUtils::deleteQuietly(dir);
}

JTEST(IOZip) {
    File* dir = tempDir();
    File* log = new File(dir, String("server.log"));
    std::string text;
    for (int i = 0; i < 2000; i++) text += "line " + std::to_string(i) + " of the log file\n";
    writeText(log, String(text));
    // TruncateToZipFileAppender flow
    File* zipFile = new File(dir, String("server.log.2026.zip"));
    auto* zos = new ZipOutputStream(new FileOutputStream(zipFile));
    auto* entry = new ZipEntry(log->getName());
    entry->setMethod(ZipEntry::DEFLATED);
    entry->setCrc(FileUtils::checksumCRC32(log));
    zos->putNextEntry(entry);
    FileInputStream* fis = FileUtils::openInputStream(log);
    auto* buffer = new Array<int8_t>(1024);
    int32_t readed;
    while ((readed = fis->read(buffer)) != -1) zos->write(buffer, 0, readed);
    auto* e2 = new ZipEntry(String("stored.txt"));
    e2->setMethod(ZipEntry::STORED);
    e2->setSize(3);
    auto* crc = new CRC32();
    crc->update(Array<int8_t>::of({'a', 'b', 'c'}));
    e2->setCrc(crc->getValue());
    zos->putNextEntry(e2);
    zos->write(Array<int8_t>::of({'a', 'b', 'c'}), 0, 3);
    JCHECK_THROWS(ZipException, zos->putNextEntry(new ZipEntry(String("stored.txt"))));
    zos->close();
    fis->close();
    JCHECK_EQ(entry->getSize(), static_cast<int64_t>(text.size()));
    JCHECK(entry->getCompressedSize() > 0 && entry->getCompressedSize() < entry->getSize());
    // parse: local header, inflate, data descriptor, central directory
    Array<int8_t>* z = FileUtils::readFileToByteArray(zipFile);
    const uint8_t* p = reinterpret_cast<const uint8_t*>(z->data());
    auto u16 = [&](size_t o) { return static_cast<uint32_t>(p[o] | (p[o + 1] << 8)); };
    auto u32 = [&](size_t o) { return static_cast<uint32_t>(p[o] | (p[o + 1] << 8) | (p[o + 2] << 16) | (static_cast<uint32_t>(p[o + 3]) << 24)); };
    JCHECK_EQ(u32(0), 0x04034b50u);
    JCHECK_EQ(u16(6), 0x808u);  // data descriptor + UTF-8 names
    JCHECK_EQ(u16(8), 8u);
    size_t nameLen = u16(26);
    JCHECK_EQ(std::string(reinterpret_cast<const char*>(p + 30), nameLen), std::string("server.log"));
    size_t dataOff = 30 + nameLen + u16(28);
    std::string inflated(text.size() + 16, '\0');
    z_stream zs;
    std::memset(&zs, 0, sizeof zs);
    inflateInit2(&zs, -MAX_WBITS);
    zs.next_in = const_cast<Bytef*>(p + dataOff);
    zs.avail_in = static_cast<uInt>(z->length - static_cast<int32_t>(dataOff));
    zs.next_out = reinterpret_cast<Bytef*>(inflated.data());
    zs.avail_out = static_cast<uInt>(inflated.size());
    JCHECK_EQ(inflate(&zs, Z_FINISH), Z_STREAM_END);
    size_t consumed = zs.total_in;
    inflated.resize(zs.total_out);
    inflateEnd(&zs);
    JCHECK(inflated == text);
    size_t ext = dataOff + consumed;
    JCHECK_EQ(u32(ext), 0x08074b50u);
    JCHECK_EQ(static_cast<int64_t>(u32(ext + 4)), entry->getCrc());
    JCHECK_EQ(static_cast<int64_t>(u32(ext + 8)), entry->getCompressedSize());
    JCHECK_EQ(static_cast<size_t>(u32(ext + 12)), text.size());
    size_t end = static_cast<size_t>(z->length) - 22;
    JCHECK_EQ(u32(end), 0x06054b50u);
    JCHECK_EQ(u16(end + 10), 2u);
    size_t cen = u32(end + 16);
    JCHECK_EQ(u32(cen), 0x02014b50u);
    JCHECK_EQ(u32(cen + 16), static_cast<uint32_t>(entry->getCrc()));
    FileUtils::deleteQuietly(dir);
    // ZipEntry argument checks
    JCHECK_THROWS(IllegalArgumentException, (new ZipEntry(String("x")))->setMethod(3));
    JCHECK_THROWS(IllegalArgumentException, (new ZipEntry(String("x")))->setCrc(-1));
    auto* c2 = new CRC32();
    c2->update(Array<int8_t>::of({'1', '2', '3', '4', '5', '6', '7', '8', '9'}));
    JCHECK_EQ(c2->getValue(), INT64_C(0xCBF43926));
}
