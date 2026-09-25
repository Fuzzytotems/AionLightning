// java.security / javax.crypto over OpenSSL libcrypto: MessageDigest, SecureRandom, RSA keys,
// KeyPairGenerator, KeyGenerator, Cipher (RSA).
#include <jlang/Crypto.h>

// The low-level digest contexts (MD5_CTX, SHA_CTX, SHA256_CTX, SHA512_CTX) are plain structs
// that can live inside a GC object without any heap allocation or finalization. They are
// deprecated in OpenSSL 3 but still provided.
#define OPENSSL_SUPPRESS_DEPRECATED 1
#include <openssl/bn.h>
#include <openssl/core_names.h>
#include <openssl/err.h>
#include <openssl/evp.h>
#include <openssl/md5.h>
#include <openssl/rand.h>
#include <openssl/rsa.h>
#include <openssl/sha.h>
#include <openssl/x509.h>

#include <algorithm>
#include <cstring>

namespace jlang {

namespace {

// Java class names (getClass()->getName(), exception toString()).
bool registerCryptoNames() {
    using C = Class;
    C::registerClass<BigInteger>(String("java.math.BigInteger"), C::of<Number>(), {}, C::NONE, nullptr);
    C::registerClass<MessageDigest>(String("java.security.MessageDigest"), nullptr, {}, C::ABSTRACT, nullptr);
    C::registerClass<SecureRandom>(String("java.security.SecureRandom"), C::of<Random>(), {}, C::NONE,
                                   []() -> Object* { return new SecureRandom(); });
    C::registerClass<Key>(String("java.security.Key"), nullptr, {}, C::INTERFACE, nullptr);
    C::registerClass<RSAKey>(String("java.security.interfaces.RSAKey"), nullptr, {}, C::INTERFACE, nullptr);
    C::registerClass<RSAPublicKey>(String("java.security.interfaces.RSAPublicKey"), nullptr,
                                   {C::of<Key>(), C::of<RSAKey>()}, C::NONE, nullptr);
    C::registerClass<RSAPrivateKey>(String("java.security.interfaces.RSAPrivateKey"), nullptr,
                                    {C::of<Key>(), C::of<RSAKey>()}, C::NONE, nullptr);
    C::registerClass<SecretKey>(String("javax.crypto.spec.SecretKeySpec"), nullptr, {C::of<Key>()}, C::NONE, nullptr);
    C::registerClass<KeyPair>(String("java.security.KeyPair"), nullptr, {}, C::FINAL, nullptr);
    C::registerClass<AlgorithmParameterSpec>(String("java.security.spec.AlgorithmParameterSpec"), nullptr, {},
                                             C::INTERFACE, nullptr);
    C::registerClass<RSAKeyGenParameterSpec>(String("java.security.spec.RSAKeyGenParameterSpec"), nullptr,
                                             {C::of<AlgorithmParameterSpec>()}, C::NONE, nullptr);
    C::registerClass<KeyPairGenerator>(String("java.security.KeyPairGenerator"), nullptr, {}, C::ABSTRACT, nullptr);
    C::registerClass<KeyGenerator>(String("javax.crypto.KeyGenerator"), nullptr, {}, C::NONE, nullptr);
    C::registerClass<Cipher>(String("javax.crypto.Cipher"), nullptr, {}, C::NONE, nullptr);
    Class* gse = C::of<GeneralSecurityException>();
    C::registerClass<InvalidKeyException>(String("java.security.InvalidKeyException"), gse, {}, C::NONE, nullptr);
    C::registerClass<InvalidAlgorithmParameterException>(String("java.security.InvalidAlgorithmParameterException"),
                                                         gse, {}, C::NONE, nullptr);
    C::registerClass<DigestException>(String("java.security.DigestException"), gse, {}, C::NONE, nullptr);
    C::registerClass<NoSuchPaddingException>(String("javax.crypto.NoSuchPaddingException"), gse, {}, C::NONE, nullptr);
    C::registerClass<BadPaddingException>(String("javax.crypto.BadPaddingException"), gse, {}, C::NONE, nullptr);
    C::registerClass<IllegalBlockSizeException>(String("javax.crypto.IllegalBlockSizeException"), gse, {}, C::NONE,
                                                nullptr);
    return true;
}

[[maybe_unused]] const bool g_cryptoNamesRegistered = registerCryptoNames();

String upper(const String& s) {
    std::string r = s;
    for (auto& c : r) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    return String(r);
}

void randomBytes(void* p, size_t n) {
    if (n == 0) return;
    if (RAND_bytes(static_cast<unsigned char*>(p), static_cast<int>(n)) != 1) {
        ERR_clear_error();
        throw InternalError("RAND_bytes failed");
    }
}

}  // namespace

// ====================================================================== MessageDigest

namespace {
enum DigestKind { MD_MD5 = 1, MD_SHA1, MD_SHA224, MD_SHA256, MD_SHA384, MD_SHA512 };
static_assert(sizeof(MD5_CTX) <= 256 && sizeof(SHA_CTX) <= 256 && sizeof(SHA256_CTX) <= 256 &&
                  sizeof(SHA512_CTX) <= 256,
              "digest context storage too small");
}  // namespace

MessageDigest* MessageDigest::getInstance(const String& algorithm) {
    String u = upper(algorithm);
    int kind = 0, len = 0;
    if (u.equals("MD5")) kind = MD_MD5, len = 16;
    else if (u.equals("SHA-1") || u.equals("SHA") || u.equals("SHA1")) kind = MD_SHA1, len = 20;
    else if (u.equals("SHA-224") || u.equals("SHA224")) kind = MD_SHA224, len = 28;
    else if (u.equals("SHA-256") || u.equals("SHA256")) kind = MD_SHA256, len = 32;
    else if (u.equals("SHA-384") || u.equals("SHA384")) kind = MD_SHA384, len = 48;
    else if (u.equals("SHA-512") || u.equals("SHA512")) kind = MD_SHA512, len = 64;
    else throw NoSuchAlgorithmException(algorithm + " MessageDigest not available");
    auto* md = new MessageDigest();
    md->algorithm_ = algorithm;
    md->kind_ = kind;
    md->digestLength_ = len;
    md->initCtx();
    return md;
}

MessageDigest* MessageDigest::getInstance(const String& algorithm, const String& provider) {
    (void)provider;
    return getInstance(algorithm);
}

void MessageDigest::initCtx() {
    switch (kind_) {
        case MD_MD5: MD5_Init(reinterpret_cast<MD5_CTX*>(ctx_)); break;
        case MD_SHA1: SHA1_Init(reinterpret_cast<SHA_CTX*>(ctx_)); break;
        case MD_SHA224: SHA224_Init(reinterpret_cast<SHA256_CTX*>(ctx_)); break;
        case MD_SHA256: SHA256_Init(reinterpret_cast<SHA256_CTX*>(ctx_)); break;
        case MD_SHA384: SHA384_Init(reinterpret_cast<SHA512_CTX*>(ctx_)); break;
        case MD_SHA512: SHA512_Init(reinterpret_cast<SHA512_CTX*>(ctx_)); break;
    }
    inProgress_ = false;
}

void MessageDigest::update(const void* data, size_t len) {
    inProgress_ = true;
    if (len == 0) return;
    switch (kind_) {
        case MD_MD5: MD5_Update(reinterpret_cast<MD5_CTX*>(ctx_), data, len); break;
        case MD_SHA1: SHA1_Update(reinterpret_cast<SHA_CTX*>(ctx_), data, len); break;
        case MD_SHA224: SHA224_Update(reinterpret_cast<SHA256_CTX*>(ctx_), data, len); break;
        case MD_SHA256: SHA256_Update(reinterpret_cast<SHA256_CTX*>(ctx_), data, len); break;
        case MD_SHA384: SHA384_Update(reinterpret_cast<SHA512_CTX*>(ctx_), data, len); break;
        case MD_SHA512: SHA512_Update(reinterpret_cast<SHA512_CTX*>(ctx_), data, len); break;
    }
}

void MessageDigest::update(int8_t input) { update(&input, 1); }

void MessageDigest::update(Array<int8_t>* input) {
    if (input == nullptr) throw NullPointerException();
    update(input->data(), static_cast<size_t>(input->length));
}

void MessageDigest::update(Array<int8_t>* input, int32_t offset, int32_t len) {
    if (input == nullptr) throw IllegalArgumentException("No input buffer given");
    if (input->length - offset < len) throw IllegalArgumentException("Input buffer too short");
    inProgress_ = true;
    if (len == 0) return;
    if (offset < 0 || len < 0 || offset > input->length - len) throw ArrayIndexOutOfBoundsException();
    update(input->data() + offset, static_cast<size_t>(len));
}

Array<int8_t>* MessageDigest::digest() {
    auto* out = new Array<int8_t>(digestLength_);
    unsigned char* o = reinterpret_cast<unsigned char*>(out->data());
    switch (kind_) {
        case MD_MD5: MD5_Final(o, reinterpret_cast<MD5_CTX*>(ctx_)); break;
        case MD_SHA1: SHA1_Final(o, reinterpret_cast<SHA_CTX*>(ctx_)); break;
        case MD_SHA224: SHA224_Final(o, reinterpret_cast<SHA256_CTX*>(ctx_)); break;
        case MD_SHA256: SHA256_Final(o, reinterpret_cast<SHA256_CTX*>(ctx_)); break;
        case MD_SHA384: SHA384_Final(o, reinterpret_cast<SHA512_CTX*>(ctx_)); break;
        case MD_SHA512: SHA512_Final(o, reinterpret_cast<SHA512_CTX*>(ctx_)); break;
    }
    initCtx();
    return out;
}

Array<int8_t>* MessageDigest::digest(Array<int8_t>* input) {
    update(input);
    return digest();
}

int32_t MessageDigest::digest(Array<int8_t>* buf, int32_t offset, int32_t len) {
    if (buf == nullptr) throw IllegalArgumentException("No output buffer given");
    if (offset < 0 || len < 0) throw IllegalArgumentException("offset or len is less than 0");
    if (buf->length - offset < len)
        throw IllegalArgumentException("Output buffer too small for specified offset and length");
    if (len < digestLength_)
        throw DigestException(str("Length must be at least ", digestLength_, " for ", algorithm_, "digests"));
    Array<int8_t>* d = digest();
    std::memcpy(buf->data() + offset, d->data(), static_cast<size_t>(digestLength_));
    return digestLength_;
}

void MessageDigest::reset() { initCtx(); }

String MessageDigest::getAlgorithm() { return algorithm_; }

int32_t MessageDigest::getDigestLength() { return digestLength_; }

String MessageDigest::toString() {
    return algorithm_ + " Message Digest from SUN, " + (inProgress_ ? "<in progress>" : "<initialized>") + "\n";
}

Object* MessageDigest::clone() {
    auto* c = new MessageDigest();
    c->algorithm_ = algorithm_;
    c->kind_ = kind_;
    c->digestLength_ = digestLength_;
    c->inProgress_ = inProgress_;
    std::memcpy(c->ctx_, ctx_, sizeof ctx_);
    return c;
}

bool MessageDigest::isEqual(Array<int8_t>* a, Array<int8_t>* b) {
    if (a == b) return true;
    if (a == nullptr || b == nullptr) return false;
    if (a->length != b->length) return false;
    int r = 0;
    for (int32_t i = 0; i < a->length; i++) r |= (*a)[i] ^ (*b)[i];
    return r == 0;
}

// ====================================================================== SecureRandom

SecureRandom::SecureRandom() : Random(0) {}

SecureRandom::SecureRandom(Array<int8_t>* seed) : Random(0) { setSeed(seed); }

SecureRandom* SecureRandom::getInstance(const String& algorithm) {
    String u = upper(algorithm);
    if (u.equals("SHA1PRNG") || u.equals("NATIVEPRNG") || u.equals("NATIVEPRNGBLOCKING") ||
        u.equals("NATIVEPRNGNONBLOCKING") || u.equals("DRBG") || u.equals("WINDOWS-PRNG")) {
        auto* r = new SecureRandom();
        r->algorithm_ = algorithm;
        return r;
    }
    throw NoSuchAlgorithmException(algorithm + " SecureRandom not available");
}

Array<int8_t>* SecureRandom::getSeed(int32_t numBytes) {
    if (numBytes < 0) throw IllegalArgumentException("numBytes cannot be negative");
    auto* a = new Array<int8_t>(numBytes);
    randomBytes(a->data(), static_cast<size_t>(numBytes));
    return a;
}

void SecureRandom::nextBytes(Array<int8_t>* bytes) {
    if (bytes == nullptr) throw NullPointerException();
    randomBytes(bytes->data(), static_cast<size_t>(bytes->length));
}

void SecureRandom::setSeed(int64_t seed) {
    // Java: supplements the seed, never makes the output repeatable.
    if (seed != 0) RAND_add(&seed, sizeof seed, 0.0);
}

void SecureRandom::setSeed(Array<int8_t>* seed) {
    if (seed != nullptr && seed->length > 0) RAND_add(seed->data(), seed->length, 0.0);
}

Array<int8_t>* SecureRandom::generateSeed(int32_t numBytes) { return getSeed(numBytes); }

String SecureRandom::getAlgorithm() { return algorithm_; }

int32_t SecureRandom::next(int32_t numBits) {
    int32_t numBytes = (numBits + 7) / 8;
    uint8_t b[4] = {};
    randomBytes(b, static_cast<size_t>(numBytes));
    int32_t next = 0;
    for (int32_t i = 0; i < numBytes; i++) next = (next << 8) + (b[i] & 0xFF);
    return static_cast<int32_t>(static_cast<uint32_t>(next) >> (numBytes * 8 - numBits));
}

// ====================================================================== keys

RSAPublicKey::RSAPublicKey(BigInteger* modulus, BigInteger* publicExponent)
    : modulus_(modulus), publicExponent_(publicExponent) {
    if (modulus == nullptr || publicExponent == nullptr) throw NullPointerException();
}

namespace {

struct PkeyDeleter {
    EVP_PKEY* p = nullptr;
    ~PkeyDeleter() { EVP_PKEY_free(p); }
};

BIGNUM* newBn(BigInteger* x) {
    const auto& m = x->magnitudeBytes();
    BIGNUM* b = BN_bin2bn(m.data(), static_cast<int>(m.size()), nullptr);
    if (!b) throw OutOfMemoryError("BN_bin2bn");
    BN_set_negative(b, x->signum() < 0);
    return b;
}

// Builds an RSA EVP_PKEY via the legacy RSA API (simplest way to get DER encodings).
EVP_PKEY* makePkey(BigInteger* n, BigInteger* e, BigInteger* d, BigInteger* p, BigInteger* q,
                   BigInteger* dp, BigInteger* dq, BigInteger* qi) {
    RSA* rsa = RSA_new();
    if (!rsa) throw OutOfMemoryError("RSA_new");
    RSA_set0_key(rsa, newBn(n), e ? newBn(e) : nullptr, d ? newBn(d) : nullptr);
    if (p) {
        RSA_set0_factors(rsa, newBn(p), newBn(q));
        RSA_set0_crt_params(rsa, newBn(dp), newBn(dq), newBn(qi));
    }
    EVP_PKEY* pk = EVP_PKEY_new();
    if (!pk || EVP_PKEY_assign_RSA(pk, rsa) != 1) {
        RSA_free(rsa);
        EVP_PKEY_free(pk);
        throw InternalError("EVP_PKEY_assign_RSA failed");
    }
    return pk;
}

Array<int8_t>* derToArray(unsigned char* der, int len) {
    if (len <= 0) {
        ERR_clear_error();
        return nullptr;
    }
    auto* a = new Array<int8_t>(len);
    std::memcpy(a->data(), der, static_cast<size_t>(len));
    OPENSSL_free(der);
    return a;
}

}  // namespace

Array<int8_t>* RSAPublicKey::getEncoded() {
    PkeyDeleter pk;
    pk.p = makePkey(modulus_, publicExponent_, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr);
    unsigned char* der = nullptr;
    int len = i2d_PUBKEY(pk.p, &der);
    return derToArray(der, len);
}

bool RSAPublicKey::equals(Object* o) {
    if (o == this) return true;
    auto* k = dynamic_cast<RSAPublicKey*>(o);
    return k != nullptr && modulus_->equals(k->modulus_) && publicExponent_->equals(k->publicExponent_);
}

int32_t RSAPublicKey::hashCode() { return modulus_->hashCode() ^ publicExponent_->hashCode(); }

String RSAPublicKey::toString() {
    return String("Sun RSA public key, ") + String::valueOf(modulus_->bitLength()) + " bits\n  modulus: " +
           modulus_->toString() + "\n  public exponent: " + publicExponent_->toString();
}

RSAPrivateKey::RSAPrivateKey(BigInteger* modulus, BigInteger* privateExponent)
    : modulus_(modulus), privateExponent_(privateExponent) {
    if (modulus == nullptr || privateExponent == nullptr) throw NullPointerException();
}

RSAPrivateKey::RSAPrivateKey(BigInteger* modulus, BigInteger* publicExponent, BigInteger* privateExponent,
                             BigInteger* primeP, BigInteger* primeQ, BigInteger* primeExponentP,
                             BigInteger* primeExponentQ, BigInteger* crtCoefficient)
    : modulus_(modulus),
      privateExponent_(privateExponent),
      publicExponent_(publicExponent),
      primeP_(primeP),
      primeQ_(primeQ),
      primeExponentP_(primeExponentP),
      primeExponentQ_(primeExponentQ),
      crtCoefficient_(crtCoefficient) {
    if (!modulus || !publicExponent || !privateExponent || !primeP || !primeQ || !primeExponentP ||
        !primeExponentQ || !crtCoefficient)
        throw NullPointerException();
}

Array<int8_t>* RSAPrivateKey::getEncoded() {
    if (!isCrt()) return nullptr;
    PkeyDeleter pk;
    pk.p = makePkey(modulus_, publicExponent_, privateExponent_, primeP_, primeQ_, primeExponentP_,
                    primeExponentQ_, crtCoefficient_);
    PKCS8_PRIV_KEY_INFO* p8 = EVP_PKEY2PKCS8(pk.p);
    if (!p8) {
        ERR_clear_error();
        return nullptr;
    }
    unsigned char* der = nullptr;
    int len = i2d_PKCS8_PRIV_KEY_INFO(p8, &der);
    PKCS8_PRIV_KEY_INFO_free(p8);
    return derToArray(der, len);
}

bool RSAPrivateKey::equals(Object* o) {
    if (o == this) return true;
    auto* k = dynamic_cast<RSAPrivateKey*>(o);
    return k != nullptr && modulus_->equals(k->modulus_) && privateExponent_->equals(k->privateExponent_);
}

int32_t RSAPrivateKey::hashCode() { return modulus_->hashCode() ^ privateExponent_->hashCode(); }

SecretKey::SecretKey(Array<int8_t>* key, const String& algorithm) {
    if (key == nullptr || algorithm == nullptr) throw IllegalArgumentException("Missing argument");
    if (key->length == 0) throw IllegalArgumentException("Empty key");
    key_ = new Array<int8_t>(key->length);
    std::memcpy(key_->data(), key->data(), static_cast<size_t>(key->length));
    algorithm_ = algorithm;
}

SecretKey::SecretKey(Array<int8_t>* key, int32_t offset, int32_t len, const String& algorithm) {
    if (key == nullptr || algorithm == nullptr) throw IllegalArgumentException("Missing argument");
    if (key->length == 0) throw IllegalArgumentException("Empty key");
    if (key->length - offset < len) throw IllegalArgumentException("Invalid offset/length combination");
    if (len < 0) throw ArrayIndexOutOfBoundsException("len is negative");
    key_ = new Array<int8_t>(len);
    std::memcpy(key_->data(), key->data() + offset, static_cast<size_t>(len));
    algorithm_ = algorithm;
}

Array<int8_t>* SecretKey::getEncoded() {
    auto* c = new Array<int8_t>(key_->length);
    std::memcpy(c->data(), key_->data(), static_cast<size_t>(key_->length));
    return c;
}

bool SecretKey::equals(Object* o) {
    if (o == this) return true;
    auto* k = dynamic_cast<SecretKey*>(o);
    if (k == nullptr || !algorithm_.equalsIgnoreCase(k->algorithm_)) return false;
    return MessageDigest::isEqual(key_, k->key_);
}

int32_t SecretKey::hashCode() {
    int32_t retval = 0;
    for (int32_t i = 1; i < key_->length; i++) retval += (*key_)[i] * i;
    String lower = algorithm_.toLowerCase();
    if (lower.equals("tripledes")) return retval ^ String("desede").hashCode();
    return retval ^ lower.hashCode();
}

// ====================================================================== KeyPairGenerator

KeyPairGenerator* KeyPairGenerator::getInstance(const String& algorithm) {
    if (!upper(algorithm).equals("RSA")) throw NoSuchAlgorithmException(algorithm + " KeyPairGenerator not available");
    auto* g = new KeyPairGenerator();
    g->algorithm_ = algorithm;
    return g;
}

KeyPairGenerator* KeyPairGenerator::getInstance(const String& algorithm, const String& provider) {
    (void)provider;
    return getInstance(algorithm);
}

void KeyPairGenerator::initialize(int32_t keysize) { initialize(keysize, nullptr); }

void KeyPairGenerator::initialize(int32_t keysize, SecureRandom* random) {
    (void)random;
    if (keysize < 512) throw IllegalArgumentException("RSA keys must be at least 512 bits long");
    if (keysize > 16384) throw IllegalArgumentException("RSA keys must be no longer than 16384 bits");
    keysize_ = keysize;
    publicExponent_ = nullptr;
}

void KeyPairGenerator::initialize(AlgorithmParameterSpec* params) { initialize(params, nullptr); }

void KeyPairGenerator::initialize(AlgorithmParameterSpec* params, SecureRandom* random) {
    (void)random;
    auto* spec = dynamic_cast<RSAKeyGenParameterSpec*>(params);
    if (spec == nullptr) throw InvalidAlgorithmParameterException("Params must be instance of RSAKeyGenParameterSpec");
    int32_t size = spec->getKeysize();
    BigInteger* e = spec->getPublicExponent();
    if (e == nullptr) e = RSAKeyGenParameterSpec::F4;
    if (size < 512) throw InvalidAlgorithmParameterException("RSA keys must be at least 512 bits long");
    if (size > 16384) throw InvalidAlgorithmParameterException("RSA keys must be no longer than 16384 bits");
    if (e->compareTo(RSAKeyGenParameterSpec::F0) < 0)
        throw InvalidAlgorithmParameterException("Public exponent must be 3 or larger");
    if (!e->testBit(0)) throw InvalidAlgorithmParameterException("Public exponent must be an odd number");
    if (e->bitLength() > size) throw InvalidAlgorithmParameterException("Public exponent must be smaller than key size");
    keysize_ = size;
    publicExponent_ = e;
}

namespace {
BigInteger* getParam(EVP_PKEY* pk, const char* name) {
    BIGNUM* b = nullptr;
    if (EVP_PKEY_get_bn_param(pk, name, &b) != 1) {
        ERR_clear_error();
        throw InternalError(String("RSA key generation: missing parameter ") + name);
    }
    int n = BN_num_bytes(b);
    std::vector<uint8_t> mag(static_cast<size_t>(n));
    BN_bn2bin(b, mag.data());
    BN_clear_free(b);
    return BigInteger::fromMagnitude(n == 0 ? 0 : 1, std::move(mag));
}
}  // namespace

KeyPair* KeyPairGenerator::generateKeyPair() {
    BigInteger* e = publicExponent_ ? publicExponent_ : RSAKeyGenParameterSpec::F4;
    EVP_PKEY_CTX* ctx = EVP_PKEY_CTX_new_from_name(nullptr, "RSA", nullptr);
    if (!ctx) throw InternalError("EVP_PKEY_CTX_new_from_name(RSA) failed");
    EVP_PKEY* pk = nullptr;
    BIGNUM* eb = newBn(e);
    bool ok = EVP_PKEY_keygen_init(ctx) == 1 && EVP_PKEY_CTX_set_rsa_keygen_bits(ctx, keysize_) == 1 &&
              EVP_PKEY_CTX_set1_rsa_keygen_pubexp(ctx, eb) == 1 && EVP_PKEY_generate(ctx, &pk) == 1;
    BN_free(eb);
    EVP_PKEY_CTX_free(ctx);
    if (!ok) {
        ERR_clear_error();
        EVP_PKEY_free(pk);
        throw InternalError("RSA key generation failed");
    }
    BigInteger *n, *pe, *d, *p, *q, *dp, *dq, *qi;
    try {
        n = getParam(pk, OSSL_PKEY_PARAM_RSA_N);
        pe = getParam(pk, OSSL_PKEY_PARAM_RSA_E);
        d = getParam(pk, OSSL_PKEY_PARAM_RSA_D);
        p = getParam(pk, OSSL_PKEY_PARAM_RSA_FACTOR1);
        q = getParam(pk, OSSL_PKEY_PARAM_RSA_FACTOR2);
        dp = getParam(pk, OSSL_PKEY_PARAM_RSA_EXPONENT1);
        dq = getParam(pk, OSSL_PKEY_PARAM_RSA_EXPONENT2);
        qi = getParam(pk, OSSL_PKEY_PARAM_RSA_COEFFICIENT1);
    } catch (...) {
        EVP_PKEY_free(pk);
        throw;
    }
    EVP_PKEY_free(pk);
    return new KeyPair(new RSAPublicKey(n, pe), new RSAPrivateKey(n, pe, d, p, q, dp, dq, qi));
}

// ====================================================================== KeyGenerator

namespace {
enum KgKind { KG_BLOWFISH = 1, KG_AES, KG_DES, KG_DESEDE, KG_HMAC_SHA1, KG_HMAC_SHA256, KG_HMAC_MD5, KG_RC4 };

void setDesParity(uint8_t* k, size_t n) {
    for (size_t i = 0; i < n; i++) {
        uint8_t b = static_cast<uint8_t>(k[i] & 0xFE);
        b |= static_cast<uint8_t>((__builtin_popcount(b) & 1) ^ 1);
        k[i] = b;
    }
}
}  // namespace

KeyGenerator* KeyGenerator::getInstance(const String& algorithm) {
    String u = upper(algorithm);
    int kind = 0, bytes = 0;
    if (u.equals("BLOWFISH")) kind = KG_BLOWFISH, bytes = 16;
    else if (u.equals("AES")) kind = KG_AES, bytes = 16;
    else if (u.equals("DES")) kind = KG_DES, bytes = 8;
    else if (u.equals("DESEDE") || u.equals("TRIPLEDES")) kind = KG_DESEDE, bytes = 24;
    else if (u.equals("HMACSHA1")) kind = KG_HMAC_SHA1, bytes = 64;
    else if (u.equals("HMACSHA256")) kind = KG_HMAC_SHA256, bytes = 32;
    else if (u.equals("HMACMD5")) kind = KG_HMAC_MD5, bytes = 64;
    else if (u.equals("RC4") || u.equals("ARCFOUR")) kind = KG_RC4, bytes = 16;
    else throw NoSuchAlgorithmException(algorithm + " KeyGenerator not available");
    auto* g = new KeyGenerator();
    g->algorithm_ = algorithm;
    g->kind_ = kind;
    g->keyBytes_ = bytes;
    return g;
}

KeyGenerator* KeyGenerator::getInstance(const String& algorithm, const String& provider) {
    (void)provider;
    return getInstance(algorithm);
}

void KeyGenerator::init(int32_t keysize) { init(keysize, nullptr); }

void KeyGenerator::init(SecureRandom* random) { (void)random; }

void KeyGenerator::init(int32_t keysize, SecureRandom* random) {
    (void)random;
    switch (kind_) {
        case KG_BLOWFISH:
            if (keysize % 8 != 0 || keysize < 32 || keysize > 448)
                throw IllegalArgumentException("Keysize must be multiple of 8, and can only range from 32 to 448 (inclusive)");
            keyBytes_ = keysize / 8;
            break;
        case KG_AES:
            if (keysize != 128 && keysize != 192 && keysize != 256)
                throw IllegalArgumentException("Wrong keysize: must be equal to 128, 192 or 256");
            keyBytes_ = keysize / 8;
            break;
        case KG_DES:
            if (keysize != 56) throw IllegalArgumentException("Wrong keysize: must be equal to 56");
            keyBytes_ = 8;
            break;
        case KG_DESEDE:
            if (keysize != 112 && keysize != 168)
                throw IllegalArgumentException("Wrong keysize: must be equal to 112 or 168");
            keyBytes_ = 24;
            break;
        case KG_RC4:
            if (keysize < 40 || keysize > 1024)
                throw IllegalArgumentException("Key length must be between 40 and 1024 bit");
            keyBytes_ = (keysize + 7) / 8;
            break;
        default:
            if (keysize < 40) throw IllegalArgumentException("Key length must be at least 40 bits");
            keyBytes_ = (keysize + 7) / 8;
            break;
    }
}

SecretKey* KeyGenerator::generateKey() {
    auto* k = new Array<int8_t>(keyBytes_);
    randomBytes(k->data(), static_cast<size_t>(keyBytes_));
    if (kind_ == KG_DES || kind_ == KG_DESEDE) setDesParity(reinterpret_cast<uint8_t*>(k->data()), static_cast<size_t>(keyBytes_));
    const char* alg = algorithm_.c_str();
    String name;
    switch (kind_) {
        case KG_BLOWFISH: name = "Blowfish"; break;
        case KG_AES: name = "AES"; break;
        case KG_DES: name = "DES"; break;
        case KG_DESEDE: name = "DESede"; break;
        case KG_HMAC_SHA1: name = "HmacSHA1"; break;
        case KG_HMAC_SHA256: name = "HmacSHA256"; break;
        case KG_HMAC_MD5: name = "HmacMD5"; break;
        case KG_RC4: name = "RC4"; break;
        default: name = alg; break;
    }
    return new SecretKey(k, name);
}

// ====================================================================== Cipher (RSA)

Cipher* Cipher::getInstance(const String& transformation) {
    std::string t = upper(transformation);
    // strip whitespace around the '/' separated parts, like Java's tokenizer
    std::vector<std::string> parts;
    size_t start = 0;
    for (;;) {
        size_t slash = t.find('/', start);
        std::string p = t.substr(start, slash == std::string::npos ? std::string::npos : slash - start);
        size_t b = p.find_first_not_of(" \t"), e = p.find_last_not_of(" \t");
        parts.push_back(b == std::string::npos ? std::string() : p.substr(b, e - b + 1));
        if (slash == std::string::npos) break;
        start = slash + 1;
    }
    auto fail = [&]() -> Cipher* {
        throw NoSuchAlgorithmException(String("Cannot find any provider supporting ") + transformation);
    };
    if (parts.empty() || parts.size() == 2 || parts.size() > 3 || parts[0] != "RSA") return fail();
    bool pkcs1 = true;
    if (parts.size() == 3) {
        if (parts[1] != "ECB" && parts[1] != "NONE") return fail();
        if (parts[2] == "NOPADDING") pkcs1 = false;
        else if (parts[2] == "PKCS1PADDING") pkcs1 = true;
        else return fail();
    }
    auto* c = new Cipher();
    c->transformation_ = transformation;
    c->pkcs1_ = pkcs1;
    return c;
}

Cipher* Cipher::getInstance(const String& transformation, const String& provider) {
    (void)provider;
    return getInstance(transformation);
}

void Cipher::init(int32_t opmode, Key* key) { init(opmode, key, nullptr); }

void Cipher::init(int32_t opmode, Key* key, SecureRandom* random) {
    (void)random;
    if (opmode < ENCRYPT_MODE || opmode > UNWRAP_MODE) throw IllegalArgumentException("Invalid operation mode");
    if (key == nullptr) throw InvalidKeyException("Key must not be null");
    BigInteger* n = nullptr;
    BigInteger* exp = nullptr;
    RSAPrivateKey* crt = nullptr;
    bool priv = false;
    if (auto* pub = dynamic_cast<RSAPublicKey*>(key)) {
        n = pub->getModulus();
        exp = pub->getPublicExponent();
    } else if (auto* pk = dynamic_cast<RSAPrivateKey*>(key)) {
        n = pk->getModulus();
        exp = pk->getPrivateExponent();
        crt = pk->isCrt() ? pk : nullptr;
        priv = true;
    } else {
        throw InvalidKeyException(String("Unsupported key type: ") + key->getAlgorithm());
    }
    if (n->signum() <= 0) throw InvalidKeyException("Invalid RSA modulus");
    modulus_ = n;
    exponent_ = exp;
    crtKey_ = crt;
    privateKey_ = priv;
    mode_ = opmode;
    modLen_ = (n->bitLength() + 7) / 8;
    buffer_.assign(static_cast<size_t>(modLen_), 0);
    bufOfs_ = 0;
}

void Cipher::checkInit() {
    if (mode_ == 0) throw IllegalStateException("Cipher not initialized");
}

int32_t Cipher::getOutputSize(int32_t inputLen) {
    (void)inputLen;
    checkInit();
    return modLen_;
}

Array<int8_t>* Cipher::update(Array<int8_t>* input) {
    checkInit();
    if (input == nullptr) throw IllegalArgumentException("Null input buffer");
    if (input->length == 0) return nullptr;
    return update(input, 0, input->length);
}

Array<int8_t>* Cipher::update(Array<int8_t>* input, int32_t inputOffset, int32_t inputLen) {
    checkInit();
    if (input == nullptr || inputOffset < 0 || inputLen > input->length - inputOffset || inputLen < 0)
        throw IllegalArgumentException("Bad arguments");
    if (inputLen == 0) return nullptr;
    if (inputLen > modLen_ - bufOfs_) {
        bufOfs_ = modLen_ + 1;  // remembered: doFinal throws IllegalBlockSizeException
    } else {
        std::memcpy(buffer_.data() + bufOfs_, input->data() + inputOffset, static_cast<size_t>(inputLen));
        bufOfs_ += inputLen;
    }
    return new Array<int8_t>(0);
}

Array<int8_t>* Cipher::doFinal(Array<int8_t>* input) {
    checkInit();
    if (input == nullptr) throw IllegalArgumentException("Null input buffer");
    return doFinal(input, 0, input->length);
}

Array<int8_t>* Cipher::doFinal(Array<int8_t>* input, int32_t inputOffset, int32_t inputLen) {
    checkInit();
    if (input == nullptr || inputOffset < 0 || inputLen > input->length - inputOffset || inputLen < 0)
        throw IllegalArgumentException("Bad arguments");
    update(input, inputOffset, inputLen);
    return doFinal();
}

namespace {

// Raw RSA: m^exp mod n, as modulus-length big-endian bytes (Java RSACore.rsa).
std::vector<uint8_t> rsaCore(const std::vector<uint8_t>& msg, BigInteger* n, BigInteger* exp, RSAPrivateKey* crt,
                             int32_t modLen) {
    auto* in = new Array<int8_t>(static_cast<int32_t>(msg.size()));
    if (!msg.empty()) std::memcpy(in->data(), msg.data(), msg.size());
    BigInteger* c = new BigInteger(1, in);
    if (c->compareTo(n) >= 0) throw BadPaddingException("Message is larger than modulus");
    BigInteger* m;
    if (crt != nullptr) {
        // CRT (same result as c^d mod n)
        BigInteger* p = crt->getPrimeP();
        BigInteger* q = crt->getPrimeQ();
        BigInteger* m1 = c->modPow(crt->getPrimeExponentP(), p);
        BigInteger* m2 = c->modPow(crt->getPrimeExponentQ(), q);
        BigInteger* h = m1->subtract(m2);
        if (h->signum() < 0) h = h->add(p);
        h = h->multiply(crt->getCrtCoefficient())->mod(p);
        m = h->multiply(q)->add(m2);
    } else {
        m = c->modPow(exp, n);
    }
    const auto& mag = m->magnitudeBytes();
    std::vector<uint8_t> out(static_cast<size_t>(modLen), 0);
    std::copy(mag.begin(), mag.end(), out.end() - static_cast<std::ptrdiff_t>(mag.size()));
    return out;
}

}  // namespace

Array<int8_t>* Cipher::doFinal() {
    checkInit();
    struct Reset {
        std::vector<uint8_t>* buf;
        int32_t* ofs;
        ~Reset() {
            std::fill(buf->begin(), buf->end(), 0);  // do not keep plaintext around (like SunJCE)
            *ofs = 0;
        }
    } reset{&buffer_, &bufOfs_};
    if (bufOfs_ > modLen_) throw IllegalBlockSizeException(String("Data must not be longer than ") + String::valueOf(modLen_) + " bytes");
    std::vector<uint8_t> data(buffer_.begin(), buffer_.begin() + bufOfs_);
    bool encrypt = (mode_ == ENCRYPT_MODE || mode_ == WRAP_MODE);
    std::vector<uint8_t> out;
    RSAPrivateKey* crt = privateKey_ ? crtKey_ : nullptr;
    if (encrypt) {
        std::vector<uint8_t> padded;
        if (!pkcs1_) {
            padded = data;
        } else {
            // PKCS#1 v1.5: 00 || BT || PS || 00 || data; BT = 02 (public key, random non-zero PS)
            // or 01 (private key, PS = FF...)
            int32_t maxData = modLen_ - 11;
            if (static_cast<int32_t>(data.size()) > maxData)
                throw BadPaddingException(String("Data must not be longer than ") + String::valueOf(maxData) + " bytes");
            padded.assign(static_cast<size_t>(modLen_), 0);
            size_t psLen = static_cast<size_t>(modLen_) - 3 - data.size();
            padded[1] = privateKey_ ? 1 : 2;
            for (size_t i = 0; i < psLen; i++) {
                uint8_t b = 0xFF;
                if (!privateKey_) {
                    do randomBytes(&b, 1);
                    while (b == 0);
                }
                padded[2 + i] = b;
            }
            padded[2 + psLen] = 0;
            std::copy(data.begin(), data.end(), padded.begin() + static_cast<std::ptrdiff_t>(3 + psLen));
        }
        out = rsaCore(padded, modulus_, exponent_, crt, modLen_);
    } else {
        out = rsaCore(data, modulus_, exponent_, crt, modLen_);
        if (pkcs1_) {
            // unpad block type 2 (private key) or 1 (public key)
            uint8_t bt = privateKey_ ? 2 : 1;
            bool bad = out.size() < 11 || out[0] != 0 || out[1] != bt;
            size_t i = 2;
            if (!bad) {
                while (i < out.size() && out[i] != 0) {
                    if (bt == 1 && out[i] != 0xFF) {
                        bad = true;
                        break;
                    }
                    i++;
                }
                if (i >= out.size() || i < 10) bad = true;
            }
            if (bad) throw BadPaddingException("Decryption error");
            out.erase(out.begin(), out.begin() + static_cast<std::ptrdiff_t>(i + 1));
        }
    }
    auto* r = new Array<int8_t>(static_cast<int32_t>(out.size()));
    if (!out.empty()) std::memcpy(r->data(), out.data(), out.size());
    return r;
}

}  // namespace jlang
