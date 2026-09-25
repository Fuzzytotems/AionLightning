// Tests for <jlang/Crypto.h>: BigInteger, MessageDigest, SecureRandom, RSA keys and Cipher,
// KeyGenerator. Reference values come from Java 21 (see the generators in the port's
// scratch "javaref" programs: BigRef.java, DigestRef.java, RsaRef.java).
#include "jtest.h"

#include <jlang/Crypto.h>

#include <cstring>
#include <set>
#include <string>

using namespace jlang;

namespace {

std::string toHex(Array<int8_t>* a) {
    static const char* d = "0123456789abcdef";
    std::string s;
    for (int32_t i = 0; i < a->length; i++) {
        uint8_t b = static_cast<uint8_t>((*a)[i]);
        s.push_back(d[b >> 4]);
        s.push_back(d[b & 15]);
    }
    return s;
}

Array<int8_t>* fromHex(const std::string& h) {
    auto* a = new Array<int8_t>(static_cast<int32_t>(h.size() / 2));
    for (size_t i = 0; i < h.size() / 2; i++)
        (*a)[static_cast<int32_t>(i)] = static_cast<int8_t>(std::stoi(h.substr(2 * i, 2), nullptr, 16));
    return a;
}

Array<int8_t>* bytesOf(const char* s) {
    size_t n = std::strlen(s);
    auto* a = new Array<int8_t>(static_cast<int32_t>(n));
    std::memcpy(a->data(), s, n);
    return a;
}

BigInteger* big(const char* s) { return new BigInteger(String(s)); }
BigInteger* bigHex(const char* s) { return new BigInteger(String(s), 16); }

// Generated from Java 21 java.math.BigInteger (javaref/BigRef.java).
struct BigRow { const char* v; const char* bytes; int32_t bitLength; int32_t bitCount; int32_t signum; const char* hex; const char* r36; int32_t hash; int32_t intValue; int64_t longValue; };
const BigRow kBigRows[] = {
    {"0", "00", 0, 0, 0, "0", "0", 0, 0, INT64_C(0)},
    {"1", "01", 1, 1, 1, "1", "1", 1, 1, INT64_C(1)},
    {"-1", "ff", 0, 0, -1, "-1", "-1", -1, -1, INT64_C(-1)},
    {"127", "7f", 7, 7, 1, "7f", "3j", 127, 127, INT64_C(127)},
    {"128", "0080", 8, 1, 1, "80", "3k", 128, 128, INT64_C(128)},
    {"-128", "80", 7, 7, -1, "-80", "-3k", -128, -128, INT64_C(-128)},
    {"-129", "ff7f", 8, 1, -1, "-81", "-3l", -129, -129, INT64_C(-129)},
    {"255", "00ff", 8, 8, 1, "ff", "73", 255, 255, INT64_C(255)},
    {"256", "0100", 9, 1, 1, "100", "74", 256, 256, INT64_C(256)},
    {"-255", "ff01", 8, 7, -1, "-ff", "-73", -255, -255, INT64_C(-255)},
    {"-256", "ff00", 8, 8, -1, "-100", "-74", -256, -256, INT64_C(-256)},
    {"32767", "7fff", 15, 15, 1, "7fff", "pa7", 32767, 32767, INT64_C(32767)},
    {"32768", "008000", 16, 1, 1, "8000", "pa8", 32768, 32768, INT64_C(32768)},
    {"-32768", "8000", 15, 15, -1, "-8000", "-pa8", -32768, -32768, INT64_C(-32768)},
    {"-32769", "ff7fff", 16, 1, -1, "-8001", "-pa9", -32769, -32769, INT64_C(-32769)},
    {"65535", "00ffff", 16, 16, 1, "ffff", "1ekf", 65535, 65535, INT64_C(65535)},
    {"65536", "010000", 17, 1, 1, "10000", "1ekg", 65536, 65536, INT64_C(65536)},
    {"2147483647", "7fffffff", 31, 31, 1, "7fffffff", "zik0zj", 2147483647, 2147483647, INT64_C(2147483647)},
    {"2147483648", "0080000000", 32, 1, 1, "80000000", "zik0zk", INT32_MIN, -2147483648, INT64_C(2147483648)},
    {"-2147483648", "80000000", 31, 31, -1, "-80000000", "-zik0zk", INT32_MIN, -2147483648, INT64_C(-2147483648)},
    {"-2147483649", "ff7fffffff", 32, 1, -1, "-80000001", "-zik0zl", 2147483647, 2147483647, INT64_C(-2147483649)},
    {"4294967295", "00ffffffff", 32, 32, 1, "ffffffff", "1z141z3", -1, -1, INT64_C(4294967295)},
    {"4294967296", "0100000000", 33, 1, 1, "100000000", "1z141z4", 31, 0, INT64_C(4294967296)},
    {"9223372036854775807", "7fffffffffffffff", 63, 63, 1, "7fffffffffffffff", "1y2p0ij32e8e7", 2147483616, -1, INT64_C(9223372036854775807)},
    {"9223372036854775808", "008000000000000000", 64, 1, 1, "8000000000000000", "1y2p0ij32e8e8", INT32_MIN, 0, INT64_MIN},
    {"-9223372036854775808", "8000000000000000", 63, 63, -1, "-8000000000000000", "-1y2p0ij32e8e8", INT32_MIN, 0, INT64_MIN},
    {"-9223372036854775809", "ff7fffffffffffffff", 64, 1, -1, "-8000000000000001", "-1y2p0ij32e8e9", 2147483647, -1, INT64_C(9223372036854775807)},
    {"18446744073709551615", "00ffffffffffffffff", 64, 64, 1, "ffffffffffffffff", "3w5e11264sgsf", -32, -1, INT64_C(-1)},
    {"18446744073709551616", "010000000000000000", 65, 1, 1, "10000000000000000", "3w5e11264sgsg", 961, 0, INT64_C(0)},
    {"-18446744073709551616", "ff0000000000000000", 64, 64, -1, "-10000000000000000", "-3w5e11264sgsg", -961, 0, INT64_C(0)},
    {"340282366920938463463374607431768211455", "00ffffffffffffffffffffffffffffffff", 128, 128, 1, "ffffffffffffffffffffffffffffffff", "f5lxx1zz5pnorynqglhzmsp33", -30784, -1, INT64_C(-1)},
    {"-340282366920938463463374607431768211456", "ff00000000000000000000000000000000", 128, 128, -1, "-100000000000000000000000000000000", "-f5lxx1zz5pnorynqglhzmsp34", -923521, 0, INT64_C(0)},
    {"123456789012345678901234567890123456789012345678901234567890", "13aaf504e4bc1e62173f87a4378c37b49c8ccff196ce3f0ad2", 197, 103, 1, "13aaf504e4bc1e62173f87a4378c37b49c8ccff196ce3f0ad2", "w8g22aadxdzqcj994778lrfivxob1p0k7954gi", 1365210119, -834729262, INT64_C(-8300149958212908334)},
    {"-98765432109876543210987654321098765432109876543210", "bc6c04da5dcb7f17d6f731d6a830499828ae398116", 167, 85, -1, "-4393fb25a23480e82908ce2957cfb667d751c67eea", "-1k4two7tu6ew0j8i5sji2z8aw9uczdl2i", -250557184, -1371963114, INT64_C(3479479487609536790)},
};
struct TwosRow { const char* hex; const char* twos; const char* pos; const char* neg; };
const TwosRow kTwosRows[] = {
    {"00", "0", "0", "0"},
    {"ff", "-1", "255", "-255"},
    {"80", "-128", "128", "-128"},
    {"7f", "127", "127", "-127"},
    {"0080", "128", "128", "-128"},
    {"ff7f", "-129", "65407", "-65407"},
    {"ffff", "-1", "65535", "-65535"},
    {"ffffff80", "-128", "4294967168", "-4294967168"},
    {"000000", "0", "0", "0"},
    {"00ff", "255", "255", "-255"},
    {"ff00", "-256", "65280", "-65280"},
    {"fe", "-2", "254", "-254"},
    {"0100", "256", "256", "-256"},
    {"80000000", "-2147483648", "2147483648", "-2147483648"},
    {"ff80000000", "-2147483648", "1097364144128", "-1097364144128"},
    {"00000001", "1", "1", "-1"},
    {"ffffffffffffffffff", "-1", "4722366482869645213695", "-4722366482869645213695"},
};
struct ArithRow { const char *a, *b, *add, *sub, *mul, *div, *rem, *mod, *gcd, *and_, *or_, *xor_; int32_t cmp; const char *shl5, *shr5, *not_, *pow3; };
const ArithRow kArithRows[] = {
    {"12345678901234567890", "987654321", "12345678902222222211", "12345678900246913569", "12193263112482853211126352690", "12499999887", "339506163", "339506163", "9", "706611344", "12345678901515610867", "12345678900808999523", 1, "395061724839506172480", "385802465663580246", "-12345678901234567891", "1881676372353657772490265749424677022198701224860897069000"},
    {"-12345678901234567890", "987654321", "-12345678900246913569", "-12345678902222222211", "-12193263112482853211126352690", "-12499999887", "-339506163", "648148158", "9", "281042976", "-12345678900527956545", "-12345678900808999521", -1, "-395061724839506172480", "-385802465663580247", "12345678901234567889", "-1881676372353657772490265749424677022198701224860897069000"},
    {"12345678901234567890", "-987654321", "12345678900246913569", "12345678902222222211", "-12193263112482853211126352690", "-12499999887", "339506163", "339506163", "9", "12345678900527956546", "-281042977", "-12345678900808999523", 1, "395061724839506172480", "385802465663580246", "-12345678901234567891", "1881676372353657772490265749424677022198701224860897069000"},
    {"-12345678901234567890", "-987654321", "-12345678902222222211", "-12345678900246913569", "12193263112482853211126352690", "12499999887", "-339506163", "648148158", "9", "-12345678901515610866", "-706611345", "12345678900808999521", -1, "-395061724839506172480", "-385802465663580247", "12345678901234567889", "-1881676372353657772490265749424677022198701224860897069000"},
    {"7", "-3", "4", "10", "-21", "-2", "1", "1", "1", "5", "-1", "-6", 1, "224", "0", "-8", "343"},
    {"-7", "3", "-4", "-10", "-21", "-2", "-1", "2", "1", "1", "-5", "-6", -1, "-224", "-1", "6", "-343"},
    {"-7", "-3", "-10", "-4", "21", "2", "-1", "2", "1", "-7", "-3", "4", -1, "-224", "-1", "6", "-343"},
    {"0", "5", "5", "-5", "0", "0", "0", "0", "5", "0", "5", "5", -1, "0", "0", "-1", "0"},
    {"-1", "1", "0", "-2", "-1", "-1", "0", "0", "1", "1", "-1", "-2", -1, "-32", "-1", "0", "-1"},
    {"-256", "255", "-1", "-511", "-65280", "-1", "-1", "254", "1", "0", "-1", "-1", -1, "-8192", "-8", "255", "-16777216"},
    {"340282366920938463463374607431768211455", "18446744073709551616", "340282366920938463481821351505477763071", "340282366920938463444927863358058659839", "6277101735386680763835789423207666416083908700390324961280", "18446744073709551615", "18446744073709551615", "18446744073709551615", "1", "18446744073709551616", "340282366920938463463374607431768211455", "340282366920938463444927863358058659839", 1, "10889035741470030830827987437816582766560", "10633823966279326983230456482242756607", "-340282366920938463463374607431768211456", "39402006196394479212279040100143613804732363002753498081677580449219658047938421504518107378156933012605183906021375"},
    {"-5", "7", "2", "-12", "-35", "0", "-5", "2", "1", "3", "-1", "-4", -1, "-160", "-1", "4", "-125"},
};
struct ModPowRow { const char *base, *exp, *mod, *result; };
const ModPowRow kModPowRows[] = {
    {"4", "13", "497", "445"},
    {"-4", "13", "497", "52"},
    {"2", "-1", "7", "4"},
    {"3", "-2", "11", "5"},
    {"-3", "5", "1", "0"},
    {"0", "0", "7", "1"},
    {"12345678901234567890", "98765", "1000000007", "508039537"},
    {"-12345678901234567890", "98765", "1000000007", "491960470"},
    {"5", "0", "1", "0"},
};
struct ModInvRow { const char *a, *m, *result; };
const ModInvRow kModInvRows[] = {
    {"3", "11", "4"},
    {"-3", "11", "7"},
    {"10", "17", "12"},
    {"123456789", "1000000007", "18633540"},
};
struct RadixRow { const char* s; int32_t radix; const char* dec; };
const RadixRow kRadixRows[] = {
    {"ff", 16, "255"},
    {"-FF", 16, "-255"},
    {"+123", 10, "123"},
    {"000123", 10, "123"},
    {"-0", 10, "0"},
    {"zz", 36, "1295"},
    {"101010", 2, "42"},
    {"7fffffffffffffffffff", 16, "604462909807314587353087"},
};
struct ParseErrRow { const char* s; const char* msg; };
const ParseErrRow kParseErrRows[] = {
    {"", "Zero length BigInteger"},
    {"-", "Zero length BigInteger"},
    {"+", "Zero length BigInteger"},
    {"1-2", "Illegal embedded sign character"},
    {"--1", "Illegal embedded sign character"},
    {"12a", "For input string: \"12a\""},
    {" 1", "For input string: \" 1\""},
    {"0x10", "For input string: \"x10\""},
    {"+-1", "Illegal embedded sign character"},
};

struct DigestRow { const char* alg; int32_t inputIndex; const char* hex; };
// {alg, input length (index into kDigestInputs by length), digest} from Java 21 (javaref/DigestRef.java)
const DigestRow kDigestRows[] = {
    {"MD5", 0, "d41d8cd98f00b204e9800998ecf8427e"},
    {"MD5", 3, "900150983cd24fb0d6963f7d28e17f72"},
    {"MD5", 43, "9e107d9d372bb6826bd81d3542a419d6"},
    {"MD5", 8, "12841e4ba5e37d2fbfc78458c6714ade"},
    {"SHA-1", 0, "da39a3ee5e6b4b0d3255bfef95601890afd80709"},
    {"SHA-1", 3, "a9993e364706816aba3e25717850c26c9cd0d89d"},
    {"SHA-1", 43, "2fd4e1c67a2d28fced849ee1bb76e7391b93eb12"},
    {"SHA-1", 8, "f517ddf1d32a112ff1ad55c66d1b12cb38e7e8f7"},
    {"SHA", 0, "da39a3ee5e6b4b0d3255bfef95601890afd80709"},
    {"SHA", 3, "a9993e364706816aba3e25717850c26c9cd0d89d"},
    {"SHA", 43, "2fd4e1c67a2d28fced849ee1bb76e7391b93eb12"},
    {"SHA", 8, "f517ddf1d32a112ff1ad55c66d1b12cb38e7e8f7"},
    {"SHA1", 0, "da39a3ee5e6b4b0d3255bfef95601890afd80709"},
    {"SHA1", 3, "a9993e364706816aba3e25717850c26c9cd0d89d"},
    {"SHA1", 43, "2fd4e1c67a2d28fced849ee1bb76e7391b93eb12"},
    {"SHA1", 8, "f517ddf1d32a112ff1ad55c66d1b12cb38e7e8f7"},
    {"sha-256", 0, "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855"},
    {"sha-256", 3, "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad"},
    {"sha-256", 43, "d7a8fbb307d7809469ca9abcb0082e4f8d5651e46d3cdb762d02d0bf37c9e592"},
    {"sha-256", 8, "46970bef70aced8123f0d5d094717e2a5cd412041e03b26376049fe65b2834a4"},
    {"SHA-224", 0, "d14a028c2a3a2bc9476102bb288234c415a2b01f828ea62ac5b3e42f"},
    {"SHA-224", 3, "23097d223405d8228642a477bda255b32aadbce4bda0b3f7e36c9da7"},
    {"SHA-224", 43, "730e109bd7a8a32b1cb9d9a09aa2325d2430587ddbc0c38bad911525"},
    {"SHA-224", 8, "1fa1dc9715a45491364693be5cfc25e1e8478204b8b4ef6deee9d3b3"},
    {"SHA-384", 0, "38b060a751ac96384cd9327eb1b1e36a21fdb71114be07434c0cc7bf63f6e1da274edebfe76f65fbd51ad2f14898b95b"},
    {"SHA-384", 3, "cb00753f45a35e8bb5a03d699ac65007272c32ab0eded1631a8b605a43ff5bed8086072ba1e7cc2358baeca134c825a7"},
    {"SHA-384", 43, "ca737f1014a48f4c0b6dd43cb177b0afd9e5169367544c494011e3317dbf9a509cb1e5dc1e85a941bbee3d7f2afbc9b1"},
    {"SHA-384", 8, "81e4760980814cf38b80699a1da619d706dab566ea3d7657f5bdb0438aa2c9314acf26e372012324c7385804720cc733"},
    {"SHA-512", 0, "cf83e1357eefb8bdf1542850d66d8007d620e4050b5715dc83f4a921d36ce9ce47d0d13c5d85f2b0ff8318d2877eec2f63b931bd47417a81a538327af927da3e"},
    {"SHA-512", 3, "ddaf35a193617abacc417349ae20413112e6fa4e89a97ea20a9eeee64b55d39a2192992a274fc1a836ba3c23a3feebbd454d4423643ce80e2a9ac94fa54ca49f"},
    {"SHA-512", 43, "07e547d9586f6a73f73fbac0435ed76951218fb7d0c8d788a309d785436bbb642e93a252a954f23912547d1e8a3b5ed6e1bfd7097821233fa0538f3db854fee6"},
    {"SHA-512", 8, "72c7fe1bd33b785746a9c94f9b80d2591cd38f3c13c8aa43f7537f32a2a8c5f9b5d1cee16b69f0212e0e39a94d83f57bcf3a26bae776007cebb01f0d6311273e"},
};

// Java 21 SunRsaSign/SunJCE reference (javaref/RsaRef.java): a 1024-bit key and the results of
// RSA/ECB/NoPadding with it.
const char* const kRsa_n = "ecf98eaf294ea90e8940bb89bb458bed1117c78bf116419cb8f2d52bd315d9a6ffe59c762f4778a5ff9789f0b0eff38d0366d1b522e6f462b511a431a5a58a0c6836308a68e8921f9a3f4925ef1dd62268b617ed535a9d24b99c89248d071e406c78554c6625a37d246d062a6927b9c42cd8ac586f1f1e6c687baf01f8ceb2bb";
const char* const kRsa_e = "10001";
const char* const kRsa_d = "587b1db17d44e78e8be4e5a11991701b862450d7899efbc49eb1dcb24e507c42048e210b67bfd1e6793685c49eec190defd5363be1da46298cde17668a28f30fd2daa8d69bd2a16979916135b28124ce7f844bc66d209bcae23cf86a57d24545a4e811d35199ed2f7d05321ab1492bb682450705489df8dda28945845fb578a1";
const char* const kRsa_p = "ee8318f5dd9593ee5f7f6d386a9aab63833ad5af808a971c38b3259b0a8963fd0a9baf6e6529ed21eb82d60820de2e042dc5f79cbb6466febd21b283a86507b9";
const char* const kRsa_q = "fe599aecdb1268ec212c0143df4a7e011dbc96eebe6c153aa2d9eb61557fbee8bd4d0aaef3e49abe4b17196422a05392d292cebceb5f7964e33ff4f197182013";
const char* const kRsa_dp = "6a5692b3539693bccc6100ae0d8165f65914cb1e931db71f82fc04412aa274ace5a4e1c343613cf349902ca2b51cb9c16d32fd21a8beabd8d93403e60516fe29";
const char* const kRsa_dq = "90866be0c04e82ae01df564cb1b94b45e916bb10a7c5147f0000219e6a0daf75e1f2bfea4f8d8b5c2ba0d17db3ab3431ce533b03e01e9d04f597e6dfcc021a5f";
const char* const kRsa_qi = "a42f031f4a5eaf00803d6072a09cb7fdb77538ad9a0b73bd35e214be3bbb3e8b86fc27c4ea758fcedab7db31363b88aeade21f8b4b646ed8ee8a74f626f58ebe";
const char* const kRsa_modbytes = "00ecf98eaf294ea90e8940bb89bb458bed1117c78bf116419cb8f2d52bd315d9a6ffe59c762f4778a5ff9789f0b0eff38d0366d1b522e6f462b511a431a5a58a0c6836308a68e8921f9a3f4925ef1dd62268b617ed535a9d24b99c89248d071e406c78554c6625a37d246d062a6927b9c42cd8ac586f1f1e6c687baf01f8ceb2bb";
const char* const kRsa_x509 = "30819f300d06092a864886f70d010101050003818d0030818902818100ecf98eaf294ea90e8940bb89bb458bed1117c78bf116419cb8f2d52bd315d9a6ffe59c762f4778a5ff9789f0b0eff38d0366d1b522e6f462b511a431a5a58a0c6836308a68e8921f9a3f4925ef1dd62268b617ed535a9d24b99c89248d071e406c78554c6625a37d246d062a6927b9c42cd8ac586f1f1e6c687baf01f8ceb2bb0203010001";
const char* const kRsa_pkcs8 = "30820277020100300d06092a864886f70d0101010500048202613082025d02010002818100ecf98eaf294ea90e8940bb89bb458bed1117c78bf116419cb8f2d52bd315d9a6ffe59c762f4778a5ff9789f0b0eff38d0366d1b522e6f462b511a431a5a58a0c6836308a68e8921f9a3f4925ef1dd62268b617ed535a9d24b99c89248d071e406c78554c6625a37d246d062a6927b9c42cd8ac586f1f1e6c687baf01f8ceb2bb0203010001028180587b1db17d44e78e8be4e5a11991701b862450d7899efbc49eb1dcb24e507c42048e210b67bfd1e6793685c49eec190defd5363be1da46298cde17668a28f30fd2daa8d69bd2a16979916135b28124ce7f844bc66d209bcae23cf86a57d24545a4e811d35199ed2f7d05321ab1492bb682450705489df8dda28945845fb578a1024100ee8318f5dd9593ee5f7f6d386a9aab63833ad5af808a971c38b3259b0a8963fd0a9baf6e6529ed21eb82d60820de2e042dc5f79cbb6466febd21b283a86507b9024100fe599aecdb1268ec212c0143df4a7e011dbc96eebe6c153aa2d9eb61557fbee8bd4d0aaef3e49abe4b17196422a05392d292cebceb5f7964e33ff4f19718201302406a5692b3539693bccc6100ae0d8165f65914cb1e931db71f82fc04412aa274ace5a4e1c343613cf349902ca2b51cb9c16d32fd21a8beabd8d93403e60516fe2902410090866be0c04e82ae01df564cb1b94b45e916bb10a7c5147f0000219e6a0daf75e1f2bfea4f8d8b5c2ba0d17db3ab3431ce533b03e01e9d04f597e6dfcc021a5f024100a42f031f4a5eaf00803d6072a09cb7fdb77538ad9a0b73bd35e214be3bbb3e8b86fc27c4ea758fcedab7db31363b88aeade21f8b4b646ed8ee8a74f626f58ebe";
const char* const kRsa_cm_login_in = "015b58595e5f5c5d52535051565754554a4b48494e4f4c4d42434041464744457a7b78797e7f7c7d72737071767774756a6b68696e6f6c6d62636061666764651a1b18191e1f1c1d12131011161714150a0b08090e0f0c0d02030001060704053a3b38393e3f3c3d32333031363734352a2b28292e2f2c2d2223202126272425dadbd8d9dedfdcddd2d3d0d1d6d7d4d5cacbc8c9cecfcccdc2c3c0c1c6c7c4c5fafbf8";
const char* const kRsa_cm_login = "5df0c5ccb9e6edfa2d15cc1244e75e1ded42435fd8ca0af2d98419687baf370ebc331daf94176a6414b2a36665a1bb80ab785d9531986ff4468612416365e0c695d421725fa880a73ed2c9c2de3f2d91e166e7a204af112494895d156ce00a8015e8c2e4f1ed8cb20ef1ce5628192bdba5ff69e180da89e8666e921bbcc0a83d";
const char* const kRsa_scrambled = "75e012e241a63b11137ff2ac54e48b89b0a1d066a24cdcb8016e5c0f5e12c7e6939dc93a4962dbd8dbfa8fdad9c84a492fbe7ded4df9ea0edd6a0b305d6b38b71dd62268294ea90e8940bb89bb0872071f17c78bf116419cb8f2d52bd315d9a6ffe59c762f4778a5ff9789f0b0eff38d0366d1b522e6f462b511a431a5a58a0c";
struct RsaVector { const char* in; const char* dec; const char* enc; };
const RsaVector kRsaVectors[] = {
    {"120a11181f262d343b424950575e656c737a81888f969da4abb2b9c0c7ced5dce3eaf1f8ff060d141b222930373e454c535a61686f767d848b9299a0a7aeb5bcc3cad1d8dfe6edf4fb020910171e252c333a41484f565d646b727980878e959ca3aab1b8bfc6cdd4dbe2e9f0f7fe050c131a21282f363d444b525960676e757c", "b20363ce4e2114a0af17cc244ed2f412082110efc9a1090639d59f86fb9446f9222ef030db7a807d784384b13b368fa8f2cc8a28029c5ce3aebc3b672f4c9112dbca4f3a67cbd001c3ddf0663edcd087759aea5bc0a192f5f098e13631559c38b2e6ab241773760420080bc42dc57a25cebe625f89a071528be5b358320b2e0e", "ad5e274d8e95f7aadbb08f1436cd914758d568040a23a47cee3e40266359d39c28d0a25cc4e9155cfd599c7126d7a27148f622642c4f9df87b9920dfba3f7767384e1ba5f809170b60b432f51a542cf1dec1769ab10b7974b86740b1b5f781c5f4a8839c42eb4afa7104c19f8743a5c53950bd97fe0b20a20bbfc36dd3255fe5"},
    {"0102030405", "689d02e333efab24567ae29ec395555f5f1aa2576fa225796e246fa99604ae118cbbe32dc6e88f69847fbc71e93fc9cfe62ac0eead99b6d0468f79dc935391ee213026b57949362021758d1acc766a31f0a6116b814cc057a68313d808d4d339d26b9080c9787b33a7006c400ffc1f53874e304d3279bc2652ce913e34299ff8", "7045410cc5bf987c26462e69c902b00bfa42b196ff2190c29fe14d911c1e69b13450b658b8b1011210ef8285f0aa2ac8c685082394c6f7daf06e9f783bec892c367d23d21a23cc96bdb524124a5564faaa7e4e247452948b4ca4900fc41fdaf32b048a1b2ddb752d7f52c3925e80ab8df2e9fe8aeb58fb8dceafe2e5bc5efaa8"},
    {"0000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000ff0000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000002", "0de54757bda018763602739afdfb5b763e6e96e9929f8bb244ef6dbb2cbaccb5ad921d49cace87a9b6007fcd652d66c69dad670d2423df4a0ecfa9bd42cf45d4b85ac8f626f503c6d724f522254446b78c8c442f9ade9ab0a5e33c839a20afcbb034d143478349005fdf760456d6a468051a64bade7813b89c1c39617ee5ddf0", "698f68e90001213c1980bf813a7483812340fd92e880074e82d14274a978af2c1ce2426ae477a670c3efa6be53f1e9bc3cb25ef1798cc0e3c5c114a7343f7c3bb3d53a758f3fc8405340462e5c1d5969d7b80b775e11b2e14eb7e68822a2c794532a107c9594e3a5ea85c45302cb84a39e071561cf0b2aeda57337a358e268b6"},
    {"", "0000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000", "0000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"},
};

}  // namespace

