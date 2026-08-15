#include <IRremote.h>

// Define the IR receiver pin
const int IR_PIN = 2;

// Define the IR receiver object
IRrecv irrecv(IR_PIN);

void setup() {
  Serial.begin(9600);
  irrecv.enableIRIn();
}

void loop() {
  // Check if the IR receiver has received a signal
  if (irrecv.decode()) {
    // Print the HEX value of the button press
    Serial.println(irrecv.decodedIRData.decodedRawData, HEX);
    
    // Reset the IR receiver for the next signal
    irrecv.resume();
  }
}

/*
Power: BF40FF00
Brightness up: A35CFF00
Brightness down: A25DFF00
Red: A758FF00
Green: A659FF00
Blue: BA45FF00
White: BB44FF00
Orange: AB54FF00
Less orange: AF50FF00
yellow orange: E31CFF00
yellow: E718FF00

light green: AA55FF00
light blue: AA55FF00
aqua: E21DFF00
teal: E619FF00

lighter blue: B649FF00
purple: B24DFF00
magenta: E11EFF00
pink: E51AFF00

warm 1: B748FF00
warm 2: B34CFF00
cold 1: E01FFF00
cold 2: E41BFF00

Red up: EB14FF00
red down: EF10FF00
green up: EA15FF00
green down: EE11FF00
blue up: E916FF00
blue down: ED12FF00

quick: E817FF00
slow: EC13FF00
DIY1: F30CFF00
DIY2: F20DFF00
DIY3: F10EFF00
DIY4: F708FF00
DIY5: F609FF00
DIY6: F50AFF00

Auto: F00FFF00
Flash: F40BFF00
Jump3: FB04FF00
Jump7: FA05FF00
Fade3: F906FF00
Fade7: F807FF00

*/