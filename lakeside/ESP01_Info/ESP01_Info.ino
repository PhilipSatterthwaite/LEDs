// ESP-01 info dump.
//
// Runs the whole diagnostic sequence from code, sending an explicit "\r\n"
// after each command. Nothing here depends on the Serial Monitor's line-ending
// setting -- which is the usual reason a hand-typed AT comes back ERROR while
// the identical command works fine from a sketch. The ESP-AT parser requires
// BOTH characters; a lone \r or \n gives a clean echo and then ERROR.
//
// Monitor at 115200. Nothing to type -- it runs once at boot and stops.
//
// Power-cycle the ESP before uploading if it was last left mid-CIPSEND, since
// an interrupted send leaves it swallowing input as payload.

#define ESP_BAUD 115200

void runAT(const __FlashStringHelper *cmd, unsigned long wait) {
  Serial.println();
  Serial.print(F(">>> "));
  Serial.println(cmd);

  while (Serial1.available()) Serial1.read();   // drain anything stale

  Serial1.print(cmd);
  Serial1.print(F("\r\n"));

  // Wait for a real terminator, not an idle gap. A scan echoes the command
  // instantly and then thinks for several seconds, so an idle timeout cuts it
  // off before any results arrive.
  const char *ok = "OK\r\n";
  const char *er = "ERROR\r\n";
  uint8_t okM = 0, erM = 0;
  bool got = false;
  unsigned long start = millis();

  while (millis() - start < wait) {
    if (!Serial1.available()) continue;
    char c = Serial1.read();
    Serial.write(c);
    got = true;

    okM = (c == ok[okM]) ? okM + 1 : (c == ok[0] ? 1 : 0);
    erM = (c == er[erM]) ? erM + 1 : (c == er[0] ? 1 : 0);
    if (ok[okM] == '\0' || er[erM] == '\0') break;
  }

  if (!got) Serial.println(F("(no response)"));
}

void setup() {
  Serial.begin(115200);
  Serial1.begin(ESP_BAUD);
  delay(500);

  Serial.println(F("\n=== ESP-01 scan ==="));

  runAT(F("AT"), 3000);

  runAT(F("AT+CWMODE=3"), 5000);
  runAT(F("AT+CWLAP"),   25000);

  // AT 1.1.0.0 will sometimes return an empty list while its own AP is up.
  // Retry station-only, then put the AP back so the fallback keeps working.
  Serial.println(F("\n--- retry as station-only ---"));
  runAT(F("AT+CWMODE=1"), 5000);
  runAT(F("AT+CWLAP"),   25000);
  runAT(F("AT+CWMODE=3"), 5000);

  Serial.println(F("\n=== done ==="));
}

void loop() {}