// ======================================================================= BigInteger

JTEST(Crypto_BigInteger_Conversions) {
    for (const BigRow& r : kBigRows) {
        BigInteger* b = big(r.v);
        JCHECK_EQ(b->toString(), String(r.v));
        JCHECK_EQ(toHex(b->toByteArray()), std::string(r.bytes));
        JCHECK_EQ(b->bitLength(), r.bitLength);
        JCHECK_EQ(b->bitCount(), r.bitCount);
        JCHECK_EQ(b->signum(), r.signum);
        JCHECK_EQ(b->toString(16), String(r.hex));
        JCHECK_EQ(b->toString(36), String(r.r36));
        JCHECK_EQ(b->hashCode(), r.hash);
        JCHECK_EQ(b->intValue(), r.intValue);
        JCHECK_EQ(b->longValue(), r.longValue);
        // round trips
        JCHECK(new BigInteger(b->toByteArray()) != nullptr && (new BigInteger(b->toByteArray()))->equals(b));
        JCHECK((new BigInteger(String(r.hex), 16))->equals(b));
        JCHECK((new BigInteger(b->toString(2), 2))->equals(b));
    }
}

JTEST(Crypto_BigInteger_TwosComplementCtor) {
    for (const TwosRow& r : kTwosRows) {
        Array<int8_t>* bytes = fromHex(r.hex);
        JCHECK_EQ((new BigInteger(bytes))->toString(), String(r.twos));
        JCHECK_EQ((new BigInteger(1, bytes))->toString(), String(r.pos));
        JCHECK_EQ((new BigInteger(-1, bytes))->toString(), String(r.neg));
    }
    JCHECK_THROWS(NumberFormatException, new BigInteger(new Array<int8_t>(0)));
    JCHECK_THROWS(NumberFormatException, new BigInteger(2, fromHex("01")));
    JCHECK_THROWS(NumberFormatException, new BigInteger(0, fromHex("01")));
    JCHECK_EQ((new BigInteger(0, fromHex("0000")))->signum(), 0);
    JCHECK_EQ((new BigInteger(1, new Array<int8_t>(0)))->signum(), 0);
}

