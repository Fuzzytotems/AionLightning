// Tests for the java.nio buffers of <jlang/Nio.h>: a 4,000-step randomized transcript of
// ByteBuffer operations (slices, duplicates, wrap, byte order, bulk and typed access, compact,
// equals/compareTo/hashCode, read-only views, asCharBuffer) compared line by line with the
// output of jlang/tests/parity/BBGen.java on JDK 21; plus Charset conversions.
#include "jtest.h"

#include <jlang/Nio.h>

#include <cstdio>
#include <cstring>
#include <string>

using namespace jlang;

#include "test_nio_buffer_cases.inc"

namespace {

struct Lcg {
    uint64_t seed;
    int32_t next() {
        seed = seed * 6364136223846793005ULL + 1442695040888963407ULL;
        return static_cast<int32_t>(seed >> 33);
    }
    int32_t rnd(int32_t lo, int32_t hi) {
        int32_t span = static_cast<int32_t>(static_cast<uint32_t>(hi) - static_cast<uint32_t>(lo) + 1u);
        int64_t v = static_cast<int64_t>(static_cast<uint32_t>(next()) & 0x7fffffffu);
        int64_t m = span == -1 ? 0 : v % static_cast<int64_t>(span);
        return static_cast<int32_t>(static_cast<uint32_t>(lo) + static_cast<uint32_t>(static_cast<int32_t>(m)));
    }
};

std::string st(ByteBuffer* b) {
    return "p" + std::to_string(b->position()) + "l" + std::to_string(b->limit()) + "c" + std::to_string(b->capacity()) +
           (b->order() == ByteOrder::BIG_ENDIAN ? "B" : "L");
}

std::string arr(Array<int8_t>* a) {
    std::string s;
    char buf[8];
    for (int8_t x : *a) {
        std::snprintf(buf, sizeof buf, "%x.", static_cast<unsigned>(static_cast<uint8_t>(x)));
        s += buf;
    }
    return s;
}

std::string hex32(int32_t v) {
    char buf[16];
    std::snprintf(buf, sizeof buf, "%x", static_cast<unsigned>(v));
    return buf;
}

std::string hex64(int64_t v) {
    char buf[32];
    std::snprintf(buf, sizeof buf, "%llx", static_cast<unsigned long long>(v));
    return buf;
}

int32_t floatToIntBits(float f) {
    if (f != f) return 0x7fc00000;
    int32_t b;
    std::memcpy(&b, &f, 4);
    return b;
}

int64_t doubleToLongBits(double d) {
    if (d != d) return INT64_C(0x7ff8000000000000);
    int64_t b;
    std::memcpy(&b, &d, 8);
    return b;
}

std::string tf(bool b) { return b ? "true" : "false"; }

int signum(int32_t v) { return v > 0 ? 1 : (v < 0 ? -1 : 0); }

std::string step(Lcg& r, ByteBuffer** bufs, int nbufs, ByteBuffer*& b, int& biOut, int& opOut) {
    int bi = r.rnd(0, nbufs - 1);
    if (bufs[bi] == nullptr) bi = 0;
    b = bufs[bi];
    int op = r.rnd(0, 44);
    biOut = bi;
    opOut = op;
    std::string res;
    try {
        switch (op) {
            case 0: res = std::to_string(b->position()); break;
            case 1: { int n = r.rnd(-2, b->capacity() + 2); b->position(n); res = "pos" + std::to_string(n); break; }
            case 2: res = std::to_string(b->limit()); break;
            case 3: { int n = r.rnd(-2, b->capacity() + 2); b->limit(n); res = "lim" + std::to_string(n); break; }
            case 4: b->mark(); res = "mark"; break;
            case 5: b->reset(); res = "reset"; break;
            case 6: b->clear(); res = "clear"; break;
            case 7: b->flip(); res = "flip"; break;
            case 8: b->rewind(); res = "rewind"; break;
            case 9: res = "rem" + std::to_string(b->remaining()) + tf(b->hasRemaining()); break;
            case 10: b->compact(); res = "compact"; break;
            case 11: res = "g" + std::to_string(b->get()); break;
            case 12: { int i = r.rnd(-1, b->capacity()); res = "g" + std::to_string(i) + "=" + std::to_string(b->get(i)); break; }
            case 13: { int8_t v = static_cast<int8_t>(r.next()); b->put(v); res = "p" + std::to_string(v); break; }
            case 14: { int i = r.rnd(-1, b->capacity()); int8_t v = static_cast<int8_t>(r.next()); b->put(i, v); res = "p" + std::to_string(i) + "=" + std::to_string(v); break; }
            case 15: res = "s" + std::to_string(b->getShort()); break;
            case 16: { int i = r.rnd(-1, b->capacity()); res = "s" + std::to_string(i) + "=" + std::to_string(b->getShort(i)); break; }
            case 17: { int16_t v = static_cast<int16_t>(r.next()); b->putShort(v); res = "ps" + std::to_string(v); break; }
            case 18: { int i = r.rnd(-1, b->capacity()); int16_t v = static_cast<int16_t>(r.next()); b->putShort(i, v); res = "ps" + std::to_string(i) + "=" + std::to_string(v); break; }
            case 19: res = "i" + std::to_string(b->getInt()); break;
            case 20: { int i = r.rnd(-1, b->capacity()); res = "i" + std::to_string(i) + "=" + std::to_string(b->getInt(i)); break; }
            case 21: { int32_t v = r.next(); b->putInt(v); res = "pi" + std::to_string(v); break; }
            case 22: { int i = r.rnd(-1, b->capacity()); int32_t v = r.next(); b->putInt(i, v); res = "pi" + std::to_string(i) + "=" + std::to_string(v); break; }
            case 23: res = "L" + std::to_string(b->getLong()); break;
            case 24: { int64_t hi = static_cast<int64_t>(r.next()); int64_t lo = static_cast<int64_t>(r.next()); int64_t v = static_cast<int64_t>(static_cast<uint64_t>(hi) << 32) ^ lo; b->putLong(v); res = "pL" + std::to_string(v); break; }
            case 25: res = "c" + std::to_string(static_cast<int>(b->getChar())); break;
            case 26: { char16_t v = static_cast<char16_t>(r.next()); b->putChar(v); res = "pc" + std::to_string(static_cast<int>(v)); break; }
            case 27: res = "f" + hex32(floatToIntBits(b->getFloat())); break;
            case 28: { float v = static_cast<float>(r.next()) / 7.0f; b->putFloat(v); res = "pf"; break; }
            case 29: res = "d" + hex64(doubleToLongBits(b->getDouble())); break;
            case 30: { double v = static_cast<double>(r.next()) / 3.0; b->putDouble(v); res = "pd"; break; }
            case 31: b->order(ByteOrder::LITTLE_ENDIAN); res = "LE"; break;
            case 32: b->order(ByteOrder::BIG_ENDIAN); res = "BE"; break;
            case 33: { int t = r.rnd(1, nbufs - 1); bufs[t] = b->slice(); res = "slice>" + std::to_string(t) + ":" + st(bufs[t]) + " ao" + std::to_string(bufs[t]->arrayOffset()); break; }
            case 34: { int t = r.rnd(1, nbufs - 1); bufs[t] = b->duplicate(); res = "dup>" + std::to_string(t) + ":" + st(bufs[t]); break; }
            case 35: {
                int len = r.rnd(0, 6);
                auto* d = new Array<int8_t>(len + 2);
                int off = r.rnd(-1, 3);
                int l = r.rnd(-1, len);
                b->get(d, off, l);
                res = "ga" + std::to_string(off) + "," + std::to_string(l) + ":" + arr(d);
                break;
            }
            case 36: {
                int len = r.rnd(0, 6);
                auto* s = new Array<int8_t>(len);
                for (int i = 0; i < len; i++) (*s)[i] = static_cast<int8_t>(r.next());
                int off = r.rnd(-1, 2);
                int l = r.rnd(-1, len);
                b->put(s, off, l);
                res = "pa" + std::to_string(off) + "," + std::to_string(l);
                break;
            }
            case 37: { int t = r.rnd(0, nbufs - 1); ByteBuffer* o = bufs[t] == nullptr ? b : bufs[t]; b->put(o); res = "pb" + std::to_string(t) + ":" + st(o); break; }
            case 38: res = "arr" + arr(b->array()) + " ao" + std::to_string(b->arrayOffset()); break;
            case 39: res = std::string(b->toString()); break;
            case 40: {
                int t = r.rnd(0, nbufs - 1);
                ByteBuffer* o = bufs[t] == nullptr ? bufs[0] : bufs[t];
                res = "eq" + tf(b->equals(o)) + " cmp" + std::to_string(signum(b->compareTo(o))) + " h" + std::to_string(b->hashCode());
                break;
            }
            case 41: {
                int t = r.rnd(1, nbufs - 1);
                auto* a = new Array<int8_t>(r.rnd(0, 12));
                for (int i = 0; i < a->length; i++) (*a)[i] = static_cast<int8_t>(i);
                int off = r.rnd(-1, a->length);
                int l = r.rnd(-1, a->length);
                bufs[t] = ByteBuffer::wrap(a, off, l);
                res = "wrap>" + std::to_string(t) + ":" + st(bufs[t]);
                break;
            }
            case 42: {
                ByteBuffer* ro = b->asReadOnlyBuffer();
                res = "ro" + st(ro) + tf(ro->isReadOnly()) + tf(ro->hasArray());
                ro->put(static_cast<int8_t>(1));
                break;
            }
            case 43: {
                CharBuffer* cb = b->asCharBuffer();
                std::u16string full = u"ABé中";
                int k = r.rnd(0, 4);
                String s = String::fromUtf16(full.substr(0, static_cast<size_t>(k)));
                res = "cb" + std::to_string(cb->position()) + "," + std::to_string(cb->limit()) + "," + std::to_string(cb->capacity());
                cb->put(s);
                res += "ok" + std::to_string(cb->position());
                break;
            }
            default: {
                int t = r.rnd(1, nbufs - 1);
                ByteBuffer* o = ByteBuffer::allocate(r.rnd(0, 20));
                bufs[t] = o;
                res = "alloc>" + std::to_string(t) + ":" + st(o);
                break;
            }
        }
    } catch (ReadOnlyBufferException&) {
        res = "EX:ReadOnlyBufferException";
    } catch (InvalidMarkException&) {
        res = "EX:InvalidMarkException";
    } catch (BufferUnderflowException&) {
        res = "EX:BufferUnderflowException";
    } catch (BufferOverflowException&) {
        res = "EX:BufferOverflowException";
    } catch (IllegalArgumentException&) {
        res = "EX:IllegalArgumentException";
    } catch (IndexOutOfBoundsException&) {
        res = "EX:IndexOutOfBoundsException";
    } catch (Throwable& t) {
        res = "EX:other:" + std::string(t.toString());
    }
    return res;
}

}  // namespace

