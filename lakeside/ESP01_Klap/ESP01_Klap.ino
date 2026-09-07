// KLAP from the Mega: authenticate to the bulb and change its colour.
//
// The transport was proved already -- the ESP opened TCP to the bulb, sent a
// valid HTTP POST and got 200 with a session cookie. This adds the
// cryptography that was the actual obstacle, none of which needs the network:
// SHA-256 and AES-128-CBC run on the Mega, and auth_hash is a constant
// computed off-device, so no password appears in this sketch.
//
// SETUP
//   1. Run scratchpad/authhash.py, which asks for your TP-Link login and
//      prints a 32-byte array. Paste it over AUTH_HASH below.
//   2. Check BULB_IP -- re-provisioning moves it.
//   3. Upload. Power-cycle the ESP first.
//
// Crypto self-tests run first; if a vector fails nothing else is attempted,
// because a broken primitive is indistinguishable from a rejected password.

#include "sha256.h"
#include "aes128.h"
#include "klap.h"

#define WIFI_SSID  "servicenet"
#define WIFI_PASS  ""
#define BULB_IP    "10.9.47.3"

// AUTH_HASH lives in klap_secret.h, which is gitignored. Copy
// klap_secret.h.example over it and paste in the output of authhash.bat.
//
// It is kept out of the repository because sha256(sha1(email) + sha1(password))
// is only as strong as the password behind it: with the account email known, a
// short one can be recovered from the hash by brute force.
#include "klap_secret.h"

Klap klap;
uint8_t authHashRam[32];

// --- self-test ------------------------------------------------------------

bool vectorsPass() {
  uint8_t out[32], want[32];
  bool ok = true;

  static const uint8_t sha_abc[32] PROGMEM = {
    0xba,0x78,0x16,0xbf,0x8f,0x01,0xcf,0xea,0x41,0x41,0x40,0xde,0x5d,0xae,0x22,0x23,
    0xb0,0x03,0x61,0xa3,0x96,0x17,0x7a,0x9c,0xb4,0x10,0xff,0x61,0xf2,0x00,0x15,0xad};
  sha256((const uint8_t *)"abc", 3, nullptr, 0, nullptr, 0, out);
  memcpy_P(want, sha_abc, 32);
  if (memcmp(out, want, 32)) { Serial.println(F("  FAIL sha256")); ok = false; }

  static const uint8_t k[16] PROGMEM = {
    0x00,0x01,0x02,0x03,0x04,0x05,0x06,0x07,0x08,0x09,0x0a,0x0b,0x0c,0x0d,0x0e,0x0f};
  static const uint8_t pt[16] PROGMEM = {
    0x00,0x11,0x22,0x33,0x44,0x55,0x66,0x77,0x88,0x99,0xaa,0xbb,0xcc,0xdd,0xee,0xff};
  static const uint8_t ct[16] PROGMEM = {
    0x69,0xc4,0xe0,0xd8,0x6a,0x7b,0x04,0x30,0xd8,0xcd,0xb7,0x80,0x70,0xb4,0xc5,0x5a};
  uint8_t kk[16], blk[16], wc[16];
  memcpy_P(kk, k, 16); memcpy_P(blk, pt, 16); memcpy_P(wc, ct, 16);
  Aes128 aes; aes.setKey(kk); aes.encryptBlock(blk);
  if (memcmp(blk, wc, 16)) { Serial.println(F("  FAIL aes")); ok = false; }

  Serial.println(ok ? F("  vectors pass") : F("  VECTORS FAILED"));
  return ok;
}

// --- AT helpers used only during bring-up ----------------------------------

bool waitFor(const char *tok, unsigned long ms) {
  const unsigned long end = millis() + ms;
  uint8_t m = 0;
  while ((long)(millis() - end) < 0) {
    while (Serial1.available()) {
      const char c = Serial1.read();
      Serial.write(c);
      if (c == tok[m]) { if (tok[++m] == '\0') return true; }
      else m = (c == tok[0]) ? 1 : 0;
    }
  }
  return false;
}

void drain(unsigned long idle) {
  unsigned long last = millis();
  while (millis() - last < idle) {
    while (Serial1.available()) { Serial.write(Serial1.read()); last = millis(); }
  }
}