JTEST(Crypto_BigInteger_Arithmetic) {
    for (const ArithRow& r : kArithRows) {
        BigInteger* a = big(r.a);
        BigInteger* b = big(r.b);
        JCHECK_EQ(a->add(b)->toString(), String(r.add));
        JCHECK_EQ(a->subtract(b)->toString(), String(r.sub));
        JCHECK_EQ(a->multiply(b)->toString(), String(r.mul));
        JCHECK_EQ(a->divide(b)->toString(), String(r.div));
        JCHECK_EQ(a->remainder(b)->toString(), String(r.rem));
        JCHECK_EQ(a->mod(b->abs())->toString(), String(r.mod));
        JCHECK_EQ(a->gcd(b)->toString(), String(r.gcd));
        JCHECK_EQ(a->and_(b)->toString(), String(r.and_));
        JCHECK_EQ(a->or_(b)->toString(), String(r.or_));
        JCHECK_EQ(a->xor_(b)->toString(), String(r.xor_));
        JCHECK_EQ(a->compareTo(b), r.cmp);
        JCHECK_EQ(a->shiftLeft(5)->toString(), String(r.shl5));
        JCHECK_EQ(a->shiftRight(5)->toString(), String(r.shr5));
        JCHECK_EQ(a->shiftLeft(-5)->toString(), String(r.shr5));
        JCHECK_EQ(a->not_()->toString(), String(r.not_));
        JCHECK_EQ(a->pow(3)->toString(), String(r.pow3));
        Array<BigInteger*>* qr = a->divideAndRemainder(b);
        JCHECK_EQ((*qr)[0]->toString(), String(r.div));
        JCHECK_EQ((*qr)[1]->toString(), String(r.rem));
    }
    JCHECK_THROWS(ArithmeticException, big("5")->divide(BigInteger::ZERO));
    JCHECK_THROWS(ArithmeticException, big("5")->mod(big("-3")));
    JCHECK_THROWS(ArithmeticException, big("5")->mod(BigInteger::ZERO));
    JCHECK_THROWS(ArithmeticException, big("5")->pow(-1));
    JCHECK(big("-6")->testBit(1) && !big("-6")->testBit(0) && !big("-6")->testBit(2) && big("-6")->testBit(100));
    JCHECK_EQ(big("-6")->setBit(0)->toString(), String("-5"));
    JCHECK_EQ(big("5")->clearBit(0)->toString(), String("4"));
    JCHECK_EQ(big("5")->flipBit(1)->toString(), String("7"));
    JCHECK_EQ(big("-8")->getLowestSetBit(), 3);
    JCHECK_EQ(big("0")->getLowestSetBit(), -1);
    JCHECK_EQ(big("12")->andNot(big("4"))->toString(), String("8"));
    JCHECK_EQ(big("3")->min(big("-4"))->toString(), String("-4"));
    JCHECK_EQ(big("3")->max(big("-4"))->toString(), String("3"));
    JCHECK(big("97")->isProbablePrime(50));
    JCHECK(!big("91")->isProbablePrime(50));
    JCHECK_EQ(big("90")->nextProbablePrime()->toString(), String("97"));
    JCHECK(BigInteger::ONE->equals(BigInteger::valueOf(1)));
    JCHECK(!BigInteger::ONE->equals(big("2")));
    JCHECK_EQ(BigInteger::valueOf(INT64_MIN)->toString(), String("-9223372036854775808"));
    JCHECK_EQ(big("123456789012345678901234567890")->doubleValue(), 1.2345678901234568E29);
    JCHECK_EQ(big("-5")->doubleValue(), -5.0);
    JCHECK_EQ(big("255")->byteValue(), static_cast<int8_t>(-1));
    Number* asNumber = big("300");  // java.lang.Number
    JCHECK_EQ(asNumber->byteValue(), static_cast<int8_t>(44));
    JCHECK_EQ(asNumber->shortValue(), static_cast<int16_t>(300));
    Comparable<BigInteger*>* asComparable = big("7");
    JCHECK_EQ(asComparable->compareTo(big("8")), -1);
}

