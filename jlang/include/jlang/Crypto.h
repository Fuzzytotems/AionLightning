// jlang/Crypto.h - java.math.BigInteger, java.security (MessageDigest, SecureRandom, RSA keys and
// key pair generation) and javax.crypto (Cipher for RSA, KeyGenerator, SecretKey), implemented
// over OpenSSL libcrypto (only jlang/src/crypto*.cpp includes OpenSSL headers).
//
// Java -> C++ (see tools/cppgen/jdkmap.tsv):
//   java.math.BigInteger                 jlang::BigInteger*
//   java.security.MessageDigest          jlang::MessageDigest*
//   java.security.SecureRandom           jlang::SecureRandom*  (a jlang::Random)
//   java.security.KeyPairGenerator       jlang::KeyPairGenerator*
//   java.security.KeyPair                jlang::KeyPair*
//   java.security.Key                    jlang::Key*            (interface)
//   java.security.PublicKey              jlang::RSAPublicKey*
//   java.security.PrivateKey             jlang::RSAPrivateKey*
//   java.security.interfaces.RSAKey      jlang::RSAKey*         (interface)
//   java.security.interfaces.RSAPublicKey / RSAPrivateKey   jlang::RSAPublicKey* / jlang::RSAPrivateKey*
//   java.security.spec.RSAKeyGenParameterSpec               jlang::RSAKeyGenParameterSpec*
//   javax.crypto.Cipher / KeyGenerator / SecretKey          jlang::Cipher* / KeyGenerator* / SecretKey*
//
// Java names that are C++ keywords get a trailing underscore (CONVENTIONS §3.4):
// BigInteger.and/or/xor/not -> and_/or_/xor_/not_.
//
// Memory: every object keeps its state in GC memory (OpenSSL objects are created and freed
// inside each call), so nothing leaks when an object is collected without a destructor run.
#pragma once

#include <jlang/jlang.h>

#include <cstdint>
#include <vector>

namespace jlang {

class BigInteger;
class SecureRandom;

// ---------------------------------------------------------------------------------------
// Exceptions thrown only by this module, all java.security.GeneralSecurityExceptions; their
// toString()/getClass()->getName() use the Java names (javax.crypto.BadPaddingException...).
JLANG_DECLARE_EXCEPTION(InvalidKeyException, GeneralSecurityException);
JLANG_DECLARE_EXCEPTION(InvalidAlgorithmParameterException, GeneralSecurityException);
JLANG_DECLARE_EXCEPTION(NoSuchPaddingException, GeneralSecurityException);
JLANG_DECLARE_EXCEPTION(BadPaddingException, GeneralSecurityException);
JLANG_DECLARE_EXCEPTION(IllegalBlockSizeException, GeneralSecurityException);
JLANG_DECLARE_EXCEPTION(DigestException, GeneralSecurityException);

// ---------------------------------------------------------------------------------------
// java.math.BigInteger: immutable arbitrary-precision integer with Java semantics
// (two's complement views for toByteArray/bit operations, truncating divide, non-negative mod).
// A java.lang.Number and Comparable<BigInteger>, like Java's.
class BigInteger : public Number, public virtual Comparable<BigInteger*> {
public:
    static BigInteger* const ZERO;
    static BigInteger* const ONE;
    static BigInteger* const TWO;
    static BigInteger* const TEN;

    // BigInteger(String[, radix]): optional leading '-' or '+', digits of the radix (2..36).
    // NumberFormatException as in Java ("Zero length BigInteger", "Radix out of range",
    // "Illegal embedded sign character", "For input string: \"...\"").
    explicit BigInteger(const String& val);
    BigInteger(const String& val, int32_t radix);
    // Two's complement big-endian bytes (NumberFormatException "Zero length BigInteger" if empty).
    explicit BigInteger(Array<int8_t>* val);
    // Sign-magnitude: signum -1/0/1, magnitude big-endian unsigned bytes.
    BigInteger(int32_t signum, Array<int8_t>* magnitude);
    // Uniformly random in [0, 2^numBits - 1] (same bytes as Java for the same Random).
    BigInteger(int32_t numBits, Random* rnd);
    // Random probable prime of the given bit length (not Java's exact sequence).
    BigInteger(int32_t bitLength, int32_t certainty, Random* rnd);

