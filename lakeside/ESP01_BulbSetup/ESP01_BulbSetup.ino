// Provisions a Kasa bulb onto a WiFi network -- no phone, no app, no cloud.
//
// The Kasa app does exactly one thing that mattered: hand the bulb an SSID and
// password. That is a normal command in the same local protocol everything
// else here speaks, so there is no reason it has to come from a phone.
//
//   {"netif":{"set_stainfo":{"ssid":"...","password":"...","key_type":N}}}
//
// The bulb answers err_code 0, drops its setup AP, and joins the network.
//
// HOW TO USE
//   1. Reset the bulb: off and on three times quickly, until it blinks.
//   2. Set TARGET_* below to the network you want it on.
//   3. Upload. The ESP finds the setup AP, joins it, and sends the command.
//
// Afterwards the bulb is on TARGET_SSID and the main sketch finds it there by
// MAC. Nothing in this file is needed again unless you move it.

// The network the BULB should end up on.
//   key_type: 0 open, 1 WEP, 2 WPA, 3 WPA2
//
// LakesideLEDs is the ESP's own AP -- a handful of hosts, no client isolation,
// and the ESP's DHCP table names the bulb outright. servicenet would also work
// here (key_type 0), but it is a /15 that almost certainly isolates clients
// from each other, so the bulb would be joined and still unreachable.
#define TARGET_SSID "LakesideLEDs"
#define TARGET_PASS "lakeside450"
#define TARGET_KEY  3

// Leave empty to scan for the bulb's setup AP automatically.
#define BULB_AP       ""
#define BULB_SETUP_IP "192.168.0.1"

#define ESP_BAUD 115200

bool waitFor(const char *token, unsigned long timeout) {
  unsigned long start = millis();
  uint8_t m = 0;
  while (millis() - start < timeout) {
    while (Serial1.available()) {
      char c = Serial1.read();
      Serial.write(c);
      if (c == token[m]) { if (token[++m] == '\0') return true; }
      else m = (c == token[0]) ? 1 : 0;
    }
  }
  return false;
}

bool sendAT(const __FlashStringHelper *cmd, const char *expect, unsigned long ms) {
  Serial1.println(cmd);
  return waitFor(expect, ms);
}

// Reads until the line has been quiet for idleMs.
//
// Necessary because a scan keeps streaming after a match is found, and the ESP
// also announces its own reconnections unprompted. Issuing the next command
// while either is still arriving makes every following waitFor match the
// PREVIOUS command's OK -- so CIPSTART appears to succeed, and the failure only
// surfaces later as "link is not valid".
void drain(unsigned long idleMs) {
  unsigned long last = millis();
  while (millis() - last < idleMs) {
    while (Serial1.available()) { Serial.write(Serial1.read()); last = millis(); }
  }
}

bool findBulbAP(char *out, uint8_t size) {
  Serial1.println(F("AT+CWLAP"));
  const unsigned long deadline = millis() + 25000;
  const char *tok = "+CWLAP:(";
  uint8_t m = 0;
  int c;

  while ((long)(millis() - deadline) < 0) {
    if ((c = Serial1.read()) < 0) continue;
    Serial.write(c);

    if (c == tok[m]) {
      if (tok[++m] != '\0') continue;
      m = 0;
      while ((long)(millis() - deadline) < 0) {
        if ((c = Serial1.read()) < 0) continue;
        Serial.write(c);
        if (c == '"') break;
      }
      uint8_t n = 0;
      while ((long)(millis() - deadline) < 0) {
        if ((c = Serial1.read()) < 0) continue;
        Serial.write(c);
        if (c == '"') break;
        if (n < size - 1) out[n++] = (char)c;
      }
      out[n] = '\0';
      if (strstr(out, "TP-LINK") || strstr(out, "TP-Link") || strstr(out, "Bulb"))
        return true;
    } else {
      m = (c == tok[0]) ? 1 : 0;
    }
  }
  return false;
}

// 4-byte big-endian length, then the JSON through an XOR autokey stream seeded
// at 0xAB -- each ciphertext byte becomes the key for the next.
void writeEncrypted(const char *s) {
  const uint16_t n = strlen(s);
  Serial1.write((uint8_t)0);
  Serial1.write((uint8_t)0);
  Serial1.write((uint8_t)(n >> 8));
  Serial1.write((uint8_t)(n & 0xFF));

  uint8_t key = 0xAB;
  for (uint16_t i = 0; i < n; i++) { key ^= (uint8_t)s[i]; Serial1.write(key); }
}