JTEST(Crypto_BigInteger_ModPow) {
    for (const ModPowRow& r : kModPowRows)
        JCHECK_EQ(big(r.base)->modPow(big(r.exp), big(r.mod))->toString(), String(r.result));
    for (const ModInvRow& r : kModInvRows) JCHECK_EQ(big(r.a)->modInverse(big(r.m))->toString(), String(r.result));
    JCHECK_THROWS(ArithmeticException, big("2")->modInverse(big("4")));
    JCHECK_THROWS(ArithmeticException, big("2")->modPow(big("3"), big("0")));
    JCHECK_THROWS(ArithmeticException, big("2")->modPow(big("3"), big("-7")));
}

JTEST(Crypto_BigInteger_Parse) {
    for (const RadixRow& r : kRadixRows)
        JCHECK_EQ((new BigInteger(String(r.s), r.radix))->toString(), String(r.dec));
    for (const ParseErrRow& r : kParseErrRows) {
        bool thrown = false;
        try {
            new BigInteger(String(r.s));
        } catch (NumberFormatException& e) {
            thrown = true;
            JCHECK_EQ(e.getMessage(), String(r.msg));
        }
        JCHECK(thrown);
    }
    JCHECK_THROWS(NumberFormatException, new BigInteger(String("12"), 37));
    // toString with a bad radix uses 10, like Java
    JCHECK_EQ(big("255")->toString(99), String("255"));
}

