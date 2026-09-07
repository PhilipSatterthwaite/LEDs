// KLAP on the Mega -- step 1: prove the crypto before trusting it.
//
// A wrong hash or a broken MixColumns produces output indistinguishable from
// a wrong password, and that ambiguity has already cost this project hours.
// So both primitives are checked against published vectors first, on the
// device, and the result printed. Nothing else runs until they pass.
//
// SHA-256 vectors: FIPS 180-4 / NIST examples.
// AES-128 vector:  FIPS-197 Appendix C.1.
//
// Monitor at 115200. No network, no bulb -- pure arithmetic.

#include "sha256.h"
#include "aes128.h"

static bool allPassed = true;

void dump(const char *label, const uint8_t *b, uint8_t n) {
  Serial.print(F("  "));
  Serial.print(label);
  Serial.print(F(" "));
  for (uint8_t i = 0; i < n; i++) {
    if (b[i] < 16) Serial.print('0');
    Serial.print(b[i], HEX);
  }
  Serial.println();
}

bool check(const char *name, const uint8_t *got, const uint8_t *want, uint8_t n) {
  const bool ok = memcmp(got, want, n) == 0;
  Serial.print(ok ? F("  PASS  ") : F("  FAIL  "));
  Serial.println(name);
  if (!ok) {
    dump("got ", got, n);
    dump("want", want, n);
    allPassed = false;
  }
  return ok;
}

void testSha() {
  Serial.println(F("\n-- SHA-256 --"));
  uint8_t out[32];

  // ""
  static const uint8_t v0[32] PROGMEM = {
    0xe3,0xb0,0xc4,0x42,0x98,0xfc,0x1c,0x14,0x9a,0xfb,0xf4,0xc8,0x99,0x6f,0xb9,0x24,
    0x27,0xae,0x41,0xe4,0x64,0x9b,0x93,0x4c,0xa4,0x95,0x99,0x1b,0x78,0x52,0xb8,0x55};
  uint8_t want[32];
  sha256(nullptr, 0, nullptr, 0, nullptr, 0, out);
  memcpy_P(want, v0, 32);
  check("empty string", out, want, 32);

  // "abc"
  static const uint8_t v1[32] PROGMEM = {
    0xba,0x78,0x16,0xbf,0x8f,0x01,0xcf,0xea,0x41,0x41,0x40,0xde,0x5d,0xae,0x22,0x23,
    0xb0,0x03,0x61,0xa3,0x96,0x17,0x7a,0x9c,0xb4,0x10,0xff,0x61,0xf2,0x00,0x15,0xad};
  sha256((const uint8_t *)"abc", 3, nullptr, 0, nullptr, 0, out);
  memcpy_P(want, v1, 32);
  check("abc", out, want, 32);

  // 56-byte message: exercises the padding path that spills into a second block
  static const char msg[] = "abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq";
  static const uint8_t v2[32] PROGMEM = {
    0x24,0x8d,0x6a,0x61,0xd2,0x06,0x38,0xb8,0xe5,0xc0,0x26,0x93,0x0c,0x3e,0x60,0x39,
    0xa3,0x3c,0xe4,0x59,0x64,0xff,0x21,0x67,0xf6,0xec,0xed,0xd4,0x19,0xdb,0x06,0xc1};
  sha256((const uint8_t *)msg, sizeof(msg) - 1, nullptr, 0, nullptr, 0, out);
  memcpy_P(want, v2, 32);
  check("56-byte message", out, want, 32);

  // Three-part update, which is the shape KLAP actually uses.
  sha256((const uint8_t *)"a", 1, (const uint8_t *)"b", 1, (const uint8_t *)"c", 1, out);
  memcpy_P(want, v1, 32);
  check("split into three parts", out, want, 32);
}

void testAes() {
  Serial.println(F("\n-- AES-128 --"));

  static const uint8_t key[16] PROGMEM = {
    0x00,0x01,0x02,0x03,0x04,0x05,0x06,0x07,0x08,0x09,0x0a,0x0b,0x0c,0x0d,0x0e,0x0f};
  static const uint8_t pt[16] PROGMEM = {
    0x00,0x11,0x22,0x33,0x44,0x55,0x66,0x77,0x88,0x99,0xaa,0xbb,0xcc,0xdd,0xee,0xff};
  static const uint8_t ct[16] PROGMEM = {
    0x69,0xc4,0xe0,0xd8,0x6a,0x7b,0x04,0x30,0xd8,0xcd,0xb7,0x80,0x70,0xb4,0xc5,0x5a};

  uint8_t k[16], block[16], want[16];
  memcpy_P(k, key, 16);
  memcpy_P(block, pt, 16);
  memcpy_P(want, ct, 16);

  Aes128 aes;
  aes.setKey(k);

  aes.encryptBlock(block);
  check("FIPS-197 encrypt", block, want, 16);

  aes.decryptBlock(block);
  memcpy_P(want, pt, 16);
  check("FIPS-197 decrypt", block, want, 16);

  // CBC round trip over two blocks, which is what request bodies use.
  uint8_t iv[16];
  for (uint8_t i = 0; i < 16; i++) iv[i] = i * 7 + 1;

  uint8_t buf[32], orig[32];
  for (uint8_t i = 0; i < 32; i++) buf[i] = orig[i] = (uint8_t)(i * 3 + 5);

  aes.cbcEncrypt(buf, 32, iv);
  bool changed = memcmp(buf, orig, 32) != 0;
  Serial.print(changed ? F("  PASS  ") : F("  FAIL  "));
  Serial.println(F("CBC encrypt altered the buffer"));
  if (!changed) allPassed = false;

  aes.cbcDecrypt(buf, 32, iv);
  check("CBC round trip", buf, orig, 32);
}

void setup() {
  Serial.begin(115200);
  delay(400);
  Serial.println(F("\n=== KLAP crypto self-test ==="));

  const unsigned long t0 = micros();
  testSha();
  testAes();
  const unsigned long dt = micros() - t0;

  Serial.println();
  Serial.print(F("elapsed: "));
  Serial.print(dt / 1000.0, 1);
  Serial.println(F(" ms"));

  // Rough budget check: KLAP does a handful of hashes and one AES pass per
  // command, so per-command cost needs to stay well under a frame time.
  uint8_t out[32];
  const unsigned long t1 = micros();
  for (uint8_t i = 0; i < 10; i++) sha256((const uint8_t *)"abc", 3, nullptr, 0, nullptr, 0, out);
  Serial.print(F("sha256 x10: "));
  Serial.print((micros() - t1) / 1000.0, 1);
  Serial.println(F(" ms"));

  Serial.println();
  Serial.println(allPassed ? F("*** ALL VECTORS PASS -- crypto is sound ***")
                           : F("*** FAILURES ABOVE -- do not build on this ***"));
}

void loop() {}
