// ESP-01 bring-up bridge for the Mega.
// Passes USB serial <-> Serial1 (pins 18/19) so you can watch the module boot
// and type AT commands at it by hand. Upload this before 450LED_WiFi.ino.
//
// Wire both pins (the adapter's Q1/Q2 level shifter makes that safe), then
// work through these two steps -- they isolate the two signal directions
// without unplugging anything.
//
// STEP 1 -- tests ESP TXD -> pin 19, and power / ground / CH_PD / baud.
//   Open the monitor at 115200 and power-cycle the ESP.
//   Expect a burst of garbage, then a readable "ready".
//   The garbage is normal: the ESP8266 boot ROM prints at 74880 baud, which
//   is unreadable at 115200. The AT firmware that follows is at 115200.
//   Garbage that never resolves = baud mismatch; try ESP_BAUD 9600 or 57600.
//   Nothing at all = power, swapped TX/RX, or CH_PD low.
//
// STEP 2 -- tests pin 18 -> ESP RXD.
//   Set the monitor's line ending to "Both NL & CR", type AT, expect OK.
//   Then AT+GMR to print the firmware version.
//
// Uploading does not disturb any of this: the Mega flashes over pins 0/1,
// so the ESP can stay wired to Serial1 the whole time.

#define ESP_BAUD    115200
#define DEBUG_BAUD  115200

void setup() {
  Serial.begin(DEBUG_BAUD);
  Serial1.begin(ESP_BAUD);
  Serial.println(F("bridge up -- power-cycle the ESP now"));
}

void loop() {
  while (Serial1.available()) Serial.write(Serial1.read());
  while (Serial.available())  Serial1.write(Serial.read());
}
