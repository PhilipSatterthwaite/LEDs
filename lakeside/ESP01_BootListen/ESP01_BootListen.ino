// ESP-01 boot listener.
//
// The ESP8266 boot ROM always prints at 74880 baud on power-up, regardless of
// what the AT firmware's baud is set to. Listening at exactly that rate turns
// the power-up message into READABLE text, which definitively proves:
//   - the ESP is powered and actually running
//   - CH_PD is high (it would not boot otherwise)
//   - its TXD reaches Mega pin 19
//
// Crucially it tests none of the Mega -> ESP direction, so it separates
// "module is dead" from "module can't hear me" -- the exact split left after
// a baud scan comes back silent at every rate.
//
// Monitor at 115200, then power-cycle the ESP (pull VCC and reseat).
//
// Expect something close to:
//   ets Jan  8 2013,rst cause:2, boot mode:(3,7)
//   load 0x4010f000, len 1384, room 16
//   tail 8
//   chksum 0x2d
//
// READABLE text  -> module is fine; the fault is the Mega -> ESP RX path.
//                   Most likely TXD/RXD swapped at the adapter.
// STILL GARBAGE  -> bytes arriving but misframed; suspect a marginal or
//                   floating line rather than baud.
// SILENCE        -> not booting at all; check 3.3V at the header and CH_PD.

void setup() {
  Serial.begin(115200);
  Serial1.begin(74880);
  Serial.println(F("listening at 74880 -- power-cycle the ESP now"));
}

void loop() {
  while (Serial1.available()) Serial.write(Serial1.read());
}
