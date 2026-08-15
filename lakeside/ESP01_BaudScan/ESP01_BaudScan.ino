// ESP-01 baud scanner for the Mega.
// Cycles Serial1 (pins 18/19) through every rate an ESP-01 is likely to be
// using, sends "AT" at each one, and prints whatever comes back as both text
// and hex. The rate that answers OK is the module's baud -- put that number in
// ESP_BAUD in ESP01_Bridge.ino and 450LED_WiFi.ino.
//
// Monitor at 115200. If the banner below is itself garbage, the monitor's baud
// is wrong and nothing else here will make sense.
//
// No power-cycling needed: AT is answered any time the firmware is running.

const long BAUDS[] = { 9600, 19200, 38400, 57600, 74880, 115200, 230400 };
const uint8_t N_BAUDS = sizeof(BAUDS) / sizeof(BAUDS[0]);

void setup() {
  Serial.begin(115200);
  delay(200);
  Serial.println();
  Serial.println(F("=== ESP-01 baud scan ==="));
  Serial.println(F("If THIS line is unreadable, set the monitor to 115200."));
}

void loop() {
  for (uint8_t i = 0; i < N_BAUDS; i++) {
    long baud = BAUDS[i];

    Serial.print(F("\n--- "));
    Serial.print(baud);
    Serial.println(F(" ---"));

    Serial1.begin(baud);
    delay(120);
    while (Serial1.available()) Serial1.read();   // drain stale bytes

    Serial1.print(F("AT\r\n"));

    char buf[80];
    uint8_t n = 0;
    unsigned long start = millis();
    while (millis() - start < 600 && n < sizeof(buf) - 1) {
      if (Serial1.available()) buf[n++] = Serial1.read();
    }
    buf[n] = '\0';
    Serial1.end();

    if (n == 0) {
      Serial.println(F("(silence)"));
      continue;
    }

    Serial.print(F("text: "));
    for (uint8_t j = 0; j < n; j++) {
      uint8_t c = (uint8_t)buf[j];
      Serial.write((c >= 32 && c < 127) ? c : '.');
    }

    Serial.print(F("\nhex : "));
    for (uint8_t j = 0; j < n; j++) {
      uint8_t c = (uint8_t)buf[j];
      if (c < 16) Serial.print('0');
      Serial.print(c, HEX);
      Serial.print(' ');
    }
    Serial.println();

    if (strstr(buf, "OK")) {
      Serial.print(F(">>> MATCH -- the ESP is at "));
      Serial.println(baud);
    }
  }

  Serial.println(F("\n=== scan complete, repeating in 5s ==="));
  delay(5000);
}