void readReply(unsigned long timeout) {
  const unsigned long deadline = millis() + timeout;
  const char *tok = "+IPD,";
  uint8_t m = 0;
  int c;

  while (tok[m]) {
    if ((long)(millis() - deadline) >= 0) { Serial.println(F("\n(no reply)")); return; }
    if (!Serial1.available()) continue;
    c = Serial1.read();
    if (c == tok[m]) m++; else m = (c == tok[0]) ? 1 : 0;
  }

  uint16_t len = 0;
  while ((long)(millis() - deadline) < 0) {
    if ((c = Serial1.read()) < 0) continue;
    if (c == ':') break;
    if (c >= '0' && c <= '9') len = len * 10 + (c - '0');
  }

  Serial.println(F("\n\n--- bulb says ---"));
  uint8_t key = 0xAB;
  for (uint16_t i = 0; i < len; i++) {
    while ((c = Serial1.read()) < 0) {
      if ((long)(millis() - deadline) >= 0) { Serial.println(F("\n(truncated)")); return; }
    }
    if (i < 4) continue;                       // length prefix sits outside the cipher
    const uint8_t e = (uint8_t)c;
    Serial.write((char)(e ^ key));
    key = e;
  }
  Serial.println();
}

void setup() {
  Serial.begin(115200);
  Serial1.begin(ESP_BAUD);
  delay(400);

  Serial.println(F("\n=== Kasa bulb provisioning ==="));
  Serial.print(F("target network: "));
  Serial.println(F(TARGET_SSID));

  sendAT(F("AT+RST"), "ready", 10000);
  delay(1000);
  while (Serial1.available()) Serial1.read();

  sendAT(F("ATE0"), "OK", 3000);
  sendAT(F("AT+CWMODE=1"), "OK", 3000);

  // Saved credentials make the ESP rejoin servicenet on its own after a reset,
  // and those announcements would land in the middle of the exchange below.
  sendAT(F("AT+CWQAP"), "OK", 5000);
  drain(1500);

  char ssid[48];
  if (BULB_AP[0]) {
    strncpy(ssid, BULB_AP, sizeof(ssid) - 1);
    ssid[sizeof(ssid) - 1] = '\0';
  } else {
    Serial.println(F("\n-- scanning for the setup AP --"));
    if (!findBulbAP(ssid, sizeof(ssid))) {
      Serial.println(F("\n\n!! no TP-LINK network in range."));
      Serial.println(F("   Reset the bulb -- off/on three times quickly until it"));
      Serial.println(F("   blinks -- then run this again. Setup mode times out."));
      return;
    }
  }

  drain(1500);              // let the rest of the scan finish arriving

  Serial.print(F("\n\njoining \""));
  Serial.print(ssid);
  Serial.println(F("\" ..."));

  Serial1.print(F("AT+CWJAP=\""));
  Serial1.print(ssid);
  Serial1.println(F("\",\"\""));
  if (!waitFor("WIFI GOT IP", 25000)) { Serial.println(F("\n!! could not join the setup AP")); return; }
  drain(1200);

  sendAT(F("AT+CIPMUX=0"), "OK", 3000);
  drain(400);

  Serial1.print(F("AT+CIPSTART=\"TCP\",\""));
  Serial1.print(F(BULB_SETUP_IP));
  Serial1.println(F("\",9999"));
  // CONNECT is specific to this command; OK is not, and matching a stray one
  // is exactly what produced "link is not valid" last time.
  if (!waitFor("CONNECT", 8000)) { Serial.println(F("\n!! bulb refused the connection")); return; }
  drain(400);

  char json[200];
  snprintf_P(json, sizeof(json),
    PSTR("{\"netif\":{\"set_stainfo\":{\"ssid\":\"%s\",\"password\":\"%s\",\"key_type\":%d}}}"),
    TARGET_SSID, TARGET_PASS, TARGET_KEY);

  Serial.println(F("\n\nsending credentials ..."));

  Serial1.print(F("AT+CIPSEND="));
  Serial1.println(strlen(json) + 4);
  if (!waitFor(">", 5000)) { Serial.println(F("\n!! no send prompt")); return; }

  writeEncrypted(json);
  readReply(10000);

  Serial.println(F("\n=== done ==="));
  Serial.println(F("err_code 0 means the bulb accepted it and is joining now."));
  Serial.print(F("It should leave setup mode -- \""));
  Serial.print(ssid);
  Serial.println(F("\" disappears from the WiFi list."));
  Serial.println(F("Then upload 450LED_WiFi and check the -- kasa bulb -- section."));
}

void loop() {}
