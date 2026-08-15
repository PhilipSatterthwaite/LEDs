// Finds a TP-Link Kasa device on the network and prints its IP.
//
// Use when the Kasa app cannot see the bulb -- typically because the bulb
// joined a network with no route to TP-Link's cloud, so the app never got its
// check-in. Local control does not need the cloud, only the address.
//
// Kasa devices answer a UDP broadcast on port 9999. The datagram is the same
// XOR autokey stream the TCP protocol uses (seeded at 0xAB, each ciphertext
// byte keying the next) but WITHOUT the 4-byte length prefix that TCP adds.
//
// AT+CIPDINFO=1 makes the ESP report the sender's address on each +IPD line,
// which is the whole point of the exercise.
//
// Monitor at 115200. Nothing to type.

#define WIFI_SSID  "servicenet"
#define WIFI_PASS  ""             // open network

// If the broadcast finds nothing, set this to sweep the subnet one address at
// a time instead. Slow -- roughly 4-8 minutes for a /24 -- but it works on
// networks that drop broadcast traffic between clients.
#define SWEEP        0
#define SWEEP_PREFIX "10.8.251."  // first three octets of the ESP's own STAIP

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

// Quiet variant -- the sweep would otherwise bury the result in AT chatter.
bool quietAT(unsigned long timeout, const char *token) {
  unsigned long start = millis();
  uint8_t m = 0;
  while (millis() - start < timeout) {
    while (Serial1.available()) {
      char c = Serial1.read();
      if (c == token[m]) { if (token[++m] == '\0') return true; }
      else m = (c == token[0]) ? 1 : 0;
    }
  }
  return false;
}

const char PROBE[] = "{\"system\":{\"get_sysinfo\":{}}}";

void writeEncrypted(bool withLength) {
  const uint16_t n = strlen(PROBE);
  if (withLength) {                     // TCP form only
    Serial1.write((uint8_t)0); Serial1.write((uint8_t)0);
    Serial1.write((uint8_t)(n >> 8)); Serial1.write((uint8_t)(n & 0xFF));
  }
  uint8_t key = 0xAB;
  for (uint16_t i = 0; i < n; i++) { key ^= (uint8_t)PROBE[i]; Serial1.write(key); }
}

void broadcastScan() {
  Serial.println(F("\n=== UDP broadcast discovery ==="));
  sendAT(F("AT+CIPMUX=1"), "OK", 3000);
  sendAT(F("AT+CIPDINFO=1"), "OK", 3000);

  // mode 2 lets the peer address change, so a unicast reply still arrives.
  Serial1.println(F("AT+CIPSTART=0,\"UDP\",\"255.255.255.255\",9999,9999,2"));
  if (!waitFor("OK", 6000)) { Serial.println(F("\n!! could not open UDP")); return; }

  Serial1.print(F("AT+CIPSEND=0,"));
  Serial1.println(strlen(PROBE));
  if (!waitFor(">", 4000)) { Serial.println(F("\n!! no send prompt")); return; }
  writeEncrypted(false);
  waitFor("OK", 5000);

  Serial.println(F("\n\nlistening 8s -- look for a +IPD line, the IP after the"));
  Serial.println(F("length is the device. Payload is XOR'd, so it reads as junk."));

  unsigned long start = millis();
  while (millis() - start < 8000) while (Serial1.available()) Serial.write(Serial1.read());

  sendAT(F("AT+CIPCLOSE=0"), "OK", 3000);
  sendAT(F("AT+CIPDINFO=0"), "OK", 3000);
}

void sweep() {
#if SWEEP
  Serial.println(F("\n=== subnet sweep ==="));
  sendAT(F("AT+CIPMUX=0"), "OK", 3000);

  for (uint8_t host = 2; host < 255; host++) {
    Serial1.print(F("AT+CIPSTART=\"TCP\",\""));
    Serial1.print(F(SWEEP_PREFIX));
    Serial1.print(host);
    Serial1.println(F("\",9999"));

    if (quietAT(1500, "CONNECT")) {
      Serial.print(F("\n*** something answers on port 9999 at "));
      Serial.print(F(SWEEP_PREFIX));
      Serial.println(host);
      quietAT(1000, "OK");
      Serial1.println(F("AT+CIPCLOSE"));
      quietAT(2000, "OK");
    } else {
      Serial1.println(F("AT+CIPCLOSE"));
      quietAT(600, "OK");
      if ((host % 16) == 0) { Serial.print('.'); }
    }
  }
  Serial.println(F("\nsweep done"));
#endif
}

void setup() {
  Serial.begin(115200);
  Serial1.begin(ESP_BAUD);
  delay(400);
  Serial.println(F("\n=== Kasa device scan ==="));

  sendAT(F("AT+RST"), "ready", 10000);
  delay(1000);
  while (Serial1.available()) Serial1.read();

  sendAT(F("ATE0"), "OK", 3000);
  sendAT(F("AT+CWMODE=1"), "OK", 3000);

  Serial1.print(F("AT+CWJAP=\""));
  Serial1.print(F(WIFI_SSID));
  Serial1.print(F("\",\""));
  Serial1.print(F(WIFI_PASS));
  Serial1.println(F("\""));
  if (!waitFor("OK", 20000)) { Serial.println(F("\n!! join failed")); return; }

  Serial.println(F("\n-- our address --"));
  sendAT(F("AT+CIFSR"), "OK", 4000);

  broadcastScan();
  sweep();

  Serial.println(F("\n=== done ==="));
}

void loop() {}