JTEST(Crypto_BigInteger_Random) {
    Random* r1 = new Random(42);
    Random* r2 = new Random(42);
    BigInteger* a = new BigInteger(100, r1);
    BigInteger* b = new BigInteger(100, r2);
    JCHECK(a->equals(b));
    JCHECK(a->bitLength() <= 100);
    JCHECK(a->signum() >= 0);
    // Java: new BigInteger(100, new Random(42)) (bytes from Random.nextBytes)
    JCHECK_EQ((new BigInteger(100, new Random(42)))->toString(), String("444809422218074872498625162792"));
    BigInteger* p = BigInteger::probablePrime(64, new Random());
    JCHECK_EQ(p->bitLength(), 64);
    JCHECK(p->isProbablePrime(64));
}

// ======================================================================= MessageDigest

JTEST(Crypto_MessageDigest_Vectors) {
    const char* inputs[] = {"", "abc", "The quick brown fox jumps over the lazy dog", "p\xC3\xA4ssw\xC3\xB6rd"};
    for (const DigestRow& r : kDigestRows) {
        const char* in = r.inputIndex == 0 ? inputs[0] : r.inputIndex == 3 ? inputs[1] : r.inputIndex == 43 ? inputs[2] : inputs[3];
        MessageDigest* md = MessageDigest::getInstance(String(r.alg));
        JCHECK_EQ(md->getAlgorithm(), String(r.alg));
        JCHECK_EQ(toHex(md->digest(bytesOf(in))), std::string(r.hex));
        // digest() resets: same result again, and byte-wise updates give the same digest
        for (const char* p = in; *p; p++) md->update(static_cast<int8_t>(*p));
        JCHECK_EQ(toHex(md->digest()), std::string(r.hex));
        JCHECK_EQ(md->getDigestLength() * 2, static_cast<int32_t>(std::strlen(r.hex)));
    }
}

