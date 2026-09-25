// java.math.BigInteger over OpenSSL BIGNUM (temporary BIGNUMs per operation; the value itself
// is kept in GC memory as sign + big-endian magnitude, so nothing leaks when it is collected).
#include <jlang/Crypto.h>

#include <openssl/bn.h>
#include <openssl/err.h>
#include <openssl/rand.h>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>

namespace jlang {

namespace {

struct Bn {
    BIGNUM* p;
    Bn() : p(BN_new()) {
        if (!p) throw OutOfMemoryError("BN_new");
    }
    explicit Bn(BIGNUM* q) : p(q) {}
    ~Bn() { BN_clear_free(p); }
    Bn(const Bn&) = delete;
    Bn& operator=(const Bn&) = delete;
    operator BIGNUM*() const { return p; }
};

struct Ctx {
    BN_CTX* p;
    Ctx() : p(BN_CTX_new()) {
        if (!p) throw OutOfMemoryError("BN_CTX_new");
    }
    ~Ctx() { BN_CTX_free(p); }
    operator BN_CTX*() const { return p; }
};

void check(int ok) {
    if (!ok) throw ArithmeticException("BigInteger: OpenSSL BIGNUM operation failed");
}

void toBn(BigInteger* x, BIGNUM* out) {
    const auto& m = x->magnitudeBytes();
    check(BN_bin2bn(m.data(), static_cast<int>(m.size()), out) != nullptr);
    BN_set_negative(out, x->signum() < 0 ? 1 : 0);
}

BigInteger* fromBn(const BIGNUM* b) {
    int n = BN_num_bytes(b);
    std::vector<uint8_t> mag(static_cast<size_t>(n));
    if (n > 0) BN_bn2bin(b, mag.data());
    int32_t sig = BN_is_zero(b) ? 0 : (BN_is_negative(b) ? -1 : 1);
    return BigInteger::fromMagnitude(sig, std::move(mag));
}

// Two's complement big-endian representation of x in exactly `len` bytes (len large enough).
std::vector<uint8_t> twos(BigInteger* x, size_t len) {
    const auto& m = x->magnitudeBytes();
    std::vector<uint8_t> r(len, 0);
    std::copy(m.begin(), m.end(), r.begin() + static_cast<std::ptrdiff_t>(len - m.size()));
    if (x->signum() < 0) {
        // negate: invert and add one
        int carry = 1;
        for (size_t i = len; i-- > 0;) {
            int v = static_cast<uint8_t>(~r[i]) + carry;
            r[i] = static_cast<uint8_t>(v);
            carry = v >> 8;
        }
    }
    return r;
}

// Magnitude minus one (x != 0), as big-endian bytes (may have a leading zero).
std::vector<uint8_t> magMinusOne(const std::vector<uint8_t>& m) {
    std::vector<uint8_t> r(m);
    for (size_t i = r.size(); i-- > 0;) {
        if (r[i] != 0) {
            r[i]--;
            break;
        }
        r[i] = 0xFF;
    }
    return r;
}

int bitLengthOf(const std::vector<uint8_t>& m) {
    size_t i = 0;
    while (i < m.size() && m[i] == 0) i++;
    if (i == m.size()) return 0;
    int top = 0;
    for (uint8_t b = m[i]; b; b >>= 1) top++;
    return static_cast<int>((m.size() - i - 1) * 8) + top;
}

int popCount(const std::vector<uint8_t>& m) {
    int c = 0;
    for (uint8_t b : m) c += __builtin_popcount(b);
    return c;
}

const int kDigitsPerInt[37] = {0,  0,  30, 19, 15, 13, 11, 11, 10, 9, 9, 8, 8, 8, 8, 7, 7, 7, 7,
                               7,  7,  7,  6,  6,  6,  6,  6,  6,  6, 6, 6, 6, 6, 6, 6, 6, 5};

int digitValue(unsigned char c, int radix) {
    int d;
    if (c >= '0' && c <= '9') d = c - '0';
    else if (c >= 'a' && c <= 'z') d = c - 'a' + 10;
    else if (c >= 'A' && c <= 'Z') d = c - 'A' + 10;
    else return -1;
    return d < radix ? d : -1;
}

[[noreturn]] void throwForInput(const std::string& s) {
    throw NumberFormatException(String("For input string: \"") + String(s) + String("\""));
}

Array<int8_t>* toJavaBytes(const std::vector<uint8_t>& v) {
    auto* a = new Array<int8_t>(static_cast<int32_t>(v.size()));
    if (!v.empty()) std::memcpy(a->data(), v.data(), v.size());
    return a;
}

}  // namespace

// ------------------------------------------------------------------ construction

BigInteger* BigInteger::fromMagnitude(int32_t signum, std::vector<uint8_t> mag) {
    auto* r = new BigInteger();
    r->signum_ = signum;
    r->mag_ = std::move(mag);
    r->normalize();
    return r;
}

void BigInteger::normalize() {
    size_t i = 0;
    while (i < mag_.size() && mag_[i] == 0) i++;
    if (i > 0) mag_.erase(mag_.begin(), mag_.begin() + static_cast<std::ptrdiff_t>(i));
    if (mag_.empty()) signum_ = 0;
}

BigInteger::BigInteger(const String& val) { initFromString(val, 10); }

BigInteger::BigInteger(const String& val, int32_t radix) { initFromString(val, radix); }

void BigInteger::initFromString(const String& val, int32_t radix) {
    const std::string& s = val;
    size_t len = s.size();
    size_t cursor = 0;
    if (radix < 2 || radix > 36) throw NumberFormatException("Radix out of range");
    if (len == 0) throw NumberFormatException("Zero length BigInteger");
    int sign = 1;
    size_t index1 = s.rfind('-');
    size_t index2 = s.rfind('+');
    if (index1 != std::string::npos) {
        if (index1 != 0 || index2 != std::string::npos)
            throw NumberFormatException("Illegal embedded sign character");
        sign = -1;
        cursor = 1;
    } else if (index2 != std::string::npos) {
        if (index2 != 0) throw NumberFormatException("Illegal embedded sign character");
        cursor = 1;
    }
    if (cursor == len) throw NumberFormatException("Zero length BigInteger");
    while (cursor < len && digitValue(static_cast<unsigned char>(s[cursor]), radix) == 0) cursor++;
    if (cursor == len) {
        signum_ = 0;
        mag_.clear();
        return;
    }
    size_t numDigits = len - cursor;
    size_t per = static_cast<size_t>(kDigitsPerInt[radix]);
    size_t firstGroupLen = numDigits % per;
    if (firstGroupLen == 0) firstGroupLen = per;
    // Validate group by group (Java reports the first bad group).
    Bn acc;
    BN_zero(acc);
    size_t groupLen = firstGroupLen;
    while (cursor < len) {
        std::string group = s.substr(cursor, groupLen);
        cursor += groupLen;
        groupLen = per;
        BN_ULONG gv = 0;
        BN_ULONG mul = 1;
        for (unsigned char c : group) {
            int d = digitValue(c, radix);
            if (d < 0) throwForInput(group);
            gv = gv * static_cast<BN_ULONG>(radix) + static_cast<BN_ULONG>(d);
            mul *= static_cast<BN_ULONG>(radix);
        }
        check(BN_mul_word(acc, mul));
        check(BN_add_word(acc, gv));
    }
    int n = BN_num_bytes(acc);
    mag_.assign(static_cast<size_t>(n), 0);
    BN_bn2bin(acc, mag_.data());
    signum_ = sign;
    normalize();
}

BigInteger::BigInteger(Array<int8_t>* val) {
    if (val == nullptr) throw NullPointerException();
    if (val->length == 0) throw NumberFormatException("Zero length BigInteger");
    initFromTwosComplement(reinterpret_cast<const uint8_t*>(val->data()), static_cast<size_t>(val->length));
}

void BigInteger::initFromTwosComplement(const uint8_t* p, size_t n) {
    if (n == 0 || (p[0] & 0x80) == 0) {
        signum_ = 1;
        mag_.assign(p, p + n);
        normalize();
        return;
    }
    // negative: magnitude = ~x + 1
    std::vector<uint8_t> m(p, p + n);
    int carry = 1;
    for (size_t i = n; i-- > 0;) {
        int v = static_cast<uint8_t>(~m[i]) + carry;
        m[i] = static_cast<uint8_t>(v);
        carry = v >> 8;
    }
    if (carry) m.insert(m.begin(), 1);  // unreachable for a negative input, kept for safety
    signum_ = -1;
    mag_ = std::move(m);
    normalize();
}

BigInteger::BigInteger(int32_t signum, Array<int8_t>* magnitude) {
    if (magnitude == nullptr) throw NullPointerException();
    if (signum < -1 || signum > 1) throw NumberFormatException("Invalid signum value");
    const uint8_t* p = reinterpret_cast<const uint8_t*>(magnitude->data());
    mag_.assign(p, p + magnitude->length);
    signum_ = 1;
    normalize();
    if (mag_.empty()) {
        signum_ = 0;
    } else {
        if (signum == 0) throw NumberFormatException("signum-magnitude mismatch");
        signum_ = signum;
    }
}

BigInteger::BigInteger(int32_t numBits, Random* rnd) {
    if (numBits < 0) throw IllegalArgumentException("numBits must be non-negative");
    int32_t numBytes = static_cast<int32_t>((static_cast<int64_t>(numBits) + 7) / 8);
    auto* bytes = new Array<int8_t>(numBytes);
    if (numBytes > 0) {
        rnd->nextBytes(bytes);
        int excessBits = 8 * numBytes - numBits;
        (*bytes)[0] = static_cast<int8_t>((*bytes)[0] & ((1 << (8 - excessBits)) - 1));
    }
    const uint8_t* p = reinterpret_cast<const uint8_t*>(bytes->data());
    mag_.assign(p, p + numBytes);
    signum_ = 1;
    normalize();
}

BigInteger::BigInteger(int32_t bitLength, int32_t certainty, Random* rnd) {
    (void)certainty;
    BigInteger* p = probablePrime(bitLength, rnd);
    signum_ = p->signum_;
    mag_ = p->mag_;
}

BigInteger* BigInteger::valueOf(int64_t val) {
    auto* r = new BigInteger();
    if (val == 0) return r;
    uint64_t u = val < 0 ? (~static_cast<uint64_t>(val) + 1) : static_cast<uint64_t>(val);
    r->signum_ = val < 0 ? -1 : 1;
    r->mag_.resize(8);
    for (int i = 7; i >= 0; i--) {
        r->mag_[static_cast<size_t>(i)] = static_cast<uint8_t>(u);
        u >>= 8;
    }
    r->normalize();
    return r;
}

BigInteger* BigInteger::probablePrime(int32_t bitLength, Random* rnd) {
    (void)rnd;
    if (bitLength < 2) throw ArithmeticException("bitLength < 2");
    Bn p;
    check(BN_generate_prime_ex(p, bitLength, 0, nullptr, nullptr, nullptr));
    return fromBn(p);
}

// ------------------------------------------------------------------ arithmetic

BigInteger* BigInteger::add(BigInteger* val) {
    if (val->signum_ == 0) return this;
    if (signum_ == 0) return val;
    Bn a, b, r;
    toBn(this, a);
    toBn(val, b);
    check(BN_add(r, a, b));
    return fromBn(r);
}

BigInteger* BigInteger::subtract(BigInteger* val) {
    if (val->signum_ == 0) return this;
    Bn a, b, r;
    toBn(this, a);
    toBn(val, b);
    check(BN_sub(r, a, b));
    return fromBn(r);
}

BigInteger* BigInteger::multiply(BigInteger* val) {
    if (val->signum_ == 0 || signum_ == 0) return ZERO;
    Bn a, b, r;
    Ctx ctx;
    toBn(this, a);
    toBn(val, b);
    check(BN_mul(r, a, b, ctx));
    return fromBn(r);
}

BigInteger* BigInteger::divide(BigInteger* val) {
    if (val->signum_ == 0) throw ArithmeticException("BigInteger divide by zero");
    Bn a, b, q, rem;
    Ctx ctx;
    toBn(this, a);
    toBn(val, b);
    check(BN_div(q, rem, a, b, ctx));
    return fromBn(q);
}

BigInteger* BigInteger::remainder(BigInteger* val) {
    if (val->signum_ == 0) throw ArithmeticException("BigInteger divide by zero");
    Bn a, b, q, rem;
    Ctx ctx;
    toBn(this, a);
    toBn(val, b);
    check(BN_div(q, rem, a, b, ctx));
    return fromBn(rem);
}

Array<BigInteger*>* BigInteger::divideAndRemainder(BigInteger* val) {
    if (val->signum_ == 0) throw ArithmeticException("BigInteger divide by zero");
    Bn a, b, q, rem;
    Ctx ctx;
    toBn(this, a);
    toBn(val, b);
    check(BN_div(q, rem, a, b, ctx));
    auto* r = new Array<BigInteger*>(2);
    (*r)[0] = fromBn(q);
    (*r)[1] = fromBn(rem);
    return r;
}

BigInteger* BigInteger::mod(BigInteger* m) {
    if (m->signum_ <= 0) throw ArithmeticException("BigInteger: modulus not positive");
    Bn a, b, r;
    Ctx ctx;
    toBn(this, a);
    toBn(m, b);
    check(BN_nnmod(r, a, b, ctx));
    return fromBn(r);
}

BigInteger* BigInteger::modInverse(BigInteger* m) {
    if (m->signum_ != 1) throw ArithmeticException("BigInteger: modulus not positive");
    if (m->equals(ONE)) return ZERO;
    Bn a, b, r;
    Ctx ctx;
    toBn(this, a);
    toBn(m, b);
    check(BN_nnmod(a, a, b, ctx));
    if (BN_mod_inverse(r, a, b, ctx) == nullptr) {
        ERR_clear_error();
        throw ArithmeticException("BigInteger not invertible.");
    }
    return fromBn(r);
}

BigInteger* BigInteger::modPow(BigInteger* exponent, BigInteger* m) {
    if (m->signum_ <= 0) throw ArithmeticException("BigInteger: modulus not positive");
    if (exponent->signum_ == 0) return m->equals(ONE) ? ZERO : ONE;
    if (m->equals(ONE)) return ZERO;
    BigInteger* base = this;
    BigInteger* exp = exponent;
    if (exp->signum_ < 0) {
        base = base->modInverse(m);
        exp = exp->negate();
    }
    Bn a, e, n, r;
    Ctx ctx;
    toBn(base, a);
    toBn(exp, e);
    toBn(m, n);
    check(BN_nnmod(a, a, n, ctx));
    check(BN_mod_exp(r, a, e, n, ctx));
    return fromBn(r);
}

BigInteger* BigInteger::pow(int32_t exponent) {
    if (exponent < 0) throw ArithmeticException("Negative exponent");
    if (exponent == 0) return ONE;
    if (signum_ == 0) return this;
    Bn a, e, r;
    Ctx ctx;
    toBn(this, a);
    check(BN_set_word(e, static_cast<BN_ULONG>(exponent)));
    check(BN_exp(r, a, e, ctx));
    return fromBn(r);
}

BigInteger* BigInteger::gcd(BigInteger* val) {
    if (val->signum_ == 0) return abs();
    if (signum_ == 0) return val->abs();
    Bn a, b, r;
    Ctx ctx;
    toBn(this, a);
    toBn(val, b);
    check(BN_gcd(r, a, b, ctx));
    return fromBn(r);
}

BigInteger* BigInteger::abs() { return signum_ >= 0 ? this : negate(); }

BigInteger* BigInteger::negate() {
    if (signum_ == 0) return this;
    return fromMagnitude(-signum_, mag_);
}

int32_t BigInteger::signum() { return signum_; }

// ------------------------------------------------------------------ bits

BigInteger* BigInteger::shiftLeft(int32_t n) {
    if (signum_ == 0) return this;
    if (n == 0) return this;
    if (n < 0) {
        if (n == INT32_MIN) throw ArithmeticException("Shift distance of Integer.MIN_VALUE not supported.");
        return shiftRight(-n);
    }
    Bn a, r;
    toBn(this, a);
    BN_set_negative(a, 0);
    check(BN_lshift(r, a, n));
    BN_set_negative(r, signum_ < 0);
    return fromBn(r);
}

BigInteger* BigInteger::shiftRight(int32_t n) {
    if (signum_ == 0 || n == 0) return this;
    if (n < 0) {
        if (n == INT32_MIN) throw ArithmeticException("Shift distance of Integer.MIN_VALUE not supported.");
        return shiftLeft(-n);
    }
    Bn a, r;
    toBn(this, a);
    BN_set_negative(a, 0);
    if (signum_ > 0) {
        check(BN_rshift(r, a, n));
        return fromBn(r);
    }
    // floor for negatives: -(((|x| - 1) >> n) + 1)
    check(BN_sub_word(a, 1));
    check(BN_rshift(r, a, n));
    check(BN_add_word(r, 1));
    BN_set_negative(r, 1);
    return fromBn(r);
}

namespace {
template<class Op>
BigInteger* bitwise(BigInteger* x, BigInteger* y, Op op) {
    size_t len = std::max(x->magnitudeBytes().size(), y->magnitudeBytes().size()) + 1;
    std::vector<uint8_t> a = twos(x, len), b = twos(y, len);
    std::vector<uint8_t> r(len);
    for (size_t i = 0; i < len; i++) r[i] = static_cast<uint8_t>(op(a[i], b[i]));
    auto* arr = toJavaBytes(r);
    return new BigInteger(arr);
}
}  // namespace

BigInteger* BigInteger::and_(BigInteger* val) {
    return bitwise(this, val, [](uint8_t a, uint8_t b) { return a & b; });
}
BigInteger* BigInteger::or_(BigInteger* val) {
    return bitwise(this, val, [](uint8_t a, uint8_t b) { return a | b; });
}
BigInteger* BigInteger::xor_(BigInteger* val) {
    return bitwise(this, val, [](uint8_t a, uint8_t b) { return a ^ b; });
}
BigInteger* BigInteger::andNot(BigInteger* val) {
    return bitwise(this, val, [](uint8_t a, uint8_t b) { return a & static_cast<uint8_t>(~b); });
}
BigInteger* BigInteger::not_() {
    // ~x == -x - 1
    return negate()->subtract(ONE);
}

bool BigInteger::testBit(int32_t n) {
    if (n < 0) throw ArithmeticException("Negative bit address");
    size_t byteIdx = static_cast<size_t>(n) / 8;
    int bit = n % 8;
    if (signum_ >= 0) {
        if (byteIdx >= mag_.size()) return false;
        return (mag_[mag_.size() - 1 - byteIdx] >> bit) & 1;
    }
    std::vector<uint8_t> m1 = magMinusOne(mag_);
    if (byteIdx >= m1.size()) return true;
    return !((m1[m1.size() - 1 - byteIdx] >> bit) & 1);
}

namespace {
BigInteger* withBit(BigInteger* x, int32_t n, int mode) {  // 0 set, 1 clear, 2 flip
    if (n < 0) throw ArithmeticException("Negative bit address");
    size_t len = std::max(x->magnitudeBytes().size(), static_cast<size_t>(n) / 8 + 1) + 1;
    std::vector<uint8_t> t = twos(x, len);
    size_t idx = len - 1 - static_cast<size_t>(n) / 8;
    uint8_t mask = static_cast<uint8_t>(1u << (n % 8));
    if (mode == 0) t[idx] |= mask;
    else if (mode == 1) t[idx] &= static_cast<uint8_t>(~mask);
    else t[idx] ^= mask;
    return new BigInteger(toJavaBytes(t));
}
}  // namespace

BigInteger* BigInteger::setBit(int32_t n) { return withBit(this, n, 0); }
BigInteger* BigInteger::clearBit(int32_t n) { return withBit(this, n, 1); }
BigInteger* BigInteger::flipBit(int32_t n) { return withBit(this, n, 2); }

int32_t BigInteger::getLowestSetBit() {
    if (signum_ == 0) return -1;
    // Same for x and -x.
    int32_t pos = 0;
    for (size_t i = mag_.size(); i-- > 0;) {
        if (mag_[i] != 0) return pos + __builtin_ctz(mag_[i]);
        pos += 8;
    }
    return -1;
}

int32_t BigInteger::bitLength() {
    if (signum_ >= 0) return bitLengthOf(mag_);
    return bitLengthOf(magMinusOne(mag_));
}

int32_t BigInteger::bitCount() {
    if (signum_ >= 0) return popCount(mag_);
    return popCount(magMinusOne(mag_));
}

bool BigInteger::isProbablePrime(int32_t certainty) {
    if (certainty <= 0) return true;
    BigInteger* w = abs();
    if (w->equals(TWO)) return true;
    if (!w->testBit(0) || w->equals(ONE) || w->signum_ == 0) return false;
    Bn a;
    Ctx ctx;
    toBn(w, a);
    int r = BN_check_prime(a, ctx, nullptr);
    if (r < 0) throw ArithmeticException("BigInteger: primality test failed");
    return r == 1;
}

BigInteger* BigInteger::nextProbablePrime() {
    if (signum_ < 0) throw ArithmeticException("start < 0: " + toString());
    if (signum_ == 0 || equals(ONE)) return TWO;
    BigInteger* c = add(ONE);
    if (!c->testBit(0)) c = c->add(ONE);
    while (!c->isProbablePrime(100)) c = c->add(TWO);
    return c;
}

// ------------------------------------------------------------------ comparison / hashing

int32_t BigInteger::compareTo(BigInteger* val) {
    if (signum_ != val->signum_) return signum_ > val->signum_ ? 1 : -1;
    if (signum_ == 0) return 0;
    int c;
    if (mag_.size() != val->mag_.size()) c = mag_.size() > val->mag_.size() ? 1 : -1;
    else {
        int m = std::memcmp(mag_.data(), val->mag_.data(), mag_.size());
        c = m == 0 ? 0 : (m > 0 ? 1 : -1);
    }
    return signum_ > 0 ? c : -c;
}

bool BigInteger::equals(Object* x) {
    if (x == this) return true;
    auto* b = dynamic_cast<BigInteger*>(x);
    if (b == nullptr) return false;
    return signum_ == b->signum_ && mag_ == b->mag_;
}

int32_t BigInteger::hashCode() {
    // Java: over the int[] magnitude (big-endian 32-bit words), then * signum.
    int32_t h = 0;
    size_t n = mag_.size();
    size_t firstLen = n % 4 == 0 ? 4 : n % 4;
    size_t i = 0;
    bool first = true;
    while (i < n) {
        size_t wl = first ? firstLen : 4;
        first = false;
        uint32_t w = 0;
        for (size_t k = 0; k < wl; k++) w = (w << 8) | mag_[i + k];
        i += wl;
        h = static_cast<int32_t>(31 * h + static_cast<int64_t>(w));
    }
    return h * signum_;
}

BigInteger* BigInteger::min(BigInteger* val) { return compareTo(val) < 0 ? this : val; }
BigInteger* BigInteger::max(BigInteger* val) { return compareTo(val) > 0 ? this : val; }

// ------------------------------------------------------------------ conversions

String BigInteger::toString() { return toString(10); }

String BigInteger::toString(int32_t radix) {
    if (signum_ == 0) return String("0");
    if (radix < 2 || radix > 36) radix = 10;
    Bn a;
    toBn(this, a);
    BN_set_negative(a, 0);
    // chunk = radix^k fitting in BN_ULONG
    BN_ULONG chunk = static_cast<BN_ULONG>(radix);
    int k = 1;
    while (chunk <= (~static_cast<BN_ULONG>(0)) / static_cast<BN_ULONG>(radix)) {
        chunk *= static_cast<BN_ULONG>(radix);
        k++;
    }
    static const char digits[] = "0123456789abcdefghijklmnopqrstuvwxyz";
    std::string out;
    while (!BN_is_zero(a)) {
        BN_ULONG rem = BN_div_word(a, chunk);
        if (rem == static_cast<BN_ULONG>(-1)) throw ArithmeticException("BigInteger: toString failed");
        bool last = BN_is_zero(a);
        for (int i = 0; i < k; i++) {
            if (last && rem == 0) break;
            out.push_back(digits[rem % static_cast<BN_ULONG>(radix)]);
            rem /= static_cast<BN_ULONG>(radix);
        }
    }
    if (signum_ < 0) out.push_back('-');
    std::reverse(out.begin(), out.end());
    return String(out);
}

Array<int8_t>* BigInteger::toByteArray() {
    size_t byteLen = static_cast<size_t>(bitLength() / 8 + 1);
    return toJavaBytes(twos(this, byteLen));
}

int32_t BigInteger::intValue() { return static_cast<int32_t>(longValue()); }

int64_t BigInteger::longValue() {
    uint64_t u = 0;
    size_t n = mag_.size();
    size_t take = std::min<size_t>(n, 8);
    for (size_t i = n - take; i < n; i++) u = (u << 8) | mag_[i];
    if (signum_ < 0) u = ~u + 1;
    return static_cast<int64_t>(u);
}

double BigInteger::doubleValue() {
    if (signum_ == 0) return 0.0;
    if (bitLength() <= 62) return static_cast<double>(longValue());
    std::string s = toString(10);
    return std::strtod(s.c_str(), nullptr);  // correctly rounded (round half even), +-Infinity on overflow
}

float BigInteger::floatValue() {
    if (signum_ == 0) return 0.0f;
    if (bitLength() <= 62) return static_cast<float>(longValue());
    std::string s = toString(10);
    return std::strtof(s.c_str(), nullptr);
}

}  // namespace jlang