JTEST(NioByteBufferParity) {
    Lcg r{777};
    ByteBuffer* bufs[6] = {ByteBuffer::allocate(24), nullptr, nullptr, nullptr, nullptr, nullptr};
    const int n = static_cast<int>(sizeof kBBExpected / sizeof kBBExpected[0]);
    JCHECK_EQ(n, 4000);
    int mismatches = 0;
    for (int s = 0; s < n; s++) {
        ByteBuffer* b = nullptr;
        int bi = 0, op = 0;
        std::string res = step(r, bufs, 6, b, bi, op);
        std::string line = std::to_string(s) + " " + std::to_string(bi) + " " + std::to_string(op) + " " + res + " " + st(b);
        if (line != kBBExpected[s]) {
            if (mismatches++ < 5) jtest::fail(__FILE__, __LINE__, "step " + std::to_string(s) + ": got [" + line + "] want [" + kBBExpected[s] + "]");
        }
    }
    JCHECK_EQ(mismatches, 0);
}

JTEST(NioByteBufferBasics) {
    ByteBuffer* b = ByteBuffer::allocate(16);
    JCHECK(b->order() == ByteOrder::BIG_ENDIAN);
    b->order(ByteOrder::LITTLE_ENDIAN)->putInt(0x01020304);
    JCHECK_EQ(b->get(0), 4);
    JCHECK_EQ(b->position(), 4);
    b->putShort(static_cast<int16_t>(-2));
    b->flip();
    JCHECK_EQ(b->remaining(), 6);
    JCHECK_EQ(b->getInt(), 0x01020304);
    JCHECK_EQ(b->getShort(), -2);
    JCHECK_THROWS(BufferUnderflowException, b->get());
    b->clear();
    // slice shares storage and resets the order
    b->position(2);
    ByteBuffer* s = b->slice();
    JCHECK(s->order() == ByteOrder::BIG_ENDIAN);
    JCHECK_EQ(s->capacity(), 14);
    JCHECK_EQ(s->arrayOffset(), 2);
    JCHECK(s->array() == b->array());
    s->put(0, static_cast<int8_t>(99));
    JCHECK_EQ(b->get(2), 99);
    // Dispatcher idiom: (ByteBuffer) buf.slice().limit(sz) with a bad size
    JCHECK_THROWS(IllegalArgumentException, s->limit(-1));
    JCHECK_THROWS(IllegalArgumentException, s->limit(15));
    JCHECK_THROWS(IllegalArgumentException, b->position(17));
    JCHECK_EQ(b->toString(), String("java.nio.HeapByteBuffer[pos=2 lim=16 cap=16]"));
    JCHECK_EQ(ByteBuffer::allocateDirect(8)->toString(), String("java.nio.DirectByteBuffer[pos=0 lim=8 cap=8]"));
    JCHECK(ByteBuffer::allocateDirect(8)->isDirect());
    JCHECK_EQ(ByteOrder::LITTLE_ENDIAN.toString(), String("LITTLE_ENDIAN"));
    JCHECK(ByteOrder::nativeOrder() == ByteOrder::LITTLE_ENDIAN);
    // readS/writeS style UTF-16LE chars
    ByteBuffer* w = ByteBuffer::allocate(8)->order(ByteOrder::LITTLE_ENDIAN);
    w->putChar(u'A')->putChar(u'中');
    JCHECK_EQ(w->get(0), 'A');
    JCHECK_EQ(w->get(1), 0);
    JCHECK_EQ(static_cast<uint8_t>(w->get(2)), 0x2d);
    JCHECK_EQ(w->getChar(2), u'中');
    // compact semantics used by Dispatcher.read
    ByteBuffer* c = ByteBuffer::allocate(8);
    c->put(static_cast<int8_t>(1))->put(static_cast<int8_t>(2))->put(static_cast<int8_t>(3));
    c->flip();
    c->get();
    c->compact();
    JCHECK_EQ(c->position(), 2);
    JCHECK_EQ(c->limit(), 8);
    JCHECK_EQ(c->get(0), 2);
    // RDCConnection: data.asCharBuffer().put(result) keeps the byte buffer's position
    ByteBuffer* rdc = ByteBuffer::allocate(16)->order(ByteOrder::LITTLE_ENDIAN);
    rdc->asCharBuffer()->put(String("hi"));
    JCHECK_EQ(rdc->position(), 0);
    JCHECK_EQ(rdc->get(0), 'h');
    JCHECK_EQ(rdc->get(2), 'i');
    JCHECK_THROWS(BufferOverflowException, ByteBuffer::allocate(3)->asCharBuffer()->put(String("ab")));
}