JTEST(Crypto_MessageDigest_Api) {
    MessageDigest* md = MessageDigest::getInstance(String("SHA-1"));
    for (int i = 0; i < 1000000; i++) md->update(static_cast<int8_t>('a'));
    JCHECK_EQ(toHex(md->digest()), std::string("34aa973cd4c4daa4f61eeb2bdbad27316534016f"));
    md = MessageDigest::getInstance(String("SHA-256"));
    Array<int8_t>* buf = bytesOf("xxabcxx");
    md->reset();
    md->update(buf, 2, 3);
    JCHECK_EQ(toHex(md->digest()), std::string("ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad"));
    JCHECK_THROWS(IllegalArgumentException, md->update(buf, 5, 3));
    JCHECK_THROWS(IllegalArgumentException, md->update(nullptr, 0, 1));
    bool thrown = false;
    try {
        MessageDigest::getInstance(String("FOO"));
    } catch (NoSuchAlgorithmException& e) {
        thrown = true;
        JCHECK_EQ(e.getMessage(), String("FOO MessageDigest not available"));
        JCHECK_EQ(e.toString(), String("java.security.NoSuchAlgorithmException: FOO MessageDigest not available"));
    }
    JCHECK(thrown);
    JCHECK_EQ(md->toString(), String("SHA-256 Message Digest from SUN, <initialized>\n"));
    // AccountUtils.encodePassword: SHA-1 over UTF-8 bytes
    MessageDigest* sha1 = MessageDigest::getInstance(String("SHA-1"));
    sha1->update(String("p\xC3\xA4ssw\xC3\xB6rd").getBytes(String("UTF-8")));
    JCHECK_EQ(toHex(sha1->digest()), std::string("f517ddf1d32a112ff1ad55c66d1b12cb38e7e8f7"));
    // clone keeps the state
    MessageDigest* m1 = MessageDigest::getInstance(String("MD5"));
    m1->update(bytesOf("ab"));
    auto* m2 = jlang::cast<MessageDigest>(m1->clone());
    m1->update(bytesOf("c"));
    m2->update(bytesOf("c"));
    JCHECK_EQ(toHex(m1->digest()), toHex(m2->digest()));
    Array<int8_t>* out = new Array<int8_t>(20);
    MessageDigest* m3 = MessageDigest::getInstance(String("MD5"));
    JCHECK_EQ(m3->digest(out, 2, 16), 16);
    JCHECK_EQ(toHex(out).substr(4, 32), std::string("d41d8cd98f00b204e9800998ecf8427e"));
    JCHECK_THROWS(DigestException, m3->digest(out, 0, 8));
    JCHECK(MessageDigest::isEqual(fromHex("0102"), fromHex("0102")));
    JCHECK(!MessageDigest::isEqual(fromHex("0102"), fromHex("0103")));
}

// ======================================================================= SecureRandom / KeyGenerator