    static BigInteger* valueOf(int64_t val);
    static BigInteger* probablePrime(int32_t bitLength, Random* rnd);

    BigInteger* add(BigInteger* val);
    BigInteger* subtract(BigInteger* val);
    BigInteger* multiply(BigInteger* val);
    BigInteger* divide(BigInteger* val);                       // truncates toward zero
    BigInteger* remainder(BigInteger* val);                    // sign of this
    Array<BigInteger*>* divideAndRemainder(BigInteger* val);
    BigInteger* mod(BigInteger* m);                            // always >= 0; m > 0
    BigInteger* modPow(BigInteger* exponent, BigInteger* m);   // negative exponent -> modInverse
    BigInteger* modInverse(BigInteger* m);
    BigInteger* pow(int32_t exponent);
    BigInteger* gcd(BigInteger* val);
    BigInteger* abs();
    BigInteger* negate();
    int32_t signum();

    BigInteger* shiftLeft(int32_t n);
    BigInteger* shiftRight(int32_t n);                         // arithmetic (floor)
    BigInteger* and_(BigInteger* val);
    BigInteger* or_(BigInteger* val);
    BigInteger* xor_(BigInteger* val);
    BigInteger* not_();
    BigInteger* andNot(BigInteger* val);
    bool testBit(int32_t n);
    BigInteger* setBit(int32_t n);
    BigInteger* clearBit(int32_t n);
    BigInteger* flipBit(int32_t n);
    int32_t getLowestSetBit();
    int32_t bitLength();
    int32_t bitCount();

    bool isProbablePrime(int32_t certainty);
    BigInteger* nextProbablePrime();

    int32_t compareTo(BigInteger* val) override;
    bool equals(Object* x) override;
    int32_t hashCode() override;                              // Java's exact hash
    BigInteger* min(BigInteger* val);
    BigInteger* max(BigInteger* val);

    String toString() override;
    String toString(int32_t radix);                            // lower-case digits
    // Two's complement, big-endian, minimal length including a sign bit
    // (bitLength()/8 + 1 bytes): 128 -> {0x00, 0x80}, -128 -> {0x80}, 0 -> {0x00}.
    Array<int8_t>* toByteArray();

    int32_t intValue() override;
    int64_t longValue() override;
    float floatValue() override;
    double doubleValue() override;

    // --- jlang extensions ---
    // Unsigned big-endian magnitude without leading zero bytes (empty for zero). Not a copy
    // of Java API; used by the crypto implementation.
    const std::vector<uint8_t>& magnitudeBytes() const { return mag_; }
    static BigInteger* fromMagnitude(int32_t signum, std::vector<uint8_t> mag);

private:
    BigInteger() = default;
    void initFromString(const String& val, int32_t radix);
    void initFromTwosComplement(const uint8_t* p, size_t n);
    void normalize();
    int32_t signum_ = 0;
    std::vector<uint8_t> mag_;  // big-endian, no leading zeros; empty iff signum_ == 0
};

inline BigInteger* const BigInteger::ZERO = BigInteger::valueOf(0);
inline BigInteger* const BigInteger::ONE = BigInteger::valueOf(1);
inline BigInteger* const BigInteger::TWO = BigInteger::valueOf(2);
inline BigInteger* const BigInteger::TEN = BigInteger::valueOf(10);

// ---------------------------------------------------------------------------------------
// java.security.MessageDigest: MD5, SHA-1 (aliases "SHA", "SHA1"), SHA-224, SHA-256,
// SHA-384, SHA-512 (names are case-insensitive, as in Java). Not thread-safe (like Java).
class MessageDigest : public virtual Object {
public:
    // NoSuchAlgorithmException("<alg> MessageDigest not available") for unknown names.
    static MessageDigest* getInstance(const String& algorithm);
    static MessageDigest* getInstance(const String& algorithm, const String& provider);
    static bool isEqual(Array<int8_t>* digesta, Array<int8_t>* digestb);

