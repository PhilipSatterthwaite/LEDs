// Can the Mega reach the bulb and speak HTTP to it?
//
// This answers the networking half of the question on its own, with no crypto
// involved. KLAP's first step is a plain HTTP POST:
//
//   POST /app/handshake1   body = 16 random bytes
//   -> 200, Set-Cookie: TP_SESSIONID=...
//      body = 16 bytes remote_seed + 32 bytes server_hash
//
// Sending that needs nothing but TCP. Only the REPLY has to be verified
// against sha256(local_seed + remote_seed + auth_hash), and that comes later.
//
// If this prints 48 bytes and a session cookie, the ESP can reach the bulb and
// carry HTTP to it, and what remains is implementing SHA-256 and AES-128-CBC
// on the Mega -- work, but ordinary work.
//
// Monitor at 115200. Power-cycle the ESP first.

#define WIFI_SSID  "servicenet"
#define WIFI_PASS  ""              // open network

#define BULB_IP    "10.9.47.3"
#define BULB_PORT  80

#define ESP_BAUD   115200

bool waitFor(const char *token, unsigned long timeout) {
  const unsigned long end = millis() + timeout;
  uint8_t m = 0;
  while ((long)(millis() - end) < 0) {
    while (Serial1.available()) {
      char c = Serial1.read();
      Serial.write(c);
      if (c == token[m]) { if (token[++m] == '\0') return true; }
      else m = (c == token[0]) ? 1 : 0;
    }
  }
  return false;
}

void drain(unsigned long idleMs) {
  unsigned long last = millis();
  while (millis() - last < idleMs) {
    while (Serial1.available()) { Serial.write(Serial1.read()); last = millis(); }
  }
}

bool sendAT(const __FlashStringHelper *cmd, const char *expect, unsigned long ms) {
  Serial1.println(cmd);
  const bool ok = waitFor(expect, ms);
  drain(300);
  return ok;
}

void setup() {
  Serial.begin(115200);
  Serial1.begin(ESP_BAUD);
  delay(400);
  Serial.println(F("\n=== can the Mega reach the bulb over HTTP? ==="));

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

  Serial.println(F("\n-- our address --"));
  sendAT(F("AT+CIFSR"), "OK", 5000);

  sendAT(F("AT+CIPMUX=0"), "OK", 4000);

  Serial.print(F("\nopening TCP to "));
  Serial.print(F(BULB_IP));
  Serial.print(':');
  Serial.println(BULB_PORT);

  Serial1.print(F("AT+CIPSTART=\"TCP\",\""));
  Serial1.print(F(BULB_IP));
  Serial1.print(F("\","));
  Serial1.println(BULB_PORT);
  if (!waitFor("CONNECT", 10000)) {
    Serial.println(F("\n\n!! could not open the connection."));
    Serial.println(F("   That would be a networking problem after all."));
    return;
  }
  drain(500);

  Serial.println(F("\n\n*** TCP CONNECTED -- the Mega can reach the bulb ***"));

  // 16 bytes of local_seed. Not cryptographically strong here; this run only
  // establishes that the exchange happens at all.
  uint8_t seed[16];
  randomSeed(analogRead(A0) ^ micros());
  for (uint8_t i = 0; i < 16; i++) seed[i] = random(256);

  char head[160];
  const int hlen = snprintf_P(head, sizeof(head),
    PSTR("POST /app/handshake1 HTTP/1.1\r\n"
         "Host: %s\r\n"
         "Content-Type: application/octet-stream\r\n"
         "Content-Length: 16\r\n"
         "Connection: close\r\n\r\n"),
    BULB_IP);

  Serial.println(F("\nsending handshake1 ..."));
  Serial1.print(F("AT+CIPSEND="));
  Serial1.println(hlen + 16);
  if (!waitFor(">", 5000)) { Serial.println(F("\n!! no send prompt")); return; }

  Serial1.write((const uint8_t *)head, hlen);
  Serial1.write(seed, 16);

  if (!waitFor("SEND OK", 6000)) { Serial.println(F("\n!! send failed")); return; }

  Serial.println(F("\n\n-- reply --"));

  // Echo everything, then summarise. The body is binary, so the interesting
  // part is the byte count and whether a session cookie came back.
  uint16_t bytes = 0;
  bool sawCookie = false, sawOk = false;
  const char *ck = "TP_SESSIONID";
  const char *ok = "200 OK";
  uint8_t mc = 0, mo = 0;

  const unsigned long end = millis() + 12000;
  while ((long)(millis() - end) < 0) {
    if (!Serial1.available()) continue;
    const char c = Serial1.read();
    Serial.write(c);
    bytes++;
    if (c == ck[mc]) { if (ck[++mc] == '\0') { sawCookie = true; mc = 0; } }
    else mc = (c == ck[0]) ? 1 : 0;
    if (c == ok[mo]) { if (ok[++mo] == '\0') { sawOk = true; mo = 0; } }
    else mo = (c == ok[0]) ? 1 : 0;
  }

  Serial.println();
  Serial.println(F("\n=== summary ==="));
  Serial.print(F("bytes received : ")); Serial.println(bytes);
  Serial.print(F("HTTP 200       : ")); Serial.println(sawOk ? F("yes") : F("no"));
  Serial.print(F("session cookie : ")); Serial.println(sawCookie ? F("yes") : F("no"));
  Serial.println();
  if (sawOk && sawCookie) {
    Serial.println(F("The Mega reaches the bulb and speaks HTTP to it."));
    Serial.println(F("What is left is SHA-256 and AES-128-CBC on the Mega."));
  } else {
    Serial.println(F("Connected, but the exchange did not complete as expected."));
  }
}

void loop() {}