JTEST(Crypto_SecureRandom) {
    SecureRandom* r = new SecureRandom();
    Array<int8_t>* a = new Array<int8_t>(32);
    Array<int8_t>* b = new Array<int8_t>(32);
    r->nextBytes(a);
    r->nextBytes(b);
    JCHECK(toHex(a) != toHex(b));
    for (int i = 0; i < 1000; i++) {
        int32_t v = r->nextInt(10);
        JCHECK(v >= 0 && v < 10);
    }
    std::set<int32_t> seen;
    for (int i = 0; i < 200; i++) seen.insert(r->nextInt(4));
    JCHECK_EQ(seen.size(), static_cast<size_t>(4));
    double d = r->nextDouble();
    JCHECK(d >= 0.0 && d < 1.0);
    r->setSeed(12345);  // mixes entropy in; output stays unpredictable
    SecureRandom* s = SecureRandom::getInstance(String("SHA1PRNG"));
    JCHECK_EQ(s->getAlgorithm(), String("SHA1PRNG"));
    JCHECK_THROWS(NoSuchAlgorithmException, SecureRandom::getInstance(String("NOPE")));
    JCHECK_EQ(s->generateSeed(7)->length, 7);
    Random* asRandom = s;  // SecureRandom is a java.util.Random
    JCHECK(asRandom->nextLong() != asRandom->nextLong());
}

JTEST(Crypto_KeyGenerator_Blowfish) {
    KeyGenerator* g = KeyGenerator::getInstance(String("Blowfish"));
    JCHECK_EQ(g->getAlgorithm(), String("Blowfish"));
    SecretKey* k1 = g->generateKey();
    SecretKey* k2 = g->generateKey();
    JCHECK_EQ(k1->getEncoded()->length, 16);  // SunJCE default: 128 bits
    JCHECK_EQ(k1->getAlgorithm(), String("Blowfish"));
    JCHECK_EQ(k1->getFormat(), String("RAW"));
    JCHECK(toHex(k1->getEncoded()) != toHex(k2->getEncoded()));
    // getEncoded returns a copy
    Array<int8_t>* e = k1->getEncoded();
    (*e)[0] = static_cast<int8_t>((*e)[0] + 1);
    JCHECK(toHex(e) != toHex(k1->getEncoded()));
    Key* asKey = k1;  // SM_INIT: blowfishKey.getEncoded() through java.security.Key
    JCHECK_EQ(asKey->getEncoded()->length, 16);
    g->init(448);
    JCHECK_EQ(g->generateKey()->getEncoded()->length, 56);
    JCHECK_THROWS(IllegalArgumentException, g->init(20));
    JCHECK_THROWS(NoSuchAlgorithmException, KeyGenerator::getInstance(String("Twofish")));
    JCHECK_EQ(KeyGenerator::getInstance(String("AES"))->generateKey()->getEncoded()->length, 16);
    JCHECK(k1->equals(new SecretKey(k1->getEncoded(), String("Blowfish"))));
}

// ======================================================================= RSA

namespace {
RSAPrivateKey* javaPrivateKey(bool crt) {
    BigInteger* n = bigHex(kRsa_n);
    if (!crt) return new RSAPrivateKey(n, bigHex(kRsa_d));
    return new RSAPrivateKey(n, bigHex(kRsa_e), bigHex(kRsa_d), bigHex(kRsa_p), bigHex(kRsa_q), bigHex(kRsa_dp),
                             bigHex(kRsa_dq), bigHex(kRsa_qi));
}
}  // namespace

JTEST(Crypto_Rsa_JavaVectors) {
    RSAPublicKey* pub = new RSAPublicKey(bigHex(kRsa_n), bigHex(kRsa_e));
    JCHECK_EQ(toHex(pub->getModulus()->toByteArray()), std::string(kRsa_modbytes));
    JCHECK_EQ(toHex(pub->getEncoded()), std::string(kRsa_x509));
    JCHECK_EQ(toHex(javaPrivateKey(true)->getEncoded()), std::string(kRsa_pkcs8));
    JCHECK_EQ(pub->getAlgorithm(), String("RSA"));
    JCHECK_EQ(pub->getFormat(), String("X.509"));
    for (bool crt : {true, false}) {
        Cipher* dec = Cipher::getInstance(String("RSA/ECB/nopadding"));
        dec->init(Cipher::DECRYPT_MODE, javaPrivateKey(crt));
        Cipher* enc = Cipher::getInstance(String("RSA/ECB/NoPadding"));
        enc->init(Cipher::ENCRYPT_MODE, pub);
        for (const RsaVector& v : kRsaVectors) {
            Array<int8_t>* in = fromHex(v.in);
            JCHECK_EQ(toHex(dec->doFinal(in)), std::string(v.dec));
            JCHECK_EQ(toHex(enc->doFinal(in)), std::string(v.enc));
            // the cipher is reusable after doFinal
            JCHECK_EQ(toHex(dec->doFinal(in)), std::string(v.dec));
        }
        // CM_LOGIN: doFinal(data, 0, 128) on a 163-byte packet
        JCHECK_EQ(toHex(dec->doFinal(fromHex(kRsa_cm_login_in), 0, 128)), std::string(kRsa_cm_login));
    }
}

JTEST(Crypto_Rsa_Errors) {
    Cipher* dec = Cipher::getInstance(String("RSA/ECB/nopadding"));
    JCHECK_THROWS(IllegalStateException, dec->doFinal(new Array<int8_t>(1)));
    dec->init(Cipher::DECRYPT_MODE, javaPrivateKey(true));
    Array<int8_t>* mod = fromHex(std::string(kRsa_modbytes).substr(2));  // == modulus
    try {
        dec->doFinal(mod);
        JCHECK(false);
    } catch (BadPaddingException& e) {
        JCHECK_EQ(e.toString(), String("javax.crypto.BadPaddingException: Message is larger than modulus"));
    }
    try {
        dec->doFinal(new Array<int8_t>(129));
        JCHECK(false);
    } catch (IllegalBlockSizeException& e) {
        JCHECK_EQ(e.toString(),
                  String("javax.crypto.IllegalBlockSizeException: Data must not be longer than 128 bytes"));
    }
    // both are GeneralSecurityExceptions (CM_LOGIN catches that)
    JCHECK_THROWS(GeneralSecurityException, dec->doFinal(mod));
    // still usable after errors
    JCHECK_EQ(toHex(dec->doFinal(fromHex(kRsaVectors[1].in))), std::string(kRsaVectors[1].dec));
    try {
        Cipher::getInstance(String("Blowfish/CBC/Foo"));
        JCHECK(false);
    } catch (NoSuchAlgorithmException& e) {
        JCHECK_EQ(e.toString(),
                  String("java.security.NoSuchAlgorithmException: Cannot find any provider supporting Blowfish/CBC/Foo"));
    }
    JCHECK_THROWS(IllegalArgumentException, dec->doFinal(mod, 100, 100));
    JCHECK_THROWS(IllegalArgumentException, dec->doFinal(nullptr, 0, 0));
    JCHECK_THROWS(InvalidKeyException, dec->init(Cipher::DECRYPT_MODE, nullptr));
    JCHECK_THROWS(InvalidKeyException,
                  dec->init(Cipher::DECRYPT_MODE, KeyGenerator::getInstance(String("Blowfish"))->generateKey()));
}