    void update(int8_t input);
    void update(Array<int8_t>* input);
    // IllegalArgumentException("No input buffer given" / "Bad arguments" / "Input buffer too short")
    void update(Array<int8_t>* input, int32_t offset, int32_t len);
    // Completes the hash and resets the digest.
    Array<int8_t>* digest();
    Array<int8_t>* digest(Array<int8_t>* input);
    // Writes the digest into buf[offset..], returns its length (DigestException if len is too small).
    int32_t digest(Array<int8_t>* buf, int32_t offset, int32_t len);
    void reset();
    String getAlgorithm();             // the name given to getInstance
    int32_t getDigestLength();
    String toString() override;
    Object* clone() override;

    // jlang extension: raw update.
    void update(const void* data, size_t len);

private:
    MessageDigest() = default;
    void initCtx();
    String algorithm_;
    int32_t kind_ = 0;
    int32_t digestLength_ = 0;
    bool inProgress_ = false;
    // OpenSSL MD5_CTX / SHA_CTX / SHA256_CTX / SHA512_CTX storage (plain data, no heap).
    alignas(16) unsigned char ctx_[256] = {};
};

// ---------------------------------------------------------------------------------------
// java.security.SecureRandom: a jlang::Random whose bits come from OpenSSL's CSPRNG
// (RAND_bytes). setSeed only mixes additional entropy in (never makes the output repeatable).
class SecureRandom : public Random {
public:
    SecureRandom();
    explicit SecureRandom(Array<int8_t>* seed);
    // "SHA1PRNG", "NativePRNG", "NativePRNGBlocking", "NativePRNGNonBlocking", "DRBG", "Windows-PRNG";
    // NoSuchAlgorithmException("<alg> SecureRandom not available") otherwise.
    static SecureRandom* getInstance(const String& algorithm);
    static Array<int8_t>* getSeed(int32_t numBytes);

