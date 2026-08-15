// Answers one question definitively: can the ESP open a TCP connection to a
// client of its own SoftAP?
//
// Every previous attempt was tangled up with MQTT traffic, a running web
// server, and my own commands overlapping the ESP's replies -- so a failure
// could never be attributed with confidence. This does nothing else: brings up
// the AP, waits for the bulb, and makes ONE connection attempt, waiting for a
// definite outcome at every step rather than a fixed delay.
//
// Crucially it does not close the link first. AT+CIPCLOSE on a link that is
// not open answers UNLINK/ERROR, and that reply arriving late is exactly what
// could have been misread as CIPSTART failing.
//
// The bulb must already be provisioned onto AP_SSID.
// Monitor at 115200. Nothing to type. Power-cycle the ESP first.

#define AP_SSID    "LakesideLEDs"
#define AP_PASS    "lakeside450"
#define BULB_MAC   "24:2f:d0:59:10:34"
#define BULB_PORT  9999
#define BULB_LINK  3

#define ESP_BAUD   115200

const char PROBE[] = "{\"system\":{\"get_sysinfo\":{}}}";

// Returns the index of whichever token arrives first, or -1 on timeout. Waiting
// on a set rather than one string is the point: we learn what actually
// happened instead of only whether the hoped-for thing did.
int waitAny(const char *const *tok, uint8_t count, unsigned long timeout) {
  uint8_t m[5] = {0};
  const unsigned long end = millis() + timeout;
  while ((long)(millis() - end) < 0) {
    const int c = Serial1.read();
    if (c < 0) continue;
    Serial.write(c);
    for (uint8_t i = 0; i < count; i++) {
      if (c == tok[i][m[i]]) { if (tok[i][++m[i]] == '\0') return i; }
      else m[i] = (c == tok[i][0]) ? 1 : 0;
    }
  }
  return -1;
}

bool waitFor(const char *token, unsigned long timeout) {
  const char *one[1] = { token };
  return waitAny(one, 1, timeout) == 0;
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
  drain(500);
  return ok;
}

// Looks for the bulb among the AP's clients. Returns its address, or empty.
bool findBulb(char *out, uint8_t size) {
  out[0] = '\0';
  Serial1.println(F("AT+CWLIF"));

  char line[48];
  uint8_t n = 0;
  const unsigned long deadline = millis() + 5000;

  while ((long)(millis() - deadline) < 0) {
    const int c = Serial1.read();
    if (c < 0) continue;
    Serial.write(c);

    if (c == '\n' || c == '\r') {
      line[n] = '\0';
      for (char *p = line; *p; p++) if (*p >= 'A' && *p <= 'Z') *p += 32;
      char *comma = strchr(line, ',');
      if (comma && strstr(comma + 1, BULB_MAC)) {
        *comma = '\0';
        char *ip = line;
        while (*ip && (*ip < '0' || *ip > '9')) ip++;
        strncpy(out, ip, size - 1);
        out[size - 1] = '\0';
      }
      n = 0;
    } else if (n < sizeof(line) - 1) {
      line[n++] = (char)c;
    }
  }
  drain(600);                       // let CWLIF finish before anything else
  return out[0] != '\0';
}