bool sendAT(const __FlashStringHelper *cmd, const char *expect, unsigned long ms) {
  Serial1.println(cmd);
  const bool ok = waitFor(expect, ms);
  drain(300);
  return ok;
}

// --- bulb commands ---------------------------------------------------------

bool setHsv(uint16_t hue, uint8_t sat, uint8_t bri) {
  char json[210];
  snprintf_P(json, sizeof(json),
    PSTR("{\"smartlife.iot.smartbulb.lightingservice\":{\"transition_light_state\":"
         "{\"ignore_default\":1,\"on_off\":1,\"hue\":%u,\"saturation\":%u,"
         "\"color_temp\":0,\"brightness\":%u,\"transition_period\":400}}}"),
    hue, sat, bri);

  const unsigned long t0 = millis();
  const bool ok = klap.request(json);
  Serial.print(F("  set_hsv("));
  Serial.print(hue); Serial.print(','); Serial.print(sat); Serial.print(',');
  Serial.print(bri); Serial.print(F(") -> "));
  Serial.print(ok ? F("ok") : F("FAILED"));
  Serial.print(F("  ")); Serial.print(millis() - t0); Serial.println(F("ms"));
  if (ok && klap.bodyLen) {
    Serial.print(F("    reply: "));
    Serial.println((char *)klap.buf);
  }
  return ok;
}

void setup() {
  Serial.begin(115200);
  delay(400);
  Serial.println(F("\n=== KLAP from the Mega ==="));

  Serial.println(F("\n-- crypto self-test --"));
  if (!vectorsPass()) { Serial.println(F("\nstopping.")); return; }

  memcpy_P(authHashRam, AUTH_HASH, 32);
  bool blank = true;
  for (uint8_t i = 0; i < 32; i++) if (authHashRam[i]) { blank = false; break; }
  if (blank) {
    Serial.println(F("\nAUTH_HASH is still the placeholder."));
    Serial.println(F("Run scratchpad/authhash.py and paste its output in."));
    return;
  }

  Serial1.begin(115200);
  sendAT(F("AT+RST"), "ready", 10000);
  delay(1200);
  drain(1000);
  sendAT(F("ATE0"), "OK", 3000);
  sendAT(F("AT+CWMODE=1"), "OK", 4000);
  sendAT(F("AT+CWQAP"), "OK", 5000);
  drain(1000);

  Serial.print(F("\njoining "));
  Serial.println(F(WIFI_SSID));
  Serial1.print(F("AT+CWJAP=\""));
  Serial1.print(F(WIFI_SSID));
  Serial1.print(F("\",\""));
  Serial1.print(F(WIFI_PASS));
  Serial1.println(F("\""));
  if (!waitFor("WIFI GOT IP", 25000)) { Serial.println(F("\n!! join failed")); return; }
  drain(1500);
  sendAT(F("AT+CIPMUX=0"), "OK", 4000);

  randomSeed(analogRead(A0) ^ micros());

  klap.esp = &Serial1;
  klap.host = BULB_IP;
  klap.authHash = authHashRam;

  Serial.print(F("\nhandshaking with "));
  Serial.println(F(BULB_IP));
  const unsigned long t0 = millis();
  if (!klap.handshake()) {
    Serial.println(F("\n!! handshake failed"));
    Serial.print(F("   last HTTP status: ")); Serial.println(klap.status);
    Serial.print(F("   cookie: ")); Serial.println(klap.cookie[0] ? klap.cookie : "(none)");
    Serial.println(F("   A 200 with the right cookie but a hash mismatch means"));
    Serial.println(F("   AUTH_HASH does not match this device."));
    return;
  }
  Serial.print(F("\n*** AUTHENTICATED in "));
  Serial.print(millis() - t0);
  Serial.println(F("ms ***"));
  Serial.print(F("  cookie ")); Serial.println(klap.cookie);
  Serial.print(F("  seq    ")); Serial.println(klap.seq);

  Serial.println(F("\nwatch the lamp:"));
  setHsv(0,   100, 60);  delay(1200);
  setHsv(120, 100, 60);  delay(1200);
  setHsv(240, 100, 60);  delay(1200);
  setHsv(30,   78, 40);

  Serial.println(F("\n=== done -- the Mega is driving the bulb ==="));
}

void loop() {}
