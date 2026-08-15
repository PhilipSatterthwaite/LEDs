// Reads a Kasa bulb's full sysinfo -- including its MAC -- straight off the
// bulb, using its own setup access point. No app, no cloud, no network
// registration required, which is the point: this works before the bulb has
// ever successfully joined anything.
//
// SETUP
//   1. Factory-reset the bulb: switch it off and on three times in quick
//      succession until it blinks. It now advertises its own open AP.
//   2. Find that network in your phone's WiFi list. It looks like
//      "TP-LINK_Smart Bulb_A1B2". Put the exact name in BULB_AP below.
//   3. Upload this. The ESP joins that AP and asks the bulb who it is.
//
// The reply is the same XOR autokey stream as the rest of the Kasa protocol,
// decrypted here on the fly, so the output is readable JSON. Look for
// "mic_mac" (or "mac") -- that is the address to register on servicenet.
//
// Monitor at 115200. Nothing to type.

#define BULB_AP      "TP-LINK_Smart Bulb_XXXX"   // exact name, including spaces
#define BULB_AP_PASS ""                          // setup AP is open

// Kasa devices sit at .1 on their own setup network. If the AT+CIPSTA readout
// below shows a different gateway, put that here instead.
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

const char PROBE[] = "{\"system\":{\"get_sysinfo\":{}}}";

// Streams the reply out decrypted rather than buffering it -- sysinfo runs to
// several hundred bytes, and there is no reason to hold it all in SRAM.
void readSysinfo(unsigned long timeout) {
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

  // The first four bytes are the length prefix and sit outside the cipher;
  // the key seeds at 0xAB on the first JSON byte.
  uint8_t key = 0xAB;
  for (uint16_t i = 0; i < len; i++) {
    while ((c = Serial1.read()) < 0) {
      if ((long)(millis() - deadline) >= 0) { Serial.println(F("\n(truncated)")); return; }
    }
    if (i < 4) continue;
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
  Serial.println(F("\n=== Kasa bulb info ==="));

  sendAT(F("AT+RST"), "ready", 10000);
  delay(1000);
  while (Serial1.available()) Serial1.read();

  sendAT(F("ATE0"), "OK", 3000);
  sendAT(F("AT+CWMODE=1"), "OK", 3000);

  Serial.print(F("\njoining \""));
  Serial.print(F(BULB_AP));
  Serial.println(F("\" ..."));

  Serial1.print(F("AT+CWJAP=\""));
  Serial1.print(F(BULB_AP));
  Serial1.print(F("\",\""));
  Serial1.print(F(BULB_AP_PASS));
  Serial1.println(F("\""));
  if (!waitFor("OK", 25000)) {
    Serial.println(F("\n!! could not join -- is the bulb blinking, and is"));
    Serial.println(F("   BULB_AP spelled exactly as it appears in the list?"));
    return;
  }

  // Shows the gateway, which is the bulb itself. If it is not 192.168.0.1,
  // that address goes in BULB_SETUP_IP.
  Serial.println(F("\n-- addresses on the bulb's own network --"));
  sendAT(F("AT+CIPSTA?"), "OK", 4000);

  sendAT(F("AT+CIPMUX=0"), "OK", 3000);

  Serial1.print(F("AT+CIPSTART=\"TCP\",\""));
  Serial1.print(F(BULB_SETUP_IP));
  Serial1.println(F("\",9999"));
  if (!waitFor("OK", 8000)) { Serial.println(F("\n!! bulb refused the connection")); return; }

  const uint16_t n = strlen(PROBE);
  Serial1.print(F("AT+CIPSEND="));
  Serial1.println(n + 4);
  if (!waitFor(">", 5000)) { Serial.println(F("\n!! no send prompt")); return; }

  Serial1.write((uint8_t)0);
  Serial1.write((uint8_t)0);
  Serial1.write((uint8_t)(n >> 8));
  Serial1.write((uint8_t)(n & 0xFF));

  uint8_t key = 0xAB;
  for (uint16_t i = 0; i < n; i++) { key ^= (uint8_t)PROBE[i]; Serial1.write(key); }

  readSysinfo(10000);

  Serial.println(F("\n=== done -- look for mic_mac ==="));
}

void loop() {}