    void nextBytes(Array<int8_t>* bytes) override;
    void setSeed(int64_t seed) override;
    void setSeed(Array<int8_t>* seed);
    Array<int8_t>* generateSeed(int32_t numBytes);
    String getAlgorithm();

protected:
    int32_t next(int32_t numBits) override;

private:
    String algorithm_ = "NativePRNG";
};

// ---------------------------------------------------------------------------------------
// Keys.

// java.security.Key
class Key : public virtual Object {
public:
    virtual String getAlgorithm() = 0;
    virtual String getFormat() = 0;
    virtual Array<int8_t>* getEncoded() = 0;
};

// java.security.interfaces.RSAKey
class RSAKey : public virtual Object {
public:
    virtual BigInteger* getModulus() = 0;
};

// java.security.interfaces.RSAPublicKey (also used for java.security.PublicKey).
class RSAPublicKey : public virtual Key, public virtual RSAKey {
public:
    RSAPublicKey(BigInteger* modulus, BigInteger* publicExponent);
    BigInteger* getModulus() override { return modulus_; }
    virtual BigInteger* getPublicExponent() { return publicExponent_; }
    String getAlgorithm() override { return "RSA"; }
    String getFormat() override { return "X.509"; }
    // DER SubjectPublicKeyInfo, as Java's RSAPublicKeyImpl.getEncoded().
    Array<int8_t>* getEncoded() override;
    bool equals(Object* o) override;
    int32_t hashCode() override;
    String toString() override;

private:
    BigInteger* modulus_;
    BigInteger* publicExponent_;
};

// java.security.interfaces.RSAPrivateKey (also used for java.security.PrivateKey). Keys made
// by KeyPairGenerator carry the CRT components too (Java's RSAPrivateCrtKey getters).
class RSAPrivateKey : public virtual Key, public virtual RSAKey {
public:
    RSAPrivateKey(BigInteger* modulus, BigInteger* privateExponent);
    // CRT form (RSAPrivateCrtKeySpec order).
    RSAPrivateKey(BigInteger* modulus, BigInteger* publicExponent, BigInteger* privateExponent,
                  BigInteger* primeP, BigInteger* primeQ, BigInteger* primeExponentP,
                  BigInteger* primeExponentQ, BigInteger* crtCoefficient);
    BigInteger* getModulus() override { return modulus_; }
    virtual BigInteger* getPrivateExponent() { return privateExponent_; }
    // CRT components; nullptr for a key built from (modulus, privateExponent) only.
    virtual BigInteger* getPublicExponent() { return publicExponent_; }
    virtual BigInteger* getPrimeP() { return primeP_; }
    virtual BigInteger* getPrimeQ() { return primeQ_; }
    virtual BigInteger* getPrimeExponentP() { return primeExponentP_; }
    virtual BigInteger* getPrimeExponentQ() { return primeExponentQ_; }
    virtual BigInteger* getCrtCoefficient() { return crtCoefficient_; }
    bool isCrt() { return primeP_ != nullptr; }
    String getAlgorithm() override { return "RSA"; }
    String getFormat() override { return "PKCS#8"; }
    // DER PKCS#8 PrivateKeyInfo (CRT keys only; nullptr otherwise).
    Array<int8_t>* getEncoded() override;
    bool equals(Object* o) override;
    int32_t hashCode() override;

private:
    BigInteger* modulus_;
    BigInteger* privateExponent_;
    BigInteger* publicExponent_ = nullptr;
    BigInteger* primeP_ = nullptr;
    BigInteger* primeQ_ = nullptr;
    BigInteger* primeExponentP_ = nullptr;
    BigInteger* primeExponentQ_ = nullptr;
    BigInteger* crtCoefficient_ = nullptr;
};

// javax.crypto.SecretKey (a javax.crypto.spec.SecretKeySpec): raw key bytes + algorithm.
class SecretKey : public virtual Key {
public:
    SecretKey(Array<int8_t>* key, const String& algorithm);
    SecretKey(Array<int8_t>* key, int32_t offset, int32_t len, const String& algorithm);
    String getAlgorithm() override { return algorithm_; }
    String getFormat() override { return "RAW"; }
    Array<int8_t>* getEncoded() override;  // a copy, as in Java
    bool equals(Object* o) override;
    int32_t hashCode() override;

private:
    Array<int8_t>* key_;
    String algorithm_;
};

// java.security.KeyPair
class KeyPair : public virtual Object {
public:
    KeyPair(RSAPublicKey* publicKey, RSAPrivateKey* privateKey)
        : publicKey_(publicKey), privateKey_(privateKey) {}
    RSAPublicKey* getPublic() { return publicKey_; }
    RSAPrivateKey* getPrivate() { return privateKey_; }

private:
    RSAPublicKey* publicKey_;
    RSAPrivateKey* privateKey_;
};

// java.security.spec.AlgorithmParameterSpec (marker base)
class AlgorithmParameterSpec : public virtual Object {};

// java.security.spec.RSAKeyGenParameterSpec
class RSAKeyGenParameterSpec : public AlgorithmParameterSpec {
public:
    static BigInteger* const F0;  // 3
    static BigInteger* const F4;  // 65537
    RSAKeyGenParameterSpec(int32_t keysize, BigInteger* publicExponent)
        : keysize_(keysize), publicExponent_(publicExponent) {}
    int32_t getKeysize() { return keysize_; }
    BigInteger* getPublicExponent() { return publicExponent_; }

private:
    int32_t keysize_;
    BigInteger* publicExponent_;
};

inline BigInteger* const RSAKeyGenParameterSpec::F0 = BigInteger::valueOf(3);
inline BigInteger* const RSAKeyGenParameterSpec::F4 = BigInteger::valueOf(65537);

// java.security.KeyPairGenerator ("RSA" only). Keys have a modulus of exactly keysize bits,
// like Java's (default 2048 bits, exponent F4).
class KeyPairGenerator : public virtual Object {
public:
    static KeyPairGenerator* getInstance(const String& algorithm);
    static KeyPairGenerator* getInstance(const String& algorithm, const String& provider);
    String getAlgorithm() { return algorithm_; }
    // InvalidParameterException (-> jlang::IllegalArgumentException) for sizes outside 512..16384.
    void initialize(int32_t keysize);
    void initialize(int32_t keysize, SecureRandom* random);
    // InvalidAlgorithmParameterException for a non-RSA spec or a bad size/exponent.
    void initialize(AlgorithmParameterSpec* params);
    void initialize(AlgorithmParameterSpec* params, SecureRandom* random);
    KeyPair* generateKeyPair();
    KeyPair* genKeyPair() { return generateKeyPair(); }

private:
    KeyPairGenerator() = default;
    String algorithm_;
    int32_t keysize_ = 2048;
    BigInteger* publicExponent_ = nullptr;  // F4 when null
};

// javax.crypto.KeyGenerator: "Blowfish" (default 128-bit keys, like SunJCE), "AES" (128),
// "DES" (64 with parity), "DESede"/"TripleDES" (192), "HmacSHA1" (512), "HmacSHA256" (256), "HmacMD5" (512), "RC4"/"ARCFOUR" (128).
class KeyGenerator : public virtual Object {
public:
    static KeyGenerator* getInstance(const String& algorithm);
    static KeyGenerator* getInstance(const String& algorithm, const String& provider);
    String getAlgorithm() { return algorithm_; }
    void init(int32_t keysize);
    void init(int32_t keysize, SecureRandom* random);
    void init(SecureRandom* random);
    SecretKey* generateKey();

private:
    KeyGenerator() = default;
    String algorithm_;
    int32_t kind_ = 0;
    int32_t keyBytes_ = 16;
};

// javax.crypto.Cipher: RSA only ("RSA", "RSA/ECB/NoPadding", "RSA/NONE/NoPadding",
// "RSA/ECB/PKCS1Padding"; case-insensitive). Mirrors SunJCE's RSACipher: the output of
// doFinal is always modulus-length bytes for NoPadding (left-padded with zeros); input longer
// than the modulus -> IllegalBlockSizeException; input value >= modulus -> BadPaddingException.
class Cipher : public virtual Object {
public:
    static constexpr int32_t ENCRYPT_MODE = 1;
    static constexpr int32_t DECRYPT_MODE = 2;
    static constexpr int32_t WRAP_MODE = 3;
    static constexpr int32_t UNWRAP_MODE = 4;
    static constexpr int32_t PUBLIC_KEY = 1;
    static constexpr int32_t PRIVATE_KEY = 2;
    static constexpr int32_t SECRET_KEY = 3;