JTEST(Crypto_Rsa_KeyGen_LoginServer) {
    // KeyGen.init(): RSAKeyGenParameterSpec(1024, F4), 10 key pairs in the login server
    KeyPairGenerator* g = KeyPairGenerator::getInstance(String("RSA"));
    RSAKeyGenParameterSpec* spec = new RSAKeyGenParameterSpec(1024, RSAKeyGenParameterSpec::F4);
    g->initialize(spec);
    JCHECK_EQ(RSAKeyGenParameterSpec::F4->toString(), String("65537"));
    JCHECK_EQ(RSAKeyGenParameterSpec::F0->toString(), String("3"));
    for (int k = 0; k < 3; k++) {
        KeyPair* kp = g->generateKeyPair();
        RSAPublicKey* pub = kp->getPublic();
        RSAPrivateKey* priv = kp->getPrivate();
        BigInteger* n = pub->getModulus();
        JCHECK_EQ(n->bitLength(), 1024);
        JCHECK(pub->getPublicExponent()->equals(RSAKeyGenParameterSpec::F4));
        JCHECK(priv->getModulus()->equals(n));
        // EncryptedRSAKeyPair relies on a 129-byte toByteArray with a leading 0x00
        Array<int8_t>* mb = n->toByteArray();
        JCHECK_EQ(mb->length, 0x81);
        JCHECK_EQ((*mb)[0], static_cast<int8_t>(0));
        // n = p*q, e*d == 1 mod lcm(p-1, q-1)
        JCHECK(priv->getPrimeP()->multiply(priv->getPrimeQ())->equals(n));
        BigInteger* p1 = priv->getPrimeP()->subtract(BigInteger::ONE);
        BigInteger* q1 = priv->getPrimeQ()->subtract(BigInteger::ONE);
        BigInteger* lcm = p1->multiply(q1)->divide(p1->gcd(q1));
        JCHECK(pub->getPublicExponent()->multiply(priv->getPrivateExponent())->mod(lcm)->equals(BigInteger::ONE));
        // raw RSA round trip (client encrypts with the public key, server decrypts)
        Cipher* enc = Cipher::getInstance(String("RSA/ECB/NoPadding"));
        enc->init(Cipher::ENCRYPT_MODE, pub);
        Cipher* dec = Cipher::getInstance(String("RSA/ECB/nopadding"));
        dec->init(Cipher::DECRYPT_MODE, priv);
        Array<int8_t>* msg = new Array<int8_t>(128);
        SecureRandom* rnd = new SecureRandom();
        rnd->nextBytes(msg);
        (*msg)[0] = 0;  // < modulus
        Array<int8_t>* c = enc->doFinal(msg);
        JCHECK_EQ(c->length, 128);
        JCHECK_EQ(toHex(dec->doFinal(c)), toHex(msg));
        // non-CRT private key gives the same result
        Cipher* dec2 = Cipher::getInstance(String("RSA"));
        (void)dec2;
        Cipher* dec3 = Cipher::getInstance(String("RSA/ECB/NoPadding"));
        dec3->init(Cipher::DECRYPT_MODE, new RSAPrivateKey(n, priv->getPrivateExponent()));
        JCHECK_EQ(toHex(dec3->doFinal(c)), toHex(msg));
    }
    JCHECK_THROWS(InvalidAlgorithmParameterException, g->initialize(new RSAKeyGenParameterSpec(256, RSAKeyGenParameterSpec::F4)));
    JCHECK_THROWS(InvalidAlgorithmParameterException, g->initialize(new RSAKeyGenParameterSpec(1024, BigInteger::valueOf(4))));
    JCHECK_THROWS(IllegalArgumentException, g->initialize(128));
    JCHECK_THROWS(NoSuchAlgorithmException, KeyPairGenerator::getInstance(String("DSA")));
    g->initialize(512);
    JCHECK_EQ(g->generateKeyPair()->getPublic()->getModulus()->bitLength(), 512);
}

JTEST(Crypto_Rsa_EncryptedModulusScramble) {
    // loginserver EncryptedRSAKeyPair.encryptModulus on Java's modulus, compared with Java
    Array<int8_t>* em = bigHex(kRsa_n)->toByteArray();
    if (em->length == 0x81 && (*em)[0] == 0) {
        auto* temp = new Array<int8_t>(0x80);
        arraycopy(em, 1, temp, 0, 0x80);
        em = temp;
    }
    for (int i = 0; i < 4; i++) {
        int8_t temp = (*em)[i];
        (*em)[i] = (*em)[0x4d + i];
        (*em)[0x4d + i] = temp;
    }
    for (int i = 0; i < 0x40; i++) (*em)[i] = static_cast<int8_t>((*em)[i] ^ (*em)[0x40 + i]);
    for (int i = 0; i < 4; i++) (*em)[0x0d + i] = static_cast<int8_t>((*em)[0x0d + i] ^ (*em)[0x34 + i]);
    for (int i = 0; i < 0x40; i++) (*em)[0x40 + i] = static_cast<int8_t>((*em)[0x40 + i] ^ (*em)[i]);
    JCHECK_EQ(toHex(em), std::string(kRsa_scrambled));
}

JTEST(Crypto_Rsa_Pkcs1) {
    RSAPublicKey* pub = new RSAPublicKey(bigHex(kRsa_n), bigHex(kRsa_e));
    Cipher* enc = Cipher::getInstance(String("RSA/ECB/PKCS1Padding"));
    enc->init(Cipher::ENCRYPT_MODE, pub);
    Cipher* dec = Cipher::getInstance(String("RSA"));
    dec->init(Cipher::DECRYPT_MODE, javaPrivateKey(true));
    Array<int8_t>* c = enc->doFinal(bytesOf("hello"));
    JCHECK_EQ(c->length, 128);
    JCHECK_EQ(String(dec->doFinal(c)), String("hello"));
    JCHECK_THROWS(BadPaddingException, enc->doFinal(new Array<int8_t>(118)));
}

JTEST(Crypto_JavaNames) {
    JCHECK_EQ(big("1")->getClass()->getName(), String("java.math.BigInteger"));
    JCHECK_EQ(Cipher::getInstance(String("RSA"))->getClass()->getName(), String("javax.crypto.Cipher"));
    JCHECK_EQ(MessageDigest::getInstance(String("MD5"))->getClass()->getName(), String("java.security.MessageDigest"));
}
