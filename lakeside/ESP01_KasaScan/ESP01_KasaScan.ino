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

// Networks that drop broadcast between clients often still pass unicast, so a
// silent broadcast is not the end of it. The sweep below sends the same probe
// to every address individually. It runs automatically -- over UDP there is no
// handshake to wait out, so the whole /24 takes about half a minute rather
// than the many minutes a TCP connect sweep would.
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

// Waits for a token while watching for replies at the same time. 254 probes
// generate a lot of routine AT chatter, and a hit arriving in the middle of it
// would be invisible -- so ordinary traffic is swallowed and only lines
// carrying +IPD are surfaced.
char lineBuf[100];
uint8_t lineN = 0;
uint8_t hits = 0;

bool pump(const char *token, unsigned long timeout) {
  const unsigned long end = millis() + timeout;
  uint8_t m = 0;
  while ((long)(millis() - end) < 0) {
    const int c = Serial1.read();
    if (c < 0) continue;

    if (c == '\n' || c == '\r') {
      lineBuf[lineN] = '\0';
      if (lineN > 4 && strstr(lineBuf, "+IPD")) {
        Serial.print(F("\n*** REPLY: "));
        Serial.println(lineBuf);
        hits++;
      }
      lineN = 0;
    } else if (lineN < sizeof(lineBuf) - 1) {
      lineBuf[lineN++] = (char)c;
    }

    if (c == token[m]) { if (token[++m] == '\0') return true; }
    else m = (c == token[0]) ? 1 : 0;
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

// Sends the probe to every address on the /24 individually. UDP mode 2 lets a
// single socket retarget per send, so this is one open connection and 254
// datagrams rather than 254 connection attempts.
void sweep() {
  Serial.println(F("\n\n=== unicast sweep ==="));
  Serial.print(F("probing "));
  Serial.print(F(SWEEP_PREFIX));
  Serial.println(F("1-254, about 30s -- a dot is 16 addresses"));

  sendAT(F("AT+CIPMUX=1"), "OK", 3000);
  sendAT(F("AT+CIPDINFO=1"), "OK", 3000);

  Serial1.print(F("AT+CIPSTART=0,\"UDP\",\""));
  Serial1.print(F(SWEEP_PREFIX));
  Serial1.println(F("1\",9999,9999,2"));
  if (!waitFor("OK", 6000)) { Serial.println(F("\n!! could not open UDP")); return; }

  const uint16_t n = strlen(PROBE);
  hits = 0;

  for (uint16_t host = 1; host < 255; host++) {
    Serial1.print(F("AT+CIPSEND=0,"));
    Serial1.print(n);
    Serial1.print(F(",\""));
    Serial1.print(F(SWEEP_PREFIX));
    Serial1.print(host);
    Serial1.println(F("\",9999"));

    if (!pump(">", 1200)) continue;
    writeEncrypted(false);
    pump("SEND OK", 1500);

    if ((host % 16) == 0) Serial.print('.');
  }

  Serial.println(F("\nlistening 5s for stragglers"));
  pump("\xFF", 5000);                    // no token -- just drains and reports

  Serial.print(F("\nreplies: "));
  Serial.println(hits);

  sendAT(F("AT+CIPCLOSE=0"), "OK", 3000);
  sendAT(F("AT+CIPDINFO=0"), "OK", 3000);
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

  // The netmask decides whether the sweep below even covers the right range.
  // A campus DHCP pool is often far wider than a /24, and the bulb can then
  // sit on a different third octet entirely -- in which case a silent sweep
  // means "looked in the wrong place", not "unreachable".
  Serial.println(F("\n-- subnet --"));
  if (!sendAT(F("AT+CIPSTA?"), "OK", 4000)) sendAT(F("AT+CIPSTA_CUR?"), "OK", 4000);
  Serial.println(F("\nif netmask is not 255.255.255.0, SWEEP_PREFIX is too narrow"));

  broadcastScan();
  sweep();

  Serial.println(F("\n=== done ==="));
}

void loop() {}