    // NoSuchAlgorithmException("Cannot find any provider supporting <transformation>").
    static Cipher* getInstance(const String& transformation);
    static Cipher* getInstance(const String& transformation, const String& provider);

    // InvalidKeyException for a null or non-RSA key.
    void init(int32_t opmode, Key* key);
    void init(int32_t opmode, Key* key, SecureRandom* random);

    // Buffers input; returns an empty array (RSA produces output only in doFinal).
    Array<int8_t>* update(Array<int8_t>* input);
    Array<int8_t>* update(Array<int8_t>* input, int32_t inputOffset, int32_t inputLen);
    Array<int8_t>* doFinal();
    Array<int8_t>* doFinal(Array<int8_t>* input);
    // IllegalArgumentException("Bad arguments") for a null input or bad offset/length.
    Array<int8_t>* doFinal(Array<int8_t>* input, int32_t inputOffset, int32_t inputLen);

    String getAlgorithm() { return transformation_; }
    int32_t getBlockSize() { return 0; }
    int32_t getOutputSize(int32_t inputLen);

private:
    Cipher() = default;
    void checkInit();
    String transformation_;
    bool pkcs1_ = false;
    int32_t mode_ = 0;          // 0 = not initialized
    BigInteger* modulus_ = nullptr;
    BigInteger* exponent_ = nullptr;
    RSAPrivateKey* crtKey_ = nullptr;  // for CRT decryption with private keys
    bool privateKey_ = false;
    std::vector<uint8_t> buffer_;
    int32_t bufOfs_ = 0;
    int32_t modLen_ = 0;
};

}  // namespace jlang