void setup() {
  Serial.begin(115200);
  Serial1.begin(ESP_BAUD);
  delay(400);

  Serial.println(F("\n=== can the ESP reach its own AP client? ==="));

  sendAT(F("AT+RST"), "ready", 10000);
  delay(1500);
  drain(1000);

  sendAT(F("ATE0"), "OK", 3000);
  sendAT(F("AT+CWMODE=3"), "OK", 4000);

  Serial1.print(F("AT+CWSAP=\""));
  Serial1.print(F(AP_SSID));
  Serial1.print(F("\",\""));
  Serial1.print(F(AP_PASS));
  Serial1.println(F("\",6,3"));
  if (!waitFor("OK", 10000)) { Serial.println(F("\n!! could not start the AP")); return; }
  drain(800);

  Serial.println(F("\n\nwaiting for the bulb to join (up to 3 minutes)"));
  Serial.println(F("it re-associates on its own -- give it time"));

  char ip[20];
  bool found = false;
  for (uint8_t i = 0; i < 18 && !found; i++) {
    delay(10000);
    Serial.print(F("\n\n-- check "));
    Serial.print(i + 1);
    Serial.println(F(" --"));
    found = findBulb(ip, sizeof(ip));
  }

  if (!found) {
    Serial.println(F("\n\nbulb never joined the AP -- nothing to test."));
    Serial.println(F("Re-provision it onto this network and try again."));
    return;
  }

  Serial.print(F("\n\nbulb is at "));
  Serial.println(ip);

  sendAT(F("AT+CIPMUX=1"), "OK", 4000);
  delay(1000);
  drain(600);

  // The single measurement. No close beforehand, so nothing else can be
  // mistaken for this command's reply.
  Serial.print(F("\n\nAT+CIPSTART to "));
  Serial.print(ip);
  Serial.println(F(":9999 -- this is the actual test\n"));

  Serial1.print(F("AT+CIPSTART="));
  Serial1.print(BULB_LINK);
  Serial1.print(F(",\"TCP\",\""));
  Serial1.print(ip);
  Serial1.print(F("\","));
  Serial1.println(BULB_PORT);

  const char *outcomes[4] = { "CONNECT", "ALREADY", "ERROR", "CLOSED" };
  const int which = waitAny(outcomes, 4, 12000);

  Serial.println();
  switch (which) {
    case 0: Serial.println(F("\n*** CONNECT -- the ESP CAN reach its own AP client.")); break;
    case 1: Serial.println(F("\n*** ALREADY CONNECTED -- link was open; rerun after a power cycle.")); return;
    case 2: Serial.println(F("\n*** ERROR -- refused. This is the SoftAP limit, confirmed.")); return;
    case 3: Serial.println(F("\n*** CLOSED -- opened then dropped immediately.")); return;
    default: Serial.println(F("\n*** no reply at all within 12s -- inconclusive.")); return;
  }

  drain(600);

  // Connected. Prove it carries real traffic by asking the bulb who it is.
  const uint16_t n = strlen(PROBE);
  Serial1.print(F("AT+CIPSEND="));
  Serial1.print(BULB_LINK);
  Serial1.print(',');
  Serial1.println(n + 4);
  if (!waitFor(">", 5000)) { Serial.println(F("\n!! no send prompt")); return; }

  Serial1.write((uint8_t)0); Serial1.write((uint8_t)0);
  Serial1.write((uint8_t)(n >> 8)); Serial1.write((uint8_t)(n & 0xFF));
  uint8_t key = 0xAB;
  for (uint16_t i = 0; i < n; i++) { key ^= (uint8_t)PROBE[i]; Serial1.write(key); }

  Serial.println(F("\n\nreply (decrypted):"));

  const unsigned long deadline = millis() + 10000;
  const char *tok = "+IPD,";
  uint8_t m = 0;
  int c;
  while (tok[m] && (long)(millis() - deadline) < 0) {
    if ((c = Serial1.read()) < 0) continue;
    if (c == tok[m]) m++; else m = (c == tok[0]) ? 1 : 0;
  }
  uint16_t len = 0;
  while ((long)(millis() - deadline) < 0) {
    if ((c = Serial1.read()) < 0) continue;
    if (c == ':') break;
    if (c >= '0' && c <= '9') len = len * 10 + (c - '0');
  }
  key = 0xAB;
  for (uint16_t i = 0; i < len; i++) {
    while ((c = Serial1.read()) < 0) if ((long)(millis() - deadline) >= 0) return;
    if (i < 4) continue;
    Serial.write((char)((uint8_t)c ^ key));
    key = (uint8_t)c;
  }

  Serial.println(F("\n\n*** the bulb answered. BULB_ENABLE 1 will work."));
}

void loop() {}