JTEST(NioCharsets) {
    Charset* le = Charset::forName(String("UTF-16le"));
    JCHECK_EQ(le->name(), String("UTF-16LE"));
    JCHECK(Charset::forName(String("utf8")) == Charset::forName(String("UTF-8")));
    JCHECK(Charset::forName(String("latin1")) == StandardCharsets::ISO_8859_1());
    JCHECK_THROWS(UnsupportedCharsetException, Charset::forName(String("x-no-such")));
    JCHECK_THROWS(IllegalCharsetNameException, Charset::forName(String("bad name")));
    Array<int8_t>* b = le->encodeToArray(String("Aé"));
    JCHECK_EQ(b->length, 4);
    JCHECK_EQ((*b)[0], 'A');
    JCHECK_EQ(static_cast<uint8_t>((*b)[2]), 0xe9);
    JCHECK_EQ(le->decodeToString(b), String("Aé"));
    JCHECK_EQ(String("x").getBytes(le)->length, 2);
    // malformed UTF-8 -> U+FFFD with the JDK's granularity
    const char bad[] = "a\xC3\x28" "b\xE4\xB8" "c\xF0\x9F\x98" "\xFF";
    String d = StandardCharsets::UTF_8()->decodeToString(reinterpret_cast<const int8_t*>(bad), static_cast<int32_t>(sizeof bad - 1));
    JCHECK_EQ(d, String("a\xEF\xBF\xBD(b\xEF\xBF\xBD" "c\xEF\xBF\xBD\xEF\xBF\xBD"));
    // unmappable -> '?'
    Array<int8_t>* iso = StandardCharsets::ISO_8859_1()->encodeToArray(String("中\xC3\xA9"));
    JCHECK_EQ(iso->length, 2);
    JCHECK_EQ((*iso)[0], '?');
    JCHECK_EQ(static_cast<uint8_t>((*iso)[1]), 0xe9);
    // exact JDK 21 replacement granularity (reference output of new String(bytes, UTF_8),
    // InputStreamReader, String.getBytes)
    {
        const uint8_t in8[] = {'a', 0xC3, 0x28, 'b', 0xE4, 0xB8, 'c', 0xF0, 0x9F, 0x98, 0xFF, 0xE0, 0x80, 0x80,
                               0xED, 0xA0, 0x80, 0xF4, 0x90, 0x80, 0x80, 0xE4, 0xB8};
        std::u16string want = u"a\uFFFD(b\uFFFDc";
        want.append(11, u'\uFFFD');
        JCHECK(detail::decodeBytes(Charset::UTF_8_KIND, in8, sizeof in8) == want);
        // streaming decoder fed one byte at a time gives the same result
        detail::CharDecoderState st;
        std::u16string acc;
        for (uint8_t b : in8) st.decode(&b, 1, acc, false);
        st.decode(nullptr, 0, acc, true);
        JCHECK(acc == want);
        const uint8_t in16[] = {0x41, 0x00, 0x3D, 0xD8, 0x42, 0x00, 0x00, 0xDC, 0x43};
        JCHECK(detail::decodeBytes(Charset::UTF_16LE_KIND, in16, sizeof in16) == std::u16string(u"A\uFFFD\uFFFD\uFFFD"));
        const char16_t iso[] = {0x4e2d, 0xe9, 0xD83D, 0xDE00, 'x', 0xD800};
        JCHECK(detail::encodeChars(Charset::ISO_8859_1_KIND, iso, 6) == std::string("?\xE9?x?"));
        const char16_t lone[] = {0xD800, 'x', 0xDC00};
        JCHECK(detail::encodeChars(Charset::UTF_8_KIND, lone, 3) == std::string("?x?"));
        const char16_t hi[] = {'a', 0xD800};
        JCHECK(detail::encodeChars(Charset::UTF_16LE_KIND, hi, 2) == std::string("a\0\xFD\xFF", 4));
    }
    ByteBuffer* e = StandardCharsets::UTF_8()->encode(String("hey"));
    JCHECK_EQ(e->remaining(), 3);
    JCHECK_EQ(StandardCharsets::UTF_8()->decode(e)->toString(), String("hey"));
    JCHECK_EQ(e->remaining(), 0);
}
